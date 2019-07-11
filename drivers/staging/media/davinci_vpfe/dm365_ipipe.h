/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 */

#ifndef _DAVINCI_VPFE_DM365_IPIPE_H
#define _DAVINCI_VPFE_DM365_IPIPE_H

#include <linux/platform_device.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "davinci_vpfe_user.h"
#include "vpfe_video.h"

#define CEIL(a, b)	(((a) + (b-1)) / (b))

enum ipipe_noise_filter {
	IPIPE_D2F_1ST = 0,
	IPIPE_D2F_2ND = 1,
};

/* Used for driver storage */
struct ipipe_otfdpc_2_0 {
	/* 0 - disable, 1 - enable */
	unsigned char en;
	/* defect detection method */
	enum vpfe_ipipe_otfdpc_det_meth det_method;
	/* Algorithm used. Applicable only when IPIPE_DPC_OTF_MIN_MAX2 is
	 * used
	 */
	enum vpfe_ipipe_otfdpc_alg alg;
	struct vpfe_ipipe_otfdpc_2_0_cfg otfdpc_2_0;
};

struct ipipe_otfdpc_3_0 {
	/* 0 - disable, 1 - enable */
	unsigned char en;
	/* defect detection method */
	enum vpfe_ipipe_otfdpc_det_meth det_method;
	/* Algorithm used. Applicable only when IPIPE_DPC_OTF_MIN_MAX2 is
	 * used
	 */
	enum vpfe_ipipe_otfdpc_alg alg;
	struct vpfe_ipipe_otfdpc_3_0_cfg otfdpc_3_0;
};

/* Structure for configuring Luminance Adjustment module */
struct ipipe_lum_adj {
	/* Brightness adjustments */
	unsigned char brightness;
	/* contrast adjustments */
	unsigned char contrast;
};

enum ipipe_rgb2rgb {
	IPIPE_RGB2RGB_1 = 0,
	IPIPE_RGB2RGB_2 = 1,
};

struct ipipe_module_params {
	__u32 flag;
	struct vpfe_ipipe_input_config input_config;
	struct vpfe_ipipe_lutdpc lutdpc;
	struct vpfe_ipipe_otfdpc otfdpc;
	struct vpfe_ipipe_nf nf1;
	struct vpfe_ipipe_nf nf2;
	struct vpfe_ipipe_gic gic;
	struct vpfe_ipipe_wb wbal;
	struct vpfe_ipipe_cfa cfa;
	struct vpfe_ipipe_rgb2rgb rgb2rgb1;
	struct vpfe_ipipe_rgb2rgb rgb2rgb2;
	struct vpfe_ipipe_gamma gamma;
	struct vpfe_ipipe_3d_lut lut;
	struct vpfe_ipipe_rgb2yuv rgb2yuv;
	struct vpfe_ipipe_gbce gbce;
	struct vpfe_ipipe_yuv422_conv yuv422_conv;
	struct vpfe_ipipe_yee yee;
	struct vpfe_ipipe_car car;
	struct vpfe_ipipe_cgs cgs;
	struct ipipe_lum_adj lum_adj;
};

#define IPIPE_PAD_SINK			0
#define IPIPE_PAD_SOURCE		1

#define IPIPE_PADS_NUM			2

#define IPIPE_OUTPUT_NONE		0
#define IPIPE_OUTPUT_RESIZER		(1 << 0)

enum ipipe_input_entity {
	IPIPE_INPUT_NONE = 0,
	IPIPE_INPUT_MEMORY = 1,
	IPIPE_INPUT_CCDC = 2,
};


struct vpfe_ipipe_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[IPIPE_PADS_NUM];
	struct v4l2_mbus_framefmt formats[IPIPE_PADS_NUM];
	enum ipipe_input_entity input;
	unsigned int output;
	struct v4l2_ctrl_handler ctrls;
	void __iomem *base_addr;
	void __iomem *isp5_base_addr;
	struct ipipe_module_params config;
};

struct ipipe_module_if {
	unsigned int param_offset;
	unsigned int param_size;
	unsigned int config_offset;
	int (*set)(struct vpfe_ipipe_device *ipipe, void *param);
	int (*get)(struct vpfe_ipipe_device *ipipe, void *param);
};

/* data paths */
enum ipipe_data_paths {
	IPIPE_RAW2YUV,
	/* Bayer RAW input to YCbCr output */
	IPIPE_RAW2RAW,
	/* Bayer Raw to Bayer output */
	IPIPE_RAW2BOX,
	/* Bayer Raw to Boxcar output */
	IPIPE_YUV2YUV
	/* YUV Raw to YUV Raw output */
};

#define IPIPE_COLPTN_R_Ye	0x0
#define IPIPE_COLPTN_Gr_Cy	0x1
#define IPIPE_COLPTN_Gb_G	0x2
#define IPIPE_COLPTN_B_Mg	0x3

#define COLPAT_EE_SHIFT		0
#define COLPAT_EO_SHIFT		2
#define COLPAT_OE_SHIFT		4
#define COLPAT_OO_SHIFT		6

#define ipipe_sgrbg_pattern \
	(IPIPE_COLPTN_Gr_Cy <<  COLPAT_EE_SHIFT | \
	IPIPE_COLPTN_R_Ye  << COLPAT_EO_SHIFT | \
	IPIPE_COLPTN_B_Mg  << COLPAT_OE_SHIFT | \
	IPIPE_COLPTN_Gb_G  << COLPAT_OO_SHIFT)

#define ipipe_srggb_pattern \
	(IPIPE_COLPTN_R_Ye <<  COLPAT_EE_SHIFT | \
	IPIPE_COLPTN_Gr_Cy  << COLPAT_EO_SHIFT | \
	IPIPE_COLPTN_Gb_G  << COLPAT_OE_SHIFT | \
	IPIPE_COLPTN_B_Mg  << COLPAT_OO_SHIFT)

int vpfe_ipipe_register_entities(struct vpfe_ipipe_device *ipipe,
		struct v4l2_device *v4l2_dev);
int vpfe_ipipe_init(struct vpfe_ipipe_device *ipipe,
		struct platform_device *pdev);
void vpfe_ipipe_unregister_entities(struct vpfe_ipipe_device *ipipe);
void vpfe_ipipe_cleanup(struct vpfe_ipipe_device *ipipe,
		struct platform_device *pdev);
void vpfe_ipipe_enable(struct vpfe_device *vpfe_dev, int en);

#endif		/* _DAVINCI_VPFE_DM365_IPIPE_H */
