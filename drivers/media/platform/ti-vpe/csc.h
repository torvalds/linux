/*
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
#ifndef TI_CSC_H
#define TI_CSC_H

/* VPE color space converter regs */
#define CSC_CSC00		0x00
#define CSC_A0_MASK		0x1fff
#define CSC_A0_SHIFT		0
#define CSC_B0_MASK		0x1fff
#define CSC_B0_SHIFT		16

#define CSC_CSC01		0x04
#define CSC_C0_MASK		0x1fff
#define CSC_C0_SHIFT		0
#define CSC_A1_MASK		0x1fff
#define CSC_A1_SHIFT		16

#define CSC_CSC02		0x08
#define CSC_B1_MASK		0x1fff
#define CSC_B1_SHIFT		0
#define CSC_C1_MASK		0x1fff
#define CSC_C1_SHIFT		16

#define CSC_CSC03		0x0c
#define CSC_A2_MASK		0x1fff
#define CSC_A2_SHIFT		0
#define CSC_B2_MASK		0x1fff
#define CSC_B2_SHIFT		16

#define CSC_CSC04		0x10
#define CSC_C2_MASK		0x1fff
#define CSC_C2_SHIFT		0
#define CSC_D0_MASK		0x0fff
#define CSC_D0_SHIFT		16

#define CSC_CSC05		0x14
#define CSC_D1_MASK		0x0fff
#define CSC_D1_SHIFT		0
#define CSC_D2_MASK		0x0fff
#define CSC_D2_SHIFT		16

#define CSC_BYPASS		(1 << 28)

struct csc_data {
	void __iomem		*base;
	struct resource		*res;

	struct platform_device	*pdev;
};

void csc_dump_regs(struct csc_data *csc);
void csc_set_coeff_bypass(struct csc_data *csc, u32 *csc_reg5);
void csc_set_coeff(struct csc_data *csc, u32 *csc_reg0,
		enum v4l2_colorspace src_colorspace,
		enum v4l2_colorspace dst_colorspace);
struct csc_data *csc_create(struct platform_device *pdev);

#endif
