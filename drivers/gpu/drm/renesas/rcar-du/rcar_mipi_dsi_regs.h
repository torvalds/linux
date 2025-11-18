/* SPDX-License-Identifier: GPL-2.0 */
/*
 * R-Car MIPI DSI Interface Registers Definitions
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 */

#ifndef __RCAR_MIPI_DSI_REGS_H__
#define __RCAR_MIPI_DSI_REGS_H__

#define LINKSR				0x010
#define LINKSR_LPBUSY			(1 << 1)
#define LINKSR_HSBUSY			(1 << 0)

#define TXSETR				0x100
#define TXSETR_LANECNT_MASK		(0x3 << 0)

/*
 * DSI Command Transfer Registers
 */
#define TXCMSETR			0x110
#define TXCMSETR_SPDTYP			(1 << 8)	/* 0:HS 1:LP */
#define TXCMSETR_LPPDACC		(1 << 0)
#define TXCMCR				0x120
#define TXCMCR_BTATYP			(1 << 2)
#define TXCMCR_BTAREQ			(1 << 1)
#define TXCMCR_TXREQ			(1 << 0)
#define TXCMSR				0x130
#define TXCMSR_CLSNERR			(1 << 18)
#define TXCMSR_AXIERR			(1 << 16)
#define TXCMSR_TXREQEND			(1 << 0)
#define TXCMSCR				0x134
#define TXCMSCR_CLSNERR			(1 << 18)
#define TXCMSCR_AXIERR			(1 << 16)
#define TXCMSCR_TXREQEND		(1 << 0)
#define TXCMIER				0x138
#define TXCMIER_CLSNERR			(1 << 18)
#define TXCMIER_AXIERR			(1 << 16)
#define TXCMIER_TXREQEND		(1 << 0)
#define TXCMADDRSET0R			0x140
#define TXCMPHDR			0x150
#define TXCMPHDR_FMT			(1 << 24)	/* 0:SP 1:LP */
#define TXCMPHDR_VC(n)			(((n) & 0x3) << 22)
#define TXCMPHDR_DT(n)			(((n) & 0x3f) << 16)
#define TXCMPHDR_DATA1(n)		(((n) & 0xff) << 8)
#define TXCMPHDR_DATA0(n)		(((n) & 0xff) << 0)
#define TXCMPPD0R			0x160
#define TXCMPPD1R			0x164
#define TXCMPPD2R			0x168
#define TXCMPPD3R			0x16c

#define RXSETR				0x200
#define RXSETR_CRCEN			(((n) & 0xf) << 24)
#define RXSETR_ECCEN			(((n) & 0xf) << 16)
#define RXPSETR				0x210
#define RXPSETR_LPPDACC			(1 << 0)
#define RXPSR				0x220
#define RXPSR_ECCERR1B			(1 << 28)
#define RXPSR_UEXTRGERR			(1 << 25)
#define RXPSR_RESPTOERR			(1 << 24)
#define RXPSR_OVRERR			(1 << 23)
#define RXPSR_AXIERR			(1 << 22)
#define RXPSR_CRCERR			(1 << 21)
#define RXPSR_WCERR			(1 << 20)
#define RXPSR_UEXDTERR			(1 << 19)
#define RXPSR_UEXPKTERR			(1 << 18)
#define RXPSR_ECCERR			(1 << 17)
#define RXPSR_MLFERR			(1 << 16)
#define RXPSR_RCVACK			(1 << 14)
#define RXPSR_RCVEOT			(1 << 10)
#define RXPSR_RCVAKE			(1 << 9)
#define RXPSR_RCVRESP			(1 << 8)
#define RXPSR_BTAREQEND			(1 << 0)
#define RXPSCR				0x224
#define RXPSCR_ECCERR1B			(1 << 28)
#define RXPSCR_UEXTRGERR		(1 << 25)
#define RXPSCR_RESPTOERR		(1 << 24)
#define RXPSCR_OVRERR			(1 << 23)
#define RXPSCR_AXIERR			(1 << 22)
#define RXPSCR_CRCERR			(1 << 21)
#define RXPSCR_WCERR			(1 << 20)
#define RXPSCR_UEXDTERR			(1 << 19)
#define RXPSCR_UEXPKTERR		(1 << 18)
#define RXPSCR_ECCERR			(1 << 17)
#define RXPSCR_MLFERR			(1 << 16)
#define RXPSCR_RCVACK			(1 << 14)
#define RXPSCR_RCVEOT			(1 << 10)
#define RXPSCR_RCVAKE			(1 << 9)
#define RXPSCR_RCVRESP			(1 << 8)
#define RXPSCR_BTAREQEND		(1 << 0)
#define RXPIER				0x228
#define RXPIER_ECCERR1B			(1 << 28)
#define RXPIER_UEXTRGERR		(1 << 25)
#define RXPIER_RESPTOERR		(1 << 24)
#define RXPIER_OVRERR			(1 << 23)
#define RXPIER_AXIERR			(1 << 22)
#define RXPIER_CRCERR			(1 << 21)
#define RXPIER_WCERR			(1 << 20)
#define RXPIER_UEXDTERR			(1 << 19)
#define RXPIER_UEXPKTERR		(1 << 18)
#define RXPIER_ECCERR			(1 << 17)
#define RXPIER_MLFERR			(1 << 16)
#define RXPIER_RCVACK			(1 << 14)
#define RXPIER_RCVEOT			(1 << 10)
#define RXPIER_RCVAKE			(1 << 9)
#define RXPIER_RCVRESP			(1 << 8)
#define RXPIER_BTAREQEND		(1 << 0)
#define RXPADDRSET0R			0x230
#define RXPSIZESETR			0x238
#define RXPSIZESETR_SIZE(n)		(((n) & 0xf) << 3)
#define RXPHDR				0x240
#define RXPHDR_FMT			(1 << 24)	/* 0:SP 1:LP */
#define RXPHDR_VC(n)			(((n) & 0x3) << 22)
#define RXPHDR_DT(n)			(((n) & 0x3f) << 16)
#define RXPHDR_DATA1(n)			(((n) & 0xff) << 8)
#define RXPHDR_DATA0(n)			(((n) & 0xff) << 0)
#define RXPPD0R				0x250
#define RXPPD1R				0x254
#define RXPPD2R				0x258
#define RXPPD3R				0x25c
#define AKEPR				0x300
#define AKEPR_VC(n)			(((n) & 0x3) << 22)
#define AKEPR_DT(n)			(((n) & 0x3f) << 16)
#define AKEPR_ERRRPT(n)			(((n) & 0xffff) << 0)
#define RXRESPTOSETR			0x400
#define TACR				0x500
#define TASR				0x510
#define TASCR				0x514
#define TAIER				0x518
#define TOSR				0x610
#define TOSR_TATO			(1 << 2)
#define TOSR_LRXHTO			(1 << 1)
#define TOSR_HRXTO			(1 << 0)
#define TOSCR				0x614
#define TOSCR_TATO			(1 << 2)
#define TOSCR_LRXHTO			(1 << 1)
#define TOSCR_HRXTO			(1 << 0)

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
#define PPISETR_DLEN_MASK		(0xf << 0)
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

#define PPIDL0SR			0x740
#define PPIDL0SR_DIR			(1 << 10)
#define PPIDL0SR_STPST			(1 << 6)

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
#define VCLKSET_DIV_V3U(x)		(((x) & 0x3) << 4)
#define VCLKSET_DIV_V4H(x)		(((x) & 0x7) << 4)
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

#define PHTR				0x1038
#define PHTR_TEST			(1 << 16)

#define PHTC				0x103c
#define PHTC_TESTCLR			(1 << 0)

#endif /* __RCAR_MIPI_DSI_REGS_H__ */
