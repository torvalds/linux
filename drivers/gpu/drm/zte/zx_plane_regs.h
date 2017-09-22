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

/* VL registers */
#define VL_CTRL0			0x00
#define VL_UPDATE			BIT(3)
#define VL_CTRL1			0x04
#define VL_YUV420_PLANAR		BIT(5)
#define VL_YUV422_SHIFT			3
#define VL_YUV422_YUYV			(0 << VL_YUV422_SHIFT)
#define VL_YUV422_YVYU			(1 << VL_YUV422_SHIFT)
#define VL_YUV422_UYVY			(2 << VL_YUV422_SHIFT)
#define VL_YUV422_VYUY			(3 << VL_YUV422_SHIFT)
#define VL_FMT_YUV420			0
#define VL_FMT_YUV422			1
#define VL_FMT_YUV420_P010		2
#define VL_FMT_YUV420_HANTRO		3
#define VL_FMT_YUV444_8BIT		4
#define VL_FMT_YUV444_10BIT		5
#define VL_CTRL2			0x08
#define VL_SCALER_BYPASS_MODE		BIT(0)
#define VL_STRIDE			0x0c
#define LUMA_STRIDE_SHIFT		16
#define LUMA_STRIDE_MASK		(0xffff << LUMA_STRIDE_SHIFT)
#define CHROMA_STRIDE_SHIFT		0
#define CHROMA_STRIDE_MASK		(0xffff << CHROMA_STRIDE_SHIFT)
#define VL_SRC_SIZE			0x10
#define VL_Y				0x14
#define VL_POS_START			0x30
#define VL_POS_END			0x34

#define LUMA_STRIDE(x)	 (((x) << LUMA_STRIDE_SHIFT) & LUMA_STRIDE_MASK)
#define CHROMA_STRIDE(x) (((x) << CHROMA_STRIDE_SHIFT) & CHROMA_STRIDE_MASK)

/* RSZ registers */
#define RSZ_SRC_CFG			0x00
#define RSZ_DEST_CFG			0x04
#define RSZ_ENABLE_CFG			0x14

#define RSZ_VL_LUMA_HOR			0x08
#define RSZ_VL_LUMA_VER			0x0c
#define RSZ_VL_CHROMA_HOR		0x10
#define RSZ_VL_CHROMA_VER		0x14
#define RSZ_VL_CTRL_CFG			0x18
#define RSZ_VL_FMT_SHIFT		3
#define RSZ_VL_FMT_MASK			(0x3 << RSZ_VL_FMT_SHIFT)
#define RSZ_VL_FMT_YCBCR420		(0x0 << RSZ_VL_FMT_SHIFT)
#define RSZ_VL_FMT_YCBCR422		(0x1 << RSZ_VL_FMT_SHIFT)
#define RSZ_VL_FMT_YCBCR444		(0x2 << RSZ_VL_FMT_SHIFT)
#define RSZ_VL_ENABLE_CFG		0x1c

#define RSZ_VER_SHIFT			16
#define RSZ_VER_MASK			(0xffff << RSZ_VER_SHIFT)
#define RSZ_HOR_SHIFT			0
#define RSZ_HOR_MASK			(0xffff << RSZ_HOR_SHIFT)

#define RSZ_VER(x)	(((x) << RSZ_VER_SHIFT) & RSZ_VER_MASK)
#define RSZ_HOR(x)	(((x) << RSZ_HOR_SHIFT) & RSZ_HOR_MASK)

#define RSZ_DATA_STEP_SHIFT		16
#define RSZ_DATA_STEP_MASK		(0xffff << RSZ_DATA_STEP_SHIFT)
#define RSZ_PARA_STEP_SHIFT		0
#define RSZ_PARA_STEP_MASK		(0xffff << RSZ_PARA_STEP_SHIFT)

#define RSZ_DATA_STEP(x) (((x) << RSZ_DATA_STEP_SHIFT) & RSZ_DATA_STEP_MASK)
#define RSZ_PARA_STEP(x) (((x) << RSZ_PARA_STEP_SHIFT) & RSZ_PARA_STEP_MASK)

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
