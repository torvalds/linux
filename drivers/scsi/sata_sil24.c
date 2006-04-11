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
#include <asm/io.h>

#define DRV_NAME	"sata_sil24"
#define DRV_VERSION	"0.24"

/*
 * Port request block (PRB) 32 bytes
 */
struct sil24_prb {
	u16	ctrl;
	u16	prot;
	u32	rx_cnt;
	u8	fis[6 * 4];
};

/*
 * Scatter gather entry (SGE) 16 bytes
 */
struct sil24_sge {
	u64	addr;
	u32	cnt;
	u32	flags;
};

/*
 * Port multiplier
 */
struct sil24_port_multiplier {
	u32	diag;
	u32	sactive;
};

enum {
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

	/*
	 * Port registers
	 * (8192 bytes @ +0x0000, +0x2000, +0x4000 and +0x6000 @ BAR2)
	 */
	PORT_REGS_SIZE		= 0x2000,
	PORT_PRB		= 0x0000, /* (32 bytes PRB + 16 bytes SGEs * 6) * 31 (3968 bytes) */

	PORT_PM			= 0x0f80, /* 8 bytes PM * 16 (128 bytes) */
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
	PORT_CS_RESUME		= (1 << 6), /* port resume */
	PORT_CS_32BIT_ACTV	= (1 << 10), /* 32-bit activation */
	PORT_CS_PM_EN		= (1 << 13), /* port multiplier enable */
	PORT_CS_RDY		= (1 << 31), /* port ready to accept commands */

	/* PORT_IRQ_STAT/ENABLE_SET/CLR */
	/* bits[11:0] are masked */
	PORT_IRQ_COMPLETE	= (1 << 0), /* command(s) completed */
	PORT_IRQ_ERROR		= (1 << 1), /* command execution error */
	PORT_IRQ_PORTRDY_CHG	= (1 << 2), /* port ready change */
	PORT_IRQ_PWR_CHG	= (1 << 3), /* power management change */
	PORT_IRQ_PHYRDY_CHG	= (1 << 4), /* PHY ready change */
	PORT_IRQ_COMWAKE	= (1 << 5), /* COMWAKE received */
	PORT_IRQ_UNK_FIS	= (1 << 6), /* Unknown FIS received */
	PORT_IRQ_SDB_FIS	= (1 << 11), /* SDB FIS received */

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
	PORT_CERR_XFR_MSGABRT	= 34, /* PSD ecode 10 - master abort */
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

	/* board id */
	BID_SIL3124		= 0,
	BID_SIL3132		= 1,
	BID_SIL3131		= 2,

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

/* ap->host_set->private_data */
struct sil24_host_priv {
	void __iomem *host_base;	/* global controller control (128 bytes @BAR0) */
	void __iomem *port_base;	/* port registers (4 * 8192 bytes @BAR2) */
};

static void sil24_dev_config(struct ata_port *ap, struct ata_device *dev);
static u8 sil24_check_status(struct ata_port *ap);
static u32 sil24_scr_read(struct ata_port *ap, unsigned sc_reg);
static void sil24_scr_write(struct ata_port *ap, unsigned sc_reg, u32 val);
static void sil24_tf_read(struct ata_port *ap, struct ata_taskfile *tf);
static int sil24_probe_reset(struct ata_port *ap, unsigned int *classes);
static void sil24_qc_prep(struct ata_queued_cmd *qc);
static unsigned int sil24_qc_issue(struct ata_queued_cmd *qc);
static void sil24_irq_clear(struct ata_port *ap);
static void sil24_eng_timeout(struct ata_port *ap);
static irqreturn_t sil24_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int sil24_port_start(struct ata_port *ap);
static void sil24_port_stop(struct ata_port *ap);
static void sil24_host_stop(struct ata_host_set *host_set);
static int sil24_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);

static const struct pci_device_id sil24_pci_tbl[] = {
	{ 0x1095, 0x3124, PCI_ANY_ID, PCI_ANY_ID, 0, 0, BID_SIL3124 },
	{ 0x8086, 0x3124, PCI_ANY_ID, PCI_ANY_ID, 0, 0, BID_SIL3124 },
	{ 0x1095, 0x3132, PCI_ANY_ID, PCI_ANY_ID, 0, 0, BID_SIL3132 },
	{ 0x1095, 0x3131, PCI_ANY_ID, PCI_ANY_ID, 0, 0, BID_SIL3131 },
	{ 0x1095, 0x3531, PCI_ANY_ID, PCI_ANY_ID, 0, 0, BID_SIL3131 },
	{ } /* terminate list */
};

static struct pci_driver sil24_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= sil24_pci_tbl,
	.probe			= sil24_init_one,
	.remove			= ata_pci_remove_one, /* safe? */
};

static struct scsi_host_template sil24_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static const struct ata_port_operations sil24_ops = {
	.port_disable		= ata_port_disable,

	.dev_config		= sil24_dev_config,

	.check_status		= sil24_check_status,
	.check_altstatus	= sil24_check_status,
	.dev_select		= ata_noop_dev_select,

	.tf_read		= sil24_tf_read,

	.probe_reset		= sil24_probe_reset,

	.qc_prep		= sil24_qc_prep,
	.qc_issue		= sil24_qc_issue,

	.eng_timeout		= sil24_eng_timeout,

	.irq_handler		= sil24_interrupt,
	.irq_clear		= sil24_irq_clear,

	.scr_read		= sil24_scr_read,
	.scr_write		= sil24_scr_write,

	.port_start		= sil24_port_start,
	.port_stop		= sil24_port_stop,
	.host_stop		= sil24_host_stop,
};

/*
 * Use bits 30-31 of host_flags to encode available port numbers.
 * Current maxium is 4.
 */
#define SIL24_NPORTS2FLAG(nports)	((((unsigned)(nports) - 1) & 0x3) << 30)
#define SIL24_FLAG2NPORTS(flag)		((((flag) >> 30) & 0x3) + 1)

static struct ata_port_info sil24_port_info[] = {
	/* sil_3124 */
	{
		.sht		= &sil24_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | ATA_FLAG_PIO_DMA |
				  SIL24_NPORTS2FLAG(4),
		.pio_mask	= 0x1f,			/* pio0-4 */
		.mwdma_mask	= 0x07,			/* mwdma0-2 */
		.udma_mask	= 0x3f,			/* udma0-5 */
		.port_ops	= &sil24_ops,
	},
	/* sil_3132 */
	{
		.sht		= &sil24_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | ATA_FLAG_PIO_DMA |
				  SIL24_NPORTS2FLAG(2),
		.pio_mask	= 0x1f,			/* pio0-4 */
		.mwdma_mask	= 0x07,			/* mwdma0-2 */
		.udma_mask	= 0x3f,			/* udma0-5 */
		.port_ops	= &sil24_ops,
	},
	/* sil_3131/sil_3531 */
	{
		.sht		= &sil24_sht,
		.host_flags	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | ATA_FLAG_PIO_DMA |
				  SIL24_NPORTS2FLAG(1),
		.pio_mask	= 0x1f,			/* pio0-4 */
		.mwdma_mask	= 0x07,			/* mwdma0-2 */
		.udma_mask	= 0x3f,			/* udma0-5 */
		.port_ops	= &sil24_ops,
	},
};

static void sil24_dev_config(struct ata_port *ap, struct ata_device *dev)
{
	void __iomem *port = (void __iomem *)ap->ioaddr.cmd_addr;

	if (dev->cdb_len == 16)
		writel(PORT_CS_CDB16, port + PORT_CTRL_STAT);
	else
		writel(PORT_CS_CDB16, port + PORT_CTRL_CLR);
}

static inline void sil24_update_tf(struct ata_port *ap)
{
	struct sil24_port_priv *pp = ap->private_data;
	void __iomem *port = (void __iomem *)ap->ioaddr.cmd_addr;
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
	void __iomem *scr_addr = (void __iomem *)ap->ioaddr.scr_addr;
	if (sc_reg < ARRAY_SIZE(sil24_scr_map)) {
		void __iomem *addr;
		addr = scr_addr + sil24_scr_map[sc_reg] * 4;
		return readl(scr_addr + sil24_scr_map[sc_reg] * 4);
	}
	return 0xffffffffU;
}

static void sil24_scr_write(struct ata_port *ap, unsigned sc_reg, u32 val)
{
	void __iomem *scr_addr = (void __iomem *)ap->ioaddr.scr_addr;
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

static int sil24_softreset(struct ata_port *ap, int verbose,
			   unsigned int *class)
{
	void __iomem *port = (void __iomem *)ap->ioaddr.cmd_addr;
	struct sil24_port_priv *pp = ap->private_data;
	struct sil24_prb *prb = &pp->cmd_block[0].ata.prb;
	dma_addr_t paddr = pp->cmd_block_dma;
	unsigned long timeout = jiffies + ATA_TMOUT_BOOT * HZ;
	u32 irq_enable, irq_stat;

	DPRINTK("ENTER\n");

	if (!sata_dev_present(ap)) {
		DPRINTK("PHY reports no device\n");
		*class = ATA_DEV_NONE;
		goto out;
	}

	/* temporarily turn off IRQs during SRST */
	irq_enable = readl(port + PORT_IRQ_ENABLE_SET);
	writel(irq_enable, port + PORT_IRQ_ENABLE_CLR);

	/*
	 * XXX: Not sure whether the following sleep is needed or not.
	 * The original driver had it.  So....
	 */
	msleep(10);

	prb->ctrl = PRB_CTRL_SRST;
	prb->fis[1] = 0; /* no PM yet */

	writel((u32)paddr, port + PORT_CMD_ACTIVATE);

	do {
		irq_stat = readl(port + PORT_IRQ_STAT);
		writel(irq_stat, port + PORT_IRQ_STAT);		/* clear irq */

		irq_stat >>= PORT_IRQ_RAW_SHIFT;
		if (irq_stat & (PORT_IRQ_COMPLETE | PORT_IRQ_ERROR))
			break;

		msleep(100);
	} while (time_before(jiffies, timeout));

	/* restore IRQs */
	writel(irq_enable, port + PORT_IRQ_ENABLE_SET);

	if (!(irq_stat & PORT_IRQ_COMPLETE)) {
		DPRINTK("EXIT, srst failed\n");
		return -EIO;
	}

	sil24_update_tf(ap);
	*class = ata_dev_classify(&pp->tf);

	if (*class == ATA_DEV_UNKNOWN)
		*class = ATA_DEV_NONE;

 out:
	DPRINTK("EXIT, class=%u\n", *class);
	return 0;
}

static int sil24_hardreset(struct ata_port *ap, int verbose,
			   unsigned int *class)
{
	unsigned int dummy_class;

	/* sil24 doesn't report device signature after hard reset */
	return sata_std_hardreset(ap, verbose, &dummy_class);
}

static int sil24_probe_reset(struct ata_port *ap, unsigned int *classes)
{
	return ata_drive_probe_reset(ap, ata_std_probeinit,
				     sil24_softreset, sil24_hardreset,
				     ata_std_postreset, classes);
}

static inline void sil24_fill_sg(struct ata_queued_cmd *qc,
				 struct sil24_sge *sge)
{
	struct scatterlist *sg;
	unsigned int idx = 0;

	ata_for_each_sg(sg, qc) {
		sge->addr = cpu_to_le64(sg_dma_address(sg));
		sge->cnt = cpu_to_le32(sg_dma_len(sg));
		if (ata_sg_is_last(sg, qc))
			sge->flags = cpu_to_le32(SGE_TRM);
		else
			sge->flags = 0;

		sge++;
		idx++;
	}
}

static void sil24_qc_prep(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct sil24_port_priv *pp = ap->private_data;
	union sil24_cmd_block *cb = pp->cmd_block + qc->tag;
	struct sil24_prb *prb;
	struct sil24_sge *sge;

	switch (qc->tf.protocol) {
	case ATA_PROT_PIO:
	case ATA_PROT_DMA:
	case ATA_PROT_NODATA:
		prb = &cb->ata.prb;
		sge = cb->ata.sge;
		prb->ctrl = 0;
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
				prb->ctrl = PRB_CTRL_PACKET_WRITE;
			else
				prb->ctrl = PRB_CTRL_PACKET_READ;
		} else
			prb->ctrl = 0;

		break;

	default:
		prb = NULL;	/* shut up, gcc */
		sge = NULL;
		BUG();
	}

	ata_tf_to_fis(&qc->tf, prb->fis, 0);

	if (qc->flags & ATA_QCFLAG_DMAMAP)
		sil24_fill_sg(qc, sge);
}

static unsigned int sil24_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *port = (void __iomem *)ap->ioaddr.cmd_addr;
	struct sil24_port_priv *pp = ap->private_data;
	dma_addr_t paddr = pp->cmd_block_dma + qc->tag * sizeof(*pp->cmd_block);

	writel((u32)paddr, port + PORT_CMD_ACTIVATE);
	return 0;
}

static void sil24_irq_clear(struct ata_port *ap)
{
	/* unused */
}

static int __sil24_restart_controller(void __iomem *port)
{
	u32 tmp;
	int cnt;

	writel(PORT_CS_INIT, port + PORT_CTRL_STAT);

	/* Max ~10ms */
	for (cnt = 0; cnt < 10000; cnt++) {
		tmp = readl(port + PORT_CTRL_STAT);
		if (tmp & PORT_CS_RDY)
			return 0;
		udelay(1);
	}

	return -1;
}

static void sil24_restart_controller(struct ata_port *ap)
{
	if (__sil24_restart_controller((void __iomem *)ap->ioaddr.cmd_addr))
		printk(KERN_ERR DRV_NAME
		       " ata%u: failed to restart controller\n", ap->id);
}

static int __sil24_reset_controller(void __iomem *port)
{
	int cnt;
	u32 tmp;

	/* Reset controller state.  Is this correct? */
	writel(PORT_CS_DEV_RST, port + PORT_CTRL_STAT);
	readl(port + PORT_CTRL_STAT);	/* sync */

	/* Max ~100ms */
	for (cnt = 0; cnt < 1000; cnt++) {
		udelay(100);
		tmp = readl(port + PORT_CTRL_STAT);
		if (!(tmp & PORT_CS_DEV_RST))
			break;
	}

	if (tmp & PORT_CS_DEV_RST)
		return -1;

	if (tmp & PORT_CS_RDY)
		return 0;

	return __sil24_restart_controller(port);
}

static void sil24_reset_controller(struct ata_port *ap)
{
	printk(KERN_NOTICE DRV_NAME
	       " ata%u: resetting controller...\n", ap->id);
	if (__sil24_reset_controller((void __iomem *)ap->ioaddr.cmd_addr))
                printk(KERN_ERR DRV_NAME
                       " ata%u: failed to reset controller\n", ap->id);
}

static void sil24_eng_timeout(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;

	qc = ata_qc_from_tag(ap, ap->active_tag);

	printk(KERN_ERR "ata%u: command timeout\n", ap->id);
	qc->err_mask |= AC_ERR_TIMEOUT;
	ata_eh_qc_complete(qc);

	sil24_reset_controller(ap);
}

static void sil24_error_intr(struct ata_port *ap, u32 slot_stat)
{
	struct ata_queued_cmd *qc = ata_qc_from_tag(ap, ap->active_tag);
	struct sil24_port_priv *pp = ap->private_data;
	void __iomem *port = (void __iomem *)ap->ioaddr.cmd_addr;
	u32 irq_stat, cmd_err, sstatus, serror;
	unsigned int err_mask;

	irq_stat = readl(port + PORT_IRQ_STAT);
	writel(irq_stat, port + PORT_IRQ_STAT);		/* clear irq */

	if (!(irq_stat & PORT_IRQ_ERROR)) {
		/* ignore non-completion, non-error irqs for now */
		printk(KERN_WARNING DRV_NAME
		       "ata%u: non-error exception irq (irq_stat %x)\n",
		       ap->id, irq_stat);
		return;
	}

	cmd_err = readl(port + PORT_CMD_ERR);
	sstatus = readl(port + PORT_SSTATUS);
	serror = readl(port + PORT_SERROR);
	if (serror)
		writel(serror, port + PORT_SERROR);

	/*
	 * Don't log ATAPI device errors.  They're supposed to happen
	 * and any serious errors will be logged using sense data by
	 * the SCSI layer.
	 */
	if (ap->device[0].class != ATA_DEV_ATAPI || cmd_err > PORT_CERR_SDB)
		printk("ata%u: error interrupt on port%d\n"
		       "  stat=0x%x irq=0x%x cmd_err=%d sstatus=0x%x serror=0x%x\n",
		       ap->id, ap->port_no, slot_stat, irq_stat, cmd_err, sstatus, serror);

	if (cmd_err == PORT_CERR_DEV || cmd_err == PORT_CERR_SDB) {
		/*
		 * Device is reporting error, tf registers are valid.
		 */
		sil24_update_tf(ap);
		err_mask = ac_err_mask(pp->tf.command);
		sil24_restart_controller(ap);
	} else {
		/*
		 * Other errors.  libata currently doesn't have any
		 * mechanism to report these errors.  Just turn on
		 * ATA_ERR.
		 */
		err_mask = AC_ERR_OTHER;
		sil24_reset_controller(ap);
	}

	if (qc) {
		qc->err_mask |= err_mask;
		ata_qc_complete(qc);
	}
}

static inline void sil24_host_intr(struct ata_port *ap)
{
	struct ata_queued_cmd *qc = ata_qc_from_tag(ap, ap->active_tag);
	void __iomem *port = (void __iomem *)ap->ioaddr.cmd_addr;
	u32 slot_stat;

	slot_stat = readl(port + PORT_SLOT_STAT);
	if (!(slot_stat & HOST_SSTAT_ATTN)) {
		struct sil24_port_priv *pp = ap->private_data;
		/*
		 * !HOST_SSAT_ATTN guarantees successful completion,
		 * so reading back tf registers is unnecessary for
		 * most commands.  TODO: read tf registers for
		 * commands which require these values on successful
		 * completion (EXECUTE DEVICE DIAGNOSTIC, CHECK POWER,
		 * DEVICE RESET and READ PORT MULTIPLIER (any more?).
		 */
		sil24_update_tf(ap);

		if (qc) {
			qc->err_mask |= ac_err_mask(pp->tf.command);
			ata_qc_complete(qc);
		}
	} else
		sil24_error_intr(ap, slot_stat);
}

static irqreturn_t sil24_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	struct sil24_host_priv *hpriv = host_set->private_data;
	unsigned handled = 0;
	u32 status;
	int i;

	status = readl(hpriv->host_base + HOST_IRQ_STAT);

	if (status == 0xffffffff) {
		printk(KERN_ERR DRV_NAME ": IRQ status == 0xffffffff, "
		       "PCI fault or device removal?\n");
		goto out;
	}

	if (!(status & IRQ_STAT_4PORTS))
		goto out;

	spin_lock(&host_set->lock);

	for (i = 0; i < host_set->n_ports; i++)
		if (status & (1 << i)) {
			struct ata_port *ap = host_set->ports[i];
			if (ap && !(ap->flags & ATA_FLAG_DISABLED)) {
				sil24_host_intr(host_set->ports[i]);
				handled++;
			} else
				printk(KERN_ERR DRV_NAME
				       ": interrupt from disabled port %d\n", i);
		}

	spin_unlock(&host_set->lock);
 out:
	return IRQ_RETVAL(handled);
}

static inline void sil24_cblk_free(struct sil24_port_priv *pp, struct device *dev)
{
	const size_t cb_size = sizeof(*pp->cmd_block);

	dma_free_coherent(dev, cb_size, pp->cmd_block, pp->cmd_block_dma);
}

static int sil24_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct sil24_port_priv *pp;
	union sil24_cmd_block *cb;
	size_t cb_size = sizeof(*cb);
	dma_addr_t cb_dma;
	int rc = -ENOMEM;

	pp = kzalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp)
		goto err_out;

	pp->tf.command = ATA_DRDY;

	cb = dma_alloc_coherent(dev, cb_size, &cb_dma, GFP_KERNEL);
	if (!cb)
		goto err_out_pp;
	memset(cb, 0, cb_size);

	rc = ata_pad_alloc(ap, dev);
	if (rc)
		goto err_out_pad;

	pp->cmd_block = cb;
	pp->cmd_block_dma = cb_dma;

	ap->private_data = pp;

	return 0;

err_out_pad:
	sil24_cblk_free(pp, dev);
err_out_pp:
	kfree(pp);
err_out:
	return rc;
}

static void sil24_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct sil24_port_priv *pp = ap->private_data;

	sil24_cblk_free(pp, dev);
	ata_pad_free(ap, dev);
	kfree(pp);
}

static void sil24_host_stop(struct ata_host_set *host_set)
{
	struct sil24_host_priv *hpriv = host_set->private_data;
	struct pci_dev *pdev = to_pci_dev(host_set->dev);

	pci_iounmap(pdev, hpriv->host_base);
	pci_iounmap(pdev, hpriv->port_base);
	kfree(hpriv);
}

static int sil24_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version = 0;
	unsigned int board_id = (unsigned int)ent->driver_data;
	struct ata_port_info *pinfo = &sil24_port_info[board_id];
	struct ata_probe_ent *probe_ent = NULL;
	struct sil24_host_priv *hpriv = NULL;
	void __iomem *host_base = NULL;
	void __iomem *port_base = NULL;
	int i, rc;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto out_disable;

	rc = -ENOMEM;
	/* map mmio registers */
	host_base = pci_iomap(pdev, 0, 0);
	if (!host_base)
		goto out_free;
	port_base = pci_iomap(pdev, 2, 0);
	if (!port_base)
		goto out_free;

	/* allocate & init probe_ent and hpriv */
	probe_ent = kzalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (!probe_ent)
		goto out_free;

	hpriv = kzalloc(sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		goto out_free;

	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	probe_ent->sht		= pinfo->sht;
	probe_ent->host_flags	= pinfo->host_flags;
	probe_ent->pio_mask	= pinfo->pio_mask;
	probe_ent->mwdma_mask	= pinfo->mwdma_mask;
	probe_ent->udma_mask	= pinfo->udma_mask;
	probe_ent->port_ops	= pinfo->port_ops;
	probe_ent->n_ports	= SIL24_FLAG2NPORTS(pinfo->host_flags);

	probe_ent->irq = pdev->irq;
	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = port_base;
	probe_ent->private_data = hpriv;

	hpriv->host_base = host_base;
	hpriv->port_base = port_base;

	/*
	 * Configure the device
	 */
	/*
	 * FIXME: This device is certainly 64-bit capable.  We just
	 * don't know how to use it.  After fixing 32bit activation in
	 * this function, enable 64bit masks here.
	 */
	rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "32-bit DMA enable failed\n");
		goto out_free;
	}
	rc = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	if (rc) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "32-bit consistent DMA enable failed\n");
		goto out_free;
	}

	/* GPIO off */
	writel(0, host_base + HOST_FLASH_CMD);

	/* Mask interrupts during initialization */
	writel(0, host_base + HOST_CTRL);

	for (i = 0; i < probe_ent->n_ports; i++) {
		void __iomem *port = port_base + i * PORT_REGS_SIZE;
		unsigned long portu = (unsigned long)port;
		u32 tmp;
		int cnt;

		probe_ent->port[i].cmd_addr = portu + PORT_PRB;
		probe_ent->port[i].scr_addr = portu + PORT_SCONTROL;

		ata_std_ports(&probe_ent->port[i]);

		/* Initial PHY setting */
		writel(0x20c, port + PORT_PHY_CFG);

		/* Clear port RST */
		tmp = readl(port + PORT_CTRL_STAT);
		if (tmp & PORT_CS_PORT_RST) {
			writel(PORT_CS_PORT_RST, port + PORT_CTRL_CLR);
			readl(port + PORT_CTRL_STAT);	/* sync */
			for (cnt = 0; cnt < 10; cnt++) {
				msleep(10);
				tmp = readl(port + PORT_CTRL_STAT);
				if (!(tmp & PORT_CS_PORT_RST))
					break;
			}
			if (tmp & PORT_CS_PORT_RST)
				dev_printk(KERN_ERR, &pdev->dev,
				           "failed to clear port RST\n");
		}

		/* Zero error counters. */
		writel(0x8000, port + PORT_DECODE_ERR_THRESH);
		writel(0x8000, port + PORT_CRC_ERR_THRESH);
		writel(0x8000, port + PORT_HSHK_ERR_THRESH);
		writel(0x0000, port + PORT_DECODE_ERR_CNT);
		writel(0x0000, port + PORT_CRC_ERR_CNT);
		writel(0x0000, port + PORT_HSHK_ERR_CNT);

		/* FIXME: 32bit activation? */
		writel(0, port + PORT_ACTIVATE_UPPER_ADDR);
		writel(PORT_CS_32BIT_ACTV, port + PORT_CTRL_STAT);

		/* Configure interrupts */
		writel(0xffff, port + PORT_IRQ_ENABLE_CLR);
		writel(PORT_IRQ_COMPLETE | PORT_IRQ_ERROR | PORT_IRQ_SDB_FIS,
		       port + PORT_IRQ_ENABLE_SET);

		/* Clear interrupts */
		writel(0x0fff0fff, port + PORT_IRQ_STAT);
		writel(PORT_CS_IRQ_WOC, port + PORT_CTRL_CLR);

		/* Clear port multiplier enable and resume bits */
		writel(PORT_CS_PM_EN | PORT_CS_RESUME, port + PORT_CTRL_CLR);

		/* Reset itself */
		if (__sil24_reset_controller(port))
			dev_printk(KERN_ERR, &pdev->dev,
			           "failed to reset controller\n");
	}

	/* Turn on interrupts */
	writel(IRQ_STAT_4PORTS, host_base + HOST_CTRL);

	pci_set_master(pdev);

	/* FIXME: check ata_device_add return value */
	ata_device_add(probe_ent);

	kfree(probe_ent);
	return 0;

 out_free:
	if (host_base)
		pci_iounmap(pdev, host_base);
	if (port_base)
		pci_iounmap(pdev, port_base);
	kfree(probe_ent);
	kfree(hpriv);
	pci_release_regions(pdev);
 out_disable:
	pci_disable_device(pdev);
	return rc;
}

static int __init sil24_init(void)
{
	return pci_module_init(&sil24_pci_driver);
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
