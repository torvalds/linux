/*
 *  RZ1000/1001 driver based upon
 *
 *  linux/drivers/ide/pci/rz1000.c	Version 0.06	January 12, 2003
 *  Copyright (C) 1995-1998  Linus Torvalds & author (see below)
 *  Principal Author:  mlord@pobox.com (Mark Lord)
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 *  This file provides support for disabling the buggy read-ahead
 *  mode of the RZ1000 IDE chipset, commonly used on Intel motherboards.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"pata_rz1000"
#define DRV_VERSION	"0.2.2"


/**
 *	rz1000_prereset		-	probe begin
 *	@ap: ATA port
 *
 *	Set up cable type and use generics
 */

static int rz1000_prereset(struct ata_port *ap)
{
	ap->cbl = ATA_CBL_PATA40;
	return ata_std_prereset(ap);
}

/**
 *	rz1000_error_handler		-	probe reset
 *	@ap: ATA port
 *
 *	Perform the ATA standard reset sequence
 */

static void rz1000_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, rz1000_prereset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	rz1000_set_mode		-	mode setting function
 *	@ap: ATA interface
 *
 *	Use a non standard set_mode function. We don't want to be tuned. We
 *	would prefer to be BIOS generic but for the fact our hardware is
 *	whacked out.
 */

static void rz1000_set_mode(struct ata_port *ap)
{
	int i;

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		struct ata_device *dev = &ap->device[i];
		if (ata_dev_enabled(dev)) {
			/* We don't really care */
			dev->pio_mode = XFER_PIO_0;
			dev->xfer_mode = XFER_PIO_0;
			dev->xfer_shift = ATA_SHIFT_PIO;
			dev->flags |= ATA_DFLAG_PIO;
		}
	}
}


static struct scsi_host_template rz1000_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
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
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations rz1000_port_ops = {
	.set_mode	= rz1000_set_mode,

	.port_disable	= ata_port_disable,
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.error_handler	= rz1000_error_handler,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_pio_data_xfer,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= rz1000_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,

	.port_start	= ata_port_start,
	.port_stop	= ata_port_stop,
	.host_stop	= ata_host_stop
};

/**
 *	rz1000_init_one - Register RZ1000 ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in rz1000_pci_tbl matching with @pdev
 *
 *	Configure an RZ1000 interface. This doesn't require much special
 *	handling except that we *MUST* kill the chipset readahead or the
 *	user may experience data corruption.
 */

static int rz1000_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_port_info *port_info[2];
	u16 reg;
	static struct ata_port_info info = {
		.sht = &rz1000_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.port_ops = &rz1000_port_ops
	};

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	/* Be exceptionally paranoid as we must be sure to apply the fix */
	if (pci_read_config_word(pdev, 0x40, &reg) != 0)
		goto fail;
	reg &= 0xDFFF;
	if (pci_write_config_word(pdev, 0x40, reg) != 0)
		goto fail;
	printk(KERN_INFO DRV_NAME ": disabled chipset readahead.\n");

	port_info[0] = &info;
	port_info[1] = &info;
	return ata_pci_init_one(pdev, port_info, 2);
fail:
	printk(KERN_ERR DRV_NAME ": failed to disable read-ahead on chipset..\n");
	/* Not safe to use so skip */
	return -ENODEV;
}

static const struct pci_device_id pata_rz1000[] = {
	{ PCI_VDEVICE(PCTECH, PCI_DEVICE_ID_PCTECH_RZ1000), },
	{ PCI_VDEVICE(PCTECH, PCI_DEVICE_ID_PCTECH_RZ1001), },

	{ },
};

static struct pci_driver rz1000_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= pata_rz1000,
	.probe 		= rz1000_init_one,
	.remove		= ata_pci_remove_one
};

static int __init rz1000_init(void)
{
	return pci_register_driver(&rz1000_pci_driver);
}

static void __exit rz1000_exit(void)
{
	pci_unregister_driver(&rz1000_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for RZ1000 PCI ATA");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pata_rz1000);
MODULE_VERSION(DRV_VERSION);

module_init(rz1000_init);
module_exit(rz1000_exit);

