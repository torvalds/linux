/*
 * pata_opti.c 	- ATI PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
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
#define DRV_VERSION "0.2.9"

enum {
	READ_REG	= 0,	/* index of Read cycle timing register */
	WRITE_REG 	= 1,	/* index of Write cycle timing register */
	CNTRL_REG 	= 3,	/* index of Control register */
	STRAP_REG 	= 5,	/* index of Strap register */
	MISC_REG 	= 6	/* index of Miscellaneous register */
};

/**
 *	opti_pre_reset		-	probe begin
 *	@link: ATA link
 *	@deadline: deadline jiffies for the operation
 *
 *	Set up cable type and use generic probe init
 */

static int opti_pre_reset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	static const struct pci_bits opti_enable_bits[] = {
		{ 0x45, 1, 0x80, 0x00 },
		{ 0x40, 1, 0x08, 0x00 }
	};

	if (!pci_test_config_bits(pdev, &opti_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_sff_prereset(link, deadline);
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
	void __iomem *regio = ap->ioaddr.cmd_addr;

	/* These 3 unlock the control register access */
	ioread16(regio + 1);
	ioread16(regio + 1);
	iowrite8(3, regio + 2);

	/* Do the I/O */
	iowrite8(val, regio + reg);

	/* Relock */
	iowrite8(0x83, regio + 2);
}

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
	void __iomem *regio = ap->ioaddr.cmd_addr;
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

	iowrite8(0xff, regio + 5);
	clock = ioread16(regio + 5) & 1;

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
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations opti_port_ops = {
	.inherits	= &ata_sff_port_ops,
	.cable_detect	= ata_cable_40wire,
	.set_piomode	= opti_set_piomode,
	.prereset	= opti_pre_reset,
};

static int opti_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.port_ops = &opti_port_ops
	};
	const struct ata_port_info *ppi[] = { &info, NULL };
	static int printed_version;

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &dev->dev, "version " DRV_VERSION "\n");

	return ata_pci_sff_init_one(dev, ppi, &opti_sht, NULL, 0);
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
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
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
