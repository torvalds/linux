/*
 * ibm_emac.h
 *
 *
 *      Armin Kuster akuster@mvista.com
 *      June, 2002
 *
 * Copyright 2002 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _IBM_EMAC_H_
#define _IBM_EMAC_H_
/* General defines needed for the driver */

/* Emac */
typedef struct emac_regs {
	u32 em0mr0;
	u32 em0mr1;
	u32 em0tmr0;
	u32 em0tmr1;
	u32 em0rmr;
	u32 em0isr;
	u32 em0iser;
	u32 em0iahr;
	u32 em0ialr;
	u32 em0vtpid;
	u32 em0vtci;
	u32 em0ptr;
	u32 em0iaht1;
	u32 em0iaht2;
	u32 em0iaht3;
	u32 em0iaht4;
	u32 em0gaht1;
	u32 em0gaht2;
	u32 em0gaht3;
	u32 em0gaht4;
	u32 em0lsah;
	u32 em0lsal;
	u32 em0ipgvr;
	u32 em0stacr;
	u32 em0trtr;
	u32 em0rwmr;
} emac_t;

/* MODE REG 0 */
#define EMAC_M0_RXI			0x80000000
#define EMAC_M0_TXI			0x40000000
#define EMAC_M0_SRST			0x20000000
#define EMAC_M0_TXE			0x10000000
#define EMAC_M0_RXE			0x08000000
#define EMAC_M0_WKE			0x04000000

/* MODE Reg 1 */
#define EMAC_M1_FDE			0x80000000
#define EMAC_M1_ILE			0x40000000
#define EMAC_M1_VLE			0x20000000
#define EMAC_M1_EIFC			0x10000000
#define EMAC_M1_APP			0x08000000
#define EMAC_M1_AEMI			0x02000000
#define EMAC_M1_IST			0x01000000
#define EMAC_M1_MF_1000GPCS		0x00c00000	/* Internal GPCS */
#define EMAC_M1_MF_1000MBPS		0x00800000	/* External GPCS */
#define EMAC_M1_MF_100MBPS		0x00400000
#define EMAC_M1_RFS_16K                 0x00280000	/* 000 for 512 byte */
#define EMAC_M1_TR			0x00008000
#ifdef CONFIG_IBM_EMAC4
#define EMAC_M1_RFS_8K                  0x00200000
#define EMAC_M1_RFS_4K                  0x00180000
#define EMAC_M1_RFS_2K                  0x00100000
#define EMAC_M1_RFS_1K                  0x00080000
#define EMAC_M1_TX_FIFO_16K             0x00050000	/* 0's for 512 byte */
#define EMAC_M1_TX_FIFO_8K              0x00040000
#define EMAC_M1_TX_FIFO_4K              0x00030000
#define EMAC_M1_TX_FIFO_2K              0x00020000
#define EMAC_M1_TX_FIFO_1K              0x00010000
#define EMAC_M1_TX_TR                   0x00008000
#define EMAC_M1_TX_MWSW                 0x00001000	/* 0 wait for status */
#define EMAC_M1_JUMBO_ENABLE            0x00000800	/* Upt to 9Kr status */
#define EMAC_M1_OPB_CLK_66              0x00000008	/* 66Mhz */
#define EMAC_M1_OPB_CLK_83              0x00000010	/* 83Mhz */
#define EMAC_M1_OPB_CLK_100             0x00000018	/* 100Mhz */
#define EMAC_M1_OPB_CLK_100P            0x00000020	/* 100Mhz+ */
#else				/* CONFIG_IBM_EMAC4 */
#define EMAC_M1_RFS_4K			0x00300000	/* ~4k for 512 byte */
#define EMAC_M1_RFS_2K			0x00200000
#define EMAC_M1_RFS_1K			0x00100000
#define EMAC_M1_TX_FIFO_2K		0x00080000	/* 0's for 512 byte */
#define EMAC_M1_TX_FIFO_1K		0x00040000
#define EMAC_M1_TR0_DEPEND		0x00010000	/* 0'x for single packet */
#define EMAC_M1_TR1_DEPEND		0x00004000
#define EMAC_M1_TR1_MULTI		0x00002000
#define EMAC_M1_JUMBO_ENABLE		0x00001000
#endif				/* CONFIG_IBM_EMAC4 */
#define EMAC_M1_BASE			(EMAC_M1_TX_FIFO_2K | \
					EMAC_M1_APP | \
					EMAC_M1_TR | EMAC_M1_VLE)

/* Transmit Mode Register 0 */
#define EMAC_TMR0_GNP0			0x80000000
#define EMAC_TMR0_GNP1			0x40000000
#define EMAC_TMR0_GNPD			0x20000000
#define EMAC_TMR0_FC			0x10000000
#define EMAC_TMR0_TFAE_2_32		0x00000001
#define EMAC_TMR0_TFAE_4_64		0x00000002
#define EMAC_TMR0_TFAE_8_128		0x00000003
#define EMAC_TMR0_TFAE_16_256		0x00000004
#define EMAC_TMR0_TFAE_32_512		0x00000005
#define EMAC_TMR0_TFAE_64_1024		0x00000006
#define EMAC_TMR0_TFAE_128_2048		0x00000007

/* Receive Mode Register */
#define EMAC_RMR_SP			0x80000000
#define EMAC_RMR_SFCS			0x40000000
#define EMAC_RMR_ARRP			0x20000000
#define EMAC_RMR_ARP			0x10000000
#define EMAC_RMR_AROP			0x08000000
#define EMAC_RMR_ARPI			0x04000000
#define EMAC_RMR_PPP			0x02000000
#define EMAC_RMR_PME			0x01000000
#define EMAC_RMR_PMME			0x00800000
#define EMAC_RMR_IAE			0x00400000
#define EMAC_RMR_MIAE			0x00200000
#define EMAC_RMR_BAE			0x00100000
#define EMAC_RMR_MAE			0x00080000
#define EMAC_RMR_RFAF_2_32		0x00000001
#define EMAC_RMR_RFAF_4_64		0x00000002
#define EMAC_RMR_RFAF_8_128		0x00000003
#define EMAC_RMR_RFAF_16_256		0x00000004
#define EMAC_RMR_RFAF_32_512		0x00000005
#define EMAC_RMR_RFAF_64_1024		0x00000006
#define EMAC_RMR_RFAF_128_2048		0x00000007
#define EMAC_RMR_BASE			(EMAC_RMR_IAE | EMAC_RMR_BAE)

/* Interrupt Status & enable Regs */
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
#define EMAC_ISR_DBDM			0x00000200
#define EMAC_ISR_DB0			0x00000100
#define EMAC_ISR_SE0			0x00000080
#define EMAC_ISR_TE0			0x00000040
#define EMAC_ISR_DB1			0x00000020
#define EMAC_ISR_SE1			0x00000010
#define EMAC_ISR_TE1			0x00000008
#define EMAC_ISR_MOS			0x00000002
#define EMAC_ISR_MOF			0x00000001

/* STA CONTROL REG */
#define EMAC_STACR_OC			0x00008000
#define EMAC_STACR_PHYE			0x00004000
#define EMAC_STACR_WRITE		0x00002000
#define EMAC_STACR_READ			0x00001000
#define EMAC_STACR_CLK_83MHZ		0x00000800	/* 0's for 50Mhz */
#define EMAC_STACR_CLK_66MHZ		0x00000400
#define EMAC_STACR_CLK_100MHZ		0x00000C00

/* Transmit Request Threshold Register */
#define EMAC_TRTR_1600			0x18000000	/* 0's for 64 Bytes */
#define EMAC_TRTR_1024			0x0f000000
#define EMAC_TRTR_512			0x07000000
#define EMAC_TRTR_256			0x03000000
#define EMAC_TRTR_192			0x10000000
#define EMAC_TRTR_128			0x01000000

#define EMAC_TX_CTRL_GFCS		0x0200
#define EMAC_TX_CTRL_GP			0x0100
#define EMAC_TX_CTRL_ISA		0x0080
#define EMAC_TX_CTRL_RSA		0x0040
#define EMAC_TX_CTRL_IVT		0x0020
#define EMAC_TX_CTRL_RVT		0x0010
#define EMAC_TX_CTRL_TAH_CSUM		0x000e	/* TAH only */
#define EMAC_TX_CTRL_TAH_SEG4		0x000a	/* TAH only */
#define EMAC_TX_CTRL_TAH_SEG3		0x0008	/* TAH only */
#define EMAC_TX_CTRL_TAH_SEG2		0x0006	/* TAH only */
#define EMAC_TX_CTRL_TAH_SEG1		0x0004	/* TAH only */
#define EMAC_TX_CTRL_TAH_SEG0		0x0002	/* TAH only */
#define EMAC_TX_CTRL_TAH_DIS		0x0000	/* TAH only */

#define EMAC_TX_CTRL_DFLT ( \
	MAL_TX_CTRL_INTR | EMAC_TX_CTRL_GFCS | EMAC_TX_CTRL_GP )

/* madmal transmit status / Control bits */
#define EMAC_TX_ST_BFCS			0x0200
#define EMAC_TX_ST_BPP			0x0100
#define EMAC_TX_ST_LCS			0x0080
#define EMAC_TX_ST_ED			0x0040
#define EMAC_TX_ST_EC			0x0020
#define EMAC_TX_ST_LC			0x0010
#define EMAC_TX_ST_MC			0x0008
#define EMAC_TX_ST_SC			0x0004
#define EMAC_TX_ST_UR			0x0002
#define EMAC_TX_ST_SQE			0x0001

/* madmal receive status / Control bits */
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
#define EMAC_BAD_RX_PACKET		0x02ff
#define EMAC_CSUM_VER_ERROR		0x0003

/* identify a bad rx packet dependent on emac features */
#ifdef CONFIG_IBM_EMAC4
#define EMAC_IS_BAD_RX_PACKET(desc) \
	(((desc & (EMAC_BAD_RX_PACKET & ~EMAC_CSUM_VER_ERROR)) || \
	((desc & EMAC_CSUM_VER_ERROR) == EMAC_RX_ST_ORE) || \
	((desc & EMAC_CSUM_VER_ERROR) == EMAC_RX_ST_IRE)))
#else
#define EMAC_IS_BAD_RX_PACKET(desc) \
	 (desc & EMAC_BAD_RX_PACKET)
#endif

/* SoC implementation specific EMAC register defaults */
#if defined(CONFIG_440GP)
#define EMAC_RWMR_DEFAULT		0x80009000
#define EMAC_TMR0_DEFAULT		0x00000000
#define EMAC_TMR1_DEFAULT		0xf8640000
#elif defined(CONFIG_440GX)
#define EMAC_RWMR_DEFAULT		0x1000a200
#define EMAC_TMR0_DEFAULT		EMAC_TMR0_TFAE_2_32
#define EMAC_TMR1_DEFAULT		0xa00f0000
#elif defined(CONFIG_440SP)
#define EMAC_RWMR_DEFAULT		0x08002000
#define EMAC_TMR0_DEFAULT		EMAC_TMR0_TFAE_128_2048
#define EMAC_TMR1_DEFAULT		0xf8200000
#else
#define EMAC_RWMR_DEFAULT		0x0f002000
#define EMAC_TMR0_DEFAULT		0x00000000
#define EMAC_TMR1_DEFAULT		0x380f0000
#endif				/* CONFIG_440GP */

/* Revision specific EMAC register defaults */
#ifdef CONFIG_IBM_EMAC4
#define EMAC_M1_DEFAULT			(EMAC_M1_BASE | \
					EMAC_M1_OPB_CLK_83 | \
					EMAC_M1_TX_MWSW)
#define EMAC_RMR_DEFAULT		(EMAC_RMR_BASE | \
					EMAC_RMR_RFAF_128_2048)
#define EMAC_TMR0_XMIT			(EMAC_TMR0_GNP0 | \
					EMAC_TMR0_DEFAULT)
#define EMAC_TRTR_DEFAULT		EMAC_TRTR_1024
#else				/* !CONFIG_IBM_EMAC4 */
#define EMAC_M1_DEFAULT			EMAC_M1_BASE
#define EMAC_RMR_DEFAULT		EMAC_RMR_BASE
#define EMAC_TMR0_XMIT			EMAC_TMR0_GNP0
#define EMAC_TRTR_DEFAULT		EMAC_TRTR_1600
#endif				/* CONFIG_IBM_EMAC4 */

#endif
