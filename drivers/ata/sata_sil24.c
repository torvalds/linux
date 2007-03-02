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
#define DRV_VERSION	"0.8"

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

/*
 * Port multiplier
 */
struct sil24_port_multiplier {
	__le32	diag;
	__le32	sactive;
};

enum {
	SIL24_HOST_BAR		= 0,
	SIL24_PORT_BAR		= 2,

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
				  PORT_IRQ_UNK_FIS,

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
				  ATA_FLAG_NCQ | ATA_FLAG_SKIP_D2H_BSY,
	SIL24_FLAG_PCIX_IRQ_WOC	= (1 << 24), /* IRQ loss errata on PCI-X */

	IRQ_STAT_4PORTS		= 0xf,
};

struct sil24_ata_block {
	struct sil24_prb prb;
	struct sil24_sge sge[LIBATA_MAX_PRD];
};

struct sil24_atapi_block {
	struct sil24_prb prb;
	u8 cdb[16];
	struct sil24_sge sge[LIBATA_MAX_PRD - 1];
};

union sil24_cmd_block {
	struct sil24_ata_block ata;
	struct sil24_atapi_block atapi;
};

static struct sil24_cerr_info {
	unsigned int err_mask, action;
	const char *desc;
} sil24_cerr_db[] = {
	[0]			= { AC_ERR_DEV, ATA_EH_REVALIDATE,
				    "device error" },
	[PORT_CERR_DEV]		= { AC_ERR_DEV, ATA_EH_REVALIDATE,
				    "device error via D2H FIS" },
	[PORT_CERR_SDB]		= { AC_ERR_DEV, ATA_EH_REVALIDATE,
				    "device error via SDB FIS" },
	[PORT_CERR_DATA]	= { AC_ERR_ATA_BUS, ATA_EH_SOFTRESET,
				    "error in data FIS" },
	[PORT_CERR_SEND]	= { AC_ERR_ATA_BUS, ATA_EH_SOFTRESET,
				    "failed to transmit command FIS" },
	[PORT_CERR_INCONSISTENT] = { AC_ERR_HSM, ATA_EH_SOFTRESET,
				     "protocol mismatch" },
	[PORT_CERR_DIRECTION]	= { AC_ERR_HSM, ATA_EH_SOFTRESET,
				    "data directon mismatch" },
	[PORT_CERR_UNDERRUN]	= { AC_ERR_HSM, ATA_EH_SOFTRESET,
				    "ran out of SGEs while writing" },
	[PORT_CERR_OVERRUN]	= { AC_ERR_HSM, ATA_EH_SOFTRESET,
				    "ran out of SGEs while reading" },
	[PORT_CERR_PKT_PROT]	= { AC_ERR_HSM, ATA_EH_SOFTRESET,
				    "invalid data directon for ATAPI CDB" },
	[PORT_CERR_SGT_BOUNDARY] = { AC_ERR_SYSTEM, ATA_EH_SOFTRESET,
				     "SGT no on qword boundary" },
	[PORT_CERR_SGT_TGTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_SOFTRESET,
				    "PCI target abort while fetching SGT" },
	[PORT_CERR_SGT_MSTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_SOFTRESET,
				    "PCI master abort while fetching SGT" },
	[PORT_CERR_SGT_PCIPERR]	= { AC_ERR_HOST_BUS, ATA_EH_SOFTRESET,
				    "PCI parity error while fetching SGT" },
	[PORT_CERR_CMD_BOUNDARY] = { AC_ERR_SYSTEM, ATA_EH_SOFTRESET,
				     "PRB not on qword boundary" },
	[PORT_CERR_CMD_TGTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_SOFTRESET,
				    "PCI target abort while fetching PRB" },
	[PORT_CERR_CMD_MSTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_SOFTRESET,
				    "PCI master abort while fetching PRB" },
	[PORT_CERR_CMD_PCIPERR]	= { AC_ERR_HOST_BUS, ATA_EH_SOFTRESET,
				    "PCI parity error while fetching PRB" },
	[PORT_CERR_XFR_UNDEF]	= { AC_ERR_HOST_BUS, ATA_EH_SOFTRESET,
				    "undefined error while transferring data" },
	[PORT_CERR_XFR_TGTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_SOFTRESET,
				    "PCI target abort while transferring data" },
	[PORT_CERR_XFR_MSTABRT]	= { AC_ERR_HOST_BUS, ATA_EH_SOFTRESET,
				    "PCI master abort while transferring data" },
	[PORT_CERR_XFR_PCIPERR]	= { AC_ERR_HOST_BUS, ATA_EH_SOFTRESET,
				    "PCI parity error while transferring data" },
	[PORT_CERR_SENDSERVICE]	= { AC_ERR_HSM, ATA_EH_SOFTRESET,
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
	struct ata_taskfile tf;			/* Cached taskfile registers */
};

static void sil24_dev_config(struct ata_port *ap, struct ata_device *dev);
static u8 sil24_check_status(struct ata_port *ap);
static u32 sil24_scr_read(struct ata_port *ap, unsigned sc_reg);
static void sil24_scr_write(struct ata_port *ap, unsigned sc_reg, u32 val);
static void sil24_tf_read(struct ata_port *ap, struct ata_taskfile *tf);
static void sil24_qc_prep(struct ata_queued_cmd *qc);
static unsigned int sil24_qc_issue(struct ata_queued_cmd *qc);
static void sil24_irq_clear(struct ata_port *ap);
static irqreturn_t sil24_interrupt(int irq, void *dev_instance);
static void sil24_freeze(struct ata_port *ap);
static void sil24_thaw(struct ata_port *ap);
static void sil24_error_handler(struct ata_port *ap);
static void sil24_post_internal_cmd(struct ata_queued_cmd *qc);
static int sil24_port_start(struct ata_port *ap);
static int sil24_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
#ifdef CONFIG_PM
static int sil24_pci_device_resume(struct pci_dev *pdev);
#endif

static const struct pci_device_id sil24_pci_tbl[] = {
	{ PCI_VDEVICE(CMD, 0x3124), BID_SIL3124 },
	{ PCI_VDEVICE(INTEL, 0x3124), BID_SIL3124 },
	{ PCI_VDEVICE(CMD, 0x3132), BID_SIL3132 },
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
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.change_queue_depth	= ata_scsi_change_queue_depth,
	.can_queue		= SIL24_MAX_CMDS,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
#ifdef CONFIG_PM
	.suspend		= ata_scsi_device_suspend,
	.resume			= ata_scsi_device_resume,
#endif
};

static const struct ata_port_operations sil24_ops = {
	.port_disable		= ata_port_disable,

	.dev_config		= sil24_dev_config,

	.check_status		= sil24_check_status,
	.check_altstatus	= sil24_check_status,
	.dev_select		= ata_noop_dev_select,

	.tf_read		= sil24_tf_read,

	.qc_prep		= sil24_qc_prep,
	.qc_issue		= sil24_qc_issue,

	.irq_handler		= sil24_interrupt,
	.irq_clear		= sil24_irq_clear,
	.irq_on			= ata_dummy_irq_on,
	.irq_ack		= ata_dummy_irq_ack,

	.scr_read		= sil24_scr_read,
	.scr_write		= sil24_scr_write,

	.freeze			= sil24_freeze,
	.thaw			= sil24_thaw,
	.error_handler		= sil24_error_handler,
	.post_internal_cmd	= sil24_post_internal_cmd,

	.port_start		= sil24_port_start,
};

/*
 * Use bits 30-31 of port_flags to encode available port numbers.
 * Current maxium is 4.
 */
#define SIL24_NPORTS2FLAG(nports)	((((unsigned)(nports) - 1) & 0x3) << 30)
#define SIL24_FLAG2NPORTS(flag)		((((flag) >> 30) & 0x3) + 1)

static struct ata_port_info sil24_port_info[] = {
	/* sil_3124 */
	{
		.sht		= &sil24_sht,
		.flags		= SIL24_COMMON_FLAGS | SIL24_NPORTS2FLAG(4) |
				  SIL24_FLAG_PCIX_IRQ_WOC,
		.pio_mask	= 0x1f,			/* pio0-4 */
		.mwdma_mask	= 0x07,			/* mwdma0-2 */
		.udma_mask	= 0x3f,			/* udma0-5 */
		.port_ops	= &sil24_ops,
	},
	/* sil_3132 */
	{
		.sht		= &sil24_sht,
		.flags		= SIL24_COMMON_FLAGS | SIL24_NPORTS2FLAG(2),
		.pio_mask	= 0x1f,			/* pio0-4 */
		.mwdma_mask	= 0x07,			/* mwdma0-2 */
		.udma_mask	= 0x3f,			/* udma0-5 */
		.port_ops	= &sil24_ops,
	},
	/* sil_3131/sil_3531 */
	{
		.sht		= &sil24_sht,
		.flags		= SIL24_COMMON_FLAGS | SIL24_NPORTS2FLAG(1),
		.pio_mask	= 0x1f,			/* pio0-4 */
		.mwdma_mask	= 0x07,			/* mwdma0-2 */
		.udma_mask	= 0x3f,			/* udma0-5 */
		.port_ops	= &sil24_ops,
	},
};

static int sil24_tag(int tag)
{
	if (unlikely(ata_tag_internal(tag)))
		return 0;
	return tag;
}

static void sil24_dev_config(struct ata_port *ap, struct ata_device *dev)
{
	void __iomem *port = ap->ioaddr.cmd_addr;

	if (dev->cdb_len == 16)
		writel(PORT_CS_CDB16, port + PORT_CTRL_STAT);
	else
		writel(PORT_CS_CDB16, port + PORT_CTRL_CLR);
}

static inline void sil24_update_tf(struct ata_port *ap)
{
	struct sil24_port_priv *pp = ap->private_data;
	void __iomem *port = ap->ioaddr.cmd_addr;
	struct sil24_prb __iomem *prb = port;
	u8 fis[6 * 4];

	memcpy_fromio(fis, prb->fis, 6 * 4);
	ata_tf_from_fis(fis, &pp->tf);
}

static u8 sil24_check_status(struct ata_port *ap)
{
	struct sil24_port_priv *pp = ap->private_data;
	return pp->tf.command;
}

static int sil24_scr_map[] = {
	[SCR_CONTROL]	= 0,
	[SCR_STATUS]	= 1,
	[SCR_ERROR]	= 2,
	[SCR_ACTIVE]	= 3,
};

static u32 sil24_scr_read(struct ata_port *ap, unsigned sc_reg)
{
	void __iomem *scr_addr = ap->ioaddr.scr_addr;
	if (sc_reg < ARRAY_SIZE(sil24_scr_map)) {
		void __iomem *addr;
		addr = scr_addr + sil24_scr_map[sc_reg] * 4;
		return readl(scr_addr + sil24_scr_map[sc_reg] * 4);
	}
	return 0xffffffffU;
}

static void sil24_scr_write(struct ata_port *ap, unsigned sc_reg, u32 val)
{
	void __iomem *scr_addr = ap->ioaddr.scr_addr;
	if (sc_reg < ARRAY_SIZE(sil24_scr_map)) {
		void __iomem *addr;
		addr = scr_addr + sil24_scr_map[sc_reg] * 4;
		writel(val, scr_addr + sil24_scr_map[sc_reg] * 4);
	}
}

static void sil24_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct sil24_port_priv *pp = ap->private_data;
	*tf = pp->tf;
}

static int sil24_init_port(struct ata_port *ap)
{
	void __iomem *port = ap->ioaddr.cmd_addr;
	u32 tmp;

	writel(PORT_CS_INIT, port + PORT_CTRL_STAT);
	ata_wait_register(port + PORT_CTRL_STAT,
			  PORT_CS_INIT, PORT_CS_INIT, 10, 100);
	tmp = ata_wait_register(port + PORT_CTRL_STAT,
				PORT_CS_RDY, 0, 10, 100);

	if ((tmp & (PORT_CS_INIT | PORT_CS_RDY)) != PORT_CS_RDY)
		return -EIO;
	return 0;
}

static int sil24_softreset(struct ata_port *ap, unsigned int *class)
{
	void __iomem *port = ap->ioaddr.cmd_addr;
	struct sil24_port_priv *pp = ap->private_data;
	struct sil24_prb *prb = &pp->cmd_block[0].ata.prb;
	dma_addr_t paddr = pp->cmd_block_dma;
	u32 mask, irq_stat;
	const char *reason;

	DPRINTK("ENTER\n");

	if (ata_port_offline(ap)) {
		DPRINTK("PHY reports no device\n");
		*class = ATA_DEV_NONE;
		goto out;
	}

	/* put the port into known state */
	if (sil24_init_port(ap)) {
		reason ="port not ready";
		goto err;
	}

	/* do SRST */
	prb->ctrl = cpu_to_le16(PRB_CTRL_SRST);
	prb->fis[1] = 0; /* no PMP yet */

	writel((u32)paddr, port + PORT_CMD_ACTIVATE);
	writel((u64)paddr >> 32, port + PORT_CMD_ACTIVATE + 4);

	mask = (PORT_IRQ_COMPLETE | PORT_IRQ_ERROR) << PORT_IRQ_RAW_SHIFT;
	irq_stat = ata_wait_register(port + PORT_IRQ_STAT, mask, 0x0,
				     100, ATA_TMOUT_BOOT / HZ * 1000);

	writel(irq_stat, port + PORT_IRQ_STAT); /* clear IRQs */
	irq_stat >>= PORT_IRQ_RAW_SHIFT;

	if (!(irq_stat & PORT_IRQ_COMPLETE)) {
		if (irq_stat & PORT_IRQ_ERROR)
			reason = "SRST command error";
		else
			reason = "timeout";
		goto err;
	}

	sil24_update_tf(ap);
	*class = ata_dev_classify(&pp->tf);

	if (*class == ATA_DEV_UNKNOWN)
		*class = ATA_DEV_NONE;

 out:
	DPRINTK("EXIT, class=%u\n", *class);
	return 0;

 err:
	ata_port_printk(ap, KERN_ERR, "softreset failed (%s)\n", reason);
	return -EIO;
}

static int sil24_hardreset(struct ata_port *ap, unsigned int *class)
{
	void __iomem *port = ap->ioaddr.cmd_addr;
	const char *reason;
	int tout_msec, rc;
	u32 tmp;

	/* sil24 does the right thing(tm) without any protection */
	sata_set_spd(ap);

	tout_msec = 100;
	if (ata_port_online(ap))
		tout_msec = 5000;

	writel(PORT_CS_DEV_RST, port + PORT_CTRL_STAT);
	tmp = ata_wait_register(port + PORT_CTRL_STAT,
				PORT_CS_DEV_RST, PORT_CS_DEV_RST, 10, tout_msec);

	/* SStatus oscillates between zero and valid status after
	 * DEV_RST, debounce it.
	 */
	rc = sata_phy_debounce(ap, sata_deb_timing_long);
	if (rc) {
		reason = "PHY debouncing failed";
		goto err;
	}

	if (tmp & PORT_CS_DEV_RST) {
		if (ata_port_offline(ap))
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
	ata_port_printk(ap, KERN_ERR, "hardreset failed (%s)\n", reason);
	return -EIO;
}

static inline void sil24_fill_sg(struct ata_queued_cmd *qc,
				 struct sil24_sge *sge)
{
	struct scatterlist *sg;

	ata_for_each_sg(sg, qc) {
		sge->addr = cpu_to_le64(sg_dma_address(sg));
		sge->cnt = cpu_to_le32(sg_dma_len(sg));
		if (ata_sg_is_last(sg, qc))
			sge->flags = cpu_to_le32(SGE_TRM);
		else
			sge->flags = 0;
		sge++;
	}
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

	switch (qc->tf.protocol) {
	case ATA_PROT_PIO:
	case ATA_PROT_DMA:
	case ATA_PROT_NCQ:
	case ATA_PROT_NODATA:
		prb = &cb->ata.prb;
		sge = cb->ata.sge;
		break;

	case ATA_PROT_ATAPI:
	case ATA_PROT_ATAPI_DMA:
	case ATA_PROT_ATAPI_NODATA:
		prb = &cb->atapi.prb;
		sge = cb->atapi.sge;
		memset(cb->atapi.cdb, 0, 32);
		memcpy(cb->atapi.cdb, qc->cdb, qc->dev->cdb_len);

		if (qc->tf.protocol != ATA_PROT_ATAPI_NODATA) {
			if (qc->tf.flags & ATA_TFLAG_WRITE)
				ctrl = PRB_CTRL_PACKET_WRITE;
			else
				ctrl = PRB_CTRL_PACKET_READ;
		}
		break;

	default:
		prb = NULL;	/* shut up, gcc */
		sge = NULL;
		BUG();
	}

	prb->ctrl = cpu_to_le16(ctrl);
	ata_tf_to_fis(&qc->tf, prb->fis, 0);

	if (qc->flags & ATA_QCFLAG_DMAMAP)
		sil24_fill_sg(qc, sge);
}

static unsigned int sil24_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct sil24_port_priv *pp = ap->private_data;
	void __iomem *port = ap->ioaddr.cmd_addr;
	unsigned int tag = sil24_tag(qc->tag);
	dma_addr_t paddr;
	void __iomem *activate;

	paddr = pp->cmd_block_dma + tag * sizeof(*pp->cmd_block);
	activate = port + PORT_CMD_ACTIVATE + tag * 8;

	writel((u32)paddr, activate);
	writel((u64)paddr >> 32, activate + 4);

	return 0;
}

static void sil24_irq_clear(struct ata_port *ap)
{
	/* unused */
}

static void sil24_freeze(struct ata_port *ap)
{
	void __iomem *port = ap->ioaddr.cmd_addr;

	/* Port-wide IRQ mask in HOST_CTRL doesn't really work, clear
	 * PORT_IRQ_ENABLE instead.
	 */
	writel(0xffff, port + PORT_IRQ_ENABLE_CLR);
}

static void sil24_thaw(struct ata_port *ap)
{
	void __iomem *port = ap->ioaddr.cmd_addr;
	u32 tmp;

	/* clear IRQ */
	tmp = readl(port + PORT_IRQ_STAT);
	writel(tmp, port + PORT_IRQ_STAT);

	/* turn IRQ back on */
	writel(DEF_PORT_IRQ, port + PORT_IRQ_ENABLE_SET);
}

static void sil24_error_intr(struct ata_port *ap)
{
	void __iomem *port = ap->ioaddr.cmd_addr;
	struct ata_eh_info *ehi = &ap->eh_info;
	int freeze = 0;
	u32 irq_stat;

	/* on error, we need to clear IRQ explicitly */
	irq_stat = readl(port + PORT_IRQ_STAT);
	writel(irq_stat, port + PORT_IRQ_STAT);

	/* first, analyze and record host port events */
	ata_ehi_clear_desc(ehi);

	ata_ehi_push_desc(ehi, "irq_stat 0x%08x", irq_stat);

	if (irq_stat & (PORT_IRQ_PHYRDY_CHG | PORT_IRQ_DEV_XCHG)) {
		ata_ehi_hotplugged(ehi);
		ata_ehi_push_desc(ehi, ", %s",
			       irq_stat & PORT_IRQ_PHYRDY_CHG ?
			       "PHY RDY changed" : "device exchanged");
		freeze = 1;
	}

	if (irq_stat & PORT_IRQ_UNK_FIS) {
		ehi->err_mask |= AC_ERR_HSM;
		ehi->action |= ATA_EH_SOFTRESET;
		ata_ehi_push_desc(ehi , ", unknown FIS");
		freeze = 1;
	}

	/* deal with command error */
	if (irq_stat & PORT_IRQ_ERROR) {
		struct sil24_cerr_info *ci = NULL;
		unsigned int err_mask = 0, action = 0;
		struct ata_queued_cmd *qc;
		u32 cerr;

		/* analyze CMD_ERR */
		cerr = readl(port + PORT_CMD_ERR);
		if (cerr < ARRAY_SIZE(sil24_cerr_db))
			ci = &sil24_cerr_db[cerr];

		if (ci && ci->desc) {
			err_mask |= ci->err_mask;
			action |= ci->action;
			ata_ehi_push_desc(ehi, ", %s", ci->desc);
		} else {
			err_mask |= AC_ERR_OTHER;
			action |= ATA_EH_SOFTRESET;
			ata_ehi_push_desc(ehi, ", unknown command error %d",
					  cerr);
		}

		/* record error info */
		qc = ata_qc_from_tag(ap, ap->active_tag);
		if (qc) {
			sil24_update_tf(ap);
			qc->err_mask |= err_mask;
		} else
			ehi->err_mask |= err_mask;

		ehi->action |= action;
	}

	/* freeze or abort */
	if (freeze)
		ata_port_freeze(ap);
	else
		ata_port_abort(ap);
}

static void sil24_finish_qc(struct ata_queued_cmd *qc)
{
	if (qc->flags & ATA_QCFLAG_RESULT_TF)
		sil24_update_tf(qc->ap);
}

static inline void sil24_host_intr(struct ata_port *ap)
{
	void __iomem *port = ap->ioaddr.cmd_addr;
	u32 slot_stat, qc_active;
	int rc;

	slot_stat = readl(port + PORT_SLOT_STAT);

	if (unlikely(slot_stat & HOST_SSTAT_ATTN)) {
		sil24_error_intr(ap);
		return;
	}

	if (ap->flags & SIL24_FLAG_PCIX_IRQ_WOC)
		writel(PORT_IRQ_COMPLETE, port + PORT_IRQ_STAT);

	qc_active = slot_stat & ~HOST_SSTAT_ATTN;
	rc = ata_qc_complete_multiple(ap, qc_active, sil24_finish_qc);
	if (rc > 0)
		return;
	if (rc < 0) {
		struct ata_eh_info *ehi = &ap->eh_info;
		ehi->err_mask |= AC_ERR_HSM;
		ehi->action |= ATA_EH_SOFTRESET;
		ata_port_freeze(ap);
		return;
	}

	if (ata_ratelimit())
		ata_port_printk(ap, KERN_INFO, "spurious interrupt "
			"(slot_stat 0x%x active_tag %d sactive 0x%x)\n",
			slot_stat, ap->active_tag, ap->sactive);
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
				sil24_host_intr(host->ports[i]);
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
	struct ata_eh_context *ehc = &ap->eh_context;

	if (sil24_init_port(ap)) {
		ata_eh_freeze_port(ap);
		ehc->i.action |= ATA_EH_HARDRESET;
	}

	/* perform recovery */
	ata_do_eh(ap, ata_std_prereset, sil24_softreset, sil24_hardreset,
		  ata_std_postreset);
}

static void sil24_post_internal_cmd(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	if (qc->flags & ATA_QCFLAG_FAILED)
		qc->err_mask |= AC_ERR_OTHER;

	/* make DMA engine forget about the failed command */
	if (qc->err_mask)
		sil24_init_port(ap);
}

static int sil24_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host->dev;
	struct sil24_port_priv *pp;
	union sil24_cmd_block *cb;
	size_t cb_size = sizeof(*cb) * SIL24_MAX_CMDS;
	dma_addr_t cb_dma;
	int rc;

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	pp->tf.command = ATA_DRDY;

	cb = dmam_alloc_coherent(dev, cb_size, &cb_dma, GFP_KERNEL);
	if (!cb)
		return -ENOMEM;
	memset(cb, 0, cb_size);

	rc = ata_pad_alloc(ap, dev);
	if (rc)
		return rc;

	pp->cmd_block = cb;
	pp->cmd_block_dma = cb_dma;

	ap->private_data = pp;

	return 0;
}

static void sil24_init_controller(struct pci_dev *pdev, int n_ports,
				  unsigned long port_flags,
				  void __iomem *host_base,
				  void __iomem *port_base)
{
	u32 tmp;
	int i;

	/* GPIO off */
	writel(0, host_base + HOST_FLASH_CMD);

	/* clear global reset & mask interrupts during initialization */
	writel(0, host_base + HOST_CTRL);

	/* init ports */
	for (i = 0; i < n_ports; i++) {
		void __iomem *port = port_base + i * PORT_REGS_SIZE;

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
				dev_printk(KERN_ERR, &pdev->dev,
				           "failed to clear port RST\n");
		}

		/* Configure IRQ WoC */
		if (port_flags & SIL24_FLAG_PCIX_IRQ_WOC)
			writel(PORT_CS_IRQ_WOC, port + PORT_CTRL_STAT);
		else
			writel(PORT_CS_IRQ_WOC, port + PORT_CTRL_CLR);

		/* Zero error counters. */
		writel(0x8000, port + PORT_DECODE_ERR_THRESH);
		writel(0x8000, port + PORT_CRC_ERR_THRESH);
		writel(0x8000, port + PORT_HSHK_ERR_THRESH);
		writel(0x0000, port + PORT_DECODE_ERR_CNT);
		writel(0x0000, port + PORT_CRC_ERR_CNT);
		writel(0x0000, port + PORT_HSHK_ERR_CNT);

		/* Always use 64bit activation */
		writel(PORT_CS_32BIT_ACTV, port + PORT_CTRL_CLR);

		/* Clear port multiplier enable and resume bits */
		writel(PORT_CS_PMP_EN | PORT_CS_PMP_RESUME,
		       port + PORT_CTRL_CLR);
	}

	/* Turn on interrupts */
	writel(IRQ_STAT_4PORTS, host_base + HOST_CTRL);
}

static int sil24_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version = 0;
	struct device *dev = &pdev->dev;
	unsigned int board_id = (unsigned int)ent->driver_data;
	struct ata_port_info *pinfo = &sil24_port_info[board_id];
	struct ata_probe_ent *probe_ent;
	void __iomem *host_base;
	void __iomem *port_base;
	int i, rc;
	u32 tmp;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev,
				(1 << SIL24_HOST_BAR) | (1 << SIL24_PORT_BAR),
				DRV_NAME);
	if (rc)
		return rc;

	/* allocate & init probe_ent */
	probe_ent = devm_kzalloc(dev, sizeof(*probe_ent), GFP_KERNEL);
	if (!probe_ent)
		return -ENOMEM;

	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	probe_ent->sht		= pinfo->sht;
	probe_ent->port_flags	= pinfo->flags;
	probe_ent->pio_mask	= pinfo->pio_mask;
	probe_ent->mwdma_mask	= pinfo->mwdma_mask;
	probe_ent->udma_mask	= pinfo->udma_mask;
	probe_ent->port_ops	= pinfo->port_ops;
	probe_ent->n_ports	= SIL24_FLAG2NPORTS(pinfo->flags);

	probe_ent->irq = pdev->irq;
	probe_ent->irq_flags = IRQF_SHARED;
	probe_ent->iomap = pcim_iomap_table(pdev);

	host_base = probe_ent->iomap[SIL24_HOST_BAR];
	port_base = probe_ent->iomap[SIL24_PORT_BAR];

	/*
	 * Configure the device
	 */
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

	/* Apply workaround for completion IRQ loss on PCI-X errata */
	if (probe_ent->port_flags & SIL24_FLAG_PCIX_IRQ_WOC) {
		tmp = readl(host_base + HOST_CTRL);
		if (tmp & (HOST_CTRL_TRDY | HOST_CTRL_STOP | HOST_CTRL_DEVSEL))
			dev_printk(KERN_INFO, &pdev->dev,
				   "Applying completion IRQ loss on PCI-X "
				   "errata fix\n");
		else
			probe_ent->port_flags &= ~SIL24_FLAG_PCIX_IRQ_WOC;
	}

	for (i = 0; i < probe_ent->n_ports; i++) {
		void __iomem *port = port_base + i * PORT_REGS_SIZE;

		probe_ent->port[i].cmd_addr = port;
		probe_ent->port[i].scr_addr = port + PORT_SCONTROL;

		ata_std_ports(&probe_ent->port[i]);
	}

	sil24_init_controller(pdev, probe_ent->n_ports, probe_ent->port_flags,
			      host_base, port_base);

	pci_set_master(pdev);

	if (!ata_device_add(probe_ent))
		return -ENODEV;

	devm_kfree(dev, probe_ent);
	return 0;
}

#ifdef CONFIG_PM
static int sil24_pci_device_resume(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	void __iomem *host_base = host->iomap[SIL24_HOST_BAR];
	void __iomem *port_base = host->iomap[SIL24_PORT_BAR];
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	if (pdev->dev.power.power_state.event == PM_EVENT_SUSPEND)
		writel(HOST_CTRL_GLOBAL_RST, host_base + HOST_CTRL);

	sil24_init_controller(pdev, host->n_ports, host->ports[0]->flags,
			      host_base, port_base);

	ata_host_resume(host);

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
