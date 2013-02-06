/* linux/drivers/media/video/samsung/tvout/hw_if/mixer.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Mixer raw ftn  file for Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/sched.h>

#include <mach/regs-mixer.h>

#include "../s5p_tvout_common_lib.h"
#include "../s5p_tvout_ctrl.h"
#include "hw_if.h"

#undef tvout_dbg

#ifdef CONFIG_MIXER_DEBUG
#define tvout_dbg(fmt, ...)					\
		printk(KERN_INFO "\t[MIXER] %s(): " fmt,	\
			__func__, ##__VA_ARGS__)
#else
#define tvout_dbg(fmt, ...)
#endif

void __iomem	*mixer_base;
spinlock_t	lock_mixer;


extern int s5p_vp_ctrl_get_src_addr(u32* top_y_addr, u32* top_c_addr);
int s5p_mixer_set_show(enum s5p_mixer_layer layer, bool show)
{
	u32 mxr_config;

	tvout_dbg("%d, %d\n", layer, show);

	switch (layer) {
	case MIXER_VIDEO_LAYER:
		mxr_config = (show) ?
				(readl(mixer_base + S5P_MXR_CFG) |
					S5P_MXR_CFG_VIDEO_ENABLE) :
				(readl(mixer_base + S5P_MXR_CFG) &
					~S5P_MXR_CFG_VIDEO_ENABLE);
		break;

	case MIXER_GPR0_LAYER:
		mxr_config = (show) ?
				(readl(mixer_base + S5P_MXR_CFG) |
					S5P_MXR_CFG_GRAPHIC0_ENABLE) :
				(readl(mixer_base + S5P_MXR_CFG) &
					~S5P_MXR_CFG_GRAPHIC0_ENABLE);
		break;

	case MIXER_GPR1_LAYER:
		mxr_config = (show) ?
				(readl(mixer_base + S5P_MXR_CFG) |
					S5P_MXR_CFG_GRAPHIC1_ENABLE) :
				(readl(mixer_base + S5P_MXR_CFG) &
					~S5P_MXR_CFG_GRAPHIC1_ENABLE);
		break;

	default:
		tvout_err("invalid layer parameter = %d\n", layer);
		return -1;
	}

	writel(mxr_config, mixer_base + S5P_MXR_CFG);

	return 0;
}

int s5p_mixer_set_priority(enum s5p_mixer_layer layer, u32 priority)
{
	u32 layer_cfg;

	tvout_dbg("%d, %d\n", layer, priority);

	switch (layer) {
	case MIXER_VIDEO_LAYER:
		layer_cfg = S5P_MXR_LAYER_CFG_VID_PRIORITY_CLR(
				readl(mixer_base + S5P_MXR_LAYER_CFG)) |
				S5P_MXR_LAYER_CFG_VID_PRIORITY(priority);
		break;

	case MIXER_GPR0_LAYER:
		layer_cfg = S5P_MXR_LAYER_CFG_GRP0_PRIORITY_CLR(
				readl(mixer_base + S5P_MXR_LAYER_CFG)) |
				S5P_MXR_LAYER_CFG_GRP0_PRIORITY(priority);
		break;

	case MIXER_GPR1_LAYER:
		layer_cfg = S5P_MXR_LAYER_CFG_GRP1_PRIORITY_CLR(
				readl(mixer_base + S5P_MXR_LAYER_CFG)) |
				S5P_MXR_LAYER_CFG_GRP1_PRIORITY(priority);
		break;

	default:
		tvout_err("invalid layer parameter = %d\n", layer);
		return -1;
	}

	writel(layer_cfg, mixer_base + S5P_MXR_LAYER_CFG);

	return 0;
}

void s5p_mixer_set_pre_mul_mode(enum s5p_mixer_layer layer, bool enable)
{
	u32 reg;

	switch (layer) {
	case MIXER_GPR0_LAYER:
		reg = readl(mixer_base + S5P_MXR_GRAPHIC0_CFG);

		if (enable)
			reg |= S5P_MXR_PRE_MUL_MODE;
		else
			reg &= ~S5P_MXR_PRE_MUL_MODE;

		writel(reg, mixer_base + S5P_MXR_GRAPHIC0_CFG);
		break;
	case MIXER_GPR1_LAYER:
		reg = readl(mixer_base + S5P_MXR_GRAPHIC1_CFG);

		if (enable)
			reg |= S5P_MXR_PRE_MUL_MODE;
		else
			reg &= ~S5P_MXR_PRE_MUL_MODE;

		writel(reg, mixer_base + S5P_MXR_GRAPHIC1_CFG);
		break;
	case MIXER_VIDEO_LAYER:
		break;
	}
}

int s5p_mixer_set_pixel_blend(enum s5p_mixer_layer layer, bool enable)
{
	u32 temp_reg;

	tvout_dbg("%d, %d\n", layer, enable);

	switch (layer) {
	case MIXER_GPR0_LAYER:
		temp_reg = readl(mixer_base + S5P_MXR_GRAPHIC0_CFG)
			& (~S5P_MXR_PIXEL_BLEND_ENABLE) ;

		if (enable)
			temp_reg |= S5P_MXR_PIXEL_BLEND_ENABLE;
		else
			temp_reg |= S5P_MXR_PIXEL_BLEND_DISABLE;

		writel(temp_reg, mixer_base + S5P_MXR_GRAPHIC0_CFG);
		break;

	case MIXER_GPR1_LAYER:
		temp_reg = readl(mixer_base + S5P_MXR_GRAPHIC1_CFG)
			& (~S5P_MXR_PIXEL_BLEND_ENABLE) ;

		if (enable)
			temp_reg |= S5P_MXR_PIXEL_BLEND_ENABLE;
		else
			temp_reg |= S5P_MXR_PIXEL_BLEND_DISABLE;

		writel(temp_reg, mixer_base + S5P_MXR_GRAPHIC1_CFG);
		break;

	default:
		tvout_err("invalid layer parameter = %d\n", layer);

		return -1;
	}

	return 0;
}

int s5p_mixer_set_layer_blend(enum s5p_mixer_layer layer, bool enable)
{
	u32 temp_reg;

	tvout_dbg("%d, %d\n", layer, enable);

	switch (layer) {
	case MIXER_VIDEO_LAYER:
		temp_reg = readl(mixer_base + S5P_MXR_VIDEO_CFG)
			   & (~S5P_MXR_VIDEO_CFG_BLEND_EN) ;

		if (enable)
			temp_reg |= S5P_MXR_VIDEO_CFG_BLEND_EN;
		else
			temp_reg |= S5P_MXR_VIDEO_CFG_BLEND_DIS;

		writel(temp_reg, mixer_base + S5P_MXR_VIDEO_CFG);
		break;

	case MIXER_GPR0_LAYER:
		temp_reg = readl(mixer_base + S5P_MXR_GRAPHIC0_CFG)
			   & (~S5P_MXR_WIN_BLEND_ENABLE) ;

		if (enable)
			temp_reg |= S5P_MXR_WIN_BLEND_ENABLE;
		else
			temp_reg |= S5P_MXR_WIN_BLEND_DISABLE;

		writel(temp_reg, mixer_base + S5P_MXR_GRAPHIC0_CFG);
		break;

	case MIXER_GPR1_LAYER:
		temp_reg = readl(mixer_base + S5P_MXR_GRAPHIC1_CFG)
			   & (~S5P_MXR_WIN_BLEND_ENABLE) ;

		if (enable)
			temp_reg |= S5P_MXR_WIN_BLEND_ENABLE;
		else
			temp_reg |= S5P_MXR_WIN_BLEND_DISABLE;

		writel(temp_reg, mixer_base + S5P_MXR_GRAPHIC1_CFG);
		break;

	default:
		tvout_err("invalid layer parameter = %d\n", layer);

		return -1;
	}

	return 0;
}

int s5p_mixer_set_alpha(enum s5p_mixer_layer layer, u32 alpha)
{
	u32 temp_reg;

	tvout_dbg("%d, %d\n", layer, alpha);

	switch (layer) {
	case MIXER_VIDEO_LAYER:
		temp_reg = readl(mixer_base + S5P_MXR_VIDEO_CFG)
			   & (~S5P_MXR_VIDEO_CFG_ALPHA_MASK) ;
		temp_reg |= S5P_MXR_VIDEO_CFG_ALPHA_VALUE(alpha);
		writel(temp_reg, mixer_base + S5P_MXR_VIDEO_CFG);
		break;

	case MIXER_GPR0_LAYER:
		temp_reg = readl(mixer_base + S5P_MXR_GRAPHIC0_CFG)
			   & (~S5P_MXR_VIDEO_CFG_ALPHA_MASK) ;
		temp_reg |= S5P_MXR_GRP_ALPHA_VALUE(alpha);
		writel(temp_reg, mixer_base + S5P_MXR_GRAPHIC0_CFG);
		break;

	case MIXER_GPR1_LAYER:
		temp_reg = readl(mixer_base + S5P_MXR_GRAPHIC1_CFG)
			   & (~S5P_MXR_VIDEO_CFG_ALPHA_MASK) ;
		temp_reg |= S5P_MXR_GRP_ALPHA_VALUE(alpha);
		writel(temp_reg, mixer_base + S5P_MXR_GRAPHIC1_CFG);
		break;

	default:
		tvout_err("invalid layer parameter = %d\n", layer);
		return -1;
	}

	return 0;
}

int s5p_mixer_set_grp_base_address(enum s5p_mixer_layer layer, u32 base_addr)
{
	tvout_dbg("%d, 0x%x\n", layer, base_addr);

	if (S5P_MXR_GRP_ADDR_ILLEGAL(base_addr)) {
		tvout_err("address is not word align = %d\n", base_addr);
		return -1;
	}

	switch (layer) {
	case MIXER_GPR0_LAYER:
		writel(S5P_MXR_GPR_BASE(base_addr),
			mixer_base + S5P_MXR_GRAPHIC0_BASE);
		break;

	case MIXER_GPR1_LAYER:
		writel(S5P_MXR_GPR_BASE(base_addr),
			mixer_base + S5P_MXR_GRAPHIC1_BASE);
		break;

	default:
		tvout_err("invalid layer parameter = %d\n", layer);
		return -1;
	}

	return 0;
}

int s5p_mixer_set_grp_layer_dst_pos(enum s5p_mixer_layer layer,
					u32 dst_offs_x, u32 dst_offs_y)
{
	tvout_dbg("%d, %d, %d\n", layer, dst_offs_x, dst_offs_y);

	switch (layer) {
	case MIXER_GPR0_LAYER:
		writel(S5P_MXR_GRP_DESTX(dst_offs_x) |
			S5P_MXR_GRP_DESTY(dst_offs_y),
			mixer_base + S5P_MXR_GRAPHIC0_DXY);
		break;

	case MIXER_GPR1_LAYER:
		writel(S5P_MXR_GRP_DESTX(dst_offs_x) |
			S5P_MXR_GRP_DESTY(dst_offs_y),
			mixer_base + S5P_MXR_GRAPHIC1_DXY);
		break;

	default:
		tvout_err("invalid layer parameter = %d\n", layer);
		return -1;
	}

	return 0;
}

int s5p_mixer_set_grp_layer_src_pos(enum s5p_mixer_layer layer, u32 src_offs_x,
			u32 src_offs_y, u32 span, u32 width, u32 height)
{
	tvout_dbg("%d, %d, %d, %d, %d, %d\n", layer, span, width, height,
		 src_offs_x, src_offs_y);

	switch (layer) {
	case MIXER_GPR0_LAYER:
		writel(S5P_MXR_GRP_SPAN(span),
			mixer_base + S5P_MXR_GRAPHIC0_SPAN);
		writel(S5P_MXR_GRP_WIDTH(width) | S5P_MXR_GRP_HEIGHT(height),
		       mixer_base + S5P_MXR_GRAPHIC0_WH);
		writel(S5P_MXR_GRP_STARTX(src_offs_x) |
			S5P_MXR_GRP_STARTY(src_offs_y),
		       mixer_base + S5P_MXR_GRAPHIC0_SXY);
		break;

	case MIXER_GPR1_LAYER:
		writel(S5P_MXR_GRP_SPAN(span),
			mixer_base + S5P_MXR_GRAPHIC1_SPAN);
		writel(S5P_MXR_GRP_WIDTH(width) | S5P_MXR_GRP_HEIGHT(height),
		       mixer_base + S5P_MXR_GRAPHIC1_WH);
		writel(S5P_MXR_GRP_STARTX(src_offs_x) |
			S5P_MXR_GRP_STARTY(src_offs_y),
		       mixer_base + S5P_MXR_GRAPHIC1_SXY);
		break;

	default:
		tvout_err(" invalid layer parameter = %d\n", layer);
		return -1;
	}

	return 0;
}

void s5p_mixer_set_bg_color(enum s5p_mixer_bg_color_num colornum,
				u32 color_y, u32 color_cb, u32 color_cr)
{
	u32 reg_value;

	reg_value = S5P_MXR_BG_COLOR_Y(color_y) |
			S5P_MXR_BG_COLOR_CB(color_cb) |
			S5P_MXR_BG_COLOR_CR(color_cr);

	switch (colornum) {
	case MIXER_BG_COLOR_0:
		writel(reg_value, mixer_base + S5P_MXR_BG_COLOR0);
		break;

	case MIXER_BG_COLOR_1:
		writel(reg_value, mixer_base + S5P_MXR_BG_COLOR1);
		break;

	case MIXER_BG_COLOR_2:
		writel(reg_value, mixer_base + S5P_MXR_BG_COLOR2);
		break;
	}
}
void s5p_mixer_set_video_limiter(u32 y_min, u32 y_max,
		u32 c_min, u32 c_max, bool enable)
{
	u32 reg_value;

	reg_value = readl(mixer_base + S5P_MXR_VIDEO_CFG)
		& (~S5P_MXR_VIDEO_CFG_LIMITER_EN) ;

	if (enable)
		reg_value |= S5P_MXR_VIDEO_CFG_LIMITER_EN;
	else
		reg_value |= S5P_MXR_VIDEO_CFG_LIMITER_DIS;

	writel(reg_value, mixer_base + S5P_MXR_VIDEO_CFG);

	reg_value = S5P_MXR_VIDEO_LIMITER_PARA_Y_UPPER(y_max) |
		S5P_MXR_VIDEO_LIMITER_PARA_Y_LOWER(y_min) |
		S5P_MXR_VIDEO_LIMITER_PARA_C_UPPER(c_max) |
		S5P_MXR_VIDEO_LIMITER_PARA_C_LOWER(c_min);

	writel(reg_value, mixer_base + S5P_MXR_VIDEO_LIMITER_PARA_CFG);

}

void s5p_mixer_init_status_reg(enum s5p_mixer_burst_mode burst,
				enum s5p_tvout_endian endian)
{
	u32 temp_reg = 0;

	temp_reg = S5P_MXR_STATUS_SYNC_ENABLE | S5P_MXR_STATUS_OPERATING;

	switch (burst) {
	case MIXER_BURST_8:
		temp_reg |= S5P_MXR_STATUS_8_BURST;
		break;
	case MIXER_BURST_16:
		temp_reg |= S5P_MXR_STATUS_16_BURST;
		break;
	}

	switch (endian) {
	case TVOUT_BIG_ENDIAN:
		temp_reg |= S5P_MXR_STATUS_BIG_ENDIAN;
		break;
	case TVOUT_LITTLE_ENDIAN:
		temp_reg |= S5P_MXR_STATUS_LITTLE_ENDIAN;
		break;
	}

	writel(temp_reg, mixer_base + S5P_MXR_STATUS);
}

int s5p_mixer_init_display_mode(enum s5p_tvout_disp_mode mode,
				enum s5p_tvout_o_mode output_mode,
				enum s5p_mixer_rgb rgb_type)
{
	u32 temp_reg = readl(mixer_base + S5P_MXR_CFG);

	tvout_dbg("%d, %d\n", mode, output_mode);

	switch (mode) {
	case TVOUT_NTSC_M:
	case TVOUT_NTSC_443:
		temp_reg &= ~S5P_MXR_CFG_HD;
		temp_reg &= ~S5P_MXR_CFG_PAL;
		temp_reg &= S5P_MXR_CFG_INTERLACE;
		break;

	case TVOUT_PAL_BDGHI:
	case TVOUT_PAL_M:
	case TVOUT_PAL_N:
	case TVOUT_PAL_NC:
	case TVOUT_PAL_60:
		temp_reg &= ~S5P_MXR_CFG_HD;
		temp_reg |= S5P_MXR_CFG_PAL;
		temp_reg &= S5P_MXR_CFG_INTERLACE;
		break;

	case TVOUT_480P_60_16_9:
	case TVOUT_480P_60_4_3:
	case TVOUT_480P_59:
		temp_reg &= ~S5P_MXR_CFG_HD;
		temp_reg &= ~S5P_MXR_CFG_PAL;
		temp_reg |= S5P_MXR_CFG_PROGRASSIVE;
		break;

	case TVOUT_576P_50_16_9:
	case TVOUT_576P_50_4_3:
		temp_reg &= ~S5P_MXR_CFG_HD;
		temp_reg |= S5P_MXR_CFG_PAL;
		temp_reg |= S5P_MXR_CFG_PROGRASSIVE;
		break;

	case TVOUT_720P_50:
	case TVOUT_720P_59:
	case TVOUT_720P_60:
		temp_reg |= S5P_MXR_CFG_HD;
		temp_reg &= ~S5P_MXR_CFG_HD_1080I;
		temp_reg |= S5P_MXR_CFG_PROGRASSIVE;
		break;

#ifdef CONFIG_HDMI_14A_3D
	case TVOUT_720P_60_SBS_HALF:
	case TVOUT_720P_59_SBS_HALF:
	case TVOUT_720P_50_TB:
		temp_reg |= S5P_MXR_CFG_HD;
		temp_reg &= ~S5P_MXR_CFG_HD_1080I;
		temp_reg |= S5P_MXR_CFG_PROGRASSIVE;
		break;
#endif

	case TVOUT_1080I_50:
	case TVOUT_1080I_59:
	case TVOUT_1080I_60:
		temp_reg |= S5P_MXR_CFG_HD;
		temp_reg |= S5P_MXR_CFG_HD_1080I;
		temp_reg &= S5P_MXR_CFG_INTERLACE;
		break;

	case TVOUT_1080P_50:
	case TVOUT_1080P_59:
	case TVOUT_1080P_60:
	case TVOUT_1080P_30:
		temp_reg |= S5P_MXR_CFG_HD;
		temp_reg |= S5P_MXR_CFG_HD_1080P;
		temp_reg |= S5P_MXR_CFG_PROGRASSIVE;
		break;

#ifdef CONFIG_HDMI_14A_3D
	case TVOUT_1080P_24_TB:
	case TVOUT_1080P_23_TB:
		temp_reg |= S5P_MXR_CFG_HD;
		temp_reg |= S5P_MXR_CFG_HD_1080P;
		temp_reg |= S5P_MXR_CFG_PROGRASSIVE;
		break;
#endif
	default:
		tvout_err("invalid mode parameter = %d\n", mode);
		return -1;
	}

	switch (output_mode) {
	case TVOUT_COMPOSITE:
		temp_reg &= S5P_MXR_CFG_TV_OUT;
		temp_reg &= ~(0x1<<8);
		temp_reg |= MIXER_YUV444<<8;
		break;

	case TVOUT_HDMI_RGB:
	case TVOUT_DVI:
		temp_reg |= S5P_MXR_CFG_HDMI_OUT;
		temp_reg &= ~(0x1<<8);
		temp_reg |= MIXER_RGB888<<8;
		break;

	case TVOUT_HDMI:
		temp_reg |= S5P_MXR_CFG_HDMI_OUT;
		temp_reg &= ~(0x1<<8);
		temp_reg |= MIXER_YUV444<<8;
		break;

	default:
		tvout_err("invalid mode parameter = %d\n", mode);
		return -1;
	}

	if (0 <= rgb_type  && rgb_type <= 3)
		temp_reg |= rgb_type<<9;
	else
		printk(KERN_INFO "Wrong rgb type!!\n");

	tvout_dbg(KERN_INFO "Color range RGB Type : %x\n", rgb_type);
	writel(temp_reg, mixer_base + S5P_MXR_CFG);

	return 0;
}

void s5p_mixer_scaling(enum s5p_mixer_layer layer,
		struct s5ptvfb_user_scaling scaling)
{
	u32 reg, ver_val = 0, hor_val = 0;

	switch (scaling.ver) {
	case VERTICAL_X1:
		ver_val = 0;
		break;
	case VERTICAL_X2:
		ver_val = 1;
		break;
	}

	switch (scaling.hor) {
	case HORIZONTAL_X1:
		hor_val = 0;
		break;
	case HORIZONTAL_X2:
		hor_val = 1;
		break;
	}

	switch (layer) {
	case MIXER_GPR0_LAYER:
		reg = readl(mixer_base + S5P_MXR_GRAPHIC0_WH);
		reg |= S5P_MXR_GRP_V_SCALE(ver_val);
		reg |= S5P_MXR_GRP_H_SCALE(hor_val);
		writel(reg, mixer_base + S5P_MXR_GRAPHIC0_WH);
		break;
	case MIXER_GPR1_LAYER:
		reg = readl(mixer_base + S5P_MXR_GRAPHIC1_WH);
		reg |= S5P_MXR_GRP_V_SCALE(ver_val);
		reg |= S5P_MXR_GRP_H_SCALE(hor_val);
		writel(reg, mixer_base + S5P_MXR_GRAPHIC1_WH);
		break;
	case MIXER_VIDEO_LAYER:
		break;
	}
}

void s5p_mixer_set_color_format(enum s5p_mixer_layer layer,
	enum s5p_mixer_color_fmt format)
{
	u32 reg;

	switch (layer) {
	case MIXER_GPR0_LAYER:
		reg = readl(mixer_base + S5P_MXR_GRAPHIC0_CFG);
		reg &= ~(S5P_MXR_EG_COLOR_FORMAT(0xf));
		reg |= S5P_MXR_EG_COLOR_FORMAT(format);
		writel(reg, mixer_base + S5P_MXR_GRAPHIC0_CFG);
		break;
	case MIXER_GPR1_LAYER:
		reg = readl(mixer_base + S5P_MXR_GRAPHIC1_CFG);
		reg &= ~(S5P_MXR_EG_COLOR_FORMAT(0xf));
		reg |= S5P_MXR_EG_COLOR_FORMAT(format);
		writel(reg, mixer_base + S5P_MXR_GRAPHIC1_CFG);
		break;
	case MIXER_VIDEO_LAYER:
		break;
	}
}

void s5p_mixer_set_chroma_key(enum s5p_mixer_layer layer, bool enabled, u32 key)
{
	u32 reg;

	switch (layer) {
	case MIXER_GPR0_LAYER:
		reg = readl(mixer_base + S5P_MXR_GRAPHIC0_CFG);

		if (enabled)
			reg &= ~S5P_MXR_BLANK_CHANGE_NEW_PIXEL;
		else
			reg |= S5P_MXR_BLANK_CHANGE_NEW_PIXEL;

		writel(reg, mixer_base + S5P_MXR_GRAPHIC0_CFG);
		writel(S5P_MXR_GPR_BLANK_COLOR(key),
			mixer_base + S5P_MXR_GRAPHIC0_BLANK);
		break;
	case MIXER_GPR1_LAYER:
		reg = readl(mixer_base + S5P_MXR_GRAPHIC1_CFG);

		if (enabled)
			reg &= ~S5P_MXR_BLANK_CHANGE_NEW_PIXEL;
		else
			reg |= S5P_MXR_BLANK_CHANGE_NEW_PIXEL;

		writel(reg, mixer_base + S5P_MXR_GRAPHIC1_CFG);
		writel(S5P_MXR_GPR_BLANK_COLOR(key),
				mixer_base + S5P_MXR_GRAPHIC1_BLANK);
		break;
	case MIXER_VIDEO_LAYER:
		break;
	}
}

void s5p_mixer_init_bg_dither_enable(bool cr_dither_enable,
					bool cb_dither_enable,
					bool y_dither_enable)
{
	u32 temp_reg = 0;

	tvout_dbg("%d, %d, %d\n", cr_dither_enable, cb_dither_enable,
		y_dither_enable);

	temp_reg = (cr_dither_enable) ?
		   (temp_reg | S5P_MXR_BG_CR_DIHER_EN) :
		   (temp_reg & ~S5P_MXR_BG_CR_DIHER_EN);
	temp_reg = (cb_dither_enable) ?
		   (temp_reg | S5P_MXR_BG_CB_DIHER_EN) :
		   (temp_reg & ~S5P_MXR_BG_CB_DIHER_EN);
	temp_reg = (y_dither_enable) ?
		   (temp_reg | S5P_MXR_BG_Y_DIHER_EN) :
		   (temp_reg & ~S5P_MXR_BG_Y_DIHER_EN);

	writel(temp_reg, mixer_base + S5P_MXR_BG_CFG);

}

void s5p_mixer_init_csc_coef_default(enum s5p_mixer_rgb csc_type)
{
	tvout_dbg("%d\n", csc_type);

	switch (csc_type) {
	case MIXER_RGB601_16_235:
		writel((0 << 30) | (153 << 20) | (300 << 10) | (58 << 0),
			mixer_base + S5P_MXR_CM_COEFF_Y);
		writel((936 << 20) | (851 << 10) | (262 << 0),
			mixer_base + S5P_MXR_CM_COEFF_CB);
		writel((262 << 20) | (805 << 10) | (982 << 0),
			mixer_base + S5P_MXR_CM_COEFF_CR);
		break;

	case MIXER_RGB601_0_255:
		writel((1 << 30) | (132 << 20) | (258 << 10) | (50 << 0),
			mixer_base + S5P_MXR_CM_COEFF_Y);
		writel((949 << 20) | (876 << 10) | (225 << 0),
			mixer_base + S5P_MXR_CM_COEFF_CB);
		writel((225 << 20) | (836 << 10) | (988 << 0),
			mixer_base + S5P_MXR_CM_COEFF_CR);
		break;

	case MIXER_RGB709_16_235:
		writel((0 << 30) | (109 << 20) | (366 << 10) | (36 << 0),
			mixer_base + S5P_MXR_CM_COEFF_Y);
		writel((964 << 20) | (822 << 10) | (216 << 0),
			mixer_base + S5P_MXR_CM_COEFF_CB);
		writel((262 << 20) | (787 << 10) | (1000 << 0),
			mixer_base + S5P_MXR_CM_COEFF_CR);
		break;

	case MIXER_RGB709_0_255:
		writel((1 << 30) | (94 << 20) | (314 << 10) | (32 << 0),
			mixer_base + S5P_MXR_CM_COEFF_Y);
		writel((972 << 20) | (851 << 10) | (225 << 0),
			mixer_base + S5P_MXR_CM_COEFF_CB);
		writel((225 << 20) | (820 << 10) | (1004 << 0),
			mixer_base + S5P_MXR_CM_COEFF_CR);
		break;

	default:
		tvout_err("invalid csc_type parameter = %d\n", csc_type);
		break;
	}
}

void s5p_mixer_start(void)
{
	writel((readl(mixer_base + S5P_MXR_STATUS) | S5P_MXR_STATUS_RUN),
		mixer_base + S5P_MXR_STATUS);
}

void s5p_mixer_stop(void)
{
	u32 reg = readl(mixer_base + S5P_MXR_STATUS);

	reg &= ~S5P_MXR_STATUS_RUN;

	writel(reg, mixer_base + S5P_MXR_STATUS);

	do {
		reg = readl(mixer_base + S5P_MXR_STATUS);
	} while (!(reg & S5P_MXR_STATUS_IDLE_MODE));
}

void s5p_mixer_set_underflow_int_enable(enum s5p_mixer_layer layer, bool en)
{
	u32 enable_mask = 0;

	switch (layer) {
	case MIXER_VIDEO_LAYER:
		enable_mask = S5P_MXR_INT_EN_VP_ENABLE;
		break;

	case MIXER_GPR0_LAYER:
		enable_mask = S5P_MXR_INT_EN_GRP0_ENABLE;
		break;

	case MIXER_GPR1_LAYER:
		enable_mask = S5P_MXR_INT_EN_GRP1_ENABLE;
		break;
	}

	if (en) {
		writel((readl(mixer_base + S5P_MXR_INT_EN) | enable_mask),
			mixer_base + S5P_MXR_INT_EN);
	} else {
		writel((readl(mixer_base + S5P_MXR_INT_EN) & ~enable_mask),
			mixer_base + S5P_MXR_INT_EN);
	}
}

void s5p_mixer_set_vsync_interrupt(bool en)
{
	if (en) {
		writel(S5P_MXR_INT_STATUS_VSYNC_CLEARED, mixer_base +
			S5P_MXR_INT_STATUS);
		writel((readl(mixer_base + S5P_MXR_INT_EN) |
			S5P_MXR_INT_EN_VSYNC_ENABLE),
			mixer_base + S5P_MXR_INT_EN);
	} else {
		writel((readl(mixer_base + S5P_MXR_INT_EN) &
			~S5P_MXR_INT_EN_VSYNC_ENABLE),
			mixer_base + S5P_MXR_INT_EN);
	}

	tvout_dbg("%s mixer VSYNC interrupt.\n", en? "Enable": "Disable");
}

void s5p_mixer_clear_pend_all(void)
{
	writel(S5P_MXR_INT_STATUS_INT_FIRED | S5P_MXR_INT_STATUS_VP_FIRED |
		S5P_MXR_INT_STATUS_GRP0_FIRED | S5P_MXR_INT_STATUS_GRP1_FIRED,
			mixer_base + S5P_MXR_INT_STATUS);
}

irqreturn_t s5p_mixer_irq(int irq, void *dev_id)
{
	bool v_i_f;
	bool g0_i_f;
	bool g1_i_f;
	bool mxr_i_f;
	u32 temp_reg = 0;
	unsigned long spin_flags;
	u32 top_y_addr, top_c_addr;
	int i = 0;
	unsigned int pre_vp_buff_idx;

	spin_lock_irqsave(&lock_mixer, spin_flags);

	v_i_f = (readl(mixer_base + S5P_MXR_INT_STATUS)
			& S5P_MXR_INT_STATUS_VP_FIRED) ? true : false;
	g0_i_f = (readl(mixer_base + S5P_MXR_INT_STATUS)
			& S5P_MXR_INT_STATUS_GRP0_FIRED) ? true : false;
	g1_i_f = (readl(mixer_base + S5P_MXR_INT_STATUS)
			& S5P_MXR_INT_STATUS_GRP1_FIRED) ? true : false;
	mxr_i_f = (readl(mixer_base + S5P_MXR_INT_STATUS)
			& S5P_MXR_INT_STATUS_INT_FIRED) ? true : false;

	if (mxr_i_f) {
		temp_reg |= S5P_MXR_INT_STATUS_INT_FIRED;

		if (v_i_f) {
			temp_reg |= S5P_MXR_INT_STATUS_VP_FIRED;
			tvout_dbg("VP fifo under run!!\n");
		}

		if (g0_i_f) {
			temp_reg |= S5P_MXR_INT_STATUS_GRP0_FIRED;
			tvout_dbg("GRP0 fifo under run!!\n");
		}

		if (g1_i_f) {
			temp_reg |= S5P_MXR_INT_STATUS_GRP1_FIRED;
			tvout_dbg("GRP1 fifo under run!!\n");
		}

		if (!v_i_f && !g0_i_f && !g1_i_f) {
			writel(S5P_MXR_INT_STATUS_VSYNC_CLEARED,
				mixer_base + S5P_MXR_INT_STATUS);
			s5p_vp_ctrl_get_src_addr(&top_y_addr, &top_c_addr);

			pre_vp_buff_idx = s5ptv_vp_buff.vp_access_buff_idx;
			for (i = 0; i < S5PTV_VP_BUFF_CNT; i++) {
				if (top_y_addr == s5ptv_vp_buff.vp_buffs[i].phy_base) {
					s5ptv_vp_buff.vp_access_buff_idx = i;
					break;
				}
			}

			for (i = 0; i < S5PTV_VP_BUFF_CNT - 1; i++) {
				if (s5ptv_vp_buff.copy_buff_idxs[i]
					== s5ptv_vp_buff.vp_access_buff_idx) {
					s5ptv_vp_buff.copy_buff_idxs[i] = pre_vp_buff_idx;
					break;
				}
			}
			wake_up(&s5ptv_wq);
		} else {
			writel(temp_reg, mixer_base + S5P_MXR_INT_STATUS);
		}
	}
	spin_unlock_irqrestore(&lock_mixer, spin_flags);

	return IRQ_HANDLED;
}

void s5p_mixer_init(void __iomem *addr)
{
	mixer_base = addr;

	spin_lock_init(&lock_mixer);
}
