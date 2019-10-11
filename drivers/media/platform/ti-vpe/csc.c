// SPDX-License-Identifier: GPL-2.0-only
/*
 * Color space converter library
 *
 * Copyright (c) 2013 Texas Instruments Inc.
 *
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>

#include "csc.h"

/*
 * 12 coefficients in the order:
 * a0, b0, c0, a1, b1, c1, a2, b2, c2, d0, d1, d2
 */
struct quantization {
	u16	coeff[12];
};

struct colorspace {
	struct quantization limited;
	struct quantization full;
};

struct encoding_direction {
	struct colorspace r601;
	struct colorspace r709;
};

struct csc_coeffs {
	struct encoding_direction y2r;
	struct encoding_direction r2y;
};

/* default colorspace coefficients */
static struct csc_coeffs csc_coeffs = {
	.y2r = {
		.r601 = {
			.limited = {
				{	/* SDTV */
				0x0400, 0x0000, 0x057D, 0x0400, 0x1EA7, 0x1D35,
				0x0400, 0x06EF, 0x1FFE, 0x0D40, 0x0210, 0x0C88,
				}
			},
			.full = {
				{	/* SDTV */
				0x04A8, 0x1FFE, 0x0662, 0x04A8, 0x1E6F, 0x1CBF,
				0x04A8, 0x0812, 0x1FFF, 0x0C84, 0x0220, 0x0BAC,
				}
			},
		},
		.r709 = {
			.limited = {
				{	/* HDTV */
				0x0400, 0x0000, 0x0629, 0x0400, 0x1F45, 0x1E2B,
				0x0400, 0x0742, 0x0000, 0x0CEC, 0x0148, 0x0C60,
				}
			},
			.full = {
				{	/* HDTV */
				0x04A8, 0x0000, 0x072C, 0x04A8, 0x1F26, 0x1DDE,
				0x04A8, 0x0873, 0x0000, 0x0C20, 0x0134, 0x0B7C,
				}
			},
		},
	},
	.r2y = {
		.r601 = {
			.limited = {
				{	/* SDTV */
				0x0132, 0x0259, 0x0075, 0x1F50, 0x1EA5, 0x020B,
				0x020B, 0x1E4A, 0x1FAB, 0x0000, 0x0200, 0x0200,
				}
			},
			.full = {
				{	/* SDTV */
				0x0107, 0x0204, 0x0064, 0x1F68, 0x1ED6, 0x01C2,
				0x01C2, 0x1E87, 0x1FB7, 0x0040, 0x0200, 0x0200,
				}
			},
		},
		.r709 = {
			.limited = {
				{	/* HDTV */
				0x00DA, 0x02DC, 0x004A, 0x1F88, 0x1E6C, 0x020C,
				0x020C, 0x1E24, 0x1FD0, 0x0000, 0x0200, 0x0200,
				}
			},
			.full = {
				{	/* HDTV */
				0x00bb, 0x0275, 0x003f, 0x1f99, 0x1ea5, 0x01c2,
				0x01c2, 0x1e67, 0x1fd7, 0x0040, 0x0200, 0x0200,
				}
			},
		},
	},

};

void csc_dump_regs(struct csc_data *csc)
{
	struct device *dev = &csc->pdev->dev;

#define DUMPREG(r) dev_dbg(dev, "%-35s %08x\n", #r, \
	ioread32(csc->base + CSC_##r))

	dev_dbg(dev, "CSC Registers @ %pa:\n", &csc->res->start);

	DUMPREG(CSC00);
	DUMPREG(CSC01);
	DUMPREG(CSC02);
	DUMPREG(CSC03);
	DUMPREG(CSC04);
	DUMPREG(CSC05);

#undef DUMPREG
}
EXPORT_SYMBOL(csc_dump_regs);

void csc_set_coeff_bypass(struct csc_data *csc, u32 *csc_reg5)
{
	*csc_reg5 |= CSC_BYPASS;
}
EXPORT_SYMBOL(csc_set_coeff_bypass);

/*
 * set the color space converter coefficient shadow register values
 */
void csc_set_coeff(struct csc_data *csc, u32 *csc_reg0,
		   struct v4l2_format *src_fmt, struct v4l2_format *dst_fmt)
{
	u32 *csc_reg5 = csc_reg0 + 5;
	u32 *shadow_csc = csc_reg0;
	u16 *coeff, *end_coeff;
	const struct v4l2_pix_format *pix;
	const struct v4l2_pix_format_mplane *mp;
	const struct v4l2_format_info *src_finfo, *dst_finfo;
	enum v4l2_ycbcr_encoding src_ycbcr_enc, dst_ycbcr_enc;
	enum v4l2_quantization src_quantization, dst_quantization;
	u32 src_pixelformat, dst_pixelformat;

	switch (src_fmt->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		pix = &src_fmt->fmt.pix;
		src_pixelformat = pix->pixelformat;
		src_ycbcr_enc = pix->ycbcr_enc;
		src_quantization = pix->quantization;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
	default:
		mp = &src_fmt->fmt.pix_mp;
		src_pixelformat = mp->pixelformat;
		src_ycbcr_enc = mp->ycbcr_enc;
		src_quantization = mp->quantization;
		break;
	}

	switch (dst_fmt->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		pix = &dst_fmt->fmt.pix;
		dst_pixelformat = pix->pixelformat;
		dst_ycbcr_enc = pix->ycbcr_enc;
		dst_quantization = pix->quantization;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
	default:
		mp = &dst_fmt->fmt.pix_mp;
		dst_pixelformat = mp->pixelformat;
		dst_ycbcr_enc = mp->ycbcr_enc;
		dst_quantization = mp->quantization;
		break;
	}

	src_finfo = v4l2_format_info(src_pixelformat);
	dst_finfo = v4l2_format_info(dst_pixelformat);

	if (v4l2_is_format_yuv(src_finfo) &&
	    v4l2_is_format_rgb(dst_finfo)) {
		/* Y2R */

		/*
		 * These are not the standard default values but are
		 * set this way for historical compatibility
		 */
		if (src_ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT)
			src_ycbcr_enc = V4L2_YCBCR_ENC_601;

		if (src_quantization == V4L2_QUANTIZATION_DEFAULT)
			src_quantization = V4L2_QUANTIZATION_FULL_RANGE;

		if (src_ycbcr_enc == V4L2_YCBCR_ENC_601) {
			if (src_quantization == V4L2_QUANTIZATION_FULL_RANGE)
				coeff = csc_coeffs.y2r.r601.full.coeff;
			else
				coeff = csc_coeffs.y2r.r601.limited.coeff;
		} else if (src_ycbcr_enc == V4L2_YCBCR_ENC_709) {
			if (src_quantization == V4L2_QUANTIZATION_FULL_RANGE)
				coeff = csc_coeffs.y2r.r709.full.coeff;
			else
				coeff = csc_coeffs.y2r.r709.limited.coeff;
		} else {
			/* Should never reach this, but it keeps gcc happy */
			coeff = csc_coeffs.y2r.r601.full.coeff;
		}
	} else if (v4l2_is_format_rgb(src_finfo) &&
		   v4l2_is_format_yuv(dst_finfo)) {
		/* R2Y */

		/*
		 * These are not the standard default values but are
		 * set this way for historical compatibility
		 */
		if (dst_ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT)
			dst_ycbcr_enc = V4L2_YCBCR_ENC_601;

		if (dst_quantization == V4L2_QUANTIZATION_DEFAULT)
			dst_quantization = V4L2_QUANTIZATION_FULL_RANGE;

		if (dst_ycbcr_enc == V4L2_YCBCR_ENC_601) {
			if (dst_quantization == V4L2_QUANTIZATION_FULL_RANGE)
				coeff = csc_coeffs.r2y.r601.full.coeff;
			else
				coeff = csc_coeffs.r2y.r601.limited.coeff;
		} else if (dst_ycbcr_enc == V4L2_YCBCR_ENC_709) {
			if (dst_quantization == V4L2_QUANTIZATION_FULL_RANGE)
				coeff = csc_coeffs.r2y.r709.full.coeff;
			else
				coeff = csc_coeffs.r2y.r709.limited.coeff;
		} else {
			/* Should never reach this, but it keeps gcc happy */
			coeff = csc_coeffs.r2y.r601.full.coeff;
		}
	} else {
		*csc_reg5 |= CSC_BYPASS;
		return;
	}

	end_coeff = coeff + 12;

	for (; coeff < end_coeff; coeff += 2)
		*shadow_csc++ = (*(coeff + 1) << 16) | *coeff;
}
EXPORT_SYMBOL(csc_set_coeff);

struct csc_data *csc_create(struct platform_device *pdev, const char *res_name)
{
	struct csc_data *csc;

	dev_dbg(&pdev->dev, "csc_create\n");

	csc = devm_kzalloc(&pdev->dev, sizeof(*csc), GFP_KERNEL);
	if (!csc) {
		dev_err(&pdev->dev, "couldn't alloc csc_data\n");
		return ERR_PTR(-ENOMEM);
	}

	csc->pdev = pdev;

	csc->res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						res_name);
	if (csc->res == NULL) {
		dev_err(&pdev->dev, "missing '%s' platform resources data\n",
			res_name);
		return ERR_PTR(-ENODEV);
	}

	csc->base = devm_ioremap_resource(&pdev->dev, csc->res);
	if (IS_ERR(csc->base)) {
		dev_err(&pdev->dev, "failed to ioremap\n");
		return ERR_CAST(csc->base);
	}

	return csc;
}
EXPORT_SYMBOL(csc_create);

MODULE_DESCRIPTION("TI VIP/VPE Color Space Converter");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_LICENSE("GPL v2");
