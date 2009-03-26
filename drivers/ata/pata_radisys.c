/*
 *    pata_radisys.c - Intel PATA/SATA controllers
 *
 *	(C) 2006 Red Hat <alan@lxorguk.ukuu.org.uk>
 *
 *    Some parts based on ata_piix.c by Jeff Garzik and others.
 *
 *    A PIIX relative, this device has a single ATA channel and no
 *    slave timings, SITRE or PPE. In that sense it is a close relative
 *    of the original PIIX. It does however support UDMA 33/66 per channel
 *    although no other modes/timings. Also lacking is 32bit I/O on the ATA
 *    port.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/ata.h>

#define DRV_NAME	"pata_radisys"
#define DRV_VERSION	"0.4.4"

/**
 *	radisys_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: ATA port
 *	@adev: Device whose timings we are configuring
 *
 *	Set PIO mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void radisys_set_piomode (struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio	= adev->pio_mode - XFER_PIO_0;
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	u16 idetm_data;
	int control = 0;

	/*
	 *	See Intel Document 298600-004 for the timing programing rules
	 *	for PIIX/ICH. Note that the early PIIX does not have the slave
	 *	timing port at 0x44. The Radisys is a relative of the PIIX
	 *	but not the same so be careful.
	 */

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },	/* Check me */
			    { 0, 0 },
			    { 1, 1 },
			    { 2, 2 },
			    { 3, 3 }, };

	if (pio > 0)
		control |= 1;	/* TIME1 enable */
	if (ata_pio_need_iordy(adev))
		control |= 2;	/* IE IORDY */

	pci_read_config_word(dev, 0x40, &idetm_data);

	/* Enable IE and TIME as appropriate. Clear the other
	   drive timing bits */
	idetm_data &= 0xCCCC;
	idetm_data |= (control << (4 * adev->devno));
	idetm_data |= (timings[pio][0] << 12) |
			(timings[pio][1] << 8);
	pci_write_config_word(dev, 0x40, idetm_data);

	/* Track which port is configured */
	ap->private_data = adev;
}

/**
 *	radisys_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device to program
 *
 *	Set MWDMA mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void radisys_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	u16 idetm_data;
	u8 udma_enable;

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 1 },
			    { 2, 2 },
			    { 3, 3 }, };

	/*
	 * MWDMA is driven by the PIO timings. We must also enable
	 * IORDY unconditionally.
	 */

	pci_read_config_word(dev, 0x40, &idetm_data);
	pci_read_config_byte(dev, 0x48, &udma_enable);

	if (adev->dma_mode < XFER_UDMA_0) {
		unsigned int mwdma	= adev->dma_mode - XFER_MW_DMA_0;
		const unsigned int needed_pio[3] = {
			XFER_PIO_0, XFER_PIO_3, XFER_PIO_4
		};
		int pio = needed_pio[mwdma] - XFER_PIO_0;
		int control = 3;	/* IORDY|TIME0 */

		/* If the drive MWDMA is faster than it can do PIO then
		   we must force PIO0 for PIO cycles. */

		if (adev->pio_mode < needed_pio[mwdma])
			control = 1;

		/* Mask out the relevant control and timing bits we will load. Also
		   clear the other drive TIME register as a precaution */

		idetm_data &= 0xCCCC;
		idetm_data |= control << (4 * adev->devno);
		idetm_data |= (timings[pio][0] << 12) | (timings[pio][1] << 8);

		udma_enable &= ~(1 << adev->devno);
	} else {
		u8 udma_mode;

		/* UDMA66 on: UDMA 33 and 66 are switchable via register 0x4A */

		pci_read_config_byte(dev, 0x4A, &udma_mode);

		if (adev->xfer_mode == XFER_UDMA_2)
			udma_mode &= ~ (1 << adev->devno);
		else /* UDMA 4 */
			udma_mode |= (1 << adev->devno);

		pci_write_config_byte(dev, 0x4A, udma_mode);

		udma_enable |= (1 << adev->devno);
	}
	pci_write_config_word(dev, 0x40, idetm_data);
	pci_write_config_byte(dev, 0x48, udma_enable);

	/* Track which port is configured */
	ap->private_data = adev;
}

/**
 *	radisys_qc_issue	-	command issue
 *	@qc: command pending
 *
 *	Called when the libata layer is about to issue a command. We wrap
 *	this interface so that we can load the correct ATA timings if
 *	necessary. Our logic also clears TIME0/TIME1 for the other device so
 *	that, even if we get this wrong, cycles to the other device will
 *	be made PIO0.
 */

static unsigned int radisys_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;

	if (adev != ap->private_data) {
		/* UDMA timing is not shared */
		if (adev->dma_mode < XFER_UDMA_0) {
			if (adev->dma_mode)
				radisys_set_dmamode(ap, adev);
			else if (adev->pio_mode)
				radisys_set_piomode(ap, adev);
		}
	}
	return ata_sff_qc_issue(qc);
}


static struct scsi_host_template radisys_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations radisys_pata_ops = {
	.inherits		= &ata_bmdma_port_ops,
	.qc_issue		= radisys_qc_issue,
	.cable_detect		= ata_cable_unknown,
	.set_piomode		= radisys_set_piomode,
	.set_dmamode		= radisys_set_dmamode,
};


/**
 *	radisys_init_one - Register PIIX ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in radisys_pci_tbl matching with @pdev
 *
 *	Called from kernel PCI layer.  We probe for combined mode (sigh),
 *	and then hand over control to libata, for it to do the rest.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int radisys_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	static const struct ata_port_info info = {
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA12_ONLY,
		.udma_mask	= ATA_UDMA24_ONLY,
		.port_ops	= &radisys_pata_ops,
	};
	const struct ata_port_info *ppi[] = { &info, NULL };

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev,
			   "version " DRV_VERSION "\n");

	return ata_pci_sff_init_one(pdev, ppi, &radisys_sht, NULL);
}

static const struct pci_device_id radisys_pci_tbl[] = {
	{ PCI_VDEVICE(RADISYS, 0x8201), },

	{ }	/* terminate list */
};

static struct pci_driver radisys_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= radisys_pci_tbl,
	.probe			= radisys_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= ata_pci_device_suspend,
	.resume			= ata_pci_device_resume,
#endif
};

static int __init radisys_init(void)
{
	return pci_register_driver(&radisys_pci_driver);
}

static void __exit radisys_exit(void)
{
	pci_unregister_driver(&radisys_pci_driver);
}

module_init(radisys_init);
module_exit(radisys_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("SCSI low-level driver for Radisys R82600 controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, radisys_pci_tbl);
MODULE_VERSION(DRV_VERSION);

