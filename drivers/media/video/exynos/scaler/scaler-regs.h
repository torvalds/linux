/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Register header file for Exynos Scaler driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* Status */
#define SCALER_STATUS			0x00

/* Configuration */
#define SCALER_CFG			0x04
#define SCALER_CFG_FILL_EN		(1 << 24)
#define SCALER_CFG_BL_DIV_ALPHA_EN	(1 << 17)
#define SCALER_CFG_BLEND_EN		(1 << 16)
#define SCALER_CFG_CSC_Y_OFFSET_SRC	(1 << 10)
#define SCALER_CFG_CSC_Y_OFFSET_DST	(1 << 9)
#define SCALER_CFG_SOFT_RST		(1 << 1)
#define SCALER_CFG_START_CMD		(1 << 0)

/* Interrupt */
#define SCALER_INT_EN			0x08
#define SCALER_INT_EN_FRAME_END		(1 << 0)
#define SCALER_INT_EN_ALL		0x807fffff

#define SCALER_INT_STATUS		0x0c
#define SCALER_INT_STATUS_FRAME_END	(1 << 0)

#define SCALER_SRC_CFG			0x10

/* Source Image Configuration */
#define SCALER_CFG_TILE_EN		(1 << 10)
#define SCALER_CFG_VHALF_PHASE_EN	(1 << 9)
#define SCALER_CFG_BIG_ENDIAN		(1 << 8)
#define SCALER_CFG_BYTE_SWAP		(2 << 5)
#define SCALER_CFG_HWORD_SWAP		(3 << 5)
#define SCALER_CFG_FMT_MASK		(0x1f << 0)
#define SCALER_CFG_FMT_YCBCR420_2P	(0 << 0)
#define SCALER_CFG_FMT_YUYV		(0xa << 0)
#define SCALER_CFG_FMT_UYVY		(0xb << 0)
#define SCALER_CFG_FMT_YVYU		(9 << 0)
#define SCALER_CFG_FMT_YCBCR422_2P	(2 << 0)
#define SCALER_CFG_FMT_YCBCR444_2P	(3 << 0)
#define SCALER_CFG_FMT_RGB565		(4 << 0)
#define SCALER_CFG_FMT_ARGB1555		(5 << 0)
#define SCALER_CFG_FMT_ARGB4444		(0xc << 0)
#define SCALER_CFG_FMT_ARGB8888		(6 << 0)
#define SCALER_CFG_FMT_RGBA8888		(0xe << 0)
#define SCALER_CFG_FMT_P_ARGB8888	(7 << 0)
#define SCALER_CFG_FMT_L8A8		(0xd << 0)
#define SCALER_CFG_FMT_L8		(0xf << 0)
#define SCALER_CFG_FMT_YCRCB420_2P	(0x10 << 0)
#define SCALER_CFG_FMT_YCRCB422_2P	(0x12 << 0)
#define SCALER_CFG_FMT_YCRCB444_2P	(0x13 << 0)
#define SCALER_CFG_FMT_YCBCR420_3P	(0x14 << 0)
#define SCALER_CFG_FMT_YCBCR422_3P	(0x16 << 0)
#define SCALER_CFG_FMT_YCBCR444_3P	(0x17 << 0)

/* Source Y Base Address */
#define SCALER_SRC_Y_BASE		0x14
#define SCALER_SRC_CB_BASE		0x18
#define SCALER_SRC_CR_BASE		0x294
#define SCALER_SRC_SPAN			0x1c
#define SCALER_SRC_CSPAN_MASK		(0xffff << 16)
#define SCALER_SRC_YSPAN_MASK		(0xffff << 0)

#define SCALER_SRC_Y_POS		0x20
#define SCALER_SRC_YX(x)		((x) << 18)
#define SCALER_SRC_YY(x)		((x) << 2)

#define SCALER_SRC_WH			0x24
#define SCALER_SRC_W(x)			((x) << 16)
#define SCALER_SRC_H(x)			((x) << 0)

#define SCALER_SRC_C_POS		0x28

#define SCALER_DST_CFG			0x30
#define SCALER_DST_Y_BASE		0x34
#define SCALER_DST_CB_BASE		0x38
#define SCALER_DST_CR_BASE		0x298
#define SCALER_DST_SPAN			0x3c
#define SCALER_DST_CSPAN_MASK		(0xffff << 16)
#define SCALER_DST_YSPAN_MASK		(0xffff << 0)

#define SCALER_DST_WH			0x40
#define SCALER_DST_W(x)			((x) << 16)
#define SCALER_DST_H(x)			((x) << 0)

#define SCALER_DST_POS			0x44
#define SCALER_DST_X(x)			((x) << 16)
#define SCALER_DST_Y(x)			((x) << 0)

#define SCALER_H_RATIO			0x50
#define SCALER_V_RATIO			0x54
#define SCALER_RATIO_MASK		(0x7ffff << 0)

#define SCALER_ROT_CFG			0x58
#define SCALER_ROT_MASK			(3 << 0)
#define SCALER_FLIP_MASK		(3 << 2)
#define SCALER_FLIP_X_EN		(1 << 3)
#define SCALER_FLIP_Y_EN		(1 << 2)
#define SCALER_ROT_90			(1 << 0)
#define SCALER_ROT_180			(2 << 0)
#define SCALER_ROT_270			(3 << 0)

#define SCALER_LAT_CON			0x5c

#define SCALER_YHCOEF			0x60
#define SCALER_YVCOEF			0xf0
#define SCALER_CHCOEF			0x140
#define SCALER_CVCOEF			0x1d0

#define SCALER_CSC_COEF00		0x220
#define SCALER_CSC_COEF10		0x224
#define SCALER_CSC_COEF20		0x228
#define SCALER_CSC_COEF01		0x22c
#define SCALER_CSC_COEF11		0x230
#define SCALER_CSC_COEF21		0x234
#define SCALER_CSC_COEF02		0x238
#define SCALER_CSC_COEF12		0x23c
#define SCALER_CSC_COEF22		0x240
#define SCALER_CSC_COEF_MASK		(0xfff << 0)

#define SCALER_DITH_CFG			0x250
#define SCALER_DITH_R_MASK		(3 << 6)
#define SCALER_DITH_G_MASK		(3 << 3)
#define SCALER_DITH_B_MASK		(3 << 0)
#define SCALER_DITH_R_SHIFT		(6)
#define SCALER_DITH_G_SHIFT		(3)
#define SCALER_DITH_B_SHIFT		(0)

#define SCALER_VER			0x260

#define SCALER_CRC_COLOR01		0x270
#define SCALER_CRC_COLOR23		0x274
#define SCALER_CYCLE_COUNT		0x278

#define SCALER_SRC_BLEND_COLOR		0x280
#define SCALER_SRC_BLEND_ALPHA		0x284
#define SCALER_DST_BLEND_COLOR		0x288
#define SCALER_DST_BLEND_ALPHA		0x28c
#define SCALER_SEL_INV_MASK		(1 << 31)
#define SCALER_SEL_MASK			(2 << 29)
#define SCALER_OP_SEL_INV_MASK		(1 << 28)
#define SCALER_OP_SEL_MASK		(0xf << 24)
#define SCALER_SEL_INV_SHIFT		(31)
#define SCALER_SEL_SHIFT		(29)
#define SCALER_OP_SEL_INV_SHIFT		(28)
#define SCALER_OP_SEL_SHIFT		(24)

#define SCALER_FILL_COLOR		0x290

#define SCALER_TIMEOUT_CTRL		0x2c0
#define SCALER_TIMEOUT_CNT		0x2c4
