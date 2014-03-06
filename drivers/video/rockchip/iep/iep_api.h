#ifndef __IEP_API_H__
#define __IEP_API_H__


#include "iep.h"
//#include "../lcdc/hwapi_lcdc.h"

void
iep_set_act_info(iep_img *img, unsigned int w, unsigned int h, unsigned int x_off, unsigned int y_off);

void
iep_set_vir_info(iep_img *img, unsigned int w, unsigned int h, unsigned int *yrgb, unsigned int *uv, unsigned int *v, unsigned int format );

void 
iep_set_scl_up_mode(IEP_MSG *msg, unsigned char mode);

void
iep_set_color_enhance(IEP_MSG *msg, unsigned char color_enh_en,float color_enh_coe);

void
iep_rgb_cg(IEP_MSG *msg,unsigned char cg_en,double cg_rr,double cg_rg,double cg_rb);

void
iep_set_deinterlace(IEP_MSG *msg, unsigned char mode, unsigned char dein_high_fre_en, unsigned char dein_edge_interp_en);

void
iep_set_dil_ei_smooth(IEP_MSG *msg,unsigned int en);

void
iep_set_rgb2yuv(IEP_MSG *msg, unsigned char rgb2yuv_mode, unsigned char rgb2yuv_clip_en);

void
iep_set_yuv2rgb(IEP_MSG *msg, unsigned char yuv2rgb_mode, unsigned char yuv2rgb_clip_en);

void
iep_set_dither_up(IEP_MSG *msg,unsigned int en);

void
iep_set_lcdc_path(IEP_MSG *msg);

void
iep_set_3D_denoise(IEP_MSG *msg);

void
iep_set_yuv_normal_mode_enh(IEP_MSG *msg,float saturation,float contrast,signed char brightness,signed char angle);

void
iep_set_yuv_black_screen(IEP_MSG *msg);

void
iep_set_yuv_blue_screen(IEP_MSG *msg);

void
iep_set_yuv_color_bar(IEP_MSG *msg,unsigned char color_bar_y,unsigned char color_bar_u,unsigned char color_bar_v);

#endif
