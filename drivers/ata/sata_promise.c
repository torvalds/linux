/*
 *  sata_promise.c - Promise SATA
 *
 *  Maintained by:  Jeff Garzik <jgarzik@pobox.com>
 *  		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2003-2004 Red Hat, Inc.
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 *  Hardware information only available under NDA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/libata.h>
#include <asm/io.h>
#include "sata_promise.h"

#define DRV_NAME	"sata_promise"
#define DRV_VERSION	"1.05"


enum {
	/* register offsets */
	PDC_FEATURE		= 0x04, /* Feature/Error reg (per port) */
	PDC_SECTOR_COUNT	= 0x08, /* Sector count reg (per port) */
	PDC_SECTOR_NUMBER	= 0x0C, /* Sector number reg (per port) */
	PDC_CYLINDER_LOW	= 0x10, /* Cylinder low reg (per port) */
	PDC_CYLINDER_HIGH	= 0x14, /* Cylinder high reg (per port) */
	PDC_DEVICE		= 0x18, /* Device/Head reg (per port) */
	PDC_COMMAND		= 0x1C, /* Command/status reg (per port) */
	PDC_ALTSTATUS		= 0x38, /* Alternate-status/device-control reg (per port) */
	PDC_PKT_SUBMIT		= 0x40, /* Command packet pointer addr */
	PDC_INT_SEQMASK		= 0x40,	/* Mask of asserted SEQ INTs */
	PDC_FLASH_CTL		= 0x44, /* Flash control register */
	PDC_GLOBAL_CTL		= 0x48, /* Global control/status (per port) */
	PDC_CTLSTAT		= 0x60,	/* IDE control and status (per port) */
	PDC_SATA_PLUG_CSR	= 0x6C, /* SATA Plug control/status reg */
	PDC2_SATA_PLUG_CSR	= 0x60, /* SATAII Plug control/status reg */
	PDC_TBG_MODE		= 0x41C, /* TBG mode (not SATAII) */
	PDC_SLEW_CTL		= 0x470, /* slew rate control reg (not SATAII) */

	PDC_ERR_MASK		= (1<<19) | (1<<20) | (1<<21) | (1<<22) |
				  (1<<8) | (1<<9) | (1<<10),

	board_2037x		= 0,	/* FastTrak S150 TX2plus */
	board_20319		= 1,	/* FastTrak S150 TX4 */
	board_20619		= 2,	/* FastTrak TX4000 */
	board_2057x		= 3,	/* SATAII150 Tx2plus */
	board_40518		= 4,	/* SATAII150 Tx4 */

	PDC_HAS_PATA		= (1 << 1), /* PDC20375/20575 has PATA */

	/* Sequence counter control registers bit definitions */
	PDC_SEQCNTRL_INT_MASK	= (1 << 5), /* Sequence Interrupt Mask */

	/* Feature register values */
	PDC_FEATURE_ATAPI_PIO	= 0x00, /* ATAPI data xfer by PIO */
	PDC_FEATURE_ATAPI_DMA	= 0x01, /* ATAPI data xfer by DMA */

	/* Device/Head register values */
	PDC_DEVICE_SATA		= 0xE0, /* Device/Head value for SATA devices */

	/* PDC_CTLSTAT bit definitions */
	PDC_DMA_ENABLE		= (1 << 7),
	PDC_IRQ_DISABLE		= (1 << 10),
	PDC_RESET		= (1 << 11), /* HDMA reset */

	PDC_COMMON_FLAGS	= ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO |
				  ATA_FLAG_PIO_POLLING,

	/* hp->flags bits */
	PDC_FLAG_GEN_II		= (1 << 0),
};


struct pdc_port_priv {
	u8			*pkt;
	dma_addr_t		pkt_dma;
};

struct pdc_host_priv {
	unsigned long		flags;
	unsigned long		port_flags[ATA_MAX_PORTS];
};

static u32 pdc_sata_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void pdc_sata_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);
static int pdc_ata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static irqreturn_t pdc_interrupt (int irq, void *dev_instance);
static void pdc_eng_timeout(struct ata_port *ap);
static int pdc_port_start(struct ata_port *ap);
static void pdc_port_stop(struct ata_port *ap);
static void pdc_pata_phy_reset(struct ata_port *ap);
static void pdc_qc_prep(struct ata_queued_cmd *qc);
static void pdc_tf_load_mmio(struct ata_port *ap, const struct ata_taskfile *tf);
static void pdc_exec_command_mmio(struct ata_port *ap, const struct ata_taskfile *tf);
static int pdc_check_atapi_dma(struct ata_queued_cmd *qc);
static int pdc_old_check_atapi_dma(struct ata_queued_cmd *qc);
static void pdc_irq_clear(struct ata_port *ap);
static unsigned int pdc_qc_issue_prot(struct ata_queued_cmd *qc);
static void pdc_host_stop(struct ata_host *host);
static void pdc_freeze(struct ata_port *ap);
static void pdc_thaw(struct ata_port *ap);
static void pdc_error_handler(struct ata_port *ap);
static void pdc_post_internal_cmd(struct ata_queued_cmd *qc);


static struct scsi_host_template pdc_ata_sht = {
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
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static const struct ata_port_operations pdc_sata_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= pdc_tf_load_mmio,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= pdc_exec_command_mmio,
	.dev_select		= ata_std_dev_select,
	.check_atapi_dma	= pdc_check_atapi_dma,

	.qc_prep		= pdc_qc_prep,
	.qc_issue		= pdc_qc_issue_prot,
	.freeze			= pdc_freeze,
	.thaw			= pdc_thaw,
	.error_handler		= pdc_error_handler,
	.post_internal_cmd	= pdc_post_internal_cmd,
	.data_xfer		= ata_mmio_data_xfer,
	.irq_handler		= pdc_interrupt,
	.irq_clear		= pdc_irq_clear,

	.scr_read		= pdc_sata_scr_read,
	.scr_write		= pdc_sata_scr_write,
	.port_start		= pdc_port_start,
	.port_stop		= pdc_port_stop,
	.host_stop		= pdc_host_stop,
};

/* First-generation chips need a more restrictive ->check_atapi_dma op */
static const struct ata_port_operations pdc_old_sata_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= pdc_tf_load_mmio,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= pdc_exec_command_mmio,
	.dev_select		= ata_std_dev_select,
	.check_atapi_dma	= pdc_old_check_atapi_dma,

	.qc_prep		= pdc_qc_prep,
	.qc_issue		= pdc_qc_issue_prot,
	.freeze			= pdc_freeze,
	.thaw			= pdc_thaw,
	.error_handler		= pdc_error_handler,
	.post_internal_cmd	= pdc_post_internal_cmd,
	.data_xfer		= ata_mmio_data_xfer,
	.irq_handler		= pdc_interrupt,
	.irq_clear		= pdc_irq_clear,

	.scr_read		= pdc_sata_scr_read,
	.scr_write		= pdc_sata_scr_write,
	.port_start		= pdc_port_start,
	.port_stop		= pdc_port_stop,
	.host_stop		= pdc_host_stop,
};

static const struct ata_port_operations pdc_pata_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= pdc_tf_load_mmio,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= pdc_exec_command_mmio,
	.dev_select		= ata_std_dev_select,
	.check_atapi_dma	= pdc_check_atapi_dma,

	.phy_reset		= pdc_pata_phy_reset,

	.qc_prep		= pdc_qc_prep,
	.qc_issue		= pdc_qc_issue_prot,
	.data_xfer		= ata_mmio_data_xfer,
	.eng_timeout		= pdc_eng_timeout,
	.irq_handler		= pdc_interrupt,
	.irq_clear		= pdc_irq_clear,

	.port_start		= pdc_port_start,
	.port_stop		= pdc_port_stop,
	.host_stop		= pdc_host_stop,
};

static const struct ata_port_info pdc_port_info[] = {
	/* board_2037x */
	{
		.sht		= &pdc_ata_sht,
		.flags		= PDC_COMMON_FLAGS,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_old_sata_ops,
	},

	/* board_20319 */
	{
		.sht		= &pdc_ata_sht,
		.flags		= PDC_COMMON_FLAGS | ATA_FLAG_SATA,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_old_sata_ops,
	},

	/* board_20619 */
	{
		.sht		= &pdc_ata_sht,
		.flags		= PDC_COMMON_FLAGS | ATA_FLAG_SRST | ATA_FLAG_SLAVE_POSS,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_pata_ops,
	},

	/* board_2057x */
	{
		.sht		= &pdc_ata_sht,
		.flags		= PDC_COMMON_FLAGS,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_sata_ops,
	},

	/* board_40518 */
	{
		.sht		= &pdc_ata_sht,
		.flags		= PDC_COMMON_FLAGS | ATA_FLAG_SATA,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_sata_ops,
	},
};

static const struct pci_device_id pdc_ata_pci_tbl[] = {
	{ PCI_VDEVICE(PROMISE, 0x3371), board_2037x },
	{ PCI_VDEVICE(PROMISE, 0x3373), board_2037x },
	{ PCI_VDEVICE(PROMISE, 0x3375), board_2037x },
	{ PCI_VDEVICE(PROMISE, 0x3376), board_2037x },
	{ PCI_VDEVICE(PROMISE, 0x3570), board_2057x },
	{ PCI_VDEVICE(PROMISE, 0x3571), board_2057x },
	{ PCI_VDEVICE(PROMISE, 0x3574), board_2057x },
	{ PCI_VDEVICE(PROMISE, 0x3577), board_2057x },
	{ PCI_VDEVICE(PROMISE, 0x3d73), board_2057x },
	{ PCI_VDEVICE(PROMISE, 0x3d75), board_2057x },

	{ PCI_VDEVICE(PROMISE, 0x3318), board_20319 },
	{ PCI_VDEVICE(PROMISE, 0x3319), board_20319 },
	{ PCI_VDEVICE(PROMISE, 0x3515), board_20319 },
	{ PCI_VDEVICE(PROMISE, 0x3519), board_20319 },
	{ PCI_VDEVICE(PROMISE, 0x3d17), board_40518 },
	{ PCI_VDEVICE(PROMISE, 0x3d18), board_40518 },

	{ PCI_VDEVICE(PROMISE, 0x6629), board_20619 },

	{ }	/* terminate list */
};


static struct pci_driver pdc_ata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= pdc_ata_pci_tbl,
	.probe			= pdc_ata_init_one,
	.remove			= ata_pci_remove_one,
};


static int pdc_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host->dev;
	struct pdc_host_priv *hp = ap->host->private_data;
	struct pdc_port_priv *pp;
	int rc;

	/* fix up port flags and cable type for SATA+PATA chips */
	ap->flags |= hp->port_flags[ap->port_no];
	if (ap->flags & ATA_FLAG_SATA)
		ap->cbl = ATA_CBL_SATA;

	rc = ata_port_start(ap);
	if (rc)
		return rc;

	pp = kzalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		rc = -ENOMEM;
		goto err_out;
	}

	pp->pkt = dma_alloc_coherent(dev, 128, &pp->pkt_dma, GFP_KERNEL);
	if (!pp->pkt) {
		rc = -ENOMEM;
		goto err_out_kfree;
	}

	ap->private_data = pp;

	/* fix up PHYMODE4 align timing */
	if ((hp->flags & PDC_FLAG_GEN_II) && sata_scr_valid(ap)) {
		void __iomem *mmio = (void __iomem *) ap->ioaddr.scr_addr;
		unsigned int tmp;

		tmp = readl(mmio + 0x014);
		tmp = (tmp & ~3) | 1;	/* set bits 1:0 = 0:1 */
		writel(tmp, mmio + 0x014);
	}

	return 0;

err_out_kfree:
	kfree(pp);
err_out:
	ata_port_stop(ap);
	return rc;
}


static void pdc_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host->dev;
	struct pdc_port_priv *pp = ap->private_data;

	ap->private_data = NULL;
	dma_free_coherent(dev, 128, pp->pkt, pp->pkt_dma);
	kfree(pp);
	ata_port_stop(ap);
}


static void pdc_host_stop(struct ata_host *host)
{
	struct pdc_host_priv *hp = host->private_data;

	ata_pci_host_stop(host);

	kfree(hp);
}


static void pdc_reset_port(struct ata_port *ap)
{
	void __iomem *mmio = (void __iomem *) ap->ioaddr.cmd_addr + PDC_CTLSTAT;
	unsigned int i;
	u32 tmp;

	for (i = 11; i > 0; i--) {
		tmp = readl(mmio);
		if (tmp & PDC_RESET)
			break;

		udelay(100);

		tmp |= PDC_RESET;
		writel(tmp, mmio);
	}

	tmp &= ~PDC_RESET;
	writel(tmp, mmio);
	readl(mmio);	/* flush */
}

static void pdc_pata_cbl_detect(struct ata_port *ap)
{
	u8 tmp;
	void __iomem *mmio = (void __iomem *) ap->ioaddr.cmd_addr + PDC_CTLSTAT + 0x03;

	tmp = readb(mmio);

	if (tmp & 0x01) {
		ap->cbl = ATA_CBL_PATA40;
		ap->udma_mask &= ATA_UDMA_MASK_40C;
	} else
		ap->cbl = ATA_CBL_PATA80;
}

static void pdc_pata_phy_reset(struct ata_port *ap)
{
	pdc_pata_cbl_detect(ap);
	pdc_reset_port(ap);
	ata_port_probe(ap);
	ata_bus_reset(ap);
}

static u32 pdc_sata_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL || ap->cbl != ATA_CBL_SATA)
		return 0xffffffffU;
	return readl((void __iomem *) ap->ioaddr.scr_addr + (sc_reg * 4));
}


static void pdc_sata_scr_write (struct ata_port *ap, unsigned int sc_reg,
			       u32 val)
{
	if (sc_reg > SCR_CONTROL || ap->cbl != ATA_CBL_SATA)
		return;
	writel(val, (void __iomem *) ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void pdc_atapi_dma_pkt(struct ata_taskfile *tf,
			      dma_addr_t sg_table,
			      unsigned int cdb_len, u8 *cdb,
			      u8 *buf)
{
	u32 *buf32 = (u32 *) buf;

	/* set control bits (byte 0), zero delay seq id (byte 3),
	 * and seq id (byte 2)
	 */
	if (!(tf->flags & ATA_TFLAG_WRITE))
		buf32[0] = cpu_to_le32(PDC_PKT_READ);
	else
		buf32[0] = 0;
	buf32[1] = cpu_to_le32(sg_table);	/* S/G table addr */
	buf32[2] = 0;				/* no next-packet */

	/* we can represent cdb lengths 2/4/6/8/10/12/14/16 */
	BUG_ON(cdb_len & ~0x1E);

	buf[12] = (((cdb_len >> 1) & 7) << 5) | ATA_REG_DATA | PDC_LAST_REG;
	memcpy(buf+13, cdb, cdb_len);
}

static void pdc_qc_prep(struct ata_queued_cmd *qc)
{
	struct pdc_port_priv *pp = qc->ap->private_data;
	unsigned int i;

	VPRINTK("ENTER\n");

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
		ata_qc_prep(qc);
		/* fall through */

	case ATA_PROT_NODATA:
		i = pdc_pkt_header(&qc->tf, qc->ap->prd_dma,
				   qc->dev->devno, pp->pkt);

		if (qc->tf.flags & ATA_TFLAG_LBA48)
			i = pdc_prep_lba48(&qc->tf, pp->pkt, i);
		else
			i = pdc_prep_lba28(&qc->tf, pp->pkt, i);

		pdc_pkt_footer(&qc->tf, pp->pkt, i);
		break;

	case ATA_PROT_ATAPI:
	case ATA_PROT_ATAPI_NODATA:
		ata_qc_prep(qc);
		break;

	case ATA_PROT_ATAPI_DMA:
		ata_qc_prep(qc);
		pdc_atapi_dma_pkt(&qc->tf, qc->ap->prd_dma, qc->dev->cdb_len, qc->cdb, pp->pkt);
		break;

	default:
		break;
	}
}

static void pdc_freeze(struct ata_port *ap)
{
	void __iomem *mmio = (void __iomem *) ap->ioaddr.cmd_addr;
	u32 tmp;

	tmp = readl(mmio + PDC_CTLSTAT);
	tmp |= PDC_IRQ_DISABLE;
	tmp &= ~PDC_DMA_ENABLE;
	writel(tmp, mmio + PDC_CTLSTAT);
	readl(mmio + PDC_CTLSTAT); /* flush */
}

static void pdc_thaw(struct ata_port *ap)
{
	void __iomem *mmio = (void __iomem *) ap->ioaddr.cmd_addr;
	u32 tmp;

	/* clear IRQ */
	readl(mmio + PDC_INT_SEQMASK);

	/* turn IRQ back on */
	tmp = readl(mmio + PDC_CTLSTAT);
	tmp &= ~PDC_IRQ_DISABLE;
	writel(tmp, mmio + PDC_CTLSTAT);
	readl(mmio + PDC_CTLSTAT); /* flush */
}

static void pdc_error_handler(struct ata_port *ap)
{
	ata_reset_fn_t hardreset;

	if (!(ap->pflags & ATA_PFLAG_FROZEN))
		pdc_reset_port(ap);

	hardreset = NULL;
	if (sata_scr_valid(ap))
		hardreset = sata_std_hardreset;

	/* perform recovery */
	ata_do_eh(ap, ata_std_prereset, ata_std_softreset, hardreset,
		  ata_std_postreset);
}

static void pdc_post_internal_cmd(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	if (qc->flags & ATA_QCFLAG_FAILED)
		qc->err_mask |= AC_ERR_OTHER;

	/* make DMA engine forget about the failed command */
	if (qc->err_mask)
		pdc_reset_port(ap);
}

static void pdc_eng_timeout(struct ata_port *ap)
{
	struct ata_host *host = ap->host;
	u8 drv_stat;
	struct ata_queued_cmd *qc;
	unsigned long flags;

	DPRINTK("ENTER\n");

	spin_lock_irqsave(&host->lock, flags);

	qc = ata_qc_from_tag(ap, ap->active_tag);

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
	case ATA_PROT_NODATA:
		ata_port_printk(ap, KERN_ERR, "command timeout\n");
		drv_stat = ata_wait_idle(ap);
		qc->err_mask |= __ac_err_mask(drv_stat);
		break;

	default:
		drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);

		ata_port_printk(ap, KERN_ERR,
				"unknown timeout, cmd 0x%x stat 0x%x\n",
				qc->tf.command, drv_stat);

		qc->err_mask |= ac_err_mask(drv_stat);
		break;
	}

	spin_unlock_irqrestore(&host->lock, flags);
	ata_eh_qc_complete(qc);
	DPRINTK("EXIT\n");
}

static inline unsigned int pdc_host_intr( struct ata_port *ap,
                                          struct ata_queued_cmd *qc)
{
	unsigned int handled = 0;
	u32 tmp;
	void __iomem *mmio = (void __iomem *) ap->ioaddr.cmd_addr + PDC_GLOBAL_CTL;

	tmp = readl(mmio);
	if (tmp & PDC_ERR_MASK) {
		qc->err_mask |= AC_ERR_DEV;
		pdc_reset_port(ap);
	}

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
	case ATA_PROT_NODATA:
	case ATA_PROT_ATAPI_DMA:
		qc->err_mask |= ac_err_mask(ata_wait_idle(ap));
		ata_qc_complete(qc);
		handled = 1;
		break;

        default:
		ap->stats.idle_irq++;
		break;
        }

	return handled;
}

static void pdc_irq_clear(struct ata_port *ap)
{
	struct ata_host *host = ap->host;
	void __iomem *mmio = host->mmio_base;

	readl(mmio + PDC_INT_SEQMASK);
}

static irqreturn_t pdc_interrupt (int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	struct ata_port *ap;
	u32 mask = 0;
	unsigned int i, tmp;
	unsigned int handled = 0;
	void __iomem *mmio_base;

	VPRINTK("ENTER\n");

	if (!host || !host->mmio_base) {
		VPRINTK("QUICK EXIT\n");
		return IRQ_NONE;
	}

	mmio_base = host->mmio_base;

	/* reading should also clear interrupts */
	mask = readl(mmio_base + PDC_INT_SEQMASK);

	if (mask == 0xffffffff) {
		VPRINTK("QUICK EXIT 2\n");
		return IRQ_NONE;
	}

	spin_lock(&host->lock);

	mask &= 0xffff;		/* only 16 tags possible */
	if (!mask) {
		VPRINTK("QUICK EXIT 3\n");
		goto done_irq;
	}

	writel(mask, mmio_base + PDC_INT_SEQMASK);

	for (i = 0; i < host->n_ports; i++) {
		VPRINTK("port %u\n", i);
		ap = host->ports[i];
		tmp = mask & (1 << (i + 1));
		if (tmp && ap &&
		    !(ap->flags & ATA_FLAG_DISABLED)) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING)))
				handled += pdc_host_intr(ap, qc);
		}
	}

	VPRINTK("EXIT\n");

done_irq:
	spin_unlock(&host->lock);
	return IRQ_RETVAL(handled);
}

static inline void pdc_packet_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pdc_port_priv *pp = ap->private_data;
	unsigned int port_no = ap->port_no;
	u8 seq = (u8) (port_no + 1);

	VPRINTK("ENTER, ap %p\n", ap);

	writel(0x00000001, ap->host->mmio_base + (seq * 4));
	readl(ap->host->mmio_base + (seq * 4));	/* flush */

	pp->pkt[2] = seq;
	wmb();			/* flush PRD, pkt writes */
	writel(pp->pkt_dma, (void __iomem *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
	readl((void __iomem *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT); /* flush */
}

static unsigned int pdc_wait_for_drq(struct ata_port *ap)
{ 
	void __iomem *port_mmio = (void __iomem *) ap->ioaddr.cmd_addr;
	unsigned int i;
	unsigned int status;

	/* Following pdc-ultra's WaitForDrq() we loop here until BSY
	 * is clear and DRQ is set in altstatus. We could possibly call
	 * ata_busy_wait() and loop until DRQ is set, but since we don't
	 * know how much time a call to ata_busy_wait() took, we don't
	 * know when to time out the outer loop.
	 */
	for(i = 0; i < 1000; ++i) {
		status = readb(port_mmio + PDC_ALTSTATUS);
		if (status == 0xFF)
			break;
		if (status & ATA_BUSY)
			;
		else if (status & (ATA_DRQ | ATA_ERR))
			break;
		mdelay(1);
	}
	if (i >= 1000)
		ata_port_printk(ap, KERN_WARNING, "%s timed out\n", __FUNCTION__);
	return status;
}

static unsigned int pdc_wait_on_busy(struct ata_port *ap)
{
	unsigned int status = ata_busy_wait(ap, ATA_BUSY, 1000);
	if (status != 0xff && (status & ATA_BUSY))
		ata_port_printk(ap, KERN_WARNING, "%s timed out\n", __FUNCTION__);
	return status;
}

static void pdc_issue_atapi_pkt_cmd(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *port_mmio = (void __iomem *) ap->ioaddr.cmd_addr;
	void __iomem *host_mmio = ap->host->mmio_base;
	unsigned int nbytes;
	unsigned int tmp;

	writeb(0x00, port_mmio + PDC_CTLSTAT); /* route drive INT to SEQ 0 */
	writeb(PDC_SEQCNTRL_INT_MASK, host_mmio + 0); /* but mask SEQ 0 INT */

	/* select drive */
	if (sata_scr_valid(ap)) {
		tmp = PDC_DEVICE_SATA;
	} else {
		tmp = ATA_DEVICE_OBS;
		if (qc->dev->devno != 0)
			tmp |= ATA_DEV1;
	}
	writeb(tmp, port_mmio + PDC_DEVICE);
	pdc_wait_on_busy(ap);

	writeb(0x00, port_mmio + PDC_SECTOR_COUNT);
	writeb(0x00, port_mmio + PDC_SECTOR_NUMBER);

	/* set feature and byte counter registers */
	if (qc->tf.protocol != ATA_PROT_ATAPI_DMA) {
		tmp = PDC_FEATURE_ATAPI_PIO;
		/* set byte counter register to real transfer byte count */
		nbytes = qc->nbytes;
		if (!nbytes)
			nbytes = qc->nsect << 9;
		if (nbytes > 0xffff)
			nbytes = 0xffff;
	} else {
		tmp = PDC_FEATURE_ATAPI_DMA;
		/* set byte counter register to 0 */
		nbytes = 0;
	}
	writeb(tmp, port_mmio + PDC_FEATURE);
	writeb(nbytes & 0xFF, port_mmio + PDC_CYLINDER_LOW);
	writeb((nbytes >> 8) & 0xFF, port_mmio + PDC_CYLINDER_HIGH);

	/* send ATAPI packet command 0xA0 */
	writeb(ATA_CMD_PACKET, port_mmio + PDC_COMMAND);

	/* pdc_qc_issue_prot() currently sends ATAPI PIO packets back
	 * to libata. If we start handling those packets ourselves,
	 * then we must busy-wait for INT (CTLSTAT bit 27) at this point
	 * if the device has ATA_DFLAG_CDB_INTR set.
	 */

	pdc_wait_for_drq(ap);

	/* now the device only waits for the CDB */
}

static unsigned int pdc_qc_issue_prot(struct ata_queued_cmd *qc)
{
	switch (qc->tf.protocol) {
	case ATA_PROT_ATAPI_DMA:
		pdc_issue_atapi_pkt_cmd(qc);
		/*FALLTHROUGH*/
	case ATA_PROT_DMA:
	case ATA_PROT_NODATA:
		pdc_packet_start(qc);
		return 0;

	default:
		break;
	}

	return ata_qc_issue_prot(qc);
}

static void pdc_tf_load_mmio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	WARN_ON (tf->protocol == ATA_PROT_DMA ||
		 tf->protocol == ATA_PROT_NODATA);
	ata_tf_load(ap, tf);
}


static void pdc_exec_command_mmio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	WARN_ON (tf->protocol == ATA_PROT_DMA ||
		 tf->protocol == ATA_PROT_NODATA);
	ata_exec_command(ap, tf);
}

static int pdc_check_atapi_dma(struct ata_queued_cmd *qc)
{
	u8 *scsicmd = qc->scsicmd->cmnd;
	int pio = 1; /* atapi dma off by default */

	/* Whitelist commands that may use DMA. */
	switch (scsicmd[0]) {
	case WRITE_12:
	case WRITE_10:
	case WRITE_6:
	case READ_12:
	case READ_10:
	case READ_6:
	case 0xad: /* READ_DVD_STRUCTURE */
	case 0xbe: /* READ_CD */
		pio = 0;
	}
	/* -45150 (FFFF4FA2) to -1 (FFFFFFFF) shall use PIO mode */
	if (scsicmd[0] == WRITE_10) {
		unsigned int lba;
		lba = (scsicmd[2] << 24) | (scsicmd[3] << 16) | (scsicmd[4] << 8) | scsicmd[5];
		if (lba >= 0xFFFF4FA2)
			pio = 1;
	}
	return pio;
}

static int pdc_old_check_atapi_dma(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* First generation chips cannot use ATAPI DMA on SATA ports */
	if (sata_scr_valid(ap))
		return 1;
	return pdc_check_atapi_dma(qc);
}

static void pdc_ata_setup_port(struct ata_ioports *port, unsigned long base)
{
	port->cmd_addr		= base;
	port->data_addr		= base;
	port->feature_addr	=
	port->error_addr	= base + 0x4;
	port->nsect_addr	= base + 0x8;
	port->lbal_addr		= base + 0xc;
	port->lbam_addr		= base + 0x10;
	port->lbah_addr		= base + 0x14;
	port->device_addr	= base + 0x18;
	port->command_addr	=
	port->status_addr	= base + 0x1c;
	port->altstatus_addr	=
	port->ctl_addr		= base + 0x38;
}


static void pdc_host_init(unsigned int chip_id, struct ata_probe_ent *pe)
{
	void __iomem *mmio = pe->mmio_base;
	struct pdc_host_priv *hp = pe->private_data;
	int hotplug_offset;
	u32 tmp;

	if (hp->flags & PDC_FLAG_GEN_II)
		hotplug_offset = PDC2_SATA_PLUG_CSR;
	else
		hotplug_offset = PDC_SATA_PLUG_CSR;

	/*
	 * Except for the hotplug stuff, this is voodoo from the
	 * Promise driver.  Label this entire section
	 * "TODO: figure out why we do this"
	 */

	/* enable BMR_BURST, maybe change FIFO_SHD to 8 dwords */
	tmp = readl(mmio + PDC_FLASH_CTL);
	tmp |= 0x02000;	/* bit 13 (enable bmr burst) */
	if (!(hp->flags & PDC_FLAG_GEN_II))
		tmp |= 0x10000;	/* bit 16 (fifo threshold at 8 dw) */
	writel(tmp, mmio + PDC_FLASH_CTL);

	/* clear plug/unplug flags for all ports */
	tmp = readl(mmio + hotplug_offset);
	writel(tmp | 0xff, mmio + hotplug_offset);

	/* mask plug/unplug ints */
	tmp = readl(mmio + hotplug_offset);
	writel(tmp | 0xff0000, mmio + hotplug_offset);

	/* don't initialise TBG or SLEW on 2nd generation chips */
	if (hp->flags & PDC_FLAG_GEN_II)
		return;

	/* reduce TBG clock to 133 Mhz. */
	tmp = readl(mmio + PDC_TBG_MODE);
	tmp &= ~0x30000; /* clear bit 17, 16*/
	tmp |= 0x10000;  /* set bit 17:16 = 0:1 */
	writel(tmp, mmio + PDC_TBG_MODE);

	readl(mmio + PDC_TBG_MODE);	/* flush */
	msleep(10);

	/* adjust slew rate control register. */
	tmp = readl(mmio + PDC_SLEW_CTL);
	tmp &= 0xFFFFF03F; /* clear bit 11 ~ 6 */
	tmp  |= 0x00000900; /* set bit 11-9 = 100b , bit 8-6 = 100 */
	writel(tmp, mmio + PDC_SLEW_CTL);
}

static int pdc_ata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_probe_ent *probe_ent = NULL;
	struct pdc_host_priv *hp;
	unsigned long base;
	void __iomem *mmio_base;
	unsigned int board_idx = (unsigned int) ent->driver_data;
	int pci_dev_busy = 0;
	int rc;
	u8 tmp;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pci_dev_busy = 1;
		goto err_out;
	}

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	probe_ent = kzalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (probe_ent == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	probe_ent->dev = pci_dev_to_dev(pdev);
	INIT_LIST_HEAD(&probe_ent->node);

	mmio_base = pci_iomap(pdev, 3, 0);
	if (mmio_base == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}
	base = (unsigned long) mmio_base;

	hp = kzalloc(sizeof(*hp), GFP_KERNEL);
	if (hp == NULL) {
		rc = -ENOMEM;
		goto err_out_free_ent;
	}

	probe_ent->private_data = hp;

	probe_ent->sht		= pdc_port_info[board_idx].sht;
	probe_ent->port_flags	= pdc_port_info[board_idx].flags;
	probe_ent->pio_mask	= pdc_port_info[board_idx].pio_mask;
	probe_ent->mwdma_mask	= pdc_port_info[board_idx].mwdma_mask;
	probe_ent->udma_mask	= pdc_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= pdc_port_info[board_idx].port_ops;

       	probe_ent->irq = pdev->irq;
       	probe_ent->irq_flags = IRQF_SHARED;
	probe_ent->mmio_base = mmio_base;

	pdc_ata_setup_port(&probe_ent->port[0], base + 0x200);
	pdc_ata_setup_port(&probe_ent->port[1], base + 0x280);

	probe_ent->port[0].scr_addr = base + 0x400;
	probe_ent->port[1].scr_addr = base + 0x500;

	/* notice 4-port boards */
	switch (board_idx) {
	case board_40518:
		hp->flags |= PDC_FLAG_GEN_II;
		/* Fall through */
	case board_20319:
       		probe_ent->n_ports = 4;

		pdc_ata_setup_port(&probe_ent->port[2], base + 0x300);
		pdc_ata_setup_port(&probe_ent->port[3], base + 0x380);

		probe_ent->port[2].scr_addr = base + 0x600;
		probe_ent->port[3].scr_addr = base + 0x700;
		break;
	case board_2057x:
		hp->flags |= PDC_FLAG_GEN_II;
		/* Fall through */
	case board_2037x:
		/* TX2plus boards also have a PATA port */
		tmp = readb(mmio_base + PDC_FLASH_CTL+1);
		if (!(tmp & 0x80)) {
			probe_ent->n_ports = 3;
			pdc_ata_setup_port(&probe_ent->port[2], base + 0x300);
			hp->port_flags[2] = ATA_FLAG_SLAVE_POSS;
			printk(KERN_INFO DRV_NAME " PATA port found\n");
		} else
			probe_ent->n_ports = 2;
		hp->port_flags[0] = ATA_FLAG_SATA;
		hp->port_flags[1] = ATA_FLAG_SATA;
		break;
	case board_20619:
		probe_ent->n_ports = 4;

		pdc_ata_setup_port(&probe_ent->port[2], base + 0x300);
		pdc_ata_setup_port(&probe_ent->port[3], base + 0x380);

		probe_ent->port[2].scr_addr = base + 0x600;
		probe_ent->port[3].scr_addr = base + 0x700;
		break;
	default:
		BUG();
		break;
	}

	pci_set_master(pdev);

	/* initialize adapter */
	pdc_host_init(board_idx, probe_ent);

	/* FIXME: Need any other frees than hp? */
	if (!ata_device_add(probe_ent))
		kfree(hp);

	kfree(probe_ent);

	return 0;

err_out_free_ent:
	kfree(probe_ent);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	if (!pci_dev_busy)
		pci_disable_device(pdev);
	return rc;
}


static int __init pdc_ata_init(void)
{
	return pci_register_driver(&pdc_ata_pci_driver);
}


static void __exit pdc_ata_exit(void)
{
	pci_unregister_driver(&pdc_ata_pci_driver);
}


MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("Promise ATA TX2/TX4/TX4000 low-level driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pdc_ata_pci_tbl);
MODULE_VERSION(DRV_VERSION);

module_init(pdc_ata_init);
module_exit(pdc_ata_exit);
