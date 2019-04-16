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

#ifndef _DAVINCI_VPFE_DM365_ISIF_H
#define _DAVINCI_VPFE_DM365_ISIF_H

#include <linux/platform_device.h>

#include <mach/mux.h>

#include <media/davinci/vpfe_types.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#include "davinci_vpfe_user.h"
#include "dm365_isif_regs.h"
#include "vpfe_video.h"

#define ISIF_CULLING_HCAPT_ODD		0xff
#define ISIF_CULLING_HCAPT_EVEN		0xff
#define ISIF_CULLING_VCAPT		0xff

#define ISIF_CADU_BITS			0x07ff
#define ISIF_CADL_BITS			0x0ffff

enum isif_pixfmt {
	ISIF_PIXFMT_RAW = 0,
	ISIF_PIXFMT_YCBCR_16BIT = 1,
	ISIF_PIXFMT_YCBCR_8BIT = 2,
};

enum isif_frmfmt {
	ISIF_FRMFMT_PROGRESSIVE = 0,
	ISIF_FRMFMT_INTERLACED = 1,
};

/* PIXEL ORDER IN MEMORY from LSB to MSB */
/* only applicable for 8-bit input mode  */
enum isif_pixorder {
	ISIF_PIXORDER_YCBYCR = 0,
	ISIF_PIXORDER_CBYCRY = 1,
};

enum isif_buftype {
	ISIF_BUFTYPE_FLD_INTERLEAVED = 0,
	ISIF_BUFTYPE_FLD_SEPARATED = 1,
};

struct isif_ycbcr_config {
	/* v4l2 pixel format */
	unsigned long v4l2_pix_fmt;
	/* isif pixel format */
	enum isif_pixfmt pix_fmt;
	/* isif frame format */
	enum isif_frmfmt frm_fmt;
	/* isif crop window */
	struct v4l2_rect win;
	/* field polarity */
	enum vpfe_pin_pol fid_pol;
	/* interface VD polarity */
	enum vpfe_pin_pol vd_pol;
	/* interface HD polarity */
	enum vpfe_pin_pol hd_pol;
	/* isif pix order. Only used for ycbcr capture */
	enum isif_pixorder pix_order;
	/* isif buffer type. Only used for ycbcr capture */
	enum isif_buftype buf_type;
};

enum isif_cfa_pattern {
	ISIF_CFA_PAT_MOSAIC = 0,
	ISIF_CFA_PAT_STRIPE = 1,
};

enum isif_data_msb {
	/* MSB b15 */
	ISIF_BIT_MSB_15 = 0,
	/* MSB b14 */
	ISIF_BIT_MSB_14 = 1,
	/* MSB b13 */
	ISIF_BIT_MSB_13 = 2,
	/* MSB b12 */
	ISIF_BIT_MSB_12 = 3,
	/* MSB b11 */
	ISIF_BIT_MSB_11 = 4,
	/* MSB b10 */
	ISIF_BIT_MSB_10 = 5,
	/* MSB b9 */
	ISIF_BIT_MSB_9 = 6,
	/* MSB b8 */
	ISIF_BIT_MSB_8 = 7,
	/* MSB b7 */
	ISIF_BIT_MSB_7 = 8,
};

struct isif_params_raw {
	/* v4l2 pixel format */
	unsigned long v4l2_pix_fmt;
	/* isif pixel format */
	enum isif_pixfmt pix_fmt;
	/* isif frame format */
	enum isif_frmfmt frm_fmt;
	/* video window */
	struct v4l2_rect win;
	/* field polarity */
	enum vpfe_pin_pol fid_pol;
	/* interface VD polarity */
	enum vpfe_pin_pol vd_pol;
	/* interface HD polarity */
	enum vpfe_pin_pol hd_pol;
	/* buffer type. Applicable for interlaced mode */
	enum isif_buftype buf_type;
	/* cfa pattern */
	enum isif_cfa_pattern cfa_pat;
	/* Data MSB position */
	enum isif_data_msb data_msb;
	/* Enable horizontal flip */
	unsigned char horz_flip_en;
	/* Enable image invert vertically */
	unsigned char image_invert_en;
	unsigned char dpcm_predictor;
	struct vpfe_isif_raw_config config_params;
};

enum isif_data_pack {
	ISIF_PACK_16BIT = 0,
	ISIF_PACK_12BIT = 1,
	ISIF_PACK_8BIT = 2,
};

struct isif_gain_values {
	unsigned int cr_gain;
	unsigned int cgr_gain;
	unsigned int cgb_gain;
	unsigned int cb_gain;
	unsigned int offset;
};

struct isif_oper_config {
	struct isif_ycbcr_config ycbcr;
	struct isif_params_raw bayer;
	enum isif_data_pack data_pack;
	struct isif_gain_values isif_gain_params;
	void __iomem *base_addr;
	void __iomem *linear_tbl0_addr;
	void __iomem *linear_tbl1_addr;
};

#define ISIF_PAD_SINK      0
#define ISIF_PAD_SOURCE    1

#define ISIF_PADS_NUM      2

enum isif_input_entity {
	ISIF_INPUT_NONE = 0,
	ISIF_INPUT_PARALLEL = 1,
};

#define ISIF_OUTPUT_NONE	(0)
#define ISIF_OUTPUT_MEMORY	(1 << 0)
#define ISIF_OUTPUT_IPIPEIF	(1 << 1)

struct vpfe_isif_device {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[ISIF_PADS_NUM];
	struct v4l2_mbus_framefmt	formats[ISIF_PADS_NUM];
	enum isif_input_entity		input;
	unsigned int			output;
	struct v4l2_ctrl_handler        ctrls;
	struct v4l2_rect		crop;
	struct isif_oper_config		isif_cfg;
	struct vpfe_video_device	video_out;
};

enum v4l2_field vpfe_isif_get_fid(struct vpfe_device *vpfe_dev);
void vpfe_isif_unregister_entities(struct vpfe_isif_device *isif);
int vpfe_isif_register_entities(struct vpfe_isif_device *isif,
				struct v4l2_device *dev);
int vpfe_isif_init(struct vpfe_isif_device *isif, struct platform_device *pdev);
void vpfe_isif_cleanup(struct vpfe_isif_device *vpfe_isif,
		       struct platform_device *pdev);
void vpfe_isif_vidint1_isr(struct vpfe_isif_device *isif);
void vpfe_isif_buffer_isr(struct vpfe_isif_device *isif);

#endif		/* _DAVINCI_VPFE_DM365_ISIF_H */
