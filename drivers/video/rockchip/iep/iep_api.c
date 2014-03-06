#include "iep_api.h"



void
iep_set_act_info(iep_img *img, unsigned int w, unsigned int h, unsigned int x_off, unsigned int y_off)
{
    img->act_w = w;
    img->act_h = h;
    img->x_off = x_off;
    img->y_off = y_off;
}

void
iep_set_vir_info(iep_img *img, unsigned int w, unsigned int h, unsigned int *yrgb, unsigned int *uv, unsigned int *v, unsigned int format )
{
    img->vir_w = w;
    img->vir_h = h;
    img->mem_addr = yrgb;
    img->uv_addr = uv;
    img->v_addr = v;
    img->format = format;
}

void 
iep_set_scl_up_mode(IEP_MSG *msg, unsigned char mode)
{
    msg->scale_up_mode = mode;
}

void
iep_set_color_enhance(IEP_MSG *msg, unsigned char color_enh_en,float color_enh_coe)
{
    msg->rgb_color_enhance_en = color_enh_en;
    msg->rgb_enh_coe = color_enh_coe;

}

void
iep_rgb_cg(IEP_MSG *msg,unsigned char cg_en,double cg_rr,double cg_rg,double cg_rb)
{
    msg->rgb_cg_en=cg_en;
    msg->cg_rr=cg_rr;
    msg->cg_rg=cg_rg;
    msg->cg_rb=cg_rb;
}

void
iep_set_deinterlace(IEP_MSG *msg, unsigned char mode, unsigned char dein_high_fre_en, unsigned char dein_edge_interp_en)
{
    msg->dein_mode = mode;
    msg->dein_high_fre_en = dein_high_fre_en;
    msg->dein_ei_mode = dein_edge_interp_en;    
}
void
iep_set_dil_ei_smooth(IEP_MSG *msg,unsigned int en)
{
	msg->dein_ei_smooth = en;
}
void
iep_set_dil_ei(IEP_MSG *msg,unsigned char ei_sel,unsigned char ei_radius,unsigned char ei_smooth_en,unsigned char ei_mode)
{
	msg->dein_ei_sel=ei_sel;
	msg->dein_ei_radius=ei_radius;
	msg->dein_ei_smooth=ei_smooth_en;
	msg->dein_ei_mode=ei_mode;
}
void
iep_set_dil_hf(IEP_MSG *msg,unsigned char dil_hf_en,unsigned char dil_hf_fct)
{
	msg->dein_high_fre_en=dil_hf_en;
	msg->dein_high_fre_fct=dil_hf_fct;
}

void
iep_set_rgb2yuv(IEP_MSG *msg, unsigned char rgb2yuv_mode, unsigned char rgb2yuv_clip_en)
{
    msg->rgb2yuv_mode = rgb2yuv_mode;
    msg->rgb2yuv_clip_en = rgb2yuv_clip_en; 
}

void
iep_set_yuv2rgb(IEP_MSG *msg, unsigned char yuv2rgb_mode, unsigned char yuv2rgb_clip_en)
{
    msg->yuv2rgb_mode = yuv2rgb_mode;
    msg->yuv2rgb_clip_en = yuv2rgb_clip_en; 
}

void
iep_set_dither_up(IEP_MSG *msg,unsigned int en)
{
	msg->dither_up_en = en;
}

void
iep_set_lcdc_path(IEP_MSG *msg)
{
	msg->lcdc_path_en = 1;
}

void
iep_set_3D_denoise(IEP_MSG *msg)
{
	msg->yuv_3D_denoise_en = 1;
}

void
iep_set_yuv_normal_mode_enh(IEP_MSG *msg,float saturation,float contrast,signed char brightness,signed char angle)
{
	msg->yuv_enhance_en = 1;
	msg->video_mode = normal_mode;
	msg->yuv_enh_saturation = saturation;
	msg->yuv_enh_contrast = contrast;
	msg->yuv_enh_brightness = brightness;
	msg->yuv_enh_hue_angle = angle;
}

void
iep_set_yuv_black_screen(IEP_MSG *msg)
{
	msg->yuv_enhance_en = 1;
	msg->video_mode = black_screen;
}

void
iep_set_yuv_blue_screen(IEP_MSG *msg)
{
	msg->yuv_enhance_en = 1;
	msg->video_mode = blue_screen;
}

void
iep_set_yuv_color_bar(IEP_MSG *msg,unsigned char color_bar_y,unsigned char color_bar_u,unsigned char color_bar_v)
{
	msg->yuv_enhance_en = 1;
	msg->video_mode = color_bar;
	msg->color_bar_y = color_bar_y;
	msg->color_bar_u = color_bar_u;
	msg->color_bar_v = color_bar_v;
}



