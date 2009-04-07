/*
 * sata_sil24.c - Driver for Silicon Image 3124/3132 SATA-2 controllers
 *
 * Copyright 2005  Tejun Heo
 *
 * Based on preview driver from Silicon Image.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/libata.h>

#define DRV_NAME	"sata_sil24"
#define DRV_VERSION	"1.1"

/*
 * Port request block (PRB) 32 bytes
 */
struct sil24_prb {
	__le16	ctrl;
	__le16	prot;
	__le32	rx_cnt;
	u8	fis[6 * 4];
};

/*
 * Scatter gather entry (SGE) 16 bytes
 */
struct sil24_sge {
	__le64	addr;
	__le32	cnt;
	__le32	flags;
};


enum {
	SIL24_HOST_BAR		= 0,
	SIL24_PORT_BAR		= 2,

	/* sil24 fetches in chunks of 64bytes.  The first block
	 * contains the PRB and two SGEs.  From the second block, it's
	 * consisted of four SGEs and called SGT.  Calculate the
	 * number of SGTs that fit into one page.
	 */
	SIL24_PRB_SZ		= sizeof(struct sil24_prb)
				  + 2 * sizeof(struct sil24_sge),
	SIL24_MAX_SGT		= (PAGE_SIZE - SIL24_PRB_SZ)
				  / (4 * sizeof(struct sil24_sge)),

	/* This will give us one unused SGEs for ATA.  This extra SGE
	 * will be used to store CDB for ATAPI devices.
	 */
	SIL24_MAX_SGE		= 4 * SIL24_MAX_SGT + 1,

	/*
	 * Global controller registers (128 bytes @ BAR0)
	 */
		/* 32 bit regs */
	HOST_SLOT_STAT		= 0x00, /* 32 bit slot stat * 4 */
	HOST_CTRL		= 0x40,
	HOST_IRQ_STAT		= 0x44,
	HOST_PHY_CFG		= 0x48,
	HOST_BIST_CTRL		= 0x50,
	HOST_BIST_PTRN		= 0x54,
	HOST_BIST_STAT		= 0x58,
	HOST_MEM_BIST_STAT	= 0x5c,
	HOST_FLASH_CMD		= 0x70,
		/* 8 bit regs */
	HOST_FLASH_DATA		= 0x74,
	HOST_TRANSITION_DETECT	= 0x75,
	HOST_GPIO_CTRL		= 0x76,
	HOST_I2C_ADDR		= 0x78, /* 32 bit */
	HOST_I2C_DATA		= 0x7c,
	HOST_I2C_XFER_CNT	= 0x7e,
	HOST_I2C_CTRL		= 0x7f,

	/* HOST_SLOT_STAT bits */
	HOST_SSTAT_ATTN		= (1 << 31),

	/* HOST_CTRL bits */
	HOST_CTRL_M66EN		= (1 << 16), /* M66EN PCI bus signal */
	HOST_CTRL_TRDY		= (1 << 17), /* latched PCI TRDY */
	HOST_CTRL_STOP		= (1 << 18), /* latched PCI STOP */
	HOST_CTRL_DEVSEL	= (1 << 19), /* latched PCI DEVSEL */
	HOST_CTRL_REQ64		= (1 << 20), /* latched PCI REQ64 */
	HOST_CTRL_GLOBAL_RST	= (1 << 31), /* global reset */

	/*
	 * Port registers
	 * (8192 bytes @ +0x0000, +0x2000, +0x4000 and +0x6000 @ BAR2)
	 */
	PORT_REGS_SIZE		= 0x2000,

	PORT_LRAM		= 0x0000, /* 31 LRAM slots and PMP regs */
	PORT_LRAM_SLOT_SZ	= 0x0080, /* 32 bytes PRB + 2 SGE, ACT... */

	PORT_PMP		= 0x0f80, /* 8 bytes PMP * 16 (128 bytes) */
	PORT_PMP_STATUS		= 0x0000, /* port device status offset */
	PORT_PMP_QACTIVE	= 0x0004, /* port device QActive offset */
	PORT_PMP_SIZE		= 0x0008, /* 8 bytes per PMP */

		/* 32 bit regs */
	PORT_CTRL_STAT		= 0x1000, /* write: ctrl-set, read: stat */
	PORT_CTRL_CLR		= 0x1004, /* write: ctrl-clear */
	PORT_IRQ_STAT		= 0x1008, /* high: status, low: interrupt */
	PORT_IRQ_ENABLE_SET	= 0x1010, /* write: enable-set */
	PORT_IRQ_ENABLE_CLR	= 0x1014, /* write: enable-clear */
	PORT_ACTIVATE_UPPER_ADDR= 0x101c,
	PORT_EXEC_FIFO		= 0x1020, /* command execution fifo */
	PORT_CMD_ERR		= 0x1024, /* command error number */
	PORT_FIS_CFG		= 0x1028,
	PORT_FIFO_THRES		= 0x102c,
		/* 16 bit regs */
	PORT_DECODE_ERR_CNT	= 0x1040,
	PORT_DECODE_ERR_THRESH	= 0x1042,
	PORT_CRC_ERR_CNT	= 0x1044,
	PORT_CRC_ERR_THRESH	= 0x1046,
	PORT_HSHK_ERR_CNT	= 0x1048,
	PORT_HSHK_ERR_THRESH	= 0x104a,
		/* 32 bit regs */
	PORT_PHY_CFG		= 0x1050,
	PORT_SLOT_STAT		= 0x1800,
	PORT_CMD_ACTIVATE	= 0x1c00, /* 64 bit cmd activate * 31 (248 bytes) */
	PORT_CONTEXT		= 0x1e04,
	PORT_EXEC_DIAG		= 0x1e00, /* 32bit exec diag * 16 (64 bytes, 0-10 used on 3124) */
	PORT_PSD_DIAG		= 0x1e40, /* 32bit psd diag * 16 (64 bytes, 0-8 used on 3124) */
	PORT_SCONTROL		= 0x1f00,
	PORT_SSTATUS		= 0x1f04,
	PORT_SERROR		= 0x1f08,
	PORT_SACTIVE		= 0x1f0c,

	/* PORT_CTRL_STAT bits */
	PORT_CS_PORT_RST	= (1 << 0), /* port reset */
	PORT_CS_DEV_RST		= (1 << 1), /* device reset */
	PORT_CS_INIT		= (1 << 2), /* port initialize */
	PORT_CS_IRQ_WOC		= (1 << 3), /* interrupt write one to clear */
	PORT_CS_CDB16		= (1 << 5), /* 0=12b cdb, 1=16b cdb */
	PORT_CS_PMP_RESUME	= (1 << 6), /* PMP resume */
	PORT_CS_32BIT_ACTV	= (1 << 10), /* 32-bit activation */
	PORT_CS_PMP_EN		= (1 << 13), /* port multiplier enable */
	PORT_CS_RDY		= (1 << 31), /* port ready to accept commands */

	/* PORT_IRQ_STAT/ENABLE_SET/CLR */
	/* bits[11:0] are masked */
	PORT_IRQ_COMPLETE	= (1 << 0), /* command(s) completed */
	PORT_IRQ_ERROR		= (1 << 1), /* command execution error */
	PORT_IRQ_PORTRDY_CHG	= (1 << 2), /* port ready change */
	PORT_IRQ_PWR_CHG	= (1 << 3), /* power management change */
	PORT_IRQ_PHYRDY_CHG	= (1 << 4), /* PHY ready change */
	PORT_IRQ_COMWAKE	= (1 << 5), /* COMWAKE received */
	PORT_IRQ_UNK_FIS	= (1 << 6), /* unknown FIS received */
	PORT_IRQ_DEV_XCHG	= (1 << 7), /* device exchanged */
	PORT_IRQ_8B10B		= (1 << 8), /* 8b/10b decode error threshold */
	PORT_IRQ_CRC		= (1 << 9), /* CRC error threshold */
	PORT_IRQ_HANDSHAKE	= (1 << 10), /* handshake error threshold */
	PORT_IRQ_SDB_NOTIFY	= (1 << 11), /* SDB notify received */

	DEF_PORT_IRQ		= PORT_IRQ_COMPLETE | PORT_IRQ_ERROR |
				  PORT_IRQ_PHYRDY_CHG | PORT_IRQ_DEV_XCHG |
				  PORT_IRQ_UNK_FIS | PORT_IRQ_SDB_NOTIFY,

	/* bits[27:16] are unmasked (raw) */
	PORT_IRQ_RAW_SHIFT	= 16,
	PORT_IRQ_MASKED_MASK	= 0x7ff,
	PORT_IRQ_RAW_MASK	= (0x7ff << PORT_IRQ_RAW_SHIFT),

	/* ENABLE_SET/CLR specific, intr steering - 2 bit field */
	PORT_IRQ_STEER_SHIFT	= 30,
	PORT_IRQ_STEER_MASK	= (3 << PORT_IRQ_STEER_SHIFT),

	/* PORT_CMD_ERR constants */
	PORT_CERR_DEV		= 1, /* Error bit in D2H Register FIS */
	PORT_CERR_SDB		= 2, /* Error bit in SDB FIS */
	PORT_CERR_DATA		= 3, /* Error in data FIS not detected by dev */
	PORT_CERR_SEND		= 4, /* Initial cmd FIS transmission failure */
	PORT_CERR_INCONSISTENT	= 5, /* Protocol mismatch */
	PORT_CERR_DIRECTION	= 6, /* Data direction mismatch */
	PORT_CERR_UNDERRUN	= 7, /* Ran out of SGEs while writing */
	PORT_CERR_OVERRUN	= 8, /* Ran out of SGEs while reading */
	PORT_CERR_PKT_PROT	= 11, /* DIR invalid in 1st PIO setup of ATAPI */
	PORT_CERR_SGT_BOUNDARY	= 16, /* PLD ecode 00 - SGT not on qword boundary */
	PORT_CERR_SGT_TGTABRT	= 17, /* PLD ecode 01 - target abort */
	PORT_CERR_SGT_MSTABRT	= 18, /* PLD ecode 10 - master abort */
	PORT_CERR_SGT_PCIPERR	= 19, /* PLD ecode 11 - PCI parity err while fetching SGT */
	PORT_CERR_CMD_BOUNDARY	= 24, /* ctrl[15:13] 001 - PRB not on qword boundary */
	PORT_CERR_CMD_TGTABRT	= 25, /* ctrl[15:13] 010 - target abort */
	PORT_CERR_CMD_MSTABRT	= 26, /* ctrl[15:13] 100 - master abort */
	PORT_CERR_CMD_PCIPERR	= 27, /* ctrl[15:13] 110 - PCI parity err while fetching PRB */
	PORT_CERR_XFR_UNDEF	= 32, /* PSD ecode 00 - undefined */
	PORT_CERR_XFR_TGTABRT	= 33, /* PSD ecode 01 - target abort */
	PORT_CERR_XFR_MSTABRT	= 34, /* PSD ecode 10 - master abort */
	PORT_CERR_XFR_PCIPERR	= 35, /* PSD ecode 11 - PCI prity err during transfer */
	PORT_CERR_SENDSERVICE	= 36, /* FIS received while sending service */

	/* bits of PRB control field */
	PRB_CTRL_PROTOCOL	= (1 << 0), /* override def. ATA protocol */
	PRB_CTRL_PACKET_READ	= (1 << 4), /* PACKET cmd read */
	PRB_CTRL_PACKET_WRITE	= (1 << 5), /* PACKET cmd write */
	PRB_CTRL_NIEN		= (1 << 6), /* Mask completion irq */
	PRB_CTRL_SRST		= (1 << 7), /* Soft reset request (ign BSY?) */

	/* PRB protocol field */
	PRB_PROT_PACKET		= (1 << 0),
	PRB_PROT_TCQ		= (1 << 1),
	PRB_PROT_NCQ		= (1 << 2),
	PRB_PROT_READ		= (1 << 3),
	PRB_PROT_WRITE		= (1 << 4),
	PRB_PROT_TRANSPARENT	= (1 << 5),

	/*
	 * Other constants
	 */
	SGE_TRM			= (1 << 31), /* Last SGE in chain */
	SGE_LNK			= (1 << 30), /* linked list
						Points to SGT, not SGE */
	SGE_DRD			= (1 << 29), /* discard data read (/dev/null)
						data address ignored */

	SIL24_MAX_CMDS		= 31,

	/* board id */
	BID_SIL3124		= 0,
	BID_SIL3132		= 1,
	BID_SIL3131		= 2,

	/* host flags */
	SIL24_COMMON_FLAGS	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | ATA_FLAG_PIO_DMA |
				  ATA_FLAG_NCQ | ATA_FLAG_ACPI_SATA |
				  ATA_FLAG_AN | ATA_FLAG_PMP,
	SIL24_FLAG_PCIX_IRQ_WOC	= (1 << 24), /* IRQ loss errata on PCI-X */

	IRQ_STAT_4PORTS		= 0xf,
};

struct sil24_ata_block {
	struct sil24_prb prb;
	struct sil24_sge sge[SIL24_MAX_SGE];
};

struct sil24_atapi_block {
	struct sil24_prb prb;
	u8 cdb[16];
	struct sil24_sge sge[SIL24_MAX_SGE];
};

union sil24_cmd_block {
	struct sil24_ata_block ata;
	struct sil24_atapi_block atapi;
};

static struct sil24_cerr_info {
	unsigned int err_mask, action;
	const char *desc;
} sil24_cerr_db[] = {
	[0]			= { AC_ERR_DEV, 0,
				    "device error" },
	[PORT_CERR_DEV]		= { AC_ERR_DEV, 0,
				    "device error via D2H FIS" },
	[PORT_CERR_SDB]		= { AC_ERR_DEV, 0,
				    "device error via SDB FIS" },
	[PORT_CERR_DATA]	= { AC_ERR_ATA_BUS, ATA_EH_RESET,
				    "error in data FIS" },
	[PORT_CERR_SEND]	= { AC_ERR_ATA_BUS, ATA_EH_RESET,
				    "failed to transmit command FIS" },
	[PORT_CERR_INCONSISTENT] = { AC_ERR_HSM, ATA_EH_RESET,
				     "protocol mismatch" },
	[PORT_CERR_DIRECTION]	= { AC_ERR_HSM, ATA_EH_RESET,
				    "data directon mismatch" },
	[PORT_CERR_UNDERRUN]	= { AC_ERR_HSM, ATA_EH_RESET,
				    "ran out of SGEs while writing" },
	[PORT_CERR_OVERRUN]	= { AC_ERR_HSM, ATA_EH_RESET,
				    "ran out of SGEs while reading" },
	[PORT_CERR_PKT_PROT]	= { AC_ERR_HSM, ATA_EH_RESET,
				    "invalid data directon for ATAPI CDB" },
	[PORT_CERR_SGT_BOUNDARY] = { AC_ERR_SYSTEM, ATA_EH_RESET,
				     "SGT not on qword boundary" },
	[PORT_CERR_SGT_TGTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_RESET,
				    "PCI target abort while fetching SGT" },
	[PORT_CERR_SGT_MSTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_RESET,
				    "PCI master abort while fetching SGT" },
	[PORT_CERR_SGT_PCIPERR]	= { AC_ERR_HOST_BUS, ATA_EH_RESET,
				    "PCI parity error while fetching SGT" },
	[PORT_CERR_CMD_BOUNDARY] = { AC_ERR_SYSTEM, ATA_EH_RESET,
				     "PRB not on qword boundary" },
	[PORT_CERR_CMD_TGTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_RESET,
				    "PCI target abort while fetching PRB" },
	[PORT_CERR_CMD_MSTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_RESET,
				    "PCI master abort while fetching PRB" },
	[PORT_CERR_CMD_PCIPERR]	= { AC_ERR_HOST_BUS, ATA_EH_RESET,
				    "PCI parity error while fetching PRB" },
	[PORT_CERR_XFR_UNDEF]	= { AC_ERR_HOST_BUS, ATA_EH_RESET,
				    "undefined error while transferring data" },
	[PORT_CERR_XFR_TGTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_RESET,
				    "PCI target abort while transferring data" },
	[PORT_CERR_XFR_MSTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_RESET,
				    "PCI master abort while transferring data" },
	[PORT_CERR_XFR_PCIPERR]	= { AC_ERR_HOST_BUS, ATA_EH_RESET,
				    "PCI parity error while transferring data" },
	[PORT_CERR_SENDSERVICE]	= { AC_ERR_HSM, ATA_EH_RESET,
				    "FIS received while sending service FIS" },
};

/*
 * ap->private_data
 *
 * The preview driver always returned 0 for status.  We emulate it
 * here from the previous interrupt.
 */
struct sil24_port_priv {
	union sil24_cmd_block *cmd_block;	/* 32 cmd blocks */
	dma_addr_t cmd_block_dma;		/* DMA base addr for them */
	int do_port_rst;
};

static void sil24_dev_config(struct ata_device *dev);
static int sil24_scr_read(struct ata_link *link, unsigned sc_reg, u32 *val);
static int sil24_scr_write(struct ata_link *link, unsigned sc_reg, u32 val);
static int sil24_qc_defer(struct ata_queued_cmd *qc);
static void sil24_qc_prep(struct ata_queued_cmd *qc);
static unsigned int sil24_qc_issue(struct ata_queued_cmd *qc);
static bool sil24_qc_fill_rtf(struct ata_queued_cmd *qc);
static void sil24_pmp_attach(struct ata_port *ap);
static void sil24_pmp_detach(struct ata_port *ap);
static void sil24_freeze(struct ata_port *ap);
static void sil24_thaw(struct ata_port *ap);
static int sil24_softreset(struct ata_link *link, unsigned int *class,
			   unsigned long deadline);
static int sil24_hardreset(struct ata_link *link, unsigned int *class,
			   unsigned long deadline);
static int sil24_pmp_hardreset(struct ata_link *link, unsigned int *class,
			       unsigned long deadline);
static void sil24_error_handler(struct ata_port *ap);
static void sil24_post_internal_cmd(struct ata_queued_cmd *qc);
static int sil24_port_start(struct ata_port *ap);
static int sil24_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
#ifdef CONFIG_PM
static int sil24_pci_device_resume(struct pci_dev *pdev);
static int sil24_port_resume(struct ata_port *ap);
#endif

static const struct pci_device_id sil24_pci_tbl[] = {
	{ PCI_VDEVICE(CMD, 0x3124), BID_SIL3124 },
	{ PCI_VDEVICE(INTEL, 0x3124), BID_SIL3124 },
	{ PCI_VDEVICE(CMD, 0x3132), BID_SIL3132 },
	{ PCI_VDEVICE(CMD, 0x0242), BID_SIL3132 },
	{ PCI_VDEVICE(CMD, 0x0244), BID_SIL3132 },
	{ PCI_VDEVICE(CMD, 0x3131), BID_SIL3131 },
	{ PCI_VDEVICE(CMD, 0x3531), BID_SIL3131 },

	{ } /* terminate list */
};

static struct pci_driver sil24_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= sil24_pci_tbl,
	.probe			= sil24_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= ata_pci_device_suspend,
	.resume			= sil24_pci_device_resume,
#endif
};

static struct scsi_host_template sil24_sht = {
	ATA_NCQ_SHT(DRV_NAME),
	.can_queue		= SIL24_MAX_CMDS,
	.sg_tablesize		= SIL24_MAX_SGE,
	.dma_boundary		= ATA_DMA_BOUNDARY,
};

static struct ata_port_operations sil24_ops = {
	.inherits		= &sata_pmp_port_ops,

	.qc_defer		= sil24_qc_defer,
	.qc_prep		= sil24_qc_prep,
	.qc_issue		= sil24_qc_issue,
	.qc_fill_rtf		= sil24_qc_fill_rtf,

	.freeze			= sil24_freeze,
	.thaw			= sil24_thaw,
	.softreset		= sil24_softreset,
	.hardreset		= sil24_hardreset,
	.pmp_softreset		= sil24_softreset,
	.pmp_hardreset		= sil24_pmp_hardreset,
	.error_handler		= sil24_error_handler,
	.post_internal_cmd	= sil24_post_internal_cmd,
	.dev_config		= sil24_dev_config,

	.scr_read		= sil24_scr_read,
	.scr_write		= sil24_scr_write,
	.pmp_attach		= sil24_pmp_attach,
	.pmp_detach		= sil24_pmp_detach,

	.port_start		= sil24_port_start,
#ifdef CONFIG_PM
	.port_resume		= sil24_port_resume,
#endif
};

/*
 * Use bits 30-31 of port_flags to encode available port numbers.
 * Current maxium is 4.
 */
#define SIL24_NPORTS2FLAG(nports)	((((unsigned)(nports) - 1) & 0x3) << 30)
#define SIL24_FLAG2NPORTS(flag)		((((flag) >> 30) & 0x3) + 1)

static const struct ata_port_info sil24_port_info[] = {
	/* sil_3124 */
	{
		.flags		= SIL24_COMMON_FLAGS | SIL24_NPORTS2FLAG(4) |
				  SIL24_FLAG_PCIX_IRQ_WOC,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA5,
		.port_ops	= &sil24_ops,
	},
	/* sil_3132 */
	{
		.flags		= SIL24_COMMON_FLAGS | SIL24_NPORTS2FLAG(2),
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA5,
		.port_ops	= &sil24_ops,
	},
	/* sil_3131/sil_3531 */
	{
		.flags		= SIL24_COMMON_FLAGS | SIL24_NPORTS2FLAG(1),
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA5,
		.port_ops	= &sil24_ops,
	},
};

static int sil24_tag(int tag)
{
	if (unlikely(ata_tag_internal(tag)))
		return 0;
	return tag;
}

static unsigned long sil24_port_offset(struct ata_port *ap)
{
	return ap->port_no * PORT_REGS_SIZE;
}

static void __iomem *sil24_port_base(struct ata_port *ap)
{
	return ap->host->iomap[SIL24_PORT_BAR] + sil24_port_offset(ap);
}

static void sil24_dev_config(struct ata_device *dev)
{
	void __iomem *port = sil24_port_base(dev->link->ap);

	if (dev->cdb_len == 16)
		writel(PORT_CS_CDB16, port + PORT_CTRL_STAT);
	else
		writel(PORT_CS_CDB16, port + PORT_CTRL_CLR);
}

static void sil24_read_tf(struct ata_port *ap, int tag, struct ata_taskfile *tf)
{
	void __iomem *port = sil24_port_base(ap);
	struct sil24_prb __iomem *prb;
	u8 fis[6 * 4];

	prb = port + PORT_LRAM + sil24_tag(tag) * PORT_LRAM_SLOT_SZ;
	memcpy_fromio(fis, prb->fis, sizeof(fis));
	ata_tf_from_fis(fis, tf);
}

static int sil24_scr_map[] = {
	[SCR_CONTROL]	= 0,
	[SCR_STATUS]	= 1,
	[SCR_ERROR]	= 2,
	[SCR_ACTIVE]	= 3,
};

static int sil24_scr_read(struct ata_link *link, unsigned sc_reg, u32 *val)
{
	void __iomem *scr_addr = sil24_port_base(link->ap) + PORT_SCONTROL;

	if (sc_reg < ARRAY_SIZE(sil24_scr_map)) {
		void __iomem *addr;
		addr = scr_addr + sil24_scr_map[sc_reg] * 4;
		*val = readl(scr_addr + sil24_scr_map[sc_reg] * 4);
		return 0;
	}
	return -EINVAL;
}

static int sil24_scr_write(struct ata_link *link, unsigned sc_reg, u32 val)
{
	void __iomem *scr_addr = sil24_port_base(link->ap) + PORT_SCONTROL;

	if (sc_reg < ARRAY_SIZE(sil24_scr_map)) {
		void __iomem *addr;
		addr = scr_addr + sil24_scr_map[sc_reg] * 4;
		writel(val, scr_addr + sil24_scr_map[sc_reg] * 4);
		return 0;
	}
	return -EINVAL;
}

static void sil24_config_port(struct ata_port *ap)
{
	void __iomem *port = sil24_port_base(ap);

	/* configure IRQ WoC */
	if (ap->flags & SIL24_FLAG_PCIX_IRQ_WOC)
		writel(PORT_CS_IRQ_WOC, port + PORT_CTRL_STAT);
	else
		writel(PORT_CS_IRQ_WOC, port + PORT_CTRL_CLR);

	/* zero error counters. */
	writel(0x8000, port + PORT_DECODE_ERR_THRESH);
	writel(0x8000, port + PORT_CRC_ERR_THRESH);
	writel(0x8000, port + PORT_HSHK_ERR_THRESH);
	writel(0x0000, port + PORT_DECODE_ERR_CNT);
	writel(0x0000, port + PORT_CRC_ERR_CNT);
	writel(0x0000, port + PORT_HSHK_ERR_CNT);

	/* always use 64bit activation */
	writel(PORT_CS_32BIT_ACTV, port + PORT_CTRL_CLR);

	/* clear port multiplier enable and resume bits */
	writel(PORT_CS_PMP_EN | PORT_CS_PMP_RESUME, port + PORT_CTRL_CLR);
}

static void sil24_config_pmp(struct ata_port *ap, int attached)
{
	void __iomem *port = sil24_port_base(ap);

	if (attached)
		writel(PORT_CS_PMP_EN, port + PORT_CTRL_STAT);
	else
		writel(PORT_CS_PMP_EN, port + PORT_CTRL_CLR);
}

static void sil24_clear_pmp(struct ata_port *ap)
{
	void __iomem *port = sil24_port_base(ap);
	int i;

	writel(PORT_CS_PMP_RESUME, port + PORT_CTRL_CLR);

	for (i = 0; i < SATA_PMP_MAX_PORTS; i++) {
		void __iomem *pmp_base = port + PORT_PMP + i * PORT_PMP_SIZE;

		writel(0, pmp_base + PORT_PMP_STATUS);
		writel(0, pmp_base + PORT_PMP_QACTIVE);
	}
}

static int sil24_init_port(struct ata_port *ap)
{
	void __iomem *port = sil24_port_base(ap);
	struct sil24_port_priv *pp = ap->private_data;
	u32 tmp;

	/* clear PMP error status */
	if (sata_pmp_attached(ap))
		sil24_clear_pmp(ap);

	writel(PORT_CS_INIT, port + PORT_CTRL_STAT);
	ata_wait_register(port + PORT_CTRL_STAT,
			  PORT_CS_INIT, PORT_CS_INIT, 10, 100);
	tmp = ata_wait_register(port + PORT_CTRL_STAT,
				PORT_CS_RDY, 0, 10, 100);

	if ((tmp & (PORT_CS_INIT | PORT_CS_RDY)) != PORT_CS_RDY) {
		pp->do_port_rst = 1;
		ap->link.eh_context.i.action |= ATA_EH_RESET;
		return -EIO;
	}

	return 0;
}

static int sil24_exec_polled_cmd(struct ata_port *ap, int pmp,
				 const struct ata_taskfile *tf,
				 int is_cmd, u32 ctrl,
				 unsigned long timeout_msec)
{
	void __iomem *port = sil24_port_base(ap);
	struct sil24_port_priv *pp = ap->private_data;
	struct sil24_prb *prb = &pp->cmd_block[0].ata.prb;
	dma_addr_t paddr = pp->cmd_block_dma;
	u32 irq_enabled, irq_mask, irq_stat;
	int rc;

	prb->ctrl = cpu_to_le16(ctrl);
	ata_tf_to_fis(tf, pmp, is_cmd, prb->fis);

	/* temporarily plug completion and error interrupts */
	irq_enabled = readl(port + PORT_IRQ_ENABLE_SET);
	writel(PORT_IRQ_COMPLETE | PORT_IRQ_ERROR, port + PORT_IRQ_ENABLE_CLR);

	writel((u32)paddr, port + PORT_CMD_ACTIVATE);
	writel((u64)paddr >> 32, port + PORT_CMD_ACTIVATE + 4);

	irq_mask = (PORT_IRQ_COMPLETE | PORT_IRQ_ERROR) << PORT_IRQ_RAW_SHIFT;
	irq_stat = ata_wait_register(port + PORT_IRQ_STAT, irq_mask, 0x0,
				     10, timeout_msec);

	writel(irq_mask, port + PORT_IRQ_STAT); /* clear IRQs */
	irq_stat >>= PORT_IRQ_RAW_SHIFT;

	if (irq_stat & PORT_IRQ_COMPLETE)
		rc = 0;
	else {
		/* force port into known state */
		sil24_init_port(ap);

		if (irq_stat & PORT_IRQ_ERROR)
			rc = -EIO;
		else
			rc = -EBUSY;
	}

	/* restore IRQ enabled */
	writel(irq_enabled, port + PORT_IRQ_ENABLE_SET);

	return rc;
}

static int sil24_softreset(struct ata_link *link, unsigned int *class,
			   unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	int pmp = sata_srst_pmp(link);
	unsigned long timeout_msec = 0;
	struct ata_taskfile tf;
	const char *reason;
	int rc;

	DPRINTK("ENTER\n");

	/* put the port into known state */
	if (sil24_init_port(ap)) {
		reason = "port not ready";
		goto err;
	}

	/* do SRST */
	if (time_after(deadline, jiffies))
		timeout_msec = jiffies_to_msecs(deadline - jiffies);

	ata_tf_init(link->device, &tf);	/* doesn't really matter */
	rc = sil24_exec_polled_cmd(ap, pmp, &tf, 0, PRB_CTRL_SRST,
				   timeout_msec);
	if (rc == -EBUSY) {
		reason = "timeout";
		goto err;
	} else if (rc) {
		reason = "SRST command error";
		goto err;
	}

	sil24_read_tf(ap, 0, &tf);
	*class = ata_dev_classify(&tf);

	DPRINTK("EXIT, class=%u\n", *class);
	return 0;

 err:
	ata_link_printk(link, KERN_ERR, "softreset failed (%s)\n", reason);
	return -EIO;
}

static int sil24_hardreset(struct ata_link *link, unsigned int *class,
			   unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	void __iomem *port = sil24_port_base(ap);
	struct sil24_port_priv *pp = ap->private_data;
	int did_port_rst = 0;
	const char *reason;
	int tout_msec, rc;
	u32 tmp;

 retry:
	/* Sometimes, DEV_RST is not enough to recover the controller.
	 * This happens often after PM DMA CS errata.
	 */
	if (pp->do_port_rst) {
		ata_port_printk(ap, KERN_WARNING, "controller in dubious "
				"state, performing PORT_RST\n");

		writel(PORT_CS_PORT_RST, port + PORT_CTRL_STAT);
		msleep(10);
		writel(PORT_CS_PORT_RST, port + PORT_CTRL_CLR);
		ata_wait_register(port + PORT_CTRL_STAT, PORT_CS_RDY, 0,
				  10, 5000);

		/* restore port configuration */
		sil24_config_port(ap);
		sil24_config_pmp(ap, ap->nr_pmp_links);

		pp->do_port_rst = 0;
		did_port_rst = 1;
	}

	/* sil24 does the right thing(tm) without any protection */
	sata_set_spd(link);

	tout_msec = 100;
	if (ata_link_online(link))
		tout_msec = 5000;

	writel(PORT_CS_DEV_RST, port + PORT_CTRL_STAT);
	tmp = ata_wait_register(port + PORT_CTRL_STAT,
				PORT_CS_DEV_RST, PORT_CS_DEV_RST, 10,
				tout_msec);

	/* SStatus oscillates between zero and valid status after
	 * DEV_RST, debounce it.
	 */
	rc = sata_link_debounce(link, sata_deb_timing_long, deadline);
	if (rc) {
		reason = "PHY debouncing failed";
		goto err;
	}

	if (tmp & PORT_CS_DEV_RST) {
		if (ata_link_offline(link))
			return 0;
		reason = "link not ready";
		goto err;
	}

	/* Sil24 doesn't store signature FIS after hardreset, so we
	 * can't wait for BSY to clear.  Some devices take a long time
	 * to get ready and those devices will choke if we don't wait
	 * for BSY clearance here.  Tell libata to perform follow-up
	 * softreset.
	 */
	return -EAGAIN;

 err:
	if (!did_port_rst) {
		pp->do_port_rst = 1;
		goto retry;
	}

	ata_link_printk(link, KERN_ERR, "hardreset failed (%s)\n", reason);
	return -EIO;
}

static inline void sil24_fill_sg(struct ata_queued_cmd *qc,
				 struct sil24_sge *sge)
{
	struct scatterlist *sg;
	struct sil24_sge *last_sge = NULL;
	unsigned int si;

	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		sge->addr = cpu_to_le64(sg_dma_address(sg));
		sge->cnt = cpu_to_le32(sg_dma_len(sg));
		sge->flags = 0;

		last_sge = sge;
		sge++;
	}

	last_sge->flags = cpu_to_le32(SGE_TRM);
}

static int sil24_qc_defer(struct ata_queued_cmd *qc)
{
	struct ata_link *link = qc->dev->link;
	struct ata_port *ap = link->ap;
	u8 prot = qc->tf.protocol;

	/*
	 * There is a bug in the chip:
	 * Port LRAM Causes the PRB/SGT Data to be Corrupted
	 * If the host issues a read request for LRAM and SActive registers
	 * while active commands are available in the port, PRB/SGT data in
	 * the LRAM can become corrupted. This issue applies only when
	 * reading from, but not writing to, the LRAM.
	 *
	 * Therefore, reading LRAM when there is no particular error [and
	 * other commands may be outstanding] is prohibited.
	 *
	 * To avoid this bug there are two situations where a command must run
	 * exclusive of any other commands on the port:
	 *
	 * - ATAPI commands which check the sense data
	 * - Passthrough ATA commands which always have ATA_QCFLAG_RESULT_TF
	 *   set.
	 *
 	 */
	int is_excl = (ata_is_atapi(prot) ||
		       (qc->flags & ATA_QCFLAG_RESULT_TF));

	if (unlikely(ap->excl_link)) {
		if (link == ap->excl_link) {
			if (ap->nr_active_links)
				return ATA_DEFER_PORT;
			qc->flags |= ATA_QCFLAG_CLEAR_EXCL;
		} else
			return ATA_DEFER_PORT;
	} else if (unlikely(is_excl)) {
		ap->excl_link = link;
		if (ap->nr_active_links)
			return ATA_DEFER_PORT;
		qc->flags |= ATA_QCFLAG_CLEAR_EXCL;
	}

	return ata_std_qc_defer(qc);
}

static void sil24_qc_prep(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct sil24_port_priv *pp = ap->private_data;
	union sil24_cmd_block *cb;
	struct sil24_prb *prb;
	struct sil24_sge *sge;
	u16 ctrl = 0;

	cb = &pp->cmd_block[sil24_tag(qc->tag)];

	if (!ata_is_atapi(qc->tf.protocol)) {
		prb = &cb->ata.prb;
		sge = cb->ata.sge;
	} else {
		prb = &cb->atapi.prb;
		sge = cb->atapi.sge;
		memset(cb->atapi.cdb, 0, 32);
		memcpy(cb->atapi.cdb, qc->cdb, qc->dev->cdb_len);

		if (ata_is_data(qc->tf.protocol)) {
			if (qc->tf.flags & ATA_TFLAG_WRITE)
				ctrl = PRB_CTRL_PACKET_WRITE;
			else
				ctrl = PRB_CTRL_PACKET_READ;
		}
	}

	prb->ctrl = cpu_to_le16(ctrl);
	ata_tf_to_fis(&qc->tf, qc->dev->link->pmp, 1, prb->fis);

	if (qc->flags & ATA_QCFLAG_DMAMAP)
		sil24_fill_sg(qc, sge);
}

static unsigned int sil24_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct sil24_port_priv *pp = ap->private_data;
	void __iomem *port = sil24_port_base(ap);
	unsigned int tag = sil24_tag(qc->tag);
	dma_addr_t paddr;
	void __iomem *activate;

	paddr = pp->cmd_block_dma + tag * sizeof(*pp->cmd_block);
	activate = port + PORT_CMD_ACTIVATE + tag * 8;

	writel((u32)paddr, activate);
	writel((u64)paddr >> 32, activate + 4);

	return 0;
}

static bool sil24_qc_fill_rtf(struct ata_queued_cmd *qc)
{
	sil24_read_tf(qc->ap, qc->tag, &qc->result_tf);
	return true;
}

static void sil24_pmp_attach(struct ata_port *ap)
{
	u32 *gscr = ap->link.device->gscr;

	sil24_config_pmp(ap, 1);
	sil24_init_port(ap);

	if (sata_pmp_gscr_vendor(gscr) == 0x11ab &&
	    sata_pmp_gscr_devid(gscr) == 0x4140) {
		ata_port_printk(ap, KERN_INFO,
			"disabling NCQ support due to sil24-mv4140 quirk\n");
		ap->flags &= ~ATA_FLAG_NCQ;
	}
}

static void sil24_pmp_detach(struct ata_port *ap)
{
	sil24_init_port(ap);
	sil24_config_pmp(ap, 0);

	ap->flags |= ATA_FLAG_NCQ;
}

static int sil24_pmp_hardreset(struct ata_link *link, unsigned int *class,
			       unsigned long deadline)
{
	int rc;

	rc = sil24_init_port(link->ap);
	if (rc) {
		ata_link_printk(link, KERN_ERR,
				"hardreset failed (port not ready)\n");
		return rc;
	}

	return sata_std_hardreset(link, class, deadline);
}

static void sil24_freeze(struct ata_port *ap)
{
	void __iomem *port = sil24_port_base(ap);

	/* Port-wide IRQ mask in HOST_CTRL doesn't really work, clear
	 * PORT_IRQ_ENABLE instead.
	 */
	writel(0xffff, port + PORT_IRQ_ENABLE_CLR);
}

static void sil24_thaw(struct ata_port *ap)
{
	void __iomem *port = sil24_port_base(ap);
	u32 tmp;

	/* clear IRQ */
	tmp = readl(port + PORT_IRQ_STAT);
	writel(tmp, port + PORT_IRQ_STAT);

	/* turn IRQ back on */
	writel(DEF_PORT_IRQ, port + PORT_IRQ_ENABLE_SET);
}

static void sil24_error_intr(struct ata_port *ap)
{
	void __iomem *port = sil24_port_base(ap);
	struct sil24_port_priv *pp = ap->private_data;
	struct ata_queued_cmd *qc = NULL;
	struct ata_link *link;
	struct ata_eh_info *ehi;
	int abort = 0, freeze = 0;
	u32 irq_stat;

	/* on error, we need to clear IRQ explicitly */
	irq_stat = readl(port + PORT_IRQ_STAT);
	writel(irq_stat, port + PORT_IRQ_STAT);

	/* first, analyze and record host port events */
	link = &ap->link;
	ehi = &link->eh_info;
	ata_ehi_clear_desc(ehi);

	ata_ehi_push_desc(ehi, "irq_stat 0x%08x", irq_stat);

	if (irq_stat & PORT_IRQ_SDB_NOTIFY) {
		ata_ehi_push_desc(ehi, "SDB notify");
		sata_async_notification(ap);
	}

	if (irq_stat & (PORT_IRQ_PHYRDY_CHG | PORT_IRQ_DEV_XCHG)) {
		ata_ehi_hotplugged(ehi);
		ata_ehi_push_desc(ehi, "%s",
				  irq_stat & PORT_IRQ_PHYRDY_CHG ?
				  "PHY RDY changed" : "device exchanged");
		freeze = 1;
	}

	if (irq_stat & PORT_IRQ_UNK_FIS) {
		ehi->err_mask |= AC_ERR_HSM;
		ehi->action |= ATA_EH_RESET;
		ata_ehi_push_desc(ehi, "unknown FIS");
		freeze = 1;
	}

	/* deal with command error */
	if (irq_stat & PORT_IRQ_ERROR) {
		struct sil24_cerr_info *ci = NULL;
		unsigned int err_mask = 0, action = 0;
		u32 context, cerr;
		int pmp;

		abort = 1;

		/* DMA Context Switch Failure in Port Multiplier Mode
		 * errata.  If we have active commands to 3 or more
		 * devices, any error condition on active devices can
		 * corrupt DMA context switching.
		 */
		if (ap->nr_active_links >= 3) {
			ehi->err_mask |= AC_ERR_OTHER;
			ehi->action |= ATA_EH_RESET;
			ata_ehi_push_desc(ehi, "PMP DMA CS errata");
			pp->do_port_rst = 1;
			freeze = 1;
		}

		/* find out the offending link and qc */
		if (sata_pmp_attached(ap)) {
			context = readl(port + PORT_CONTEXT);
			pmp = (context >> 5) & 0xf;

			if (pmp < ap->nr_pmp_links) {
				link = &ap->pmp_link[pmp];
				ehi = &link->eh_info;
				qc = ata_qc_from_tag(ap, link->active_tag);

				ata_ehi_clear_desc(ehi);
				ata_ehi_push_desc(ehi, "irq_stat 0x%08x",
						  irq_stat);
			} else {
				err_mask |= AC_ERR_HSM;
				action |= ATA_EH_RESET;
				freeze = 1;
			}
		} else
			qc = ata_qc_from_tag(ap, link->active_tag);

		/* analyze CMD_ERR */
		cerr = readl(port + PORT_CMD_ERR);
		if (cerr < ARRAY_SIZE(sil24_cerr_db))
			ci = &sil24_cerr_db[cerr];

		if (ci && ci->desc) {
			err_mask |= ci->err_mask;
			action |= ci->action;
			if (action & ATA_EH_RESET)
				freeze = 1;
			ata_ehi_push_desc(ehi, "%s", ci->desc);
		} else {
			err_mask |= AC_ERR_OTHER;
			action |= ATA_EH_RESET;
			freeze = 1;
			ata_ehi_push_desc(ehi, "unknown command error %d",
					  cerr);
		}

		/* record error info */
		if (qc)
			qc->err_mask |= err_mask;
		else
			ehi->err_mask |= err_mask;

		ehi->action |= action;

		/* if PMP, resume */
		if (sata_pmp_attached(ap))
			writel(PORT_CS_PMP_RESUME, port + PORT_CTRL_STAT);
	}

	/* freeze or abort */
	if (freeze)
		ata_port_freeze(ap);
	else if (abort) {
		if (qc)
			ata_link_abort(qc->dev->link);
		else
			ata_port_abort(ap);
	}
}

static inline void sil24_host_intr(struct ata_port *ap)
{
	void __iomem *port = sil24_port_base(ap);
	u32 slot_stat, qc_active;
	int rc;

	/* If PCIX_IRQ_WOC, there's an inherent race window between
	 * clearing IRQ pending status and reading PORT_SLOT_STAT
	 * which may cause spurious interrupts afterwards.  This is
	 * unavoidable and much better than losing interrupts which
	 * happens if IRQ pending is cleared after reading
	 * PORT_SLOT_STAT.
	 */
	if (ap->flags & SIL24_FLAG_PCIX_IRQ_WOC)
		writel(PORT_IRQ_COMPLETE, port + PORT_IRQ_STAT);

	slot_stat = readl(port + PORT_SLOT_STAT);

	if (unlikely(slot_stat & HOST_SSTAT_ATTN)) {
		sil24_error_intr(ap);
		return;
	}

	qc_active = slot_stat & ~HOST_SSTAT_ATTN;
	rc = ata_qc_complete_multiple(ap, qc_active);
	if (rc > 0)
		return;
	if (rc < 0) {
		struct ata_eh_info *ehi = &ap->link.eh_info;
		ehi->err_mask |= AC_ERR_HSM;
		ehi->action |= ATA_EH_RESET;
		ata_port_freeze(ap);
		return;
	}

	/* spurious interrupts are expected if PCIX_IRQ_WOC */
	if (!(ap->flags & SIL24_FLAG_PCIX_IRQ_WOC) && ata_ratelimit())
		ata_port_printk(ap, KERN_INFO, "spurious interrupt "
			"(slot_stat 0x%x active_tag %d sactive 0x%x)\n",
			slot_stat, ap->link.active_tag, ap->link.sactive);
}

static irqreturn_t sil24_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	void __iomem *host_base = host->iomap[SIL24_HOST_BAR];
	unsigned handled = 0;
	u32 status;
	int i;

	status = readl(host_base + HOST_IRQ_STAT);

	if (status == 0xffffffff) {
		printk(KERN_ERR DRV_NAME ": IRQ status == 0xffffffff, "
		       "PCI fault or device removal?\n");
		goto out;
	}

	if (!(status & IRQ_STAT_4PORTS))
		goto out;

	spin_lock(&host->lock);

	for (i = 0; i < host->n_ports; i++)
		if (status & (1 << i)) {
			struct ata_port *ap = host->ports[i];
			if (ap && !(ap->flags & ATA_FLAG_DISABLED)) {
				sil24_host_intr(ap);
				handled++;
			} else
				printk(KERN_ERR DRV_NAME
				       ": interrupt from disabled port %d\n", i);
		}

	spin_unlock(&host->lock);
 out:
	return IRQ_RETVAL(handled);
}

static void sil24_error_handler(struct ata_port *ap)
{
	struct sil24_port_priv *pp = ap->private_data;

	if (sil24_init_port(ap))
		ata_eh_freeze_port(ap);

	sata_pmp_error_handler(ap);

	pp->do_port_rst = 0;
}

static void sil24_post_internal_cmd(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* make DMA engine forget about the failed command */
	if ((qc->flags & ATA_QCFLAG_FAILED) && sil24_init_port(ap))
		ata_eh_freeze_port(ap);
}

static int sil24_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host->dev;
	struct sil24_port_priv *pp;
	union sil24_cmd_block *cb;
	size_t cb_size = sizeof(*cb) * SIL24_MAX_CMDS;
	dma_addr_t cb_dma;

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	cb = dmam_alloc_coherent(dev, cb_size, &cb_dma, GFP_KERNEL);
	if (!cb)
		return -ENOMEM;
	memset(cb, 0, cb_size);

	pp->cmd_block = cb;
	pp->cmd_block_dma = cb_dma;

	ap->private_data = pp;

	ata_port_pbar_desc(ap, SIL24_HOST_BAR, -1, "host");
	ata_port_pbar_desc(ap, SIL24_PORT_BAR, sil24_port_offset(ap), "port");

	return 0;
}

static void sil24_init_controller(struct ata_host *host)
{
	void __iomem *host_base = host->iomap[SIL24_HOST_BAR];
	u32 tmp;
	int i;

	/* GPIO off */
	writel(0, host_base + HOST_FLASH_CMD);

	/* clear global reset & mask interrupts during initialization */
	writel(0, host_base + HOST_CTRL);

	/* init ports */
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		void __iomem *port = sil24_port_base(ap);


		/* Initial PHY setting */
		writel(0x20c, port + PORT_PHY_CFG);

		/* Clear port RST */
		tmp = readl(port + PORT_CTRL_STAT);
		if (tmp & PORT_CS_PORT_RST) {
			writel(PORT_CS_PORT_RST, port + PORT_CTRL_CLR);
			tmp = ata_wait_register(port + PORT_CTRL_STAT,
						PORT_CS_PORT_RST,
						PORT_CS_PORT_RST, 10, 100);
			if (tmp & PORT_CS_PORT_RST)
				dev_printk(KERN_ERR, host->dev,
					   "failed to clear port RST\n");
		}

		/* configure port */
		sil24_config_port(ap);
	}

	/* Turn on interrupts */
	writel(IRQ_STAT_4PORTS, host_base + HOST_CTRL);
}

static int sil24_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	extern int __MARKER__sil24_cmd_block_is_sized_wrongly;
	static int printed_version;
	struct ata_port_info pi = sil24_port_info[ent->driver_data];
	const struct ata_port_info *ppi[] = { &pi, NULL };
	void __iomem * const *iomap;
	struct ata_host *host;
	int rc;
	u32 tmp;

	/* cause link error if sil24_cmd_block is sized wrongly */
	if (sizeof(union sil24_cmd_block) != PAGE_SIZE)
		__MARKER__sil24_cmd_block_is_sized_wrongly = 1;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/* acquire resources */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev,
				(1 << SIL24_HOST_BAR) | (1 << SIL24_PORT_BAR),
				DRV_NAME);
	if (rc)
		return rc;
	iomap = pcim_iomap_table(pdev);

	/* apply workaround for completion IRQ loss on PCI-X errata */
	if (pi.flags & SIL24_FLAG_PCIX_IRQ_WOC) {
		tmp = readl(iomap[SIL24_HOST_BAR] + HOST_CTRL);
		if (tmp & (HOST_CTRL_TRDY | HOST_CTRL_STOP | HOST_CTRL_DEVSEL))
			dev_printk(KERN_INFO, &pdev->dev,
				   "Applying completion IRQ loss on PCI-X "
				   "errata fix\n");
		else
			pi.flags &= ~SIL24_FLAG_PCIX_IRQ_WOC;
	}

	/* allocate and fill host */
	host = ata_host_alloc_pinfo(&pdev->dev, ppi,
				    SIL24_FLAG2NPORTS(ppi[0]->flags));
	if (!host)
		return -ENOMEM;
	host->iomap = iomap;

	/* configure and activate the device */
	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
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

	/* Set max read request size to 4096.  This slightly increases
	 * write throughput for pci-e variants.
	 */
	pcie_set_readrq(pdev, 4096);

	sil24_init_controller(host);

	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, sil24_interrupt, IRQF_SHARED,
				 &sil24_sht);
}

#ifdef CONFIG_PM
static int sil24_pci_device_resume(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	void __iomem *host_base = host->iomap[SIL24_HOST_BAR];
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	if (pdev->dev.power.power_state.event == PM_EVENT_SUSPEND)
		writel(HOST_CTRL_GLOBAL_RST, host_base + HOST_CTRL);

	sil24_init_controller(host);

	ata_host_resume(host);

	return 0;
}

static int sil24_port_resume(struct ata_port *ap)
{
	sil24_config_pmp(ap, ap->nr_pmp_links);
	return 0;
}
#endif

static int __init sil24_init(void)
{
	return pci_register_driver(&sil24_pci_driver);
}

static void __exit sil24_exit(void)
{
	pci_unregister_driver(&sil24_pci_driver);
}

MODULE_AUTHOR("Tejun Heo");
MODULE_DESCRIPTION("Silicon Image 3124/3132 SATA low-level driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, sil24_pci_tbl);

module_init(sil24_init);
module_exit(sil24_exit);
