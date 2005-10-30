/*
 * drivers/net/ibm_emac/ibm_emac.h
 *
 * Register definitions for PowerPC 4xx on-chip ethernet contoller
 *
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * Based on original work by
 *      Matt Porter <mporter@kernel.crashing.org>
 *      Armin Kuster <akuster@mvista.com>
 * 	Copyright 2002-2004 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef __IBM_EMAC_H_
#define __IBM_EMAC_H_

#include <linux/config.h>
#include <linux/types.h>

/* This is a simple check to prevent use of this driver on non-tested SoCs */
#if !defined(CONFIG_405GP) && !defined(CONFIG_405GPR) && !defined(CONFIG_405EP) && \
    !defined(CONFIG_440GP) && !defined(CONFIG_440GX) && !defined(CONFIG_440SP) && \
    !defined(CONFIG_440EP) && !defined(CONFIG_NP405H)
#error	"Unknown SoC. Please, check chip user manual and make sure EMAC defines are OK"
#endif

/* EMAC registers 		Write Access rules */
struct emac_regs {
	u32 mr0;		/* special 	*/
	u32 mr1;		/* Reset 	*/
	u32 tmr0;		/* special 	*/
	u32 tmr1;		/* special 	*/
	u32 rmr;		/* Reset 	*/
	u32 isr;		/* Always 	*/
	u32 iser;		/* Reset 	*/
	u32 iahr;		/* Reset, R, T 	*/
	u32 ialr;		/* Reset, R, T 	*/
	u32 vtpid;		/* Reset, R, T 	*/
	u32 vtci;		/* Reset, R, T 	*/
	u32 ptr;		/* Reset,    T 	*/
	u32 iaht1;		/* Reset, R	*/
	u32 iaht2;		/* Reset, R	*/
	u32 iaht3;		/* Reset, R	*/
	u32 iaht4;		/* Reset, R	*/
	u32 gaht1;		/* Reset, R	*/
	u32 gaht2;		/* Reset, R	*/
	u32 gaht3;		/* Reset, R	*/
	u32 gaht4;		/* Reset, R	*/
	u32 lsah;
	u32 lsal;
	u32 ipgvr;		/* Reset,    T 	*/
	u32 stacr;		/* special 	*/
	u32 trtr;		/* special 	*/
	u32 rwmr;		/* Reset 	*/
	u32 octx;
	u32 ocrx;
	u32 ipcr;
};

#if !defined(CONFIG_IBM_EMAC4)
#define EMAC_ETHTOOL_REGS_VER		0
#define EMAC_ETHTOOL_REGS_SIZE		(sizeof(struct emac_regs) - sizeof(u32))
#else
#define EMAC_ETHTOOL_REGS_VER		1
#define EMAC_ETHTOOL_REGS_SIZE		sizeof(struct emac_regs)
#endif

/* EMACx_MR0 */
#define EMAC_MR0_RXI			0x80000000
#define EMAC_MR0_TXI			0x40000000
#define EMAC_MR0_SRST			0x20000000
#define EMAC_MR0_TXE			0x10000000
#define EMAC_MR0_RXE			0x08000000
#define EMAC_MR0_WKE			0x04000000

/* EMACx_MR1 */
#define EMAC_MR1_FDE			0x80000000
#define EMAC_MR1_ILE			0x40000000
#define EMAC_MR1_VLE			0x20000000
#define EMAC_MR1_EIFC			0x10000000
#define EMAC_MR1_APP			0x08000000
#define EMAC_MR1_IST			0x01000000

#define EMAC_MR1_MF_MASK		0x00c00000
#define EMAC_MR1_MF_10			0x00000000
#define EMAC_MR1_MF_100			0x00400000
#if !defined(CONFIG_IBM_EMAC4)
#define EMAC_MR1_MF_1000		0x00000000
#define EMAC_MR1_MF_1000GPCS		0x00000000
#define EMAC_MR1_MF_IPPA(id)		0x00000000
#else
#define EMAC_MR1_MF_1000		0x00800000
#define EMAC_MR1_MF_1000GPCS		0x00c00000
#define EMAC_MR1_MF_IPPA(id)		(((id) & 0x1f) << 6)
#endif

#define EMAC_TX_FIFO_SIZE		2048

#if !defined(CONFIG_IBM_EMAC4)
#define EMAC_MR1_RFS_4K			0x00300000
#define EMAC_MR1_RFS_16K		0x00000000
#define EMAC_RX_FIFO_SIZE(gige)		4096
#define EMAC_MR1_TFS_2K			0x00080000
#define EMAC_MR1_TR0_MULT		0x00008000
#define EMAC_MR1_JPSM			0x00000000
#define EMAC_MR1_BASE(opb)		(EMAC_MR1_TFS_2K | EMAC_MR1_TR0_MULT)
#else
#define EMAC_MR1_RFS_4K			0x00180000
#define EMAC_MR1_RFS_16K		0x00280000
#define EMAC_RX_FIFO_SIZE(gige)		((gige) ? 16384 : 4096)
#define EMAC_MR1_TFS_2K			0x00020000
#define EMAC_MR1_TR			0x00008000
#define EMAC_MR1_MWSW_001		0x00001000
#define EMAC_MR1_JPSM			0x00000800
#define EMAC_MR1_OBCI_MASK		0x00000038
#define EMAC_MR1_OBCI_50		0x00000000
#define EMAC_MR1_OBCI_66		0x00000008
#define EMAC_MR1_OBCI_83		0x00000010
#define EMAC_MR1_OBCI_100		0x00000018
#define EMAC_MR1_OBCI_100P		0x00000020
#define EMAC_MR1_OBCI(freq)		((freq) <= 50  ? EMAC_MR1_OBCI_50 : \
					 (freq) <= 66  ? EMAC_MR1_OBCI_66 : \
					 (freq) <= 83  ? EMAC_MR1_OBCI_83 : \
					 (freq) <= 100 ? EMAC_MR1_OBCI_100 : EMAC_MR1_OBCI_100P)
#define EMAC_MR1_BASE(opb)		(EMAC_MR1_TFS_2K | EMAC_MR1_TR | \
					 EMAC_MR1_MWSW_001 | EMAC_MR1_OBCI(opb))
#endif

/* EMACx_TMR0 */
#define EMAC_TMR0_GNP			0x80000000
#if !defined(CONFIG_IBM_EMAC4)
#define EMAC_TMR0_DEFAULT		0x00000000	
#else
#define EMAC_TMR0_TFAE_2_32		0x00000001
#define EMAC_TMR0_TFAE_4_64		0x00000002
#define EMAC_TMR0_TFAE_8_128		0x00000003
#define EMAC_TMR0_TFAE_16_256		0x00000004
#define EMAC_TMR0_TFAE_32_512		0x00000005
#define EMAC_TMR0_TFAE_64_1024		0x00000006
#define EMAC_TMR0_TFAE_128_2048		0x00000007
#define EMAC_TMR0_DEFAULT		EMAC_TMR0_TFAE_2_32
#endif
#define EMAC_TMR0_XMIT			(EMAC_TMR0_GNP | EMAC_TMR0_DEFAULT)

/* EMACx_TMR1 */

/* IBM manuals are not very clear here. 
 * This is my interpretation of how things are. --ebs
 */
#if defined(CONFIG_40x)
#define EMAC_FIFO_ENTRY_SIZE		8
#define EMAC_MAL_BURST_SIZE		(16 * 4)
#else
#define EMAC_FIFO_ENTRY_SIZE		16
#define EMAC_MAL_BURST_SIZE		(64 * 4)
#endif

#if !defined(CONFIG_IBM_EMAC4)
#define EMAC_TMR1(l,h)			(((l) << 27) | (((h) & 0xff) << 16))
#else
#define EMAC_TMR1(l,h)			(((l) << 27) | (((h) & 0x3ff) << 14))
#endif

/* EMACx_RMR */
#define EMAC_RMR_SP			0x80000000
#define EMAC_RMR_SFCS			0x40000000
#define EMAC_RMR_RRP			0x20000000
#define EMAC_RMR_RFP			0x10000000
#define EMAC_RMR_ROP			0x08000000
#define EMAC_RMR_RPIR			0x04000000
#define EMAC_RMR_PPP			0x02000000
#define EMAC_RMR_PME			0x01000000
#define EMAC_RMR_PMME			0x00800000
#define EMAC_RMR_IAE			0x00400000
#define EMAC_RMR_MIAE			0x00200000
#define EMAC_RMR_BAE			0x00100000
#define EMAC_RMR_MAE			0x00080000
#if !defined(CONFIG_IBM_EMAC4)
#define EMAC_RMR_BASE			0x00000000
#else
#define EMAC_RMR_RFAF_2_32		0x00000001
#define EMAC_RMR_RFAF_4_64		0x00000002
#define EMAC_RMR_RFAF_8_128		0x00000003
#define EMAC_RMR_RFAF_16_256		0x00000004
#define EMAC_RMR_RFAF_32_512		0x00000005
#define EMAC_RMR_RFAF_64_1024		0x00000006
#define EMAC_RMR_RFAF_128_2048		0x00000007
#define EMAC_RMR_BASE			EMAC_RMR_RFAF_128_2048
#endif

/* EMACx_ISR & EMACx_ISER */
#if !defined(CONFIG_IBM_EMAC4)
#define EMAC_ISR_TXPE			0x00000000
#define EMAC_ISR_RXPE			0x00000000
#define EMAC_ISR_TXUE			0x00000000
#define EMAC_ISR_RXOE			0x00000000
#else
#define EMAC_ISR_TXPE			0x20000000
#define EMAC_ISR_RXPE			0x10000000
#define EMAC_ISR_TXUE			0x08000000
#define EMAC_ISR_RXOE			0x04000000
#endif
#define EMAC_ISR_OVR			0x02000000
#define EMAC_ISR_PP			0x01000000
#define EMAC_ISR_BP			0x00800000
#define EMAC_ISR_RP			0x00400000
#define EMAC_ISR_SE			0x00200000
#define EMAC_ISR_ALE			0x00100000
#define EMAC_ISR_BFCS			0x00080000
#define EMAC_ISR_PTLE			0x00040000
#define EMAC_ISR_ORE			0x00020000
#define EMAC_ISR_IRE			0x00010000
#define EMAC_ISR_SQE			0x00000080
#define EMAC_ISR_TE			0x00000040
#define EMAC_ISR_MOS			0x00000002
#define EMAC_ISR_MOF			0x00000001

/* EMACx_STACR */
#define EMAC_STACR_PHYD_MASK		0xffff
#define EMAC_STACR_PHYD_SHIFT		16
#define EMAC_STACR_OC			0x00008000
#define EMAC_STACR_PHYE			0x00004000
#define EMAC_STACR_STAC_MASK		0x00003000
#define EMAC_STACR_STAC_READ		0x00001000
#define EMAC_STACR_STAC_WRITE		0x00002000
#if !defined(CONFIG_IBM_EMAC4)
#define EMAC_STACR_OPBC_MASK		0x00000C00
#define EMAC_STACR_OPBC_50		0x00000000
#define EMAC_STACR_OPBC_66		0x00000400
#define EMAC_STACR_OPBC_83		0x00000800
#define EMAC_STACR_OPBC_100		0x00000C00
#define EMAC_STACR_OPBC(freq)		((freq) <= 50 ? EMAC_STACR_OPBC_50 : \
					 (freq) <= 66 ? EMAC_STACR_OPBC_66 : \
					 (freq) <= 83 ? EMAC_STACR_OPBC_83 : EMAC_STACR_OPBC_100)
#define EMAC_STACR_BASE(opb)		EMAC_STACR_OPBC(opb)
#else
#define EMAC_STACR_BASE(opb)		0x00000000
#endif
#define EMAC_STACR_PCDA_MASK		0x1f
#define EMAC_STACR_PCDA_SHIFT		5
#define EMAC_STACR_PRA_MASK		0x1f

/* EMACx_TRTR */
#if !defined(CONFIG_IBM_EMAC4)
#define EMAC_TRTR_SHIFT			27
#else
#define EMAC_TRTR_SHIFT			24
#endif
#define EMAC_TRTR(size)			((((size) >> 6) - 1) << EMAC_TRTR_SHIFT)

/* EMACx_RWMR */
#if !defined(CONFIG_IBM_EMAC4)
#define EMAC_RWMR(l,h)			(((l) << 23) | ( ((h) & 0x1ff) << 7))	
#else
#define EMAC_RWMR(l,h)			(((l) << 22) | ( ((h) & 0x3ff) << 6))	
#endif

/* EMAC specific TX descriptor control fields (write access) */
#define EMAC_TX_CTRL_GFCS		0x0200
#define EMAC_TX_CTRL_GP			0x0100
#define EMAC_TX_CTRL_ISA		0x0080
#define EMAC_TX_CTRL_RSA		0x0040
#define EMAC_TX_CTRL_IVT		0x0020
#define EMAC_TX_CTRL_RVT		0x0010
#define EMAC_TX_CTRL_TAH_CSUM		0x000e

/* EMAC specific TX descriptor status fields (read access) */
#define EMAC_TX_ST_BFCS			0x0200
#define EMAC_TX_ST_LCS			0x0080
#define EMAC_TX_ST_ED			0x0040
#define EMAC_TX_ST_EC			0x0020
#define EMAC_TX_ST_LC			0x0010
#define EMAC_TX_ST_MC			0x0008
#define EMAC_TX_ST_SC			0x0004
#define EMAC_TX_ST_UR			0x0002
#define EMAC_TX_ST_SQE			0x0001
#if !defined(CONFIG_IBM_EMAC_TAH)
#define EMAC_IS_BAD_TX(v)		((v) & (EMAC_TX_ST_LCS | EMAC_TX_ST_ED | \
					 EMAC_TX_ST_EC | EMAC_TX_ST_LC | \
					 EMAC_TX_ST_MC | EMAC_TX_ST_UR))
#else
#define EMAC_IS_BAD_TX(v)		((v) & (EMAC_TX_ST_LCS | EMAC_TX_ST_ED | \
					 EMAC_TX_ST_EC | EMAC_TX_ST_LC))
#endif					 

/* EMAC specific RX descriptor status fields (read access) */
#define EMAC_RX_ST_OE			0x0200
#define EMAC_RX_ST_PP			0x0100
#define EMAC_RX_ST_BP			0x0080
#define EMAC_RX_ST_RP			0x0040
#define EMAC_RX_ST_SE			0x0020
#define EMAC_RX_ST_AE			0x0010
#define EMAC_RX_ST_BFCS			0x0008
#define EMAC_RX_ST_PTL			0x0004
#define EMAC_RX_ST_ORE			0x0002
#define EMAC_RX_ST_IRE			0x0001
#define EMAC_RX_TAH_BAD_CSUM		0x0003
#define EMAC_BAD_RX_MASK		(EMAC_RX_ST_OE | EMAC_RX_ST_BP | \
					 EMAC_RX_ST_RP | EMAC_RX_ST_SE | \
					 EMAC_RX_ST_AE | EMAC_RX_ST_BFCS | \
					 EMAC_RX_ST_PTL | EMAC_RX_ST_ORE | \
					 EMAC_RX_ST_IRE )
#endif /* __IBM_EMAC_H_ */
