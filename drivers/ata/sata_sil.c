/*
 *  sata_sil.c - Silicon Image SATA
 *
 *  Maintained by:  Jeff Garzik <jgarzik@pobox.com>
 *  		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2003-2005 Red Hat, Inc.
 *  Copyright 2003 Benjamin Herrenschmidt
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
 *  Documentation for SiI 3112:
 *  http://gkernel.sourceforge.net/specs/sii/3112A_SiI-DS-0095-B2.pdf.bz2
 *
 *  Other errata and documentation available under NDA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"sata_sil"
#define DRV_VERSION	"2.3"

enum {
	SIL_MMIO_BAR		= 5,

	/*
	 * host flags
	 */
	SIL_FLAG_NO_SATA_IRQ	= (1 << 28),
	SIL_FLAG_RERR_ON_DMA_ACT = (1 << 29),
	SIL_FLAG_MOD15WRITE	= (1 << 30),

	SIL_DFL_PORT_FLAGS	= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO,
	SIL_DFL_LINK_FLAGS	= ATA_LFLAG_HRST_TO_RESUME,

	/*
	 * Controller IDs
	 */
	sil_3112		= 0,
	sil_3112_no_sata_irq	= 1,
	sil_3512		= 2,
	sil_3114		= 3,

	/*
	 * Register offsets
	 */
	SIL_SYSCFG		= 0x48,

	/*
	 * Register bits
	 */
	/* SYSCFG */
	SIL_MASK_IDE0_INT	= (1 << 22),
	SIL_MASK_IDE1_INT	= (1 << 23),
	SIL_MASK_IDE2_INT	= (1 << 24),
	SIL_MASK_IDE3_INT	= (1 << 25),
	SIL_MASK_2PORT		= SIL_MASK_IDE0_INT | SIL_MASK_IDE1_INT,
	SIL_MASK_4PORT		= SIL_MASK_2PORT |
				  SIL_MASK_IDE2_INT | SIL_MASK_IDE3_INT,

	/* BMDMA/BMDMA2 */
	SIL_INTR_STEERING	= (1 << 1),

	SIL_DMA_ENABLE		= (1 << 0),  /* DMA run switch */
	SIL_DMA_RDWR		= (1 << 3),  /* DMA Rd-Wr */
	SIL_DMA_SATA_IRQ	= (1 << 4),  /* OR of all SATA IRQs */
	SIL_DMA_ACTIVE		= (1 << 16), /* DMA running */
	SIL_DMA_ERROR		= (1 << 17), /* PCI bus error */
	SIL_DMA_COMPLETE	= (1 << 18), /* cmd complete / IRQ pending */
	SIL_DMA_N_SATA_IRQ	= (1 << 6),  /* SATA_IRQ for the next channel */
	SIL_DMA_N_ACTIVE	= (1 << 24), /* ACTIVE for the next channel */
	SIL_DMA_N_ERROR		= (1 << 25), /* ERROR for the next channel */
	SIL_DMA_N_COMPLETE	= (1 << 26), /* COMPLETE for the next channel */

	/* SIEN */
	SIL_SIEN_N		= (1 << 16), /* triggered by SError.N */

	/*
	 * Others
	 */
	SIL_QUIRK_MOD15WRITE	= (1 << 0),
	SIL_QUIRK_UDMA5MAX	= (1 << 1),
};

static int sil_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
#ifdef CONFIG_PM
static int sil_pci_device_resume(struct pci_dev *pdev);
#endif
static void sil_dev_config(struct ata_device *dev);
static int sil_scr_read(struct ata_port *ap, unsigned int sc_reg, u32 *val);
static int sil_scr_write(struct ata_port *ap, unsigned int sc_reg, u32 val);
static int sil_set_mode(struct ata_link *link, struct ata_device **r_failed);
static void sil_freeze(struct ata_port *ap);
static void sil_thaw(struct ata_port *ap);


static const struct pci_device_id sil_pci_tbl[] = {
	{ PCI_VDEVICE(CMD, 0x3112), sil_3112 },
	{ PCI_VDEVICE(CMD, 0x0240), sil_3112 },
	{ PCI_VDEVICE(CMD, 0x3512), sil_3512 },
	{ PCI_VDEVICE(CMD, 0x3114), sil_3114 },
	{ PCI_VDEVICE(ATI, 0x436e), sil_3112 },
	{ PCI_VDEVICE(ATI, 0x4379), sil_3112_no_sata_irq },
	{ PCI_VDEVICE(ATI, 0x437a), sil_3112_no_sata_irq },

	{ }	/* terminate list */
};


/* TODO firmware versions should be added - eric */
static const struct sil_drivelist {
	const char *product;
	unsigned int quirk;
} sil_blacklist [] = {
	{ "ST320012AS",		SIL_QUIRK_MOD15WRITE },
	{ "ST330013AS",		SIL_QUIRK_MOD15WRITE },
	{ "ST340017AS",		SIL_QUIRK_MOD15WRITE },
	{ "ST360015AS",		SIL_QUIRK_MOD15WRITE },
	{ "ST380023AS",		SIL_QUIRK_MOD15WRITE },
	{ "ST3120023AS",	SIL_QUIRK_MOD15WRITE },
	{ "ST340014ASL",	SIL_QUIRK_MOD15WRITE },
	{ "ST360014ASL",	SIL_QUIRK_MOD15WRITE },
	{ "ST380011ASL",	SIL_QUIRK_MOD15WRITE },
	{ "ST3120022ASL",	SIL_QUIRK_MOD15WRITE },
	{ "ST3160021ASL",	SIL_QUIRK_MOD15WRITE },
	{ "Maxtor 4D060H3",	SIL_QUIRK_UDMA5MAX },
	{ }
};

static struct pci_driver sil_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= sil_pci_tbl,
	.probe			= sil_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= ata_pci_device_suspend,
	.resume			= sil_pci_device_resume,
#endif
};

static struct scsi_host_template sil_sht = {
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

static const struct ata_port_operations sil_ops = {
	.dev_config		= sil_dev_config,
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,
	.set_mode		= sil_set_mode,
	.bmdma_setup            = ata_bmdma_setup,
	.bmdma_start            = ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,
	.freeze			= sil_freeze,
	.thaw			= sil_thaw,
	.error_handler		= ata_bmdma_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.scr_read		= sil_scr_read,
	.scr_write		= sil_scr_write,
	.port_start		= ata_port_start,
};

static const struct ata_port_info sil_port_info[] = {
	/* sil_3112 */
	{
		.flags		= SIL_DFL_PORT_FLAGS | SIL_FLAG_MOD15WRITE,
		.link_flags	= SIL_DFL_LINK_FLAGS,
		.pio_mask	= 0x1f,			/* pio0-4 */
		.mwdma_mask	= 0x07,			/* mwdma0-2 */
		.udma_mask	= ATA_UDMA5,
		.port_ops	= &sil_ops,
	},
	/* sil_3112_no_sata_irq */
	{
		.flags		= SIL_DFL_PORT_FLAGS | SIL_FLAG_MOD15WRITE |
				  SIL_FLAG_NO_SATA_IRQ,
		.link_flags	= SIL_DFL_LINK_FLAGS,
		.pio_mask	= 0x1f,			/* pio0-4 */
		.mwdma_mask	= 0x07,			/* mwdma0-2 */
		.udma_mask	= ATA_UDMA5,
		.port_ops	= &sil_ops,
	},
	/* sil_3512 */
	{
		.flags		= SIL_DFL_PORT_FLAGS | SIL_FLAG_RERR_ON_DMA_ACT,
		.link_flags	= SIL_DFL_LINK_FLAGS,
		.pio_mask	= 0x1f,			/* pio0-4 */
		.mwdma_mask	= 0x07,			/* mwdma0-2 */
		.udma_mask	= ATA_UDMA5,
		.port_ops	= &sil_ops,
	},
	/* sil_3114 */
	{
		.flags		= SIL_DFL_PORT_FLAGS | SIL_FLAG_RERR_ON_DMA_ACT,
		.link_flags	= SIL_DFL_LINK_FLAGS,
		.pio_mask	= 0x1f,			/* pio0-4 */
		.mwdma_mask	= 0x07,			/* mwdma0-2 */
		.udma_mask	= ATA_UDMA5,
		.port_ops	= &sil_ops,
	},
};

/* per-port register offsets */
/* TODO: we can probably calculate rather than use a table */
static const struct {
	unsigned long tf;	/* ATA taskfile register block */
	unsigned long ctl;	/* ATA control/altstatus register block */
	unsigned long bmdma;	/* DMA register block */
	unsigned long bmdma2;	/* DMA register block #2 */
	unsigned long fifo_cfg;	/* FIFO Valid Byte Count and Control */
	unsigned long scr;	/* SATA control register block */
	unsigned long sien;	/* SATA Interrupt Enable register */
	unsigned long xfer_mode;/* data transfer mode register */
	unsigned long sfis_cfg;	/* SATA FIS reception config register */
} sil_port[] = {
	/* port 0 ... */
	/*   tf    ctl  bmdma  bmdma2  fifo    scr   sien   mode   sfis */
	{  0x80,  0x8A,   0x0,  0x10,  0x40, 0x100, 0x148,  0xb4, 0x14c },
	{  0xC0,  0xCA,   0x8,  0x18,  0x44, 0x180, 0x1c8,  0xf4, 0x1cc },
	{ 0x280, 0x28A, 0x200, 0x210, 0x240, 0x300, 0x348, 0x2b4, 0x34c },
	{ 0x2C0, 0x2CA, 0x208, 0x218, 0x244, 0x380, 0x3c8, 0x2f4, 0x3cc },
	/* ... port 3 */
};

MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("low-level driver for Silicon Image SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, sil_pci_tbl);
MODULE_VERSION(DRV_VERSION);

static int slow_down;
module_param(slow_down, int, 0444);
MODULE_PARM_DESC(slow_down, "Sledgehammer used to work around random problems, by limiting commands to 15 sectors (0=off, 1=on)");


static unsigned char sil_get_device_cache_line(struct pci_dev *pdev)
{
	u8 cache_line = 0;
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &cache_line);
	return cache_line;
}

/**
 *	sil_set_mode		-	wrap set_mode functions
 *	@link: link to set up
 *	@r_failed: returned device when we fail
 *
 *	Wrap the libata method for device setup as after the setup we need
 *	to inspect the results and do some configuration work
 */

static int sil_set_mode(struct ata_link *link, struct ata_device **r_failed)
{
	struct ata_port *ap = link->ap;
	void __iomem *mmio_base = ap->host->iomap[SIL_MMIO_BAR];
	void __iomem *addr = mmio_base + sil_port[ap->port_no].xfer_mode;
	struct ata_device *dev;
	u32 tmp, dev_mode[2] = { };
	int rc;

	rc = ata_do_set_mode(link, r_failed);
	if (rc)
		return rc;

	ata_link_for_each_dev(dev, link) {
		if (!ata_dev_enabled(dev))
			dev_mode[dev->devno] = 0;	/* PIO0/1/2 */
		else if (dev->flags & ATA_DFLAG_PIO)
			dev_mode[dev->devno] = 1;	/* PIO3/4 */
		else
			dev_mode[dev->devno] = 3;	/* UDMA */
		/* value 2 indicates MDMA */
	}

	tmp = readl(addr);
	tmp &= ~((1<<5) | (1<<4) | (1<<1) | (1<<0));
	tmp |= dev_mode[0];
	tmp |= (dev_mode[1] << 4);
	writel(tmp, addr);
	readl(addr);	/* flush */
	return 0;
}

static inline void __iomem *sil_scr_addr(struct ata_port *ap,
					 unsigned int sc_reg)
{
	void __iomem *offset = ap->ioaddr.scr_addr;

	switch (sc_reg) {
	case SCR_STATUS:
		return offset + 4;
	case SCR_ERROR:
		return offset + 8;
	case SCR_CONTROL:
		return offset;
	default:
		/* do nothing */
		break;
	}

	return NULL;
}

static int sil_scr_read(struct ata_port *ap, unsigned int sc_reg, u32 *val)
{
	void __iomem *mmio = sil_scr_addr(ap, sc_reg);

	if (mmio) {
		*val = readl(mmio);
		return 0;
	}
	return -EINVAL;
}

static int sil_scr_write(struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	void __iomem *mmio = sil_scr_addr(ap, sc_reg);

	if (mmio) {
		writel(val, mmio);
		return 0;
	}
	return -EINVAL;
}

static void sil_host_intr(struct ata_port *ap, u32 bmdma2)
{
	struct ata_eh_info *ehi = &ap->link.eh_info;
	struct ata_queued_cmd *qc = ata_qc_from_tag(ap, ap->link.active_tag);
	u8 status;

	if (unlikely(bmdma2 & SIL_DMA_SATA_IRQ)) {
		u32 serror;

		/* SIEN doesn't mask SATA IRQs on some 3112s.  Those
		 * controllers continue to assert IRQ as long as
		 * SError bits are pending.  Clear SError immediately.
		 */
		sil_scr_read(ap, SCR_ERROR, &serror);
		sil_scr_write(ap, SCR_ERROR, serror);

		/* Sometimes spurious interrupts occur, double check
		 * it's PHYRDY CHG.
		 */
		if (serror & SERR_PHYRDY_CHG) {
			ap->link.eh_info.serror |= serror;
			goto freeze;
		}

		if (!(bmdma2 & SIL_DMA_COMPLETE))
			return;
	}

	if (unlikely(!qc || (qc->tf.flags & ATA_TFLAG_POLLING))) {
		/* this sometimes happens, just clear IRQ */
		ata_chk_status(ap);
		return;
	}

	/* Check whether we are expecting interrupt in this state */
	switch (ap->hsm_task_state) {
	case HSM_ST_FIRST:
		/* Some pre-ATAPI-4 devices assert INTRQ
		 * at this state when ready to receive CDB.
		 */

		/* Check the ATA_DFLAG_CDB_INTR flag is enough here.
		 * The flag was turned on only for atapi devices.  No
		 * need to check ata_is_atapi(qc->tf.protocol) again.
		 */
		if (!(qc->dev->flags & ATA_DFLAG_CDB_INTR))
			goto err_hsm;
		break;
	case HSM_ST_LAST:
		if (ata_is_dma(qc->tf.protocol)) {
			/* clear DMA-Start bit */
			ap->ops->bmdma_stop(qc);

			if (bmdma2 & SIL_DMA_ERROR) {
				qc->err_mask |= AC_ERR_HOST_BUS;
				ap->hsm_task_state = HSM_ST_ERR;
			}
		}
		break;
	case HSM_ST:
		break;
	default:
		goto err_hsm;
	}

	/* check main status, clearing INTRQ */
	status = ata_chk_status(ap);
	if (unlikely(status & ATA_BUSY))
		goto err_hsm;

	/* ack bmdma irq events */
	ata_bmdma_irq_clear(ap);

	/* kick HSM in the ass */
	ata_hsm_move(ap, qc, status, 0);

	if (unlikely(qc->err_mask) && ata_is_dma(qc->tf.protocol))
		ata_ehi_push_desc(ehi, "BMDMA2 stat 0x%x", bmdma2);

	return;

 err_hsm:
	qc->err_mask |= AC_ERR_HSM;
 freeze:
	ata_port_freeze(ap);
}

static irqreturn_t sil_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	void __iomem *mmio_base = host->iomap[SIL_MMIO_BAR];
	int handled = 0;
	int i;

	spin_lock(&host->lock);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		u32 bmdma2 = readl(mmio_base + sil_port[ap->port_no].bmdma2);

		if (unlikely(!ap || ap->flags & ATA_FLAG_DISABLED))
			continue;

		/* turn off SATA_IRQ if not supported */
		if (ap->flags & SIL_FLAG_NO_SATA_IRQ)
			bmdma2 &= ~SIL_DMA_SATA_IRQ;

		if (bmdma2 == 0xffffffff ||
		    !(bmdma2 & (SIL_DMA_COMPLETE | SIL_DMA_SATA_IRQ)))
			continue;

		sil_host_intr(ap, bmdma2);
		handled = 1;
	}

	spin_unlock(&host->lock);

	return IRQ_RETVAL(handled);
}

static void sil_freeze(struct ata_port *ap)
{
	void __iomem *mmio_base = ap->host->iomap[SIL_MMIO_BAR];
	u32 tmp;

	/* global IRQ mask doesn't block SATA IRQ, turn off explicitly */
	writel(0, mmio_base + sil_port[ap->port_no].sien);

	/* plug IRQ */
	tmp = readl(mmio_base + SIL_SYSCFG);
	tmp |= SIL_MASK_IDE0_INT << ap->port_no;
	writel(tmp, mmio_base + SIL_SYSCFG);
	readl(mmio_base + SIL_SYSCFG);	/* flush */
}

static void sil_thaw(struct ata_port *ap)
{
	void __iomem *mmio_base = ap->host->iomap[SIL_MMIO_BAR];
	u32 tmp;

	/* clear IRQ */
	ata_chk_status(ap);
	ata_bmdma_irq_clear(ap);

	/* turn on SATA IRQ if supported */
	if (!(ap->flags & SIL_FLAG_NO_SATA_IRQ))
		writel(SIL_SIEN_N, mmio_base + sil_port[ap->port_no].sien);

	/* turn on IRQ */
	tmp = readl(mmio_base + SIL_SYSCFG);
	tmp &= ~(SIL_MASK_IDE0_INT << ap->port_no);
	writel(tmp, mmio_base + SIL_SYSCFG);
}

/**
 *	sil_dev_config - Apply device/host-specific errata fixups
 *	@dev: Device to be examined
 *
 *	After the IDENTIFY [PACKET] DEVICE step is complete, and a
 *	device is known to be present, this function is called.
 *	We apply two errata fixups which are specific to Silicon Image,
 *	a Seagate and a Maxtor fixup.
 *
 *	For certain Seagate devices, we must limit the maximum sectors
 *	to under 8K.
 *
 *	For certain Maxtor devices, we must not program the drive
 *	beyond udma5.
 *
 *	Both fixups are unfairly pessimistic.  As soon as I get more
 *	information on these errata, I will create a more exhaustive
 *	list, and apply the fixups to only the specific
 *	devices/hosts/firmwares that need it.
 *
 *	20040111 - Seagate drives affected by the Mod15Write bug are blacklisted
 *	The Maxtor quirk is in the blacklist, but I'm keeping the original
 *	pessimistic fix for the following reasons...
 *	- There seems to be less info on it, only one device gleaned off the
 *	Windows	driver, maybe only one is affected.  More info would be greatly
 *	appreciated.
 *	- But then again UDMA5 is hardly anything to complain about
 */
static void sil_dev_config(struct ata_device *dev)
{
	struct ata_port *ap = dev->link->ap;
	int print_info = ap->link.eh_context.i.flags & ATA_EHI_PRINTINFO;
	unsigned int n, quirks = 0;
	unsigned char model_num[ATA_ID_PROD_LEN + 1];

	ata_id_c_string(dev->id, model_num, ATA_ID_PROD, sizeof(model_num));

	for (n = 0; sil_blacklist[n].product; n++)
		if (!strcmp(sil_blacklist[n].product, model_num)) {
			quirks = sil_blacklist[n].quirk;
			break;
		}

	/* limit requests to 15 sectors */
	if (slow_down ||
	    ((ap->flags & SIL_FLAG_MOD15WRITE) &&
	     (quirks & SIL_QUIRK_MOD15WRITE))) {
		if (print_info)
			ata_dev_printk(dev, KERN_INFO, "applying Seagate "
				       "errata fix (mod15write workaround)\n");
		dev->max_sectors = 15;
		return;
	}

	/* limit to udma5 */
	if (quirks & SIL_QUIRK_UDMA5MAX) {
		if (print_info)
			ata_dev_printk(dev, KERN_INFO, "applying Maxtor "
				       "errata fix %s\n", model_num);
		dev->udma_mask &= ATA_UDMA5;
		return;
	}
}

static void sil_init_controller(struct ata_host *host)
{
	struct pci_dev *pdev = to_pci_dev(host->dev);
	void __iomem *mmio_base = host->iomap[SIL_MMIO_BAR];
	u8 cls;
	u32 tmp;
	int i;

	/* Initialize FIFO PCI bus arbitration */
	cls = sil_get_device_cache_line(pdev);
	if (cls) {
		cls >>= 3;
		cls++;  /* cls = (line_size/8)+1 */
		for (i = 0; i < host->n_ports; i++)
			writew(cls << 8 | cls,
			       mmio_base + sil_port[i].fifo_cfg);
	} else
		dev_printk(KERN_WARNING, &pdev->dev,
			   "cache line size not set.  Driver may not function\n");

	/* Apply R_ERR on DMA activate FIS errata workaround */
	if (host->ports[0]->flags & SIL_FLAG_RERR_ON_DMA_ACT) {
		int cnt;

		for (i = 0, cnt = 0; i < host->n_ports; i++) {
			tmp = readl(mmio_base + sil_port[i].sfis_cfg);
			if ((tmp & 0x3) != 0x01)
				continue;
			if (!cnt)
				dev_printk(KERN_INFO, &pdev->dev,
					   "Applying R_ERR on DMA activate "
					   "FIS errata fix\n");
			writel(tmp & ~0x3, mmio_base + sil_port[i].sfis_cfg);
			cnt++;
		}
	}

	if (host->n_ports == 4) {
		/* flip the magic "make 4 ports work" bit */
		tmp = readl(mmio_base + sil_port[2].bmdma);
		if ((tmp & SIL_INTR_STEERING) == 0)
			writel(tmp | SIL_INTR_STEERING,
			       mmio_base + sil_port[2].bmdma);
	}
}

static int sil_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	int board_id = ent->driver_data;
	const struct ata_port_info *ppi[] = { &sil_port_info[board_id], NULL };
	struct ata_host *host;
	void __iomem *mmio_base;
	int n_ports, rc;
	unsigned int i;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/* allocate host */
	n_ports = 2;
	if (board_id == sil_3114)
		n_ports = 4;

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, n_ports);
	if (!host)
		return -ENOMEM;

	/* acquire resources and fill host */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, 1 << SIL_MMIO_BAR, DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(pdev);
	if (rc)
		return rc;
	host->iomap = pcim_iomap_table(pdev);

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;

	mmio_base = host->iomap[SIL_MMIO_BAR];

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		struct ata_ioports *ioaddr = &ap->ioaddr;

		ioaddr->cmd_addr = mmio_base + sil_port[i].tf;
		ioaddr->altstatus_addr =
		ioaddr->ctl_addr = mmio_base + sil_port[i].ctl;
		ioaddr->bmdma_addr = mmio_base + sil_port[i].bmdma;
		ioaddr->scr_addr = mmio_base + sil_port[i].scr;
		ata_std_ports(ioaddr);

		ata_port_pbar_desc(ap, SIL_MMIO_BAR, -1, "mmio");
		ata_port_pbar_desc(ap, SIL_MMIO_BAR, sil_port[i].tf, "tf");
	}

	/* initialize and activate */
	sil_init_controller(host);

	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, sil_interrupt, IRQF_SHARED,
				 &sil_sht);
}

#ifdef CONFIG_PM
static int sil_pci_device_resume(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	sil_init_controller(host);
	ata_host_resume(host);

	return 0;
}
#endif

static int __init sil_init(void)
{
	return pci_register_driver(&sil_pci_driver);
}

static void __exit sil_exit(void)
{
	pci_unregister_driver(&sil_pci_driver);
}


module_init(sil_init);
module_exit(sil_exit);
