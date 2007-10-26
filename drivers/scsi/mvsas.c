/*
	mvsas.c - Marvell 88SE6440 SAS/SATA support

	Copyright 2007 Red Hat, Inc.

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
#include <scsi/libsas.h>
#include <asm/io.h>

#define DRV_NAME "mvsas"
#define DRV_VERSION "0.1"

#define mr32(reg)	readl(regs + MVS_##reg)
#define mw32(reg,val)	writel((val), regs + MVS_##reg)
#define mw32_f(reg,val)	do {		\
	writel((val), regs + MVS_##reg);	\
	readl(regs + MVS_##reg);		\
	} while (0)

/* driver compile-time configuration */
enum driver_configuration {
	MVS_TX_RING_SZ		= 1024,	/* TX ring size (12-bit) */
	MVS_RX_RING_SZ		= 1024, /* RX ring size (12-bit) */
					/* software requires power-of-2
					   ring size */

	MVS_SLOTS		= 512,	/* command slots */
	MVS_SLOT_BUF_SZ		= 8192, /* cmd tbl + IU + status + PRD */
	MVS_SSP_CMD_SZ		= 64,	/* SSP command table buffer size */
	MVS_ATA_CMD_SZ		= 128,	/* SATA command table buffer size */
	MVS_OAF_SZ		= 64,	/* Open address frame buffer size */

	MVS_RX_FIS_COUNT	= 17,	/* Optional rx'd FISs (max 17) */
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
	MVS_GBL_PORT_TYPE	= 0x00,  /* port type */

	MVS_CTL			= 0x100, /* SAS/SATA port configuration */
	MVS_PCS			= 0x104, /* SAS/SATA port control/status */
	MVS_CMD_LIST_LO		= 0x108, /* cmd list addr */
	MVS_CMD_LIST_HI		= 0x10C,
	MVS_RX_FIS_LO		= 0x110, /* RX FIS list addr */
	MVS_RX_FIS_HI		= 0x114,

	MVS_TX_CFG		= 0x120, /* TX configuration */
	MVS_TX_LO		= 0x124, /* TX (delivery) ring addr */
	MVS_TX_HI		= 0x128,

	MVS_RX_PROD_IDX		= 0x12C, /* RX producer pointer */
	MVS_RX_CONS_IDX		= 0x130, /* RX consumer pointer (RO) */
	MVS_RX_CFG		= 0x134, /* RX configuration */
	MVS_RX_LO		= 0x138, /* RX (completion) ring addr */
	MVS_RX_HI		= 0x13C,

	MVS_INT_COAL		= 0x148, /* Int coalescing config */
	MVS_INT_COAL_TMOUT	= 0x14C, /* Int coalescing timeout */
	MVS_INT_STAT		= 0x150, /* Central int status */
	MVS_INT_MASK		= 0x154, /* Central int enable */
	MVS_INT_STAT_SRS	= 0x158, /* SATA register set status */

					 /* ports 1-3 follow after this */
	MVS_P0_INT_STAT		= 0x160, /* port0 interrupt status */
	MVS_P0_INT_MASK		= 0x164, /* port0 interrupt mask */

					 /* ports 1-3 follow after this */
	MVS_P0_SER_CTLSTAT	= 0x180, /* port0 serial control/status */

	MVS_CMD_ADDR		= 0x1B8, /* Command register port (addr) */
	MVS_CMD_DATA		= 0x1BC, /* Command register port (data) */

					 /* ports 1-3 follow after this */
	MVS_P0_CFG_ADDR		= 0x1C0, /* port0 phy register address */
	MVS_P0_CFG_DATA		= 0x1C4, /* port0 phy register data */
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
	AUTO_DET		= (1U << 8),	/* port0 SAS/SATA autodetect */
	SAS_MODE		= (1U << 0),	/* port0 SAS(1), SATA(0) mode */
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
	CINT_CI_STOP		= (1U << 10),	/* cmd issue stopped */
	CINT_DONE		= (1U << 0),	/* cmd completion */

						/* shl for ports 1-3 */
	CINT_PORT_STOPPED	= (1U << 16),	/* port0 stopped */
	CINT_PORT		= (1U << 8),	/* port0 event */

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

	/* MVS_Px_INT_STAT, MVS_Px_INT_MASK (per-phy events) */
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
	PCS_SATA_RETRY		= (1U << 8),	/* retry ctl FIS on R_ERR */
	PCS_RSP_RX_EN		= (1U << 7),	/* raw response rx */
	PCS_SELF_CLEAR		= (1U << 5),	/* self-clearing int mode */
	PCS_FIS_RX_EN		= (1U << 4),	/* FIS rx enable */
	PCS_CMD_STOP_ERR	= (1U << 3),	/* cmd stop-on-err enable */
	PCS_CMD_RST		= (1U << 2),	/* reset cmd issue */
	PCS_CMD_EN		= (1U << 0),	/* enable cmd issue */
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
	PHYR_IDENTIFY		= 0x0,	/* info for IDENTIFY frame */
	PHYR_ADDR_LO		= 0x4,	/* my SAS address (low) */
	PHYR_ADDR_HI		= 0x8,	/* my SAS address (high) */
	PHYR_ATT_DEV_INFO	= 0xC,	/* attached device info */
	PHYR_ATT_ADDR_LO	= 0x10,	/* attached dev SAS addr (low) */
	PHYR_ATT_ADDR_HI	= 0x14,	/* attached dev SAS addr (high) */
	PHYR_SATA_CTL		= 0x18,	/* SATA control */
	PHYR_PHY_STAT		= 0x1C,	/* PHY status */
	PHYR_WIDE_PORT		= 0x38,	/* wide port participating */
	PHYR_CURRENT0		= 0x80,	/* current connection info 0 */
	PHYR_CURRENT1		= 0x84,	/* current connection info 1 */
	PHYR_CURRENT2		= 0x88,	/* current connection info 2 */
};

enum pci_cfg_registers {
	PCR_PHY_CTL		= 0x40,
	PCR_PHY_CTL2		= 0x90,
};

enum pci_cfg_register_bits {
	PCTL_PWR_ON		= (0xFU << 24),
	PCTL_OFF		= (0xFU << 12),
};

enum nvram_layout_offsets {
	NVR_SIG			= 0x00,		/* 0xAA, 0x55 */
	NVR_SAS_ADDR		= 0x02,		/* 8-byte SAS address */
};

enum chip_flavors {
	chip_6320,
	chip_6440,
	chip_6480,
};

struct mvs_chip_info {
	unsigned int		n_phy;
	unsigned int		srs_sz;
	unsigned int		slot_width;
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

struct mvs_slot_info {
	struct sas_task		*task;
	unsigned int		n_elem;

	/* DMA buffer for storing cmd tbl, open addr frame, status buffer,
	 * and PRD table
	 */
	void			*buf;
	dma_addr_t		buf_dma;

	void			*response;
};

struct mvs_port {
	struct asd_sas_port	sas_port;
};

struct mvs_phy {
	struct mvs_port		*port;
	struct asd_sas_phy	sas_phy;

	u8			frame_rcvd[24 + 1024];
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

	struct mvs_cmd_hdr	*slot;		/* DMA command header slots */
	dma_addr_t		slot_dma;

	const struct mvs_chip_info *chip;

					/* further per-slot information */
	struct mvs_slot_info	slot_info[MVS_SLOTS];
	unsigned long		tags[(MVS_SLOTS / sizeof(unsigned long)) + 1];

	struct mvs_phy		phy[MVS_MAX_PHYS];
	struct mvs_port		port[MVS_MAX_PHYS];
};

static struct scsi_transport_template *mvs_stt;

static const struct mvs_chip_info mvs_chips[] = {
	[chip_6320] =		{ 2, 16, 9 },
	[chip_6440] =		{ 4, 16, 9 },
	[chip_6480] =		{ 8, 32, 10 },
};

static struct scsi_host_template mvs_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= sas_queuecommand,
	.target_alloc		= sas_target_alloc,
	.slave_configure	= sas_slave_configure,
	.slave_destroy		= sas_slave_destroy,
	.change_queue_depth	= sas_change_queue_depth,
	.change_queue_type	= sas_change_queue_type,
	.bios_param		= sas_bios_param,
	.can_queue		= 1,
	.cmd_per_lun		= 1,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.max_sectors		= SCSI_DEFAULT_MAX_SECTORS,
	.use_clustering		= ENABLE_CLUSTERING,
	.eh_device_reset_handler= sas_eh_device_reset_handler,
	.eh_bus_reset_handler	= sas_eh_bus_reset_handler,
	.slave_alloc		= sas_slave_alloc,
	.target_destroy		= sas_target_destroy,
	.ioctl			= sas_ioctl,
};

static void mvs_int_rx(struct mvs_info *mvi, bool self_clear);

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

static void mvs_tag_clear(struct mvs_info *mvi, unsigned int tag)
{
	mvi->tags[tag / sizeof(unsigned long)] &=
		~(1UL << (tag % sizeof(unsigned long)));
}

static void mvs_tag_set(struct mvs_info *mvi, unsigned int tag)
{
	mvi->tags[tag / sizeof(unsigned long)] |=
		(1UL << (tag % sizeof(unsigned long)));
}

static bool mvs_tag_test(struct mvs_info *mvi, unsigned int tag)
{
	return mvi->tags[tag / sizeof(unsigned long)] &
		(1UL << (tag % sizeof(unsigned long)));
}

static int mvs_tag_alloc(struct mvs_info *mvi, unsigned int *tag_out)
{
	unsigned int i;

	for (i = 0; i < MVS_SLOTS; i++)
		if (!mvs_tag_test(mvi, i)) {
			mvs_tag_set(mvi, i);
			*tag_out = i;
			return 0;
		}

	return -EBUSY;
}

static int mvs_eep_read(void __iomem *regs, unsigned int addr, u32 *data)
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

static int mvs_eep_read_buf(void __iomem *regs, unsigned int addr,
			    void *buf, unsigned int buflen)
{
	unsigned int addr_end, tmp_addr, i, j;
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

		tmp8 = (u8 *) &tmp;
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

		tmp8 = (u8 *) &tmp;
		j = addr_end - tmp_addr;
		for (i = 0; i < j; i++)
			*buf8++ = tmp8[i];

		tmp_addr += 4;
	}

	return 0;
}

static int mvs_nvram_read(struct mvs_info *mvi, unsigned int addr,
			  void *buf, unsigned int buflen)
{
	void __iomem *regs = mvi->regs;
	int rc, i;
	unsigned int sum;
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

	if (hdr[0] != 0x5A) {		/* entry id */
		msg = "invalid nvram entry id";
		rc = -ENOENT;
		goto err_out;
	}

	tmp = buf;
	sum = ((unsigned int)hdr[0]) + ((unsigned int)hdr[1]);
	for (i = 0; i < buflen; i++)
		sum += ((unsigned int)tmp[i]);

	if (sum) {
		msg = "nvram checksum failure";
		rc = -EILSEQ;
		goto err_out;
	}

	return 0;

err_out:
	dev_printk(KERN_ERR, &mvi->pdev->dev, "%s", msg);
	return rc;
}

static void mvs_int_port(struct mvs_info *mvi, int port_no, u32 events)
{
	/* FIXME */
}

static void mvs_int_sata(struct mvs_info *mvi)
{
	/* FIXME */
}

static void mvs_slot_free(struct mvs_info *mvi, struct sas_task *task,
			  struct mvs_slot_info *slot, unsigned int slot_idx)
{
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

	mvs_tag_clear(mvi, slot_idx);
}

static void mvs_slot_err(struct mvs_info *mvi, struct sas_task *task,
			 unsigned int slot_idx)
{
	/* FIXME */
}

static void mvs_slot_complete(struct mvs_info *mvi, u32 rx_desc)
{
	unsigned int slot_idx = rx_desc & RXQ_SLOT_MASK;
	struct mvs_slot_info *slot = &mvi->slot_info[slot_idx];
	struct sas_task *task = slot->task;
	struct task_status_struct *tstat = &task->task_status;
	bool aborted;

	spin_lock(&task->task_state_lock);
	aborted = task->task_state_flags & SAS_TASK_STATE_ABORTED;
	if (!aborted) {
		task->task_state_flags &=
			~(SAS_TASK_STATE_PENDING | SAS_TASK_AT_INITIATOR);
		task->task_state_flags |= SAS_TASK_STATE_DONE;
	}
	spin_unlock(&task->task_state_lock);

	if (aborted)
		return;

	memset(tstat, 0, sizeof(*tstat));
	tstat->resp = SAS_TASK_COMPLETE;

	/* error info record present */
	if (rx_desc & RXQ_ERR) {
		tstat->stat = SAM_CHECK_COND;
		mvs_slot_err(mvi, task, slot_idx);
		goto out;
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
		/* hw says status == 0, datapres == 0 */
		if (rx_desc & RXQ_GOOD)
			tstat->stat = SAM_GOOD;

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

	case SAS_PROTOCOL_SMP:
		tstat->stat = SAM_GOOD;
		break;

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
		if ((rx_desc & (RXQ_DONE | RXQ_ERR | RXQ_ATTN)) == RXQ_DONE)
			tstat->stat = SAM_GOOD;
		else
			tstat->stat = SAM_CHECK_COND;
		/* FIXME: read taskfile data from SATA register set
		 * associated with SATA target
		 */
		break;

	default:
		tstat->stat = SAM_CHECK_COND;
		break;
	}

out:
	mvs_slot_free(mvi, task, slot, slot_idx);
	task->task_done(task);
}

static void mvs_int_full(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp, stat;
	int i;

	stat = mr32(INT_STAT);

	for (i = 0; i < MVS_MAX_PORTS; i++) {
		tmp = (stat >> i) & (CINT_PORT | CINT_PORT_STOPPED);
		if (tmp)
			mvs_int_port(mvi, i, tmp);
	}

	if (stat & CINT_SRS)
		mvs_int_sata(mvi);

	if (stat & (CINT_CI_STOP | CINT_DONE))
		mvs_int_rx(mvi, false);

	mw32(INT_STAT, stat);
}

static void mvs_int_rx(struct mvs_info *mvi, bool self_clear)
{
	u32 rx_prod_idx, rx_desc;
	bool attn = false;

	/* the first dword in the RX ring is special: it contains
	 * a mirror of the hardware's RX producer index, so that
	 * we don't have to stall the CPU reading that register.
	 * The actual RX ring is offset by one dword, due to this.
	 */
	rx_prod_idx = le32_to_cpu(mvi->rx[0]) & 0xfff;
	if (rx_prod_idx == 0xfff) {	/* h/w hasn't touched RX ring yet */
		mvi->rx_cons = 0xfff;
		return;
	}
	if (mvi->rx_cons == 0xfff)
		mvi->rx_cons = MVS_RX_RING_SZ - 1;

	while (mvi->rx_cons != rx_prod_idx) {
		/* increment our internal RX consumer pointer */
		mvi->rx_cons = (mvi->rx_cons + 1) & (MVS_RX_RING_SZ - 1);

		/* Read RX descriptor at offset+1, due to above */
		rx_desc = le32_to_cpu(mvi->rx[mvi->rx_cons + 1]);

		if (rx_desc & RXQ_DONE)
			/* we had a completion, error or no */
			mvs_slot_complete(mvi, rx_desc);

		if (rx_desc & RXQ_ATTN)
			attn = true;
	}

	if (attn && self_clear)
		mvs_int_full(mvi);

}

static irqreturn_t mvs_interrupt(int irq, void *opaque)
{
	struct mvs_info *mvi = opaque;
	void __iomem *regs = mvi->regs;
	u32 stat;

	stat = mr32(GBL_INT_STAT);
	if (stat == 0 || stat == 0xffffffff)
		return IRQ_NONE;

	spin_lock(&mvi->lock);

	mvs_int_full(mvi);

	spin_unlock(&mvi->lock);

	return IRQ_HANDLED;
}

static irqreturn_t mvs_msi_interrupt(int irq, void *opaque)
{
	struct mvs_info *mvi = opaque;

	spin_lock(&mvi->lock);

	mvs_int_rx(mvi, true);

	spin_unlock(&mvi->lock);

	return IRQ_HANDLED;
}

struct mvs_task_exec_info {
	struct sas_task		*task;
	struct mvs_cmd_hdr	*hdr;
	unsigned int		tag;
	int			n_elem;
};

static int mvs_task_prep_smp(struct mvs_info *mvi, struct mvs_task_exec_info *tei)
{
	int elem, rc;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct scatterlist *sg_req, *sg_resp;
	unsigned int req_len, resp_len, tag = tei->tag;

	/*
	 * DMA-map SMP request, response buffers
	 */

	sg_req = &tei->task->smp_task.smp_req;
	elem = pci_map_sg(mvi->pdev, sg_req, 1, PCI_DMA_TODEVICE);
	if (!elem)
		return -ENOMEM;
	req_len = sg_dma_len(sg_req);

	sg_resp = &tei->task->smp_task.smp_resp;
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
	 * Fill in TX ring and command slot header
	 */

	mvi->tx[tag] = cpu_to_le32(
		(TXQ_CMD_SMP << TXQ_CMD_SHIFT) | TXQ_MODE_I | tag);

	hdr->flags = 0;
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | (req_len / 4));
	hdr->tags = cpu_to_le32(tag);
	hdr->data_len = 0;
	hdr->cmd_tbl = cpu_to_le64(sg_dma_address(sg_req));
	hdr->open_frame = 0;
	hdr->status_buf = cpu_to_le64(sg_dma_address(sg_resp));
	hdr->prd_tbl = 0;

	return 0;

err_out_2:
	pci_unmap_sg(mvi->pdev, &tei->task->smp_task.smp_resp, 1,
		     PCI_DMA_FROMDEVICE);
err_out:
	pci_unmap_sg(mvi->pdev, &tei->task->smp_task.smp_req, 1,
		     PCI_DMA_TODEVICE);
	return rc;
}

static int mvs_task_prep_ata(struct mvs_info *mvi,
			     struct mvs_task_exec_info *tei)
{
	struct sas_task *task = tei->task;
	struct domain_device *dev = task->dev;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct asd_sas_port *sas_port = dev->port;
	unsigned int tag = tei->tag;
	struct mvs_slot_info *slot = &mvi->slot_info[tag];
	u32 flags = (tei->n_elem << MCH_PRD_LEN_SHIFT);
	struct scatterlist *sg;
	struct mvs_prd *buf_prd;
	void *buf_tmp;
	u8 *buf_cmd, *buf_oaf;
	dma_addr_t buf_tmp_dma;
	unsigned int i, req_len, resp_len;

	/* FIXME: fill in SATA register set */
	mvi->tx[tag] = cpu_to_le32(TXQ_MODE_I | tag |
		(TXQ_CMD_STP << TXQ_CMD_SHIFT) |
		(sas_port->phy_mask << TXQ_PHY_SHIFT));

	if (task->ata_task.use_ncq)
		flags |= MCH_FPDMA;
	if (dev->sata_dev.command_set == ATAPI_COMMAND_SET)
		flags |= MCH_ATAPI;
	/* FIXME: fill in port multiplier number */

	hdr->flags = cpu_to_le32(flags);
	hdr->tags = cpu_to_le32(tag);
	hdr->data_len = cpu_to_le32(task->total_xfer_len);

	/*
	 * arrange MVS_SLOT_BUF_SZ-sized DMA buffer according to our needs
	 */
	memset(slot->buf, 0, MVS_SLOT_BUF_SZ);

	/* region 1: command table area (MVS_ATA_CMD_SZ bytes) ***************/
	buf_cmd =
	buf_tmp = slot->buf;
	buf_tmp_dma = slot->buf_dma;

	hdr->cmd_tbl = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_ATA_CMD_SZ;
	buf_tmp_dma += MVS_ATA_CMD_SZ;

	/* region 2: open address frame area (MVS_OAF_SZ bytes) **********/
	/* used for STP.  unused for SATA? */
	buf_oaf = buf_tmp;
	hdr->open_frame = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_OAF_SZ;
	buf_tmp_dma += MVS_OAF_SZ;

	/* region 3: PRD table ***********************************************/
	buf_prd = buf_tmp;
	hdr->prd_tbl = cpu_to_le64(buf_tmp_dma);

	i = sizeof(struct mvs_prd) * tei->n_elem;
	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ********/
	/* FIXME: probably unused, for SATA.  kept here just in case
	 * we get a STP/SATA error information record
	 */
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);

	req_len = sizeof(struct ssp_frame_hdr) + 28;
	resp_len = MVS_SLOT_BUF_SZ - MVS_ATA_CMD_SZ -
		   sizeof(struct mvs_err_info) - i;

	/* request, response lengths */
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | (req_len / 4));

	/* fill in command FIS and ATAPI CDB */
	memcpy(buf_cmd, &task->ata_task.fis,
	       sizeof(struct host_to_dev_fis));
	memcpy(buf_cmd + 0x40, task->ata_task.atapi_packet, 16);

	/* fill in PRD (scatter/gather) table, if any */
	sg = task->scatter;
	for (i = 0; i < tei->n_elem; i++) {
		buf_prd->addr = cpu_to_le64(sg_dma_address(sg));
		buf_prd->len = cpu_to_le32(sg_dma_len(sg));

		sg++;
		buf_prd++;
	}

	return 0;
}

static int mvs_task_prep_ssp(struct mvs_info *mvi,
			     struct mvs_task_exec_info *tei)
{
	struct sas_task *task = tei->task;
	struct asd_sas_port *sas_port = task->dev->port;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct mvs_slot_info *slot;
	struct scatterlist *sg;
	unsigned int resp_len, req_len, i, tag = tei->tag;
	struct mvs_prd *buf_prd;
	struct ssp_frame_hdr *ssp_hdr;
	void *buf_tmp;
	u8 *buf_cmd, *buf_oaf, fburst = 0;
	dma_addr_t buf_tmp_dma;
	u32 flags;

	slot = &mvi->slot_info[tag];

	mvi->tx[tag] = cpu_to_le32(TXQ_MODE_I | tag |
		(TXQ_CMD_SSP << TXQ_CMD_SHIFT) |
		(sas_port->phy_mask << TXQ_PHY_SHIFT));

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
	memset(slot->buf, 0, MVS_SLOT_BUF_SZ);

	/* region 1: command table area (MVS_SSP_CMD_SZ bytes) ***************/
	buf_cmd =
	buf_tmp = slot->buf;
	buf_tmp_dma = slot->buf_dma;

	hdr->cmd_tbl = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_SSP_CMD_SZ;
	buf_tmp_dma += MVS_SSP_CMD_SZ;

	/* region 2: open address frame area (MVS_OAF_SZ bytes) **********/
	buf_oaf = buf_tmp;
	hdr->open_frame = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_OAF_SZ;
	buf_tmp_dma += MVS_OAF_SZ;

	/* region 3: PRD table ***********************************************/
	buf_prd = buf_tmp;
	hdr->prd_tbl = cpu_to_le64(buf_tmp_dma);

	i = sizeof(struct mvs_prd) * tei->n_elem;
	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ********/
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);

	req_len = sizeof(struct ssp_frame_hdr) + 28;
	resp_len = MVS_SLOT_BUF_SZ - MVS_SSP_CMD_SZ - MVS_OAF_SZ -
		   sizeof(struct mvs_err_info) - i;

	/* request, response lengths */
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | (req_len / 4));

	/* generate open address frame hdr (first 12 bytes) */
	buf_oaf[0] = (1 << 7) | (1 << 4) | 0x1;	/* initiator, SSP, ftype 1h */
	buf_oaf[1] = task->dev->linkrate & 0xf;
	buf_oaf[2] = tag >> 8;
	buf_oaf[3] = tag;
	memcpy(buf_oaf + 4, task->dev->sas_addr, SAS_ADDR_SIZE);

	/* fill in SSP frame header */
	ssp_hdr = (struct ssp_frame_hdr *) buf_cmd;
	ssp_hdr->frame_type = SSP_COMMAND;
	memcpy(ssp_hdr->hashed_dest_addr, task->dev->hashed_sas_addr,
	       HASHED_SAS_ADDR_SIZE);
	memcpy(ssp_hdr->hashed_src_addr,
	       task->dev->port->ha->hashed_sas_addr, HASHED_SAS_ADDR_SIZE);
	ssp_hdr->tag = cpu_to_be16(tag);

	/* fill in command frame IU */
	buf_cmd += sizeof(*ssp_hdr);
	memcpy(buf_cmd, &task->ssp_task.LUN, 8);
	buf_cmd[9] = fburst |
		task->ssp_task.task_attr |
		(task->ssp_task.task_prio << 3);
	memcpy(buf_cmd + 12, &task->ssp_task.cdb, 16);

	/* fill in PRD (scatter/gather) table, if any */
	sg = task->scatter;
	for (i = 0; i < tei->n_elem; i++) {
		buf_prd->addr = cpu_to_le64(sg_dma_address(sg));
		buf_prd->len = cpu_to_le32(sg_dma_len(sg));

		sg++;
		buf_prd++;
	}

	return 0;
}

static int mvs_task_exec(struct sas_task *task, const int num, gfp_t gfp_flags)
{
	struct mvs_info *mvi = task->dev->port->ha->lldd_ha;
	unsigned int tag = 0xdeadbeef, rc, n_elem = 0;
	void __iomem *regs = mvi->regs;
	unsigned long flags;
	struct mvs_task_exec_info tei;

	/* FIXME: STP/SATA support not complete yet */
	if (task->task_proto == SAS_PROTOCOL_SATA || task->task_proto == SAS_PROTOCOL_STP)
		return -SAS_DEV_NO_RESPONSE;

	if (task->num_scatter) {
		n_elem = pci_map_sg(mvi->pdev, task->scatter,
				    task->num_scatter, task->data_dir);
		if (!n_elem)
			return -ENOMEM;
	}

	spin_lock_irqsave(&mvi->lock, flags);

	rc = mvs_tag_alloc(mvi, &tag);
	if (rc)
		goto err_out;

	mvi->slot_info[tag].task = task;
	mvi->slot_info[tag].n_elem = n_elem;
	tei.task = task;
	tei.hdr = &mvi->slot[tag];
	tei.tag = tag;
	tei.n_elem = n_elem;

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		rc = mvs_task_prep_smp(mvi, &tei);
		break;
	case SAS_PROTOCOL_SSP:
		rc = mvs_task_prep_ssp(mvi, &tei);
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
		rc = mvs_task_prep_ata(mvi, &tei);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	if (rc)
		goto err_out_tag;

	/* TODO: select normal or high priority */

	mw32(RX_PROD_IDX, mvi->tx_prod);

	mvi->tx_prod = (mvi->tx_prod + 1) & (MVS_TX_RING_SZ - 1);

	spin_lock(&task->task_state_lock);
	task->task_state_flags |= SAS_TASK_AT_INITIATOR;
	spin_unlock(&task->task_state_lock);

	spin_unlock_irqrestore(&mvi->lock, flags);
	return 0;

err_out_tag:
	mvs_tag_clear(mvi, tag);
err_out:
	if (n_elem)
		pci_unmap_sg(mvi->pdev, task->scatter, n_elem, task->data_dir);
	spin_unlock_irqrestore(&mvi->lock, flags);
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
				  sizeof(*mvi->tx) * MVS_TX_RING_SZ,
				  mvi->tx, mvi->tx_dma);
	if (mvi->rx_fis)
		dma_free_coherent(&mvi->pdev->dev, MVS_RX_FISL_SZ,
				  mvi->rx_fis, mvi->rx_fis_dma);
	if (mvi->rx)
		dma_free_coherent(&mvi->pdev->dev,
				  sizeof(*mvi->rx) * MVS_RX_RING_SZ,
				  mvi->rx, mvi->rx_dma);
	if (mvi->slot)
		dma_free_coherent(&mvi->pdev->dev,
				  sizeof(*mvi->slot) * MVS_RX_RING_SZ,
				  mvi->slot, mvi->slot_dma);
	if (mvi->peri_regs)
		iounmap(mvi->peri_regs);
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
	void __iomem *reg;
	int rc = 0, phy_id = sas_phy->id;
	u32 tmp;

	reg = mvi->regs + MVS_P0_SER_CTLSTAT + (phy_id * 4);

	switch (func) {
	case PHY_FUNC_SET_LINK_RATE: {
		struct sas_phy_linkrates *rates = funcdata;
		u32 lrmin = 0, lrmax = 0;

		lrmin = (rates->minimum_linkrate << 8);
		lrmax = (rates->maximum_linkrate << 12);

		tmp = readl(reg);
		if (lrmin) {
			tmp &= ~(0xf << 8);
			tmp |= lrmin;
		}
		if (lrmax) {
			tmp &= ~(0xf << 12);
			tmp |= lrmax;
		}
		writel(tmp, reg);
		break;
	}

	case PHY_FUNC_HARD_RESET:
		tmp = readl(reg);
		if (tmp & PHY_RST_HARD)
			break;
		writel(tmp | PHY_RST_HARD, reg);
		break;

	case PHY_FUNC_LINK_RESET:
		writel(readl(reg) | PHY_RST, reg);
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

static struct mvs_info * __devinit mvs_alloc(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct mvs_info *mvi;
	unsigned long res_start, res_len;
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
	}

	SHOST_TO_SAS_HA(mvi->shost) = &mvi->sas;
	mvi->shost->transportt = mvs_stt;
	mvi->shost->max_id = ~0;
	mvi->shost->max_lun = ~0;
	mvi->shost->max_cmd_len = ~0;

	mvi->sas.sas_ha_name = DRV_NAME;
	mvi->sas.dev = &pdev->dev;
	mvi->sas.lldd_module = THIS_MODULE;
	mvi->sas.sas_addr = &mvi->sas_addr[0];
	mvi->sas.sas_phy = arr_phy;
	mvi->sas.sas_port = arr_port;
	mvi->sas.num_phys = chip->n_phy;
	mvi->sas.lldd_max_execute_num = MVS_TX_RING_SZ - 1;/* FIXME: correct? */
	mvi->sas.lldd_queue_size = MVS_TX_RING_SZ - 1;	   /* FIXME: correct? */
	mvi->sas.lldd_ha = mvi;
	mvi->sas.core.shost = mvi->shost;

	mvs_tag_set(mvi, MVS_TX_RING_SZ - 1);

	/*
	 * ioremap main and peripheral registers
	 */

	res_start = pci_resource_start(pdev, 2);
	res_len = pci_resource_len(pdev, 2);
	if (!res_start || !res_len)
		goto err_out;

	mvi->peri_regs = ioremap_nocache(res_start, res_len);
	if (!mvi->regs)
		goto err_out;

	res_start = pci_resource_start(pdev, 4);
	res_len = pci_resource_len(pdev, 4);
	if (!res_start || !res_len)
		goto err_out;

	mvi->regs = ioremap_nocache(res_start, res_len);
	if (!mvi->regs)
		goto err_out;

	/*
	 * alloc and init our DMA areas
	 */

	mvi->tx = dma_alloc_coherent(&pdev->dev,
				     sizeof(*mvi->tx) * MVS_TX_RING_SZ,
				     &mvi->tx_dma, GFP_KERNEL);
	if (!mvi->tx)
		goto err_out;
	memset(mvi->tx, 0, sizeof(*mvi->tx) * MVS_TX_RING_SZ);

	mvi->rx_fis = dma_alloc_coherent(&pdev->dev, MVS_RX_FISL_SZ,
				     &mvi->rx_fis_dma, GFP_KERNEL);
	if (!mvi->rx_fis)
		goto err_out;
	memset(mvi->rx_fis, 0, MVS_RX_FISL_SZ);

	mvi->rx = dma_alloc_coherent(&pdev->dev,
				     sizeof(*mvi->rx) * MVS_RX_RING_SZ,
				     &mvi->rx_dma, GFP_KERNEL);
	if (!mvi->rx)
		goto err_out;
	memset(mvi->rx, 0, sizeof(*mvi->rx) * MVS_RX_RING_SZ);

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

#if 0
static u32 mvs_phy_read(struct mvs_info *mvi, unsigned int phy_id, u32 addr)
{
	void __iomem *regs = mvi->regs;
	void __iomem *phy_regs = regs + MVS_P0_CFG_ADDR + (phy_id * 8);

	writel(addr, phy_regs);
	return readl(phy_regs + 4);
}
#endif

static void mvs_phy_write(struct mvs_info *mvi, unsigned int phy_id,
			  u32 addr, u32 val)
{
	void __iomem *regs = mvi->regs;
	void __iomem *phy_regs = regs + MVS_P0_CFG_ADDR + (phy_id * 8);

	writel(addr, phy_regs);
	writel(val, phy_regs + 4);
	readl(phy_regs);	/* flush */
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
	mvs_cw32(regs, CMD_WD_TIMER, 0xffffff);

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
}

static int __devinit mvs_hw_init(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	int i;
	u32 tmp, cctl;

	/* make sure interrupts are masked immediately (paranoia) */
	mw32(GBL_CTL, 0);
	tmp = mr32(GBL_CTL);

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

	/* make sure RST is set; HBA_RST /should/ have done that for us */
	cctl = mr32(CTL);
	if (cctl & CCTL_RST)
		cctl &= ~CCTL_RST;
	else
		mw32_f(CTL, cctl | CCTL_RST);

	pci_read_config_dword(mvi->pdev, PCR_PHY_CTL, &tmp);
	tmp |= PCTL_PWR_ON;
	tmp &= ~PCTL_OFF;
	pci_write_config_dword(mvi->pdev, PCR_PHY_CTL, tmp);

	pci_read_config_dword(mvi->pdev, PCR_PHY_CTL2, &tmp);
	tmp |= PCTL_PWR_ON;
	tmp &= ~PCTL_OFF;
	pci_write_config_dword(mvi->pdev, PCR_PHY_CTL2, tmp);

	mw32_f(CTL, cctl);

	mvs_phy_hacks(mvi);

	mw32(CMD_LIST_LO, mvi->slot_dma);
	mw32(CMD_LIST_HI, (mvi->slot_dma >> 16) >> 16);

	mw32(RX_FIS_LO, mvi->rx_fis_dma);
	mw32(RX_FIS_HI, (mvi->rx_fis_dma >> 16) >> 16);

	mw32(TX_CFG, MVS_TX_RING_SZ);
	mw32(TX_LO, mvi->tx_dma);
	mw32(TX_HI, (mvi->tx_dma >> 16) >> 16);

	mw32(RX_CFG, MVS_RX_RING_SZ);
	mw32(RX_LO, mvi->rx_dma);
	mw32(RX_HI, (mvi->rx_dma >> 16) >> 16);

	/* init and reset phys */
	for (i = 0; i < mvi->chip->n_phy; i++) {
		/* FIXME: is this the correct dword order? */
		u32 lo = *((u32 *) &mvi->sas_addr[0]);
		u32 hi = *((u32 *) &mvi->sas_addr[4]);

		/* set phy local SAS address */
		mvs_phy_write(mvi, i, PHYR_ADDR_LO, lo);
		mvs_phy_write(mvi, i, PHYR_ADDR_HI, hi);

		/* reset phy */
		tmp = readl(regs + MVS_P0_SER_CTLSTAT + (i * 4));
		tmp |= PHY_RST;
		writel(tmp, regs + MVS_P0_SER_CTLSTAT + (i * 4));
	}

	msleep(100);

	for (i = 0; i < mvi->chip->n_phy; i++) {
		/* set phy int mask */
		writel(PHYEV_BROAD_CH | PHYEV_RDY_CH,
		       regs + MVS_P0_INT_MASK + (i * 8));

		/* clear phy int status */
		tmp = readl(regs + MVS_P0_INT_STAT + (i * 8));
		writel(tmp, regs + MVS_P0_INT_STAT + (i * 8));
	}

	/* FIXME: update wide port bitmaps */

	/* ladies and gentlemen, start your engines */
	mw32(TX_CFG, MVS_TX_RING_SZ | TX_EN);
	mw32(RX_CFG, MVS_RX_RING_SZ | RX_EN);
	mw32(PCS, PCS_SATA_RETRY | PCS_FIS_RX_EN | PCS_CMD_EN |
	     ((mvi->flags & MVF_MSI) ? PCS_SELF_CLEAR : 0));

	/* re-enable interrupts globally */
	mw32(GBL_CTL, INT_EN);

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

	if (!pci_enable_msi(pdev)) {
		mvi->flags |= MVF_MSI;
		irq_handler = mvs_msi_interrupt;
	}

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

	sas_unregister_ha(&mvi->sas);
	sas_remove_host(mvi->shost);
	scsi_remove_host(mvi->shost);

	free_irq(pdev->irq, mvi);
	if (mvi->flags & MVF_MSI)
		pci_disable_msi(pdev);
	mvs_free(mvi);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct sas_domain_function_template mvs_transport_ops = {
	.lldd_execute_task	= mvs_task_exec,
	.lldd_control_phy	= mvs_phy_control,
};

static struct pci_device_id __devinitdata mvs_pci_table[] = {
	{ PCI_VDEVICE(MARVELL, 0x6320), chip_6320 },
	{ PCI_VDEVICE(MARVELL, 0x6340), chip_6440 },
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

