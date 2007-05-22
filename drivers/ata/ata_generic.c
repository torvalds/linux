/*
 *  ata_generic.c - Generic PATA/SATA controller driver.
 *  Copyright 2005 Red Hat Inc <alan@redhat.com>, all rights reserved.
 *
 *  Elements from ide/pci/generic.c
 *	    Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 *	    Portions (C) Copyright 2002  Red Hat Inc <alan@redhat.com>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  Driver for PCI IDE interfaces implementing the standard bus mastering
 *  interface functionality. This assumes the BIOS did the drive set up and
 *  tuning for us. By default we do not grab all IDE class devices as they
 *  may have other drivers or need fixups to avoid problems. Instead we keep
 *  a default list of stuff without documentation/driver that appears to
 *  work.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "ata_generic"
#define DRV_VERSION "0.2.12"

/*
 *	A generic parallel ATA driver using libata
 */

/**
 *	generic_set_mode	-	mode setting
 *	@ap: interface to set up
 *	@unused: returned device on error
 *
 *	Use a non standard set_mode function. We don't want to be tuned.
 *	The BIOS configured everything. Our job is not to fiddle. We
 *	read the dma enabled bits from the PCI configuration of the device
 *	and respect them.
 */

static int generic_set_mode(struct ata_port *ap, struct ata_device **unused)
{
	int dma_enabled = 0;
	int i;

	/* Bits 5 and 6 indicate if DMA is active on master/slave */
	if (ap->ioaddr.bmdma_addr)
		dma_enabled = ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		struct ata_device *dev = &ap->device[i];
		if (ata_dev_enabled(dev)) {
			/* We don't really care */
			dev->pio_mode = XFER_PIO_0;
			dev->dma_mode = XFER_MW_DMA_0;
			/* We do need the right mode information for DMA or PIO
			   and this comes from the current configuration flags */
			if (dma_enabled & (1 << (5 + i))) {
				ata_id_to_dma_mode(dev, XFER_MW_DMA_0);
				dev->flags &= ~ATA_DFLAG_PIO;
			} else {
				ata_dev_printk(dev, KERN_INFO, "configured for PIO\n");
				dev->xfer_mode = XFER_PIO_0;
				dev->xfer_shift = ATA_SHIFT_PIO;
				dev->flags |= ATA_DFLAG_PIO;
			}
		}
	}
	return 0;
}

static struct scsi_host_template generic_sht = {
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

static struct ata_port_operations generic_port_ops = {
	.set_mode	= generic_set_mode,

	.port_disable	= ata_port_disable,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.data_xfer	= ata_data_xfer,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_unknown,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

static int all_generic_ide;		/* Set to claim all devices */

/**
 *	ata_generic_init		-	attach generic IDE
 *	@dev: PCI device found
 *	@id: match entry
 *
 *	Called each time a matching IDE interface is found. We check if the
 *	interface is one we wish to claim and if so we perform any chip
 *	specific hacks then let the ATA layer do the heavy lifting.
 */

static int ata_generic_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	u16 command;
	static const struct ata_port_info info = {
		.sht = &generic_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x3f,
		.port_ops = &generic_port_ops
	};
	const struct ata_port_info *ppi[] = { &info, NULL };

	/* Don't use the generic entry unless instructed to do so */
	if (id->driver_data == 1 && all_generic_ide == 0)
		return -ENODEV;

	/* Devices that need care */
	if (dev->vendor == PCI_VENDOR_ID_UMC &&
	    dev->device == PCI_DEVICE_ID_UMC_UM8886A &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		return -ENODEV;

	if (dev->vendor == PCI_VENDOR_ID_OPTI &&
	    dev->device == PCI_DEVICE_ID_OPTI_82C558 &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		return -ENODEV;

	/* Don't re-enable devices in generic mode or we will break some
	   motherboards with disabled and unused IDE controllers */
	pci_read_config_word(dev, PCI_COMMAND, &command);
	if (!(command & PCI_COMMAND_IO))
		return -ENODEV;

	if (dev->vendor == PCI_VENDOR_ID_AL)
	    	ata_pci_clear_simplex(dev);

	return ata_pci_init_one(dev, ppi);
}

static struct pci_device_id ata_generic[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_SAMURAI_IDE), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HOLTEK, PCI_DEVICE_ID_HOLTEK_6565), },
	{ PCI_DEVICE(PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8673F), },
	{ PCI_DEVICE(PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8886A), },
	{ PCI_DEVICE(PCI_VENDOR_ID_UMC,    PCI_DEVICE_ID_UMC_UM8886BF), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HINT,   PCI_DEVICE_ID_HINT_VXPROII_IDE), },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA,    PCI_DEVICE_ID_VIA_82C561), },
	{ PCI_DEVICE(PCI_VENDOR_ID_OPTI,   PCI_DEVICE_ID_OPTI_82C558), },
	{ PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA,PCI_DEVICE_ID_TOSHIBA_PICCOLO), },
	{ PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA,PCI_DEVICE_ID_TOSHIBA_PICCOLO_1), },
	{ PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA,PCI_DEVICE_ID_TOSHIBA_PICCOLO_2),  },
	/* Must come last. If you add entries adjust this table appropriately */
	{ PCI_ANY_ID,		PCI_ANY_ID,			   PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_STORAGE_IDE << 8, 0xFFFFFF00UL, 1},
	{ 0, },
};

static struct pci_driver ata_generic_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= ata_generic,
	.probe 		= ata_generic_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

static int __init ata_generic_init(void)
{
	return pci_register_driver(&ata_generic_pci_driver);
}


static void __exit ata_generic_exit(void)
{
	pci_unregister_driver(&ata_generic_pci_driver);
}


MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for generic ATA");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, ata_generic);
MODULE_VERSION(DRV_VERSION);

module_init(ata_generic_init);
module_exit(ata_generic_exit);

module_param(all_generic_ide, int, 0);
