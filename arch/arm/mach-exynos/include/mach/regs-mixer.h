/* linux/arch/arm/mach-exynos/include/mach/regs-mixer.h
 *
 * Copyright (c) 2010 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Mixer register header file for Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_REGS_MIXER_H
#define __ARCH_ARM_REGS_MIXER_H

/*
 * Register part
 */
#define S5P_MXR_STATUS				(0x0000)
#define S5P_MXR_CFG				(0x0004)
#define S5P_MXR_INT_EN				(0x0008)
#define S5P_MXR_INT_STATUS			(0x000C)
#define S5P_MXR_LAYER_CFG			(0x0010)
#define S5P_MXR_VIDEO_CFG			(0x0014)
#define S5P_MXR_GRAPHIC0_CFG			(0x0020)
#define S5P_MXR_GRAPHIC0_BASE			(0x0024)
#define S5P_MXR_GRAPHIC0_SPAN			(0x0028)
#define S5P_MXR_GRAPHIC0_SXY			(0x002C)
#define S5P_MXR_GRAPHIC0_WH			(0x0030)
#define S5P_MXR_GRAPHIC0_DXY			(0x0034)
#define S5P_MXR_GRAPHIC0_BLANK			(0x0038)
#define S5P_MXR_GRAPHIC1_CFG			(0x0040)
#define S5P_MXR_GRAPHIC1_BASE			(0x0044)
#define S5P_MXR_GRAPHIC1_SPAN			(0x0048)
#define S5P_MXR_GRAPHIC1_SXY			(0x004C)
#define S5P_MXR_GRAPHIC1_WH			(0x0050)
#define S5P_MXR_GRAPHIC1_DXY			(0x0054)
#define S5P_MXR_GRAPHIC1_BLANK			(0x0058)
#define S5P_MXR_BG_CFG				(0x0060)
#define S5P_MXR_BG_COLOR0			(0x0064)
#define S5P_MXR_BG_COLOR1			(0x0068)
#define S5P_MXR_BG_COLOR2			(0x006C)
#define S5P_MXR_CM_COEFF_Y			(0x0080)
#define S5P_MXR_CM_COEFF_CB			(0x0084)
#define S5P_MXR_CM_COEFF_CR			(0x0088)
#define S5P_MXR_VER				(0x0100)

#define S5P_MXR_STATUS_S			(0x2000)
#define S5P_MXR_CFG_S				(0x2004)
#define S5P_MXR_LAYER_CFG_S			(0x2010)
#define S5P_MXR_VIDEO_CFG_S			(0x2014)
#define S5P_MXR_GRAPHIC0_CFG_S			(0x2020)
#define S5P_MXR_GRAPHIC0_BASE_S			(0x2024)
#define S5P_MXR_GRAPHIC0_SPAN_S			(0x2028)
#define S5P_MXR_GRAPHIC0_SXY_S			(0x202C)
#define S5P_MXR_GRAPHIC0_WH_S			(0x2030)
#define S5P_MXR_GRAPHIC0_DXY_S			(0x2034)
#define S5P_MXR_GRAPHIC0_BLANK_PIXEL_S		(0x2038)
#define S5P_MXR_GRAPHIC1_CFG_S			(0x2040)
#define S5P_MXR_GRAPHIC1_BASE_S			(0x2044)
#define S5P_MXR_GRAPHIC1_SPAN_S			(0x2048)
#define S5P_MXR_GRAPHIC1_SXY_S			(0x204C)
#define S5P_MXR_GRAPHIC1_WH_S			(0x2050)
#define S5P_MXR_GRAPHIC1_DXY_S			(0x2054)
#define S5P_MXR_GRAPHIC1_BLANK_PIXEL_S		(0x2058)
#define S5P_MXR_BG_COLOR0_S			(0x2064)
#define S5P_MXR_BG_COLOR1_S			(0x2068)
#define S5P_MXR_BG_COLOR2_S			(0x206C)

/*
 * Bit definition part
 */
/* MIXER_STATUS */
#define S5P_MXR_STATUS_16_BURST			(1 << 7)
#define S5P_MXR_STATUS_8_BURST			(0 << 7)
#define S5P_MXR_STATUS_LITTLE_ENDIAN		(0 << 3)
#define S5P_MXR_STATUS_BIG_ENDIAN		(1 << 3)
#define S5P_MXR_STATUS_SYNC_DISABLE		(0 << 2)
#define S5P_MXR_STATUS_SYNC_ENABLE		(1 << 2)
#define S5P_MXR_STATUS_OPERATING		(0 << 1)
#define S5P_MXR_STATUS_IDLE_MODE		(1 << 1)
#define S5P_MXR_STATUS_STOP			(0 << 0)
#define S5P_MXR_STATUS_RUN			(1 << 0)

/* MIXER_CGF */
#define S5P_MXR_CFG_TV_OUT			(~(1 << 7))
#define S5P_MXR_CFG_HDMI_OUT			(1 << 7)
#define S5P_MXR_CFG_HD_720P			(0 << 6)
#define S5P_MXR_CFG_HD_1080I			(1 << 6)
#define S5P_MXR_CFG_HD_1080P			(1 << 6)
#define S5P_MXR_CFG_GRAPHIC1_DISABLE		(0 << 5)
#define S5P_MXR_CFG_GRAPHIC1_ENABLE		(1 << 5)
#define S5P_MXR_CFG_GRAPHIC0_DISABLE		(0 << 4)
#define S5P_MXR_CFG_GRAPHIC0_ENABLE		(1 << 4)
#define S5P_MXR_CFG_VIDEO_DISABLE		(0 << 3)
#define S5P_MXR_CFG_VIDEO_ENABLE		(1 << 3)
#define S5P_MXR_CFG_INTERLACE			(~(1 << 2))
#define S5P_MXR_CFG_PROGRASSIVE			(1 << 2)
#define S5P_MXR_CFG_NTSC			(0 << 1)
#define S5P_MXR_CFG_PAL				(1 << 1)
#define S5P_MXR_CFG_SD				(0 << 0)
#define S5P_MXR_CFG_HD				(1 << 0)

/* MIXER_INT_EN */
#define S5P_MXR_INT_EN_VSYNC_ENABLE		(1 << 11)
#define S5P_MXR_INT_EN_VP_DISABLE		(0 << 10)
#define S5P_MXR_INT_EN_VP_ENABLE		(1 << 10)
#define S5P_MXR_INT_EN_GRP1_DISABLE		(0 << 9)
#define S5P_MXR_INT_EN_GRP1_ENABLE		(1 << 9)
#define S5P_MXR_INT_EN_GRP0_DISABLE		(0 << 8)
#define S5P_MXR_INT_EN_GRP0_ENABLE		(1 << 8)

/* MIXER_INT_STATUS */
#define S5P_MXR_INT_STATUS_VSYNC_CLEARED	(1 << 11)
#define S5P_MXR_INT_STATUS_VP_N_FIRED		(0 << 10)
#define S5P_MXR_INT_STATUS_VP_FIRED		(1 << 10)
#define S5P_MXR_INT_STATUS_GRP1_N_FIRED		(0 << 9)
#define S5P_MXR_INT_STATUS_GRP1_FIRED		(1 << 9)
#define S5P_MXR_INT_STATUS_GRP0_N_FIRED		(0 << 8)
#define S5P_MXR_INT_STATUS_GRP0_FIRED		(1 << 8)
#define S5P_MXR_INT_STATUS_INT_FIRED		(1 << 0)

/* MIXER_LAYER_CFG */
#define S5P_MXR_LAYER_CFG_GRP1_HIDE		(0 << 8)
#define S5P_MXR_LAYER_CFG_GRP1_PRIORITY(x)	(((x) & 0xF) << 8)
#define S5P_MXR_LAYER_CFG_GRP1_PRIORITY_CLR(x)	((x) & (~(0xF << 8)))
#define S5P_MXR_LAYER_CFG_GRP1_PRIORITY_INFO(x)	((x) & (0xF << 8))
#define S5P_MXR_LAYER_CFG_GRP0_HIDE		(0 << 4)
#define S5P_MXR_LAYER_CFG_GRP0_PRIORITY(x)	(((x) & 0xF) << 4)
#define S5P_MXR_LAYER_CFG_GRP0_PRIORITY_CLR(x)	((x) & (~(0xF << 4)))
#define S5P_MXR_LAYER_CFG_GRP0_PRIORITY_INFO(x)	((x) & (0xF << 4))
#define S5P_MXR_LAYER_CFG_VID_HIDE		(0 << 0)
#define S5P_MXR_LAYER_CFG_VID_PRIORITY(x)	(((x) & 0xF) << 0)
#define S5P_MXR_LAYER_CFG_VID_PRIORITY_CLR(x)	((x) & (~(0xF << 0)))
#define S5P_MXR_LAYER_CFG_VID_PRIORITY_INFO(x)	((x) & (0xF << 0))

/* MIXER_VIDEO_CFG */
#define S5P_MXR_VIDEO_CFG_BLEND_DIS		(0 << 16)
#define S5P_MXR_VIDEO_CFG_BLEND_EN		(1 << 16)
#define S5P_MXR_VIDEO_CFG_ALPHA_MASK		(0xFF)
#define S5P_MXR_VIDEO_CFG_ALPHA_VALUE(x)	(((x) & 0xFF) << 0)
#define S5P_MXR_VIDEO_CFG_ALPHA_VALUE_CLR(x)	((x) & (~(0xFF << 0)))

/* MIXER_VIDEO_LIMITER_PARA_CFG */

/* MIXER_GRAPHIC0_CFG */
/* MIXER_GRAPHIC1_CFG */
#define S5P_MXR_BLANK_CHANGE_NEW_PIXEL		(1 << 21)
#define S5P_MXR_BLANK_NOT_CHANGE_NEW_PIXEL	(0 << 21)
#define S5P_MXR_PRE_MUL_MODE			(1 << 20)
#define S5P_MXR_NORMAL_MODE			(0 << 20)
#define S5P_MXR_WIN_BLEND_ENABLE		(1 << 17)
#define S5P_MXR_WIN_BLEND_DISABLE		(0 << 17)
#define S5P_MXR_PIXEL_BLEND_ENABLE		(1 << 16)
#define S5P_MXR_PIXEL_BLEND_DISABLE		(0 << 16)
#define S5P_MXR_EG_COLOR_FORMAT(x)		(((x) & 0xF) << 8)
#define S5P_MXR_EG_COLOR_FORMAT_CLEAR(x)	((x) & (~(0xF << 8)))
#define S5P_MXR_GRP_ALPHA_VALUE(x)		(((x) & 0xFF) << 0)
#define S5P_MXR_GRP_ALPHA_VALUE_CLEAR(x)	((x) & (~(0xFF << 0)))

/* MIXER_GRAPHIC0_BASE */
/* MIXER_GRAPHIC1_BASE */
#define S5P_MXR_GPR_BASE(x)			((x) & 0xFFFFFFFF)
#define S5P_MXR_GRP_ADDR_ILLEGAL(x)		((x) & 0x3)

/* MIXER_GRAPHIC0_SPAN */
#define S5P_MXR_GRP_SPAN(x)			((x) & 0x7FFF)

/* MIXER_GRAPHIC0_WH */
#define S5P_MXR_GRP_H_SCALE(x)			(((x) & 0x1) << 28)
#define S5P_MXR_GRP_V_SCALE(x)			(((x) & 0x1) << 12)
#define S5P_MXR_GRP_WIDTH(x)			(((x) & 0x7FF) << 16)
#define S5P_MXR_GRP_HEIGHT(x)			(((x) & 0x7FF) << 0)

/* MIXER_GRAPHIC0_XY */
#define S5P_MXR_GRP_STARTX(x)			(((x) & 0x7FF) << 16)
#define S5P_MXR_GRP_STARTY(x)			(((x) & 0x7FF) << 0)

/* MIXER_GRAPHIC0_DXY */
#define S5P_MXR_GRP_DESTX(x)			(((x) & 0x7FF) << 16)
#define S5P_MXR_GRP_DESTY(x)			(((x) & 0x7FF) << 0)

/* MIXER_GRAPHIC0_BLANK */
#define S5P_MXR_GPR_BLANK_COLOR(x)		((x) & 0xFFFFFFFF)

/* MIXER_BG_CFG */
#define S5P_MXR_BG_CR_DIHER_EN			(1 << 19)	/* Not support in S5PV210 */
#define S5P_MXR_BG_CB_DIHER_EN			(1 << 18)	/* Not support in S5PV210 */
#define S5P_MXR_BG_Y_DIHER_EN			(1 << 17)	/* Not support in S5PV210 */

/* MIXER_BG_COLOR0/1/2 */
#define S5P_MXR_BG_COEFF_0(x)			(((x) & 0x3F) << 20)
#define S5P_MXR_BG_COEFF_1(x)			(((x) & 0x3F) << 10)
#define S5P_MXR_BG_COEFF_2(x)			(((x) & 0x3F) << 0)

/* MIXER_CM_COEFF_Y */
#define S5P_MXR_BG_COLOR_WIDE			(1 << 30)
#define S5P_MXR_BG_COLOR_NARROW			(0 << 30)
#define S5P_MXR_BG_COLOR_Y(x)			(((x) & 0xFF) << 16)

/* MIXER_CM_COEFF_CB */
#define S5P_MXR_BG_COLOR_CB(x)			(((x) & 0xFF) << 8)

/* MIXER_CM_COEFF_Cr */
#define S5P_MXR_BG_COLOR_CR(x)			(((x) & 0xFF) << 0)
#endif /* __ARCH_ARM_REGS_MIXER_H */

