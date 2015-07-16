/*
 * Color space converter library
 *
 * Copyright (c) 2013 Texas Instruments Inc.
 *
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include "csc.h"

/*
 * 16 coefficients in the order:
 * a0, b0, c0, a1, b1, c1, a2, b2, c2, d0, d1, d2
 * (we may need to pass non-default values from user space later on, we might
 * need to make the coefficient struct more easy to populate)
 */
struct colorspace_coeffs {
	u16	sd[12];
	u16	hd[12];
};

/* VIDEO_RANGE: limited range, GRAPHICS_RANGE: full range */
#define	CSC_COEFFS_VIDEO_RANGE_Y2R	0
#define	CSC_COEFFS_GRAPHICS_RANGE_Y2R	1
#define	CSC_COEFFS_VIDEO_RANGE_R2Y	2
#define	CSC_COEFFS_GRAPHICS_RANGE_R2Y	3

/* default colorspace coefficients */
static struct colorspace_coeffs colorspace_coeffs[4] = {
	[CSC_COEFFS_VIDEO_RANGE_Y2R] = {
		{
			/* SDTV */
			0x0400, 0x0000, 0x057D, 0x0400, 0x1EA7, 0x1D35,
			0x0400, 0x06EF, 0x1FFE, 0x0D40, 0x0210, 0x0C88,
		},
		{
			/* HDTV */
			0x0400, 0x0000, 0x0629, 0x0400, 0x1F45, 0x1E2B,
			0x0400, 0x0742, 0x0000, 0x0CEC, 0x0148, 0x0C60,
		},
	},
	[CSC_COEFFS_GRAPHICS_RANGE_Y2R] = {
		{
			/* SDTV */
			0x04A8, 0x1FFE, 0x0662, 0x04A8, 0x1E6F, 0x1CBF,
			0x04A8, 0x0812, 0x1FFF, 0x0C84, 0x0220, 0x0BAC,
		},
		{
			/* HDTV */
			0x04A8, 0x0000, 0x072C, 0x04A8, 0x1F26, 0x1DDE,
			0x04A8, 0x0873, 0x0000, 0x0C20, 0x0134, 0x0B7C,
		},
	},
	[CSC_COEFFS_VIDEO_RANGE_R2Y] = {
		{
			/* SDTV */
			0x0132, 0x0259, 0x0075, 0x1F50, 0x1EA5, 0x020B,
			0x020B, 0x1E4A, 0x1FAB, 0x0000, 0x0200, 0x0200,
		},
		{
			/* HDTV */
			0x00DA, 0x02DC, 0x004A, 0x1F88, 0x1E6C, 0x020C,
			0x020C, 0x1E24, 0x1FD0, 0x0000, 0x0200, 0x0200,
		},
	},
	[CSC_COEFFS_GRAPHICS_RANGE_R2Y] = {
		{
			/* SDTV */
			0x0107, 0x0204, 0x0064, 0x1F68, 0x1ED6, 0x01C2,
			0x01C2, 0x1E87, 0x1FB7, 0x0040, 0x0200, 0x0200,
		},
		{
			/* HDTV */
			0x04A8, 0x0000, 0x072C, 0x04A8, 0x1F26, 0x1DDE,
			0x04A8, 0x0873, 0x0000, 0x0C20, 0x0134, 0x0B7C,
		},
	},
};

void csc_dump_regs(struct csc_data *csc)
{
	struct device *dev = &csc->pdev->dev;

#define DUMPREG(r) dev_dbg(dev, "%-35s %08x\n", #r, \
	ioread32(csc->base + CSC_##r))

	DUMPREG(CSC00);
	DUMPREG(CSC01);
	DUMPREG(CSC02);
	DUMPREG(CSC03);
	DUMPREG(CSC04);
	DUMPREG(CSC05);

#undef DUMPREG
}

void csc_set_coeff_bypass(struct csc_data *csc, u32 *csc_reg5)
{
	*csc_reg5 |= CSC_BYPASS;
}

/*
 * set the color space converter coefficient shadow register values
 */
void csc_set_coeff(struct csc_data *csc, u32 *csc_reg0,
		enum v4l2_colorspace src_colorspace,
		enum v4l2_colorspace dst_colorspace)
{
	u32 *csc_reg5 = csc_reg0 + 5;
	u32 *shadow_csc = csc_reg0;
	struct colorspace_coeffs *sd_hd_coeffs;
	u16 *coeff, *end_coeff;
	enum v4l2_colorspace yuv_colorspace;
	int sel = 0;

	/*
	 * support only graphics data range(full range) for now, a control ioctl
	 * would be nice here
	 */
	/* Y2R */
	if (dst_colorspace == V4L2_COLORSPACE_SRGB &&
			(src_colorspace == V4L2_COLORSPACE_SMPTE170M ||
			src_colorspace == V4L2_COLORSPACE_REC709)) {
		/* Y2R */
		sel = 1;
		yuv_colorspace = src_colorspace;
	} else if ((dst_colorspace == V4L2_COLORSPACE_SMPTE170M ||
			dst_colorspace == V4L2_COLORSPACE_REC709) &&
			src_colorspace == V4L2_COLORSPACE_SRGB) {
		/* R2Y */
		sel = 3;
		yuv_colorspace = dst_colorspace;
	} else {
		*csc_reg5 |= CSC_BYPASS;
		return;
	}

	sd_hd_coeffs = &colorspace_coeffs[sel];

	/* select between SD or HD coefficients */
	if (yuv_colorspace == V4L2_COLORSPACE_SMPTE170M)
		coeff = sd_hd_coeffs->sd;
	else
		coeff = sd_hd_coeffs->hd;

	end_coeff = coeff + 12;

	for (; coeff < end_coeff; coeff += 2)
		*shadow_csc++ = (*(coeff + 1) << 16) | *coeff;
}

struct csc_data *csc_create(struct platform_device *pdev)
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
			"csc");
	if (csc->res == NULL) {
		dev_err(&pdev->dev, "missing platform resources data\n");
		return ERR_PTR(-ENODEV);
	}

	csc->base = devm_ioremap_resource(&pdev->dev, csc->res);
	if (IS_ERR(csc->base)) {
		dev_err(&pdev->dev, "failed to ioremap\n");
		return ERR_CAST(csc->base);
	}

	return csc;
}
