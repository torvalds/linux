/*
 *
 *  Cloned from drivers/media/video/s5p-tv/regs-mixer.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Mixer register header file for Samsung Mixer driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#ifndef SAMSUNG_REGS_MIXER_H
#define SAMSUNG_REGS_MIXER_H

/*
 * Register part
 */
#define MXR_STATUS			0x0000
#define MXR_CFG				0x0004
#define MXR_INT_EN			0x0008
#define MXR_INT_STATUS			0x000C
#define MXR_LAYER_CFG			0x0010
#define MXR_VIDEO_CFG			0x0014
#define MXR_GRAPHIC0_CFG		0x0020
#define MXR_GRAPHIC0_BASE		0x0024
#define MXR_GRAPHIC0_SPAN		0x0028
#define MXR_GRAPHIC0_SXY		0x002C
#define MXR_GRAPHIC0_WH			0x0030
#define MXR_GRAPHIC0_DXY		0x0034
#define MXR_GRAPHIC0_BLANK		0x0038
#define MXR_GRAPHIC1_CFG		0x0040
#define MXR_GRAPHIC1_BASE		0x0044
#define MXR_GRAPHIC1_SPAN		0x0048
#define MXR_GRAPHIC1_SXY		0x004C
#define MXR_GRAPHIC1_WH			0x0050
#define MXR_GRAPHIC1_DXY		0x0054
#define MXR_GRAPHIC1_BLANK		0x0058
#define MXR_BG_CFG			0x0060
#define MXR_BG_COLOR0			0x0064
#define MXR_BG_COLOR1			0x0068
#define MXR_BG_COLOR2			0x006C
#define MXR_CM_COEFF_Y			0x0080
#define MXR_CM_COEFF_CB			0x0084
#define MXR_CM_COEFF_CR			0x0088
#define MXR_MO				0x0304
#define MXR_RESOLUTION			0x0310

#define MXR_CFG_S			0x2004
#define MXR_GRAPHIC0_BASE_S		0x2024
#define MXR_GRAPHIC1_BASE_S		0x2044

/* for parametrized access to layer registers */
#define MXR_GRAPHIC_CFG(i)		(0x0020 + (i) * 0x20)
#define MXR_GRAPHIC_BASE(i)		(0x0024 + (i) * 0x20)
#define MXR_GRAPHIC_SPAN(i)		(0x0028 + (i) * 0x20)
#define MXR_GRAPHIC_SXY(i)		(0x002C + (i) * 0x20)
#define MXR_GRAPHIC_WH(i)		(0x0030 + (i) * 0x20)
#define MXR_GRAPHIC_DXY(i)		(0x0034 + (i) * 0x20)
#define MXR_GRAPHIC_BLANK(i)		(0x0038 + (i) * 0x20)
#define MXR_GRAPHIC_BASE_S(i)		(0x2024 + (i) * 0x20)

/*
 * Bit definition part
 */

/* generates mask for range of bits */
#define MXR_MASK(high_bit, low_bit) \
	(((2 << ((high_bit) - (low_bit))) - 1) << (low_bit))

#define MXR_MASK_VAL(val, high_bit, low_bit) \
	(((val) << (low_bit)) & MXR_MASK(high_bit, low_bit))

/* bits for MXR_STATUS */
#define MXR_STATUS_SOFT_RESET		(1 << 8)
#define MXR_STATUS_16_BURST		(1 << 7)
#define MXR_STATUS_BURST_MASK		(1 << 7)
#define MXR_STATUS_BIG_ENDIAN		(1 << 3)
#define MXR_STATUS_ENDIAN_MASK		(1 << 3)
#define MXR_STATUS_SYNC_ENABLE		(1 << 2)
#define MXR_STATUS_REG_IDLE		(1 << 1)
#define MXR_STATUS_REG_RUN		(1 << 0)

/* bits for MXR_CFG */
#define MXR_CFG_LAYER_UPDATE		(1 << 31)
#define MXR_CFG_LAYER_UPDATE_COUNT_MASK (3 << 29)
#define MXR_CFG_QUANT_RANGE_FULL	(0 << 9)
#define MXR_CFG_QUANT_RANGE_LIMITED	(1 << 9)
#define MXR_CFG_RGB601			(0 << 10)
#define MXR_CFG_RGB709			(1 << 10)

#define MXR_CFG_RGB_FMT_MASK		0x600
#define MXR_CFG_OUT_YUV444		(0 << 8)
#define MXR_CFG_OUT_RGB888		(1 << 8)
#define MXR_CFG_OUT_MASK		(1 << 8)
#define MXR_CFG_DST_SDO			(0 << 7)
#define MXR_CFG_DST_HDMI		(1 << 7)
#define MXR_CFG_DST_MASK		(1 << 7)
#define MXR_CFG_SCAN_HD_720		(0 << 6)
#define MXR_CFG_SCAN_HD_1080		(1 << 6)
#define MXR_CFG_GRP1_ENABLE		(1 << 5)
#define MXR_CFG_GRP0_ENABLE		(1 << 4)
#define MXR_CFG_VP_ENABLE		(1 << 3)
#define MXR_CFG_SCAN_INTERLACE		(0 << 2)
#define MXR_CFG_SCAN_PROGRESSIVE	(1 << 2)
#define MXR_CFG_SCAN_NTSC		(0 << 1)
#define MXR_CFG_SCAN_PAL		(1 << 1)
#define MXR_CFG_SCAN_SD			(0 << 0)
#define MXR_CFG_SCAN_HD			(1 << 0)
#define MXR_CFG_SCAN_MASK		0x47

/* bits for MXR_VIDEO_CFG */
#define MXR_VID_CFG_BLEND_EN		(1 << 16)

/* bits for MXR_GRAPHICn_CFG */
#define MXR_GRP_CFG_COLOR_KEY_DISABLE	(1 << 21)
#define MXR_GRP_CFG_BLEND_PRE_MUL	(1 << 20)
#define MXR_GRP_CFG_WIN_BLEND_EN	(1 << 17)
#define MXR_GRP_CFG_PIXEL_BLEND_EN	(1 << 16)
#define MXR_GRP_CFG_MISC_MASK		((3 << 16) | (3 << 20) | 0xff)
#define MXR_GRP_CFG_FORMAT_VAL(x)	MXR_MASK_VAL(x, 11, 8)
#define MXR_GRP_CFG_FORMAT_MASK		MXR_GRP_CFG_FORMAT_VAL(~0)
#define MXR_GRP_CFG_ALPHA_VAL(x)	MXR_MASK_VAL(x, 7, 0)

/* bits for MXR_GRAPHICn_WH */
#define MXR_GRP_WH_H_SCALE(x)		MXR_MASK_VAL(x, 28, 28)
#define MXR_GRP_WH_V_SCALE(x)		MXR_MASK_VAL(x, 12, 12)
#define MXR_GRP_WH_WIDTH(x)		MXR_MASK_VAL(x, 26, 16)
#define MXR_GRP_WH_HEIGHT(x)		MXR_MASK_VAL(x, 10, 0)

/* bits for MXR_RESOLUTION */
#define MXR_MXR_RES_HEIGHT(x)		MXR_MASK_VAL(x, 26, 16)
#define MXR_MXR_RES_WIDTH(x)		MXR_MASK_VAL(x, 10, 0)

/* bits for MXR_GRAPHICn_SXY */
#define MXR_GRP_SXY_SX(x)		MXR_MASK_VAL(x, 26, 16)
#define MXR_GRP_SXY_SY(x)		MXR_MASK_VAL(x, 10, 0)

/* bits for MXR_GRAPHICn_DXY */
#define MXR_GRP_DXY_DX(x)		MXR_MASK_VAL(x, 26, 16)
#define MXR_GRP_DXY_DY(x)		MXR_MASK_VAL(x, 10, 0)

/* bits for MXR_INT_EN */
#define MXR_INT_EN_VSYNC		(1 << 11)
#define MXR_INT_EN_ALL			(0x0f << 8)

/* bits for MXR_INT_STATUS */
#define MXR_INT_CLEAR_VSYNC		(1 << 11)
#define MXR_INT_STATUS_VSYNC		(1 << 0)

/* bits for MXR_LAYER_CFG */
#define MXR_LAYER_CFG_GRP1_VAL(x)	MXR_MASK_VAL(x, 11, 8)
#define MXR_LAYER_CFG_GRP1_MASK		MXR_LAYER_CFG_GRP1_VAL(~0)
#define MXR_LAYER_CFG_GRP0_VAL(x)	MXR_MASK_VAL(x, 7, 4)
#define MXR_LAYER_CFG_GRP0_MASK		MXR_LAYER_CFG_GRP0_VAL(~0)
#define MXR_LAYER_CFG_VP_VAL(x)		MXR_MASK_VAL(x, 3, 0)
#define MXR_LAYER_CFG_VP_MASK		MXR_LAYER_CFG_VP_VAL(~0)

/* bits for MXR_CM_COEFF_Y */
#define MXR_CM_COEFF_RGB_FULL		(1 << 30)

#endif /* SAMSUNG_REGS_MIXER_H */

