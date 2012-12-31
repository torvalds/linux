/* linux/arch/arm/mach-exynos/include/mach/regs-vp.h
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Video processor register header file for Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_REGS_VP_H
#define __ARCH_ARM_REGS_VP_H

/*
 * Register part
 */
#define S5P_VP_ENABLE				(0x0000)
#define S5P_VP_SRESET				(0x0004)
#define S5P_VP_SHADOW_UPDATE			(0x0008)
#define S5P_VP_FIELD_ID				(0x000C)
#define S5P_VP_MODE				(0x0010)
#define S5P_VP_IMG_SIZE_Y			(0x0014)
#define S5P_VP_IMG_SIZE_C			(0x0018)
#define S5P_VP_PER_RATE_CTRL			(0x001C)
#define S5P_VP_TOP_Y_PTR			(0x0028)
#define S5P_VP_BOT_Y_PTR			(0x002C)
#define S5P_VP_TOP_C_PTR			(0x0030)
#define S5P_VP_BOT_C_PTR			(0x0034)
#define S5P_VP_ENDIAN_MODE			(0x03CC)
#define S5P_VP_SRC_H_POSITION			(0x0044)
#define S5P_VP_SRC_V_POSITION			(0x0048)
#define S5P_VP_SRC_WIDTH			(0x004C)
#define S5P_VP_SRC_HEIGHT			(0x0050)
#define S5P_VP_DST_H_POSITION			(0x0054)
#define S5P_VP_DST_V_POSITION			(0x0058)
#define S5P_VP_DST_WIDTH			(0x005C)
#define S5P_VP_DST_HEIGHT			(0x0060)
#define S5P_VP_H_RATIO				(0x0064)
#define S5P_VP_V_RATIO				(0x0068)
#define S5P_VP_POLY8_Y0_LL			(0x006C)
#define S5P_VP_POLY8_Y0_LH			(0x0070)
#define S5P_VP_POLY8_Y0_HL			(0x0074)
#define S5P_VP_POLY8_Y0_HH			(0x0078)
#define S5P_VP_POLY8_Y1_LL			(0x007C)
#define S5P_VP_POLY8_Y1_LH			(0x0080)
#define S5P_VP_POLY8_Y1_HL			(0x0084)
#define S5P_VP_POLY8_Y1_HH			(0x0088)
#define S5P_VP_POLY8_Y2_LL			(0x008C)
#define S5P_VP_POLY8_Y2_LH			(0x0090)
#define S5P_VP_POLY8_Y2_HL			(0x0094)
#define S5P_VP_POLY8_Y2_HH			(0x0098)
#define S5P_VP_POLY8_Y3_LL			(0x009C)
#define S5P_VP_POLY8_Y3_LH			(0x00A0)
#define S5P_VP_POLY8_Y3_HL			(0x00A4)
#define S5P_VP_POLY8_Y3_HH			(0x00A8)
#define S5P_VP_POLY4_Y0_LL			(0x00EC)
#define S5P_VP_POLY4_Y0_LH			(0x00F0)
#define S5P_VP_POLY4_Y0_HL			(0x00F4)
#define S5P_VP_POLY4_Y0_HH			(0x00F8)
#define S5P_VP_POLY4_Y1_LL			(0x00FC)
#define S5P_VP_POLY4_Y1_LH			(0x0100)
#define S5P_VP_POLY4_Y1_HL			(0x0104)
#define S5P_VP_POLY4_Y1_HH			(0x0108)
#define S5P_VP_POLY4_Y2_LL			(0x010C)
#define S5P_VP_POLY4_Y2_LH			(0x0110)
#define S5P_VP_POLY4_Y2_HL			(0x0114)
#define S5P_VP_POLY4_Y2_HH			(0x0118)
#define S5P_VP_POLY4_Y3_LL			(0x011C)
#define S5P_VP_POLY4_Y3_LH			(0x0120)
#define S5P_VP_POLY4_Y3_HL			(0x0124)
#define S5P_VP_POLY4_Y3_HH			(0x0128)
#define S5P_VP_POLY4_C0_LL			(0x012C)
#define S5P_VP_POLY4_C0_LH			(0x0130)
#define S5P_VP_POLY4_C0_HL			(0x0134)
#define S5P_VP_POLY4_C0_HH			(0x0138)
#define S5P_VP_POLY4_C1_LL			(0x013C)
#define S5P_VP_POLY4_C1_LH			(0x0140)
#define S5P_VP_POLY4_C1_HL			(0x0144)
#define S5P_VP_POLY4_C1_HH			(0x0148)
#define S5P_VP_FIELD_ID_S			(0x016C)
#define S5P_VP_MODE_S				(0x0170)
#define S5P_VP_IMG_SIZE_Y_S			(0x0174)
#define S5P_VP_IMG_SIZE_C_S			(0x0178)
#define S5P_VP_TOP_Y_PTR_S			(0x0190)
#define S5P_VP_BOT_Y_PTR_S			(0x0194)
#define S5P_VP_TOP_C_PTR_S			(0x0198)
#define S5P_VP_BOT_C_PTR_S			(0x019C)
#define S5P_VP_SRC_H_POSITION_S			(0x01AC)
#define S5P_VP_SRC_V_POSITION_S			(0x01B0)
#define S5P_VP_SRC_WIDTH_S			(0x01B4)
#define S5P_VP_SRC_HEIGHT_S			(0x01B8)
#define S5P_VP_DST_H_POSITION_S			(0x01BC)
#define S5P_VP_DST_V_POSITION_S			(0x01C0)
#define S5P_VP_DST_WIDTH_S			(0x01C4)
#define S5P_VP_DST_HEIGHT_S			(0x01C8)
#define S5P_VP_H_RATIO_S			(0x01CC)
#define S5P_VP_V_RATIO_S			(0x01D0)
#define S5P_PP_CSC_Y2Y_COEF			(0x01D4)
#define S5P_PP_CSC_CB2Y_COEF			(0x01D8)
#define S5P_PP_CSC_CR2Y_COEF			(0x01DC)
#define S5P_PP_CSC_Y2CB_COEF			(0x01E0)
#define S5P_PP_CSC_CB2CB_COEF			(0x01E4)
#define S5P_PP_CSC_CR2CB_COEF			(0x01E8)
#define S5P_PP_CSC_Y2CR_COEF			(0x01EC)
#define S5P_PP_CSC_CB2CR_COEF			(0x01F0)
#define S5P_PP_CSC_CR2CR_COEF			(0x01F4)
#define S5P_PP_BYPASS				(0x0200)
#define S5P_PP_SATURATION			(0x020C)
#define S5P_PP_SHARPNESS			(0x0210)
#define S5P_PP_LINE_EQ0				(0x0218)
#define S5P_PP_LINE_EQ1				(0x021C)
#define S5P_PP_LINE_EQ2				(0x0220)
#define S5P_PP_LINE_EQ3				(0x0224)
#define S5P_PP_LINE_EQ4				(0x0228)
#define S5P_PP_LINE_EQ5				(0x022C)
#define S5P_PP_LINE_EQ6				(0x0230)
#define S5P_PP_LINE_EQ7				(0x0234)
#define S5P_PP_BRIGHT_OFFSET			(0x0238)
#define S5P_PP_CSC_EN				(0x023C)
#define S5P_PP_BYPASS_S				(0x0258)
#define S5P_PP_SATURATION_S			(0x025C)
#define S5P_PP_SHARPNESS_S			(0x0260)
#define S5P_PP_LINE_EQ0_S			(0x0268)
#define S5P_PP_LINE_EQ1_S			(0x026C)
#define S5P_PP_LINE_EQ2_S			(0x0270)
#define S5P_PP_LINE_EQ3_S			(0x0274)
#define S5P_PP_LINE_EQ4_S			(0x0278)
#define S5P_PP_LINE_EQ5_S			(0x027C)
#define S5P_PP_LINE_EQ6_S			(0x0280)
#define S5P_PP_LINE_EQ7_S			(0x0284)
#define S5P_PP_BRIGHT_OFFSET_S			(0x0288)
#define S5P_PP_CSC_EN_S				(0x028C)
#define S5P_PP_CSC_Y2Y_COEF_S			(0x0290)
#define S5P_PP_CSC_CB2Y_COEF_S			(0x0294)
#define S5P_PP_CSC_CR2Y_COEF_S			(0x0298)
#define S5P_PP_CSC_Y2CB_COEF_S			(0x029C)
#define S5P_PP_CSC_CB2CB_COEF_S			(0x02A0)
#define S5P_PP_CSC_CR2CB_COEF_S			(0x02A4)
#define S5P_PP_CSC_Y2CR_COEF_S			(0x02A8)
#define S5P_PP_CSC_CB2CR_COEF_S			(0x02AC)
#define S5P_PP_CSC_CR2CR_COEF_S			(0x02B0)
#define S5P_VP_ENDIAN_MODE_S			(0x03EC)
#define S5P_VP_VERSION_INFO			(0x03FC)

/*
 * Bit definition part
 */
 /* VP_ENABLE */
#define S5P_VP_ENABLE_ON_S			(1 << 2)
#define S5P_VP_ENABLE_OPERATING			(1 << 1)
#define S5P_VP_ENABLE_IDLE_MODE			(0 << 1)
#define S5P_VP_ENABLE_ON			(1 << 0)
#define S5P_VP_ENABLE_OFF			(0 << 0)

/* VP_SRESET */
#define S5P_VP_SRESET_LAST_COMPLETE		(0 << 0)
#define S5P_VP_SRESET_PROCESSING		(1 << 0)

/* VP_SHADOW_UPDATE */
#define S5P_VP_SHADOW_UPDATE_DISABLE		(0 << 0)
#define S5P_VP_SHADOW_UPDATE_ENABLE		(1 << 0)

/* VP_FIELD_ID */
#define S5P_VP_FIELD_ID_TOP			(0 << 0)
#define S5P_VP_FIELD_ID_BOTTOM			(1 << 0)

/* VP_MODE */
#define S5P_VP_MODE_IMG_TYPE_YUV420_NV12	(0 << 6)
#define S5P_VP_MODE_IMG_TYPE_YUV420_NV21	(1 << 6)
#define S5P_VP_MODE_LINE_SKIP_OFF		(0 << 5)
#define S5P_VP_MODE_LINE_SKIP_ON		(1 << 5)
#define S5P_VP_MODE_MEM_MODE_LINEAR		(0 << 4)
#define S5P_VP_MODE_MEM_MODE_2D_TILE		(1 << 4)
#define S5P_VP_MODE_CROMA_EXP_C_TOP_PTR		(0 << 3)
#define S5P_VP_MODE_CROMA_EXP_C_TOPBOTTOM_PTR	(1 << 3)
#define S5P_VP_MODE_FIELD_ID_MAN_TOGGLING	(0 << 2)
#define S5P_VP_MODE_FIELD_ID_AUTO_TOGGLING	(1 << 2)
#define S5P_VP_MODE_2D_IPC_DISABLE		(0 << 1)
#define S5P_VP_MODE_2D_IPC_ENABLE		(1 << 1)

/* VP_IMG_SIZE_Y */
/* VP_IMG_SIZE_C */
#define S5P_VP_IMG_HSIZE(x)			(((x) & 0x3FFF) << 16)
#define S5P_VP_IMG_VSIZE(x)			(((x) & 0x3FFF) << 0)
#define S5P_VP_IMG_SIZE_ILLEGAL(x)		((x) & 0x7)

/* VP_PER_RATE_CTRL */ /* Not support in S5PV210 */
#define S5P_VP_PEL_RATE_CTRL(x)			(((x) & 0x3) << 0)

/* VP_TOP_Y_PTR */
/* VP_BOT_Y_PTR */
/* VP_TOP_C_PTR */
/* VP_BOT_C_PTR */
#define S5P_VP_PTR_ILLEGAL(x)			((x) & 0x7)

/* VP_ENDIAN_MODE */
#define S5P_VP_ENDIAN_MODE_BIG			(0 << 0)
#define S5P_VP_ENDIAN_MODE_LITTLE		(1 << 0)

/* VP_SRC_H_POSITION */
#define S5P_VP_SRC_H_POSITION_VAL(x)		(((x) & 0x7FF) << 4)
#define S5P_VP_SRC_X_FRACT_STEP(x)		((x) & 0xF)

/* VP_SRC_V_POSITION */
#define S5P_VP_SRC_V_POSITION_VAL(x)		((x) & 0x7FF)

/* VP_SRC_WIDTH */
/* VP_SRC_HEIGHT */
#define S5P_VP_SRC_WIDTH_VAL(x)			((x) & 0x7FF)
#define S5P_VP_SRC_HEIGHT_VAL(x)		((x) & 0x7FF)

/* VP_DST_H_POSITION */
/* VP_DST_V_POSITION */
#define S5P_VP_DST_H_POSITION_VAL(x)		((x) & 0x7FF)
#define S5P_VP_DST_V_POSITION_VAL(x)		((x) & 0x7FF)

/* VP_DST_WIDTH */
/* VP_DST_HEIGHT */
#define S5P_VP_DST_WIDTH_VAL(x)			((x) & 0x7FF)
#define S5P_VP_DST_HEIGHT_VAL(x)		((x) & 0x7FF)

/* VP_H_RATIO */
/* VP_V_RATIO */
#define S5P_VP_H_RATIO_VAL(x)			((x) & 0x7FFFF)
#define S5P_VP_V_RATIO_VAL(x)			((x) & 0x7FFFF)

/* PP_CSC_Y2Y_COEF */
#define S5P_PP_Y2Y_COEF_601_TO_709		(0x400)
#define S5P_PP_Y2Y_COEF_709_TO_601		(0x400)

/* PP_CSC_CB2Y_COEF */
#define S5P_PP_CB2Y_COEF_601_TO_709		(0x879)
#define S5P_PP_CB2Y_COEF_709_TO_601		(0x068)

/* PP_CSC_CR2Y_COEF */
#define S5P_PP_CR2Y_COEF_601_TO_709		(0x8D9)
#define S5P_PP_CR2Y_COEF_709_TO_601		(0x0C9)

/* PP_CSC_Y2CB_COEF */
#define S5P_PP_Y2CB_COEF_601_TO_709		(0x0)
#define S5P_PP_Y2CB_COEF_709_TO_601		(0x0)

/* PP_CSC_CB2CB_COEF */
#define S5P_PP_CB2CB_COEF_601_TO_709		(0x413)
#define S5P_PP_CB2CB_COEF_709_TO_601		(0x3F6)

/* PP_CSC_CR2CB_COEF */
#define S5P_PP_CR2CB_COEF_601_TO_709		(0x875)
#define S5P_PP_CR2CB_COEF_709_TO_601		(0x871)

/* PP_CSC_Y2CR_COEF */
#define S5P_PP_Y2CR_COEF_601_TO_709		(0x0)
#define S5P_PP_Y2CR_COEF_709_TO_601		(0x0)

/* PP_CSC_CB2CR_COEF */
#define S5P_PP_CB2CR_COEF_601_TO_709		(0x04D)
#define S5P_PP_CB2CR_COEF_709_TO_601		(0x84A)

/* PP_CSC_CR2CR_COEF */
#define S5P_PP_CR2CR_COEF_601_TO_709		(0x41A)
#define S5P_PP_CR2CR_COEF_709_TO_601		(0xBEF)

#define S5P_PP_CSC_COEF(x)			((x) & 0xFFF)

/* PP_BYPASS */
#define S5P_VP_BY_PASS_ENABLE			(0)
#define S5P_VP_BY_PASS_DISABLE			(1)

/* PP_SATURATION */
#define S5P_VP_SATURATION(x)			((x) & 0xFF)

/* PP_SHARPNESS */
#define S5P_VP_TH_HNOISE(x)			(((x) & 0xF) << 8)
#define S5P_VP_SHARPNESS(x)			((x) & 0x3)

/* PP_LINE_EQ0 ~ 7 */
#define S5P_VP_LINE_INTC(x)			(((x) & 0xFFFF) << 8)
#define S5P_VP_LINE_SLOPE(x)			((x) & 0xFF)
#define S5P_VP_LINE_INTC_CLEAR(x)		((x) & ~(0xFFFF << 8))
#define S5P_VP_LINE_SLOPE_CLEAR(x)		((x) & ~0xFF)

/* PP_BRIGHT_OFFSET */
#define S5P_VP_BRIGHT_OFFSET(x)			((x) & 0x1FF)

/* PP_CSC_EN */
#define S5P_VP_SUB_Y_OFFSET_ENABLE		(1 << 1)
#define S5P_VP_SUB_Y_OFFSET_DISABLE		(0 << 1)
#define S5P_VP_CSC_ENABLE			(1)
#define S5P_VP_CSC_DISABLE			(0)

#endif /* __ARCH_ARM_REGS_VP_H */
