/*
 *	pata_hpt3x3		-	HPT3x3 driver
 *	(c) Copyright 2005-2006 Red Hat
 *
 *	Was pata_hpt34x but the naming was confusing as it supported the
 *	343 and 363 so it has been renamed.
 *
 *	Based on:
 *	linux/drivers/ide/pci/hpt34x.c		Version 0.40	Sept 10, 2002
 *	Copyright (C) 1998-2000	Andre Hedrick <andre@linux-ide.org>
 *
 *	May be copied or modified under the terms of the GNU General Public
 *	License
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME	"pata_hpt3x3"
#define DRV_VERSION	"0.4.3"

/**
 *	hpt3x3_set_piomode		-	PIO setup
 *	@ap: ATA interface
 *	@adev: device on the interface
 *
 *	Set our PIO requirements. This is fairly simple on the HPT3x3 as
 *	all we have to do is clear the MWDMA and UDMA bits then load the
 *	mode number.
 */

static void hpt3x3_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 r1, r2;
	int dn = 2 * ap->port_no + adev->devno;

	pci_read_config_dword(pdev, 0x44, &r1);
	pci_read_config_dword(pdev, 0x48, &r2);
	/* Load the PIO timing number */
	r1 &= ~(7 << (3 * dn));
	r1 |= (adev->pio_mode - XFER_PIO_0) << (3 * dn);
	r2 &= ~(0x11 << dn);	/* Clear MWDMA and UDMA bits */

	pci_write_config_dword(pdev, 0x44, r1);
	pci_write_config_dword(pdev, 0x48, r2);
}

/**
 *	hpt3x3_set_dmamode		-	DMA timing setup
 *	@ap: ATA interface
 *	@adev: Device being configured
 *
 *	Set up the channel for MWDMA or UDMA modes. Much the same as with
 *	PIO, load the mode number and then set MWDMA or UDMA flag.
 */

static void hpt3x3_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 r1, r2;
	int dn = 2 * ap->port_no + adev->devno;
	int mode_num = adev->dma_mode & 0x0F;

	pci_read_config_dword(pdev, 0x44, &r1);
	pci_read_config_dword(pdev, 0x48, &r2);
	/* Load the timing number */
	r1 &= ~(7 << (3 * dn));
	r1 |= (mode_num << (3 * dn));
	r2 &= ~(0x11 << dn);	/* Clear MWDMA and UDMA bits */

	if (adev->dma_mode >= XFER_UDMA_0)
		r2 |= 0x01 << dn;	/* Ultra mode */
	else
		r2 |= 0x10 << dn;	/* MWDMA */

	pci_write_config_dword(pdev, 0x44, r1);
	pci_write_config_dword(pdev, 0x48, r2);
}

static struct scsi_host_template hpt3x3_sht = {
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

static struct ata_port_operations hpt3x3_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= hpt3x3_set_piomode,
	.set_dmamode	= hpt3x3_set_dmamode,
	.mode_filter	= ata_pci_default_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_40wire,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

/**
 *	hpt3x3_init_chipset	-	chip setup
 *	@dev: PCI device
 *
 *	Perform the setup required at boot and on resume.
 */

static void hpt3x3_init_chipset(struct pci_dev *dev)
{
	u16 cmd;
	/* Initialize the board */
	pci_write_config_word(dev, 0x80, 0x00);
	/* Check if it is a 343 or a 363. 363 has COMMAND_MEMORY set */
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (cmd & PCI_COMMAND_MEMORY)
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0xF0);
	else
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x20);
}


/**
 *	hpt3x3_init_one		-	Initialise an HPT343/363
 *	@dev: PCI device
 *	@id: Entry in match table
 *
 *	Perform basic initialisation. The chip has a quirk that it won't
 *	function unless it is at XX00. The old ATA driver touched this up
 *	but we leave it for pci quirks to do properly.
 */

static int hpt3x3_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.sht = &hpt3x3_sht,
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.udma_mask = 0x07,
		.port_ops = &hpt3x3_port_ops
	};
	const struct ata_port_info *ppi[] = { &info, NULL };

	hpt3x3_init_chipset(dev);
	/* Now kick off ATA set up */
	return ata_pci_init_one(dev, ppi);
}

#ifdef CONFIG_PM
static int hpt3x3_reinit_one(struct pci_dev *dev)
{
	hpt3x3_init_chipset(dev);
	return ata_pci_device_resume(dev);
}
#endif

static const struct pci_device_id hpt3x3[] = {
	{ PCI_VDEVICE(TTI, PCI_DEVICE_ID_TTI_HPT343), },

	{ },
};

static struct pci_driver hpt3x3_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= hpt3x3,
	.probe 		= hpt3x3_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= hpt3x3_reinit_one,
#endif
};

static int __init hpt3x3_init(void)
{
	return pci_register_driver(&hpt3x3_pci_driver);
}


static void __exit hpt3x3_exit(void)
{
	pci_unregister_driver(&hpt3x3_pci_driver);
}


MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for the Highpoint HPT343/363");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, hpt3x3);
MODULE_VERSION(DRV_VERSION);

module_init(hpt3x3_init);
module_exit(hpt3x3_exit);
