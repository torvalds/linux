#ifndef __SII9233A_INTERFACE_H__
#define __SII9233A_INTERFACE_H__

#include "sii9233_drv.h"

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a hardware power reset

void sii_hardware_reset(sii9233a_info_t *info);
int sii_get_pwr5v_status(void);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a input hdmi port

char sii_get_hdmi_port(void);
void sii_set_hdmi_port(char port);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a output signal horizontal parameters

int sii_get_h_active(void);
int sii_get_h_total(void);
int sii_get_hs_width(void);
int sii_get_hs_frontporch(void);
int sii_get_hs_backporch(void);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a output signal vertical parameters

int sii_get_v_active(void);
int sii_get_v_total(void);
int sii_get_hs_width(void);
int sii_get_vs_to_de(void);
int sii_get_vs_frontporch(void);
int sii_get_vs_backporch(void);
int sii_get_video_pixel_clock(void);
int sii_get_frame_rate(void);
void dump_input_video_info(void);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a audio realated api

int sii_is_hdmi_mode(void);
int sii_get_audio_sampling_freq(void);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// sii9233a general status
int sii_get_chip_id(void);

#endif

