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
 *  Hardware documentation available under NDA.
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

#ifdef CONFIG_PPC_OF
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#endif /* CONFIG_PPC_OF */

#define DRV_NAME	"sata_svw"
#define DRV_VERSION	"2.1"

enum {
	/* ap->flags bits */
	K2_FLAG_SATA_8_PORTS		= (1 << 24),
	K2_FLAG_NO_ATAPI_DMA		= (1 << 25),

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

	board_svw4			= 0,
	board_svw8			= 1,
};

static u8 k2_stat_check_status(struct ata_port *ap);


static int k2_sata_check_atapi_dma(struct ata_queued_cmd *qc)
{
	if (qc->ap->flags & K2_FLAG_NO_ATAPI_DMA)
		return -1;	/* ATAPI DMA not supported */

	return 0;
}

static u32 k2_sata_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;
	return readl((void __iomem *) ap->ioaddr.scr_addr + (sc_reg * 4));
}


static void k2_sata_scr_write (struct ata_port *ap, unsigned int sc_reg,
			       u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;
	writel(val, (void __iomem *) ap->ioaddr.scr_addr + (sc_reg * 4));
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
	u16 nsect, lbal, lbam, lbah, feature;

	tf->command = k2_stat_check_status(ap);
	tf->device = readw(ioaddr->device_addr);
	feature = readw(ioaddr->error_addr);
	nsect = readw(ioaddr->nsect_addr);
	lbal = readw(ioaddr->lbal_addr);
	lbam = readw(ioaddr->lbam_addr);
	lbah = readw(ioaddr->lbah_addr);

	tf->feature = feature;
	tf->nsect = nsect;
	tf->lbal = lbal;
	tf->lbam = lbam;
	tf->lbah = lbah;

	if (tf->flags & ATA_TFLAG_LBA48) {
		tf->hob_feature = feature >> 8;
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

static void k2_bmdma_setup_mmio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 dmactl;
	void __iomem *mmio = (void __iomem *) ap->ioaddr.bmdma_addr;
	/* load PRD table addr. */
	mb();	/* make sure PRD table writes are visible to controller */
	writel(ap->prd_dma, mmio + ATA_DMA_TABLE_OFS);

	/* specify data direction, triple-check start bit is clear */
	dmactl = readb(mmio + ATA_DMA_CMD);
	dmactl &= ~(ATA_DMA_WR | ATA_DMA_START);
	if (!rw)
		dmactl |= ATA_DMA_WR;
	writeb(dmactl, mmio + ATA_DMA_CMD);

	/* issue r/w command if this is not a ATA DMA command*/
	if (qc->tf.protocol != ATA_PROT_DMA)
		ap->ops->exec_command(ap, &qc->tf);
}

/**
 *	k2_bmdma_start_mmio - Start a PCI IDE BMDMA transaction (MMIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */

static void k2_bmdma_start_mmio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *mmio = (void __iomem *) ap->ioaddr.bmdma_addr;
	u8 dmactl;

	/* start host DMA transaction */
	dmactl = readb(mmio + ATA_DMA_CMD);
	writeb(dmactl | ATA_DMA_START, mmio + ATA_DMA_CMD);
	/* There is a race condition in certain SATA controllers that can
	   be seen when the r/w command is given to the controller before the
	   host DMA is started. On a Read command, the controller would initiate
	   the command to the drive even before it sees the DMA start. When there
	   are very fast drives connected to the controller, or when the data request
	   hits in the drive cache, there is the possibility that the drive returns a part
	   or all of the requested data to the controller before the DMA start is issued.
	   In this case, the controller would become confused as to what to do with the data.
	   In the worst case when all the data is returned back to the controller, the
	   controller could hang. In other cases it could return partial data returning
	   in data corruption. This problem has been seen in PPC systems and can also appear
	   on an system with very fast disks, where the SATA controller is sitting behind a
	   number of bridges, and hence there is significant latency between the r/w command
	   and the start command. */
	/* issue r/w command if the access is to ATA*/
	if (qc->tf.protocol == ATA_PROT_DMA)
		ap->ops->exec_command(ap, &qc->tf);
}


static u8 k2_stat_check_status(struct ata_port *ap)
{
       	return readl((void __iomem *) ap->ioaddr.status_addr);
}

#ifdef CONFIG_PPC_OF
/*
 * k2_sata_proc_info
 * inout : decides on the direction of the dataflow and the meaning of the
 *	   variables
 * buffer: If inout==FALSE data is being written to it else read from it
 * *start: If inout==FALSE start of the valid data in the buffer
 * offset: If inout==FALSE offset from the beginning of the imaginary file
 *	   from which we start writing into the buffer
 * length: If inout==FALSE max number of bytes to be written into the buffer
 *	   else number of bytes in the buffer
 */
static int k2_sata_proc_info(struct Scsi_Host *shost, char *page, char **start,
			     off_t offset, int count, int inout)
{
	struct ata_port *ap;
	struct device_node *np;
	int len, index;

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
		const u32 *reg = get_property(np, "reg", NULL);
		if (!reg)
			continue;
		if (index == *reg)
			break;
	}
	if (np == NULL)
		return 0;

	len = sprintf(page, "devspec: %s\n", np->full_name);

	return len;
}
#endif /* CONFIG_PPC_OF */


static struct scsi_host_template k2_sata_sht = {
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
#ifdef CONFIG_PPC_OF
	.proc_info		= k2_sata_proc_info,
#endif
	.bios_param		= ata_std_bios_param,
};


static const struct ata_port_operations k2_sata_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= k2_sata_tf_load,
	.tf_read		= k2_sata_tf_read,
	.check_status		= k2_stat_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,
	.check_atapi_dma	= k2_sata_check_atapi_dma,
	.bmdma_setup		= k2_bmdma_setup_mmio,
	.bmdma_start		= k2_bmdma_start_mmio,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.data_xfer		= ata_data_xfer,
	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= ata_bmdma_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,
	.irq_ack		= ata_irq_ack,
	.scr_read		= k2_sata_scr_read,
	.scr_write		= k2_sata_scr_write,
	.port_start		= ata_port_start,
};

static const struct ata_port_info k2_port_info[] = {
	/* board_svw4 */
	{
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | K2_FLAG_NO_ATAPI_DMA,
		.pio_mask	= 0x1f,
		.mwdma_mask	= 0x07,
		.udma_mask	= 0x7f,
		.port_ops	= &k2_sata_ops,
	},
	/* board_svw8 */
	{
		.flags		= ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY |
				  ATA_FLAG_MMIO | K2_FLAG_NO_ATAPI_DMA |
				  K2_FLAG_SATA_8_PORTS,
		.pio_mask	= 0x1f,
		.mwdma_mask	= 0x07,
		.udma_mask	= 0x7f,
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


static int k2_sata_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	const struct ata_port_info *ppi[] =
		{ &k2_port_info[ent->driver_data], NULL };
	struct ata_host *host;
	void __iomem *mmio_base;
	int n_ports, i, rc;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " DRV_VERSION "\n");

	/* allocate host */
	n_ports = 4;
	if (ppi[0]->flags & K2_FLAG_SATA_8_PORTS)
		n_ports = 8;

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, n_ports);
	if (!host)
		return -ENOMEM;

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
	if (pci_resource_len(pdev, 5) == 0)
		return -ENODEV;

	/* Request and iomap PCI regions */
	rc = pcim_iomap_regions(pdev, 1 << 5, DRV_NAME);
	if (rc == -EBUSY)
		pcim_pin_device(pdev);
	if (rc)
		return rc;
	host->iomap = pcim_iomap_table(pdev);
	mmio_base = host->iomap[5];

	/* different controllers have different number of ports - currently 4 or 8 */
	/* All ports are on the same function. Multi-function device is no
	 * longer available. This should not be seen in any system. */
	for (i = 0; i < host->n_ports; i++)
		k2_sata_setup_port(&host->ports[i]->ioaddr,
				   mmio_base + i * K2_SATA_PORT_OFFSET);

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
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
	return ata_host_activate(host, pdev->irq, ata_interrupt, IRQF_SHARED,
				 &k2_sata_sht);
}

/* 0x240 is device ID for Apple K2 device
 * 0x241 is device ID for Serverworks Frodo4
 * 0x242 is device ID for Serverworks Frodo8
 * 0x24a is device ID for BCM5785 (aka HT1000) HT southbridge integrated SATA
 * controller
 * */
static const struct pci_device_id k2_sata_pci_tbl[] = {
	{ PCI_VDEVICE(SERVERWORKS, 0x0240), board_svw4 },
	{ PCI_VDEVICE(SERVERWORKS, 0x0241), board_svw4 },
	{ PCI_VDEVICE(SERVERWORKS, 0x0242), board_svw8 },
	{ PCI_VDEVICE(SERVERWORKS, 0x024a), board_svw4 },
	{ PCI_VDEVICE(SERVERWORKS, 0x024b), board_svw4 },

	{ }
};

static struct pci_driver k2_sata_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= k2_sata_pci_tbl,
	.probe			= k2_sata_init_one,
	.remove			= ata_pci_remove_one,
};

static int __init k2_sata_init(void)
{
	return pci_register_driver(&k2_sata_pci_driver);
}

static void __exit k2_sata_exit(void)
{
	pci_unregister_driver(&k2_sata_pci_driver);
}

MODULE_AUTHOR("Benjamin Herrenschmidt");
MODULE_DESCRIPTION("low-level driver for K2 SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, k2_sata_pci_tbl);
MODULE_VERSION(DRV_VERSION);

module_init(k2_sata_init);
module_exit(k2_sata_exit);
