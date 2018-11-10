/* Copyright (c) 2017 Mozilla */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include "rnnoise.h"
#define FRAME_SIZE 480
#define FRAME_SIZE_SHIFT 2
#define FRAME_SIZE (120<<FRAME_SIZE_SHIFT)
#define WINDOW_SIZE (2*FRAME_SIZE)
#define FREQ_SIZE (FRAME_SIZE + 1)

#define PITCH_MIN_PERIOD 60
#define PITCH_MAX_PERIOD 768
#define PITCH_FRAME_SIZE 960
#define PITCH_BUF_SIZE (PITCH_MAX_PERIOD+PITCH_FRAME_SIZE)

#define SQUARE(x) ((x)*(x))

#define SMOOTH_BANDS 1

#if SMOOTH_BANDS
#define NB_BANDS 22
#else
#define NB_BANDS 21
#endif

#define CEPS_MEM 8
#define NB_DELTA_CEPS 6

#define NB_FEATURES (NB_BANDS+3*NB_DELTA_CEPS+2)

int lowpass = FREQ_SIZE;
int band_lp = NB_BANDS;
static const opus_int16 eband5ms[] = {
/*0  200 400 600 800  1k 1.2 1.4 1.6  2k 2.4 2.8 3.2  4k 4.8 5.6 6.8  8k 9.6 12k 15.6 20k*/
  0,  1,  2,  3,  4,  5,  6,  7,  8, 10, 12, 14, 16, 20, 24, 28, 34, 40, 48, 60, 78, 100
};

int main() {
  int i;
  static const float a_hp[2] = {-1.99599, 0.99600}; 
  static const float b_hp[2] = {-2, 1};
  float a_noise[2] = {0};
  float b_noise[2] = {0};
  float a_sig[2] = {0};
  float b_sig[2] = {0};
  float mem_hp_x[2]={0};
  float mem_hp_n[2]={0};
  float mem_resp_x[2]={0};
  float mem_resp_n[2]={0};  

  float x[FRAME_SIZE];
  float n[FRAME_SIZE];
  float xn[FRAME_SIZE];
  int vad_cnt=0;
  int lowpass = FREQ_SIZE; // ??
  int band_lp = NB_BANDS;
  int gain_change = 0;


  FILE *f1, *f2, *fout, *allname;
  DenoiseState *st, *noise_state, *noisy;
  st = rnnoise_create();
  noise_state = rnnoise_create();
  noisy = rnnoise_create();

  allname = fopen("/home/rleite/study_materials/srt/codelab/rnnoise-master/examples/doc.txt", "r");
  char fnm[21];
  int count = 0;
  int init_flag = 0;

  f2 = fopen("/home/rleite/study_materials/srt/codelab/rnnoise-master/noise/noise3-train.raw", "r");
  fout = fopen("/home/rleite/study_materials/srt/codelab/rnnoise-master/noise/features.raw", "a");

  while (1) {
    kiss_fft_cpx X[FREQ_SIZE], Y[FREQ_SIZE], N[FREQ_SIZE], P[WINDOW_SIZE];
    float Ex[NB_BANDS], Ey[NB_BANDS], En[NB_BANDS], Ep[NB_BANDS];
    float Exp[NB_BANDS];
    float Ln[NB_BANDS];
    float features[NB_FEATURES];
    float g[NB_BANDS];
    //float gf[FREQ_SIZE]={1};
    short tmp[FRAME_SIZE];
    float vad=0;
    //float vad_prob;
    float E=0;

    float speech_gain;
    float noise_gain;

    if(!init_flag || ++gain_change>=50) {
      // Various SNR and biquad coefficents.  -30dB -- 20dB
      speech_gain = pow(10., (-40+(rand()%60))/20.);
      noise_gain = pow(10., (-30+(rand()%50))/20.);
      if (rand()%10==0) noise_gain = 0;
      noise_gain *= speech_gain;
      if (rand()%10==0) speech_gain = 0;
      rand_resp(a_noise, b_noise);
      rand_resp(a_sig, b_sig);
      lowpass = FREQ_SIZE * 3000./24000. * pow(50., rand()/(double)RAND_MAX); // What's "lowpass"
      for (i=0;i<NB_BANDS;i++) {
        if (eband5ms[i]<<FRAME_SIZE_SHIFT > lowpass) {
          band_lp = i;
          break;
        }
      }
      gain_change = 0;
      init_flag = 1;
    }
    
    fgets(fnm,21,allname);
    fseek(allname,1,SEEK_CUR);
    f1 = fopen(fnm, "r");
    count++;
    if(count==1445) break;
 
    while(1) {

      if (speech_gain) {
        fread(tmp, sizeof(short), FRAME_SIZE, f1);
        if(feof(f1)) break;
        for (i=0;i<FRAME_SIZE;i++) x[i] = speech_gain*tmp[i]; // What's "speech_gain"?
        for (i=0;i<FRAME_SIZE;i++) E += tmp[i]*(float)tmp[i];      
      }
      else {
        for (i=0;i<FRAME_SIZE;i++) x[i] = 0;
        E = 0;
      }
    

      if (noise_gain) {
        fread(tmp, sizeof(short), FRAME_SIZE, f2);
        if (feof(f2)) {rewind(f2);break;}
        for (i=0;i<FRAME_SIZE;i++) n[i] = noise_gain*tmp[i]; // What's "noise_gain"?
      }
      else {
        for (i=0;i<FRAME_SIZE;i++) n[i] = 0;
      }
      
      biquad(x, mem_hp_x, x, b_hp, a_hp, FRAME_SIZE);
      biquad(x, mem_resp_x, x, b_sig, a_sig, FRAME_SIZE);
      biquad(n, mem_hp_n, n, b_hp, a_hp, FRAME_SIZE);
      biquad(n, mem_resp_n, n, b_noise, a_noise, FRAME_SIZE);
      for (i=0;i<FRAME_SIZE;i++) xn[i] = x[i] + n[i];

      if (E > 1e9f) {
        vad_cnt=0;
      } else if (E > 1e8f) {
        vad_cnt -= 5;
      } else if (E > 1e7f) {
        vad_cnt++;
      } else {
        vad_cnt+=2;
      }
      if (vad_cnt < 0) vad_cnt = 0;
      if (vad_cnt > 15) vad_cnt = 15;

      if (vad_cnt >= 10) vad = 0;
      else if (vad_cnt > 0) vad = 0.5f;
      else vad = 1.f;

      frame_analysis(st, Y, Ey, x);  // frame_analysis is invoked within compute_frame_features??
                                     // Ey Y -- clean speech
      frame_analysis(noise_state, N, En, n); // only noise
      for (i=0;i<NB_BANDS;i++) Ln[i] = log10(1e-2+En[i]); // log (Noisy Energy) ?? Use for various SNR??
      int silence = compute_frame_features(noisy, X, P, Ex, Ep, Exp, features, xn);
      pitch_filter(X, P, Ex, Ep, Exp, g);
      //printf("%f %d\n", noisy->last_gain, noisy->last_period);
      for (i=0;i<NB_BANDS;i++) {
        g[i] = sqrt((Ey[i]+1e-3)/(Ex[i]+1e-3));
        if (g[i] > 1) g[i] = 1;
        if (silence || i > band_lp) g[i] = -1;
        if (Ey[i] < 5e-2 && Ex[i] < 5e-2) g[i] = -1;
        if (vad==0 && noise_gain==0) g[i] = -1;
      }
      fwrite(features, sizeof(float), NB_FEATURES, fout);
      fwrite(g, sizeof(float), NB_BANDS, fout);
      fwrite(Ln, sizeof(float), NB_BANDS, fout);
      fwrite(&vad, sizeof(float), 1, fout); 
      
    }
    fclose(f1);
  } 
  rnnoise_destroy(st);

  fclose(f2);
  fclose(fout);
  fclose(allname);
  return 0;
}
