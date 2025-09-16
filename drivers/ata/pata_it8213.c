// SPDX-License-Identifier: GPL-2.0-only
/*
 *    pata_it8213.c - iTE Tech. Inc.  IT8213 PATA driver
 *
 *    The IT8213 is a very Intel ICH like device for timing purposes, having
 *    a similar register layout and the same split clock arrangement. Cable
 *    detection is different, and it does not have slave channels or all the
 *    clutter of later ICH/SATA setups.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/ata.h>

#define DRV_NAME	"pata_it8213"
#define DRV_VERSION	"0.0.3"

/**
 *	it8213_pre_reset	-	probe begin
 *	@link: link
 *	@deadline: deadline jiffies for the operation
 *
 *	Filter out ports by the enable bits before doing the normal reset
 *	and probe.
 */

static int it8213_pre_reset(struct ata_link *link, unsigned long deadline)
{
	static const struct pci_bits it8213_enable_bits[] = {
		{ 0x41U, 1U, 0x80UL, 0x80UL },	/* port 0 */
	};
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	if (!pci_test_config_bits(pdev, &it8213_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_sff_prereset(link, deadline);
}

/**
 *	it8213_cable_detect	-	check for 40/80 pin
 *	@ap: Port
 *
 *	Perform cable detection for the 8213 ATA interface. This is
 *	different to the PIIX arrangement
 */

static int it8213_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u8 tmp;
	pci_read_config_byte(pdev, 0x42, &tmp);
	if (tmp & 2)	/* The initial docs are incorrect */
		return ATA_CBL_PATA40;
	return ATA_CBL_PATA80;
}

/**
 *	it8213_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device whose timings we are configuring
 *
 *	Set PIO mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void it8213_set_piomode (struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio	= adev->pio_mode - XFER_PIO_0;
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	unsigned int master_port = ap->port_no ? 0x42 : 0x40;
	u16 master_data;
	int control = 0;

	/*
	 *	See Intel Document 298600-004 for the timing programming rules
	 *	for PIIX/ICH. The 8213 is a clone so very similar
	 */

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	if (pio > 1)
		control |= 1;	/* TIME */
	if (ata_pio_need_iordy(adev))	/* PIO 3/4 require IORDY */
		control |= 2;	/* IE */
	/* Bit 2 is set for ATAPI on the IT8213 - reverse of ICH/PIIX */
	if (adev->class != ATA_DEV_ATA)
		control |= 4;	/* PPE */

	pci_read_config_word(dev, master_port, &master_data);

	/* Set PPE, IE, and TIME as appropriate */
	if (adev->devno == 0) {
		master_data &= 0xCCF0;
		master_data |= control;
		master_data |= (timings[pio][0] << 12) |
			(timings[pio][1] << 8);
	} else {
		u8 slave_data;

		master_data &= 0xFF0F;
		master_data |= (control << 4);

		/* Slave timing in separate register */
		pci_read_config_byte(dev, 0x44, &slave_data);
		slave_data &= 0xF0;
		slave_data |= (timings[pio][0] << 2) | timings[pio][1];
		pci_write_config_byte(dev, 0x44, slave_data);
	}

	master_data |= 0x4000;	/* Ensure SITRE is set */
	pci_write_config_word(dev, master_port, master_data);
}

/**
 *	it8213_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device to program
 *
 *	Set UDMA/MWDMA mode for device, in host controller PCI config space.
 *	This device is basically an ICH alike.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void it8213_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	u16 master_data;
	u8 speed		= adev->dma_mode;
	int devid		= adev->devno;
	u8 udma_enable;

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	pci_read_config_word(dev, 0x40, &master_data);
	pci_read_config_byte(dev, 0x48, &udma_enable);

	if (speed >= XFER_UDMA_0) {
		unsigned int udma = adev->dma_mode - XFER_UDMA_0;
		u16 udma_timing;
		u16 ideconf;
		int u_clock, u_speed;

		/* Clocks follow the PIIX style */
		u_speed = min(2 - (udma & 1), udma);
		if (udma > 4)
			u_clock = 0x1000;	/* 100Mhz */
		else if (udma > 2)
			u_clock = 1;		/* 66Mhz */
		else
			u_clock = 0;		/* 33Mhz */

		udma_enable |= (1 << devid);

		/* Load the UDMA cycle time */
		pci_read_config_word(dev, 0x4A, &udma_timing);
		udma_timing &= ~(3 << (4 * devid));
		udma_timing |= u_speed << (4 * devid);
		pci_write_config_word(dev, 0x4A, udma_timing);

		/* Load the clock selection */
		pci_read_config_word(dev, 0x54, &ideconf);
		ideconf &= ~(0x1001 << devid);
		ideconf |= u_clock << devid;
		pci_write_config_word(dev, 0x54, ideconf);
	} else {
		/*
		 * MWDMA is driven by the PIO timings. We must also enable
		 * IORDY unconditionally along with TIME1. PPE has already
		 * been set when the PIO timing was set.
		 */
		unsigned int mwdma	= adev->dma_mode - XFER_MW_DMA_0;
		unsigned int control;
		u8 slave_data;
		static const unsigned int needed_pio[3] = {
			XFER_PIO_0, XFER_PIO_3, XFER_PIO_4
		};
		int pio = needed_pio[mwdma] - XFER_PIO_0;

		control = 3;	/* IORDY|TIME1 */

		/* If the drive MWDMA is faster than it can do PIO then
		   we must force PIO into PIO0 */

		if (adev->pio_mode < needed_pio[mwdma])
			/* Enable DMA timing only */
			control |= 8;	/* PIO cycles in PIO0 */

		if (devid) {	/* Slave */
			master_data &= 0xFF4F;  /* Mask out IORDY|TIME1|DMAONLY */
			master_data |= control << 4;
			pci_read_config_byte(dev, 0x44, &slave_data);
			slave_data &= 0xF0;
			/* Load the matching timing */
			slave_data |= ((timings[pio][0] << 2) | timings[pio][1]) << (ap->port_no ? 4 : 0);
			pci_write_config_byte(dev, 0x44, slave_data);
		} else { 	/* Master */
			master_data &= 0xCCF4;	/* Mask out IORDY|TIME1|DMAONLY
						   and master timing bits */
			master_data |= control;
			master_data |=
				(timings[pio][0] << 12) |
				(timings[pio][1] << 8);
		}
		udma_enable &= ~(1 << devid);
		pci_write_config_word(dev, 0x40, master_data);
	}
	pci_write_config_byte(dev, 0x48, udma_enable);
}

static const struct scsi_host_template it8213_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};


static struct ata_port_operations it8213_ops = {
	.inherits		= &ata_bmdma_port_ops,
	.cable_detect		= it8213_cable_detect,
	.set_piomode		= it8213_set_piomode,
	.set_dmamode		= it8213_set_dmamode,
	.reset.prereset		= it8213_pre_reset,
};


/**
 *	it8213_init_one - Register 8213 ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in it8213_pci_tbl matching with @pdev
 *
 *	Called from kernel PCI layer.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int it8213_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static const struct ata_port_info info = {
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA12_ONLY,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &it8213_ops,
	};
	/* Current IT8213 stuff is single port */
	const struct ata_port_info *ppi[] = { &info, &ata_dummy_port_info };

	ata_print_version_once(&pdev->dev, DRV_VERSION);

	return ata_pci_bmdma_init_one(pdev, ppi, &it8213_sht, NULL, 0);
}

static const struct pci_device_id it8213_pci_tbl[] = {
	{ PCI_VDEVICE(ITE, PCI_DEVICE_ID_ITE_8213), },

	{ }	/* terminate list */
};

static struct pci_driver it8213_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= it8213_pci_tbl,
	.probe			= it8213_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM_SLEEP
	.suspend		= ata_pci_device_suspend,
	.resume			= ata_pci_device_resume,
#endif
};

module_pci_driver(it8213_pci_driver);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("SCSI low-level driver for the ITE 8213");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, it8213_pci_tbl);
MODULE_VERSION(DRV_VERSION);
