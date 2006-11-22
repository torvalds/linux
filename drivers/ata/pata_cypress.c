/*
 * pata_cypress.c 	- Cypress PATA for new ATA layer
 *			  (C) 2006 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *
 * Based heavily on
 * linux/drivers/ide/pci/cy82c693.c		Version 0.40	Sep. 10, 2002
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_cypress"
#define DRV_VERSION "0.1.4"

/* here are the offset definitions for the registers */

enum {
	CY82_IDE_CMDREG		= 0x04,
	CY82_IDE_ADDRSETUP	= 0x48,
	CY82_IDE_MASTER_IOR	= 0x4C,
	CY82_IDE_MASTER_IOW	= 0x4D,
	CY82_IDE_SLAVE_IOR	= 0x4E,
	CY82_IDE_SLAVE_IOW	= 0x4F,
	CY82_IDE_MASTER_8BIT	= 0x50,
	CY82_IDE_SLAVE_8BIT	= 0x51,

	CY82_INDEX_PORT		= 0x22,
	CY82_DATA_PORT		= 0x23,

	CY82_INDEX_CTRLREG1	= 0x01,
	CY82_INDEX_CHANNEL0	= 0x30,
	CY82_INDEX_CHANNEL1	= 0x31,
	CY82_INDEX_TIMEOUT	= 0x32
};

static int cy82c693_pre_reset(struct ata_port *ap)
{
	ap->cbl = ATA_CBL_PATA40;
	return ata_std_prereset(ap);
}

static void cy82c693_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, cy82c693_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	cy82c693_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the PIO mode setup.
 */

static void cy82c693_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct ata_timing t;
	const unsigned long T = 1000000 / 33;
	short time_16, time_8;
	u32 addr;

	if (ata_timing_compute(adev, adev->pio_mode, &t, T, 1) < 0) {
		printk(KERN_ERR DRV_NAME ": mome computation failed.\n");
		return;
	}

	time_16 = FIT(t.recover, 0, 15) | (FIT(t.active, 0, 15) << 4);
	time_8 = FIT(t.act8b, 0, 15) | (FIT(t.rec8b, 0, 15) << 4);

	if (adev->devno == 0) {
		pci_read_config_dword(pdev, CY82_IDE_ADDRSETUP, &addr);

		addr &= ~0x0F;	/* Mask bits */
		addr |= FIT(t.setup, 0, 15);

		pci_write_config_dword(pdev, CY82_IDE_ADDRSETUP, addr);
		pci_write_config_byte(pdev, CY82_IDE_MASTER_IOR, time_16);
		pci_write_config_byte(pdev, CY82_IDE_MASTER_IOW, time_16);
		pci_write_config_byte(pdev, CY82_IDE_MASTER_8BIT, time_8);
	} else {
		pci_read_config_dword(pdev, CY82_IDE_ADDRSETUP, &addr);

		addr &= ~0xF0;	/* Mask bits */
		addr |= (FIT(t.setup, 0, 15) << 4);

		pci_write_config_dword(pdev, CY82_IDE_ADDRSETUP, addr);
		pci_write_config_byte(pdev, CY82_IDE_SLAVE_IOR, time_16);
		pci_write_config_byte(pdev, CY82_IDE_SLAVE_IOW, time_16);
		pci_write_config_byte(pdev, CY82_IDE_SLAVE_8BIT, time_8);
	}
}

/**
 *	cy82c693_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the DMA mode setup.
 */

static void cy82c693_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	int reg = CY82_INDEX_CHANNEL0 + ap->port_no;

	/* Be afraid, be very afraid. Magic registers  in low I/O space */
	outb(reg, 0x22);
	outb(adev->dma_mode - XFER_MW_DMA_0, 0x23);

	/* 0x50 gives the best behaviour on the Alpha's using this chip */
	outb(CY82_INDEX_TIMEOUT, 0x22);
	outb(0x50, 0x23);
}

static struct scsi_host_template cy82c693_sht = {
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
	.resume			= ata_scsi_device_resume,
	.suspend		= ata_scsi_device_suspend,
};

static struct ata_port_operations cy82c693_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= cy82c693_set_piomode,
	.set_dmamode	= cy82c693_set_dmamode,
	.mode_filter	= ata_pci_default_filter,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= cy82c693_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,

	.bmdma_setup 	= ata_bmdma_setup,
	.bmdma_start 	= ata_bmdma_start,
	.bmdma_stop	= ata_bmdma_stop,
	.bmdma_status 	= ata_bmdma_status,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= ata_pio_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,

	.port_start	= ata_port_start,
	.port_stop	= ata_port_stop,
	.host_stop	= ata_host_stop
};

static int cy82c693_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static struct ata_port_info info = {
		.sht = &cy82c693_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.mwdma_mask = 0x07,
		.port_ops = &cy82c693_port_ops
	};
	static struct ata_port_info *port_info[1] = { &info };

	/* Devfn 1 is the ATA primary. The secondary is magic and on devfn2.
	   For the moment we don't handle the secondary. FIXME */

	if (PCI_FUNC(pdev->devfn) != 1)
		return -ENODEV;

	return ata_pci_init_one(pdev, port_info, 1);
}

static const struct pci_device_id cy82c693[] = {
	{ PCI_VDEVICE(CONTAQ, PCI_DEVICE_ID_CONTAQ_82C693), },

	{ },
};

static struct pci_driver cy82c693_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= cy82c693,
	.probe 		= cy82c693_init_one,
	.remove		= ata_pci_remove_one,
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
};

static int __init cy82c693_init(void)
{
	return pci_register_driver(&cy82c693_pci_driver);
}


static void __exit cy82c693_exit(void)
{
	pci_unregister_driver(&cy82c693_pci_driver);
}


MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for the CY82C693 PATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, cy82c693);
MODULE_VERSION(DRV_VERSION);

module_init(cy82c693_init);
module_exit(cy82c693_exit);
