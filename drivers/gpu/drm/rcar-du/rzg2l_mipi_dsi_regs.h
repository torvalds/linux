/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RZ/G2L MIPI DSI Interface Registers Definitions
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#ifndef __RZG2L_MIPI_DSI_REGS_H__
#define __RZG2L_MIPI_DSI_REGS_H__

#include <linux/bits.h>

/* DPHY Registers */
#define DSIDPHYCTRL0			0x00
#define DSIDPHYCTRL0_CAL_EN_HSRX_OFS	BIT(16)
#define DSIDPHYCTRL0_CMN_MASTER_EN	BIT(8)
#define DSIDPHYCTRL0_RE_VDD_DETVCCQLV18	BIT(2)
#define DSIDPHYCTRL0_EN_LDO1200		BIT(1)
#define DSIDPHYCTRL0_EN_BGR		BIT(0)

#define DSIDPHYTIM0			0x04
#define DSIDPHYTIM0_TCLK_MISS(x)	((x) << 24)
#define DSIDPHYTIM0_T_INIT(x)		((x) << 0)

#define DSIDPHYTIM1			0x08
#define DSIDPHYTIM1_THS_PREPARE(x)	((x) << 24)
#define DSIDPHYTIM1_TCLK_PREPARE(x)	((x) << 16)
#define DSIDPHYTIM1_THS_SETTLE(x)	((x) << 8)
#define DSIDPHYTIM1_TCLK_SETTLE(x)	((x) << 0)

#define DSIDPHYTIM2			0x0c
#define DSIDPHYTIM2_TCLK_TRAIL(x)	((x) << 24)
#define DSIDPHYTIM2_TCLK_POST(x)	((x) << 16)
#define DSIDPHYTIM2_TCLK_PRE(x)		((x) << 8)
#define DSIDPHYTIM2_TCLK_ZERO(x)	((x) << 0)

#define DSIDPHYTIM3			0x10
#define DSIDPHYTIM3_TLPX(x)		((x) << 24)
#define DSIDPHYTIM3_THS_EXIT(x)		((x) << 16)
#define DSIDPHYTIM3_THS_TRAIL(x)	((x) << 8)
#define DSIDPHYTIM3_THS_ZERO(x)		((x) << 0)

/* --------------------------------------------------------*/
/* Link Registers */
#define LINK_REG_OFFSET			0x10000

/* Link Status Register */
#define LINKSR				0x10
#define LINKSR_LPBUSY			BIT(13)
#define LINKSR_HSBUSY			BIT(12)
#define LINKSR_VICHRUN1			BIT(8)
#define LINKSR_SQCHRUN1			BIT(4)
#define LINKSR_SQCHRUN0			BIT(0)

/* Tx Set Register */
#define TXSETR				0x100
#define TXSETR_NUMLANECAP		(0x3 << 16)
#define TXSETR_DLEN			(1 << 9)
#define TXSETR_CLEN			(1 << 8)
#define TXSETR_NUMLANEUSE(x)		(((x) & 0x3) << 0)

/* HS Clock Set Register */
#define HSCLKSETR			0x104
#define HSCLKSETR_HSCLKMODE_CONT	(1 << 1)
#define HSCLKSETR_HSCLKMODE_NON_CONT	(0 << 1)
#define HSCLKSETR_HSCLKRUN_HS		(1 << 0)
#define HSCLKSETR_HSCLKRUN_LP		(0 << 0)

/* Reset Control Register */
#define RSTCR				0x110
#define RSTCR_SWRST			BIT(0)
#define RSTCR_FCETXSTP			BIT(16)

/* Reset Status Register */
#define RSTSR				0x114
#define RSTSR_DL0DIR			(1 << 15)
#define RSTSR_DLSTPST			(0xf << 8)
#define RSTSR_SWRSTV1			(1 << 4)
#define RSTSR_SWRSTIB			(1 << 3)
#define RSTSR_SWRSTAPB			(1 << 2)
#define RSTSR_SWRSTLP			(1 << 1)
#define RSTSR_SWRSTHS			(1 << 0)

/* Clock Lane Stop Time Set Register */
#define CLSTPTSETR			0x314
#define CLSTPTSETR_CLKKPT(x)		((x) << 24)
#define CLSTPTSETR_CLKBFHT(x)		((x) << 16)
#define CLSTPTSETR_CLKSTPT(x)		((x) << 2)

/* LP Transition Time Set Register */
#define LPTRNSTSETR			0x318
#define LPTRNSTSETR_GOLPBKT(x)		((x) << 0)

/* Physical Lane Status Register */
#define PLSR				0x320
#define PLSR_CLHS2LP			BIT(27)
#define PLSR_CLLP2HS			BIT(26)

/* Video-Input Channel 1 Set 0 Register */
#define VICH1SET0R			0x400
#define VICH1SET0R_VSEN			BIT(12)
#define VICH1SET0R_HFPNOLP		BIT(10)
#define VICH1SET0R_HBPNOLP		BIT(9)
#define VICH1SET0R_HSANOLP		BIT(8)
#define VICH1SET0R_VSTPAFT		BIT(1)
#define VICH1SET0R_VSTART		BIT(0)

/* Video-Input Channel 1 Set 1 Register */
#define VICH1SET1R			0x404
#define VICH1SET1R_DLY(x)		(((x) & 0xfff) << 2)

/* Video-Input Channel 1 Status Register */
#define VICH1SR				0x410
#define VICH1SR_VIRDY			BIT(3)
#define VICH1SR_RUNNING			BIT(2)
#define VICH1SR_STOP			BIT(1)
#define VICH1SR_START			BIT(0)

/* Video-Input Channel 1 Pixel Packet Set Register */
#define VICH1PPSETR			0x420
#define VICH1PPSETR_DT_RGB18		(0x1e << 16)
#define VICH1PPSETR_DT_RGB18_LS		(0x2e << 16)
#define VICH1PPSETR_DT_RGB24		(0x3e << 16)
#define VICH1PPSETR_TXESYNC_PULSE	(1 << 15)
#define VICH1PPSETR_VC(x)		((x) << 22)

/* Video-Input Channel 1 Vertical Size Set Register */
#define VICH1VSSETR			0x428
#define VICH1VSSETR_VACTIVE(x)		(((x) & 0x7fff) << 16)
#define VICH1VSSETR_VSPOL_LOW		(1 << 15)
#define VICH1VSSETR_VSPOL_HIGH		(0 << 15)
#define VICH1VSSETR_VSA(x)		(((x) & 0xfff) << 0)

/* Video-Input Channel 1 Vertical Porch Set Register */
#define VICH1VPSETR			0x42c
#define VICH1VPSETR_VFP(x)		(((x) & 0x1fff) << 16)
#define VICH1VPSETR_VBP(x)		(((x) & 0x1fff) << 0)

/* Video-Input Channel 1 Horizontal Size Set Register */
#define VICH1HSSETR			0x430
#define VICH1HSSETR_HACTIVE(x)		(((x) & 0x7fff) << 16)
#define VICH1HSSETR_HSPOL_LOW		(1 << 15)
#define VICH1HSSETR_HSPOL_HIGH		(0 << 15)
#define VICH1HSSETR_HSA(x)		(((x) & 0xfff) << 0)

/* Video-Input Channel 1 Horizontal Porch Set Register */
#define VICH1HPSETR			0x434
#define VICH1HPSETR_HFP(x)		(((x) & 0x1fff) << 16)
#define VICH1HPSETR_HBP(x)		(((x) & 0x1fff) << 0)

#endif /* __RZG2L_MIPI_DSI_REGS_H__ */
