/*
 *    pata_oldpiix.c - Intel PATA/SATA controllers
 *
 *	(C) 2005 Red Hat
 *
 *    Some parts based on ata_piix.c by Jeff Garzik and others.
 *
 *    Early PIIX differs significantly from the later PIIX as it lacks
 *    SITRE and the slave timing registers. This means that you have to
 *    set timing per channel, or be clever. Libata tells us whenever it
 *    does drive selection and we use this to reload the timings.
 *
 *    Because of these behaviour differences PIIX gets its own driver module.
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

#define DRV_NAME	"pata_oldpiix"
#define DRV_VERSION	"0.5.5"

/**
 *	oldpiix_pre_reset		-	probe begin
 *	@link: ATA link
 *	@deadline: deadline jiffies for the operation
 *
 *	Set up cable type and use generic probe init
 */

static int oldpiix_pre_reset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	static const struct pci_bits oldpiix_enable_bits[] = {
		{ 0x41U, 1U, 0x80UL, 0x80UL },	/* port 0 */
		{ 0x43U, 1U, 0x80UL, 0x80UL },	/* port 1 */
	};

	if (!pci_test_config_bits(pdev, &oldpiix_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_sff_prereset(link, deadline);
}

/**
 *	oldpiix_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device whose timings we are configuring
 *
 *	Set PIO mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void oldpiix_set_piomode (struct ata_port *ap, struct ata_device *adev)
{
	unsigned int pio	= adev->pio_mode - XFER_PIO_0;
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	unsigned int idetm_port= ap->port_no ? 0x42 : 0x40;
	u16 idetm_data;
	int control = 0;

	/*
	 *	See Intel Document 298600-004 for the timing programing rules
	 *	for PIIX/ICH. Note that the early PIIX does not have the slave
	 *	timing port at 0x44.
	 */

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	if (pio > 1)
		control |= 1;	/* TIME */
	if (ata_pio_need_iordy(adev))
		control |= 2;	/* IE */

	/* Intel specifies that the prefetch/posting is for disk only */
	if (adev->class == ATA_DEV_ATA)
		control |= 4;	/* PPE */

	pci_read_config_word(dev, idetm_port, &idetm_data);

	/*
	 * Set PPE, IE and TIME as appropriate.
	 * Clear the other drive's timing bits.
	 */
	if (adev->devno == 0) {
		idetm_data &= 0xCCE0;
		idetm_data |= control;
	} else {
		idetm_data &= 0xCC0E;
		idetm_data |= (control << 4);
	}
	idetm_data |= (timings[pio][0] << 12) |
			(timings[pio][1] << 8);
	pci_write_config_word(dev, idetm_port, idetm_data);

	/* Track which port is configured */
	ap->private_data = adev;
}

/**
 *	oldpiix_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: Device to program
 *
 *	Set MWDMA mode for device, in host controller PCI config space.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void oldpiix_set_dmamode (struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *dev	= to_pci_dev(ap->host->dev);
	u8 idetm_port		= ap->port_no ? 0x42 : 0x40;
	u16 idetm_data;

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	/*
	 * MWDMA is driven by the PIO timings. We must also enable
	 * IORDY unconditionally along with TIME1. PPE has already
	 * been set when the PIO timing was set.
	 */

	unsigned int mwdma	= adev->dma_mode - XFER_MW_DMA_0;
	unsigned int control;
	const unsigned int needed_pio[3] = {
		XFER_PIO_0, XFER_PIO_3, XFER_PIO_4
	};
	int pio = needed_pio[mwdma] - XFER_PIO_0;

	pci_read_config_word(dev, idetm_port, &idetm_data);

	control = 3;	/* IORDY|TIME0 */
	/* Intel specifies that the PPE functionality is for disk only */
	if (adev->class == ATA_DEV_ATA)
		control |= 4;	/* PPE enable */

	/* If the drive MWDMA is faster than it can do PIO then
	   we must force PIO into PIO0 */

	if (adev->pio_mode < needed_pio[mwdma])
		/* Enable DMA timing only */
		control |= 8;	/* PIO cycles in PIO0 */

	/* Mask out the relevant control and timing bits we will load. Also
	   clear the other drive TIME register as a precaution */
	if (adev->devno == 0) {
		idetm_data &= 0xCCE0;
		idetm_data |= control;
	} else {
		idetm_data &= 0xCC0E;
		idetm_data |= (control << 4);
	}
	idetm_data |= (timings[pio][0] << 12) | (timings[pio][1] << 8);
	pci_write_config_word(dev, idetm_port, idetm_data);

	/* Track which port is configured */
	ap->private_data = adev;
}

/**
 *	oldpiix_qc_issue	-	command issue
 *	@qc: command pending
 *
 *	Called when the libata layer is about to issue a command. We wrap
 *	this interface so that we can load the correct ATA timings if
 *	necessary. Our logic also clears TIME0/TIME1 for the other device so
 *	that, even if we get this wrong, cycles to the other device will
 *	be made PIO0.
 */

static unsigned int oldpiix_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;

	if (adev != ap->private_data) {
		oldpiix_set_piomode(ap, adev);
		if (ata_dma_enabled(adev))
			oldpiix_set_dmamode(ap, adev);
	}
	return ata_sff_qc_issue(qc);
}


static struct scsi_host_template oldpiix_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations oldpiix_pata_ops = {
	.inherits		= &ata_bmdma_port_ops,
	.qc_issue		= oldpiix_qc_issue,
	.cable_detect		= ata_cable_40wire,
	.set_piomode		= oldpiix_set_piomode,
	.set_dmamode		= oldpiix_set_dmamode,
	.prereset		= oldpiix_pre_reset,
};


/**
 *	oldpiix_init_one - Register PIIX ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in oldpiix_pci_tbl matching with @pdev
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

static int oldpiix_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version;
	static const struct ata_port_info info = {
		.flags		= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.port_ops	= &oldpiix_pata_ops,
	};
	const struct ata_port_info *ppi[] = { &info, NULL };

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev,
			   "version " DRV_VERSION "\n");

	return ata_pci_sff_init_one(pdev, ppi, &oldpiix_sht, NULL);
}

static const struct pci_device_id oldpiix_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, 0x1230), },

	{ }	/* terminate list */
};

static struct pci_driver oldpiix_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= oldpiix_pci_tbl,
	.probe			= oldpiix_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= ata_pci_device_suspend,
	.resume			= ata_pci_device_resume,
#endif
};

static int __init oldpiix_init(void)
{
	return pci_register_driver(&oldpiix_pci_driver);
}

static void __exit oldpiix_exit(void)
{
	pci_unregister_driver(&oldpiix_pci_driver);
}

module_init(oldpiix_init);
module_exit(oldpiix_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("SCSI low-level driver for early PIIX series controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, oldpiix_pci_tbl);
MODULE_VERSION(DRV_VERSION);

