/*
 * pata_opti.c 	- ATI PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *
 * Based on
 *  linux/drivers/ide/pci/opti621.c		Version 0.7	Sept 10, 2002
 *
 *  Copyright (C) 1996-1998  Linus Torvalds & authors (see below)
 *
 * Authors:
 * Jaromir Koutek <miri@punknet.cz>,
 * Jan Harkes <jaharkes@cwi.nl>,
 * Mark Lord <mlord@pobox.com>
 * Some parts of code are from ali14xx.c and from rz1000.c.
 *
 * Also consulted the FreeBSD prototype driver by Kevin Day to try
 * and resolve some confusions. Further documentation can be found in
 * Ralf Brown's interrupt list
 *
 * If you have other variants of the Opti range (Viper/Vendetta) please
 * try this driver with those PCI idents and report back. For the later
 * chips see the pata_optidma driver
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

#define DRV_NAME "pata_opti"
#define DRV_VERSION "0.2.5"

enum {
	READ_REG	= 0,	/* index of Read cycle timing register */
	WRITE_REG 	= 1,	/* index of Write cycle timing register */
	CNTRL_REG 	= 3,	/* index of Control register */
	STRAP_REG 	= 5,	/* index of Strap register */
	MISC_REG 	= 6	/* index of Miscellaneous register */
};

/**
 *	opti_pre_reset		-	probe begin
 *	@ap: ATA port
 *
 *	Set up cable type and use generic probe init
 */

static int opti_pre_reset(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	static const struct pci_bits opti_enable_bits[] = {
		{ 0x45, 1, 0x80, 0x00 },
		{ 0x40, 1, 0x08, 0x00 }
	};

	if (!pci_test_config_bits(pdev, &opti_enable_bits[ap->port_no]))
		return -ENOENT;

	ap->cbl = ATA_CBL_PATA40;
	return ata_std_prereset(ap);
}

/**
 *	opti_probe_reset		-	probe reset
 *	@ap: ATA port
 *
 *	Perform the ATA probe and bus reset sequence plus specific handling
 *	for this hardware. The Opti needs little handling - we have no UDMA66
 *	capability that needs cable detection. All we must do is check the port
 *	is enabled.
 */

static void opti_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, opti_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
}

/**
 *	opti_write_reg		-	control register setup
 *	@ap: ATA port
 *	@value: value
 *	@reg: control register number
 *
 *	The Opti uses magic 'trapdoor' register accesses to do configuration
 *	rather than using PCI space as other controllers do. The double inw
 *	on the error register activates configuration mode. We can then write
 *	the control register
 */

static void opti_write_reg(struct ata_port *ap, u8 val, int reg)
{
	unsigned long regio = ap->ioaddr.cmd_addr;

	/* These 3 unlock the control register access */
	inw(regio + 1);
	inw(regio + 1);
	outb(3, regio + 2);

	/* Do the I/O */
	outb(val, regio + reg);

	/* Relock */
	outb(0x83, regio + 2);
}

#if 0
/**
 *	opti_read_reg		-	control register read
 *	@ap: ATA port
 *	@reg: control register number
 *
 *	The Opti uses magic 'trapdoor' register accesses to do configuration
 *	rather than using PCI space as other controllers do. The double inw
 *	on the error register activates configuration mode. We can then read
 *	the control register
 */

static u8 opti_read_reg(struct ata_port *ap, int reg)
{
	unsigned long regio = ap->ioaddr.cmd_addr;
	u8 ret;
	inw(regio + 1);
	inw(regio + 1);
	outb(3, regio + 2);
	ret = inb(regio + reg);
	outb(0x83, regio + 2);
}
#endif

/**
 *	opti_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the PIO mode setup. Timing numbers are taken from
 *	the FreeBSD driver then pre computed to keep the code clean. There
 *	are two tables depending on the hardware clock speed.
 */

static void opti_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct ata_device *pair = ata_dev_pair(adev);
	int clock;
	int pio = adev->pio_mode - XFER_PIO_0;
	unsigned long regio = ap->ioaddr.cmd_addr;
	u8 addr;

	/* Address table precomputed with prefetch off and a DCLK of 2 */
	static const u8 addr_timing[2][5] = {
		{ 0x30, 0x20, 0x20, 0x10, 0x10 },
		{ 0x20, 0x20, 0x10, 0x10, 0x10 }
	};
	static const u8 data_rec_timing[2][5] = {
		{ 0x6B, 0x56, 0x42, 0x32, 0x31 },
		{ 0x58, 0x44, 0x32, 0x22, 0x21 }
	};

	outb(0xff, regio + 5);
	clock = inw(regio + 5) & 1;

	/*
 	 *	As with many controllers the address setup time is shared
 	 *	and must suit both devices if present.
	 */

	addr = addr_timing[clock][pio];
	if (pair) {
		/* Hardware constraint */
		u8 pair_addr = addr_timing[clock][pair->pio_mode - XFER_PIO_0];
		if (pair_addr > addr)
			addr = pair_addr;
	}

	/* Commence primary programming sequence */
	opti_write_reg(ap, adev->devno, MISC_REG);
	opti_write_reg(ap, data_rec_timing[clock][pio], READ_REG);
	opti_write_reg(ap, data_rec_timing[clock][pio], WRITE_REG);
	opti_write_reg(ap, addr, MISC_REG);

	/* Programming sequence complete, override strapping */
	opti_write_reg(ap, 0x85, CNTRL_REG);
}

static struct scsi_host_template opti_sht = {
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

static struct ata_port_operations opti_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= opti_set_piomode,
/*	.set_dmamode	= opti_set_dmamode, */
	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= opti_error_handler,
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

static int opti_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static struct ata_port_info info = {
		.sht = &opti_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask = 0x1f,
		.port_ops = &opti_port_ops
	};
	static struct ata_port_info *port_info[2] = { &info, &info };
	static int printed_version;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &dev->dev, "version " DRV_VERSION "\n");

	return ata_pci_init_one(dev, port_info, 2);
}

static const struct pci_device_id opti[] = {
	{ PCI_VDEVICE(OPTI, PCI_DEVICE_ID_OPTI_82C621), 0 },
	{ PCI_VDEVICE(OPTI, PCI_DEVICE_ID_OPTI_82C825), 1 },

	{ },
};

static struct pci_driver opti_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= opti,
	.probe 		= opti_init_one,
	.remove		= ata_pci_remove_one
};

static int __init opti_init(void)
{
	return pci_register_driver(&opti_pci_driver);
}

static void __exit opti_exit(void)
{
	pci_unregister_driver(&opti_pci_driver);
}


MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Opti 621/621X");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, opti);
MODULE_VERSION(DRV_VERSION);

module_init(opti_init);
module_exit(opti_exit);
