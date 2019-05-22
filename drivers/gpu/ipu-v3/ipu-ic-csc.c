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

static const struct ipu_ic_csc_params *rgb2rgb[] = {
	&identity,
};

static const struct ipu_ic_csc_params *yuv2yuv[] = {
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

static const struct ipu_ic_csc_params *rgb2yuv_601[] = {
	&rgbf2yuvf_601,
};

static const struct ipu_ic_csc_params *yuv2rgb_601[] = {
	&yuvf2rgbf_601,
};

static int calc_csc_coeffs(struct ipu_ic_csc *csc)
{
	if (csc->out_cs.enc != V4L2_YCBCR_ENC_601)
		return -ENOTSUPP;

	if ((csc->in_cs.cs == IPUV3_COLORSPACE_YUV &&
	     csc->in_cs.quant != V4L2_QUANTIZATION_FULL_RANGE) ||
	    (csc->out_cs.cs == IPUV3_COLORSPACE_YUV &&
	     csc->out_cs.quant != V4L2_QUANTIZATION_FULL_RANGE))
		return -ENOTSUPP;

	if ((csc->in_cs.cs == IPUV3_COLORSPACE_RGB &&
	     csc->in_cs.quant != V4L2_QUANTIZATION_FULL_RANGE) ||
	    (csc->out_cs.cs == IPUV3_COLORSPACE_RGB &&
	     csc->out_cs.quant != V4L2_QUANTIZATION_FULL_RANGE))
		return -ENOTSUPP;

	if (csc->in_cs.cs == csc->out_cs.cs) {
		csc->params = (csc->in_cs.cs == IPUV3_COLORSPACE_YUV) ?
			*yuv2yuv[0] : *rgb2rgb[0];
		return 0;
	}

	csc->params = (csc->in_cs.cs == IPUV3_COLORSPACE_YUV) ?
		*yuv2rgb_601[0] : *rgb2yuv_601[0];

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
