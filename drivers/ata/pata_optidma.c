// SPDX-License-Identifier: GPL-2.0-only
/*
 * pata_optidma.c 	- Opti DMA PATA for new ATA layer
 *			  (C) 2006 Red Hat Inc
 *
 *	The Opti DMA controllers are related to the older PIO PCI controllers
 *	and indeed the VLB ones. The main differences are that the timing
 *	numbers are now based off PCI clocks not VLB and differ, and that
 *	MWDMA is supported.
 *
 *	This driver should support Viper-N+, FireStar, FireStar Plus.
 *
 *	These devices support virtual DMA for read (aka the CS5520). Later
 *	chips support UDMA33, but only if the rest of the board logic does,
 *	so you have to get this right. We don't support the virtual DMA
 *	but we do handle UDMA.
 *
 *	Bits that are worth knowing
 *		Most control registers are shadowed into I/O registers
 *		0x1F5 bit 0 tells you if the PCI/VLB clock is 33 or 25Mhz
 *		Virtual DMA registers *move* between rev 0x02 and rev 0x10
 *		UDMA requires a 66MHz FSB
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_optidma"
#define DRV_VERSION "0.3.2"

enum {
	READ_REG	= 0,	/* index of Read cycle timing register */
	WRITE_REG 	= 1,	/* index of Write cycle timing register */
	CNTRL_REG 	= 3,	/* index of Control register */
	STRAP_REG 	= 5,	/* index of Strap register */
	MISC_REG 	= 6	/* index of Miscellaneous register */
};

static int pci_clock;	/* 0 = 33 1 = 25 */

/**
 *	optidma_pre_reset		-	probe begin
 *	@link: ATA link
 *	@deadline: deadline jiffies for the operation
 *
 *	Set up cable type and use generic probe init
 */

static int optidma_pre_reset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	static const struct pci_bits optidma_enable_bits = {
		0x40, 1, 0x08, 0x00
	};

	if (ap->port_no && !pci_test_config_bits(pdev, &optidma_enable_bits))
		return -ENOENT;

	return ata_sff_prereset(link, deadline);
}

/**
 *	optidma_unlock		-	unlock control registers
 *	@ap: ATA port
 *
 *	Unlock the control register block for this adapter. Registers must not
 *	be unlocked in a situation where libata might look at them.
 */

static void optidma_unlock(struct ata_port *ap)
{
	void __iomem *regio = ap->ioaddr.cmd_addr;

	/* These 3 unlock the control register access */
	ioread16(regio + 1);
	ioread16(regio + 1);
	iowrite8(3, regio + 2);
}

/**
 *	optidma_lock		-	issue temporary relock
 *	@ap: ATA port
 *
 *	Re-lock the configuration register settings.
 */

static void optidma_lock(struct ata_port *ap)
{
	void __iomem *regio = ap->ioaddr.cmd_addr;

	/* Relock */
	iowrite8(0x83, regio + 2);
}

/**
 *	optidma_mode_setup	-	set mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *	@mode: Mode to set
 *
 *	Called to do the DMA or PIO mode setup. Timing numbers are all
 *	pre computed to keep the code clean. There are two tables depending
 *	on the hardware clock speed.
 *
 *	WARNING: While we do this the IDE registers vanish. If we take an
 *	IRQ here we depend on the host set locking to avoid catastrophe.
 */

static void optidma_mode_setup(struct ata_port *ap, struct ata_device *adev, u8 mode)
{
	struct ata_device *pair = ata_dev_pair(adev);
	int pio = adev->pio_mode - XFER_PIO_0;
	int dma = adev->dma_mode - XFER_MW_DMA_0;
	void __iomem *regio = ap->ioaddr.cmd_addr;
	u8 addr;

	/* Address table precomputed with a DCLK of 2 */
	static const u8 addr_timing[2][5] = {
		{ 0x30, 0x20, 0x20, 0x10, 0x10 },
		{ 0x20, 0x20, 0x10, 0x10, 0x10 }
	};
	static const u8 data_rec_timing[2][5] = {
		{ 0x59, 0x46, 0x30, 0x20, 0x20 },
		{ 0x46, 0x32, 0x20, 0x20, 0x10 }
	};
	static const u8 dma_data_rec_timing[2][3] = {
		{ 0x76, 0x20, 0x20 },
		{ 0x54, 0x20, 0x10 }
	};

	/* Switch from IDE to control mode */
	optidma_unlock(ap);


	/*
 	 *	As with many controllers the address setup time is shared
 	 *	and must suit both devices if present. FIXME: Check if we
 	 *	need to look at slowest of PIO/DMA mode of either device
	 */

	if (mode >= XFER_MW_DMA_0)
		addr = 0;
	else
		addr = addr_timing[pci_clock][pio];

	if (pair) {
		u8 pair_addr;
		/* Hardware constraint */
		if (pair->dma_mode)
			pair_addr = 0;
		else
			pair_addr = addr_timing[pci_clock][pair->pio_mode - XFER_PIO_0];
		if (pair_addr > addr)
			addr = pair_addr;
	}

	/* Commence primary programming sequence */
	/* First we load the device number into the timing select */
	iowrite8(adev->devno, regio + MISC_REG);
	/* Now we load the data timings into read data/write data */
	if (mode < XFER_MW_DMA_0) {
		iowrite8(data_rec_timing[pci_clock][pio], regio + READ_REG);
		iowrite8(data_rec_timing[pci_clock][pio], regio + WRITE_REG);
	} else if (mode < XFER_UDMA_0) {
		iowrite8(dma_data_rec_timing[pci_clock][dma], regio + READ_REG);
		iowrite8(dma_data_rec_timing[pci_clock][dma], regio + WRITE_REG);
	}
	/* Finally we load the address setup into the misc register */
	iowrite8(addr | adev->devno, regio + MISC_REG);

	/* Programming sequence complete, timing 0 dev 0, timing 1 dev 1 */
	iowrite8(0x85, regio + CNTRL_REG);

	/* Switch back to IDE mode */
	optidma_lock(ap);

	/* Note: at this point our programming is incomplete. We are
	   not supposed to program PCI 0x43 "things we hacked onto the chip"
	   until we've done both sets of PIO/DMA timings */
}

/**
 *	optiplus_mode_setup	-	DMA setup for Firestar Plus
 *	@ap: ATA port
 *	@adev: device
 *	@mode: desired mode
 *
 *	The Firestar plus has additional UDMA functionality for UDMA0-2 and
 *	requires we do some additional work. Because the base work we must do
 *	is mostly shared we wrap the Firestar setup functionality in this
 *	one
 */

static void optiplus_mode_setup(struct ata_port *ap, struct ata_device *adev, u8 mode)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 udcfg;
	u8 udslave;
	int dev2 = 2 * adev->devno;
	int unit = 2 * ap->port_no + adev->devno;
	int udma = mode - XFER_UDMA_0;

	pci_read_config_byte(pdev, 0x44, &udcfg);
	if (mode <= XFER_UDMA_0) {
		udcfg &= ~(1 << unit);
		optidma_mode_setup(ap, adev, adev->dma_mode);
	} else {
		udcfg |=  (1 << unit);
		if (ap->port_no) {
			pci_read_config_byte(pdev, 0x45, &udslave);
			udslave &= ~(0x03 << dev2);
			udslave |= (udma << dev2);
			pci_write_config_byte(pdev, 0x45, udslave);
		} else {
			udcfg &= ~(0x30 << dev2);
			udcfg |= (udma << dev2);
		}
	}
	pci_write_config_byte(pdev, 0x44, udcfg);
}

/**
 *	optidma_set_pio_mode	-	PIO setup callback
 *	@ap: ATA port
 *	@adev: Device
 *
 *	The libata core provides separate functions for handling PIO and
 *	DMA programming. The architecture of the Firestar makes it easier
 *	for us to have a common function so we provide wrappers
 */

static void optidma_set_pio_mode(struct ata_port *ap, struct ata_device *adev)
{
	optidma_mode_setup(ap, adev, adev->pio_mode);
}

/**
 *	optidma_set_dma_mode	-	DMA setup callback
 *	@ap: ATA port
 *	@adev: Device
 *
 *	The libata core provides separate functions for handling PIO and
 *	DMA programming. The architecture of the Firestar makes it easier
 *	for us to have a common function so we provide wrappers
 */

static void optidma_set_dma_mode(struct ata_port *ap, struct ata_device *adev)
{
	optidma_mode_setup(ap, adev, adev->dma_mode);
}

/**
 *	optiplus_set_pio_mode	-	PIO setup callback
 *	@ap: ATA port
 *	@adev: Device
 *
 *	The libata core provides separate functions for handling PIO and
 *	DMA programming. The architecture of the Firestar makes it easier
 *	for us to have a common function so we provide wrappers
 */

static void optiplus_set_pio_mode(struct ata_port *ap, struct ata_device *adev)
{
	optiplus_mode_setup(ap, adev, adev->pio_mode);
}

/**
 *	optiplus_set_dma_mode	-	DMA setup callback
 *	@ap: ATA port
 *	@adev: Device
 *
 *	The libata core provides separate functions for handling PIO and
 *	DMA programming. The architecture of the Firestar makes it easier
 *	for us to have a common function so we provide wrappers
 */

static void optiplus_set_dma_mode(struct ata_port *ap, struct ata_device *adev)
{
	optiplus_mode_setup(ap, adev, adev->dma_mode);
}

/**
 *	optidma_make_bits43	-	PCI setup helper
 *	@adev: ATA device
 *
 *	Turn the ATA device setup into PCI configuration bits
 *	for register 0x43 and return the two bits needed.
 */

static u8 optidma_make_bits43(struct ata_device *adev)
{
	static const u8 bits43[5] = {
		0, 0, 0, 1, 2
	};
	if (!ata_dev_enabled(adev))
		return 0;
	if (adev->dma_mode)
		return adev->dma_mode - XFER_MW_DMA_0;
	return bits43[adev->pio_mode - XFER_PIO_0];
}

/**
 *	optidma_set_mode	-	mode setup
 *	@link: link to set up
 *	@r_failed: out parameter for failed device
 *
 *	Use the standard setup to tune the chipset and then finalise the
 *	configuration by writing the nibble of extra bits of data into
 *	the chip.
 */

static int optidma_set_mode(struct ata_link *link, struct ata_device **r_failed)
{
	struct ata_port *ap = link->ap;
	u8 r;
	int nybble = 4 * ap->port_no;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int rc  = ata_do_set_mode(link, r_failed);
	if (rc == 0) {
		pci_read_config_byte(pdev, 0x43, &r);

		r &= (0x0F << nybble);
		r |= (optidma_make_bits43(&link->device[0]) +
		     (optidma_make_bits43(&link->device[0]) << 2)) << nybble;
		pci_write_config_byte(pdev, 0x43, r);
	}
	return rc;
}

static struct scsi_host_template optidma_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations optidma_port_ops = {
	.inherits	= &ata_bmdma_port_ops,
	.cable_detect	= ata_cable_40wire,
	.set_piomode	= optidma_set_pio_mode,
	.set_dmamode	= optidma_set_dma_mode,
	.set_mode	= optidma_set_mode,
	.prereset	= optidma_pre_reset,
};

static struct ata_port_operations optiplus_port_ops = {
	.inherits	= &optidma_port_ops,
	.set_piomode	= optiplus_set_pio_mode,
	.set_dmamode	= optiplus_set_dma_mode,
};

/**
 *	optiplus_with_udma	-	Look for UDMA capable setup
 *	@pdev: ATA controller
 */

static int optiplus_with_udma(struct pci_dev *pdev)
{
	u8 r;
	int ret = 0;
	int ioport = 0x22;
	struct pci_dev *dev1;

	/* Find function 1 */
	dev1 = pci_get_device(0x1045, 0xC701, NULL);
	if (dev1 == NULL)
		return 0;

	/* Rev must be >= 0x10 */
	pci_read_config_byte(dev1, 0x08, &r);
	if (r < 0x10)
		goto done_nomsg;
	/* Read the chipset system configuration to check our mode */
	pci_read_config_byte(dev1, 0x5F, &r);
	ioport |= (r << 8);
	outb(0x10, ioport);
	/* Must be 66Mhz sync */
	if ((inb(ioport + 2) & 1) == 0)
		goto done;

	/* Check the ATA arbitration/timing is suitable */
	pci_read_config_byte(pdev, 0x42, &r);
	if ((r & 0x36) != 0x36)
		goto done;
	pci_read_config_byte(dev1, 0x52, &r);
	if (r & 0x80)	/* IDEDIR disabled */
		ret = 1;
done:
	printk(KERN_WARNING "UDMA not supported in this configuration.\n");
done_nomsg:		/* Wrong chip revision */
	pci_dev_put(dev1);
	return ret;
}

static int optidma_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static const struct ata_port_info info_82c700 = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.port_ops = &optidma_port_ops
	};
	static const struct ata_port_info info_82c700_udma = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = ATA_PIO4,
		.mwdma_mask = ATA_MWDMA2,
		.udma_mask = ATA_UDMA2,
		.port_ops = &optiplus_port_ops
	};
	const struct ata_port_info *ppi[] = { &info_82c700, NULL };
	int rc;

	ata_print_version_once(&dev->dev, DRV_VERSION);

	rc = pcim_enable_device(dev);
	if (rc)
		return rc;

	/* Fixed location chipset magic */
	inw(0x1F1);
	inw(0x1F1);
	pci_clock = inb(0x1F5) & 1;		/* 0 = 33Mhz, 1 = 25Mhz */

	if (optiplus_with_udma(dev))
		ppi[0] = &info_82c700_udma;

	return ata_pci_bmdma_init_one(dev, ppi, &optidma_sht, NULL, 0);
}

static const struct pci_device_id optidma[] = {
	{ PCI_VDEVICE(OPTI, 0xD568), },		/* Opti 82C700 */

	{ },
};

static struct pci_driver optidma_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= optidma,
	.probe 		= optidma_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM_SLEEP
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

module_pci_driver(optidma_pci_driver);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Opti Firestar/Firestar Plus");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, optidma);
MODULE_VERSION(DRV_VERSION);
