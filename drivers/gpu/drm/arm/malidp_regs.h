/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * ARM Mali DP500/DP550/DP650 registers definition.
 */

#ifndef __MALIDP_REGS_H__
#define __MALIDP_REGS_H__

/*
 * abbreviations used:
 *    - DC - display core (general settings)
 *    - DE - display engine
 *    - SE - scaling engine
 */

/* interrupt bit masks */
#define MALIDP_DE_IRQ_UNDERRUN			(1 << 0)

#define MALIDP500_DE_IRQ_AXI_ERR		(1 << 4)
#define MALIDP500_DE_IRQ_VSYNC			(1 << 5)
#define MALIDP500_DE_IRQ_PROG_LINE		(1 << 6)
#define MALIDP500_DE_IRQ_SATURATION		(1 << 7)
#define MALIDP500_DE_IRQ_CONF_VALID		(1 << 8)
#define MALIDP500_DE_IRQ_CONF_MODE		(1 << 11)
#define MALIDP500_DE_IRQ_CONF_ACTIVE		(1 << 17)
#define MALIDP500_DE_IRQ_PM_ACTIVE		(1 << 18)
#define MALIDP500_DE_IRQ_TESTMODE_ACTIVE	(1 << 19)
#define MALIDP500_DE_IRQ_FORCE_BLNK_ACTIVE	(1 << 24)
#define MALIDP500_DE_IRQ_AXI_BUSY		(1 << 28)
#define MALIDP500_DE_IRQ_GLOBAL			(1 << 31)
#define MALIDP500_SE_IRQ_CONF_MODE		(1 << 0)
#define MALIDP500_SE_IRQ_CONF_VALID		(1 << 4)
#define MALIDP500_SE_IRQ_INIT_BUSY		(1 << 5)
#define MALIDP500_SE_IRQ_AXI_ERROR		(1 << 8)
#define MALIDP500_SE_IRQ_OVERRUN		(1 << 9)
#define MALIDP500_SE_IRQ_PROG_LINE1		(1 << 12)
#define MALIDP500_SE_IRQ_PROG_LINE2		(1 << 13)
#define MALIDP500_SE_IRQ_CONF_ACTIVE		(1 << 17)
#define MALIDP500_SE_IRQ_PM_ACTIVE		(1 << 18)
#define MALIDP500_SE_IRQ_AXI_BUSY		(1 << 28)
#define MALIDP500_SE_IRQ_GLOBAL			(1 << 31)

#define MALIDP550_DE_IRQ_SATURATION		(1 << 8)
#define MALIDP550_DE_IRQ_VSYNC			(1 << 12)
#define MALIDP550_DE_IRQ_PROG_LINE		(1 << 13)
#define MALIDP550_DE_IRQ_AXI_ERR		(1 << 16)
#define MALIDP550_SE_IRQ_EOW			(1 << 0)
#define MALIDP550_SE_IRQ_AXI_ERR		(1 << 16)
#define MALIDP550_DC_IRQ_CONF_VALID		(1 << 0)
#define MALIDP550_DC_IRQ_CONF_MODE		(1 << 4)
#define MALIDP550_DC_IRQ_CONF_ACTIVE		(1 << 16)
#define MALIDP550_DC_IRQ_DE			(1 << 20)
#define MALIDP550_DC_IRQ_SE			(1 << 24)

#define MALIDP650_DE_IRQ_DRIFT			(1 << 4)

/* bit masks that are common between products */
#define   MALIDP_CFG_VALID		(1 << 0)
#define   MALIDP_DISP_FUNC_GAMMA	(1 << 0)
#define   MALIDP_DISP_FUNC_CADJ		(1 << 4)
#define   MALIDP_DISP_FUNC_ILACED	(1 << 8)

/* register offsets for IRQ management */
#define MALIDP_REG_STATUS		0x00000
#define MALIDP_REG_SETIRQ		0x00004
#define MALIDP_REG_MASKIRQ		0x00008
#define MALIDP_REG_CLEARIRQ		0x0000c

/* register offsets */
#define MALIDP_DE_CORE_ID		0x00018
#define MALIDP_DE_DISPLAY_FUNC		0x00020

/* these offsets are relative to MALIDP5x0_TIMINGS_BASE */
#define MALIDP_DE_H_TIMINGS		0x0
#define MALIDP_DE_V_TIMINGS		0x4
#define MALIDP_DE_SYNC_WIDTH		0x8
#define MALIDP_DE_HV_ACTIVE		0xc

/* Stride register offsets relative to Lx_BASE */
#define MALIDP_DE_LG_STRIDE		0x18
#define MALIDP_DE_LV_STRIDE0		0x18
#define MALIDP550_DE_LS_R1_STRIDE	0x28

/* macros to set values into registers */
#define MALIDP_DE_H_FRONTPORCH(x)	(((x) & 0xfff) << 0)
#define MALIDP_DE_H_BACKPORCH(x)	(((x) & 0x3ff) << 16)
#define MALIDP500_DE_V_FRONTPORCH(x)	(((x) & 0xff) << 0)
#define MALIDP550_DE_V_FRONTPORCH(x)	(((x) & 0xfff) << 0)
#define MALIDP_DE_V_BACKPORCH(x)	(((x) & 0xff) << 16)
#define MALIDP_DE_H_SYNCWIDTH(x)	(((x) & 0x3ff) << 0)
#define MALIDP_DE_V_SYNCWIDTH(x)	(((x) & 0xff) << 16)
#define MALIDP_DE_H_ACTIVE(x)		(((x) & 0x1fff) << 0)
#define MALIDP_DE_V_ACTIVE(x)		(((x) & 0x1fff) << 16)

#define MALIDP_PRODUCT_ID(__core_id) ((u32)(__core_id) >> 16)

/* register offsets relative to MALIDP5x0_COEFFS_BASE */
#define MALIDP_COLOR_ADJ_COEF		0x00000
#define MALIDP_COEF_TABLE_ADDR		0x00030
#define MALIDP_COEF_TABLE_DATA		0x00034

/* Scaling engine registers and masks. */
#define   MALIDP_SE_SCALING_EN			(1 << 0)
#define   MALIDP_SE_ALPHA_EN			(1 << 1)
#define   MALIDP_SE_ENH_MASK			3
#define   MALIDP_SE_ENH(x)			(((x) & MALIDP_SE_ENH_MASK) << 2)
#define   MALIDP_SE_RGBO_IF_EN			(1 << 4)
#define   MALIDP550_SE_CTL_SEL_MASK		7
#define   MALIDP550_SE_CTL_VCSEL(x) \
		(((x) & MALIDP550_SE_CTL_SEL_MASK) << 20)
#define   MALIDP550_SE_CTL_HCSEL(x) \
		(((x) & MALIDP550_SE_CTL_SEL_MASK) << 16)

/* Blocks with offsets from SE_CONTROL register. */
#define MALIDP_SE_LAYER_CONTROL			0x14
#define   MALIDP_SE_L0_IN_SIZE			0x00
#define   MALIDP_SE_L0_OUT_SIZE			0x04
#define   MALIDP_SE_SET_V_SIZE(x)		(((x) & 0x1fff) << 16)
#define   MALIDP_SE_SET_H_SIZE(x)		(((x) & 0x1fff) << 0)
#define MALIDP_SE_SCALING_CONTROL		0x24
#define   MALIDP_SE_H_INIT_PH			0x00
#define   MALIDP_SE_H_DELTA_PH			0x04
#define   MALIDP_SE_V_INIT_PH			0x08
#define   MALIDP_SE_V_DELTA_PH			0x0c
#define   MALIDP_SE_COEFFTAB_ADDR		0x10
#define     MALIDP_SE_COEFFTAB_ADDR_MASK	0x7f
#define     MALIDP_SE_V_COEFFTAB		(1 << 8)
#define     MALIDP_SE_H_COEFFTAB		(1 << 9)
#define     MALIDP_SE_SET_V_COEFFTAB_ADDR(x) \
		(MALIDP_SE_V_COEFFTAB | ((x) & MALIDP_SE_COEFFTAB_ADDR_MASK))
#define     MALIDP_SE_SET_H_COEFFTAB_ADDR(x) \
		(MALIDP_SE_H_COEFFTAB | ((x) & MALIDP_SE_COEFFTAB_ADDR_MASK))
#define   MALIDP_SE_COEFFTAB_DATA		0x14
#define     MALIDP_SE_COEFFTAB_DATA_MASK	0x3fff
#define     MALIDP_SE_SET_COEFFTAB_DATA(x) \
		((x) & MALIDP_SE_COEFFTAB_DATA_MASK)
/* Enhance coeffents reigster offset */
#define MALIDP_SE_IMAGE_ENH			0x3C
/* ENH_LIMITS offset 0x0 */
#define     MALIDP_SE_ENH_LOW_LEVEL		24
#define     MALIDP_SE_ENH_HIGH_LEVEL		63
#define     MALIDP_SE_ENH_LIMIT_MASK		0xfff
#define     MALIDP_SE_SET_ENH_LIMIT_LOW(x) \
		((x) & MALIDP_SE_ENH_LIMIT_MASK)
#define     MALIDP_SE_SET_ENH_LIMIT_HIGH(x) \
		(((x) & MALIDP_SE_ENH_LIMIT_MASK) << 16)
#define   MALIDP_SE_ENH_COEFF0			0x04

/* register offsets and bits specific to DP500 */
#define MALIDP500_ADDR_SPACE_SIZE	0x01000
#define MALIDP500_DC_BASE		0x00000
#define MALIDP500_DC_CONTROL		0x0000c
#define   MALIDP500_DC_CONFIG_REQ	(1 << 17)
#define   MALIDP500_HSYNCPOL		(1 << 20)
#define   MALIDP500_VSYNCPOL		(1 << 21)
#define   MALIDP500_DC_CLEAR_MASK	0x300fff
#define MALIDP500_DE_LINE_COUNTER	0x00010
#define MALIDP500_DE_AXI_CONTROL	0x00014
#define MALIDP500_DE_SECURE_CTRL	0x0001c
#define MALIDP500_DE_CHROMA_KEY		0x00024
#define MALIDP500_TIMINGS_BASE		0x00028

#define MALIDP500_CONFIG_3D		0x00038
#define MALIDP500_BGND_COLOR		0x0003c
#define MALIDP500_OUTPUT_DEPTH		0x00044
#define MALIDP500_YUV_RGB_COEF		0x00048
#define MALIDP500_COLOR_ADJ_COEF	0x00078
#define MALIDP500_COEF_TABLE_ADDR	0x000a8
#define MALIDP500_COEF_TABLE_DATA	0x000ac

/*
 * The YUV2RGB coefficients on the DP500 are not in the video layer's register
 * block. They belong in a separate block above the layer's registers, hence
 * the negative offset.
 */
#define MALIDP500_LV_YUV2RGB		((s16)(-0xB8))
/*
 * To match DP550/650, the start of the coeffs registers is
 * at COLORADJ_COEFF0 instead of at YUV_RGB_COEF1.
 */
#define MALIDP500_COEFFS_BASE		0x00078
#define MALIDP500_DE_LV_BASE		0x00100
#define MALIDP500_DE_LV_PTR_BASE	0x00124
#define MALIDP500_DE_LG1_BASE		0x00200
#define MALIDP500_DE_LG1_PTR_BASE	0x0021c
#define MALIDP500_DE_LG2_BASE		0x00300
#define MALIDP500_DE_LG2_PTR_BASE	0x0031c
#define MALIDP500_SE_BASE		0x00c00
#define MALIDP500_SE_CONTROL		0x00c0c
#define MALIDP500_SE_PTR_BASE		0x00e0c
#define MALIDP500_DC_IRQ_BASE		0x00f00
#define MALIDP500_CONFIG_VALID		0x00f00
#define MALIDP500_CONFIG_ID		0x00fd4

/* register offsets and bits specific to DP550/DP650 */
#define MALIDP550_ADDR_SPACE_SIZE	0x10000
#define MALIDP550_DE_CONTROL		0x00010
#define MALIDP550_DE_LINE_COUNTER	0x00014
#define MALIDP550_DE_AXI_CONTROL	0x00018
#define MALIDP550_DE_QOS		0x0001c
#define MALIDP550_TIMINGS_BASE		0x00030
#define MALIDP550_HSYNCPOL		(1 << 12)
#define MALIDP550_VSYNCPOL		(1 << 28)

#define MALIDP550_DE_DISP_SIDEBAND	0x00040
#define MALIDP550_DE_BGND_COLOR		0x00044
#define MALIDP550_DE_OUTPUT_DEPTH	0x0004c
#define MALIDP550_COEFFS_BASE		0x00050
#define MALIDP550_DE_LV1_BASE		0x00100
#define MALIDP550_DE_LV1_PTR_BASE	0x00124
#define MALIDP550_DE_LV2_BASE		0x00200
#define MALIDP550_DE_LV2_PTR_BASE	0x00224
#define MALIDP550_DE_LG_BASE		0x00300
#define MALIDP550_DE_LG_PTR_BASE	0x0031c
#define MALIDP550_DE_LS_BASE		0x00400
#define MALIDP550_DE_LS_PTR_BASE	0x0042c
#define MALIDP550_DE_PERF_BASE		0x00500
#define MALIDP550_SE_BASE		0x08000
#define MALIDP550_SE_CONTROL		0x08010
#define MALIDP550_DC_BASE		0x0c000
#define MALIDP550_DC_CONTROL		0x0c010
#define   MALIDP550_DC_CONFIG_REQ	(1 << 16)
#define MALIDP550_CONFIG_VALID		0x0c014
#define MALIDP550_CONFIG_ID		0x0ffd4

/*
 * Starting with DP550 the register map blocks has been standardised to the
 * following layout:
 *
 *   Offset            Block registers
 *  0x00000            Display Engine
 *  0x08000            Scaling Engine
 *  0x0c000            Display Core
 *  0x10000            Secure control
 *
 * The old DP500 IP mixes some DC with the DE registers, hence the need
 * for a mapping structure.
 */

#endif /* __MALIDP_REGS_H__ */
