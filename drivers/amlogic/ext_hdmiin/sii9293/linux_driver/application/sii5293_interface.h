#ifndef __SII5293_INTERFACE_H__
#define __SII5293_INTERFACE_H__

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii5293 standby or not

void sii_set_standby(int bStandby);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii5293 output signal horizontal parameters

int sii_get_h_active(void);
int sii_get_h_total(void);
int sii_get_hs_width(void);
int sii_get_hs_frontporch(void);
int sii_get_hs_backporch(void);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii5293 output signal vertical parameters

int sii_get_v_active(void);
int sii_get_v_total(void);
int sii_get_vs_width(void);
int sii_get_vs_frontporch(void);
int sii_get_vs_backporch(void);
int sii_get_vs_to_de(void);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii5293 output signal clock parameters

int sii_get_pixel_clock(void);
int sii_get_h_freq(void);
int sii_get_v_freq(void);
int sii_get_interlaced(void);

int sii_get_pwr5v_status(void);
int sii_get_audio_sampling_freq(void);

#endif

