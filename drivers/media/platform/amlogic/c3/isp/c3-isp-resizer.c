// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2024 Amlogic, Inc. All rights reserved
 */

#include <linux/pm_runtime.h>

#include "c3-isp-common.h"
#include "c3-isp-regs.h"

#define C3_ISP_RSZ_DEF_PAD_FMT		MEDIA_BUS_FMT_YUV10_1X30
#define C3_ISP_DISP_REG(base, id)	((base) + (id) * 0x400)
#define C3_ISP_PPS_LUT_H_NUM		33
#define C3_ISP_PPS_LUT_CTYPE_0		0
#define C3_ISP_PPS_LUT_CTYPE_2		2
#define C3_ISP_SCL_EN			1
#define C3_ISP_SCL_DIS			0

/*
 * struct c3_isp_rsz_format_info - ISP resizer format information
 *
 * @mbus_code: the mbus code
 * @pads: bitmask detailing valid pads for this mbus_code
 * @is_raw: the raw format flag of mbus code
 */
struct c3_isp_rsz_format_info {
	u32 mbus_code;
	u32 pads;
	bool is_raw;
};

static const struct c3_isp_rsz_format_info c3_isp_rsz_fmts[] = {
	/* RAW formats */
	{
		.mbus_code	= MEDIA_BUS_FMT_SBGGR16_1X16,
		.pads		= BIT(C3_ISP_RSZ_PAD_SINK)
				| BIT(C3_ISP_RSZ_PAD_SOURCE),
		.is_raw = true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG16_1X16,
		.pads		= BIT(C3_ISP_RSZ_PAD_SINK)
				| BIT(C3_ISP_RSZ_PAD_SOURCE),
		.is_raw = true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG16_1X16,
		.pads		= BIT(C3_ISP_RSZ_PAD_SINK)
				| BIT(C3_ISP_RSZ_PAD_SOURCE),
		.is_raw = true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB16_1X16,
		.pads		= BIT(C3_ISP_RSZ_PAD_SINK)
				| BIT(C3_ISP_RSZ_PAD_SOURCE),
		.is_raw = true,
	},
	/* YUV formats */
	{
		.mbus_code	= MEDIA_BUS_FMT_YUV10_1X30,
		.pads		= BIT(C3_ISP_RSZ_PAD_SINK)
				| BIT(C3_ISP_RSZ_PAD_SOURCE),
		.is_raw = false,
	},
};

/*
 * struct c3_isp_pps_io_size - ISP scaler input and output size
 *
 * @thsize: input horizontal size of after preprocessing
 * @tvsize: input vertical size of after preprocessing
 * @ohsize: output horizontal size
 * @ovsize: output vertical size
 * @ihsize: input horizontal size
 * @max_hsize: maximum horizontal size
 */
struct c3_isp_pps_io_size {
	u32 thsize;
	u32 tvsize;
	u32 ohsize;
	u32 ovsize;
	u32 ihsize;
	u32 max_hsize;
};

/* The normal parameters of pps module */
static const int c3_isp_pps_lut[C3_ISP_PPS_LUT_H_NUM][4] =  {
	{  0, 511,   0,   0}, { -5, 511,   5,   0}, {-10, 511,  11,   0},
	{-14, 510,  17,  -1}, {-18, 508,  23,  -1}, {-22, 506,  29,  -1},
	{-25, 503,  36,  -2}, {-28, 500,  43,  -3}, {-32, 496,  51,  -3},
	{-34, 491,  59,  -4}, {-37, 487,  67,  -5}, {-39, 482,  75,  -6},
	{-41, 476,  84,  -7}, {-42, 470,  92,  -8}, {-44, 463, 102,  -9},
	{-45, 456, 111, -10}, {-45, 449, 120, -12}, {-47, 442, 130, -13},
	{-47, 434, 140, -15}, {-47, 425, 151, -17}, {-47, 416, 161, -18},
	{-47, 407, 172, -20}, {-47, 398, 182, -21}, {-47, 389, 193, -23},
	{-46, 379, 204, -25}, {-45, 369, 215, -27}, {-44, 358, 226, -28},
	{-43, 348, 237, -30}, {-43, 337, 249, -31}, {-41, 326, 260, -33},
	{-40, 316, 271, -35}, {-39, 305, 282, -36}, {-37, 293, 293, -37}
};

static const struct c3_isp_rsz_format_info
*rsz_find_format_by_code(u32 code, u32 pad)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(c3_isp_rsz_fmts); i++) {
		const struct c3_isp_rsz_format_info *info = &c3_isp_rsz_fmts[i];

		if (info->mbus_code == code && info->pads & BIT(pad))
			return info;
	}

	return NULL;
}

static const struct c3_isp_rsz_format_info
*rsz_find_format_by_index(u32 index, u32 pad)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(c3_isp_rsz_fmts); i++) {
		const struct c3_isp_rsz_format_info *info = &c3_isp_rsz_fmts[i];

		if (!(info->pads & BIT(pad)))
			continue;

		if (!index)
			return info;

		index--;
	}

	return NULL;
}

static void c3_isp_rsz_pps_size(struct c3_isp_resizer *rsz,
				struct c3_isp_pps_io_size *io_size)
{
	int thsize = io_size->thsize;
	int tvsize = io_size->tvsize;
	u32 ohsize = io_size->ohsize;
	u32 ovsize = io_size->ovsize;
	u32 ihsize = io_size->ihsize;
	u32 max_hsize = io_size->max_hsize;
	int h_int;
	int v_int;
	int h_fract;
	int v_fract;
	int yuv444to422_en;

	/* Calculate the integer part of horizonal scaler step */
	h_int = thsize / ohsize;

	/* Calculate the vertical part of horizonal scaler step */
	v_int = tvsize / ovsize;

	/*
	 * Calculate the fraction part of horizonal scaler step.
	 * step_h_fraction = (source / dest) * 2^24,
	 * so step_h_fraction = ((source << 12) / dest) << 12.
	 */
	h_fract = ((thsize << 12) / ohsize) << 12;

	/*
	 * Calculate the fraction part of vertical scaler step
	 * step_v_fraction = (source / dest) * 2^24,
	 * so step_v_fraction = ((source << 12) / dest) << 12.
	 */
	v_fract = ((tvsize << 12) / ovsize) << 12;

	yuv444to422_en = ihsize > (max_hsize / 2) ? 1 : 0;

	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_444TO422, rsz->id),
			   DISP0_PPS_444TO422_EN_MASK,
			   DISP0_PPS_444TO422_EN(yuv444to422_en));

	c3_isp_write(rsz->isp,
		     C3_ISP_DISP_REG(DISP0_PPS_VSC_START_PHASE_STEP, rsz->id),
		     DISP0_PPS_VSC_START_PHASE_STEP_VERT_FRAC(v_fract) |
		     DISP0_PPS_VSC_START_PHASE_STEP_VERT_INTE(v_int));

	c3_isp_write(rsz->isp,
		     C3_ISP_DISP_REG(DISP0_PPS_HSC_START_PHASE_STEP, rsz->id),
		     DISP0_PPS_HSC_START_PHASE_STEP_HORIZ_FRAC(h_fract) |
		     DISP0_PPS_HSC_START_PHASE_STEP_HORIZ_INTE(h_int));
}

static void c3_isp_rsz_pps_lut(struct c3_isp_resizer *rsz, u32 ctype)
{
	unsigned int i;

	/*
	 * Default value of this register is 0, so only need to set
	 * SCALE_LUMA_COEF_S11_MODE and SCALE_LUMA_CTYPE. This register needs
	 * to be written in one time.
	 */
	c3_isp_write(rsz->isp,
		     C3_ISP_DISP_REG(ISP_SCALE0_COEF_IDX_LUMA, rsz->id),
		     ISP_SCALE0_COEF_IDX_LUMA_COEF_S11_MODE_EN |
		     ISP_SCALE0_COEF_IDX_LUMA_CTYPE(ctype));

	for (i = 0; i < C3_ISP_PPS_LUT_H_NUM; i++) {
		c3_isp_write(rsz->isp,
			     C3_ISP_DISP_REG(ISP_SCALE0_COEF_LUMA, rsz->id),
			     ISP_SCALE0_COEF_LUMA_DATA0(c3_isp_pps_lut[i][0]) |
			     ISP_SCALE0_COEF_LUMA_DATA1(c3_isp_pps_lut[i][1]));
		c3_isp_write(rsz->isp,
			     C3_ISP_DISP_REG(ISP_SCALE0_COEF_LUMA, rsz->id),
			     ISP_SCALE0_COEF_LUMA_DATA0(c3_isp_pps_lut[i][2]) |
			     ISP_SCALE0_COEF_LUMA_DATA1(c3_isp_pps_lut[i][3]));
	}

	/*
	 * Default value of this register is 0, so only need to set
	 * SCALE_CHRO_COEF_S11_MODE and SCALE_CHRO_CTYPE. This register needs
	 * to be written in one time.
	 */
	c3_isp_write(rsz->isp,
		     C3_ISP_DISP_REG(ISP_SCALE0_COEF_IDX_CHRO, rsz->id),
		     ISP_SCALE0_COEF_IDX_CHRO_COEF_S11_MODE_EN |
		     ISP_SCALE0_COEF_IDX_CHRO_CTYPE(ctype));

	for (i = 0; i < C3_ISP_PPS_LUT_H_NUM; i++) {
		c3_isp_write(rsz->isp,
			     C3_ISP_DISP_REG(ISP_SCALE0_COEF_CHRO, rsz->id),
			     ISP_SCALE0_COEF_CHRO_DATA0(c3_isp_pps_lut[i][0]) |
			     ISP_SCALE0_COEF_CHRO_DATA1(c3_isp_pps_lut[i][1]));
		c3_isp_write(rsz->isp,
			     C3_ISP_DISP_REG(ISP_SCALE0_COEF_CHRO, rsz->id),
			     ISP_SCALE0_COEF_CHRO_DATA0(c3_isp_pps_lut[i][2]) |
			     ISP_SCALE0_COEF_CHRO_DATA1(c3_isp_pps_lut[i][3]));
	}
}

static void c3_isp_rsz_pps_disable(struct c3_isp_resizer *rsz)
{
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_HSC_EN_MASK,
			   DISP0_PPS_SCALE_EN_HSC_DIS);
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_VSC_EN_MASK,
			   DISP0_PPS_SCALE_EN_VSC_DIS);
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_PREVSC_EN_MASK,
			   DISP0_PPS_SCALE_EN_PREVSC_DIS);
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_PREHSC_EN_MASK,
			   DISP0_PPS_SCALE_EN_PREHSC_DIS);
}

static int c3_isp_rsz_pps_enable(struct c3_isp_resizer *rsz,
				 struct v4l2_subdev_state *state)
{
	struct v4l2_rect *crop;
	struct v4l2_rect *cmps;
	int max_hsize;
	int hsc_en, vsc_en;
	int preh_en, prev_en;
	u32 prehsc_rate;
	u32 prevsc_flt_num;
	int pre_vscale_max_hsize;
	u32 ihsize_after_pre_hsc;
	u32 ihsize_after_pre_hsc_alt;
	u32 vsc_tap_num_alt;
	u32 ihsize;
	u32 ivsize;
	struct c3_isp_pps_io_size io_size;

	crop = v4l2_subdev_state_get_crop(state, C3_ISP_RSZ_PAD_SINK);
	cmps = v4l2_subdev_state_get_compose(state, C3_ISP_RSZ_PAD_SINK);

	ihsize = crop->width;
	ivsize = crop->height;

	hsc_en = (ihsize == cmps->width) ? C3_ISP_SCL_DIS : C3_ISP_SCL_EN;
	vsc_en = (ivsize == cmps->height) ? C3_ISP_SCL_DIS : C3_ISP_SCL_EN;

	/* Disable pps when there no need to use pps */
	if (!hsc_en && !vsc_en) {
		c3_isp_rsz_pps_disable(rsz);
		return 0;
	}

	/* Pre-scale needs to be enable if the down scaling factor exceeds 4 */
	preh_en = (ihsize > cmps->width * 4) ? C3_ISP_SCL_EN : C3_ISP_SCL_DIS;
	prev_en = (ivsize > cmps->height * 4) ? C3_ISP_SCL_EN : C3_ISP_SCL_DIS;

	if (rsz->id == C3_ISP_RSZ_2) {
		max_hsize = C3_ISP_MAX_WIDTH;

		/* Set vertical tap number */
		prevsc_flt_num = 4;

		/* Set the max hsize of pre-vertical scale */
		pre_vscale_max_hsize = max_hsize / 2;
	} else {
		max_hsize = C3_ISP_DEFAULT_WIDTH;

		/* Set vertical tap number and the max hsize of pre-vertical */
		if (ihsize > (max_hsize / 2) &&
		    ihsize <= max_hsize && prev_en) {
			prevsc_flt_num = 2;
			pre_vscale_max_hsize = max_hsize;
		} else {
			prevsc_flt_num = 4;
			pre_vscale_max_hsize = max_hsize / 2;
		}
	}

	/*
	 * Set pre-horizonal scale rate and the hsize of after
	 * pre-horizonal scale.
	 */
	if (preh_en) {
		prehsc_rate = 1;
		ihsize_after_pre_hsc = DIV_ROUND_UP(ihsize, 2);
	} else {
		prehsc_rate = 0;
		ihsize_after_pre_hsc = ihsize;
	}

	/* Change pre-horizonal scale rate */
	if (prev_en && ihsize_after_pre_hsc >= pre_vscale_max_hsize)
		prehsc_rate += 1;

	/* Set the actual hsize of after pre-horizonal scale */
	if (preh_en)
		ihsize_after_pre_hsc_alt =
			DIV_ROUND_UP(ihsize, 1 << prehsc_rate);
	else
		ihsize_after_pre_hsc_alt = ihsize;

	/* Set vertical scaler bank length */
	if (ihsize_after_pre_hsc_alt <= (max_hsize / 2))
		vsc_tap_num_alt = 4;
	else if (ihsize_after_pre_hsc_alt <= max_hsize)
		vsc_tap_num_alt = prev_en ? 2 : 4;
	else
		vsc_tap_num_alt = prev_en ? 4 : 2;

	io_size.thsize = ihsize_after_pre_hsc_alt;
	io_size.tvsize = prev_en ? DIV_ROUND_UP(ivsize, 2) : ivsize;
	io_size.ohsize = cmps->width;
	io_size.ovsize = cmps->height;
	io_size.ihsize = ihsize;
	io_size.max_hsize = max_hsize;

	c3_isp_rsz_pps_size(rsz, &io_size);
	c3_isp_rsz_pps_lut(rsz, C3_ISP_PPS_LUT_CTYPE_0);
	c3_isp_rsz_pps_lut(rsz, C3_ISP_PPS_LUT_CTYPE_2);

	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_VSC_TAP_NUM_MASK,
			   DISP0_PPS_SCALE_EN_VSC_TAP_NUM(vsc_tap_num_alt));
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_PREVSC_FLT_NUM_MASK,
			   DISP0_PPS_SCALE_EN_PREVSC_FLT_NUM(prevsc_flt_num));

	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_PREVSC_RATE_MASK,
			   DISP0_PPS_SCALE_EN_PREVSC_RATE(prev_en));
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_PREHSC_RATE_MASK,
			   DISP0_PPS_SCALE_EN_PREHSC_RATE(prehsc_rate));

	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_HSC_EN_MASK,
			   DISP0_PPS_SCALE_EN_HSC_EN(hsc_en));
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_VSC_EN_MASK,
			   DISP0_PPS_SCALE_EN_VSC_EN(vsc_en));
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_PREVSC_EN_MASK,
			   DISP0_PPS_SCALE_EN_PREVSC_EN(prev_en));
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_PREHSC_EN_MASK,
			   DISP0_PPS_SCALE_EN_PREHSC_EN(preh_en));

	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_HSC_NOR_RS_BITS_MASK,
			   DISP0_PPS_SCALE_EN_HSC_NOR_RS_BITS(9));
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_PPS_SCALE_EN, rsz->id),
			   DISP0_PPS_SCALE_EN_VSC_NOR_RS_BITS_MASK,
			   DISP0_PPS_SCALE_EN_VSC_NOR_RS_BITS(9));

	return 0;
}

static void c3_isp_rsz_start(struct c3_isp_resizer *rsz,
			     struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_mbus_framefmt *src_fmt;
	const struct c3_isp_rsz_format_info *rsz_fmt;
	struct v4l2_rect *sink_crop;
	u32 mask;
	u32 val;

	sink_fmt = v4l2_subdev_state_get_format(state, C3_ISP_RSZ_PAD_SINK);
	sink_crop = v4l2_subdev_state_get_crop(state, C3_ISP_RSZ_PAD_SINK);
	src_fmt = v4l2_subdev_state_get_format(state, C3_ISP_RSZ_PAD_SOURCE);
	rsz_fmt = rsz_find_format_by_code(sink_fmt->code, C3_ISP_RSZ_PAD_SINK);

	if (rsz->id == C3_ISP_RSZ_0) {
		mask = ISP_TOP_DISPIN_SEL_DISP0_MASK;
		val = rsz_fmt->is_raw ? ISP_TOP_DISPIN_SEL_DISP0_MIPI_OUT
				      : ISP_TOP_DISPIN_SEL_DISP0_CORE_OUT;
	} else if (rsz->id == C3_ISP_RSZ_1) {
		mask = ISP_TOP_DISPIN_SEL_DISP1_MASK;
		val = rsz_fmt->is_raw ? ISP_TOP_DISPIN_SEL_DISP1_MIPI_OUT
				      : ISP_TOP_DISPIN_SEL_DISP1_CORE_OUT;
	} else {
		mask = ISP_TOP_DISPIN_SEL_DISP2_MASK;
		val = rsz_fmt->is_raw ? ISP_TOP_DISPIN_SEL_DISP2_MIPI_OUT
				      : ISP_TOP_DISPIN_SEL_DISP2_CORE_OUT;
	}

	c3_isp_update_bits(rsz->isp, ISP_TOP_DISPIN_SEL, mask, val);

	c3_isp_write(rsz->isp, C3_ISP_DISP_REG(ISP_DISP0_TOP_IN_SIZE, rsz->id),
		     ISP_DISP0_TOP_IN_SIZE_HSIZE(sink_fmt->width) |
		     ISP_DISP0_TOP_IN_SIZE_VSIZE(sink_fmt->height));

	c3_isp_write(rsz->isp, C3_ISP_DISP_REG(DISP0_TOP_CRP2_START, rsz->id),
		     DISP0_TOP_CRP2_START_V_START(sink_crop->top) |
		     DISP0_TOP_CRP2_START_H_START(sink_crop->left));

	c3_isp_write(rsz->isp, C3_ISP_DISP_REG(DISP0_TOP_CRP2_SIZE, rsz->id),
		     DISP0_TOP_CRP2_SIZE_V_SIZE(sink_crop->height) |
		     DISP0_TOP_CRP2_SIZE_H_SIZE(sink_crop->width));

	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_TOP_TOP_CTRL, rsz->id),
			   DISP0_TOP_TOP_CTRL_CROP2_EN_MASK,
			   DISP0_TOP_TOP_CTRL_CROP2_EN);

	if (!rsz_fmt->is_raw)
		c3_isp_rsz_pps_enable(rsz, state);

	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_TOP_OUT_SIZE, rsz->id),
			   DISP0_TOP_OUT_SIZE_SCL_OUT_HEIGHT_MASK,
			   DISP0_TOP_OUT_SIZE_SCL_OUT_HEIGHT(src_fmt->height));
	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_TOP_OUT_SIZE, rsz->id),
			   DISP0_TOP_OUT_SIZE_SCL_OUT_WIDTH_MASK,
			   DISP0_TOP_OUT_SIZE_SCL_OUT_WIDTH(src_fmt->width));

	if (rsz->id == C3_ISP_RSZ_0) {
		mask = ISP_TOP_PATH_EN_DISP0_EN_MASK;
		val = ISP_TOP_PATH_EN_DISP0_EN;
	} else if (rsz->id == C3_ISP_RSZ_1) {
		mask = ISP_TOP_PATH_EN_DISP1_EN_MASK;
		val = ISP_TOP_PATH_EN_DISP1_EN;
	} else {
		mask = ISP_TOP_PATH_EN_DISP2_EN_MASK;
		val = ISP_TOP_PATH_EN_DISP2_EN;
	}

	c3_isp_update_bits(rsz->isp, ISP_TOP_PATH_EN, mask, val);
}

static void c3_isp_rsz_stop(struct c3_isp_resizer *rsz)
{
	u32 mask;
	u32 val;

	if (rsz->id == C3_ISP_RSZ_0) {
		mask = ISP_TOP_PATH_EN_DISP0_EN_MASK;
		val = ISP_TOP_PATH_EN_DISP0_DIS;
	} else if (rsz->id == C3_ISP_RSZ_1) {
		mask = ISP_TOP_PATH_EN_DISP1_EN_MASK;
		val = ISP_TOP_PATH_EN_DISP1_DIS;
	} else {
		mask = ISP_TOP_PATH_EN_DISP2_EN_MASK;
		val = ISP_TOP_PATH_EN_DISP2_DIS;
	}

	c3_isp_update_bits(rsz->isp, ISP_TOP_PATH_EN, mask, val);

	c3_isp_update_bits(rsz->isp,
			   C3_ISP_DISP_REG(DISP0_TOP_TOP_CTRL, rsz->id),
			   DISP0_TOP_TOP_CTRL_CROP2_EN_MASK,
			   DISP0_TOP_TOP_CTRL_CROP2_DIS);

	c3_isp_rsz_pps_disable(rsz);
}

static int c3_isp_rsz_enable_streams(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     u32 pad, u64 streams_mask)
{
	struct c3_isp_resizer *rsz = v4l2_get_subdevdata(sd);

	c3_isp_rsz_start(rsz, state);

	c3_isp_params_pre_cfg(rsz->isp);
	c3_isp_stats_pre_cfg(rsz->isp);

	return v4l2_subdev_enable_streams(rsz->src_sd, rsz->src_pad, BIT(0));
}

static int c3_isp_rsz_disable_streams(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      u32 pad, u64 streams_mask)
{
	struct c3_isp_resizer *rsz = v4l2_get_subdevdata(sd);

	c3_isp_rsz_stop(rsz);

	return v4l2_subdev_disable_streams(rsz->src_sd, rsz->src_pad, BIT(0));
}

static int c3_isp_rsz_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	const struct c3_isp_rsz_format_info *info;

	info = rsz_find_format_by_index(code->index, code->pad);
	if (!info)
		return -EINVAL;

	code->code = info->mbus_code;

	return 0;
}

static void c3_isp_rsz_set_sink_fmt(struct v4l2_subdev_state *state,
				    struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_mbus_framefmt *src_fmt;
	struct v4l2_rect *sink_crop;
	struct v4l2_rect *sink_cmps;
	const struct c3_isp_rsz_format_info *rsz_fmt;

	sink_fmt = v4l2_subdev_state_get_format(state, format->pad);
	sink_crop = v4l2_subdev_state_get_crop(state, format->pad);
	sink_cmps = v4l2_subdev_state_get_compose(state, format->pad);
	src_fmt = v4l2_subdev_state_get_format(state, C3_ISP_RSZ_PAD_SOURCE);

	rsz_fmt = rsz_find_format_by_code(format->format.code, format->pad);
	if (rsz_fmt)
		sink_fmt->code = format->format.code;
	else
		sink_fmt->code = C3_ISP_RSZ_DEF_PAD_FMT;

	sink_fmt->width = clamp_t(u32, format->format.width,
				  C3_ISP_MIN_WIDTH, C3_ISP_MAX_WIDTH);
	sink_fmt->height = clamp_t(u32, format->format.height,
				   C3_ISP_MIN_HEIGHT, C3_ISP_MAX_HEIGHT);
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;

	if (rsz_fmt && rsz_fmt->is_raw) {
		sink_fmt->colorspace = V4L2_COLORSPACE_RAW;
		sink_fmt->xfer_func = V4L2_XFER_FUNC_NONE;
		sink_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	} else {
		sink_fmt->colorspace = V4L2_COLORSPACE_SRGB;
		sink_fmt->xfer_func = V4L2_XFER_FUNC_SRGB;
		sink_fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE;
	}

	sink_crop->width = sink_fmt->width;
	sink_crop->height = sink_fmt->height;
	sink_crop->left = 0;
	sink_crop->top = 0;

	sink_cmps->width = sink_crop->width;
	sink_cmps->height = sink_crop->height;
	sink_cmps->left = 0;
	sink_cmps->top = 0;

	src_fmt->code = sink_fmt->code;
	src_fmt->width = sink_cmps->width;
	src_fmt->height = sink_cmps->height;

	format->format = *sink_fmt;
}

static void c3_isp_rsz_set_source_fmt(struct v4l2_subdev_state *state,
				      struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *src_fmt;
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_rect *sink_cmps;
	const struct c3_isp_rsz_format_info *rsz_fmt;

	src_fmt = v4l2_subdev_state_get_format(state, format->pad);
	sink_fmt = v4l2_subdev_state_get_format(state, C3_ISP_RSZ_PAD_SINK);
	sink_cmps = v4l2_subdev_state_get_compose(state, C3_ISP_RSZ_PAD_SINK);

	src_fmt->code = sink_fmt->code;
	src_fmt->width = sink_cmps->width;
	src_fmt->height = sink_cmps->height;
	src_fmt->field = V4L2_FIELD_NONE;
	src_fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;

	rsz_fmt = rsz_find_format_by_code(src_fmt->code, format->pad);
	if (rsz_fmt->is_raw) {
		src_fmt->colorspace = V4L2_COLORSPACE_RAW;
		src_fmt->xfer_func = V4L2_XFER_FUNC_NONE;
		src_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	} else {
		src_fmt->colorspace = V4L2_COLORSPACE_SRGB;
		src_fmt->xfer_func = V4L2_XFER_FUNC_SRGB;
		src_fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE;
	}

	format->format = *src_fmt;
}

static int c3_isp_rsz_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_format *format)
{
	if (format->pad == C3_ISP_RSZ_PAD_SINK)
		c3_isp_rsz_set_sink_fmt(state, format);
	else
		c3_isp_rsz_set_source_fmt(state, format);

	return 0;
}

static int c3_isp_rsz_get_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_selection *sel)
{
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_rect *crop;
	struct v4l2_rect *cmps;

	if (sel->pad == C3_ISP_RSZ_PAD_SOURCE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		fmt = v4l2_subdev_state_get_format(state, sel->pad);
		sel->r.width = fmt->width;
		sel->r.height = fmt->height;
		sel->r.left = 0;
		sel->r.top = 0;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		crop = v4l2_subdev_state_get_crop(state, sel->pad);
		sel->r.width = crop->width;
		sel->r.height = crop->height;
		sel->r.left = 0;
		sel->r.top = 0;
		break;
	case V4L2_SEL_TGT_CROP:
		crop = v4l2_subdev_state_get_crop(state, sel->pad);
		sel->r = *crop;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		cmps = v4l2_subdev_state_get_compose(state, sel->pad);
		sel->r = *cmps;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int c3_isp_rsz_set_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_selection *sel)
{
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_mbus_framefmt *src_fmt;
	struct v4l2_rect *crop;
	struct v4l2_rect *cmps;

	if (sel->pad == C3_ISP_RSZ_PAD_SOURCE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		fmt = v4l2_subdev_state_get_format(state, sel->pad);
		crop = v4l2_subdev_state_get_crop(state, sel->pad);
		cmps = v4l2_subdev_state_get_compose(state, sel->pad);
		src_fmt = v4l2_subdev_state_get_format(state,
						       C3_ISP_RSZ_PAD_SOURCE);

		sel->r.left = clamp_t(s32, sel->r.left, 0, fmt->width - 1);
		sel->r.top = clamp_t(s32, sel->r.top, 0, fmt->height - 1);
		sel->r.width = clamp(sel->r.width, C3_ISP_MIN_WIDTH,
				     fmt->width - sel->r.left);
		sel->r.height = clamp(sel->r.height, C3_ISP_MIN_HEIGHT,
				      fmt->height - sel->r.top);

		crop->width = ALIGN(sel->r.width, 2);
		crop->height = ALIGN(sel->r.height, 2);
		crop->left = sel->r.left;
		crop->top = sel->r.top;

		*cmps = *crop;

		src_fmt->code = fmt->code;
		src_fmt->width = cmps->width;
		src_fmt->height = cmps->height;

		sel->r = *crop;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		crop = v4l2_subdev_state_get_crop(state, sel->pad);
		cmps = v4l2_subdev_state_get_compose(state, sel->pad);

		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = clamp(sel->r.width, C3_ISP_MIN_WIDTH,
				     crop->width);
		sel->r.height = clamp(sel->r.height, C3_ISP_MIN_HEIGHT,
				      crop->height);

		cmps->width = ALIGN(sel->r.width, 2);
		cmps->height = ALIGN(sel->r.height, 2);
		cmps->left = sel->r.left;
		cmps->top = sel->r.top;

		sel->r = *cmps;

		fmt = v4l2_subdev_state_get_format(state,
						   C3_ISP_RSZ_PAD_SOURCE);
		fmt->width = cmps->width;
		fmt->height = cmps->height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int c3_isp_rsz_init_state(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_rect *crop;
	struct v4l2_rect *cmps;

	fmt = v4l2_subdev_state_get_format(state, C3_ISP_RSZ_PAD_SINK);
	fmt->width = C3_ISP_DEFAULT_WIDTH;
	fmt->height = C3_ISP_DEFAULT_HEIGHT;
	fmt->field = V4L2_FIELD_NONE;
	fmt->code = C3_ISP_RSZ_DEF_PAD_FMT;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->xfer_func = V4L2_XFER_FUNC_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE;

	crop = v4l2_subdev_state_get_crop(state, C3_ISP_RSZ_PAD_SINK);
	crop->width = C3_ISP_DEFAULT_WIDTH;
	crop->height = C3_ISP_DEFAULT_HEIGHT;
	crop->left = 0;
	crop->top = 0;

	cmps = v4l2_subdev_state_get_compose(state, C3_ISP_RSZ_PAD_SINK);
	cmps->width = C3_ISP_DEFAULT_WIDTH;
	cmps->height = C3_ISP_DEFAULT_HEIGHT;
	cmps->left = 0;
	cmps->top = 0;

	fmt = v4l2_subdev_state_get_format(state, C3_ISP_RSZ_PAD_SOURCE);
	fmt->width = cmps->width;
	fmt->height = cmps->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->code = C3_ISP_RSZ_DEF_PAD_FMT;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->xfer_func = V4L2_XFER_FUNC_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE;

	return 0;
}

static const struct v4l2_subdev_pad_ops c3_isp_rsz_pad_ops = {
	.enum_mbus_code = c3_isp_rsz_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = c3_isp_rsz_set_fmt,
	.get_selection = c3_isp_rsz_get_selection,
	.set_selection = c3_isp_rsz_set_selection,
	.enable_streams = c3_isp_rsz_enable_streams,
	.disable_streams = c3_isp_rsz_disable_streams,
};

static const struct v4l2_subdev_ops c3_isp_rsz_subdev_ops = {
	.pad = &c3_isp_rsz_pad_ops,
};

static const struct v4l2_subdev_internal_ops c3_isp_rsz_internal_ops = {
	.init_state = c3_isp_rsz_init_state,
};

/* Media entity operations */
static const struct media_entity_operations c3_isp_rsz_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int c3_isp_rsz_register(struct c3_isp_resizer *rsz)
{
	struct v4l2_subdev *sd = &rsz->sd;
	int ret;

	v4l2_subdev_init(sd, &c3_isp_rsz_subdev_ops);
	sd->owner = THIS_MODULE;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->internal_ops = &c3_isp_rsz_internal_ops;
	snprintf(sd->name, sizeof(sd->name), "c3-isp-resizer%u", rsz->id);

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	sd->entity.ops = &c3_isp_rsz_entity_ops;

	sd->dev = rsz->isp->dev;
	v4l2_set_subdevdata(sd, rsz);

	rsz->pads[C3_ISP_RSZ_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	rsz->pads[C3_ISP_RSZ_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, C3_ISP_RSZ_PAD_MAX,
				     rsz->pads);
	if (ret)
		return ret;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_device_register_subdev(&rsz->isp->v4l2_dev, sd);
	if (ret)
		goto err_subdev_cleanup;

	return 0;

err_subdev_cleanup:
	v4l2_subdev_cleanup(sd);
err_entity_cleanup:
	media_entity_cleanup(&sd->entity);
	return ret;
}

static void c3_isp_rsz_unregister(struct c3_isp_resizer *rsz)
{
	struct v4l2_subdev *sd = &rsz->sd;

	v4l2_device_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
}

int c3_isp_resizers_register(struct c3_isp_device *isp)
{
	int ret;

	for (unsigned int i = C3_ISP_RSZ_0; i < C3_ISP_NUM_RSZ; i++) {
		struct c3_isp_resizer *rsz = &isp->resizers[i];

		rsz->id = i;
		rsz->isp = isp;
		rsz->src_sd = &isp->core.sd;
		rsz->src_pad = C3_ISP_CORE_PAD_SOURCE_VIDEO_0 + i;

		ret = c3_isp_rsz_register(rsz);
		if (ret) {
			rsz->isp = NULL;
			c3_isp_resizers_unregister(isp);
			return ret;
		}
	}

	return 0;
}

void c3_isp_resizers_unregister(struct c3_isp_device *isp)
{
	for (unsigned int i = C3_ISP_RSZ_0; i < C3_ISP_NUM_RSZ; i++) {
		struct c3_isp_resizer *rsz = &isp->resizers[i];

		if (rsz->isp)
			c3_isp_rsz_unregister(rsz);
	}
}
