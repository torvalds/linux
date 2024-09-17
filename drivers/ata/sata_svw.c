// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  sata_svw.c - ServerWorks / Apple K2 SATA
 *
 *  Maintained by: Benjamin Herrenschmidt <benh@kernel.crashing.org> and
 *		   Jeff Garzik <jgarzik@pobox.com>
 *  		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2003 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *
 *  Bits from Jeff Garzik, Copyright RedHat, Inc.
 *
 *  This driver probably works with non-Apple versions of the
 *  Broadcom chipset...
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/driver-api/libata.rst
 *
 *  Hardware documentation available under NDA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi.h>
#include <linux/libata.h>
#include <linux/of.h>

#define DRV_NAME	"sata_svw"
#define DRV_VERSION	"2.3"

enum {
	/* ap->flags bits */
	K2_FLAG_SATA_8_PORTS		= (1 << 24),
	K2_FLAG_NO_ATAPI_DMA		= (1 << 25),
	K2_FLAG_BAR_POS_3			= (1 << 26),

	/* Taskfile registers offsets */
	K2_SATA_TF_CMD_OFFSET		= 0x00,
	K2_SATA_TF_DATA_OFFSET		= 0x00,
	K2_SATA_TF_ERROR_OFFSET		= 0x04,
	K2_SATA_TF_NSECT_OFFSET		= 0x08,
	K2_SATA_TF_LBAL_OFFSET		= 0x0c,
	K2_SATA_TF_LBAM_OFFSET		= 0x10,
	K2_SATA_TF_LBAH_OFFSET		= 0x14,
	K2_SATA_TF_DEVICE_OFFSET	= 0x18,
	K2_SATA_TF_CMDSTAT_OFFSET      	= 0x1c,
	K2_SATA_TF_CTL_OFFSET		= 0x20,

	/* DMA base */
	K2_SATA_DMA_CMD_OFFSET		= 0x30,

	/* SCRs base */
	K2_SATA_SCR_STATUS_OFFSET	= 0x40,
	K2_SATA_SCR_ERROR_OFFSET	= 0x44,
	K2_SATA_SCR_CONTROL_OFFSET	= 0x48,

	/* Others */
	K2_SATA_SICR1_OFFSET		= 0x80,
	K2_SATA_SICR2_OFFSET		= 0x84,
	K2_SATA_SIM_OFFSET		= 0x88,

	/* Port stride */
	K2_SATA_PORT_OFFSET		= 0x100,

	chip_svw4			= 0,
	chip_svw8			= 1,
	chip_svw42			= 2,	/* bar 3 */
	chip_svw43			= 3,	/* bar 5 */
};

static u8 k2_stat_check_status(struct ata_port *ap);


static int k2_sata_check_atapi_dma(struct ata_queued_cmd *qc)
{
	u8 cmnd = qc->scsicmd->cmnd[0];

	if (qc->ap->flags & K2_FLAG_NO_ATAPI_DMA)
		return -1;	/* ATAPI DMA not supported */
	else {
		switch (cmnd) {
		case READ_10:
		case READ_12:
		case READ_16:
		case WRITE_10:
		case WRITE_12:
		case WRITE_16:
			return 0;

		default:
			return -1;
		}

	}
}

static int k2_sata_scr_read(struct ata_link *link,
			    unsigned int sc_reg, u32 *val)
{
	if (sc_reg > SCR_CONTROL)
		return -EINVAL;
	*val = readl(link->ap->ioaddr.scr_addr + (sc_reg * 4));
	return 0;
}


static int k2_sata_scr_write(struct ata_link *link,
			     unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return -EINVAL;
	writel(val, link->ap->ioaddr.scr_addr + (sc_reg * 4));
	return 0;
}

static int k2_sata_softreset(struct ata_link *link,
			     unsigned int *class, unsigned long deadline)
{
	u8 dmactl;
	void __iomem *mmio = link->ap->ioaddr.bmdma_addr;

	dmactl = readb(mmio + ATA_DMA_CMD);

	/* Clear the start bit */
	if (dmactl & ATA_DMA_START) {
		dmactl &= ~ATA_DMA_START;
		writeb(dmactl, mmio + ATA_DMA_CMD);
	}

	return ata_sff_softreset(link, class, deadline);
}

static int k2_sata_hardreset(struct ata_link *link,
			     unsigned int *class, unsigned long deadline)
{
	u8 dmactl;
	void __iomem *mmio = link->ap->ioaddr.bmdma_addr;

	dmactl = readb(mmio + ATA_DMA_CMD);

	/* Clear the start bit */
	if (dmactl & ATA_DMA_START) {
		dmactl &= ~ATA_DMA_START;
		writeb(dmactl, mmio + ATA_DMA_CMD);
	}

	return sata_sff_hardreset(link, class, deadline);
}

static void k2_sata_tf_load(struct ata_port *ap, const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		writeb(tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}
	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		writew(tf->feature | (((u16)tf->hob_feature) << 8),
		       ioaddr->feature_addr);
		writew(tf->nsect | (((u16)tf->hob_nsect) << 8),
		       ioaddr->nsect_addr);
		writew(tf->lbal | (((u16)tf->hob_lbal) << 8),
		       ioaddr->lbal_addr);
		writew(tf->lbam | (((u16)tf->hob_lbam) << 8),
		       ioaddr->lbam_addr);
		writew(tf->lbah | (((u16)tf->hob_lbah) << 8),
		       ioaddr->lbah_addr);
	} else if (is_addr) {
		writew(tf->feature, ioaddr->feature_addr);
		writew(tf->nsect, ioaddr->nsect_addr);
		writew(tf->lbal, ioaddr->lbal_addr);
		writew(tf->lbam, ioaddr->lbam_addr);
		writew(tf->lbah, ioaddr->lbah_addr);
	}

	if (tf->flags & ATA_TFLAG_DEVICE)
		writeb(tf->device, ioaddr->device_addr);

	ata_wait_idle(ap);
}


static void k2_sata_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u16 nsect, lbal, lbam, lbah, error;

	tf->status = k2_stat_check_status(ap);
	tf->device = readw(ioaddr->device_addr);
	error = readw(ioaddr->error_addr);
	nsect = readw(ioaddr->nsect_addr);
	lbal = readw(ioaddr->lbal_addr);
	lbam = readw(ioaddr->lbam_addr);
	lbah = readw(ioaddr->lbah_addr);

	tf->error = error;
	tf->nsect = nsect;
	tf->lbal = lbal;
	tf->lbam = lbam;
	tf->lbah = lbah;

	if (tf->flags & ATA_TFLAG_LBA48) {
		tf->hob_feature = error >> 8;
		tf->hob_nsect = nsect >> 8;
		tf->hob_lbal = lbal >> 8;
		tf->hob_lbam = lbam >> 8;
		tf->hob_lbah = lbah >> 8;
	}
}

/**
 *	k2_bmdma_setup_mmio - Set up PCI IDE BMDMA transaction (MMIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */

static void k2_bmdma_setup_mmio(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 dmactl;
	void __iomem *mmio = ap->ioaddr.bmdma_addr;

	/* load PRD table addr. */
	mb();	/* make sure PRD table writes are visible to controller */
	writel(ap->bmdma_prd_dma, mmio + ATA_DMA_TABLE_OFS);

	/* specify data direction, triple-check start bit is clear */
	dmactl = readb(mmio + ATA_DMA_CMD);
	dmactl &= ~(ATA_DMA_WR | ATA_DMA_START);
	if (!rw)
		dmactl |= ATA_DMA_WR;
	writeb(dmactl, mmio + ATA_DMA_CMD);

	/* issue r/w command if this is not a ATA DMA command*/
	if (qc->tf.protocol != ATA_PROT_DMA)
		ap->ops->sff_exec_command(ap, &qc->tf);
}

/**
 *	k2_bmdma_start_mmio - Start a PCI IDE BMDMA transaction (MMIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */

static void k2_bmdma_start_mmio(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *mmio = ap->ioaddr.bmdma_addr;
	u8 dmactl;

	/* start host DMA transaction */
	dmactl = readb(mmio + ATA_DMA_CMD);
	writeb(dmactl | ATA_DMA_START, mmio + ATA_DMA_CMD);
	/* This works around possible data corruption.

	   On certain SATA controllers that can be seen when the r/w
	   command is given to the controller before the host DMA is
	   started.

	   On a Read command, the controller would initiate the
	   command to the drive even before it sees the DMA
	   start. When there are very fast drives connected to the
	   controller, or when the data request hits in the drive
	   cache, there is the possibility that the drive returns a
	   part or all of the requested data to the controller before
	   the DMA start is issued.  In this case, the controller
	   would become confused as to what to do with the data.  In
	   the worst case when all the data is returned back to the
	   controller, the controller could hang. In other cases it
	   could return partial data returning in data
	   corruption. This problem has been seen in PPC systems and
	   can also appear on an system with very fast disks, where
	   the SATA controller is sitting behind a number of bridges,
	   and hence there is significant latency between the r/w
	   command and the start command. */
	/* issue r/w command if the access is to ATA */
	if (qc->tf.protocol == ATA_PROT_DMA)
		ap->ops->sff_exec_command(ap, &qc->tf);
}


static u8 k2_stat_check_status(struct ata_port *ap)
{
	return readl(ap->ioaddr.status_addr);
}

static int k2_sata_show_info(struct seq_file *m, struct Scsi_Host *shost)
{
	struct ata_port *ap;
	struct device_node *np;
	int index;

	/* Find  the ata_port */
	ap = ata_shost_to_port(shost);
	if (ap == NULL)
		return 0;

	/* Find the OF node for the PCI device proper */
	np = pci_device_to_OF_node(to_pci_dev(ap->host->dev));
	if (np == NULL)
		return 0;

	/* Match it to a port node */
	index = (ap == ap->host->ports[0]) ? 0 : 1;
	for (np = np->child; np != NULL; np = np->sibling) {
		const u32 *reg = of_get_property(np, "reg", NULL);
		if (!reg)
			continue;
		if (index == *reg) {
			seq_printf(m, "devspec: %pOF\n", np);
			break;
		}
	}
	return 0;
}

static struct scsi_host_template k2_sata_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
	.show_info		= k2_sata_show_info,
};


static struct ata_port_operations k2_sata_ops = {
	.inherits		= &ata_bmdma_port_ops,
	.softreset              = k2_sata_softreset,
	.hardreset              = k2_sata_hardreset,
	.sff_tf_load		= k2_sata_tf_load,
	.sff_tf_read		= k2_sata_tf_read,
	.sff_check_status	= k2_stat_check_status,
	.check_atapi_dma	= k2_sata_check_atapi_dma,
	.bmdma_setup		= k2_bmdma_setup_mmio,
	.bmdma_start		= k2_bmdma_start_mmio,
	.scr_read		= k2_sata_scr_read,
	.scr_write		= k2_sata_scr_write,
};

static const struct ata_port_info k2_port_info[] = {
	/* chip_svw4 */
	{
		.flags		= ATA_FLAG_SATA | K2_FLAG_NO_ATAPI_DMA,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &k2_sata_ops,
	},
	/* chip_svw8 */
	{
		.flags		= ATA_FLAG_SATA | K2_FLAG_NO_ATAPI_DMA |
				  K2_FLAG_SATA_8_PORTS,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &k2_sata_ops,
	},
	/* chip_svw42 */
	{
		.flags		= ATA_FLAG_SATA | K2_FLAG_BAR_POS_3,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &k2_sata_ops,
	},
	/* chip_svw43 */
	{
		.flags		= ATA_FLAG_SATA,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &k2_sata_ops,
	},
};

static void k2_sata_setup_port(struct ata_ioports *port, void __iomem *base)
{
	port->cmd_addr		= base + K2_SATA_TF_CMD_OFFSET;
	port->data_addr		= base + K2_SATA_TF_DATA_OFFSET;
	port->feature_addr	=
	port->error_addr	= base + K2_SATA_TF_ERROR_OFFSET;
	port->nsect_addr	= base + K2_SATA_TF_NSECT_OFFSET;
	port->lbal_addr		= base + K2_SATA_TF_LBAL_OFFSET;
	port->lbam_addr		= base + K2_SATA_TF_LBAM_OFFSET;
	port->lbah_addr		= base + K2_SATA_TF_LBAH_OFFSET;
	port->device_addr	= base + K2_SATA_TF_DEVICE_OFFSET;
	port->command_addr	=
	port->status_addr	= base + K2_SATA_TF_CMDSTAT_OFFSET;
	port->altstatus_addr	=
	port->ctl_addr		= base + K2_SATA_TF_CTL_OFFSET;
	port->bmdma_addr	= base + K2_SATA_DMA_CMD_OFFSET;
	port->scr_addr		= base + K2_SATA_SCR_STATUS_OFFSET;
}


static int k2_sata_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct ata_port_info *ppi[] =
		{ &k2_port_info[ent->driver_data], NULL };
	struct ata_host *host;
	void __iomem *mmio_base;
	int n_ports, i, rc, bar_pos;

	ata_print_version_once(&pdev->dev, DRV_VERSION);

	/* allocate host */
	n_ports = 4;
	if (ppi[0]->flags & K2_FLAG_SATA_8_PORTS)
		n_ports = 8;

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, n_ports);
	if (!host)
		return -ENOMEM;

	bar_pos = 5;
	if (ppi[0]->flags & K2_FLAG_BAR_POS_3)
		bar_pos = 3;
	/*
	 * If this driver happens to only be useful on Apple's K2, then
	 * we should check that here as it has a normal Serverworks ID
	 */
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/*
	 * Check if we have resources mapped at all (second function may
	 * have been disabled by firmware)
	 */
	if (pci_resource_len(pdev, bar_pos) == 0) {
		/* In IDE mode we need to pin the device to ensure that
			pcim_release does not clear the busmaster bit in config
			space, clearing causes busmaster DMA to fail on
			ports 3 & 4 */
		pcim_pin_device(pdev);
		return -ENODEV;
	}

	/* Request and iomap PCI regions */
	rc = pcim_iomap_regions(pdev, 1 << bar_pos, DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(pdev);
	if (rc)
		return rc;
	host->iomap = pcim_iomap_table(pdev);
	mmio_base = host->iomap[bar_pos];

	/* different controllers have different number of ports - currently 4 or 8 */
	/* All ports are on the same function. Multi-function device is no
	 * longer available. This should not be seen in any system. */
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		unsigned int offset = i * K2_SATA_PORT_OFFSET;

		k2_sata_setup_port(&ap->ioaddr, mmio_base + offset);

		ata_port_pbar_desc(ap, 5, -1, "mmio");
		ata_port_pbar_desc(ap, 5, offset, "port");
	}

	rc = dma_set_mask_and_coherent(&pdev->dev, ATA_DMA_MASK);
	if (rc)
		return rc;

	/* Clear a magic bit in SCR1 according to Darwin, those help
	 * some funky seagate drives (though so far, those were already
	 * set by the firmware on the machines I had access to)
	 */
	writel(readl(mmio_base + K2_SATA_SICR1_OFFSET) & ~0x00040000,
	       mmio_base + K2_SATA_SICR1_OFFSET);

	/* Clear SATA error & interrupts we don't use */
	writel(0xffffffff, mmio_base + K2_SATA_SCR_ERROR_OFFSET);
	writel(0x0, mmio_base + K2_SATA_SIM_OFFSET);

	pci_set_master(pdev);
	return ata_host_activate(host, pdev->irq, ata_bmdma_interrupt,
				 IRQF_SHARED, &k2_sata_sht);
}

/* 0x240 is device ID for Apple K2 device
 * 0x241 is device ID for Serverworks Frodo4
 * 0x242 is device ID for Serverworks Frodo8
 * 0x24a is device ID for BCM5785 (aka HT1000) HT southbridge integrated SATA
 * controller
 * */
static const struct pci_device_id k2_sata_pci_tbl[] = {
	{ PCI_VDEVICE(SERVERWORKS, 0x0240), chip_svw4 },
	{ PCI_VDEVICE(SERVERWORKS, 0x0241), chip_svw8 },
	{ PCI_VDEVICE(SERVERWORKS, 0x0242), chip_svw4 },
	{ PCI_VDEVICE(SERVERWORKS, 0x024a), chip_svw4 },
	{ PCI_VDEVICE(SERVERWORKS, 0x024b), chip_svw4 },
	{ PCI_VDEVICE(SERVERWORKS, 0x0410), chip_svw42 },
	{ PCI_VDEVICE(SERVERWORKS, 0x0411), chip_svw43 },

	{ }
};

static struct pci_driver k2_sata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= k2_sata_pci_tbl,
	.probe			= k2_sata_init_one,
	.remove			= ata_pci_remove_one,
};

module_pci_driver(k2_sata_pci_driver);

MODULE_AUTHOR("Benjamin Herrenschmidt");
MODULE_DESCRIPTION("low-level driver for K2 SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, k2_sata_pci_tbl);
MODULE_VERSION(DRV_VERSION);
