/* linux/drivers/media/video/samsung/tvout/hw_if/sdo.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Hardware interface functions for SDO (Standard Definition Output)
 *	- SDO: Analog TV encoder + DAC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/delay.h>

#include <mach/regs-clock.h>
#include <mach/regs-sdo.h>

#include "../s5p_tvout_common_lib.h"
#include "hw_if.h"

#undef tvout_dbg

#ifdef CONFIG_SDO_DEBUG
#define tvout_dbg(fmt, ...)					\
		printk(KERN_INFO "\t\t[SDO] %s(): " fmt,	\
			__func__, ##__VA_ARGS__)
#else
#define tvout_dbg(fmt, ...)
#endif

void __iomem *sdo_base;

static u32 s5p_sdo_calc_wss_cgms_crc(u32 value)
{
	u8 i;
	u8 cgms[14], crc[6], old_crc;
	u32 temp_in;

	temp_in = value;

	for (i = 0; i < 14; i++)
		cgms[i] = (u8)(temp_in >> i) & 0x1;

	/* initialize state */
	for (i = 0; i < 6; i++)
		crc[i] = 0x1;

	/* round 20 */
	for (i = 0; i < 14; i++) {
		old_crc = crc[0];
		crc[0] = crc[1];
		crc[1] = crc[2];
		crc[2] = crc[3];
		crc[3] = crc[4];
		crc[4] = old_crc ^ cgms[i] ^ crc[5];
		crc[5] = old_crc ^ cgms[i];
	}

	/* recompose to return crc */
	temp_in &= 0x3fff;

	for (i = 0; i < 6; i++)
		temp_in |= ((u32)(crc[i] & 0x1) << i);

	return temp_in;
}

static int s5p_sdo_set_antialias_filter_coeff_default(
		enum s5p_sdo_level composite_level,
		enum s5p_sdo_vsync_ratio composite_ratio)
{
	tvout_dbg("%d, %d\n", composite_level, composite_ratio);

	switch (composite_level) {
	case SDO_LEVEL_0IRE:
		switch (composite_ratio) {
		case SDO_VTOS_RATIO_10_4:
			writel(0x00000000, sdo_base + S5P_SDO_Y3);
			writel(0x00000000, sdo_base + S5P_SDO_Y4);
			writel(0x00000000, sdo_base + S5P_SDO_Y5);
			writel(0x00000000, sdo_base + S5P_SDO_Y6);
			writel(0x00000000, sdo_base + S5P_SDO_Y7);
			writel(0x00000000, sdo_base + S5P_SDO_Y8);
			writel(0x00000000, sdo_base + S5P_SDO_Y9);
			writel(0x00000000, sdo_base + S5P_SDO_Y10);
			writel(0x0000029a, sdo_base + S5P_SDO_Y11);
			writel(0x00000000, sdo_base + S5P_SDO_CB0);
			writel(0x00000000, sdo_base + S5P_SDO_CB1);
			writel(0x00000000, sdo_base + S5P_SDO_CB2);
			writel(0x00000000, sdo_base + S5P_SDO_CB3);
			writel(0x00000000, sdo_base + S5P_SDO_CB4);
			writel(0x00000001, sdo_base + S5P_SDO_CB5);
			writel(0x00000007, sdo_base + S5P_SDO_CB6);
			writel(0x00000015, sdo_base + S5P_SDO_CB7);
			writel(0x0000002b, sdo_base + S5P_SDO_CB8);
			writel(0x00000045, sdo_base + S5P_SDO_CB9);
			writel(0x00000059, sdo_base + S5P_SDO_CB10);
			writel(0x00000061, sdo_base + S5P_SDO_CB11);
			writel(0x00000000, sdo_base + S5P_SDO_CR1);
			writel(0x00000000, sdo_base + S5P_SDO_CR2);
			writel(0x00000000, sdo_base + S5P_SDO_CR3);
			writel(0x00000000, sdo_base + S5P_SDO_CR4);
			writel(0x00000002, sdo_base + S5P_SDO_CR5);
			writel(0x0000000a, sdo_base + S5P_SDO_CR6);
			writel(0x0000001e, sdo_base + S5P_SDO_CR7);
			writel(0x0000003d, sdo_base + S5P_SDO_CR8);
			writel(0x00000061, sdo_base + S5P_SDO_CR9);
			writel(0x0000007a, sdo_base + S5P_SDO_CR10);
			writel(0x0000008f, sdo_base + S5P_SDO_CR11);
			break;

		case SDO_VTOS_RATIO_7_3:
			writel(0x00000000, sdo_base + S5P_SDO_Y0);
			writel(0x00000000, sdo_base + S5P_SDO_Y1);
			writel(0x00000000, sdo_base + S5P_SDO_Y2);
			writel(0x00000000, sdo_base + S5P_SDO_Y3);
			writel(0x00000000, sdo_base + S5P_SDO_Y4);
			writel(0x00000000, sdo_base + S5P_SDO_Y5);
			writel(0x00000000, sdo_base + S5P_SDO_Y6);
			writel(0x00000000, sdo_base + S5P_SDO_Y7);
			writel(0x00000000, sdo_base + S5P_SDO_Y8);
			writel(0x00000000, sdo_base + S5P_SDO_Y9);
			writel(0x00000000, sdo_base + S5P_SDO_Y10);
			writel(0x00000281, sdo_base + S5P_SDO_Y11);
			writel(0x00000000, sdo_base + S5P_SDO_CB0);
			writel(0x00000000, sdo_base + S5P_SDO_CB1);
			writel(0x00000000, sdo_base + S5P_SDO_CB2);
			writel(0x00000000, sdo_base + S5P_SDO_CB3);
			writel(0x00000000, sdo_base + S5P_SDO_CB4);
			writel(0x00000001, sdo_base + S5P_SDO_CB5);
			writel(0x00000007, sdo_base + S5P_SDO_CB6);
			writel(0x00000015, sdo_base + S5P_SDO_CB7);
			writel(0x0000002a, sdo_base + S5P_SDO_CB8);
			writel(0x00000044, sdo_base + S5P_SDO_CB9);
			writel(0x00000057, sdo_base + S5P_SDO_CB10);
			writel(0x0000005f, sdo_base + S5P_SDO_CB11);
			writel(0x00000000, sdo_base + S5P_SDO_CR1);
			writel(0x00000000, sdo_base + S5P_SDO_CR2);
			writel(0x00000000, sdo_base + S5P_SDO_CR3);
			writel(0x00000000, sdo_base + S5P_SDO_CR4);
			writel(0x00000002, sdo_base + S5P_SDO_CR5);
			writel(0x0000000a, sdo_base + S5P_SDO_CR6);
			writel(0x0000001d, sdo_base + S5P_SDO_CR7);
			writel(0x0000003c, sdo_base + S5P_SDO_CR8);
			writel(0x0000005f, sdo_base + S5P_SDO_CR9);
			writel(0x0000007b, sdo_base + S5P_SDO_CR10);
			writel(0x00000086, sdo_base + S5P_SDO_CR11);
			break;

		default:
			tvout_err("invalid composite_ratio parameter(%d)\n",
				composite_ratio);
			return -1;
		}

		break;

	case SDO_LEVEL_75IRE:
		switch (composite_ratio) {
		case SDO_VTOS_RATIO_10_4:
			writel(0x00000000, sdo_base + S5P_SDO_Y0);
			writel(0x00000000, sdo_base + S5P_SDO_Y1);
			writel(0x00000000, sdo_base + S5P_SDO_Y2);
			writel(0x00000000, sdo_base + S5P_SDO_Y3);
			writel(0x00000000, sdo_base + S5P_SDO_Y4);
			writel(0x00000000, sdo_base + S5P_SDO_Y5);
			writel(0x00000000, sdo_base + S5P_SDO_Y6);
			writel(0x00000000, sdo_base + S5P_SDO_Y7);
			writel(0x00000000, sdo_base + S5P_SDO_Y8);
			writel(0x00000000, sdo_base + S5P_SDO_Y9);
			writel(0x00000000, sdo_base + S5P_SDO_Y10);
			writel(0x0000025d, sdo_base + S5P_SDO_Y11);
			writel(0x00000000, sdo_base + S5P_SDO_CB0);
			writel(0x00000000, sdo_base + S5P_SDO_CB1);
			writel(0x00000000, sdo_base + S5P_SDO_CB2);
			writel(0x00000000, sdo_base + S5P_SDO_CB3);
			writel(0x00000000, sdo_base + S5P_SDO_CB4);
			writel(0x00000001, sdo_base + S5P_SDO_CB5);
			writel(0x00000007, sdo_base + S5P_SDO_CB6);
			writel(0x00000014, sdo_base + S5P_SDO_CB7);
			writel(0x00000028, sdo_base + S5P_SDO_CB8);
			writel(0x0000003f, sdo_base + S5P_SDO_CB9);
			writel(0x00000052, sdo_base + S5P_SDO_CB10);
			writel(0x0000005a, sdo_base + S5P_SDO_CB11);
			writel(0x00000000, sdo_base + S5P_SDO_CR1);
			writel(0x00000000, sdo_base + S5P_SDO_CR2);
			writel(0x00000000, sdo_base + S5P_SDO_CR3);
			writel(0x00000000, sdo_base + S5P_SDO_CR4);
			writel(0x00000001, sdo_base + S5P_SDO_CR5);
			writel(0x00000009, sdo_base + S5P_SDO_CR6);
			writel(0x0000001c, sdo_base + S5P_SDO_CR7);
			writel(0x00000039, sdo_base + S5P_SDO_CR8);
			writel(0x0000005a, sdo_base + S5P_SDO_CR9);
			writel(0x00000074, sdo_base + S5P_SDO_CR10);
			writel(0x0000007e, sdo_base + S5P_SDO_CR11);
			break;

		case SDO_VTOS_RATIO_7_3:
			writel(0x00000000, sdo_base + S5P_SDO_Y0);
			writel(0x00000000, sdo_base + S5P_SDO_Y1);
			writel(0x00000000, sdo_base + S5P_SDO_Y2);
			writel(0x00000000, sdo_base + S5P_SDO_Y3);
			writel(0x00000000, sdo_base + S5P_SDO_Y4);
			writel(0x00000000, sdo_base + S5P_SDO_Y5);
			writel(0x00000000, sdo_base + S5P_SDO_Y6);
			writel(0x00000000, sdo_base + S5P_SDO_Y7);
			writel(0x00000000, sdo_base + S5P_SDO_Y8);
			writel(0x00000000, sdo_base + S5P_SDO_Y9);
			writel(0x00000000, sdo_base + S5P_SDO_Y10);
			writel(0x00000251, sdo_base + S5P_SDO_Y11);
			writel(0x00000000, sdo_base + S5P_SDO_CB0);
			writel(0x00000000, sdo_base + S5P_SDO_CB1);
			writel(0x00000000, sdo_base + S5P_SDO_CB2);
			writel(0x00000000, sdo_base + S5P_SDO_CB3);
			writel(0x00000000, sdo_base + S5P_SDO_CB4);
			writel(0x00000001, sdo_base + S5P_SDO_CB5);
			writel(0x00000006, sdo_base + S5P_SDO_CB6);
			writel(0x00000013, sdo_base + S5P_SDO_CB7);
			writel(0x00000028, sdo_base + S5P_SDO_CB8);
			writel(0x0000003f, sdo_base + S5P_SDO_CB9);
			writel(0x00000051, sdo_base + S5P_SDO_CB10);
			writel(0x00000056, sdo_base + S5P_SDO_CB11);
			writel(0x00000000, sdo_base + S5P_SDO_CR1);
			writel(0x00000000, sdo_base + S5P_SDO_CR2);
			writel(0x00000000, sdo_base + S5P_SDO_CR3);
			writel(0x00000000, sdo_base + S5P_SDO_CR4);
			writel(0x00000002, sdo_base + S5P_SDO_CR5);
			writel(0x00000005, sdo_base + S5P_SDO_CR6);
			writel(0x00000018, sdo_base + S5P_SDO_CR7);
			writel(0x00000037, sdo_base + S5P_SDO_CR8);
			writel(0x0000005A, sdo_base + S5P_SDO_CR9);
			writel(0x00000076, sdo_base + S5P_SDO_CR10);
			writel(0x0000007e, sdo_base + S5P_SDO_CR11);
			break;

		default:
			tvout_err("invalid composite_ratio parameter(%d)\n",
				composite_ratio);
			return -1;
		}

		break;

	default:
		tvout_err("invalid composite_level parameter(%d)\n",
			composite_level);
		return -1;
	}

	return 0;
}


int s5p_sdo_set_video_scale_cfg(
		enum s5p_sdo_level composite_level,
		enum s5p_sdo_vsync_ratio composite_ratio)
{
	u32 temp_reg = 0;

	tvout_dbg("%d, %d\n", composite_level, composite_ratio);

	switch (composite_level) {
	case SDO_LEVEL_0IRE:
		temp_reg |= S5P_SDO_COMPOSITE_LEVEL_SEL_0IRE;
		break;

	case SDO_LEVEL_75IRE:
		temp_reg |= S5P_SDO_COMPOSITE_LEVEL_SEL_75IRE;
		break;

	default:
		tvout_err("invalid composite_level parameter(%d)\n",
			composite_ratio);
		return -1;
	}

	switch (composite_ratio) {
	case SDO_VTOS_RATIO_10_4:
		temp_reg |= S5P_SDO_COMPOSITE_VTOS_RATIO_10_4;
		break;

	case SDO_VTOS_RATIO_7_3:
		temp_reg |= S5P_SDO_COMPOSITE_VTOS_RATIO_7_3;
		break;

	default:
		tvout_err("invalid composite_ratio parameter(%d)\n",
			composite_ratio);
		return -1;
	}

	writel(temp_reg, sdo_base + S5P_SDO_SCALE);

	return 0;
}

int s5p_sdo_set_vbi(
		bool wss_cvbs, enum s5p_sdo_closed_caption_type caption_cvbs)
{
	u32 temp_reg = 0;

	tvout_dbg("%d, %d\n", wss_cvbs, caption_cvbs);

	if (wss_cvbs)
		temp_reg = S5P_SDO_CVBS_WSS_INS;
	else
		temp_reg = S5P_SDO_CVBS_NO_WSS;

	switch (caption_cvbs) {
	case SDO_NO_INS:
		temp_reg |= S5P_SDO_CVBS_NO_CLOSED_CAPTION;
		break;

	case SDO_INS_1:
		temp_reg |= S5P_SDO_CVBS_21H_CLOSED_CAPTION;
		break;

	case SDO_INS_2:
		temp_reg |= S5P_SDO_CVBS_21H_284H_CLOSED_CAPTION;
		break;

	case SDO_INS_OTHERS:
		temp_reg |= S5P_SDO_CVBS_USE_OTHERS;
		break;

	default:
		tvout_err("invalid caption_cvbs parameter(%d)\n",
			caption_cvbs);
		return -1;
	}


	writel(temp_reg, sdo_base + S5P_SDO_VBI);

	return 0;
}

void s5p_sdo_set_offset_gain(u32 offset, u32 gain)
{
	tvout_dbg("%d, %d\n", offset, gain);

	writel(S5P_SDO_SCALE_CONV_OFFSET(offset) |
		S5P_SDO_SCALE_CONV_GAIN(gain),
		sdo_base + S5P_SDO_SCALE_CH0);
}

void s5p_sdo_set_delay(
		u32 delay_y, u32 offset_video_start, u32 offset_video_end)
{
	tvout_dbg("%d, %d, %d\n", delay_y, offset_video_start,
		offset_video_end);

	writel(S5P_SDO_DELAY_YTOC(delay_y) |
		S5P_SDO_ACTIVE_START_OFFSET(offset_video_start) |
		S5P_SDO_ACTIVE_END_OFFSET(offset_video_end),
		sdo_base + S5P_SDO_YCDELAY);
}

void s5p_sdo_set_schlock(bool color_sucarrier_pha_adj)
{
	tvout_dbg("%d\n", color_sucarrier_pha_adj);

	if (color_sucarrier_pha_adj)
		writel(S5P_SDO_COLOR_SC_PHASE_ADJ,
			sdo_base + S5P_SDO_SCHLOCK);
	else
		writel(S5P_SDO_COLOR_SC_PHASE_NOADJ,
			sdo_base + S5P_SDO_SCHLOCK);
}

void s5p_sdo_set_brightness_hue_saturation(
		struct s5p_sdo_bright_hue_saturation bri_hue_sat)
{
	u32 temp_reg = 0;

	tvout_dbg("%d, %d, %d, %d, %d, %d, %d, %d, %d\n",
		bri_hue_sat.bright_hue_sat_adj,	bri_hue_sat.gain_brightness,
		bri_hue_sat.offset_brightness, bri_hue_sat.gain0_cb_hue_sat,
		bri_hue_sat.gain1_cb_hue_sat, bri_hue_sat.gain0_cr_hue_sat,
		bri_hue_sat.gain1_cr_hue_sat, bri_hue_sat.offset_cb_hue_sat,
		bri_hue_sat.offset_cr_hue_sat);

	temp_reg = readl(sdo_base + S5P_SDO_CCCON);

	if (bri_hue_sat.bright_hue_sat_adj)
		temp_reg &= ~S5P_SDO_COMPENSATION_BHS_ADJ_OFF;
	else
		temp_reg |= S5P_SDO_COMPENSATION_BHS_ADJ_OFF;

	writel(temp_reg, sdo_base + S5P_SDO_CCCON);


	writel(S5P_SDO_BRIGHTNESS_GAIN(bri_hue_sat.gain_brightness) |
		S5P_SDO_BRIGHTNESS_OFFSET(bri_hue_sat.offset_brightness),
			sdo_base + S5P_SDO_YSCALE);

	writel(S5P_SDO_HS_CB_GAIN0(bri_hue_sat.gain0_cb_hue_sat) |
		S5P_SDO_HS_CB_GAIN1(bri_hue_sat.gain1_cb_hue_sat),
			sdo_base + S5P_SDO_CBSCALE);

	writel(S5P_SDO_HS_CR_GAIN0(bri_hue_sat.gain0_cr_hue_sat) |
		S5P_SDO_HS_CR_GAIN1(bri_hue_sat.gain1_cr_hue_sat),
			sdo_base + S5P_SDO_CRSCALE);

	writel(S5P_SDO_HS_CR_OFFSET(bri_hue_sat.offset_cr_hue_sat) |
		S5P_SDO_HS_CB_OFFSET(bri_hue_sat.offset_cb_hue_sat),
			sdo_base + S5P_SDO_CB_CR_OFFSET);
}

void s5p_sdo_set_cvbs_color_compensation(
		struct s5p_sdo_cvbs_compensation cvbs_comp)
{
	u32 temp_reg = 0;

	tvout_dbg("%d, %d, %d, %d, %d, %d\n",
		cvbs_comp.cvbs_color_compen, cvbs_comp.y_lower_mid,
		cvbs_comp.y_bottom, cvbs_comp.y_top,
		cvbs_comp.y_upper_mid, cvbs_comp.radius);

	temp_reg = readl(sdo_base + S5P_SDO_CCCON);

	if (cvbs_comp.cvbs_color_compen)
		temp_reg &= ~S5P_SDO_COMPENSATION_CVBS_COMP_OFF;
	else
		temp_reg |= S5P_SDO_COMPENSATION_CVBS_COMP_OFF;

	writel(temp_reg, sdo_base + S5P_SDO_CCCON);


	writel(S5P_SDO_Y_LOWER_MID_CVBS_CORN(cvbs_comp.y_lower_mid) |
		S5P_SDO_Y_BOTTOM_CVBS_CORN(cvbs_comp.y_bottom),
			sdo_base + S5P_SDO_CVBS_CC_Y1);

	writel(S5P_SDO_Y_TOP_CVBS_CORN(cvbs_comp.y_top) |
		S5P_SDO_Y_UPPER_MID_CVBS_CORN(cvbs_comp.y_upper_mid),
			sdo_base + S5P_SDO_CVBS_CC_Y2);

	writel(S5P_SDO_RADIUS_CVBS_CORN(cvbs_comp.radius),
			sdo_base + S5P_SDO_CVBS_CC_C);
}

void s5p_sdo_set_component_porch(
		u32 back_525, u32 front_525, u32 back_625, u32 front_625)
{
	tvout_dbg("%d, %d, %d, %d\n",
			back_525, front_525, back_625, front_625);

	writel(S5P_SDO_COMPONENT_525_BP(back_525) |
		S5P_SDO_COMPONENT_525_FP(front_525),
			sdo_base + S5P_SDO_CSC_525_PORCH);
	writel(S5P_SDO_COMPONENT_625_BP(back_625) |
		S5P_SDO_COMPONENT_625_FP(front_625),
			sdo_base + S5P_SDO_CSC_625_PORCH);
}

void s5p_sdo_set_ch_xtalk_cancel_coef(u32 coeff2, u32 coeff1)
{
	tvout_dbg("%d, %d\n", coeff2, coeff1);

	writel(S5P_SDO_XTALK_COEF02(coeff2) |
		S5P_SDO_XTALK_COEF01(coeff1),
			sdo_base + S5P_SDO_XTALK0);
}

void s5p_sdo_set_closed_caption(u32 display_cc, u32 non_display_cc)
{
	tvout_dbg("%d, %d\n", display_cc, non_display_cc);

	writel(S5P_SDO_DISPLAY_CC_CAPTION(display_cc) |
		S5P_SDO_NON_DISPLAY_CC_CAPTION(non_display_cc),
		sdo_base + S5P_SDO_ARMCC);
}

int s5p_sdo_set_wss525_data(struct s5p_sdo_525_data wss525)
{
	u32 temp_reg = 0;

	tvout_dbg("%d, %d, %d, %d\n",
		wss525.copy_permit, wss525.mv_psp,
		wss525.copy_info, wss525.display_ratio);

	switch (wss525.copy_permit) {
	case SDO_525_COPY_PERMIT:
		temp_reg = S5P_SDO_WORD2_WSS525_COPY_PERMIT;
		break;

	case SDO_525_ONECOPY_PERMIT:
		temp_reg = S5P_SDO_WORD2_WSS525_ONECOPY_PERMIT;
		break;

	case SDO_525_NOCOPY_PERMIT:
		temp_reg = S5P_SDO_WORD2_WSS525_NOCOPY_PERMIT;
		break;

	default:
		tvout_err("invalid copy_permit parameter(%d)\n",
			wss525.copy_permit);
		return -1;
	}

	switch (wss525.mv_psp) {
	case SDO_525_MV_PSP_OFF:
		temp_reg |= S5P_SDO_WORD2_WSS525_MV_PSP_OFF;
		break;

	case SDO_525_MV_PSP_ON_2LINE_BURST:
		temp_reg |= S5P_SDO_WORD2_WSS525_MV_PSP_ON_2LINE_BURST;
		break;

	case SDO_525_MV_PSP_ON_BURST_OFF:
		temp_reg |= S5P_SDO_WORD2_WSS525_MV_PSP_ON_BURST_OFF;
		break;

	case SDO_525_MV_PSP_ON_4LINE_BURST:
		temp_reg |= S5P_SDO_WORD2_WSS525_MV_PSP_ON_4LINE_BURST;
		break;

	default:
		tvout_err("invalid mv_psp parameter(%d)\n", wss525.mv_psp);
		return -1;
	}

	switch (wss525.copy_info) {
	case SDO_525_COPY_INFO:
		temp_reg |= S5P_SDO_WORD1_WSS525_COPY_INFO;
		break;

	case SDO_525_DEFAULT:
		temp_reg |= S5P_SDO_WORD1_WSS525_DEFAULT;
		break;

	default:
		tvout_err("invalid copy_info parameter(%d)\n",
				wss525.copy_info);
		return -1;
	}

	if (wss525.analog_on)
		temp_reg |= S5P_SDO_WORD2_WSS525_ANALOG_ON;
	else
		temp_reg |= S5P_SDO_WORD2_WSS525_ANALOG_OFF;

	switch (wss525.display_ratio) {
	case SDO_525_COPY_PERMIT:
		temp_reg |= S5P_SDO_WORD0_WSS525_4_3_NORMAL;
		break;

	case SDO_525_ONECOPY_PERMIT:
		temp_reg |= S5P_SDO_WORD0_WSS525_16_9_ANAMORPIC;
		break;

	case SDO_525_NOCOPY_PERMIT:
		temp_reg |= S5P_SDO_WORD0_WSS525_4_3_LETTERBOX;
		break;

	default:
		tvout_err("invalid display_ratio parameter(%d)\n",
			wss525.display_ratio);
		return -1;
	}

	writel(temp_reg |
		S5P_SDO_CRC_WSS525(s5p_sdo_calc_wss_cgms_crc(temp_reg)),
		sdo_base + S5P_SDO_WSS525);

	return 0;
}

int s5p_sdo_set_wss625_data(struct s5p_sdo_625_data wss625)
{
	u32 temp_reg = 0;

	tvout_dbg("%d, %d, %d, %d, %d, %d, %d, %d, %d\n",
		wss625.surround_sound, wss625.copyright,
		wss625.copy_protection, wss625.text_subtitles,
		wss625.open_subtitles, wss625.camera_film,
		wss625.color_encoding, wss625.helper_signal,
		wss625.display_ratio);

	if (wss625.surround_sound)
		temp_reg = S5P_SDO_WSS625_SURROUND_SOUND_ENABLE;
	else
		temp_reg = S5P_SDO_WSS625_SURROUND_SOUND_DISABLE;

	if (wss625.copyright)
		temp_reg |= S5P_SDO_WSS625_COPYRIGHT;
	else
		temp_reg |= S5P_SDO_WSS625_NO_COPYRIGHT;

	if (wss625.copy_protection)
		temp_reg |= S5P_SDO_WSS625_COPY_RESTRICTED;
	else
		temp_reg |= S5P_SDO_WSS625_COPY_NOT_RESTRICTED;

	if (wss625.text_subtitles)
		temp_reg |= S5P_SDO_WSS625_TELETEXT_SUBTITLES;
	else
		temp_reg |= S5P_SDO_WSS625_TELETEXT_NO_SUBTITLES;

	switch (wss625.open_subtitles) {
	case SDO_625_NO_OPEN_SUBTITLES:
		temp_reg |= S5P_SDO_WSS625_NO_OPEN_SUBTITLES;
		break;

	case SDO_625_INACT_OPEN_SUBTITLES:
		temp_reg |= S5P_SDO_WSS625_INACT_OPEN_SUBTITLES;
		break;

	case SDO_625_OUTACT_OPEN_SUBTITLES:
		temp_reg |= S5P_SDO_WSS625_OUTACT_OPEN_SUBTITLES;
		break;

	default:
		tvout_err("invalid open_subtitles parameter(%d)\n",
			wss625.open_subtitles);
		return -1;
	}

	switch (wss625.camera_film) {
	case SDO_625_CAMERA:
		temp_reg |= S5P_SDO_WSS625_CAMERA;
		break;

	case SDO_625_FILM:
		temp_reg |= S5P_SDO_WSS625_FILM;
		break;

	default:
		tvout_err("invalid camera_film parameter(%d)\n",
			wss625.camera_film);
		return -1;
	}

	switch (wss625.color_encoding) {
	case SDO_625_NORMAL_PAL:
		temp_reg |= S5P_SDO_WSS625_NORMAL_PAL;
		break;

	case SDO_625_MOTION_ADAPTIVE_COLORPLUS:
		temp_reg |= S5P_SDO_WSS625_MOTION_ADAPTIVE_COLORPLUS;
		break;

	default:
		tvout_err("invalid color_encoding parameter(%d)\n",
			wss625.color_encoding);
		return -1;
	}

	if (wss625.helper_signal)
		temp_reg |= S5P_SDO_WSS625_HELPER_SIG;
	else
		temp_reg |= S5P_SDO_WSS625_HELPER_NO_SIG;

	switch (wss625.display_ratio) {
	case SDO_625_4_3_FULL_576:
		temp_reg |= S5P_SDO_WSS625_4_3_FULL_576;
		break;

	case SDO_625_14_9_LETTERBOX_CENTER_504:
		temp_reg |= S5P_SDO_WSS625_14_9_LETTERBOX_CENTER_504;
		break;

	case SDO_625_14_9_LETTERBOX_TOP_504:
		temp_reg |= S5P_SDO_WSS625_14_9_LETTERBOX_TOP_504;
		break;

	case SDO_625_16_9_LETTERBOX_CENTER_430:
		temp_reg |= S5P_SDO_WSS625_16_9_LETTERBOX_CENTER_430;
		break;

	case SDO_625_16_9_LETTERBOX_TOP_430:
		temp_reg |= S5P_SDO_WSS625_16_9_LETTERBOX_TOP_430;
		break;

	case SDO_625_16_9_LETTERBOX_CENTER:
		temp_reg |= S5P_SDO_WSS625_16_9_LETTERBOX_CENTER;
		break;

	case SDO_625_14_9_FULL_CENTER_576:
		temp_reg |= S5P_SDO_WSS625_14_9_FULL_CENTER_576;
		break;

	case SDO_625_16_9_ANAMORPIC_576:
		temp_reg |= S5P_SDO_WSS625_16_9_ANAMORPIC_576;
		break;

	default:
		tvout_err("invalid display_ratio parameter(%d)\n",
			wss625.display_ratio);
		return -1;
	}

	writel(temp_reg, sdo_base + S5P_SDO_WSS625);

	return 0;
}

int s5p_sdo_set_cgmsa525_data(struct s5p_sdo_525_data cgmsa525)
{
	u32 temp_reg = 0;

	tvout_dbg("%d, %d, %d, %d\n",
		cgmsa525.copy_permit, cgmsa525.mv_psp,
		cgmsa525.copy_info, cgmsa525.display_ratio);

	switch (cgmsa525.copy_permit) {
	case SDO_525_COPY_PERMIT:
		temp_reg = S5P_SDO_WORD2_CGMS525_COPY_PERMIT;
		break;

	case SDO_525_ONECOPY_PERMIT:
		temp_reg = S5P_SDO_WORD2_CGMS525_ONECOPY_PERMIT;
		break;

	case SDO_525_NOCOPY_PERMIT:
		temp_reg = S5P_SDO_WORD2_CGMS525_NOCOPY_PERMIT;
		break;

	default:
		tvout_err("invalid copy_permit parameter(%d)\n",
			cgmsa525.copy_permit);
		return -1;
	}

	switch (cgmsa525.mv_psp) {
	case SDO_525_MV_PSP_OFF:
		temp_reg |= S5P_SDO_WORD2_CGMS525_MV_PSP_OFF;
		break;

	case SDO_525_MV_PSP_ON_2LINE_BURST:
		temp_reg |= S5P_SDO_WORD2_CGMS525_MV_PSP_ON_2LINE_BURST;
		break;

	case SDO_525_MV_PSP_ON_BURST_OFF:
		temp_reg |= S5P_SDO_WORD2_CGMS525_MV_PSP_ON_BURST_OFF;
		break;

	case SDO_525_MV_PSP_ON_4LINE_BURST:
		temp_reg |= S5P_SDO_WORD2_CGMS525_MV_PSP_ON_4LINE_BURST;
		break;

	default:
		tvout_err("invalid mv_psp parameter(%d)\n", cgmsa525.mv_psp);
		return -1;
	}

	switch (cgmsa525.copy_info) {
	case SDO_525_COPY_INFO:
		temp_reg |= S5P_SDO_WORD1_CGMS525_COPY_INFO;
		break;

	case SDO_525_DEFAULT:
		temp_reg |= S5P_SDO_WORD1_CGMS525_DEFAULT;
		break;

	default:
		tvout_err("invalid copy_info parameter(%d)\n",
				cgmsa525.copy_info);
		return -1;
	}

	if (cgmsa525.analog_on)
		temp_reg |= S5P_SDO_WORD2_CGMS525_ANALOG_ON;
	else
		temp_reg |= S5P_SDO_WORD2_CGMS525_ANALOG_OFF;

	switch (cgmsa525.display_ratio) {
	case SDO_525_COPY_PERMIT:
		temp_reg |= S5P_SDO_WORD0_CGMS525_4_3_NORMAL;
		break;

	case SDO_525_ONECOPY_PERMIT:
		temp_reg |= S5P_SDO_WORD0_CGMS525_16_9_ANAMORPIC;
		break;

	case SDO_525_NOCOPY_PERMIT:
		temp_reg |= S5P_SDO_WORD0_CGMS525_4_3_LETTERBOX;
		break;

	default:
		tvout_err("invalid display_ratio parameter(%d)\n",
			cgmsa525.display_ratio);
		return -1;
	}

	writel(temp_reg | S5P_SDO_CRC_CGMS525(
		s5p_sdo_calc_wss_cgms_crc(temp_reg)),
		sdo_base + S5P_SDO_CGMS525);

	return 0;
}


int s5p_sdo_set_cgmsa625_data(struct s5p_sdo_625_data cgmsa625)
{
	u32 temp_reg = 0;

	tvout_dbg("%d, %d, %d, %d, %d, %d, %d, %d, %d\n",
		cgmsa625.surround_sound, cgmsa625.copyright,
		cgmsa625.copy_protection, cgmsa625.text_subtitles,
		cgmsa625.open_subtitles, cgmsa625.camera_film,
		cgmsa625.color_encoding, cgmsa625.helper_signal,
		cgmsa625.display_ratio);

	if (cgmsa625.surround_sound)
		temp_reg = S5P_SDO_CGMS625_SURROUND_SOUND_ENABLE;
	else
		temp_reg = S5P_SDO_CGMS625_SURROUND_SOUND_DISABLE;

	if (cgmsa625.copyright)
		temp_reg |= S5P_SDO_CGMS625_COPYRIGHT;
	else
		temp_reg |= S5P_SDO_CGMS625_NO_COPYRIGHT;

	if (cgmsa625.copy_protection)
		temp_reg |= S5P_SDO_CGMS625_COPY_RESTRICTED;
	else
		temp_reg |= S5P_SDO_CGMS625_COPY_NOT_RESTRICTED;

	if (cgmsa625.text_subtitles)
		temp_reg |= S5P_SDO_CGMS625_TELETEXT_SUBTITLES;
	else
		temp_reg |= S5P_SDO_CGMS625_TELETEXT_NO_SUBTITLES;

	switch (cgmsa625.open_subtitles) {
	case SDO_625_NO_OPEN_SUBTITLES:
		temp_reg |= S5P_SDO_CGMS625_NO_OPEN_SUBTITLES;
		break;

	case SDO_625_INACT_OPEN_SUBTITLES:
		temp_reg |= S5P_SDO_CGMS625_INACT_OPEN_SUBTITLES;
		break;

	case SDO_625_OUTACT_OPEN_SUBTITLES:
		temp_reg |= S5P_SDO_CGMS625_OUTACT_OPEN_SUBTITLES;
		break;

	default:
		tvout_err("invalid open_subtitles parameter(%d)\n",
			cgmsa625.open_subtitles);
		return -1;
	}

	switch (cgmsa625.camera_film) {
	case SDO_625_CAMERA:
		temp_reg |= S5P_SDO_CGMS625_CAMERA;
		break;

	case SDO_625_FILM:
		temp_reg |= S5P_SDO_CGMS625_FILM;
		break;

	default:
		tvout_err("invalid camera_film parameter(%d)\n",
			cgmsa625.camera_film);
		return -1;
	}

	switch (cgmsa625.color_encoding) {
	case SDO_625_NORMAL_PAL:
		temp_reg |= S5P_SDO_CGMS625_NORMAL_PAL;
		break;

	case SDO_625_MOTION_ADAPTIVE_COLORPLUS:
		temp_reg |= S5P_SDO_CGMS625_MOTION_ADAPTIVE_COLORPLUS;
		break;

	default:
		tvout_err("invalid color_encoding parameter(%d)\n",
			cgmsa625.color_encoding);
		return -1;
	}

	if (cgmsa625.helper_signal)
		temp_reg |= S5P_SDO_CGMS625_HELPER_SIG;
	else
		temp_reg |= S5P_SDO_CGMS625_HELPER_NO_SIG;

	switch (cgmsa625.display_ratio) {
	case SDO_625_4_3_FULL_576:
		temp_reg |= S5P_SDO_CGMS625_4_3_FULL_576;
		break;

	case SDO_625_14_9_LETTERBOX_CENTER_504:
		temp_reg |= S5P_SDO_CGMS625_14_9_LETTERBOX_CENTER_504;
		break;

	case SDO_625_14_9_LETTERBOX_TOP_504:
		temp_reg |= S5P_SDO_CGMS625_14_9_LETTERBOX_TOP_504;
		break;

	case SDO_625_16_9_LETTERBOX_CENTER_430:
		temp_reg |= S5P_SDO_CGMS625_16_9_LETTERBOX_CENTER_430;
		break;

	case SDO_625_16_9_LETTERBOX_TOP_430:
		temp_reg |= S5P_SDO_CGMS625_16_9_LETTERBOX_TOP_430;
		break;

	case SDO_625_16_9_LETTERBOX_CENTER:
		temp_reg |= S5P_SDO_CGMS625_16_9_LETTERBOX_CENTER;
		break;

	case SDO_625_14_9_FULL_CENTER_576:
		temp_reg |= S5P_SDO_CGMS625_14_9_FULL_CENTER_576;
		break;

	case SDO_625_16_9_ANAMORPIC_576:
		temp_reg |= S5P_SDO_CGMS625_16_9_ANAMORPIC_576;
		break;

	default:
		tvout_err("invalid display_ratio parameter(%d)\n",
			cgmsa625.display_ratio);
		return -1;
	}

	writel(temp_reg, sdo_base + S5P_SDO_CGMS625);

	return 0;
}

int s5p_sdo_set_display_mode(
		enum s5p_tvout_disp_mode disp_mode, enum s5p_sdo_order order)
{
	u32 temp_reg = 0;

	tvout_dbg("%d, %d\n", disp_mode, order);

	switch (disp_mode) {
	case TVOUT_NTSC_M:
		temp_reg |= S5P_SDO_NTSC_M;
		s5p_sdo_set_video_scale_cfg(
			SDO_LEVEL_75IRE,
			SDO_VTOS_RATIO_10_4);

		s5p_sdo_set_antialias_filter_coeff_default(
			SDO_LEVEL_75IRE,
			SDO_VTOS_RATIO_10_4);
		break;

	case TVOUT_PAL_BDGHI:
		temp_reg |= S5P_SDO_PAL_BGHID;
		s5p_sdo_set_video_scale_cfg(
			SDO_LEVEL_0IRE,
			SDO_VTOS_RATIO_7_3);

		s5p_sdo_set_antialias_filter_coeff_default(
			SDO_LEVEL_0IRE,
			SDO_VTOS_RATIO_7_3);
		break;

	case TVOUT_PAL_M:
		temp_reg |= S5P_SDO_PAL_M;
		s5p_sdo_set_video_scale_cfg(
			SDO_LEVEL_0IRE,
			SDO_VTOS_RATIO_7_3);

		s5p_sdo_set_antialias_filter_coeff_default(
			SDO_LEVEL_0IRE,
			SDO_VTOS_RATIO_7_3);
		break;

	case TVOUT_PAL_N:
		temp_reg |= S5P_SDO_PAL_N;
		s5p_sdo_set_video_scale_cfg(
			SDO_LEVEL_0IRE,
			SDO_VTOS_RATIO_7_3);

		s5p_sdo_set_antialias_filter_coeff_default(
			SDO_LEVEL_75IRE,
			SDO_VTOS_RATIO_10_4);
		break;

	case TVOUT_PAL_NC:
		temp_reg |= S5P_SDO_PAL_NC;
		s5p_sdo_set_video_scale_cfg(
			SDO_LEVEL_0IRE,
			SDO_VTOS_RATIO_7_3);

		s5p_sdo_set_antialias_filter_coeff_default(
			SDO_LEVEL_0IRE,
			SDO_VTOS_RATIO_7_3);
		break;

	case TVOUT_PAL_60:
		temp_reg |= S5P_SDO_PAL_60;
		s5p_sdo_set_video_scale_cfg(
			SDO_LEVEL_0IRE,
			SDO_VTOS_RATIO_7_3);
		s5p_sdo_set_antialias_filter_coeff_default(
			SDO_LEVEL_0IRE,
			SDO_VTOS_RATIO_7_3);
		break;

	case TVOUT_NTSC_443:
		temp_reg |= S5P_SDO_NTSC_443;
		s5p_sdo_set_video_scale_cfg(
			SDO_LEVEL_75IRE,
			SDO_VTOS_RATIO_10_4);
		s5p_sdo_set_antialias_filter_coeff_default(
			SDO_LEVEL_75IRE,
			SDO_VTOS_RATIO_10_4);
		break;

	default:
		tvout_err("invalid disp_mode parameter(%d)\n", disp_mode);
		return -1;
	}

	temp_reg |= S5P_SDO_COMPOSITE | S5P_SDO_INTERLACED;

	switch (order) {

	case SDO_O_ORDER_COMPOSITE_CVBS_Y_C:
		temp_reg |= S5P_SDO_DAC2_CVBS | S5P_SDO_DAC1_Y |
				S5P_SDO_DAC0_C;
		break;

	case SDO_O_ORDER_COMPOSITE_CVBS_C_Y:
		temp_reg |= S5P_SDO_DAC2_CVBS | S5P_SDO_DAC1_C |
				S5P_SDO_DAC0_Y;
		break;

	case SDO_O_ORDER_COMPOSITE_Y_C_CVBS:
		temp_reg |= S5P_SDO_DAC2_Y | S5P_SDO_DAC1_C |
				S5P_SDO_DAC0_CVBS;
		break;

	case SDO_O_ORDER_COMPOSITE_Y_CVBS_C:
		temp_reg |= S5P_SDO_DAC2_Y | S5P_SDO_DAC1_CVBS |
				S5P_SDO_DAC0_C;
		break;

	case SDO_O_ORDER_COMPOSITE_C_CVBS_Y:
		temp_reg |= S5P_SDO_DAC2_C | S5P_SDO_DAC1_CVBS |
				S5P_SDO_DAC0_Y;
		break;

	case SDO_O_ORDER_COMPOSITE_C_Y_CVBS:
		temp_reg |= S5P_SDO_DAC2_C | S5P_SDO_DAC1_Y |
				S5P_SDO_DAC0_CVBS;
		break;

	default:
		tvout_err("invalid order parameter(%d)\n", order);
		return -1;
	}

	writel(temp_reg, sdo_base + S5P_SDO_CONFIG);

	return 0;
}

void s5p_sdo_clock_on(bool on)
{
	tvout_dbg("%d\n", on);

	if (on)
		writel(S5P_SDO_TVOUT_CLOCK_ON, sdo_base + S5P_SDO_CLKCON);
	else {
		mdelay(100);

		writel(S5P_SDO_TVOUT_CLOCK_OFF, sdo_base + S5P_SDO_CLKCON);
	}
}

void s5p_sdo_dac_on(bool on)
{
	tvout_dbg("%d\n", on);

	if (on) {
		writel(S5P_SDO_POWER_ON_DAC, sdo_base + S5P_SDO_DAC);

		writel(S5P_DAC_ENABLE, S5P_DAC_CONTROL);
	} else {
		writel(S5P_DAC_DISABLE, S5P_DAC_CONTROL);

		writel(S5P_SDO_POWER_DOWN_DAC, sdo_base + S5P_SDO_DAC);
	}
}

void s5p_sdo_sw_reset(bool active)
{
	tvout_dbg("%d\n", active);

	if (active)
		writel(readl(sdo_base + S5P_SDO_CLKCON) |
			S5P_SDO_TVOUT_SW_RESET,
				sdo_base + S5P_SDO_CLKCON);
	else
		writel(readl(sdo_base + S5P_SDO_CLKCON) &
			~S5P_SDO_TVOUT_SW_RESET,
				sdo_base + S5P_SDO_CLKCON);
}

void s5p_sdo_set_interrupt_enable(bool vsync_intc_en)
{
	tvout_dbg("%d\n", vsync_intc_en);

	if (vsync_intc_en)
		writel(readl(sdo_base + S5P_SDO_IRQMASK) &
			~S5P_SDO_VSYNC_IRQ_DISABLE,
				sdo_base + S5P_SDO_IRQMASK);
	else
		writel(readl(sdo_base + S5P_SDO_IRQMASK) |
			S5P_SDO_VSYNC_IRQ_DISABLE,
				sdo_base + S5P_SDO_IRQMASK);
}

void s5p_sdo_clear_interrupt_pending(void)
{
	writel(readl(sdo_base + S5P_SDO_IRQ) | S5P_SDO_VSYNC_IRQ_PEND,
			sdo_base + S5P_SDO_IRQ);
}

void s5p_sdo_init(void __iomem *addr)
{
	sdo_base = addr;
}
