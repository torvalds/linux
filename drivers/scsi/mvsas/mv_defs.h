/*
 * Marvell 88SE64xx/88SE94xx const head file
 *
 * Copyright 2007 Red Hat, Inc.
 * Copyright 2008 Marvell. <kewei@marvell.com>
 * Copyright 2009-2011 Marvell. <yuxiangl@marvell.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
*/

#ifndef _MV_DEFS_H_
#define _MV_DEFS_H_

#define PCI_DEVICE_ID_ARECA_1300	0x1300
#define PCI_DEVICE_ID_ARECA_1320	0x1320

enum chip_flavors {
	chip_6320,
	chip_6440,
	chip_6485,
	chip_9480,
	chip_9180,
	chip_9445,
	chip_9485,
	chip_1300,
	chip_1320
};

/* driver compile-time configuration */
enum driver_configuration {
	MVS_TX_RING_SZ		= 1024,	/* TX ring size (12-bit) */
	MVS_RX_RING_SZ		= 1024, /* RX ring size (12-bit) */
					/* software requires power-of-2
					   ring size */
	MVS_SOC_SLOTS		= 64,
	MVS_SOC_TX_RING_SZ	= MVS_SOC_SLOTS * 2,
	MVS_SOC_RX_RING_SZ	= MVS_SOC_SLOTS * 2,

	MVS_SLOT_BUF_SZ		= 8192, /* cmd tbl + IU + status + PRD */
	MVS_SSP_CMD_SZ		= 64,	/* SSP command table buffer size */
	MVS_ATA_CMD_SZ		= 96,	/* SATA command table buffer size */
	MVS_OAF_SZ		= 64,	/* Open address frame buffer size */
	MVS_QUEUE_SIZE		= 64,	/* Support Queue depth */
	MVS_SOC_CAN_QUEUE	= MVS_SOC_SLOTS - 2,
};

/* unchangeable hardware details */
enum hardware_details {
	MVS_MAX_PHYS		= 8,	/* max. possible phys */
	MVS_MAX_PORTS		= 8,	/* max. possible ports */
	MVS_SOC_PHYS		= 4,	/* soc phys */
	MVS_SOC_PORTS		= 4,	/* soc phys */
	MVS_MAX_DEVICES	= 1024,	/* max supported device */
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
	CINT_NON_SPEC_NCQ_ERROR	= (1U << 25),	/* Non specific NCQ error */
	CINT_SRS		= (1U << 3),	/* SRS event */
	CINT_CI_STOP		= (1U << 1),	/* cmd issue stopped */
	CINT_DONE		= (1U << 0),	/* cmd completion */

						/* shl for ports 1-3 */
	CINT_PORT_STOPPED	= (1U << 16),	/* port0 stopped */
	CINT_PORT		= (1U << 8),	/* port0 event */
	CINT_PORT_MASK_OFFSET	= 8,
	CINT_PORT_MASK		= (0xFF << CINT_PORT_MASK_OFFSET),
	CINT_PHY_MASK_OFFSET	= 4,
	CINT_PHY_MASK		= (0x0F << CINT_PHY_MASK_OFFSET),

	/* TX (delivery) ring bits */
	TXQ_CMD_SHIFT		= 29,
	TXQ_CMD_SSP		= 1,		/* SSP protocol */
	TXQ_CMD_SMP		= 2,		/* SMP protocol */
	TXQ_CMD_STP		= 3,		/* STP/SATA protocol */
	TXQ_CMD_SSP_FREE_LIST	= 4,		/* add to SSP target free list */
	TXQ_CMD_SLOT_RESET	= 7,		/* reset command slot */
	TXQ_MODE_I		= (1U << 28),	/* mode: 0=target,1=initiator */
	TXQ_MODE_TARGET 	= 0,
	TXQ_MODE_INITIATOR	= 1,
	TXQ_PRIO_HI		= (1U << 27),	/* priority: 0=normal, 1=high */
	TXQ_PRI_NORMAL		= 0,
	TXQ_PRI_HIGH		= 1,
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

	MCH_SSP_MODE_PASSTHRU	= 1,
	MCH_SSP_MODE_NORMAL	= 0,
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
	PHY_READY_MASK		= (1U << 20),

	/* MVS_Px_INT_STAT, MVS_Px_INT_MASK (per-phy events) */
	PHYEV_DEC_ERR		= (1U << 24),	/* Phy Decoding Error */
	PHYEV_DCDR_ERR		= (1U << 23),	/* STP Deocder Error */
	PHYEV_CRC_ERR		= (1U << 22),	/* STP CRC Error */
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
	PCS_EN_PORT_XMT_SHIFT2	= (8),		/* For 6485 */
	PCS_SATA_RETRY		= (1U << 8),	/* retry ctl FIS on R_ERR */
	PCS_RSP_RX_EN		= (1U << 7),	/* raw response rx */
	PCS_SATA_RETRY_2	= (1U << 6),	/* For 9180 */
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
	PORT_SSP_TRGT_MASK	= (0x1U << 19),
	PORT_SSP_INIT_MASK	= (0x1U << 11),
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
	PHYR_SATA_SIG0	= 0x20,	/*port SATA signature FIS(Byte 0-3) */
	PHYR_SATA_SIG1	= 0x24,	/*port SATA signature FIS(Byte 4-7) */
	PHYR_SATA_SIG2	= 0x28,	/*port SATA signature FIS(Byte 8-11) */
	PHYR_SATA_SIG3	= 0x2c,	/*port SATA signature FIS(Byte 12-15) */
	PHYR_R_ERR_COUNT	= 0x30, /* port R_ERR count register */
	PHYR_CRC_ERR_COUNT	= 0x34, /* port CRC error count register */
	PHYR_WIDE_PORT	= 0x38,	/* wide port participating */
	PHYR_CURRENT0		= 0x80,	/* current connection info 0 */
	PHYR_CURRENT1		= 0x84,	/* current connection info 1 */
	PHYR_CURRENT2		= 0x88,	/* current connection info 2 */
	CONFIG_ID_FRAME0       = 0x100, /* Port device ID frame register 0 */
	CONFIG_ID_FRAME1       = 0x104, /* Port device ID frame register 1 */
	CONFIG_ID_FRAME2       = 0x108, /* Port device ID frame register 2 */
	CONFIG_ID_FRAME3       = 0x10c, /* Port device ID frame register 3 */
	CONFIG_ID_FRAME4       = 0x110, /* Port device ID frame register 4 */
	CONFIG_ID_FRAME5       = 0x114, /* Port device ID frame register 5 */
	CONFIG_ID_FRAME6       = 0x118, /* Port device ID frame register 6 */
	CONFIG_ATT_ID_FRAME0   = 0x11c, /* attached ID frame register 0 */
	CONFIG_ATT_ID_FRAME1   = 0x120, /* attached ID frame register 1 */
	CONFIG_ATT_ID_FRAME2   = 0x124, /* attached ID frame register 2 */
	CONFIG_ATT_ID_FRAME3   = 0x128, /* attached ID frame register 3 */
	CONFIG_ATT_ID_FRAME4   = 0x12c, /* attached ID frame register 4 */
	CONFIG_ATT_ID_FRAME5   = 0x130, /* attached ID frame register 5 */
	CONFIG_ATT_ID_FRAME6   = 0x134, /* attached ID frame register 6 */
};

enum sas_cmd_port_registers {
	CMD_CMRST_OOB_DET	= 0x100, /* COMRESET OOB detect register */
	CMD_CMWK_OOB_DET	= 0x104, /* COMWAKE OOB detect register */
	CMD_CMSAS_OOB_DET	= 0x108, /* COMSAS OOB detect register */
	CMD_BRST_OOB_DET	= 0x10c, /* burst OOB detect register */
	CMD_OOB_SPACE	= 0x110, /* OOB space control register */
	CMD_OOB_BURST	= 0x114, /* OOB burst control register */
	CMD_PHY_TIMER		= 0x118, /* PHY timer control register */
	CMD_PHY_CONFIG0	= 0x11c, /* PHY config register 0 */
	CMD_PHY_CONFIG1	= 0x120, /* PHY config register 1 */
	CMD_SAS_CTL0		= 0x124, /* SAS control register 0 */
	CMD_SAS_CTL1		= 0x128, /* SAS control register 1 */
	CMD_SAS_CTL2		= 0x12c, /* SAS control register 2 */
	CMD_SAS_CTL3		= 0x130, /* SAS control register 3 */
	CMD_ID_TEST		= 0x134, /* ID test register */
	CMD_PL_TIMER		= 0x138, /* PL timer register */
	CMD_WD_TIMER		= 0x13c, /* WD timer register */
	CMD_PORT_SEL_COUNT	= 0x140, /* port selector count register */
	CMD_APP_MEM_CTL	= 0x144, /* Application Memory Control */
	CMD_XOR_MEM_CTL	= 0x148, /* XOR Block Memory Control */
	CMD_DMA_MEM_CTL	= 0x14c, /* DMA Block Memory Control */
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
	CMD_PORT_LAYER_TIMER1	= 0x1E0, /* Port Layer Timer 1 */
	CMD_LINK_TIMER		= 0x1E4, /* Link Timer */
};

enum mvs_info_flags {
	MVF_PHY_PWR_FIX	= (1U << 1),	/* bug workaround */
	MVF_FLAG_SOC		= (1U << 2),	/* SoC integrated controllers */
};

enum mvs_event_flags {
	PHY_PLUG_EVENT		= (3U),
	PHY_PLUG_IN		= (1U << 0),	/* phy plug in */
	PHY_PLUG_OUT		= (1U << 1),	/* phy plug out */
	EXP_BRCT_CHG		= (1U << 2),	/* broadcast change */
};

enum mvs_port_type {
	PORT_TGT_MASK	=  (1U << 5),
	PORT_INIT_PORT	=  (1U << 4),
	PORT_TGT_PORT	=  (1U << 3),
	PORT_INIT_TGT_PORT = (PORT_INIT_PORT | PORT_TGT_PORT),
	PORT_TYPE_SAS	=  (1U << 1),
	PORT_TYPE_SATA	=  (1U << 0),
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

enum pci_cfg_register_bits {
	PCTL_PWR_OFF	= (0xFU << 24),
	PCTL_COM_ON	= (0xFU << 20),
	PCTL_LINK_RST	= (0xFU << 16),
	PCTL_LINK_OFFS	= (16),
	PCTL_PHY_DSBL	= (0xFU << 12),
	PCTL_PHY_DSBL_OFFS	= (12),
	PRD_REQ_SIZE	= (0x4000),
	PRD_REQ_MASK	= (0x00007000),
	PLS_NEG_LINK_WD		= (0x3FU << 4),
	PLS_NEG_LINK_WD_OFFS	= 4,
	PLS_LINK_SPD		= (0x0FU << 0),
	PLS_LINK_SPD_OFFS	= 0,
};

enum open_frame_protocol {
	PROTOCOL_SMP	= 0x0,
	PROTOCOL_SSP	= 0x1,
	PROTOCOL_STP	= 0x2,
};

/* define for response frame datapres field */
enum datapres_field {
	NO_DATA		= 0,
	RESPONSE_DATA	= 1,
	SENSE_DATA	= 2,
};

/* define task management IU */
struct mvs_tmf_task{
	u8 tmf;
	u16 tag_of_task_to_be_managed;
};
#endif
