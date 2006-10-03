/*
 * arch/ppc/boot/include/mpsc_defs.h
 *
 * Register definitions for the Marvell Multi-Protocol Serial Controller (MPSC),
 * Serial DMA Controller (SDMA), and Baud Rate Generator (BRG).
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef	_PPC_BOOT_MPSC_DEFS_H__
#define	_PPC_BOOT_MPSC_DEFS_H__

#define	MPSC_NUM_CTLRS		2

/*
 *****************************************************************************
 *
 *	Multi-Protocol Serial Controller Interface Registers
 *
 *****************************************************************************
 */

/* Main Configuratino Register Offsets */
#define	MPSC_MMCRL			0x0000
#define	MPSC_MMCRH			0x0004
#define	MPSC_MPCR			0x0008
#define	MPSC_CHR_1			0x000c
#define	MPSC_CHR_2			0x0010
#define	MPSC_CHR_3			0x0014
#define	MPSC_CHR_4			0x0018
#define	MPSC_CHR_5			0x001c
#define	MPSC_CHR_6			0x0020
#define	MPSC_CHR_7			0x0024
#define	MPSC_CHR_8			0x0028
#define	MPSC_CHR_9			0x002c
#define	MPSC_CHR_10			0x0030
#define	MPSC_CHR_11			0x0034

#define	MPSC_MPCR_CL_5			0
#define	MPSC_MPCR_CL_6			1
#define	MPSC_MPCR_CL_7			2
#define	MPSC_MPCR_CL_8			3
#define	MPSC_MPCR_SBL_1			0
#define	MPSC_MPCR_SBL_2			3

#define	MPSC_CHR_2_TEV			(1<<1)
#define	MPSC_CHR_2_TA			(1<<7)
#define	MPSC_CHR_2_TTCS			(1<<9)
#define	MPSC_CHR_2_REV			(1<<17)
#define	MPSC_CHR_2_RA			(1<<23)
#define	MPSC_CHR_2_CRD			(1<<25)
#define	MPSC_CHR_2_EH			(1<<31)
#define	MPSC_CHR_2_PAR_ODD		0
#define	MPSC_CHR_2_PAR_SPACE		1
#define	MPSC_CHR_2_PAR_EVEN		2
#define	MPSC_CHR_2_PAR_MARK		3

/* MPSC Signal Routing */
#define	MPSC_MRR			0x0000
#define	MPSC_RCRR			0x0004
#define	MPSC_TCRR			0x0008

/*
 *****************************************************************************
 *
 *	Serial DMA Controller Interface Registers
 *
 *****************************************************************************
 */

#define	SDMA_SDC			0x0000
#define	SDMA_SDCM			0x0008
#define	SDMA_RX_DESC			0x0800
#define	SDMA_RX_BUF_PTR			0x0808
#define	SDMA_SCRDP			0x0810
#define	SDMA_TX_DESC			0x0c00
#define	SDMA_SCTDP			0x0c10
#define	SDMA_SFTDP			0x0c14

#define	SDMA_DESC_CMDSTAT_PE		(1<<0)
#define	SDMA_DESC_CMDSTAT_CDL		(1<<1)
#define	SDMA_DESC_CMDSTAT_FR		(1<<3)
#define	SDMA_DESC_CMDSTAT_OR		(1<<6)
#define	SDMA_DESC_CMDSTAT_BR		(1<<9)
#define	SDMA_DESC_CMDSTAT_MI		(1<<10)
#define	SDMA_DESC_CMDSTAT_A		(1<<11)
#define	SDMA_DESC_CMDSTAT_AM		(1<<12)
#define	SDMA_DESC_CMDSTAT_CT		(1<<13)
#define	SDMA_DESC_CMDSTAT_C		(1<<14)
#define	SDMA_DESC_CMDSTAT_ES		(1<<15)
#define	SDMA_DESC_CMDSTAT_L		(1<<16)
#define	SDMA_DESC_CMDSTAT_F		(1<<17)
#define	SDMA_DESC_CMDSTAT_P		(1<<18)
#define	SDMA_DESC_CMDSTAT_EI		(1<<23)
#define	SDMA_DESC_CMDSTAT_O		(1<<31)

#define SDMA_DESC_DFLT			(SDMA_DESC_CMDSTAT_O |	\
					SDMA_DESC_CMDSTAT_EI)

#define	SDMA_SDC_RFT			(1<<0)
#define	SDMA_SDC_SFM			(1<<1)
#define	SDMA_SDC_BLMR			(1<<6)
#define	SDMA_SDC_BLMT			(1<<7)
#define	SDMA_SDC_POVR			(1<<8)
#define	SDMA_SDC_RIFB			(1<<9)

#define	SDMA_SDCM_ERD			(1<<7)
#define	SDMA_SDCM_AR			(1<<15)
#define	SDMA_SDCM_STD			(1<<16)
#define	SDMA_SDCM_TXD			(1<<23)
#define	SDMA_SDCM_AT			(1<<31)

#define	SDMA_0_CAUSE_RXBUF		(1<<0)
#define	SDMA_0_CAUSE_RXERR		(1<<1)
#define	SDMA_0_CAUSE_TXBUF		(1<<2)
#define	SDMA_0_CAUSE_TXEND		(1<<3)
#define	SDMA_1_CAUSE_RXBUF		(1<<8)
#define	SDMA_1_CAUSE_RXERR		(1<<9)
#define	SDMA_1_CAUSE_TXBUF		(1<<10)
#define	SDMA_1_CAUSE_TXEND		(1<<11)

#define	SDMA_CAUSE_RX_MASK	(SDMA_0_CAUSE_RXBUF | SDMA_0_CAUSE_RXERR | \
	SDMA_1_CAUSE_RXBUF | SDMA_1_CAUSE_RXERR)
#define	SDMA_CAUSE_TX_MASK	(SDMA_0_CAUSE_TXBUF | SDMA_0_CAUSE_TXEND | \
	SDMA_1_CAUSE_TXBUF | SDMA_1_CAUSE_TXEND)

/* SDMA Interrupt registers */
#define	SDMA_INTR_CAUSE			0x0000
#define	SDMA_INTR_MASK			0x0080

/*
 *****************************************************************************
 *
 *	Baud Rate Generator Interface Registers
 *
 *****************************************************************************
 */

#define	BRG_BCR				0x0000
#define	BRG_BTR				0x0004

#endif /*_PPC_BOOT_MPSC_DEFS_H__ */
