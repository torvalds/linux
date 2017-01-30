/*
 * Copyright 2016 Linaro Ltd.
 * Copyright 2016 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ZX_PLANE_REGS_H__
#define __ZX_PLANE_REGS_H__

/* GL registers */
#define GL_CTRL0			0x00
#define GL_UPDATE			BIT(5)
#define GL_CTRL1			0x04
#define GL_DATA_FMT_SHIFT		0
#define GL_DATA_FMT_MASK		(0xf << GL_DATA_FMT_SHIFT)
#define GL_FMT_ARGB8888			0
#define GL_FMT_RGB888			1
#define GL_FMT_RGB565			2
#define GL_FMT_ARGB1555			3
#define GL_FMT_ARGB4444			4
#define GL_CTRL2			0x08
#define GL_GLOBAL_ALPHA_SHIFT		8
#define GL_GLOBAL_ALPHA_MASK		(0xff << GL_GLOBAL_ALPHA_SHIFT)
#define GL_CTRL3			0x0c
#define GL_SCALER_BYPASS_MODE		BIT(0)
#define GL_STRIDE			0x18
#define GL_ADDR				0x1c
#define GL_SRC_SIZE			0x38
#define GL_SRC_W_SHIFT			16
#define GL_SRC_W_MASK			(0x3fff << GL_SRC_W_SHIFT)
#define GL_SRC_H_SHIFT			0
#define GL_SRC_H_MASK			(0x3fff << GL_SRC_H_SHIFT)
#define GL_POS_START			0x9c
#define GL_POS_END			0xa0
#define GL_POS_X_SHIFT			16
#define GL_POS_X_MASK			(0x1fff << GL_POS_X_SHIFT)
#define GL_POS_Y_SHIFT			0
#define GL_POS_Y_MASK			(0x1fff << GL_POS_Y_SHIFT)

#define GL_SRC_W(x)	(((x) << GL_SRC_W_SHIFT) & GL_SRC_W_MASK)
#define GL_SRC_H(x)	(((x) << GL_SRC_H_SHIFT) & GL_SRC_H_MASK)
#define GL_POS_X(x)	(((x) << GL_POS_X_SHIFT) & GL_POS_X_MASK)
#define GL_POS_Y(x)	(((x) << GL_POS_Y_SHIFT) & GL_POS_Y_MASK)

/* CSC registers */
#define CSC_CTRL0			0x30
#define CSC_COV_MODE_SHIFT		16
#define CSC_COV_MODE_MASK		(0xffff << CSC_COV_MODE_SHIFT)
#define CSC_BT601_IMAGE_RGB2YCBCR	0
#define CSC_BT601_IMAGE_YCBCR2RGB	1
#define CSC_BT601_VIDEO_RGB2YCBCR	2
#define CSC_BT601_VIDEO_YCBCR2RGB	3
#define CSC_BT709_IMAGE_RGB2YCBCR	4
#define CSC_BT709_IMAGE_YCBCR2RGB	5
#define CSC_BT709_VIDEO_RGB2YCBCR	6
#define CSC_BT709_VIDEO_YCBCR2RGB	7
#define CSC_BT2020_IMAGE_RGB2YCBCR	8
#define CSC_BT2020_IMAGE_YCBCR2RGB	9
#define CSC_BT2020_VIDEO_RGB2YCBCR	10
#define CSC_BT2020_VIDEO_YCBCR2RGB	11
#define CSC_WORK_ENABLE			BIT(0)

/* RSZ registers */
#define RSZ_SRC_CFG			0x00
#define RSZ_DEST_CFG			0x04
#define RSZ_ENABLE_CFG			0x14

#define RSZ_VER_SHIFT			16
#define RSZ_VER_MASK			(0xffff << RSZ_VER_SHIFT)
#define RSZ_HOR_SHIFT			0
#define RSZ_HOR_MASK			(0xffff << RSZ_HOR_SHIFT)

#define RSZ_VER(x)	(((x) << RSZ_VER_SHIFT) & RSZ_VER_MASK)
#define RSZ_HOR(x)	(((x) << RSZ_HOR_SHIFT) & RSZ_HOR_MASK)

/* HBSC registers */
#define HBSC_SATURATION			0x00
#define HBSC_HUE			0x04
#define HBSC_BRIGHT			0x08
#define HBSC_CONTRAST			0x0c
#define HBSC_THRESHOLD_COL1		0x10
#define HBSC_THRESHOLD_COL2		0x14
#define HBSC_THRESHOLD_COL3		0x18
#define HBSC_CTRL0			0x28
#define HBSC_CTRL_EN			BIT(2)

#endif /* __ZX_PLANE_REGS_H__ */
