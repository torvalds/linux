// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Mentor Graphics Inc.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/sizes.h>
#include "ipu-prv.h"

#define QUANT_MAP(q)					\
	((q) == V4L2_QUANTIZATION_FULL_RANGE ||		\
	 (q) == V4L2_QUANTIZATION_DEFAULT ? 0 : 1)

/* identity matrix */
static const struct ipu_ic_csc_params identity = {
	.coeff = {
		{  128,    0,    0, },
		{    0,  128,    0, },
		{    0,    0,  128, },
	},
	.offset = { 0, 0, 0, },
	.scale = 2,
};

/*
 * RGB full-range to RGB limited-range
 *
 * R_lim = 0.8588 * R_full + 16
 * G_lim = 0.8588 * G_full + 16
 * B_lim = 0.8588 * B_full + 16
 */
static const struct ipu_ic_csc_params rgbf2rgbl = {
	.coeff = {
		{  220,    0,    0, },
		{    0,  220,    0, },
		{    0,    0,  220, },
	},
	.offset = { 64, 64, 64, },
	.scale = 1,
};

/*
 * RGB limited-range to RGB full-range
 *
 * R_full = 1.1644 * (R_lim - 16)
 * G_full = 1.1644 * (G_lim - 16)
 * B_full = 1.1644 * (B_lim - 16)
 */
static const struct ipu_ic_csc_params rgbl2rgbf = {
	.coeff = {
		{  149,    0,    0, },
		{    0,  149,    0, },
		{    0,    0,  149, },
	},
	.offset = { -37, -37, -37, },
	.scale = 2,
};

/*
 * YUV full-range to YUV limited-range
 *
 * Y_lim  = 0.8588 * Y_full + 16
 * Cb_lim = 0.8784 * (Cb_full - 128) + 128
 * Cr_lim = 0.8784 * (Cr_full - 128) + 128
 */
static const struct ipu_ic_csc_params yuvf2yuvl = {
	.coeff = {
		{  220,    0,    0, },
		{    0,  225,    0, },
		{    0,    0,  225, },
	},
	.offset = { 64, 62, 62, },
	.scale = 1,
	.sat = true,
};

/*
 * YUV limited-range to YUV full-range
 *
 * Y_full  = 1.1644 * (Y_lim - 16)
 * Cb_full = 1.1384 * (Cb_lim - 128) + 128
 * Cr_full = 1.1384 * (Cr_lim - 128) + 128
 */
static const struct ipu_ic_csc_params yuvl2yuvf = {
	.coeff = {
		{  149,    0,    0, },
		{    0,  146,    0, },
		{    0,    0,  146, },
	},
	.offset = { -37, -35, -35, },
	.scale = 2,
};

static const struct ipu_ic_csc_params *rgb2rgb[] = {
	&identity,
	&rgbf2rgbl,
	&rgbl2rgbf,
	&identity,
};

static const struct ipu_ic_csc_params *yuv2yuv[] = {
	&identity,
	&yuvf2yuvl,
	&yuvl2yuvf,
	&identity,
};

/*
 * BT.601 RGB full-range to YUV full-range
 *
 * Y =  .2990 * R + .5870 * G + .1140 * B
 * U = -.1687 * R - .3313 * G + .5000 * B + 128
 * V =  .5000 * R - .4187 * G - .0813 * B + 128
 */
static const struct ipu_ic_csc_params rgbf2yuvf_601 = {
	.coeff = {
		{   77,  150,   29, },
		{  -43,  -85,  128, },
		{  128, -107,  -21, },
	},
	.offset = { 0, 512, 512, },
	.scale = 1,
};

/* BT.601 RGB full-range to YUV limited-range */
static const struct ipu_ic_csc_params rgbf2yuvl_601 = {
	.coeff = {
		{   66,  129,   25, },
		{  -38,  -74,  112, },
		{  112,  -94,  -18, },
	},
	.offset = { 64, 512, 512, },
	.scale = 1,
	.sat = true,
};

/* BT.601 RGB limited-range to YUV full-range */
static const struct ipu_ic_csc_params rgbl2yuvf_601 = {
	.coeff = {
		{   89,  175,   34, },
		{  -50,  -99,  149, },
		{  149, -125,  -24, },
	},
	.offset = { -75, 512, 512, },
	.scale = 1,
};

/* BT.601 RGB limited-range to YUV limited-range */
static const struct ipu_ic_csc_params rgbl2yuvl_601 = {
	.coeff = {
		{   77,  150,   29, },
		{  -44,  -87,  131, },
		{  131, -110,  -21, },
	},
	.offset = { 0, 512, 512, },
	.scale = 1,
	.sat = true,
};

/*
 * BT.601 YUV full-range to RGB full-range
 *
 * R = 1. * Y +      0 * (Cb - 128) + 1.4020 * (Cr - 128)
 * G = 1. * Y -  .3441 * (Cb - 128) -  .7141 * (Cr - 128)
 * B = 1. * Y + 1.7720 * (Cb - 128) +      0 * (Cr - 128)
 *
 * equivalently (factoring out the offsets):
 *
 * R = 1. * Y  +      0 * Cb + 1.4020 * Cr - 179.456
 * G = 1. * Y  -  .3441 * Cb -  .7141 * Cr + 135.450
 * B = 1. * Y  + 1.7720 * Cb +      0 * Cr - 226.816
 */
static const struct ipu_ic_csc_params yuvf2rgbf_601 = {
	.coeff = {
		{  128,    0,  179, },
		{  128,  -44,  -91, },
		{  128,  227,    0, },
	},
	.offset = { -359, 271, -454, },
	.scale = 2,
};

/* BT.601 YUV full-range to RGB limited-range */
static const struct ipu_ic_csc_params yuvf2rgbl_601 = {
	.coeff = {
		{  110,    0,  154, },
		{  110,  -38,  -78, },
		{  110,  195,    0, },
	},
	.offset = { -276, 265, -358, },
	.scale = 2,
};

/* BT.601 YUV limited-range to RGB full-range */
static const struct ipu_ic_csc_params yuvl2rgbf_601 = {
	.coeff = {
		{   75,    0,  102, },
		{   75,  -25,  -52, },
		{   75,  129,    0, },
	},
	.offset = { -223, 136, -277, },
	.scale = 3,
};

/* BT.601 YUV limited-range to RGB limited-range */
static const struct ipu_ic_csc_params yuvl2rgbl_601 = {
	.coeff = {
		{  128,    0,  175, },
		{  128,  -43,  -89, },
		{  128,  222,    0, },
	},
	.offset = { -351, 265, -443, },
	.scale = 2,
};

static const struct ipu_ic_csc_params *rgb2yuv_601[] = {
	&rgbf2yuvf_601,
	&rgbf2yuvl_601,
	&rgbl2yuvf_601,
	&rgbl2yuvl_601,
};

static const struct ipu_ic_csc_params *yuv2rgb_601[] = {
	&yuvf2rgbf_601,
	&yuvf2rgbl_601,
	&yuvl2rgbf_601,
	&yuvl2rgbl_601,
};

static int calc_csc_coeffs(struct ipu_ic_csc *csc)
{
	const struct ipu_ic_csc_params **params_tbl;
	int tbl_idx;

	if (csc->out_cs.enc != V4L2_YCBCR_ENC_601)
		return -ENOTSUPP;

	tbl_idx = (QUANT_MAP(csc->in_cs.quant) << 1) |
		QUANT_MAP(csc->out_cs.quant);

	if (csc->in_cs.cs == csc->out_cs.cs) {
		csc->params = (csc->in_cs.cs == IPUV3_COLORSPACE_YUV) ?
			*yuv2yuv[tbl_idx] : *rgb2rgb[tbl_idx];

		return 0;
	}

	/* YUV <-> RGB encoding is required */

	params_tbl = (csc->in_cs.cs == IPUV3_COLORSPACE_YUV) ?
		yuv2rgb_601 : rgb2yuv_601;

	csc->params = *params_tbl[tbl_idx];

	return 0;
}

int __ipu_ic_calc_csc(struct ipu_ic_csc *csc)
{
	return calc_csc_coeffs(csc);
}
EXPORT_SYMBOL_GPL(__ipu_ic_calc_csc);

int ipu_ic_calc_csc(struct ipu_ic_csc *csc,
		    enum v4l2_ycbcr_encoding in_enc,
		    enum v4l2_quantization in_quant,
		    enum ipu_color_space in_cs,
		    enum v4l2_ycbcr_encoding out_enc,
		    enum v4l2_quantization out_quant,
		    enum ipu_color_space out_cs)
{
	ipu_ic_fill_colorspace(&csc->in_cs, in_enc, in_quant, in_cs);
	ipu_ic_fill_colorspace(&csc->out_cs, out_enc, out_quant, out_cs);

	return __ipu_ic_calc_csc(csc);
}
EXPORT_SYMBOL_GPL(ipu_ic_calc_csc);
