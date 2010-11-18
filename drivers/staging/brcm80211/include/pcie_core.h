/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_PCIE_CORE_H
#define	_PCIE_CORE_H

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif

/* PCIE Enumeration space offsets */
#define  PCIE_CORE_CONFIG_OFFSET	0x0
#define  PCIE_FUNC0_CONFIG_OFFSET	0x400
#define  PCIE_FUNC1_CONFIG_OFFSET	0x500
#define  PCIE_FUNC2_CONFIG_OFFSET	0x600
#define  PCIE_FUNC3_CONFIG_OFFSET	0x700
#define  PCIE_SPROM_SHADOW_OFFSET	0x800
#define  PCIE_SBCONFIG_OFFSET		0xE00

/* PCIE Bar0 Address Mapping. Each function maps 16KB config space */
#define PCIE_DEV_BAR0_SIZE		0x4000
#define PCIE_BAR0_WINMAPCORE_OFFSET	0x0
#define PCIE_BAR0_EXTSPROM_OFFSET	0x1000
#define PCIE_BAR0_PCIECORE_OFFSET	0x2000
#define PCIE_BAR0_CCCOREREG_OFFSET	0x3000

/* different register spaces to access thr'u pcie indirect access */
#define PCIE_CONFIGREGS 	1	/* Access to config space */
#define PCIE_PCIEREGS 		2	/* Access to pcie registers */

/* SB side: PCIE core and host control registers */
typedef struct sbpcieregs {
	u32 control;		/* host mode only */
	u32 PAD[2];
	u32 biststatus;	/* bist Status: 0x00C */
	u32 gpiosel;		/* PCIE gpio sel: 0x010 */
	u32 gpioouten;	/* PCIE gpio outen: 0x14 */
	u32 PAD[2];
	u32 intstatus;	/* Interrupt status: 0x20 */
	u32 intmask;		/* Interrupt mask: 0x24 */
	u32 sbtopcimailbox;	/* sb to pcie mailbox: 0x028 */
	u32 PAD[53];
	u32 sbtopcie0;	/* sb to pcie translation 0: 0x100 */
	u32 sbtopcie1;	/* sb to pcie translation 1: 0x104 */
	u32 sbtopcie2;	/* sb to pcie translation 2: 0x108 */
	u32 PAD[5];

	/* pcie core supports in direct access to config space */
	u32 configaddr;	/* pcie config space access: Address field: 0x120 */
	u32 configdata;	/* pcie config space access: Data field: 0x124 */

	/* mdio access to serdes */
	u32 mdiocontrol;	/* controls the mdio access: 0x128 */
	u32 mdiodata;	/* Data to the mdio access: 0x12c */

	/* pcie protocol phy/dllp/tlp register indirect access mechanism */
	u32 pcieindaddr;	/* indirect access to the internal register: 0x130 */
	u32 pcieinddata;	/* Data to/from the internal regsiter: 0x134 */

	u32 clkreqenctrl;	/* >= rev 6, Clkreq rdma control : 0x138 */
	u32 PAD[177];
	u32 pciecfg[4][64];	/* 0x400 - 0x7FF, PCIE Cfg Space */
	u16 sprom[64];	/* SPROM shadow Area */
} sbpcieregs_t;

/* PCI control */
#define PCIE_RST_OE	0x01	/* When set, drives PCI_RESET out to pin */
#define PCIE_RST	0x02	/* Value driven out to pin */

#define	PCIE_CFGADDR	0x120	/* offsetof(configaddr) */
#define	PCIE_CFGDATA	0x124	/* offsetof(configdata) */

/* Interrupt status/mask */
#define PCIE_INTA	0x01	/* PCIE INTA message is received */
#define PCIE_INTB	0x02	/* PCIE INTB message is received */
#define PCIE_INTFATAL	0x04	/* PCIE INTFATAL message is received */
#define PCIE_INTNFATAL	0x08	/* PCIE INTNONFATAL message is received */
#define PCIE_INTCORR	0x10	/* PCIE INTCORR message is received */
#define PCIE_INTPME	0x20	/* PCIE INTPME message is received */

/* SB to PCIE translation masks */
#define SBTOPCIE0_MASK	0xfc000000
#define SBTOPCIE1_MASK	0xfc000000
#define SBTOPCIE2_MASK	0xc0000000

/* Access type bits (0:1) */
#define SBTOPCIE_MEM	0
#define SBTOPCIE_IO	1
#define SBTOPCIE_CFG0	2
#define SBTOPCIE_CFG1	3

/* Prefetch enable bit 2 */
#define SBTOPCIE_PF		4

/* Write Burst enable for memory write bit 3 */
#define SBTOPCIE_WR_BURST	8

/* config access */
#define CONFIGADDR_FUNC_MASK	0x7000
#define CONFIGADDR_FUNC_SHF	12
#define CONFIGADDR_REG_MASK	0x0FFF
#define CONFIGADDR_REG_SHF	0

#define PCIE_CONFIG_INDADDR(f, r)	\
	((((f) & CONFIGADDR_FUNC_MASK) << CONFIGADDR_FUNC_SHF) | \
	(((r) & CONFIGADDR_REG_MASK) << CONFIGADDR_REG_SHF))

/* PCIE protocol regs Indirect Address */
#define PCIEADDR_PROT_MASK	0x300
#define PCIEADDR_PROT_SHF	8
#define PCIEADDR_PL_TLP		0
#define PCIEADDR_PL_DLLP	1
#define PCIEADDR_PL_PLP		2

/* PCIE protocol PHY diagnostic registers */
#define	PCIE_PLP_MODEREG		0x200	/* Mode */
#define	PCIE_PLP_STATUSREG		0x204	/* Status */
#define PCIE_PLP_LTSSMCTRLREG		0x208	/* LTSSM control */
#define PCIE_PLP_LTLINKNUMREG		0x20c	/* Link Training Link number */
#define PCIE_PLP_LTLANENUMREG		0x210	/* Link Training Lane number */
#define PCIE_PLP_LTNFTSREG		0x214	/* Link Training N_FTS */
#define PCIE_PLP_ATTNREG		0x218	/* Attention */
#define PCIE_PLP_ATTNMASKREG		0x21C	/* Attention Mask */
#define PCIE_PLP_RXERRCTR		0x220	/* Rx Error */
#define PCIE_PLP_RXFRMERRCTR		0x224	/* Rx Framing Error */
#define PCIE_PLP_RXERRTHRESHREG		0x228	/* Rx Error threshold */
#define PCIE_PLP_TESTCTRLREG		0x22C	/* Test Control reg */
#define PCIE_PLP_SERDESCTRLOVRDREG	0x230	/* SERDES Control Override */
#define PCIE_PLP_TIMINGOVRDREG		0x234	/* Timing param override */
#define PCIE_PLP_RXTXSMDIAGREG		0x238	/* RXTX State Machine Diag */
#define PCIE_PLP_LTSSMDIAGREG		0x23C	/* LTSSM State Machine Diag */

/* PCIE protocol DLLP diagnostic registers */
#define PCIE_DLLP_LCREG			0x100	/* Link Control */
#define PCIE_DLLP_LSREG			0x104	/* Link Status */
#define PCIE_DLLP_LAREG			0x108	/* Link Attention */
#define PCIE_DLLP_LAMASKREG		0x10C	/* Link Attention Mask */
#define PCIE_DLLP_NEXTTXSEQNUMREG	0x110	/* Next Tx Seq Num */
#define PCIE_DLLP_ACKEDTXSEQNUMREG	0x114	/* Acked Tx Seq Num */
#define PCIE_DLLP_PURGEDTXSEQNUMREG	0x118	/* Purged Tx Seq Num */
#define PCIE_DLLP_RXSEQNUMREG		0x11C	/* Rx Sequence Number */
#define PCIE_DLLP_LRREG			0x120	/* Link Replay */
#define PCIE_DLLP_LACKTOREG		0x124	/* Link Ack Timeout */
#define PCIE_DLLP_PMTHRESHREG		0x128	/* Power Management Threshold */
#define PCIE_DLLP_RTRYWPREG		0x12C	/* Retry buffer write ptr */
#define PCIE_DLLP_RTRYRPREG		0x130	/* Retry buffer Read ptr */
#define PCIE_DLLP_RTRYPPREG		0x134	/* Retry buffer Purged ptr */
#define PCIE_DLLP_RTRRWREG		0x138	/* Retry buffer Read/Write */
#define PCIE_DLLP_ECTHRESHREG		0x13C	/* Error Count Threshold */
#define PCIE_DLLP_TLPERRCTRREG		0x140	/* TLP Error Counter */
#define PCIE_DLLP_ERRCTRREG		0x144	/* Error Counter */
#define PCIE_DLLP_NAKRXCTRREG		0x148	/* NAK Received Counter */
#define PCIE_DLLP_TESTREG		0x14C	/* Test */
#define PCIE_DLLP_PKTBIST		0x150	/* Packet BIST */
#define PCIE_DLLP_PCIE11		0x154	/* DLLP PCIE 1.1 reg */

#define PCIE_DLLP_LSREG_LINKUP		(1 << 16)

/* PCIE protocol TLP diagnostic registers */
#define PCIE_TLP_CONFIGREG		0x000	/* Configuration */
#define PCIE_TLP_WORKAROUNDSREG		0x004	/* TLP Workarounds */
#define PCIE_TLP_WRDMAUPPER		0x010	/* Write DMA Upper Address */
#define PCIE_TLP_WRDMALOWER		0x014	/* Write DMA Lower Address */
#define PCIE_TLP_WRDMAREQ_LBEREG	0x018	/* Write DMA Len/ByteEn Req */
#define PCIE_TLP_RDDMAUPPER		0x01C	/* Read DMA Upper Address */
#define PCIE_TLP_RDDMALOWER		0x020	/* Read DMA Lower Address */
#define PCIE_TLP_RDDMALENREG		0x024	/* Read DMA Len Req */
#define PCIE_TLP_MSIDMAUPPER		0x028	/* MSI DMA Upper Address */
#define PCIE_TLP_MSIDMALOWER		0x02C	/* MSI DMA Lower Address */
#define PCIE_TLP_MSIDMALENREG		0x030	/* MSI DMA Len Req */
#define PCIE_TLP_SLVREQLENREG		0x034	/* Slave Request Len */
#define PCIE_TLP_FCINPUTSREQ		0x038	/* Flow Control Inputs */
#define PCIE_TLP_TXSMGRSREQ		0x03C	/* Tx StateMachine and Gated Req */
#define PCIE_TLP_ADRACKCNTARBLEN	0x040	/* Address Ack XferCnt and ARB Len */
#define PCIE_TLP_DMACPLHDR0		0x044	/* DMA Completion Hdr 0 */
#define PCIE_TLP_DMACPLHDR1		0x048	/* DMA Completion Hdr 1 */
#define PCIE_TLP_DMACPLHDR2		0x04C	/* DMA Completion Hdr 2 */
#define PCIE_TLP_DMACPLMISC0		0x050	/* DMA Completion Misc0 */
#define PCIE_TLP_DMACPLMISC1		0x054	/* DMA Completion Misc1 */
#define PCIE_TLP_DMACPLMISC2		0x058	/* DMA Completion Misc2 */
#define PCIE_TLP_SPTCTRLLEN		0x05C	/* Split Controller Req len */
#define PCIE_TLP_SPTCTRLMSIC0		0x060	/* Split Controller Misc 0 */
#define PCIE_TLP_SPTCTRLMSIC1		0x064	/* Split Controller Misc 1 */
#define PCIE_TLP_BUSDEVFUNC		0x068	/* Bus/Device/Func */
#define PCIE_TLP_RESETCTR		0x06C	/* Reset Counter */
#define PCIE_TLP_RTRYBUF		0x070	/* Retry Buffer value */
#define PCIE_TLP_TGTDEBUG1		0x074	/* Target Debug Reg1 */
#define PCIE_TLP_TGTDEBUG2		0x078	/* Target Debug Reg2 */
#define PCIE_TLP_TGTDEBUG3		0x07C	/* Target Debug Reg3 */
#define PCIE_TLP_TGTDEBUG4		0x080	/* Target Debug Reg4 */

/* MDIO control */
#define MDIOCTL_DIVISOR_MASK		0x7f	/* clock to be used on MDIO */
#define MDIOCTL_DIVISOR_VAL		0x2
#define MDIOCTL_PREAM_EN		0x80	/* Enable preamble sequnce */
#define MDIOCTL_ACCESS_DONE		0x100	/* Tranaction complete */

/* MDIO Data */
#define MDIODATA_MASK			0x0000ffff	/* data 2 bytes */
#define MDIODATA_TA			0x00020000	/* Turnaround */
#define MDIODATA_REGADDR_SHF_OLD	18	/* Regaddr shift (rev < 10) */
#define MDIODATA_REGADDR_MASK_OLD	0x003c0000	/* Regaddr Mask (rev < 10) */
#define MDIODATA_DEVADDR_SHF_OLD	22	/* Physmedia devaddr shift (rev < 10) */
#define MDIODATA_DEVADDR_MASK_OLD	0x0fc00000	/* Physmedia devaddr Mask (rev < 10) */
#define MDIODATA_REGADDR_SHF		18	/* Regaddr shift */
#define MDIODATA_REGADDR_MASK		0x007c0000	/* Regaddr Mask */
#define MDIODATA_DEVADDR_SHF		23	/* Physmedia devaddr shift */
#define MDIODATA_DEVADDR_MASK		0x0f800000	/* Physmedia devaddr Mask */
#define MDIODATA_WRITE			0x10000000	/* write Transaction */
#define MDIODATA_READ			0x20000000	/* Read Transaction */
#define MDIODATA_START			0x40000000	/* start of Transaction */

#define MDIODATA_DEV_ADDR		0x0	/* dev address for serdes */
#define	MDIODATA_BLK_ADDR		0x1F	/* blk address for serdes */

/* MDIO devices (SERDES modules)
 *  unlike old pcie cores (rev < 10), rev10 pcie serde organizes registers into a few blocks.
 *  two layers mapping (blockidx, register offset) is required
 */
#define MDIO_DEV_IEEE0		0x000
#define MDIO_DEV_IEEE1		0x001
#define MDIO_DEV_BLK0		0x800
#define MDIO_DEV_BLK1		0x801
#define MDIO_DEV_BLK2		0x802
#define MDIO_DEV_BLK3		0x803
#define MDIO_DEV_BLK4		0x804
#define MDIO_DEV_TXPLL		0x808	/* TXPLL register block idx */
#define MDIO_DEV_TXCTRL0	0x820
#define MDIO_DEV_SERDESID	0x831
#define MDIO_DEV_RXCTRL0	0x840

/* serdes regs (rev < 10) */
#define MDIODATA_DEV_PLL       		0x1d	/* SERDES PLL Dev */
#define MDIODATA_DEV_TX        		0x1e	/* SERDES TX Dev */
#define MDIODATA_DEV_RX        		0x1f	/* SERDES RX Dev */
	/* SERDES RX registers */
#define SERDES_RX_CTRL			1	/* Rx cntrl */
#define SERDES_RX_TIMER1		2	/* Rx Timer1 */
#define SERDES_RX_CDR			6	/* CDR */
#define SERDES_RX_CDRBW			7	/* CDR BW */

	/* SERDES RX control register */
#define SERDES_RX_CTRL_FORCE		0x80	/* rxpolarity_force */
#define SERDES_RX_CTRL_POLARITY		0x40	/* rxpolarity_value */

	/* SERDES PLL registers */
#define SERDES_PLL_CTRL                 1	/* PLL control reg */
#define PLL_CTRL_FREQDET_EN             0x4000	/* bit 14 is FREQDET on */

/* Power management threshold */
#define PCIE_L0THRESHOLDTIME_MASK       0xFF00	/* bits 0 - 7 */
#define PCIE_L1THRESHOLDTIME_MASK       0xFF00	/* bits 8 - 15 */
#define PCIE_L1THRESHOLDTIME_SHIFT      8	/* PCIE_L1THRESHOLDTIME_SHIFT */
#define PCIE_L1THRESHOLD_WARVAL         0x72	/* WAR value */
#define PCIE_ASPMTIMER_EXTEND		0x01000000	/* > rev7: enable extend ASPM timer */

/* SPROM offsets */
#define SRSH_ASPM_OFFSET		4	/* word 4 */
#define SRSH_ASPM_ENB			0x18	/* bit 3, 4 */
#define SRSH_ASPM_L1_ENB		0x10	/* bit 4 */
#define SRSH_ASPM_L0s_ENB		0x8	/* bit 3 */
#define SRSH_PCIE_MISC_CONFIG		5	/* word 5 */
#define SRSH_L23READY_EXIT_NOPERST	0x8000	/* bit 15 */
#define SRSH_CLKREQ_OFFSET_REV5		20	/* word 20 for srom rev <= 5 */
#define SRSH_CLKREQ_OFFSET_REV8		52	/* word 52 for srom rev 8 */
#define SRSH_CLKREQ_ENB			0x0800	/* bit 11 */
#define SRSH_BD_OFFSET                  6	/* word 6 */
#define SRSH_AUTOINIT_OFFSET            18	/* auto initialization enable */

/* Linkcontrol reg offset in PCIE Cap */
#define PCIE_CAP_LINKCTRL_OFFSET	16	/* linkctrl offset in pcie cap */
#define PCIE_CAP_LCREG_ASPML0s		0x01	/* ASPM L0s in linkctrl */
#define PCIE_CAP_LCREG_ASPML1		0x02	/* ASPM L1 in linkctrl */
#define PCIE_CLKREQ_ENAB		0x100	/* CLKREQ Enab in linkctrl */

#define PCIE_ASPM_ENAB			3	/* ASPM L0s & L1 in linkctrl */
#define PCIE_ASPM_L1_ENAB		2	/* ASPM L0s & L1 in linkctrl */
#define PCIE_ASPM_L0s_ENAB		1	/* ASPM L0s & L1 in linkctrl */
#define PCIE_ASPM_DISAB			0	/* ASPM L0s & L1 in linkctrl */

/* Status reg PCIE_PLP_STATUSREG */
#define PCIE_PLP_POLARITYINV_STAT	0x10
#endif				/* _PCIE_CORE_H */
