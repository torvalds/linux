/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rcar_mipi_dsi_regs.h  --  R-Car MIPI DSI Interface Registers Definitions
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 */

#ifndef __RCAR_MIPI_DSI_REGS_H__
#define __RCAR_MIPI_DSI_REGS_H__

#define LINKSR				0x010
#define LINKSR_LPBUSY			(1 << 1)
#define LINKSR_HSBUSY			(1 << 0)

/*
 * Video Mode Register
 */
#define TXVMSETR			0x180
#define TXVMSETR_SYNSEQ_PULSES		(0 << 16)
#define TXVMSETR_SYNSEQ_EVENTS		(1 << 16)
#define TXVMSETR_VSTPM			(1 << 15)
#define TXVMSETR_PIXWDTH		(1 << 8)
#define TXVMSETR_VSEN_EN		(1 << 4)
#define TXVMSETR_VSEN_DIS		(0 << 4)
#define TXVMSETR_HFPBPEN_EN		(1 << 2)
#define TXVMSETR_HFPBPEN_DIS		(0 << 2)
#define TXVMSETR_HBPBPEN_EN		(1 << 1)
#define TXVMSETR_HBPBPEN_DIS		(0 << 1)
#define TXVMSETR_HSABPEN_EN		(1 << 0)
#define TXVMSETR_HSABPEN_DIS		(0 << 0)

#define TXVMCR				0x190
#define TXVMCR_VFCLR			(1 << 12)
#define TXVMCR_EN_VIDEO			(1 << 0)

#define TXVMSR				0x1a0
#define TXVMSR_STR			(1 << 16)
#define TXVMSR_VFRDY			(1 << 12)
#define TXVMSR_ACT			(1 << 8)
#define TXVMSR_RDY			(1 << 0)

#define TXVMSCR				0x1a4
#define TXVMSCR_STR			(1 << 16)

#define TXVMPSPHSETR			0x1c0
#define TXVMPSPHSETR_DT_RGB16		(0x0e << 16)
#define TXVMPSPHSETR_DT_RGB18		(0x1e << 16)
#define TXVMPSPHSETR_DT_RGB18_LS	(0x2e << 16)
#define TXVMPSPHSETR_DT_RGB24		(0x3e << 16)
#define TXVMPSPHSETR_DT_YCBCR16		(0x2c << 16)

#define TXVMVPRMSET0R			0x1d0
#define TXVMVPRMSET0R_HSPOL_HIG		(0 << 17)
#define TXVMVPRMSET0R_HSPOL_LOW		(1 << 17)
#define TXVMVPRMSET0R_VSPOL_HIG		(0 << 16)
#define TXVMVPRMSET0R_VSPOL_LOW		(1 << 16)
#define TXVMVPRMSET0R_CSPC_RGB		(0 << 4)
#define TXVMVPRMSET0R_CSPC_YCbCr	(1 << 4)
#define TXVMVPRMSET0R_BPP_16		(0 << 0)
#define TXVMVPRMSET0R_BPP_18		(1 << 0)
#define TXVMVPRMSET0R_BPP_24		(2 << 0)

#define TXVMVPRMSET1R			0x1d4
#define TXVMVPRMSET1R_VACTIVE(x)	(((x) & 0x7fff) << 16)
#define TXVMVPRMSET1R_VSA(x)		(((x) & 0xfff) << 0)

#define TXVMVPRMSET2R			0x1d8
#define TXVMVPRMSET2R_VFP(x)		(((x) & 0x1fff) << 16)
#define TXVMVPRMSET2R_VBP(x)		(((x) & 0x1fff) << 0)

#define TXVMVPRMSET3R			0x1dc
#define TXVMVPRMSET3R_HACTIVE(x)	(((x) & 0x7fff) << 16)
#define TXVMVPRMSET3R_HSA(x)		(((x) & 0xfff) << 0)

#define TXVMVPRMSET4R			0x1e0
#define TXVMVPRMSET4R_HFP(x)		(((x) & 0x1fff) << 16)
#define TXVMVPRMSET4R_HBP(x)		(((x) & 0x1fff) << 0)

/*
 * PHY-Protocol Interface (PPI) Registers
 */
#define PPISETR				0x700
#define PPISETR_DLEN_0			(0x1 << 0)
#define PPISETR_DLEN_1			(0x3 << 0)
#define PPISETR_DLEN_2			(0x7 << 0)
#define PPISETR_DLEN_3			(0xf << 0)
#define PPISETR_CLEN			(1 << 8)

#define PPICLCR				0x710
#define PPICLCR_TXREQHS			(1 << 8)
#define PPICLCR_TXULPSEXT		(1 << 1)
#define PPICLCR_TXULPSCLK		(1 << 0)

#define PPICLSR				0x720
#define PPICLSR_HSTOLP			(1 << 27)
#define PPICLSR_TOHS			(1 << 26)
#define PPICLSR_STPST			(1 << 0)

#define PPICLSCR			0x724
#define PPICLSCR_HSTOLP			(1 << 27)
#define PPICLSCR_TOHS			(1 << 26)

#define PPIDLSR				0x760
#define PPIDLSR_STPST			(0xf << 0)

/*
 * Clocks registers
 */
#define LPCLKSET			0x1000
#define LPCLKSET_CKEN			(1 << 8)
#define LPCLKSET_LPCLKDIV(x)		(((x) & 0x3f) << 0)

#define CFGCLKSET			0x1004
#define CFGCLKSET_CKEN			(1 << 8)
#define CFGCLKSET_CFGCLKDIV(x)		(((x) & 0x3f) << 0)

#define DOTCLKDIV			0x1008
#define DOTCLKDIV_CKEN			(1 << 8)
#define DOTCLKDIV_DOTCLKDIV(x)		(((x) & 0x3f) << 0)

#define VCLKSET				0x100c
#define VCLKSET_CKEN			(1 << 16)
#define VCLKSET_COLOR_RGB		(0 << 8)
#define VCLKSET_COLOR_YCC		(1 << 8)
#define VCLKSET_DIV(x)			(((x) & 0x3) << 4)
#define VCLKSET_BPP_16			(0 << 2)
#define VCLKSET_BPP_18			(1 << 2)
#define VCLKSET_BPP_18L			(2 << 2)
#define VCLKSET_BPP_24			(3 << 2)
#define VCLKSET_LANE(x)			(((x) & 0x3) << 0)

#define VCLKEN				0x1010
#define VCLKEN_CKEN			(1 << 0)

#define PHYSETUP			0x1014
#define PHYSETUP_HSFREQRANGE(x)		(((x) & 0x7f) << 16)
#define PHYSETUP_HSFREQRANGE_MASK	(0x7f << 16)
#define PHYSETUP_CFGCLKFREQRANGE(x)	(((x) & 0x3f) << 8)
#define PHYSETUP_SHUTDOWNZ		(1 << 1)
#define PHYSETUP_RSTZ			(1 << 0)

#define CLOCKSET1			0x101c
#define CLOCKSET1_LOCK_PHY		(1 << 17)
#define CLOCKSET1_LOCK			(1 << 16)
#define CLOCKSET1_CLKSEL		(1 << 8)
#define CLOCKSET1_CLKINSEL_EXTAL	(0 << 2)
#define CLOCKSET1_CLKINSEL_DIG		(1 << 2)
#define CLOCKSET1_CLKINSEL_DU		(1 << 3)
#define CLOCKSET1_SHADOW_CLEAR		(1 << 1)
#define CLOCKSET1_UPDATEPLL		(1 << 0)

#define CLOCKSET2			0x1020
#define CLOCKSET2_M(x)			(((x) & 0xfff) << 16)
#define CLOCKSET2_VCO_CNTRL(x)		(((x) & 0x3f) << 8)
#define CLOCKSET2_N(x)			(((x) & 0xf) << 0)

#define CLOCKSET3			0x1024
#define CLOCKSET3_PROP_CNTRL(x)		(((x) & 0x3f) << 24)
#define CLOCKSET3_INT_CNTRL(x)		(((x) & 0x3f) << 16)
#define CLOCKSET3_CPBIAS_CNTRL(x)	(((x) & 0x7f) << 8)
#define CLOCKSET3_GMP_CNTRL(x)		(((x) & 0x3) << 0)

#define PHTW				0x1034
#define PHTW_DWEN			(1 << 24)
#define PHTW_TESTDIN_DATA(x)		(((x) & 0xff) << 16)
#define PHTW_CWEN			(1 << 8)
#define PHTW_TESTDIN_CODE(x)		(((x) & 0xff) << 0)

#define PHTC				0x103c
#define PHTC_TESTCLR			(1 << 0)

#endif /* __RCAR_MIPI_DSI_REGS_H__ */
