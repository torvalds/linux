/*
	mvsas.c - Marvell 88SE6440 SAS/SATA support

	Copyright 2007 Red Hat, Inc.
	Copyright 2008 Marvell. <kewei@marvell.com>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation; either version 2,
	or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty
	of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with this program; see the file COPYING.	If not,
	write to the Free Software Foundation, 675 Mass Ave, Cambridge,
	MA 02139, USA.

	---------------------------------------------------------------

	Random notes:
	* hardware supports controlling the endian-ness of data
	  structures.  this permits elimination of all the le32_to_cpu()
	  and cpu_to_le32() conversions.

 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <scsi/libsas.h>
#include <scsi/scsi_tcq.h>
#include <scsi/sas_ata.h>
#include <asm/io.h>

#define DRV_NAME	"mvsas"
#define DRV_VERSION	"0.5.2"
#define _MV_DUMP	0
#define MVS_DISABLE_NVRAM
#define MVS_DISABLE_MSI

#define mr32(reg)	readl(regs + MVS_##reg)
#define mw32(reg,val)	writel((val), regs + MVS_##reg)
#define mw32_f(reg,val)	do {			\
	writel((val), regs + MVS_##reg);	\
	readl(regs + MVS_##reg);		\
	} while (0)

#define MVS_ID_NOT_MAPPED	0x7f
#define MVS_CHIP_SLOT_SZ	(1U << mvi->chip->slot_width)

/* offset for D2H FIS in the Received FIS List Structure */
#define SATA_RECEIVED_D2H_FIS(reg_set)	\
	((void *) mvi->rx_fis + 0x400 + 0x100 * reg_set + 0x40)
#define SATA_RECEIVED_PIO_FIS(reg_set)	\
	((void *) mvi->rx_fis + 0x400 + 0x100 * reg_set + 0x20)
#define UNASSOC_D2H_FIS(id)		\
	((void *) mvi->rx_fis + 0x100 * id)

#define for_each_phy(__lseq_mask, __mc, __lseq, __rest)			\
	for ((__mc) = (__lseq_mask), (__lseq) = 0;			\
					(__mc) != 0 && __rest;		\
					(++__lseq), (__mc) >>= 1)

/* driver compile-time configuration */
enum driver_configuration {
	MVS_TX_RING_SZ		= 1024,	/* TX ring size (12-bit) */
	MVS_RX_RING_SZ		= 1024, /* RX ring size (12-bit) */
					/* software requires power-of-2
					   ring size */

	MVS_SLOTS		= 512,	/* command slots */
	MVS_SLOT_BUF_SZ		= 8192, /* cmd tbl + IU + status + PRD */
	MVS_SSP_CMD_SZ		= 64,	/* SSP command table buffer size */
	MVS_ATA_CMD_SZ		= 96,	/* SATA command table buffer size */
	MVS_OAF_SZ		= 64,	/* Open address frame buffer size */

	MVS_RX_FIS_COUNT	= 17,	/* Optional rx'd FISs (max 17) */

	MVS_QUEUE_SIZE		= 30,	/* Support Queue depth */
	MVS_CAN_QUEUE		= MVS_SLOTS - 1,	/* SCSI Queue depth */
};

/* unchangeable hardware details */
enum hardware_details {
	MVS_MAX_PHYS		= 8,	/* max. possible phys */
	MVS_MAX_PORTS		= 8,	/* max. possible ports */
	MVS_RX_FISL_SZ		= 0x400 + (MVS_RX_FIS_COUNT * 0x100),
};

/* peripheral registers (BAR2) */
enum peripheral_registers {
	SPI_CTL			= 0x10,	/* EEPROM control */
	SPI_CMD			= 0x14,	/* EEPROM command */
	SPI_DATA		= 0x18, /* EEPROM data */
};

enum peripheral_register_bits {
	TWSI_RDY		= (1U << 7),	/* EEPROM interface ready */
	TWSI_RD			= (1U << 4),	/* EEPROM read access */

	SPI_ADDR_MASK		= 0x3ffff,	/* bits 17:0 */
};

/* enhanced mode registers (BAR4) */
enum hw_registers {
	MVS_GBL_CTL		= 0x04,  /* global control */
	MVS_GBL_INT_STAT	= 0x08,  /* global irq status */
	MVS_GBL_PI		= 0x0C,  /* ports implemented bitmask */
	MVS_GBL_PORT_TYPE	= 0xa0,  /* port type */

	MVS_CTL			= 0x100, /* SAS/SATA port configuration */
	MVS_PCS			= 0x104, /* SAS/SATA port control/status */
	MVS_CMD_LIST_LO		= 0x108, /* cmd list addr */
	MVS_CMD_LIST_HI		= 0x10C,
	MVS_RX_FIS_LO		= 0x110, /* RX FIS list addr */
	MVS_RX_FIS_HI		= 0x114,

	MVS_TX_CFG		= 0x120, /* TX configuration */
	MVS_TX_LO		= 0x124, /* TX (delivery) ring addr */
	MVS_TX_HI		= 0x128,

	MVS_TX_PROD_IDX		= 0x12C, /* TX producer pointer */
	MVS_TX_CONS_IDX		= 0x130, /* TX consumer pointer (RO) */
	MVS_RX_CFG		= 0x134, /* RX configuration */
	MVS_RX_LO		= 0x138, /* RX (completion) ring addr */
	MVS_RX_HI		= 0x13C,
	MVS_RX_CONS_IDX		= 0x140, /* RX consumer pointer (RO) */

	MVS_INT_COAL		= 0x148, /* Int coalescing config */
	MVS_INT_COAL_TMOUT	= 0x14C, /* Int coalescing timeout */
	MVS_INT_STAT		= 0x150, /* Central int status */
	MVS_INT_MASK		= 0x154, /* Central int enable */
	MVS_INT_STAT_SRS	= 0x158, /* SATA register set status */
	MVS_INT_MASK_SRS	= 0x15C,

					 /* ports 1-3 follow after this */
	MVS_P0_INT_STAT		= 0x160, /* port0 interrupt status */
	MVS_P0_INT_MASK		= 0x164, /* port0 interrupt mask */
	MVS_P4_INT_STAT		= 0x200, /* Port 4 interrupt status */
	MVS_P4_INT_MASK		= 0x204, /* Port 4 interrupt enable mask */

					 /* ports 1-3 follow after this */
	MVS_P0_SER_CTLSTAT	= 0x180, /* port0 serial control/status */
	MVS_P4_SER_CTLSTAT	= 0x220, /* port4 serial control/status */

	MVS_CMD_ADDR		= 0x1B8, /* Command register port (addr) */
	MVS_CMD_DATA		= 0x1BC, /* Command register port (data) */

					 /* ports 1-3 follow after this */
	MVS_P0_CFG_ADDR		= 0x1C0, /* port0 phy register address */
	MVS_P0_CFG_DATA		= 0x1C4, /* port0 phy register data */
	MVS_P4_CFG_ADDR		= 0x230, /* Port 4 config address */
	MVS_P4_CFG_DATA		= 0x234, /* Port 4 config data */

					 /* ports 1-3 follow after this */
	MVS_P0_VSR_ADDR		= 0x1E0, /* port0 VSR address */
	MVS_P0_VSR_DATA		= 0x1E4, /* port0 VSR data */
	MVS_P4_VSR_ADDR		= 0x250, /* port 4 VSR addr */
	MVS_P4_VSR_DATA		= 0x254, /* port 4 VSR data */
};

enum hw_register_bits {
	/* MVS_GBL_CTL */
	INT_EN			= (1U << 1),	/* Global int enable */
	HBA_RST			= (1U << 0),	/* HBA reset */

	/* MVS_GBL_INT_STAT */
	INT_XOR			= (1U << 4),	/* XOR engine event */
	INT_SAS_SATA		= (1U << 0),	/* SAS/SATA event */

	/* MVS_GBL_PORT_TYPE */			/* shl for ports 1-3 */
	SATA_TARGET		= (1U << 16),	/* port0 SATA target enable */
	MODE_AUTO_DET_PORT7 = (1U << 15),	/* port0 SAS/SATA autodetect */
	MODE_AUTO_DET_PORT6 = (1U << 14),
	MODE_AUTO_DET_PORT5 = (1U << 13),
	MODE_AUTO_DET_PORT4 = (1U << 12),
	MODE_AUTO_DET_PORT3 = (1U << 11),
	MODE_AUTO_DET_PORT2 = (1U << 10),
	MODE_AUTO_DET_PORT1 = (1U << 9),
	MODE_AUTO_DET_PORT0 = (1U << 8),
	MODE_AUTO_DET_EN    =	MODE_AUTO_DET_PORT0 | MODE_AUTO_DET_PORT1 |
				MODE_AUTO_DET_PORT2 | MODE_AUTO_DET_PORT3 |
				MODE_AUTO_DET_PORT4 | MODE_AUTO_DET_PORT5 |
				MODE_AUTO_DET_PORT6 | MODE_AUTO_DET_PORT7,
	MODE_SAS_PORT7_MASK = (1U << 7),  /* port0 SAS(1), SATA(0) mode */
	MODE_SAS_PORT6_MASK = (1U << 6),
	MODE_SAS_PORT5_MASK = (1U << 5),
	MODE_SAS_PORT4_MASK = (1U << 4),
	MODE_SAS_PORT3_MASK = (1U << 3),
	MODE_SAS_PORT2_MASK = (1U << 2),
	MODE_SAS_PORT1_MASK = (1U << 1),
	MODE_SAS_PORT0_MASK = (1U << 0),
	MODE_SAS_SATA	=	MODE_SAS_PORT0_MASK | MODE_SAS_PORT1_MASK |
				MODE_SAS_PORT2_MASK | MODE_SAS_PORT3_MASK |
				MODE_SAS_PORT4_MASK | MODE_SAS_PORT5_MASK |
				MODE_SAS_PORT6_MASK | MODE_SAS_PORT7_MASK,

				/* SAS_MODE value may be
				 * dictated (in hw) by values
				 * of SATA_TARGET & AUTO_DET
				 */

	/* MVS_TX_CFG */
	TX_EN			= (1U << 16),	/* Enable TX */
	TX_RING_SZ_MASK		= 0xfff,	/* TX ring size, bits 11:0 */

	/* MVS_RX_CFG */
	RX_EN			= (1U << 16),	/* Enable RX */
	RX_RING_SZ_MASK		= 0xfff,	/* RX ring size, bits 11:0 */

	/* MVS_INT_COAL */
	COAL_EN			= (1U << 16),	/* Enable int coalescing */

	/* MVS_INT_STAT, MVS_INT_MASK */
	CINT_I2C		= (1U << 31),	/* I2C event */
	CINT_SW0		= (1U << 30),	/* software event 0 */
	CINT_SW1		= (1U << 29),	/* software event 1 */
	CINT_PRD_BC		= (1U << 28),	/* PRD BC err for read cmd */
	CINT_DMA_PCIE		= (1U << 27),	/* DMA to PCIE timeout */
	CINT_MEM		= (1U << 26),	/* int mem parity err */
	CINT_I2C_SLAVE		= (1U << 25),	/* slave I2C event */
	CINT_SRS		= (1U << 3),	/* SRS event */
	CINT_CI_STOP		= (1U << 1),	/* cmd issue stopped */
	CINT_DONE		= (1U << 0),	/* cmd completion */

						/* shl for ports 1-3 */
	CINT_PORT_STOPPED	= (1U << 16),	/* port0 stopped */
	CINT_PORT		= (1U << 8),	/* port0 event */
	CINT_PORT_MASK_OFFSET	= 8,
	CINT_PORT_MASK		= (0xFF << CINT_PORT_MASK_OFFSET),

	/* TX (delivery) ring bits */
	TXQ_CMD_SHIFT		= 29,
	TXQ_CMD_SSP		= 1,		/* SSP protocol */
	TXQ_CMD_SMP		= 2,		/* SMP protocol */
	TXQ_CMD_STP		= 3,		/* STP/SATA protocol */
	TXQ_CMD_SSP_FREE_LIST	= 4,		/* add to SSP targ free list */
	TXQ_CMD_SLOT_RESET	= 7,		/* reset command slot */
	TXQ_MODE_I		= (1U << 28),	/* mode: 0=target,1=initiator */
	TXQ_PRIO_HI		= (1U << 27),	/* priority: 0=normal, 1=high */
	TXQ_SRS_SHIFT		= 20,		/* SATA register set */
	TXQ_SRS_MASK		= 0x7f,
	TXQ_PHY_SHIFT		= 12,		/* PHY bitmap */
	TXQ_PHY_MASK		= 0xff,
	TXQ_SLOT_MASK		= 0xfff,	/* slot number */

	/* RX (completion) ring bits */
	RXQ_GOOD		= (1U << 23),	/* Response good */
	RXQ_SLOT_RESET		= (1U << 21),	/* Slot reset complete */
	RXQ_CMD_RX		= (1U << 20),	/* target cmd received */
	RXQ_ATTN		= (1U << 19),	/* attention */
	RXQ_RSP			= (1U << 18),	/* response frame xfer'd */
	RXQ_ERR			= (1U << 17),	/* err info rec xfer'd */
	RXQ_DONE		= (1U << 16),	/* cmd complete */
	RXQ_SLOT_MASK		= 0xfff,	/* slot number */

	/* mvs_cmd_hdr bits */
	MCH_PRD_LEN_SHIFT	= 16,		/* 16-bit PRD table len */
	MCH_SSP_FR_TYPE_SHIFT	= 13,		/* SSP frame type */

						/* SSP initiator only */
	MCH_SSP_FR_CMD		= 0x0,		/* COMMAND frame */

						/* SSP initiator or target */
	MCH_SSP_FR_TASK		= 0x1,		/* TASK frame */

						/* SSP target only */
	MCH_SSP_FR_XFER_RDY	= 0x4,		/* XFER_RDY frame */
	MCH_SSP_FR_RESP		= 0x5,		/* RESPONSE frame */
	MCH_SSP_FR_READ		= 0x6,		/* Read DATA frame(s) */
	MCH_SSP_FR_READ_RESP	= 0x7,		/* ditto, plus RESPONSE */

	MCH_PASSTHRU		= (1U << 12),	/* pass-through (SSP) */
	MCH_FBURST		= (1U << 11),	/* first burst (SSP) */
	MCH_CHK_LEN		= (1U << 10),	/* chk xfer len (SSP) */
	MCH_RETRY		= (1U << 9),	/* tport layer retry (SSP) */
	MCH_PROTECTION		= (1U << 8),	/* protection info rec (SSP) */
	MCH_RESET		= (1U << 7),	/* Reset (STP/SATA) */
	MCH_FPDMA		= (1U << 6),	/* First party DMA (STP/SATA) */
	MCH_ATAPI		= (1U << 5),	/* ATAPI (STP/SATA) */
	MCH_BIST		= (1U << 4),	/* BIST activate (STP/SATA) */
	MCH_PMP_MASK		= 0xf,		/* PMP from cmd FIS (STP/SATA)*/

	CCTL_RST		= (1U << 5),	/* port logic reset */

						/* 0(LSB first), 1(MSB first) */
	CCTL_ENDIAN_DATA	= (1U << 3),	/* PRD data */
	CCTL_ENDIAN_RSP		= (1U << 2),	/* response frame */
	CCTL_ENDIAN_OPEN	= (1U << 1),	/* open address frame */
	CCTL_ENDIAN_CMD		= (1U << 0),	/* command table */

	/* MVS_Px_SER_CTLSTAT (per-phy control) */
	PHY_SSP_RST		= (1U << 3),	/* reset SSP link layer */
	PHY_BCAST_CHG		= (1U << 2),	/* broadcast(change) notif */
	PHY_RST_HARD		= (1U << 1),	/* hard reset + phy reset */
	PHY_RST			= (1U << 0),	/* phy reset */
	PHY_MIN_SPP_PHYS_LINK_RATE_MASK = (0xF << 8),
	PHY_MAX_SPP_PHYS_LINK_RATE_MASK = (0xF << 12),
	PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET = (16),
	PHY_NEG_SPP_PHYS_LINK_RATE_MASK =
			(0xF << PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET),
	PHY_READY_MASK		= (1U << 20),

	/* MVS_Px_INT_STAT, MVS_Px_INT_MASK (per-phy events) */
	PHYEV_DEC_ERR		= (1U << 24),	/* Phy Decoding Error */
	PHYEV_UNASSOC_FIS	= (1U << 19),	/* unassociated FIS rx'd */
	PHYEV_AN		= (1U << 18),	/* SATA async notification */
	PHYEV_BIST_ACT		= (1U << 17),	/* BIST activate FIS */
	PHYEV_SIG_FIS		= (1U << 16),	/* signature FIS */
	PHYEV_POOF		= (1U << 12),	/* phy ready from 1 -> 0 */
	PHYEV_IU_BIG		= (1U << 11),	/* IU too long err */
	PHYEV_IU_SMALL		= (1U << 10),	/* IU too short err */
	PHYEV_UNK_TAG		= (1U << 9),	/* unknown tag */
	PHYEV_BROAD_CH		= (1U << 8),	/* broadcast(CHANGE) */
	PHYEV_COMWAKE		= (1U << 7),	/* COMWAKE rx'd */
	PHYEV_PORT_SEL		= (1U << 6),	/* port selector present */
	PHYEV_HARD_RST		= (1U << 5),	/* hard reset rx'd */
	PHYEV_ID_TMOUT		= (1U << 4),	/* identify timeout */
	PHYEV_ID_FAIL		= (1U << 3),	/* identify failed */
	PHYEV_ID_DONE		= (1U << 2),	/* identify done */
	PHYEV_HARD_RST_DONE	= (1U << 1),	/* hard reset done */
	PHYEV_RDY_CH		= (1U << 0),	/* phy ready changed state */

	/* MVS_PCS */
	PCS_EN_SATA_REG_SHIFT	= (16),		/* Enable SATA Register Set */
	PCS_EN_PORT_XMT_SHIFT	= (12),		/* Enable Port Transmit */
	PCS_EN_PORT_XMT_SHIFT2	= (8),		/* For 6480 */
	PCS_SATA_RETRY		= (1U << 8),	/* retry ctl FIS on R_ERR */
	PCS_RSP_RX_EN		= (1U << 7),	/* raw response rx */
	PCS_SELF_CLEAR		= (1U << 5),	/* self-clearing int mode */
	PCS_FIS_RX_EN		= (1U << 4),	/* FIS rx enable */
	PCS_CMD_STOP_ERR	= (1U << 3),	/* cmd stop-on-err enable */
	PCS_CMD_RST		= (1U << 1),	/* reset cmd issue */
	PCS_CMD_EN		= (1U << 0),	/* enable cmd issue */

	/* Port n Attached Device Info */
	PORT_DEV_SSP_TRGT	= (1U << 19),
	PORT_DEV_SMP_TRGT	= (1U << 18),
	PORT_DEV_STP_TRGT	= (1U << 17),
	PORT_DEV_SSP_INIT	= (1U << 11),
	PORT_DEV_SMP_INIT	= (1U << 10),
	PORT_DEV_STP_INIT	= (1U << 9),
	PORT_PHY_ID_MASK	= (0xFFU << 24),
	PORT_DEV_TRGT_MASK	= (0x7U << 17),
	PORT_DEV_INIT_MASK	= (0x7U << 9),
	PORT_DEV_TYPE_MASK	= (0x7U << 0),

	/* Port n PHY Status */
	PHY_RDY			= (1U << 2),
	PHY_DW_SYNC		= (1U << 1),
	PHY_OOB_DTCTD		= (1U << 0),

	/* VSR */
	/* PHYMODE 6 (CDB) */
	PHY_MODE6_LATECLK	= (1U << 29),	/* Lock Clock */
	PHY_MODE6_DTL_SPEED	= (1U << 27),	/* Digital Loop Speed */
	PHY_MODE6_FC_ORDER	= (1U << 26),	/* Fibre Channel Mode Order*/
	PHY_MODE6_MUCNT_EN	= (1U << 24),	/* u Count Enable */
	PHY_MODE6_SEL_MUCNT_LEN	= (1U << 22),	/* Training Length Select */
	PHY_MODE6_SELMUPI	= (1U << 20),	/* Phase Multi Select (init) */
	PHY_MODE6_SELMUPF	= (1U << 18),	/* Phase Multi Select (final) */
	PHY_MODE6_SELMUFF	= (1U << 16),	/* Freq Loop Multi Sel(final) */
	PHY_MODE6_SELMUFI	= (1U << 14),	/* Freq Loop Multi Sel(init) */
	PHY_MODE6_FREEZE_LOOP	= (1U << 12),	/* Freeze Rx CDR Loop */
	PHY_MODE6_INT_RXFOFFS	= (1U << 3),	/* Rx CDR Freq Loop Enable */
	PHY_MODE6_FRC_RXFOFFS	= (1U << 2),	/* Initial Rx CDR Offset */
	PHY_MODE6_STAU_0D8	= (1U << 1),	/* Rx CDR Freq Loop Saturate */
	PHY_MODE6_RXSAT_DIS	= (1U << 0),	/* Saturate Ctl */
};

enum mvs_info_flags {
	MVF_MSI			= (1U << 0),	/* MSI is enabled */
	MVF_PHY_PWR_FIX		= (1U << 1),	/* bug workaround */
};

enum sas_cmd_port_registers {
	CMD_CMRST_OOB_DET	= 0x100, /* COMRESET OOB detect register */
	CMD_CMWK_OOB_DET	= 0x104, /* COMWAKE OOB detect register */
	CMD_CMSAS_OOB_DET	= 0x108, /* COMSAS OOB detect register */
	CMD_BRST_OOB_DET	= 0x10c, /* burst OOB detect register */
	CMD_OOB_SPACE		= 0x110, /* OOB space control register */
	CMD_OOB_BURST		= 0x114, /* OOB burst control register */
	CMD_PHY_TIMER		= 0x118, /* PHY timer control register */
	CMD_PHY_CONFIG0		= 0x11c, /* PHY config register 0 */
	CMD_PHY_CONFIG1		= 0x120, /* PHY config register 1 */
	CMD_SAS_CTL0		= 0x124, /* SAS control register 0 */
	CMD_SAS_CTL1		= 0x128, /* SAS control register 1 */
	CMD_SAS_CTL2		= 0x12c, /* SAS control register 2 */
	CMD_SAS_CTL3		= 0x130, /* SAS control register 3 */
	CMD_ID_TEST		= 0x134, /* ID test register */
	CMD_PL_TIMER		= 0x138, /* PL timer register */
	CMD_WD_TIMER		= 0x13c, /* WD timer register */
	CMD_PORT_SEL_COUNT	= 0x140, /* port selector count register */
	CMD_APP_MEM_CTL		= 0x144, /* Application Memory Control */
	CMD_XOR_MEM_CTL		= 0x148, /* XOR Block Memory Control */
	CMD_DMA_MEM_CTL		= 0x14c, /* DMA Block Memory Control */
	CMD_PORT_MEM_CTL0	= 0x150, /* Port Memory Control 0 */
	CMD_PORT_MEM_CTL1	= 0x154, /* Port Memory Control 1 */
	CMD_SATA_PORT_MEM_CTL0	= 0x158, /* SATA Port Memory Control 0 */
	CMD_SATA_PORT_MEM_CTL1	= 0x15c, /* SATA Port Memory Control 1 */
	CMD_XOR_MEM_BIST_CTL	= 0x160, /* XOR Memory BIST Control */
	CMD_XOR_MEM_BIST_STAT	= 0x164, /* XOR Memroy BIST Status */
	CMD_DMA_MEM_BIST_CTL	= 0x168, /* DMA Memory BIST Control */
	CMD_DMA_MEM_BIST_STAT	= 0x16c, /* DMA Memory BIST Status */
	CMD_PORT_MEM_BIST_CTL	= 0x170, /* Port Memory BIST Control */
	CMD_PORT_MEM_BIST_STAT0 = 0x174, /* Port Memory BIST Status 0 */
	CMD_PORT_MEM_BIST_STAT1 = 0x178, /* Port Memory BIST Status 1 */
	CMD_STP_MEM_BIST_CTL	= 0x17c, /* STP Memory BIST Control */
	CMD_STP_MEM_BIST_STAT0	= 0x180, /* STP Memory BIST Status 0 */
	CMD_STP_MEM_BIST_STAT1	= 0x184, /* STP Memory BIST Status 1 */
	CMD_RESET_COUNT		= 0x188, /* Reset Count */
	CMD_MONTR_DATA_SEL	= 0x18C, /* Monitor Data/Select */
	CMD_PLL_PHY_CONFIG	= 0x190, /* PLL/PHY Configuration */
	CMD_PHY_CTL		= 0x194, /* PHY Control and Status */
	CMD_PHY_TEST_COUNT0	= 0x198, /* Phy Test Count 0 */
	CMD_PHY_TEST_COUNT1	= 0x19C, /* Phy Test Count 1 */
	CMD_PHY_TEST_COUNT2	= 0x1A0, /* Phy Test Count 2 */
	CMD_APP_ERR_CONFIG	= 0x1A4, /* Application Error Configuration */
	CMD_PND_FIFO_CTL0	= 0x1A8, /* Pending FIFO Control 0 */
	CMD_HOST_CTL		= 0x1AC, /* Host Control Status */
	CMD_HOST_WR_DATA	= 0x1B0, /* Host Write Data */
	CMD_HOST_RD_DATA	= 0x1B4, /* Host Read Data */
	CMD_PHY_MODE_21		= 0x1B8, /* Phy Mode 21 */
	CMD_SL_MODE0		= 0x1BC, /* SL Mode 0 */
	CMD_SL_MODE1		= 0x1C0, /* SL Mode 1 */
	CMD_PND_FIFO_CTL1	= 0x1C4, /* Pending FIFO Control 1 */
};

/* SAS/SATA configuration port registers, aka phy registers */
enum sas_sata_config_port_regs {
	PHYR_IDENTIFY		= 0x00,	/* info for IDENTIFY frame */
	PHYR_ADDR_LO		= 0x04,	/* my SAS address (low) */
	PHYR_ADDR_HI		= 0x08,	/* my SAS address (high) */
	PHYR_ATT_DEV_INFO	= 0x0C,	/* attached device info */
	PHYR_ATT_ADDR_LO	= 0x10,	/* attached dev SAS addr (low) */
	PHYR_ATT_ADDR_HI	= 0x14,	/* attached dev SAS addr (high) */
	PHYR_SATA_CTL		= 0x18,	/* SATA control */
	PHYR_PHY_STAT		= 0x1C,	/* PHY status */
	PHYR_SATA_SIG0		= 0x20,	/*port SATA signature FIS(Byte 0-3) */
	PHYR_SATA_SIG1		= 0x24,	/*port SATA signature FIS(Byte 4-7) */
	PHYR_SATA_SIG2		= 0x28,	/*port SATA signature FIS(Byte 8-11) */
	PHYR_SATA_SIG3		= 0x2c,	/*port SATA signature FIS(Byte 12-15) */
	PHYR_R_ERR_COUNT	= 0x30, /* port R_ERR count register */
	PHYR_CRC_ERR_COUNT	= 0x34, /* port CRC error count register */
	PHYR_WIDE_PORT		= 0x38,	/* wide port participating */
	PHYR_CURRENT0		= 0x80,	/* current connection info 0 */
	PHYR_CURRENT1		= 0x84,	/* current connection info 1 */
	PHYR_CURRENT2		= 0x88,	/* current connection info 2 */
};

/*  SAS/SATA Vendor Specific Port Registers */
enum sas_sata_vsp_regs {
	VSR_PHY_STAT		= 0x00, /* Phy Status */
	VSR_PHY_MODE1		= 0x01, /* phy tx */
	VSR_PHY_MODE2		= 0x02, /* tx scc */
	VSR_PHY_MODE3		= 0x03, /* pll */
	VSR_PHY_MODE4		= 0x04, /* VCO */
	VSR_PHY_MODE5		= 0x05, /* Rx */
	VSR_PHY_MODE6		= 0x06, /* CDR */
	VSR_PHY_MODE7		= 0x07, /* Impedance */
	VSR_PHY_MODE8		= 0x08, /* Voltage */
	VSR_PHY_MODE9		= 0x09, /* Test */
	VSR_PHY_MODE10		= 0x0A, /* Power */
	VSR_PHY_MODE11		= 0x0B, /* Phy Mode */
	VSR_PHY_VS0		= 0x0C, /* Vednor Specific 0 */
	VSR_PHY_VS1		= 0x0D, /* Vednor Specific 1 */
};

enum pci_cfg_registers {
	PCR_PHY_CTL	= 0x40,
	PCR_PHY_CTL2	= 0x90,
	PCR_DEV_CTRL	= 0xE8,
};

enum pci_cfg_register_bits {
	PCTL_PWR_ON	= (0xFU << 24),
	PCTL_OFF	= (0xFU << 12),
	PRD_REQ_SIZE	= (0x4000),
	PRD_REQ_MASK	= (0x00007000),
};

enum nvram_layout_offsets {
	NVR_SIG		= 0x00,		/* 0xAA, 0x55 */
	NVR_SAS_ADDR	= 0x02,		/* 8-byte SAS address */
};

enum chip_flavors {
	chip_6320,
	chip_6440,
	chip_6480,
};

enum port_type {
	PORT_TYPE_SAS	=  (1L << 1),
	PORT_TYPE_SATA	=  (1L << 0),
};

/* Command Table Format */
enum ct_format {
	/* SSP */
	SSP_F_H		=  0x00,
	SSP_F_IU	=  0x18,
	SSP_F_MAX	=  0x4D,
	/* STP */
	STP_CMD_FIS	=  0x00,
	STP_ATAPI_CMD	=  0x40,
	STP_F_MAX	=  0x10,
	/* SMP */
	SMP_F_T		=  0x00,
	SMP_F_DEP	=  0x01,
	SMP_F_MAX	=  0x101,
};

enum status_buffer {
	SB_EIR_OFF	=  0x00,	/* Error Information Record */
	SB_RFB_OFF	=  0x08,	/* Response Frame Buffer */
	SB_RFB_MAX	=  0x400,	/* RFB size*/
};

enum error_info_rec {
	CMD_ISS_STPD	= (1U << 31),	/* Cmd Issue Stopped */
	CMD_PI_ERR	= (1U << 30),	/* Protection info error.  see flags2 */
	RSP_OVER	= (1U << 29),	/* rsp buffer overflow */
	RETRY_LIM	= (1U << 28),	/* FIS/frame retry limit exceeded */
	UNK_FIS 	= (1U << 27),	/* unknown FIS */
	DMA_TERM	= (1U << 26),	/* DMA terminate primitive rx'd */
	SYNC_ERR	= (1U << 25),	/* SYNC rx'd during frame xmit */
	TFILE_ERR	= (1U << 24),	/* SATA taskfile Error bit set */
	R_ERR		= (1U << 23),	/* SATA returned R_ERR prim */
	RD_OFS		= (1U << 20),	/* Read DATA frame invalid offset */
	XFER_RDY_OFS	= (1U << 19),	/* XFER_RDY offset error */
	UNEXP_XFER_RDY	= (1U << 18),	/* unexpected XFER_RDY error */
	DATA_OVER_UNDER = (1U << 16),	/* data overflow/underflow */
	INTERLOCK	= (1U << 15),	/* interlock error */
	NAK		= (1U << 14),	/* NAK rx'd */
	ACK_NAK_TO	= (1U << 13),	/* ACK/NAK timeout */
	CXN_CLOSED	= (1U << 12),	/* cxn closed w/out ack/nak */
	OPEN_TO 	= (1U << 11),	/* I_T nexus lost, open cxn timeout */
	PATH_BLOCKED	= (1U << 10),	/* I_T nexus lost, pathway blocked */
	NO_DEST 	= (1U << 9),	/* I_T nexus lost, no destination */
	STP_RES_BSY	= (1U << 8),	/* STP resources busy */
	BREAK		= (1U << 7),	/* break received */
	BAD_DEST	= (1U << 6),	/* bad destination */
	BAD_PROTO	= (1U << 5),	/* protocol not supported */
	BAD_RATE	= (1U << 4),	/* cxn rate not supported */
	WRONG_DEST	= (1U << 3),	/* wrong destination error */
	CREDIT_TO	= (1U << 2),	/* credit timeout */
	WDOG_TO 	= (1U << 1),	/* watchdog timeout */
	BUF_PAR 	= (1U << 0),	/* buffer parity error */
};

enum error_info_rec_2 {
	SLOT_BSY_ERR	= (1U << 31),	/* Slot Busy Error */
	GRD_CHK_ERR	= (1U << 14),	/* Guard Check Error */
	APP_CHK_ERR	= (1U << 13),	/* Application Check error */
	REF_CHK_ERR	= (1U << 12),	/* Reference Check Error */
	USR_BLK_NM	= (1U << 0),	/* User Block Number */
};

struct mvs_chip_info {
	u32		n_phy;
	u32		srs_sz;
	u32		slot_width;
};

struct mvs_err_info {
	__le32			flags;
	__le32			flags2;
};

struct mvs_prd {
	__le64			addr;		/* 64-bit buffer address */
	__le32			reserved;
	__le32			len;		/* 16-bit length */
};

struct mvs_cmd_hdr {
	__le32			flags;		/* PRD tbl len; SAS, SATA ctl */
	__le32			lens;		/* cmd, max resp frame len */
	__le32			tags;		/* targ port xfer tag; tag */
	__le32			data_len;	/* data xfer len */
	__le64			cmd_tbl;	/* command table address */
	__le64			open_frame;	/* open addr frame address */
	__le64			status_buf;	/* status buffer address */
	__le64			prd_tbl;	/* PRD tbl address */
	__le32			reserved[4];
};

struct mvs_port {
	struct asd_sas_port	sas_port;
	u8			port_attached;
	u8			taskfileset;
	u8			wide_port_phymap;
	struct list_head	list;
};

struct mvs_phy {
	struct mvs_port		*port;
	struct asd_sas_phy	sas_phy;
	struct sas_identify	identify;
	struct scsi_device	*sdev;
	u64		dev_sas_addr;
	u64		att_dev_sas_addr;
	u32		att_dev_info;
	u32		dev_info;
	u32		phy_type;
	u32		phy_status;
	u32		irq_status;
	u32		frame_rcvd_size;
	u8		frame_rcvd[32];
	u8		phy_attached;
	enum sas_linkrate	minimum_linkrate;
	enum sas_linkrate	maximum_linkrate;
};

struct mvs_slot_info {
	struct list_head	list;
	struct sas_task		*task;
	u32			n_elem;
	u32			tx;

	/* DMA buffer for storing cmd tbl, open addr frame, status buffer,
	 * and PRD table
	 */
	void			*buf;
	dma_addr_t		buf_dma;
#if _MV_DUMP
	u32			cmd_size;
#endif

	void			*response;
	struct mvs_port		*port;
};

struct mvs_info {
	unsigned long		flags;

	spinlock_t		lock;		/* host-wide lock */
	struct pci_dev		*pdev;		/* our device */
	void __iomem		*regs;		/* enhanced mode registers */
	void __iomem		*peri_regs;	/* peripheral registers */

	u8			sas_addr[SAS_ADDR_SIZE];
	struct sas_ha_struct	sas;		/* SCSI/SAS glue */
	struct Scsi_Host	*shost;

	__le32			*tx;		/* TX (delivery) DMA ring */
	dma_addr_t		tx_dma;
	u32			tx_prod;	/* cached next-producer idx */

	__le32			*rx;		/* RX (completion) DMA ring */
	dma_addr_t		rx_dma;
	u32			rx_cons;	/* RX consumer idx */

	__le32			*rx_fis;	/* RX'd FIS area */
	dma_addr_t		rx_fis_dma;

	struct mvs_cmd_hdr	*slot;	/* DMA command header slots */
	dma_addr_t		slot_dma;

	const struct mvs_chip_info *chip;

	u8			tags[MVS_SLOTS];
	struct mvs_slot_info	slot_info[MVS_SLOTS];
				/* further per-slot information */
	struct mvs_phy		phy[MVS_MAX_PHYS];
	struct mvs_port		port[MVS_MAX_PHYS];
#ifdef MVS_USE_TASKLET
	struct tasklet_struct	tasklet;
#endif
};

static int mvs_phy_control(struct asd_sas_phy *sas_phy, enum phy_func func,
			   void *funcdata);
static u32 mvs_read_phy_ctl(struct mvs_info *mvi, u32 port);
static void mvs_write_phy_ctl(struct mvs_info *mvi, u32 port, u32 val);
static u32 mvs_read_port_irq_stat(struct mvs_info *mvi, u32 port);
static void mvs_write_port_irq_stat(struct mvs_info *mvi, u32 port, u32 val);
static void mvs_write_port_irq_mask(struct mvs_info *mvi, u32 port, u32 val);
static u32 mvs_read_port_irq_mask(struct mvs_info *mvi, u32 port);

static u32 mvs_is_phy_ready(struct mvs_info *mvi, int i);
static void mvs_detect_porttype(struct mvs_info *mvi, int i);
static void mvs_update_phyinfo(struct mvs_info *mvi, int i, int get_st);
static void mvs_release_task(struct mvs_info *mvi, int phy_no);

static int mvs_scan_finished(struct Scsi_Host *, unsigned long);
static void mvs_scan_start(struct Scsi_Host *);
static int mvs_slave_configure(struct scsi_device *sdev);

static struct scsi_transport_template *mvs_stt;

static const struct mvs_chip_info mvs_chips[] = {
	[chip_6320] =		{ 2, 16, 9  },
	[chip_6440] =		{ 4, 16, 9  },
	[chip_6480] =		{ 8, 32, 10 },
};

static struct scsi_host_template mvs_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= sas_queuecommand,
	.target_alloc		= sas_target_alloc,
	.slave_configure	= mvs_slave_configure,
	.slave_destroy		= sas_slave_destroy,
	.scan_finished		= mvs_scan_finished,
	.scan_start		= mvs_scan_start,
	.change_queue_depth	= sas_change_queue_depth,
	.change_queue_type	= sas_change_queue_type,
	.bios_param		= sas_bios_param,
	.can_queue		= 1,
	.cmd_per_lun		= 1,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.max_sectors		= SCSI_DEFAULT_MAX_SECTORS,
	.use_clustering		= ENABLE_CLUSTERING,
	.eh_device_reset_handler	= sas_eh_device_reset_handler,
	.eh_bus_reset_handler	= sas_eh_bus_reset_handler,
	.slave_alloc		= sas_slave_alloc,
	.target_destroy		= sas_target_destroy,
	.ioctl			= sas_ioctl,
};

static void mvs_hexdump(u32 size, u8 *data, u32 baseaddr)
{
	u32 i;
	u32 run;
	u32 offset;

	offset = 0;
	while (size) {
		printk("%08X : ", baseaddr + offset);
		if (size >= 16)
			run = 16;
		else
			run = size;
		size -= run;
		for (i = 0; i < 16; i++) {
			if (i < run)
				printk("%02X ", (u32)data[i]);
			else
				printk("   ");
		}
		printk(": ");
		for (i = 0; i < run; i++)
			printk("%c", isalnum(data[i]) ? data[i] : '.');
		printk("\n");
		data = &data[16];
		offset += run;
	}
	printk("\n");
}

#if _MV_DUMP
static void mvs_hba_sb_dump(struct mvs_info *mvi, u32 tag,
				   enum sas_protocol proto)
{
	u32 offset;
	struct pci_dev *pdev = mvi->pdev;
	struct mvs_slot_info *slot = &mvi->slot_info[tag];

	offset = slot->cmd_size + MVS_OAF_SZ +
	    sizeof(struct mvs_prd) * slot->n_elem;
	dev_printk(KERN_DEBUG, &pdev->dev, "+---->Status buffer[%d] :\n",
			tag);
	mvs_hexdump(32, (u8 *) slot->response,
		    (u32) slot->buf_dma + offset);
}
#endif

static void mvs_hba_memory_dump(struct mvs_info *mvi, u32 tag,
				enum sas_protocol proto)
{
#if _MV_DUMP
	u32 sz, w_ptr;
	u64 addr;
	void __iomem *regs = mvi->regs;
	struct pci_dev *pdev = mvi->pdev;
	struct mvs_slot_info *slot = &mvi->slot_info[tag];

	/*Delivery Queue */
	sz = mr32(TX_CFG) & TX_RING_SZ_MASK;
	w_ptr = slot->tx;
	addr = mr32(TX_HI) << 16 << 16 | mr32(TX_LO);
	dev_printk(KERN_DEBUG, &pdev->dev,
		"Delivery Queue Size=%04d , WRT_PTR=%04X\n", sz, w_ptr);
	dev_printk(KERN_DEBUG, &pdev->dev,
		"Delivery Queue Base Address=0x%llX (PA)"
		"(tx_dma=0x%llX), Entry=%04d\n",
		addr, mvi->tx_dma, w_ptr);
	mvs_hexdump(sizeof(u32), (u8 *)(&mvi->tx[mvi->tx_prod]),
			(u32) mvi->tx_dma + sizeof(u32) * w_ptr);
	/*Command List */
	addr = mvi->slot_dma;
	dev_printk(KERN_DEBUG, &pdev->dev,
		"Command List Base Address=0x%llX (PA)"
		"(slot_dma=0x%llX), Header=%03d\n",
		addr, slot->buf_dma, tag);
	dev_printk(KERN_DEBUG, &pdev->dev, "Command Header[%03d]:\n", tag);
	/*mvs_cmd_hdr */
	mvs_hexdump(sizeof(struct mvs_cmd_hdr), (u8 *)(&mvi->slot[tag]),
		(u32) mvi->slot_dma + tag * sizeof(struct mvs_cmd_hdr));
	/*1.command table area */
	dev_printk(KERN_DEBUG, &pdev->dev, "+---->Command Table :\n");
	mvs_hexdump(slot->cmd_size, (u8 *) slot->buf, (u32) slot->buf_dma);
	/*2.open address frame area */
	dev_printk(KERN_DEBUG, &pdev->dev, "+---->Open Address Frame :\n");
	mvs_hexdump(MVS_OAF_SZ, (u8 *) slot->buf + slot->cmd_size,
				(u32) slot->buf_dma + slot->cmd_size);
	/*3.status buffer */
	mvs_hba_sb_dump(mvi, tag, proto);
	/*4.PRD table */
	dev_printk(KERN_DEBUG, &pdev->dev, "+---->PRD table :\n");
	mvs_hexdump(sizeof(struct mvs_prd) * slot->n_elem,
		(u8 *) slot->buf + slot->cmd_size + MVS_OAF_SZ,
		(u32) slot->buf_dma + slot->cmd_size + MVS_OAF_SZ);
#endif
}

static void mvs_hba_cq_dump(struct mvs_info *mvi)
{
#if (_MV_DUMP > 2)
	u64 addr;
	void __iomem *regs = mvi->regs;
	struct pci_dev *pdev = mvi->pdev;
	u32 entry = mvi->rx_cons + 1;
	u32 rx_desc = le32_to_cpu(mvi->rx[entry]);

	/*Completion Queue */
	addr = mr32(RX_HI) << 16 << 16 | mr32(RX_LO);
	dev_printk(KERN_DEBUG, &pdev->dev, "Completion Task = 0x%p\n",
		   mvi->slot_info[rx_desc & RXQ_SLOT_MASK].task);
	dev_printk(KERN_DEBUG, &pdev->dev,
		"Completion List Base Address=0x%llX (PA), "
		"CQ_Entry=%04d, CQ_WP=0x%08X\n",
		addr, entry - 1, mvi->rx[0]);
	mvs_hexdump(sizeof(u32), (u8 *)(&rx_desc),
		    mvi->rx_dma + sizeof(u32) * entry);
#endif
}

static void mvs_hba_interrupt_enable(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	tmp = mr32(GBL_CTL);

	mw32(GBL_CTL, tmp | INT_EN);
}

static void mvs_hba_interrupt_disable(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	tmp = mr32(GBL_CTL);

	mw32(GBL_CTL, tmp & ~INT_EN);
}

static int mvs_int_rx(struct mvs_info *mvi, bool self_clear);

/* move to PCI layer or libata core? */
static int pci_go_64(struct pci_dev *pdev)
{
	int rc;

	if (!pci_set_dma_mask(pdev, DMA_64BIT_MASK)) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
		if (rc) {
			rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
			if (rc) {
				dev_printk(KERN_ERR, &pdev->dev,
					   "64-bit DMA enable failed\n");
				return rc;
			}
		}
	} else {
		rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc) {
			dev_printk(KERN_ERR, &pdev->dev,
				   "32-bit DMA enable failed\n");
			return rc;
		}
		rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc) {
			dev_printk(KERN_ERR, &pdev->dev,
				   "32-bit consistent DMA enable failed\n");
			return rc;
		}
	}

	return rc;
}

static int mvs_find_tag(struct mvs_info *mvi, struct sas_task *task, u32 *tag)
{
	if (task->lldd_task) {
		struct mvs_slot_info *slot;
		slot = (struct mvs_slot_info *) task->lldd_task;
		*tag = slot - mvi->slot_info;
		return 1;
	}
	return 0;
}

static void mvs_tag_clear(struct mvs_info *mvi, u32 tag)
{
	void *bitmap = (void *) &mvi->tags;
	clear_bit(tag, bitmap);
}

static void mvs_tag_free(struct mvs_info *mvi, u32 tag)
{
	mvs_tag_clear(mvi, tag);
}

static void mvs_tag_set(struct mvs_info *mvi, unsigned int tag)
{
	void *bitmap = (void *) &mvi->tags;
	set_bit(tag, bitmap);
}

static int mvs_tag_alloc(struct mvs_info *mvi, u32 *tag_out)
{
	unsigned int index, tag;
	void *bitmap = (void *) &mvi->tags;

	index = find_first_zero_bit(bitmap, MVS_SLOTS);
	tag = index;
	if (tag >= MVS_SLOTS)
		return -SAS_QUEUE_FULL;
	mvs_tag_set(mvi, tag);
	*tag_out = tag;
	return 0;
}

static void mvs_tag_init(struct mvs_info *mvi)
{
	int i;
	for (i = 0; i < MVS_SLOTS; ++i)
		mvs_tag_clear(mvi, i);
}

#ifndef MVS_DISABLE_NVRAM
static int mvs_eep_read(void __iomem *regs, u32 addr, u32 *data)
{
	int timeout = 1000;

	if (addr & ~SPI_ADDR_MASK)
		return -EINVAL;

	writel(addr, regs + SPI_CMD);
	writel(TWSI_RD, regs + SPI_CTL);

	while (timeout-- > 0) {
		if (readl(regs + SPI_CTL) & TWSI_RDY) {
			*data = readl(regs + SPI_DATA);
			return 0;
		}

		udelay(10);
	}

	return -EBUSY;
}

static int mvs_eep_read_buf(void __iomem *regs, u32 addr,
			    void *buf, u32 buflen)
{
	u32 addr_end, tmp_addr, i, j;
	u32 tmp = 0;
	int rc;
	u8 *tmp8, *buf8 = buf;

	addr_end = addr + buflen;
	tmp_addr = ALIGN(addr, 4);
	if (addr > 0xff)
		return -EINVAL;

	j = addr & 0x3;
	if (j) {
		rc = mvs_eep_read(regs, tmp_addr, &tmp);
		if (rc)
			return rc;

		tmp8 = (u8 *)&tmp;
		for (i = j; i < 4; i++)
			*buf8++ = tmp8[i];

		tmp_addr += 4;
	}

	for (j = ALIGN(addr_end, 4); tmp_addr < j; tmp_addr += 4) {
		rc = mvs_eep_read(regs, tmp_addr, &tmp);
		if (rc)
			return rc;

		memcpy(buf8, &tmp, 4);
		buf8 += 4;
	}

	if (tmp_addr < addr_end) {
		rc = mvs_eep_read(regs, tmp_addr, &tmp);
		if (rc)
			return rc;

		tmp8 = (u8 *)&tmp;
		j = addr_end - tmp_addr;
		for (i = 0; i < j; i++)
			*buf8++ = tmp8[i];

		tmp_addr += 4;
	}

	return 0;
}
#endif

static int mvs_nvram_read(struct mvs_info *mvi, u32 addr,
			  void *buf, u32 buflen)
{
#ifndef MVS_DISABLE_NVRAM
	void __iomem *regs = mvi->regs;
	int rc, i;
	u32 sum;
	u8 hdr[2], *tmp;
	const char *msg;

	rc = mvs_eep_read_buf(regs, addr, &hdr, 2);
	if (rc) {
		msg = "nvram hdr read failed";
		goto err_out;
	}
	rc = mvs_eep_read_buf(regs, addr + 2, buf, buflen);
	if (rc) {
		msg = "nvram read failed";
		goto err_out;
	}

	if (hdr[0] != 0x5A) {
		/* entry id */
		msg = "invalid nvram entry id";
		rc = -ENOENT;
		goto err_out;
	}

	tmp = buf;
	sum = ((u32)hdr[0]) + ((u32)hdr[1]);
	for (i = 0; i < buflen; i++)
		sum += ((u32)tmp[i]);

	if (sum) {
		msg = "nvram checksum failure";
		rc = -EILSEQ;
		goto err_out;
	}

	return 0;

err_out:
	dev_printk(KERN_ERR, &mvi->pdev->dev, "%s", msg);
	return rc;
#else
	/* FIXME , For SAS target mode */
	memcpy(buf, "\x50\x05\x04\x30\x11\xab\x00\x00", 8);
	return 0;
#endif
}

static void mvs_bytes_dmaed(struct mvs_info *mvi, int i)
{
	struct mvs_phy *phy = &mvi->phy[i];
	struct asd_sas_phy *sas_phy = mvi->sas.sas_phy[i];

	if (!phy->phy_attached)
		return;

	if (sas_phy->phy) {
		struct sas_phy *sphy = sas_phy->phy;

		sphy->negotiated_linkrate = sas_phy->linkrate;
		sphy->minimum_linkrate = phy->minimum_linkrate;
		sphy->minimum_linkrate_hw = SAS_LINK_RATE_1_5_GBPS;
		sphy->maximum_linkrate = phy->maximum_linkrate;
		sphy->maximum_linkrate_hw = SAS_LINK_RATE_3_0_GBPS;
	}

	if (phy->phy_type & PORT_TYPE_SAS) {
		struct sas_identify_frame *id;

		id = (struct sas_identify_frame *)phy->frame_rcvd;
		id->dev_type = phy->identify.device_type;
		id->initiator_bits = SAS_PROTOCOL_ALL;
		id->target_bits = phy->identify.target_port_protocols;
	} else if (phy->phy_type & PORT_TYPE_SATA) {
		/* TODO */
	}
	mvi->sas.sas_phy[i]->frame_rcvd_size = phy->frame_rcvd_size;
	mvi->sas.notify_port_event(mvi->sas.sas_phy[i],
				   PORTE_BYTES_DMAED);
}

static int mvs_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	/* give the phy enabling interrupt event time to come in (1s
	 * is empirically about all it takes) */
	if (time < HZ)
		return 0;
	/* Wait for discovery to finish */
	scsi_flush_work(shost);
	return 1;
}

static void mvs_scan_start(struct Scsi_Host *shost)
{
	int i;
	struct mvs_info *mvi = SHOST_TO_SAS_HA(shost)->lldd_ha;

	for (i = 0; i < mvi->chip->n_phy; ++i) {
		mvs_bytes_dmaed(mvi, i);
	}
}

static int mvs_slave_configure(struct scsi_device *sdev)
{
	struct domain_device *dev = sdev_to_domain_dev(sdev);
	int ret = sas_slave_configure(sdev);

	if (ret)
		return ret;

	if (dev_is_sata(dev)) {
		/* struct ata_port *ap = dev->sata_dev.ap; */
		/* struct ata_device *adev = ap->link.device; */

		/* clamp at no NCQ for the time being */
		/* adev->flags |= ATA_DFLAG_NCQ_OFF; */
		scsi_adjust_queue_depth(sdev, MSG_SIMPLE_TAG, 1);
	}
	return 0;
}

static void mvs_int_port(struct mvs_info *mvi, int phy_no, u32 events)
{
	struct pci_dev *pdev = mvi->pdev;
	struct sas_ha_struct *sas_ha = &mvi->sas;
	struct mvs_phy *phy = &mvi->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	phy->irq_status = mvs_read_port_irq_stat(mvi, phy_no);
	/*
	* events is port event now ,
	* we need check the interrupt status which belongs to per port.
	*/
	dev_printk(KERN_DEBUG, &pdev->dev,
		"Port %d Event = %X\n",
		phy_no, phy->irq_status);

	if (phy->irq_status & (PHYEV_POOF | PHYEV_DEC_ERR)) {
		mvs_release_task(mvi, phy_no);
		if (!mvs_is_phy_ready(mvi, phy_no)) {
			sas_phy_disconnected(sas_phy);
			sas_ha->notify_phy_event(sas_phy, PHYE_LOSS_OF_SIGNAL);
			dev_printk(KERN_INFO, &pdev->dev,
				"Port %d Unplug Notice\n", phy_no);

		} else
			mvs_phy_control(sas_phy, PHY_FUNC_LINK_RESET, NULL);
	}
	if (!(phy->irq_status & PHYEV_DEC_ERR)) {
		if (phy->irq_status & PHYEV_COMWAKE) {
			u32 tmp = mvs_read_port_irq_mask(mvi, phy_no);
			mvs_write_port_irq_mask(mvi, phy_no,
						tmp | PHYEV_SIG_FIS);
		}
		if (phy->irq_status & (PHYEV_SIG_FIS | PHYEV_ID_DONE)) {
			phy->phy_status = mvs_is_phy_ready(mvi, phy_no);
			if (phy->phy_status) {
				mvs_detect_porttype(mvi, phy_no);

				if (phy->phy_type & PORT_TYPE_SATA) {
					u32 tmp = mvs_read_port_irq_mask(mvi,
								phy_no);
					tmp &= ~PHYEV_SIG_FIS;
					mvs_write_port_irq_mask(mvi,
								phy_no, tmp);
				}

				mvs_update_phyinfo(mvi, phy_no, 0);
				sas_ha->notify_phy_event(sas_phy,
							PHYE_OOB_DONE);
				mvs_bytes_dmaed(mvi, phy_no);
			} else {
				dev_printk(KERN_DEBUG, &pdev->dev,
					"plugin interrupt but phy is gone\n");
				mvs_phy_control(sas_phy, PHY_FUNC_LINK_RESET,
							NULL);
			}
		} else if (phy->irq_status & PHYEV_BROAD_CH) {
			mvs_release_task(mvi, phy_no);
			sas_ha->notify_port_event(sas_phy,
						PORTE_BROADCAST_RCVD);
		}
	}
	mvs_write_port_irq_stat(mvi, phy_no, phy->irq_status);
}

static void mvs_int_sata(struct mvs_info *mvi)
{
	u32 tmp;
	void __iomem *regs = mvi->regs;
	tmp = mr32(INT_STAT_SRS);
	mw32(INT_STAT_SRS, tmp & 0xFFFF);
}

static void mvs_slot_reset(struct mvs_info *mvi, struct sas_task *task,
				u32 slot_idx)
{
	void __iomem *regs = mvi->regs;
	struct domain_device *dev = task->dev;
	struct asd_sas_port *sas_port = dev->port;
	struct mvs_port *port = mvi->slot_info[slot_idx].port;
	u32 reg_set, phy_mask;

	if (!sas_protocol_ata(task->task_proto)) {
		reg_set = 0;
		phy_mask = (port->wide_port_phymap) ? port->wide_port_phymap :
				sas_port->phy_mask;
	} else {
		reg_set = port->taskfileset;
		phy_mask = sas_port->phy_mask;
	}
	mvi->tx[mvi->tx_prod] = cpu_to_le32(TXQ_MODE_I | slot_idx |
					(TXQ_CMD_SLOT_RESET << TXQ_CMD_SHIFT) |
					(phy_mask << TXQ_PHY_SHIFT) |
					(reg_set << TXQ_SRS_SHIFT));

	mw32(TX_PROD_IDX, mvi->tx_prod);
	mvi->tx_prod = (mvi->tx_prod + 1) & (MVS_CHIP_SLOT_SZ - 1);
}

static int mvs_sata_done(struct mvs_info *mvi, struct sas_task *task,
			u32 slot_idx, int err)
{
	struct mvs_port *port = mvi->slot_info[slot_idx].port;
	struct task_status_struct *tstat = &task->task_status;
	struct ata_task_resp *resp = (struct ata_task_resp *)tstat->buf;
	int stat = SAM_GOOD;

	resp->frame_len = sizeof(struct dev_to_host_fis);
	memcpy(&resp->ending_fis[0],
	       SATA_RECEIVED_D2H_FIS(port->taskfileset),
	       sizeof(struct dev_to_host_fis));
	tstat->buf_valid_size = sizeof(*resp);
	if (unlikely(err))
		stat = SAS_PROTO_RESPONSE;
	return stat;
}

static void mvs_slot_free(struct mvs_info *mvi, u32 rx_desc)
{
	u32 slot_idx = rx_desc & RXQ_SLOT_MASK;
	mvs_tag_clear(mvi, slot_idx);
}

static void mvs_slot_task_free(struct mvs_info *mvi, struct sas_task *task,
			  struct mvs_slot_info *slot, u32 slot_idx)
{
	if (!sas_protocol_ata(task->task_proto))
		if (slot->n_elem)
			pci_unmap_sg(mvi->pdev, task->scatter,
				     slot->n_elem, task->data_dir);

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		pci_unmap_sg(mvi->pdev, &task->smp_task.smp_resp, 1,
			     PCI_DMA_FROMDEVICE);
		pci_unmap_sg(mvi->pdev, &task->smp_task.smp_req, 1,
			     PCI_DMA_TODEVICE);
		break;

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SSP:
	default:
		/* do nothing */
		break;
	}
	list_del(&slot->list);
	task->lldd_task = NULL;
	slot->task = NULL;
	slot->port = NULL;
}

static int mvs_slot_err(struct mvs_info *mvi, struct sas_task *task,
			 u32 slot_idx)
{
	struct mvs_slot_info *slot = &mvi->slot_info[slot_idx];
	u32 err_dw0 = le32_to_cpu(*(u32 *) (slot->response));
	u32 err_dw1 = le32_to_cpu(*(u32 *) (slot->response + 4));
	int stat = SAM_CHECK_COND;

	if (err_dw1 & SLOT_BSY_ERR) {
		stat = SAS_QUEUE_FULL;
		mvs_slot_reset(mvi, task, slot_idx);
	}
	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
		break;
	case SAS_PROTOCOL_SMP:
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
		if (err_dw0 & TFILE_ERR)
			stat = mvs_sata_done(mvi, task, slot_idx, 1);
		break;
	default:
		break;
	}

	mvs_hexdump(16, (u8 *) slot->response, 0);
	return stat;
}

static int mvs_slot_complete(struct mvs_info *mvi, u32 rx_desc, u32 flags)
{
	u32 slot_idx = rx_desc & RXQ_SLOT_MASK;
	struct mvs_slot_info *slot = &mvi->slot_info[slot_idx];
	struct sas_task *task = slot->task;
	struct task_status_struct *tstat;
	struct mvs_port *port;
	bool aborted;
	void *to;

	if (unlikely(!task || !task->lldd_task))
		return -1;

	mvs_hba_cq_dump(mvi);

	spin_lock(&task->task_state_lock);
	aborted = task->task_state_flags & SAS_TASK_STATE_ABORTED;
	if (!aborted) {
		task->task_state_flags &=
		    ~(SAS_TASK_STATE_PENDING | SAS_TASK_AT_INITIATOR);
		task->task_state_flags |= SAS_TASK_STATE_DONE;
	}
	spin_unlock(&task->task_state_lock);

	if (aborted) {
		mvs_slot_task_free(mvi, task, slot, slot_idx);
		mvs_slot_free(mvi, rx_desc);
		return -1;
	}

	port = slot->port;
	tstat = &task->task_status;
	memset(tstat, 0, sizeof(*tstat));
	tstat->resp = SAS_TASK_COMPLETE;

	if (unlikely(!port->port_attached || flags)) {
		mvs_slot_err(mvi, task, slot_idx);
		if (!sas_protocol_ata(task->task_proto))
			tstat->stat = SAS_PHY_DOWN;
		goto out;
	}

	/* error info record present */
	if (unlikely((rx_desc & RXQ_ERR) && (*(u64 *) slot->response))) {
		tstat->stat = mvs_slot_err(mvi, task, slot_idx);
		goto out;
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
		/* hw says status == 0, datapres == 0 */
		if (rx_desc & RXQ_GOOD) {
			tstat->stat = SAM_GOOD;
			tstat->resp = SAS_TASK_COMPLETE;
		}
		/* response frame present */
		else if (rx_desc & RXQ_RSP) {
			struct ssp_response_iu *iu =
			    slot->response + sizeof(struct mvs_err_info);
			sas_ssp_task_response(&mvi->pdev->dev, task, iu);
		}

		/* should never happen? */
		else
			tstat->stat = SAM_CHECK_COND;
		break;

	case SAS_PROTOCOL_SMP: {
			struct scatterlist *sg_resp = &task->smp_task.smp_resp;
			tstat->stat = SAM_GOOD;
			to = kmap_atomic(sg_page(sg_resp), KM_IRQ0);
			memcpy(to + sg_resp->offset,
				slot->response + sizeof(struct mvs_err_info),
				sg_dma_len(sg_resp));
			kunmap_atomic(to, KM_IRQ0);
			break;
		}

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP: {
			tstat->stat = mvs_sata_done(mvi, task, slot_idx, 0);
			break;
		}

	default:
		tstat->stat = SAM_CHECK_COND;
		break;
	}

out:
	mvs_slot_task_free(mvi, task, slot, slot_idx);
	if (unlikely(tstat->stat != SAS_QUEUE_FULL))
		mvs_slot_free(mvi, rx_desc);

	spin_unlock(&mvi->lock);
	task->task_done(task);
	spin_lock(&mvi->lock);
	return tstat->stat;
}

static void mvs_release_task(struct mvs_info *mvi, int phy_no)
{
	struct list_head *pos, *n;
	struct mvs_slot_info *slot;
	struct mvs_phy *phy = &mvi->phy[phy_no];
	struct mvs_port *port = phy->port;
	u32 rx_desc;

	if (!port)
		return;

	list_for_each_safe(pos, n, &port->list) {
		slot = container_of(pos, struct mvs_slot_info, list);
		rx_desc = (u32) (slot - mvi->slot_info);
		mvs_slot_complete(mvi, rx_desc, 1);
	}
}

static void mvs_int_full(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp, stat;
	int i;

	stat = mr32(INT_STAT);

	mvs_int_rx(mvi, false);

	for (i = 0; i < MVS_MAX_PORTS; i++) {
		tmp = (stat >> i) & (CINT_PORT | CINT_PORT_STOPPED);
		if (tmp)
			mvs_int_port(mvi, i, tmp);
	}

	if (stat & CINT_SRS)
		mvs_int_sata(mvi);

	mw32(INT_STAT, stat);
}

static int mvs_int_rx(struct mvs_info *mvi, bool self_clear)
{
	void __iomem *regs = mvi->regs;
	u32 rx_prod_idx, rx_desc;
	bool attn = false;
	struct pci_dev *pdev = mvi->pdev;

	/* the first dword in the RX ring is special: it contains
	 * a mirror of the hardware's RX producer index, so that
	 * we don't have to stall the CPU reading that register.
	 * The actual RX ring is offset by one dword, due to this.
	 */
	rx_prod_idx = mvi->rx_cons;
	mvi->rx_cons = le32_to_cpu(mvi->rx[0]);
	if (mvi->rx_cons == 0xfff)	/* h/w hasn't touched RX ring yet */
		return 0;

	/* The CMPL_Q may come late, read from register and try again
	* note: if coalescing is enabled,
	* it will need to read from register every time for sure
	*/
	if (mvi->rx_cons == rx_prod_idx)
		mvi->rx_cons = mr32(RX_CONS_IDX) & RX_RING_SZ_MASK;

	if (mvi->rx_cons == rx_prod_idx)
		return 0;

	while (mvi->rx_cons != rx_prod_idx) {

		/* increment our internal RX consumer pointer */
		rx_prod_idx = (rx_prod_idx + 1) & (MVS_RX_RING_SZ - 1);

		rx_desc = le32_to_cpu(mvi->rx[rx_prod_idx + 1]);

		if (likely(rx_desc & RXQ_DONE))
			mvs_slot_complete(mvi, rx_desc, 0);
		if (rx_desc & RXQ_ATTN) {
			attn = true;
			dev_printk(KERN_DEBUG, &pdev->dev, "ATTN %X\n",
				rx_desc);
		} else if (rx_desc & RXQ_ERR) {
			if (!(rx_desc & RXQ_DONE))
				mvs_slot_complete(mvi, rx_desc, 0);
			dev_printk(KERN_DEBUG, &pdev->dev, "RXQ_ERR %X\n",
				rx_desc);
		} else if (rx_desc & RXQ_SLOT_RESET) {
			dev_printk(KERN_DEBUG, &pdev->dev, "Slot reset[%X]\n",
				rx_desc);
			mvs_slot_free(mvi, rx_desc);
		}
	}

	if (attn && self_clear)
		mvs_int_full(mvi);

	return 0;
}

#ifdef MVS_USE_TASKLET
static void mvs_tasklet(unsigned long data)
{
	struct mvs_info *mvi = (struct mvs_info *) data;
	unsigned long flags;

	spin_lock_irqsave(&mvi->lock, flags);

#ifdef MVS_DISABLE_MSI
	mvs_int_full(mvi);
#else
	mvs_int_rx(mvi, true);
#endif
	spin_unlock_irqrestore(&mvi->lock, flags);
}
#endif

static irqreturn_t mvs_interrupt(int irq, void *opaque)
{
	struct mvs_info *mvi = opaque;
	void __iomem *regs = mvi->regs;
	u32 stat;

	stat = mr32(GBL_INT_STAT);

	if (stat == 0 || stat == 0xffffffff)
		return IRQ_NONE;

	/* clear CMD_CMPLT ASAP */
	mw32_f(INT_STAT, CINT_DONE);

#ifndef MVS_USE_TASKLET
	spin_lock(&mvi->lock);

	mvs_int_full(mvi);

	spin_unlock(&mvi->lock);
#else
	tasklet_schedule(&mvi->tasklet);
#endif
	return IRQ_HANDLED;
}

#ifndef MVS_DISABLE_MSI
static irqreturn_t mvs_msi_interrupt(int irq, void *opaque)
{
	struct mvs_info *mvi = opaque;

#ifndef MVS_USE_TASKLET
	spin_lock(&mvi->lock);

	mvs_int_rx(mvi, true);

	spin_unlock(&mvi->lock);
#else
	tasklet_schedule(&mvi->tasklet);
#endif
	return IRQ_HANDLED;
}
#endif

struct mvs_task_exec_info {
	struct sas_task *task;
	struct mvs_cmd_hdr *hdr;
	struct mvs_port *port;
	u32 tag;
	int n_elem;
};

static int mvs_task_prep_smp(struct mvs_info *mvi,
			     struct mvs_task_exec_info *tei)
{
	int elem, rc, i;
	struct sas_task *task = tei->task;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct scatterlist *sg_req, *sg_resp;
	u32 req_len, resp_len, tag = tei->tag;
	void *buf_tmp;
	u8 *buf_oaf;
	dma_addr_t buf_tmp_dma;
	struct mvs_prd *buf_prd;
	struct scatterlist *sg;
	struct mvs_slot_info *slot = &mvi->slot_info[tag];
	struct asd_sas_port *sas_port = task->dev->port;
	u32 flags = (tei->n_elem << MCH_PRD_LEN_SHIFT);
#if _MV_DUMP
	u8 *buf_cmd;
	void *from;
#endif
	/*
	 * DMA-map SMP request, response buffers
	 */
	sg_req = &task->smp_task.smp_req;
	elem = pci_map_sg(mvi->pdev, sg_req, 1, PCI_DMA_TODEVICE);
	if (!elem)
		return -ENOMEM;
	req_len = sg_dma_len(sg_req);

	sg_resp = &task->smp_task.smp_resp;
	elem = pci_map_sg(mvi->pdev, sg_resp, 1, PCI_DMA_FROMDEVICE);
	if (!elem) {
		rc = -ENOMEM;
		goto err_out;
	}
	resp_len = sg_dma_len(sg_resp);

	/* must be in dwords */
	if ((req_len & 0x3) || (resp_len & 0x3)) {
		rc = -EINVAL;
		goto err_out_2;
	}

	/*
	 * arrange MVS_SLOT_BUF_SZ-sized DMA buffer according to our needs
	 */

	/* region 1: command table area (MVS_SSP_CMD_SZ bytes) ************** */
	buf_tmp = slot->buf;
	buf_tmp_dma = slot->buf_dma;

#if _MV_DUMP
	buf_cmd = buf_tmp;
	hdr->cmd_tbl = cpu_to_le64(buf_tmp_dma);
	buf_tmp += req_len;
	buf_tmp_dma += req_len;
	slot->cmd_size = req_len;
#else
	hdr->cmd_tbl = cpu_to_le64(sg_dma_address(sg_req));
#endif

	/* region 2: open address frame area (MVS_OAF_SZ bytes) ********* */
	buf_oaf = buf_tmp;
	hdr->open_frame = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_OAF_SZ;
	buf_tmp_dma += MVS_OAF_SZ;

	/* region 3: PRD table ********************************************* */
	buf_prd = buf_tmp;
	if (tei->n_elem)
		hdr->prd_tbl = cpu_to_le64(buf_tmp_dma);
	else
		hdr->prd_tbl = 0;

	i = sizeof(struct mvs_prd) * tei->n_elem;
	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ****** */
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);

	/*
	 * Fill in TX ring and command slot header
	 */
	slot->tx = mvi->tx_prod;
	mvi->tx[mvi->tx_prod] = cpu_to_le32((TXQ_CMD_SMP << TXQ_CMD_SHIFT) |
					TXQ_MODE_I | tag |
					(sas_port->phy_mask << TXQ_PHY_SHIFT));

	hdr->flags |= flags;
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | ((req_len - 4) / 4));
	hdr->tags = cpu_to_le32(tag);
	hdr->data_len = 0;

	/* generate open address frame hdr (first 12 bytes) */
	buf_oaf[0] = (1 << 7) | (0 << 4) | 0x01; /* initiator, SMP, ftype 1h */
	buf_oaf[1] = task->dev->linkrate & 0xf;
	*(u16 *)(buf_oaf + 2) = 0xFFFF;		/* SAS SPEC */
	memcpy(buf_oaf + 4, task->dev->sas_addr, SAS_ADDR_SIZE);

	/* fill in PRD (scatter/gather) table, if any */
	for_each_sg(task->scatter, sg, tei->n_elem, i) {
		buf_prd->addr = cpu_to_le64(sg_dma_address(sg));
		buf_prd->len = cpu_to_le32(sg_dma_len(sg));
		buf_prd++;
	}

#if _MV_DUMP
	/* copy cmd table */
	from = kmap_atomic(sg_page(sg_req), KM_IRQ0);
	memcpy(buf_cmd, from + sg_req->offset, req_len);
	kunmap_atomic(from, KM_IRQ0);
#endif
	return 0;

err_out_2:
	pci_unmap_sg(mvi->pdev, &tei->task->smp_task.smp_resp, 1,
		     PCI_DMA_FROMDEVICE);
err_out:
	pci_unmap_sg(mvi->pdev, &tei->task->smp_task.smp_req, 1,
		     PCI_DMA_TODEVICE);
	return rc;
}

static void mvs_free_reg_set(struct mvs_info *mvi, struct mvs_port *port)
{
	void __iomem *regs = mvi->regs;
	u32 tmp, offs;
	u8 *tfs = &port->taskfileset;

	if (*tfs == MVS_ID_NOT_MAPPED)
		return;

	offs = 1U << ((*tfs & 0x0f) + PCS_EN_SATA_REG_SHIFT);
	if (*tfs < 16) {
		tmp = mr32(PCS);
		mw32(PCS, tmp & ~offs);
	} else {
		tmp = mr32(CTL);
		mw32(CTL, tmp & ~offs);
	}

	tmp = mr32(INT_STAT_SRS) & (1U << *tfs);
	if (tmp)
		mw32(INT_STAT_SRS, tmp);

	*tfs = MVS_ID_NOT_MAPPED;
}

static u8 mvs_assign_reg_set(struct mvs_info *mvi, struct mvs_port *port)
{
	int i;
	u32 tmp, offs;
	void __iomem *regs = mvi->regs;

	if (port->taskfileset != MVS_ID_NOT_MAPPED)
		return 0;

	tmp = mr32(PCS);

	for (i = 0; i < mvi->chip->srs_sz; i++) {
		if (i == 16)
			tmp = mr32(CTL);
		offs = 1U << ((i & 0x0f) + PCS_EN_SATA_REG_SHIFT);
		if (!(tmp & offs)) {
			port->taskfileset = i;

			if (i < 16)
				mw32(PCS, tmp | offs);
			else
				mw32(CTL, tmp | offs);
			tmp = mr32(INT_STAT_SRS) & (1U << i);
			if (tmp)
				mw32(INT_STAT_SRS, tmp);
			return 0;
		}
	}
	return MVS_ID_NOT_MAPPED;
}

static u32 mvs_get_ncq_tag(struct sas_task *task, u32 *tag)
{
	struct ata_queued_cmd *qc = task->uldd_task;

	if (qc) {
		if (qc->tf.command == ATA_CMD_FPDMA_WRITE ||
			qc->tf.command == ATA_CMD_FPDMA_READ) {
			*tag = qc->tag;
			return 1;
		}
	}

	return 0;
}

static int mvs_task_prep_ata(struct mvs_info *mvi,
			     struct mvs_task_exec_info *tei)
{
	struct sas_task *task = tei->task;
	struct domain_device *dev = task->dev;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct asd_sas_port *sas_port = dev->port;
	struct mvs_slot_info *slot;
	struct scatterlist *sg;
	struct mvs_prd *buf_prd;
	struct mvs_port *port = tei->port;
	u32 tag = tei->tag;
	u32 flags = (tei->n_elem << MCH_PRD_LEN_SHIFT);
	void *buf_tmp;
	u8 *buf_cmd, *buf_oaf;
	dma_addr_t buf_tmp_dma;
	u32 i, req_len, resp_len;
	const u32 max_resp_len = SB_RFB_MAX;

	if (mvs_assign_reg_set(mvi, port) == MVS_ID_NOT_MAPPED)
		return -EBUSY;

	slot = &mvi->slot_info[tag];
	slot->tx = mvi->tx_prod;
	mvi->tx[mvi->tx_prod] = cpu_to_le32(TXQ_MODE_I | tag |
					(TXQ_CMD_STP << TXQ_CMD_SHIFT) |
					(sas_port->phy_mask << TXQ_PHY_SHIFT) |
					(port->taskfileset << TXQ_SRS_SHIFT));

	if (task->ata_task.use_ncq)
		flags |= MCH_FPDMA;
	if (dev->sata_dev.command_set == ATAPI_COMMAND_SET) {
		if (task->ata_task.fis.command != ATA_CMD_ID_ATAPI)
			flags |= MCH_ATAPI;
	}

	/* FIXME: fill in port multiplier number */

	hdr->flags = cpu_to_le32(flags);

	/* FIXME: the low order order 5 bits for the TAG if enable NCQ */
	if (task->ata_task.use_ncq && mvs_get_ncq_tag(task, &hdr->tags))
		task->ata_task.fis.sector_count |= hdr->tags << 3;
	else
		hdr->tags = cpu_to_le32(tag);
	hdr->data_len = cpu_to_le32(task->total_xfer_len);

	/*
	 * arrange MVS_SLOT_BUF_SZ-sized DMA buffer according to our needs
	 */

	/* region 1: command table area (MVS_ATA_CMD_SZ bytes) ************** */
	buf_cmd = buf_tmp = slot->buf;
	buf_tmp_dma = slot->buf_dma;

	hdr->cmd_tbl = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_ATA_CMD_SZ;
	buf_tmp_dma += MVS_ATA_CMD_SZ;
#if _MV_DUMP
	slot->cmd_size = MVS_ATA_CMD_SZ;
#endif

	/* region 2: open address frame area (MVS_OAF_SZ bytes) ********* */
	/* used for STP.  unused for SATA? */
	buf_oaf = buf_tmp;
	hdr->open_frame = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_OAF_SZ;
	buf_tmp_dma += MVS_OAF_SZ;

	/* region 3: PRD table ********************************************* */
	buf_prd = buf_tmp;
	if (tei->n_elem)
		hdr->prd_tbl = cpu_to_le64(buf_tmp_dma);
	else
		hdr->prd_tbl = 0;

	i = sizeof(struct mvs_prd) * tei->n_elem;
	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ****** */
	/* FIXME: probably unused, for SATA.  kept here just in case
	 * we get a STP/SATA error information record
	 */
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);

	req_len = sizeof(struct host_to_dev_fis);
	resp_len = MVS_SLOT_BUF_SZ - MVS_ATA_CMD_SZ -
	    sizeof(struct mvs_err_info) - i;

	/* request, response lengths */
	resp_len = min(resp_len, max_resp_len);
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | (req_len / 4));

	task->ata_task.fis.flags |= 0x80; /* C=1: update ATA cmd reg */
	/* fill in command FIS and ATAPI CDB */
	memcpy(buf_cmd, &task->ata_task.fis, sizeof(struct host_to_dev_fis));
	if (dev->sata_dev.command_set == ATAPI_COMMAND_SET)
		memcpy(buf_cmd + STP_ATAPI_CMD,
			task->ata_task.atapi_packet, 16);

	/* generate open address frame hdr (first 12 bytes) */
	buf_oaf[0] = (1 << 7) | (2 << 4) | 0x1;	/* initiator, STP, ftype 1h */
	buf_oaf[1] = task->dev->linkrate & 0xf;
	*(u16 *)(buf_oaf + 2) = cpu_to_be16(tag);
	memcpy(buf_oaf + 4, task->dev->sas_addr, SAS_ADDR_SIZE);

	/* fill in PRD (scatter/gather) table, if any */
	for_each_sg(task->scatter, sg, tei->n_elem, i) {
		buf_prd->addr = cpu_to_le64(sg_dma_address(sg));
		buf_prd->len = cpu_to_le32(sg_dma_len(sg));
		buf_prd++;
	}

	return 0;
}

static int mvs_task_prep_ssp(struct mvs_info *mvi,
			     struct mvs_task_exec_info *tei)
{
	struct sas_task *task = tei->task;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct mvs_port *port = tei->port;
	struct mvs_slot_info *slot;
	struct scatterlist *sg;
	struct mvs_prd *buf_prd;
	struct ssp_frame_hdr *ssp_hdr;
	void *buf_tmp;
	u8 *buf_cmd, *buf_oaf, fburst = 0;
	dma_addr_t buf_tmp_dma;
	u32 flags;
	u32 resp_len, req_len, i, tag = tei->tag;
	const u32 max_resp_len = SB_RFB_MAX;
	u8 phy_mask;

	slot = &mvi->slot_info[tag];

	phy_mask = (port->wide_port_phymap) ? port->wide_port_phymap :
		task->dev->port->phy_mask;
	slot->tx = mvi->tx_prod;
	mvi->tx[mvi->tx_prod] = cpu_to_le32(TXQ_MODE_I | tag |
				(TXQ_CMD_SSP << TXQ_CMD_SHIFT) |
				(phy_mask << TXQ_PHY_SHIFT));

	flags = MCH_RETRY;
	if (task->ssp_task.enable_first_burst) {
		flags |= MCH_FBURST;
		fburst = (1 << 7);
	}
	hdr->flags = cpu_to_le32(flags |
				 (tei->n_elem << MCH_PRD_LEN_SHIFT) |
				 (MCH_SSP_FR_CMD << MCH_SSP_FR_TYPE_SHIFT));

	hdr->tags = cpu_to_le32(tag);
	hdr->data_len = cpu_to_le32(task->total_xfer_len);

	/*
	 * arrange MVS_SLOT_BUF_SZ-sized DMA buffer according to our needs
	 */

	/* region 1: command table area (MVS_SSP_CMD_SZ bytes) ************** */
	buf_cmd = buf_tmp = slot->buf;
	buf_tmp_dma = slot->buf_dma;

	hdr->cmd_tbl = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_SSP_CMD_SZ;
	buf_tmp_dma += MVS_SSP_CMD_SZ;
#if _MV_DUMP
	slot->cmd_size = MVS_SSP_CMD_SZ;
#endif

	/* region 2: open address frame area (MVS_OAF_SZ bytes) ********* */
	buf_oaf = buf_tmp;
	hdr->open_frame = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_OAF_SZ;
	buf_tmp_dma += MVS_OAF_SZ;

	/* region 3: PRD table ********************************************* */
	buf_prd = buf_tmp;
	if (tei->n_elem)
		hdr->prd_tbl = cpu_to_le64(buf_tmp_dma);
	else
		hdr->prd_tbl = 0;

	i = sizeof(struct mvs_prd) * tei->n_elem;
	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ****** */
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);

	resp_len = MVS_SLOT_BUF_SZ - MVS_SSP_CMD_SZ - MVS_OAF_SZ -
	    sizeof(struct mvs_err_info) - i;
	resp_len = min(resp_len, max_resp_len);

	req_len = sizeof(struct ssp_frame_hdr) + 28;

	/* request, response lengths */
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | (req_len / 4));

	/* generate open address frame hdr (first 12 bytes) */
	buf_oaf[0] = (1 << 7) | (1 << 4) | 0x1;	/* initiator, SSP, ftype 1h */
	buf_oaf[1] = task->dev->linkrate & 0xf;
	*(u16 *)(buf_oaf + 2) = cpu_to_be16(tag);
	memcpy(buf_oaf + 4, task->dev->sas_addr, SAS_ADDR_SIZE);

	/* fill in SSP frame header (Command Table.SSP frame header) */
	ssp_hdr = (struct ssp_frame_hdr *)buf_cmd;
	ssp_hdr->frame_type = SSP_COMMAND;
	memcpy(ssp_hdr->hashed_dest_addr, task->dev->hashed_sas_addr,
	       HASHED_SAS_ADDR_SIZE);
	memcpy(ssp_hdr->hashed_src_addr,
	       task->dev->port->ha->hashed_sas_addr, HASHED_SAS_ADDR_SIZE);
	ssp_hdr->tag = cpu_to_be16(tag);

	/* fill in command frame IU */
	buf_cmd += sizeof(*ssp_hdr);
	memcpy(buf_cmd, &task->ssp_task.LUN, 8);
	buf_cmd[9] = fburst | task->ssp_task.task_attr |
			(task->ssp_task.task_prio << 3);
	memcpy(buf_cmd + 12, &task->ssp_task.cdb, 16);

	/* fill in PRD (scatter/gather) table, if any */
	for_each_sg(task->scatter, sg, tei->n_elem, i) {
		buf_prd->addr = cpu_to_le64(sg_dma_address(sg));
		buf_prd->len = cpu_to_le32(sg_dma_len(sg));
		buf_prd++;
	}

	return 0;
}

static int mvs_task_exec(struct sas_task *task, const int num, gfp_t gfp_flags)
{
	struct domain_device *dev = task->dev;
	struct mvs_info *mvi = dev->port->ha->lldd_ha;
	struct pci_dev *pdev = mvi->pdev;
	void __iomem *regs = mvi->regs;
	struct mvs_task_exec_info tei;
	struct sas_task *t = task;
	struct mvs_slot_info *slot;
	u32 tag = 0xdeadbeef, rc, n_elem = 0;
	unsigned long flags;
	u32 n = num, pass = 0;

	spin_lock_irqsave(&mvi->lock, flags);
	do {
		dev = t->dev;
		tei.port = &mvi->port[dev->port->id];

		if (!tei.port->port_attached) {
			if (sas_protocol_ata(t->task_proto)) {
				rc = SAS_PHY_DOWN;
				goto out_done;
			} else {
				struct task_status_struct *ts = &t->task_status;
				ts->resp = SAS_TASK_UNDELIVERED;
				ts->stat = SAS_PHY_DOWN;
				t->task_done(t);
				if (n > 1)
					t = list_entry(t->list.next,
							struct sas_task, list);
				continue;
			}
		}

		if (!sas_protocol_ata(t->task_proto)) {
			if (t->num_scatter) {
				n_elem = pci_map_sg(mvi->pdev, t->scatter,
						    t->num_scatter,
						    t->data_dir);
				if (!n_elem) {
					rc = -ENOMEM;
					goto err_out;
				}
			}
		} else {
			n_elem = t->num_scatter;
		}

		rc = mvs_tag_alloc(mvi, &tag);
		if (rc)
			goto err_out;

		slot = &mvi->slot_info[tag];
		t->lldd_task = NULL;
		slot->n_elem = n_elem;
		memset(slot->buf, 0, MVS_SLOT_BUF_SZ);
		tei.task = t;
		tei.hdr = &mvi->slot[tag];
		tei.tag = tag;
		tei.n_elem = n_elem;

		switch (t->task_proto) {
		case SAS_PROTOCOL_SMP:
			rc = mvs_task_prep_smp(mvi, &tei);
			break;
		case SAS_PROTOCOL_SSP:
			rc = mvs_task_prep_ssp(mvi, &tei);
			break;
		case SAS_PROTOCOL_SATA:
		case SAS_PROTOCOL_STP:
		case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
			rc = mvs_task_prep_ata(mvi, &tei);
			break;
		default:
			dev_printk(KERN_ERR, &pdev->dev,
				"unknown sas_task proto: 0x%x\n",
				t->task_proto);
			rc = -EINVAL;
			break;
		}

		if (rc)
			goto err_out_tag;

		slot->task = t;
		slot->port = tei.port;
		t->lldd_task = (void *) slot;
		list_add_tail(&slot->list, &slot->port->list);
		/* TODO: select normal or high priority */

		spin_lock(&t->task_state_lock);
		t->task_state_flags |= SAS_TASK_AT_INITIATOR;
		spin_unlock(&t->task_state_lock);

		mvs_hba_memory_dump(mvi, tag, t->task_proto);

		++pass;
		mvi->tx_prod = (mvi->tx_prod + 1) & (MVS_CHIP_SLOT_SZ - 1);
		if (n > 1)
			t = list_entry(t->list.next, struct sas_task, list);
	} while (--n);

	rc = 0;
	goto out_done;

err_out_tag:
	mvs_tag_free(mvi, tag);
err_out:
	dev_printk(KERN_ERR, &pdev->dev, "mvsas exec failed[%d]!\n", rc);
	if (!sas_protocol_ata(t->task_proto))
		if (n_elem)
			pci_unmap_sg(mvi->pdev, t->scatter, n_elem,
				     t->data_dir);
out_done:
	if (pass)
		mw32(TX_PROD_IDX, (mvi->tx_prod - 1) & (MVS_CHIP_SLOT_SZ - 1));
	spin_unlock_irqrestore(&mvi->lock, flags);
	return rc;
}

static int mvs_task_abort(struct sas_task *task)
{
	int rc;
	unsigned long flags;
	struct mvs_info *mvi = task->dev->port->ha->lldd_ha;
	struct pci_dev *pdev = mvi->pdev;
	int tag;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_DONE) {
		rc = TMF_RESP_FUNC_COMPLETE;
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		goto out_done;
	}
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		dev_printk(KERN_DEBUG, &pdev->dev, "SMP Abort! \n");
		break;
	case SAS_PROTOCOL_SSP:
		dev_printk(KERN_DEBUG, &pdev->dev, "SSP Abort! \n");
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:{
		dev_printk(KERN_DEBUG, &pdev->dev, "STP Abort! \n");
#if _MV_DUMP
		dev_printk(KERN_DEBUG, &pdev->dev, "Dump D2H FIS: \n");
		mvs_hexdump(sizeof(struct host_to_dev_fis),
				(void *)&task->ata_task.fis, 0);
		dev_printk(KERN_DEBUG, &pdev->dev, "Dump ATAPI Cmd : \n");
		mvs_hexdump(16, task->ata_task.atapi_packet, 0);
#endif
		spin_lock_irqsave(&task->task_state_lock, flags);
		if (task->task_state_flags & SAS_TASK_NEED_DEV_RESET) {
			/* TODO */
			;
		}
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		break;
	}
	default:
		break;
	}

	if (mvs_find_tag(mvi, task, &tag)) {
		spin_lock_irqsave(&mvi->lock, flags);
		mvs_slot_task_free(mvi, task, &mvi->slot_info[tag], tag);
		spin_unlock_irqrestore(&mvi->lock, flags);
	}
	if (!mvs_task_exec(task, 1, GFP_ATOMIC))
		rc = TMF_RESP_FUNC_COMPLETE;
	else
		rc = TMF_RESP_FUNC_FAILED;
out_done:
	return rc;
}

static void mvs_free(struct mvs_info *mvi)
{
	int i;

	if (!mvi)
		return;

	for (i = 0; i < MVS_SLOTS; i++) {
		struct mvs_slot_info *slot = &mvi->slot_info[i];

		if (slot->buf)
			dma_free_coherent(&mvi->pdev->dev, MVS_SLOT_BUF_SZ,
					  slot->buf, slot->buf_dma);
	}

	if (mvi->tx)
		dma_free_coherent(&mvi->pdev->dev,
				  sizeof(*mvi->tx) * MVS_CHIP_SLOT_SZ,
				  mvi->tx, mvi->tx_dma);
	if (mvi->rx_fis)
		dma_free_coherent(&mvi->pdev->dev, MVS_RX_FISL_SZ,
				  mvi->rx_fis, mvi->rx_fis_dma);
	if (mvi->rx)
		dma_free_coherent(&mvi->pdev->dev,
				  sizeof(*mvi->rx) * (MVS_RX_RING_SZ + 1),
				  mvi->rx, mvi->rx_dma);
	if (mvi->slot)
		dma_free_coherent(&mvi->pdev->dev,
				  sizeof(*mvi->slot) * MVS_SLOTS,
				  mvi->slot, mvi->slot_dma);
#ifdef MVS_ENABLE_PERI
	if (mvi->peri_regs)
		iounmap(mvi->peri_regs);
#endif
	if (mvi->regs)
		iounmap(mvi->regs);
	if (mvi->shost)
		scsi_host_put(mvi->shost);
	kfree(mvi->sas.sas_port);
	kfree(mvi->sas.sas_phy);
	kfree(mvi);
}

/* FIXME: locking? */
static int mvs_phy_control(struct asd_sas_phy *sas_phy, enum phy_func func,
			   void *funcdata)
{
	struct mvs_info *mvi = sas_phy->ha->lldd_ha;
	int rc = 0, phy_id = sas_phy->id;
	u32 tmp;

	tmp = mvs_read_phy_ctl(mvi, phy_id);

	switch (func) {
	case PHY_FUNC_SET_LINK_RATE:{
			struct sas_phy_linkrates *rates = funcdata;
			u32 lrmin = 0, lrmax = 0;

			lrmin = (rates->minimum_linkrate << 8);
			lrmax = (rates->maximum_linkrate << 12);

			if (lrmin) {
				tmp &= ~(0xf << 8);
				tmp |= lrmin;
			}
			if (lrmax) {
				tmp &= ~(0xf << 12);
				tmp |= lrmax;
			}
			mvs_write_phy_ctl(mvi, phy_id, tmp);
			break;
		}

	case PHY_FUNC_HARD_RESET:
		if (tmp & PHY_RST_HARD)
			break;
		mvs_write_phy_ctl(mvi, phy_id, tmp | PHY_RST_HARD);
		break;

	case PHY_FUNC_LINK_RESET:
		mvs_write_phy_ctl(mvi, phy_id, tmp | PHY_RST);
		break;

	case PHY_FUNC_DISABLE:
	case PHY_FUNC_RELEASE_SPINUP_HOLD:
	default:
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static void __devinit mvs_phy_init(struct mvs_info *mvi, int phy_id)
{
	struct mvs_phy *phy = &mvi->phy[phy_id];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	sas_phy->enabled = (phy_id < mvi->chip->n_phy) ? 1 : 0;
	sas_phy->class = SAS;
	sas_phy->iproto = SAS_PROTOCOL_ALL;
	sas_phy->tproto = 0;
	sas_phy->type = PHY_TYPE_PHYSICAL;
	sas_phy->role = PHY_ROLE_INITIATOR;
	sas_phy->oob_mode = OOB_NOT_CONNECTED;
	sas_phy->linkrate = SAS_LINK_RATE_UNKNOWN;

	sas_phy->id = phy_id;
	sas_phy->sas_addr = &mvi->sas_addr[0];
	sas_phy->frame_rcvd = &phy->frame_rcvd[0];
	sas_phy->ha = &mvi->sas;
	sas_phy->lldd_phy = phy;
}

static struct mvs_info *__devinit mvs_alloc(struct pci_dev *pdev,
					    const struct pci_device_id *ent)
{
	struct mvs_info *mvi;
	unsigned long res_start, res_len, res_flag;
	struct asd_sas_phy **arr_phy;
	struct asd_sas_port **arr_port;
	const struct mvs_chip_info *chip = &mvs_chips[ent->driver_data];
	int i;

	/*
	 * alloc and init our per-HBA mvs_info struct
	 */

	mvi = kzalloc(sizeof(*mvi), GFP_KERNEL);
	if (!mvi)
		return NULL;

	spin_lock_init(&mvi->lock);
#ifdef MVS_USE_TASKLET
	tasklet_init(&mvi->tasklet, mvs_tasklet, (unsigned long)mvi);
#endif
	mvi->pdev = pdev;
	mvi->chip = chip;

	if (pdev->device == 0x6440 && pdev->revision == 0)
		mvi->flags |= MVF_PHY_PWR_FIX;

	/*
	 * alloc and init SCSI, SAS glue
	 */

	mvi->shost = scsi_host_alloc(&mvs_sht, sizeof(void *));
	if (!mvi->shost)
		goto err_out;

	arr_phy = kcalloc(MVS_MAX_PHYS, sizeof(void *), GFP_KERNEL);
	arr_port = kcalloc(MVS_MAX_PHYS, sizeof(void *), GFP_KERNEL);
	if (!arr_phy || !arr_port)
		goto err_out;

	for (i = 0; i < MVS_MAX_PHYS; i++) {
		mvs_phy_init(mvi, i);
		arr_phy[i] = &mvi->phy[i].sas_phy;
		arr_port[i] = &mvi->port[i].sas_port;
		mvi->port[i].taskfileset = MVS_ID_NOT_MAPPED;
		mvi->port[i].wide_port_phymap = 0;
		mvi->port[i].port_attached = 0;
		INIT_LIST_HEAD(&mvi->port[i].list);
	}

	SHOST_TO_SAS_HA(mvi->shost) = &mvi->sas;
	mvi->shost->transportt = mvs_stt;
	mvi->shost->max_id = 21;
	mvi->shost->max_lun = ~0;
	mvi->shost->max_channel = 0;
	mvi->shost->max_cmd_len = 16;

	mvi->sas.sas_ha_name = DRV_NAME;
	mvi->sas.dev = &pdev->dev;
	mvi->sas.lldd_module = THIS_MODULE;
	mvi->sas.sas_addr = &mvi->sas_addr[0];
	mvi->sas.sas_phy = arr_phy;
	mvi->sas.sas_port = arr_port;
	mvi->sas.num_phys = chip->n_phy;
	mvi->sas.lldd_max_execute_num = 1;
	mvi->sas.lldd_queue_size = MVS_QUEUE_SIZE;
	mvi->shost->can_queue = MVS_CAN_QUEUE;
	mvi->shost->cmd_per_lun = MVS_SLOTS / mvi->sas.num_phys;
	mvi->sas.lldd_ha = mvi;
	mvi->sas.core.shost = mvi->shost;

	mvs_tag_init(mvi);

	/*
	 * ioremap main and peripheral registers
	 */

#ifdef MVS_ENABLE_PERI
	res_start = pci_resource_start(pdev, 2);
	res_len = pci_resource_len(pdev, 2);
	if (!res_start || !res_len)
		goto err_out;

	mvi->peri_regs = ioremap_nocache(res_start, res_len);
	if (!mvi->peri_regs)
		goto err_out;
#endif

	res_start = pci_resource_start(pdev, 4);
	res_len = pci_resource_len(pdev, 4);
	if (!res_start || !res_len)
		goto err_out;

	res_flag = pci_resource_flags(pdev, 4);
	if (res_flag & IORESOURCE_CACHEABLE)
		mvi->regs = ioremap(res_start, res_len);
	else
		mvi->regs = ioremap_nocache(res_start, res_len);

	if (!mvi->regs)
		goto err_out;

	/*
	 * alloc and init our DMA areas
	 */

	mvi->tx = dma_alloc_coherent(&pdev->dev,
				     sizeof(*mvi->tx) * MVS_CHIP_SLOT_SZ,
				     &mvi->tx_dma, GFP_KERNEL);
	if (!mvi->tx)
		goto err_out;
	memset(mvi->tx, 0, sizeof(*mvi->tx) * MVS_CHIP_SLOT_SZ);

	mvi->rx_fis = dma_alloc_coherent(&pdev->dev, MVS_RX_FISL_SZ,
					 &mvi->rx_fis_dma, GFP_KERNEL);
	if (!mvi->rx_fis)
		goto err_out;
	memset(mvi->rx_fis, 0, MVS_RX_FISL_SZ);

	mvi->rx = dma_alloc_coherent(&pdev->dev,
				     sizeof(*mvi->rx) * (MVS_RX_RING_SZ + 1),
				     &mvi->rx_dma, GFP_KERNEL);
	if (!mvi->rx)
		goto err_out;
	memset(mvi->rx, 0, sizeof(*mvi->rx) * (MVS_RX_RING_SZ + 1));

	mvi->rx[0] = cpu_to_le32(0xfff);
	mvi->rx_cons = 0xfff;

	mvi->slot = dma_alloc_coherent(&pdev->dev,
				       sizeof(*mvi->slot) * MVS_SLOTS,
				       &mvi->slot_dma, GFP_KERNEL);
	if (!mvi->slot)
		goto err_out;
	memset(mvi->slot, 0, sizeof(*mvi->slot) * MVS_SLOTS);

	for (i = 0; i < MVS_SLOTS; i++) {
		struct mvs_slot_info *slot = &mvi->slot_info[i];

		slot->buf = dma_alloc_coherent(&pdev->dev, MVS_SLOT_BUF_SZ,
					       &slot->buf_dma, GFP_KERNEL);
		if (!slot->buf)
			goto err_out;
		memset(slot->buf, 0, MVS_SLOT_BUF_SZ);
	}

	/* finally, read NVRAM to get our SAS address */
	if (mvs_nvram_read(mvi, NVR_SAS_ADDR, &mvi->sas_addr, 8))
		goto err_out;
	return mvi;

err_out:
	mvs_free(mvi);
	return NULL;
}

static u32 mvs_cr32(void __iomem *regs, u32 addr)
{
	mw32(CMD_ADDR, addr);
	return mr32(CMD_DATA);
}

static void mvs_cw32(void __iomem *regs, u32 addr, u32 val)
{
	mw32(CMD_ADDR, addr);
	mw32(CMD_DATA, val);
}

static u32 mvs_read_phy_ctl(struct mvs_info *mvi, u32 port)
{
	void __iomem *regs = mvi->regs;
	return (port < 4)?mr32(P0_SER_CTLSTAT + port * 4):
		mr32(P4_SER_CTLSTAT + (port - 4) * 4);
}

static void mvs_write_phy_ctl(struct mvs_info *mvi, u32 port, u32 val)
{
	void __iomem *regs = mvi->regs;
	if (port < 4)
		mw32(P0_SER_CTLSTAT + port * 4, val);
	else
		mw32(P4_SER_CTLSTAT + (port - 4) * 4, val);
}

static u32 mvs_read_port(struct mvs_info *mvi, u32 off, u32 off2, u32 port)
{
	void __iomem *regs = mvi->regs + off;
	void __iomem *regs2 = mvi->regs + off2;
	return (port < 4)?readl(regs + port * 8):
		readl(regs2 + (port - 4) * 8);
}

static void mvs_write_port(struct mvs_info *mvi, u32 off, u32 off2,
				u32 port, u32 val)
{
	void __iomem *regs = mvi->regs + off;
	void __iomem *regs2 = mvi->regs + off2;
	if (port < 4)
		writel(val, regs + port * 8);
	else
		writel(val, regs2 + (port - 4) * 8);
}

static u32 mvs_read_port_cfg_data(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_CFG_DATA, MVS_P4_CFG_DATA, port);
}

static void mvs_write_port_cfg_data(struct mvs_info *mvi, u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_CFG_DATA, MVS_P4_CFG_DATA, port, val);
}

static void mvs_write_port_cfg_addr(struct mvs_info *mvi, u32 port, u32 addr)
{
	mvs_write_port(mvi, MVS_P0_CFG_ADDR, MVS_P4_CFG_ADDR, port, addr);
}

static u32 mvs_read_port_vsr_data(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_VSR_DATA, MVS_P4_VSR_DATA, port);
}

static void mvs_write_port_vsr_data(struct mvs_info *mvi, u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_VSR_DATA, MVS_P4_VSR_DATA, port, val);
}

static void mvs_write_port_vsr_addr(struct mvs_info *mvi, u32 port, u32 addr)
{
	mvs_write_port(mvi, MVS_P0_VSR_ADDR, MVS_P4_VSR_ADDR, port, addr);
}

static u32 mvs_read_port_irq_stat(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_INT_STAT, MVS_P4_INT_STAT, port);
}

static void mvs_write_port_irq_stat(struct mvs_info *mvi, u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_INT_STAT, MVS_P4_INT_STAT, port, val);
}

static u32 mvs_read_port_irq_mask(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_INT_MASK, MVS_P4_INT_MASK, port);
}

static void mvs_write_port_irq_mask(struct mvs_info *mvi, u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_INT_MASK, MVS_P4_INT_MASK, port, val);
}

static void __devinit mvs_phy_hacks(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	/* workaround for SATA R-ERR, to ignore phy glitch */
	tmp = mvs_cr32(regs, CMD_PHY_TIMER);
	tmp &= ~(1 << 9);
	tmp |= (1 << 10);
	mvs_cw32(regs, CMD_PHY_TIMER, tmp);

	/* enable retry 127 times */
	mvs_cw32(regs, CMD_SAS_CTL1, 0x7f7f);

	/* extend open frame timeout to max */
	tmp = mvs_cr32(regs, CMD_SAS_CTL0);
	tmp &= ~0xffff;
	tmp |= 0x3fff;
	mvs_cw32(regs, CMD_SAS_CTL0, tmp);

	/* workaround for WDTIMEOUT , set to 550 ms */
	mvs_cw32(regs, CMD_WD_TIMER, 0x86470);

	/* not to halt for different port op during wideport link change */
	mvs_cw32(regs, CMD_APP_ERR_CONFIG, 0xffefbf7d);

	/* workaround for Seagate disk not-found OOB sequence, recv
	 * COMINIT before sending out COMWAKE */
	tmp = mvs_cr32(regs, CMD_PHY_MODE_21);
	tmp &= 0x0000ffff;
	tmp |= 0x00fa0000;
	mvs_cw32(regs, CMD_PHY_MODE_21, tmp);

	tmp = mvs_cr32(regs, CMD_PHY_TIMER);
	tmp &= 0x1fffffff;
	tmp |= (2U << 29);	/* 8 ms retry */
	mvs_cw32(regs, CMD_PHY_TIMER, tmp);

	/* TEST - for phy decoding error, adjust voltage levels */
	mw32(P0_VSR_ADDR + 0, 0x8);
	mw32(P0_VSR_DATA + 0, 0x2F0);

	mw32(P0_VSR_ADDR + 8, 0x8);
	mw32(P0_VSR_DATA + 8, 0x2F0);

	mw32(P0_VSR_ADDR + 16, 0x8);
	mw32(P0_VSR_DATA + 16, 0x2F0);

	mw32(P0_VSR_ADDR + 24, 0x8);
	mw32(P0_VSR_DATA + 24, 0x2F0);

}

static void mvs_enable_xmt(struct mvs_info *mvi, int PhyId)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	tmp = mr32(PCS);
	if (mvi->chip->n_phy <= 4)
		tmp |= 1 << (PhyId + PCS_EN_PORT_XMT_SHIFT);
	else
		tmp |= 1 << (PhyId + PCS_EN_PORT_XMT_SHIFT2);
	mw32(PCS, tmp);
}

static void mvs_detect_porttype(struct mvs_info *mvi, int i)
{
	void __iomem *regs = mvi->regs;
	u32 reg;
	struct mvs_phy *phy = &mvi->phy[i];

	/* TODO check & save device type */
	reg = mr32(GBL_PORT_TYPE);

	if (reg & MODE_SAS_SATA & (1 << i))
		phy->phy_type |= PORT_TYPE_SAS;
	else
		phy->phy_type |= PORT_TYPE_SATA;
}

static void *mvs_get_d2h_reg(struct mvs_info *mvi, int i, void *buf)
{
	u32 *s = (u32 *) buf;

	if (!s)
		return NULL;

	mvs_write_port_cfg_addr(mvi, i, PHYR_SATA_SIG3);
	s[3] = mvs_read_port_cfg_data(mvi, i);

	mvs_write_port_cfg_addr(mvi, i, PHYR_SATA_SIG2);
	s[2] = mvs_read_port_cfg_data(mvi, i);

	mvs_write_port_cfg_addr(mvi, i, PHYR_SATA_SIG1);
	s[1] = mvs_read_port_cfg_data(mvi, i);

	mvs_write_port_cfg_addr(mvi, i, PHYR_SATA_SIG0);
	s[0] = mvs_read_port_cfg_data(mvi, i);

	return (void *)s;
}

static u32 mvs_is_sig_fis_received(u32 irq_status)
{
	return irq_status & PHYEV_SIG_FIS;
}

static void mvs_update_wideport(struct mvs_info *mvi, int i)
{
	struct mvs_phy *phy = &mvi->phy[i];
	struct mvs_port *port = phy->port;
	int j, no;

	for_each_phy(port->wide_port_phymap, no, j, mvi->chip->n_phy)
		if (no & 1) {
			mvs_write_port_cfg_addr(mvi, no, PHYR_WIDE_PORT);
			mvs_write_port_cfg_data(mvi, no,
						port->wide_port_phymap);
		} else {
			mvs_write_port_cfg_addr(mvi, no, PHYR_WIDE_PORT);
			mvs_write_port_cfg_data(mvi, no, 0);
		}
}

static u32 mvs_is_phy_ready(struct mvs_info *mvi, int i)
{
	u32 tmp;
	struct mvs_phy *phy = &mvi->phy[i];
	struct mvs_port *port = phy->port;;

	tmp = mvs_read_phy_ctl(mvi, i);

	if ((tmp & PHY_READY_MASK) && !(phy->irq_status & PHYEV_POOF)) {
		if (!port)
			phy->phy_attached = 1;
		return tmp;
	}

	if (port) {
		if (phy->phy_type & PORT_TYPE_SAS) {
			port->wide_port_phymap &= ~(1U << i);
			if (!port->wide_port_phymap)
				port->port_attached = 0;
			mvs_update_wideport(mvi, i);
		} else if (phy->phy_type & PORT_TYPE_SATA)
			port->port_attached = 0;
		mvs_free_reg_set(mvi, phy->port);
		phy->port = NULL;
		phy->phy_attached = 0;
		phy->phy_type &= ~(PORT_TYPE_SAS | PORT_TYPE_SATA);
	}
	return 0;
}

static void mvs_update_phyinfo(struct mvs_info *mvi, int i,
					int get_st)
{
	struct mvs_phy *phy = &mvi->phy[i];
	struct pci_dev *pdev = mvi->pdev;
	u32 tmp;
	u64 tmp64;

	mvs_write_port_cfg_addr(mvi, i, PHYR_IDENTIFY);
	phy->dev_info = mvs_read_port_cfg_data(mvi, i);

	mvs_write_port_cfg_addr(mvi, i, PHYR_ADDR_HI);
	phy->dev_sas_addr = (u64) mvs_read_port_cfg_data(mvi, i) << 32;

	mvs_write_port_cfg_addr(mvi, i, PHYR_ADDR_LO);
	phy->dev_sas_addr |= mvs_read_port_cfg_data(mvi, i);

	if (get_st) {
		phy->irq_status = mvs_read_port_irq_stat(mvi, i);
		phy->phy_status = mvs_is_phy_ready(mvi, i);
	}

	if (phy->phy_status) {
		u32 phy_st;
		struct asd_sas_phy *sas_phy = mvi->sas.sas_phy[i];

		mvs_write_port_cfg_addr(mvi, i, PHYR_PHY_STAT);
		phy_st = mvs_read_port_cfg_data(mvi, i);

		sas_phy->linkrate =
			(phy->phy_status & PHY_NEG_SPP_PHYS_LINK_RATE_MASK) >>
				PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET;
		phy->minimum_linkrate =
			(phy->phy_status &
				PHY_MIN_SPP_PHYS_LINK_RATE_MASK) >> 8;
		phy->maximum_linkrate =
			(phy->phy_status &
				PHY_MAX_SPP_PHYS_LINK_RATE_MASK) >> 12;

		if (phy->phy_type & PORT_TYPE_SAS) {
			/* Updated attached_sas_addr */
			mvs_write_port_cfg_addr(mvi, i, PHYR_ATT_ADDR_HI);
			phy->att_dev_sas_addr =
				(u64) mvs_read_port_cfg_data(mvi, i) << 32;
			mvs_write_port_cfg_addr(mvi, i, PHYR_ATT_ADDR_LO);
			phy->att_dev_sas_addr |= mvs_read_port_cfg_data(mvi, i);
			mvs_write_port_cfg_addr(mvi, i, PHYR_ATT_DEV_INFO);
			phy->att_dev_info = mvs_read_port_cfg_data(mvi, i);
			phy->identify.device_type =
			    phy->att_dev_info & PORT_DEV_TYPE_MASK;

			if (phy->identify.device_type == SAS_END_DEV)
				phy->identify.target_port_protocols =
							SAS_PROTOCOL_SSP;
			else if (phy->identify.device_type != NO_DEVICE)
				phy->identify.target_port_protocols =
							SAS_PROTOCOL_SMP;
			if (phy_st & PHY_OOB_DTCTD)
				sas_phy->oob_mode = SAS_OOB_MODE;
			phy->frame_rcvd_size =
			    sizeof(struct sas_identify_frame);
		} else if (phy->phy_type & PORT_TYPE_SATA) {
			phy->identify.target_port_protocols = SAS_PROTOCOL_STP;
			if (mvs_is_sig_fis_received(phy->irq_status)) {
				phy->att_dev_sas_addr = i;	/* temp */
				if (phy_st & PHY_OOB_DTCTD)
					sas_phy->oob_mode = SATA_OOB_MODE;
				phy->frame_rcvd_size =
				    sizeof(struct dev_to_host_fis);
				mvs_get_d2h_reg(mvi, i,
						(void *)sas_phy->frame_rcvd);
			} else {
				dev_printk(KERN_DEBUG, &pdev->dev,
					"No sig fis\n");
				phy->phy_type &= ~(PORT_TYPE_SATA);
				goto out_done;
			}
		}
		tmp64 = cpu_to_be64(phy->att_dev_sas_addr);
		memcpy(sas_phy->attached_sas_addr, &tmp64, SAS_ADDR_SIZE);

		dev_printk(KERN_DEBUG, &pdev->dev,
			"phy[%d] Get Attached Address 0x%llX ,"
			" SAS Address 0x%llX\n",
			i,
			(unsigned long long)phy->att_dev_sas_addr,
			(unsigned long long)phy->dev_sas_addr);
		dev_printk(KERN_DEBUG, &pdev->dev,
			"Rate = %x , type = %d\n",
			sas_phy->linkrate, phy->phy_type);

		/* workaround for HW phy decoding error on 1.5g disk drive */
		mvs_write_port_vsr_addr(mvi, i, VSR_PHY_MODE6);
		tmp = mvs_read_port_vsr_data(mvi, i);
		if (((phy->phy_status & PHY_NEG_SPP_PHYS_LINK_RATE_MASK) >>
		     PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET) ==
			SAS_LINK_RATE_1_5_GBPS)
			tmp &= ~PHY_MODE6_LATECLK;
		else
			tmp |= PHY_MODE6_LATECLK;
		mvs_write_port_vsr_data(mvi, i, tmp);

	}
out_done:
	if (get_st)
		mvs_write_port_irq_stat(mvi, i, phy->irq_status);
}

static void mvs_port_formed(struct asd_sas_phy *sas_phy)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct mvs_info *mvi = sas_ha->lldd_ha;
	struct asd_sas_port *sas_port = sas_phy->port;
	struct mvs_phy *phy = sas_phy->lldd_phy;
	struct mvs_port *port = &mvi->port[sas_port->id];
	unsigned long flags;

	spin_lock_irqsave(&mvi->lock, flags);
	port->port_attached = 1;
	phy->port = port;
	port->taskfileset = MVS_ID_NOT_MAPPED;
	if (phy->phy_type & PORT_TYPE_SAS) {
		port->wide_port_phymap = sas_port->phy_mask;
		mvs_update_wideport(mvi, sas_phy->id);
	}
	spin_unlock_irqrestore(&mvi->lock, flags);
}

static int mvs_I_T_nexus_reset(struct domain_device *dev)
{
	return TMF_RESP_FUNC_FAILED;
}

static int __devinit mvs_hw_init(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	int i;
	u32 tmp, cctl;

	/* make sure interrupts are masked immediately (paranoia) */
	mw32(GBL_CTL, 0);
	tmp = mr32(GBL_CTL);

	/* Reset Controller */
	if (!(tmp & HBA_RST)) {
		if (mvi->flags & MVF_PHY_PWR_FIX) {
			pci_read_config_dword(mvi->pdev, PCR_PHY_CTL, &tmp);
			tmp &= ~PCTL_PWR_ON;
			tmp |= PCTL_OFF;
			pci_write_config_dword(mvi->pdev, PCR_PHY_CTL, tmp);

			pci_read_config_dword(mvi->pdev, PCR_PHY_CTL2, &tmp);
			tmp &= ~PCTL_PWR_ON;
			tmp |= PCTL_OFF;
			pci_write_config_dword(mvi->pdev, PCR_PHY_CTL2, tmp);
		}

		/* global reset, incl. COMRESET/H_RESET_N (self-clearing) */
		mw32_f(GBL_CTL, HBA_RST);
	}

	/* wait for reset to finish; timeout is just a guess */
	i = 1000;
	while (i-- > 0) {
		msleep(10);

		if (!(mr32(GBL_CTL) & HBA_RST))
			break;
	}
	if (mr32(GBL_CTL) & HBA_RST) {
		dev_printk(KERN_ERR, &mvi->pdev->dev, "HBA reset failed\n");
		return -EBUSY;
	}

	/* Init Chip */
	/* make sure RST is set; HBA_RST /should/ have done that for us */
	cctl = mr32(CTL);
	if (cctl & CCTL_RST)
		cctl &= ~CCTL_RST;
	else
		mw32_f(CTL, cctl | CCTL_RST);

	/* write to device control _AND_ device status register? - A.C. */
	pci_read_config_dword(mvi->pdev, PCR_DEV_CTRL, &tmp);
	tmp &= ~PRD_REQ_MASK;
	tmp |= PRD_REQ_SIZE;
	pci_write_config_dword(mvi->pdev, PCR_DEV_CTRL, tmp);

	pci_read_config_dword(mvi->pdev, PCR_PHY_CTL, &tmp);
	tmp |= PCTL_PWR_ON;
	tmp &= ~PCTL_OFF;
	pci_write_config_dword(mvi->pdev, PCR_PHY_CTL, tmp);

	pci_read_config_dword(mvi->pdev, PCR_PHY_CTL2, &tmp);
	tmp |= PCTL_PWR_ON;
	tmp &= ~PCTL_OFF;
	pci_write_config_dword(mvi->pdev, PCR_PHY_CTL2, tmp);

	mw32_f(CTL, cctl);

	/* reset control */
	mw32(PCS, 0);		/*MVS_PCS */

	mvs_phy_hacks(mvi);

	mw32(CMD_LIST_LO, mvi->slot_dma);
	mw32(CMD_LIST_HI, (mvi->slot_dma >> 16) >> 16);

	mw32(RX_FIS_LO, mvi->rx_fis_dma);
	mw32(RX_FIS_HI, (mvi->rx_fis_dma >> 16) >> 16);

	mw32(TX_CFG, MVS_CHIP_SLOT_SZ);
	mw32(TX_LO, mvi->tx_dma);
	mw32(TX_HI, (mvi->tx_dma >> 16) >> 16);

	mw32(RX_CFG, MVS_RX_RING_SZ);
	mw32(RX_LO, mvi->rx_dma);
	mw32(RX_HI, (mvi->rx_dma >> 16) >> 16);

	/* enable auto port detection */
	mw32(GBL_PORT_TYPE, MODE_AUTO_DET_EN);
	msleep(1100);
	/* init and reset phys */
	for (i = 0; i < mvi->chip->n_phy; i++) {
		u32 lo = be32_to_cpu(*(u32 *)&mvi->sas_addr[4]);
		u32 hi = be32_to_cpu(*(u32 *)&mvi->sas_addr[0]);

		mvs_detect_porttype(mvi, i);

		/* set phy local SAS address */
		mvs_write_port_cfg_addr(mvi, i, PHYR_ADDR_LO);
		mvs_write_port_cfg_data(mvi, i, lo);
		mvs_write_port_cfg_addr(mvi, i, PHYR_ADDR_HI);
		mvs_write_port_cfg_data(mvi, i, hi);

		/* reset phy */
		tmp = mvs_read_phy_ctl(mvi, i);
		tmp |= PHY_RST;
		mvs_write_phy_ctl(mvi, i, tmp);
	}

	msleep(100);

	for (i = 0; i < mvi->chip->n_phy; i++) {
		/* clear phy int status */
		tmp = mvs_read_port_irq_stat(mvi, i);
		tmp &= ~PHYEV_SIG_FIS;
		mvs_write_port_irq_stat(mvi, i, tmp);

		/* set phy int mask */
		tmp = PHYEV_RDY_CH | PHYEV_BROAD_CH | PHYEV_UNASSOC_FIS |
			PHYEV_ID_DONE | PHYEV_DEC_ERR;
		mvs_write_port_irq_mask(mvi, i, tmp);

		msleep(100);
		mvs_update_phyinfo(mvi, i, 1);
		mvs_enable_xmt(mvi, i);
	}

	/* FIXME: update wide port bitmaps */

	/* little endian for open address and command table, etc. */
	/* A.C.
	 * it seems that ( from the spec ) turning on big-endian won't
	 * do us any good on big-endian machines, need further confirmation
	 */
	cctl = mr32(CTL);
	cctl |= CCTL_ENDIAN_CMD;
	cctl |= CCTL_ENDIAN_DATA;
	cctl &= ~CCTL_ENDIAN_OPEN;
	cctl |= CCTL_ENDIAN_RSP;
	mw32_f(CTL, cctl);

	/* reset CMD queue */
	tmp = mr32(PCS);
	tmp |= PCS_CMD_RST;
	mw32(PCS, tmp);
	/* interrupt coalescing may cause missing HW interrput in some case,
	 * and the max count is 0x1ff, while our max slot is 0x200,
	 * it will make count 0.
	 */
	tmp = 0;
	mw32(INT_COAL, tmp);

	tmp = 0x100;
	mw32(INT_COAL_TMOUT, tmp);

	/* ladies and gentlemen, start your engines */
	mw32(TX_CFG, 0);
	mw32(TX_CFG, MVS_CHIP_SLOT_SZ | TX_EN);
	mw32(RX_CFG, MVS_RX_RING_SZ | RX_EN);
	/* enable CMD/CMPL_Q/RESP mode */
	mw32(PCS, PCS_SATA_RETRY | PCS_FIS_RX_EN | PCS_CMD_EN);

	/* enable completion queue interrupt */
	tmp = (CINT_PORT_MASK | CINT_DONE | CINT_MEM | CINT_SRS);
	mw32(INT_MASK, tmp);

	/* Enable SRS interrupt */
	mw32(INT_MASK_SRS, 0xFF);
	return 0;
}

static void __devinit mvs_print_info(struct mvs_info *mvi)
{
	struct pci_dev *pdev = mvi->pdev;
	static int printed_version;

	if (!printed_version++)
		dev_printk(KERN_INFO, &pdev->dev, "version " DRV_VERSION "\n");

	dev_printk(KERN_INFO, &pdev->dev, "%u phys, addr %llx\n",
		   mvi->chip->n_phy, SAS_ADDR(mvi->sas_addr));
}

static int __devinit mvs_pci_init(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	int rc;
	struct mvs_info *mvi;
	irq_handler_t irq_handler = mvs_interrupt;

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	pci_set_master(pdev);

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out_disable;

	rc = pci_go_64(pdev);
	if (rc)
		goto err_out_regions;

	mvi = mvs_alloc(pdev, ent);
	if (!mvi) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	rc = mvs_hw_init(mvi);
	if (rc)
		goto err_out_mvi;

#ifndef MVS_DISABLE_MSI
	if (!pci_enable_msi(pdev)) {
		u32 tmp;
		void __iomem *regs = mvi->regs;
		mvi->flags |= MVF_MSI;
		irq_handler = mvs_msi_interrupt;
		tmp = mr32(PCS);
		mw32(PCS, tmp | PCS_SELF_CLEAR);
	}
#endif

	rc = request_irq(pdev->irq, irq_handler, IRQF_SHARED, DRV_NAME, mvi);
	if (rc)
		goto err_out_msi;

	rc = scsi_add_host(mvi->shost, &pdev->dev);
	if (rc)
		goto err_out_irq;

	rc = sas_register_ha(&mvi->sas);
	if (rc)
		goto err_out_shost;

	pci_set_drvdata(pdev, mvi);

	mvs_print_info(mvi);

	mvs_hba_interrupt_enable(mvi);

	scsi_scan_host(mvi->shost);

	return 0;

err_out_shost:
	scsi_remove_host(mvi->shost);
err_out_irq:
	free_irq(pdev->irq, mvi);
err_out_msi:
	if (mvi->flags |= MVF_MSI)
		pci_disable_msi(pdev);
err_out_mvi:
	mvs_free(mvi);
err_out_regions:
	pci_release_regions(pdev);
err_out_disable:
	pci_disable_device(pdev);
	return rc;
}

static void __devexit mvs_pci_remove(struct pci_dev *pdev)
{
	struct mvs_info *mvi = pci_get_drvdata(pdev);

	pci_set_drvdata(pdev, NULL);

	if (mvi) {
		sas_unregister_ha(&mvi->sas);
		mvs_hba_interrupt_disable(mvi);
		sas_remove_host(mvi->shost);
		scsi_remove_host(mvi->shost);

		free_irq(pdev->irq, mvi);
		if (mvi->flags & MVF_MSI)
			pci_disable_msi(pdev);
		mvs_free(mvi);
		pci_release_regions(pdev);
	}
	pci_disable_device(pdev);
}

static struct sas_domain_function_template mvs_transport_ops = {
	.lldd_execute_task	= mvs_task_exec,
	.lldd_control_phy	= mvs_phy_control,
	.lldd_abort_task	= mvs_task_abort,
	.lldd_port_formed	= mvs_port_formed,
	.lldd_I_T_nexus_reset	= mvs_I_T_nexus_reset,
};

static struct pci_device_id __devinitdata mvs_pci_table[] = {
	{ PCI_VDEVICE(MARVELL, 0x6320), chip_6320 },
	{ PCI_VDEVICE(MARVELL, 0x6340), chip_6440 },
	{
		.vendor 	= PCI_VENDOR_ID_MARVELL,
		.device 	= 0x6440,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= 0x6480,
		.class		= 0,
		.class_mask	= 0,
		.driver_data	= chip_6480,
	},
	{ PCI_VDEVICE(MARVELL, 0x6440), chip_6440 },
	{ PCI_VDEVICE(MARVELL, 0x6480), chip_6480 },

	{ }	/* terminate list */
};

static struct pci_driver mvs_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= mvs_pci_table,
	.probe		= mvs_pci_init,
	.remove		= __devexit_p(mvs_pci_remove),
};

static int __init mvs_init(void)
{
	int rc;

	mvs_stt = sas_domain_attach_transport(&mvs_transport_ops);
	if (!mvs_stt)
		return -ENOMEM;

	rc = pci_register_driver(&mvs_pci_driver);
	if (rc)
		goto err_out;

	return 0;

err_out:
	sas_release_transport(mvs_stt);
	return rc;
}

static void __exit mvs_exit(void)
{
	pci_unregister_driver(&mvs_pci_driver);
	sas_release_transport(mvs_stt);
}

module_init(mvs_init);
module_exit(mvs_exit);

MODULE_AUTHOR("Jeff Garzik <jgarzik@pobox.com>");
MODULE_DESCRIPTION("Marvell 88SE6440 SAS/SATA controller driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, mvs_pci_table);
