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
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/libata.h>
#include <asm/io.h>
#include "sata_promise.h"

#define DRV_NAME	"sata_promise"
#define DRV_VERSION	"1.04"


enum {
	PDC_PKT_SUBMIT		= 0x40, /* Command packet pointer addr */
	PDC_INT_SEQMASK		= 0x40,	/* Mask of asserted SEQ INTs */
	PDC_TBG_MODE		= 0x41,	/* TBG mode */
	PDC_FLASH_CTL		= 0x44, /* Flash control register */
	PDC_PCI_CTL		= 0x48, /* PCI control and status register */
	PDC_GLOBAL_CTL		= 0x48, /* Global control/status (per port) */
	PDC_CTLSTAT		= 0x60,	/* IDE control and status (per port) */
	PDC_SATA_PLUG_CSR	= 0x6C, /* SATA Plug control/status reg */
	PDC2_SATA_PLUG_CSR	= 0x60, /* SATAII Plug control/status reg */
	PDC_SLEW_CTL		= 0x470, /* slew rate control reg */

	PDC_ERR_MASK		= (1<<19) | (1<<20) | (1<<21) | (1<<22) |
				  (1<<8) | (1<<9) | (1<<10),

	board_2037x		= 0,	/* FastTrak S150 TX2plus */
	board_20319		= 1,	/* FastTrak S150 TX4 */
	board_20619		= 2,	/* FastTrak TX4000 */
	board_20771		= 3,	/* FastTrak TX2300 */
	board_2057x		= 4,	/* SATAII150 Tx2plus */
	board_40518		= 5,	/* SATAII150 Tx4 */

	PDC_HAS_PATA		= (1 << 1), /* PDC20375/20575 has PATA */

	PDC_RESET		= (1 << 11), /* HDMA reset */

	PDC_COMMON_FLAGS	= ATA_FLAG_NO_LEGACY | ATA_FLAG_SRST |
				  ATA_FLAG_MMIO | ATA_FLAG_NO_ATAPI |
				  ATA_FLAG_PIO_POLLING,
};


struct pdc_port_priv {
	u8			*pkt;
	dma_addr_t		pkt_dma;
};

struct pdc_host_priv {
	int			hotplug_offset;
};

static u32 pdc_sata_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void pdc_sata_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);
static int pdc_ata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static irqreturn_t pdc_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
static void pdc_eng_timeout(struct ata_port *ap);
static int pdc_port_start(struct ata_port *ap);
static void pdc_port_stop(struct ata_port *ap);
static void pdc_pata_phy_reset(struct ata_port *ap);
static void pdc_sata_phy_reset(struct ata_port *ap);
static void pdc_qc_prep(struct ata_queued_cmd *qc);
static void pdc_tf_load_mmio(struct ata_port *ap, const struct ata_taskfile *tf);
static void pdc_exec_command_mmio(struct ata_port *ap, const struct ata_taskfile *tf);
static void pdc_irq_clear(struct ata_port *ap);
static unsigned int pdc_qc_issue_prot(struct ata_queued_cmd *qc);
static void pdc_host_stop(struct ata_host_set *host_set);


static struct scsi_host_template pdc_ata_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static const struct ata_port_operations pdc_sata_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= pdc_tf_load_mmio,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= pdc_exec_command_mmio,
	.dev_select		= ata_std_dev_select,

	.phy_reset		= pdc_sata_phy_reset,

	.qc_prep		= pdc_qc_prep,
	.qc_issue		= pdc_qc_issue_prot,
	.eng_timeout		= pdc_eng_timeout,
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

	.phy_reset		= pdc_pata_phy_reset,

	.qc_prep		= pdc_qc_prep,
	.qc_issue		= pdc_qc_issue_prot,
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
		.host_flags	= PDC_COMMON_FLAGS | ATA_FLAG_SATA,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_sata_ops,
	},

	/* board_20319 */
	{
		.sht		= &pdc_ata_sht,
		.host_flags	= PDC_COMMON_FLAGS | ATA_FLAG_SATA,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_sata_ops,
	},

	/* board_20619 */
	{
		.sht		= &pdc_ata_sht,
		.host_flags	= PDC_COMMON_FLAGS | ATA_FLAG_SLAVE_POSS,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_pata_ops,
	},

	/* board_20771 */
	{
		.sht		= &pdc_ata_sht,
		.host_flags	= PDC_COMMON_FLAGS | ATA_FLAG_SATA,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_sata_ops,
	},

	/* board_2057x */
	{
		.sht		= &pdc_ata_sht,
		.host_flags	= PDC_COMMON_FLAGS | ATA_FLAG_SATA,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_sata_ops,
	},

	/* board_40518 */
	{
		.sht		= &pdc_ata_sht,
		.host_flags	= PDC_COMMON_FLAGS | ATA_FLAG_SATA,
		.pio_mask	= 0x1f, /* pio0-4 */
		.mwdma_mask	= 0x07, /* mwdma0-2 */
		.udma_mask	= 0x7f, /* udma0-6 ; FIXME */
		.port_ops	= &pdc_sata_ops,
	},
};

static const struct pci_device_id pdc_ata_pci_tbl[] = {
	{ PCI_VENDOR_ID_PROMISE, 0x3371, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3570, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3571, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3373, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3375, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3376, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },
	{ PCI_VENDOR_ID_PROMISE, 0x3574, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2057x },
	{ PCI_VENDOR_ID_PROMISE, 0x3d75, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2057x },
	{ PCI_VENDOR_ID_PROMISE, 0x3d73, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_2037x },

	{ PCI_VENDOR_ID_PROMISE, 0x3318, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20319 },
	{ PCI_VENDOR_ID_PROMISE, 0x3319, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20319 },
	{ PCI_VENDOR_ID_PROMISE, 0x3519, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20319 },
	{ PCI_VENDOR_ID_PROMISE, 0x3d17, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20319 },
	{ PCI_VENDOR_ID_PROMISE, 0x3d18, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_40518 },

	{ PCI_VENDOR_ID_PROMISE, 0x6629, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20619 },

	{ PCI_VENDOR_ID_PROMISE, 0x3570, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  board_20771 },
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
	struct device *dev = ap->host_set->dev;
	struct pdc_port_priv *pp;
	int rc;

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

	return 0;

err_out_kfree:
	kfree(pp);
err_out:
	ata_port_stop(ap);
	return rc;
}


static void pdc_port_stop(struct ata_port *ap)
{
	struct device *dev = ap->host_set->dev;
	struct pdc_port_priv *pp = ap->private_data;

	ap->private_data = NULL;
	dma_free_coherent(dev, 128, pp->pkt, pp->pkt_dma);
	kfree(pp);
	ata_port_stop(ap);
}


static void pdc_host_stop(struct ata_host_set *host_set)
{
	struct pdc_host_priv *hp = host_set->private_data;

	ata_pci_host_stop(host_set);

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

static void pdc_sata_phy_reset(struct ata_port *ap)
{
	pdc_reset_port(ap);
	sata_phy_reset(ap);
}

static void pdc_pata_phy_reset(struct ata_port *ap)
{
	/* FIXME: add cable detect.  Don't assume 40-pin cable */
	ap->cbl = ATA_CBL_PATA40;
	ap->udma_mask &= ATA_UDMA_MASK_40C;

	pdc_reset_port(ap);
	ata_port_probe(ap);
	ata_bus_reset(ap);
}

static u32 pdc_sata_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;
	return readl((void __iomem *) ap->ioaddr.scr_addr + (sc_reg * 4));
}


static void pdc_sata_scr_write (struct ata_port *ap, unsigned int sc_reg,
			       u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;
	writel(val, (void __iomem *) ap->ioaddr.scr_addr + (sc_reg * 4));
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

	default:
		break;
	}
}

static void pdc_eng_timeout(struct ata_port *ap)
{
	struct ata_host_set *host_set = ap->host_set;
	u8 drv_stat;
	struct ata_queued_cmd *qc;
	unsigned long flags;

	DPRINTK("ENTER\n");

	spin_lock_irqsave(&host_set->lock, flags);

	qc = ata_qc_from_tag(ap, ap->active_tag);
	if (!qc) {
		printk(KERN_ERR "ata%u: BUG: timeout without command\n",
		       ap->id);
		goto out;
	}

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
	case ATA_PROT_NODATA:
		printk(KERN_ERR "ata%u: command timeout\n", ap->id);
		drv_stat = ata_wait_idle(ap);
		qc->err_mask |= __ac_err_mask(drv_stat);
		break;

	default:
		drv_stat = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);

		printk(KERN_ERR "ata%u: unknown timeout, cmd 0x%x stat 0x%x\n",
		       ap->id, qc->tf.command, drv_stat);

		qc->err_mask |= ac_err_mask(drv_stat);
		break;
	}

out:
	spin_unlock_irqrestore(&host_set->lock, flags);
	if (qc)
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
	struct ata_host_set *host_set = ap->host_set;
	void __iomem *mmio = host_set->mmio_base;

	readl(mmio + PDC_INT_SEQMASK);
}

static irqreturn_t pdc_interrupt (int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	struct ata_port *ap;
	u32 mask = 0;
	unsigned int i, tmp;
	unsigned int handled = 0;
	void __iomem *mmio_base;

	VPRINTK("ENTER\n");

	if (!host_set || !host_set->mmio_base) {
		VPRINTK("QUICK EXIT\n");
		return IRQ_NONE;
	}

	mmio_base = host_set->mmio_base;

	/* reading should also clear interrupts */
	mask = readl(mmio_base + PDC_INT_SEQMASK);

	if (mask == 0xffffffff) {
		VPRINTK("QUICK EXIT 2\n");
		return IRQ_NONE;
	}

	spin_lock(&host_set->lock);

	mask &= 0xffff;		/* only 16 tags possible */
	if (!mask) {
		VPRINTK("QUICK EXIT 3\n");
		goto done_irq;
	}

	writel(mask, mmio_base + PDC_INT_SEQMASK);

	for (i = 0; i < host_set->n_ports; i++) {
		VPRINTK("port %u\n", i);
		ap = host_set->ports[i];
		tmp = mask & (1 << (i + 1));
		if (tmp && ap &&
		    !(ap->flags & ATA_FLAG_PORT_DISABLED)) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING)))
				handled += pdc_host_intr(ap, qc);
		}
	}

	VPRINTK("EXIT\n");

done_irq:
	spin_unlock(&host_set->lock);
	return IRQ_RETVAL(handled);
}

static inline void pdc_packet_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct pdc_port_priv *pp = ap->private_data;
	unsigned int port_no = ap->port_no;
	u8 seq = (u8) (port_no + 1);

	VPRINTK("ENTER, ap %p\n", ap);

	writel(0x00000001, ap->host_set->mmio_base + (seq * 4));
	readl(ap->host_set->mmio_base + (seq * 4));	/* flush */

	pp->pkt[2] = seq;
	wmb();			/* flush PRD, pkt writes */
	writel(pp->pkt_dma, (void __iomem *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT);
	readl((void __iomem *) ap->ioaddr.cmd_addr + PDC_PKT_SUBMIT); /* flush */
}

static unsigned int pdc_qc_issue_prot(struct ata_queued_cmd *qc)
{
	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
	case ATA_PROT_NODATA:
		pdc_packet_start(qc);
		return 0;

	case ATA_PROT_ATAPI_DMA:
		BUG();
		break;

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
	int hotplug_offset = hp->hotplug_offset;
	u32 tmp;

	/*
	 * Except for the hotplug stuff, this is voodoo from the
	 * Promise driver.  Label this entire section
	 * "TODO: figure out why we do this"
	 */

	/* change FIFO_SHD to 8 dwords, enable BMR_BURST */
	tmp = readl(mmio + PDC_FLASH_CTL);
	tmp |= 0x12000;	/* bit 16 (fifo 8 dw) and 13 (bmr burst?) */
	writel(tmp, mmio + PDC_FLASH_CTL);

	/* clear plug/unplug flags for all ports */
	tmp = readl(mmio + hotplug_offset);
	writel(tmp | 0xff, mmio + hotplug_offset);

	/* mask plug/unplug ints */
	tmp = readl(mmio + hotplug_offset);
	writel(tmp | 0xff0000, mmio + hotplug_offset);

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

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/*
	 * If this driver happens to only be useful on Apple's K2, then
	 * we should check that here as it has a normal Serverworks ID
	 */
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

	/* Set default hotplug offset */
	hp->hotplug_offset = PDC_SATA_PLUG_CSR;
	probe_ent->private_data = hp;

	probe_ent->sht		= pdc_port_info[board_idx].sht;
	probe_ent->host_flags	= pdc_port_info[board_idx].host_flags;
	probe_ent->pio_mask	= pdc_port_info[board_idx].pio_mask;
	probe_ent->mwdma_mask	= pdc_port_info[board_idx].mwdma_mask;
	probe_ent->udma_mask	= pdc_port_info[board_idx].udma_mask;
	probe_ent->port_ops	= pdc_port_info[board_idx].port_ops;

       	probe_ent->irq = pdev->irq;
       	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->mmio_base = mmio_base;

	pdc_ata_setup_port(&probe_ent->port[0], base + 0x200);
	pdc_ata_setup_port(&probe_ent->port[1], base + 0x280);

	probe_ent->port[0].scr_addr = base + 0x400;
	probe_ent->port[1].scr_addr = base + 0x500;

	/* notice 4-port boards */
	switch (board_idx) {
	case board_40518:
		/* Override hotplug offset for SATAII150 */
		hp->hotplug_offset = PDC2_SATA_PLUG_CSR;
		/* Fall through */
	case board_20319:
       		probe_ent->n_ports = 4;

		pdc_ata_setup_port(&probe_ent->port[2], base + 0x300);
		pdc_ata_setup_port(&probe_ent->port[3], base + 0x380);

		probe_ent->port[2].scr_addr = base + 0x600;
		probe_ent->port[3].scr_addr = base + 0x700;
		break;
	case board_2057x:
		/* Override hotplug offset for SATAII150 */
		hp->hotplug_offset = PDC2_SATA_PLUG_CSR;
		/* Fall through */
	case board_2037x:
		probe_ent->n_ports = 2;
		break;
	case board_20771:
		probe_ent->n_ports = 2;
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
	return pci_module_init(&pdc_ata_pci_driver);
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
