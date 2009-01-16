/*
 *	ACPI PATA driver
 *
 *	(c) 2007 Red Hat
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <acpi/acpi_bus.h>

#include <linux/libata.h>
#include <linux/ata.h>

#define DRV_NAME	"pata_acpi"
#define DRV_VERSION	"0.2.3"

struct pata_acpi {
	struct ata_acpi_gtm gtm;
	void *last;
	unsigned long mask[2];
};

/**
 *	pacpi_pre_reset	-	check for 40/80 pin
 *	@ap: Port
 *	@deadline: deadline jiffies for the operation
 *
 *	Perform the PATA port setup we need.
 */

static int pacpi_pre_reset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pata_acpi *acpi = ap->private_data;
	if (ap->acpi_handle == NULL || ata_acpi_gtm(ap, &acpi->gtm) < 0)
		return -ENODEV;

	return ata_sff_prereset(link, deadline);
}

/**
 *	pacpi_cable_detect	-	cable type detection
 *	@ap: port to detect
 *
 *	Perform device specific cable detection
 */

static int pacpi_cable_detect(struct ata_port *ap)
{
	struct pata_acpi *acpi = ap->private_data;

	if ((acpi->mask[0] | acpi->mask[1]) & (0xF8 << ATA_SHIFT_UDMA))
		return ATA_CBL_PATA80;
	else
		return ATA_CBL_PATA40;
}

/**
 *	pacpi_discover_modes	-	filter non ACPI modes
 *	@adev: ATA device
 *	@mask: proposed modes
 *
 *	Try the modes available and see which ones the ACPI method will
 *	set up sensibly. From this we get a mask of ACPI modes we can use
 */

static unsigned long pacpi_discover_modes(struct ata_port *ap, struct ata_device *adev)
{
	struct pata_acpi *acpi = ap->private_data;
	struct ata_acpi_gtm probe;
	unsigned int xfer_mask;

	probe = acpi->gtm;

	ata_acpi_gtm(ap, &probe);

	xfer_mask = ata_acpi_gtm_xfermask(adev, &probe);

	if (xfer_mask & (0xF8 << ATA_SHIFT_UDMA))
		ap->cbl = ATA_CBL_PATA80;

	return xfer_mask;
}

/**
 *	pacpi_mode_filter	-	mode filter for ACPI
 *	@adev: device
 *	@mask: mask of valid modes
 *
 *	Filter the valid mode list according to our own specific rules, in
 *	this case the list of discovered valid modes obtained by ACPI probing
 */

static unsigned long pacpi_mode_filter(struct ata_device *adev, unsigned long mask)
{
	struct pata_acpi *acpi = adev->link->ap->private_data;
	return ata_bmdma_mode_filter(adev, mask & acpi->mask[adev->devno]);
}

/**
 *	pacpi_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 */

static void pacpi_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	int unit = adev->devno;
	struct pata_acpi *acpi = ap->private_data;
	const struct ata_timing *t;

	if (!(acpi->gtm.flags & 0x10))
		unit = 0;

	/* Now stuff the nS values into the structure */
	t = ata_timing_find_mode(adev->pio_mode);
	acpi->gtm.drive[unit].pio = t->cycle;
	ata_acpi_stm(ap, &acpi->gtm);
	/* See what mode we actually got */
	ata_acpi_gtm(ap, &acpi->gtm);
}

/**
 *	pacpi_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 */

static void pacpi_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	int unit = adev->devno;
	struct pata_acpi *acpi = ap->private_data;
	const struct ata_timing *t;

	if (!(acpi->gtm.flags & 0x10))
		unit = 0;

	/* Now stuff the nS values into the structure */
	t = ata_timing_find_mode(adev->dma_mode);
	if (adev->dma_mode >= XFER_UDMA_0) {
		acpi->gtm.drive[unit].dma = t->udma;
		acpi->gtm.flags |= (1 << (2 * unit));
	} else {
		acpi->gtm.drive[unit].dma = t->cycle;
		acpi->gtm.flags &= ~(1 << (2 * unit));
	}
	ata_acpi_stm(ap, &acpi->gtm);
	/* See what mode we actually got */
	ata_acpi_gtm(ap, &acpi->gtm);
}

/**
 *	pacpi_qc_issue	-	command issue
 *	@qc: command pending
 *
 *	Called when the libata layer is about to issue a command. We wrap
 *	this interface so that we can load the correct ATA timings if
 *	neccessary.
 */

static unsigned int pacpi_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;
	struct pata_acpi *acpi = ap->private_data;

	if (acpi->gtm.flags & 0x10)
		return ata_sff_qc_issue(qc);

	if (adev != acpi->last) {
		pacpi_set_piomode(ap, adev);
		if (ata_dma_enabled(adev))
			pacpi_set_dmamode(ap, adev);
		acpi->last = adev;
	}
	return ata_sff_qc_issue(qc);
}

/**
 *	pacpi_port_start	-	port setup
 *	@ap: ATA port being set up
 *
 *	Use the port_start hook to maintain private control structures
 */

static int pacpi_port_start(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct pata_acpi *acpi;

	int ret;

	if (ap->acpi_handle == NULL)
		return -ENODEV;

	acpi = ap->private_data = devm_kzalloc(&pdev->dev, sizeof(struct pata_acpi), GFP_KERNEL);
	if (ap->private_data == NULL)
		return -ENOMEM;
	acpi->mask[0] = pacpi_discover_modes(ap, &ap->link.device[0]);
	acpi->mask[1] = pacpi_discover_modes(ap, &ap->link.device[1]);
	ret = ata_sff_port_start(ap);
	if (ret < 0)
		return ret;

	return ret;
}

static struct scsi_host_template pacpi_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations pacpi_ops = {
	.inherits		= &ata_bmdma_port_ops,
	.qc_issue		= pacpi_qc_issue,
	.cable_detect		= pacpi_cable_detect,
	.mode_filter		= pacpi_mode_filter,
	.set_piomode		= pacpi_set_piomode,
	.set_dmamode		= pacpi_set_dmamode,
	.prereset		= pacpi_pre_reset,
	.port_start		= pacpi_port_start,
};


/**
 *	pacpi_init_one - Register ACPI ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in pacpi_pci_tbl matching with @pdev
 *
 *	Called from kernel PCI layer.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int pacpi_init_one (struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.flags		= ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,

		.pio_mask	= 0x1f,
		.mwdma_mask	= 0x07,
		.udma_mask 	= 0x7f,

		.port_ops	= &pacpi_ops,
	};
	const struct ata_port_info *ppi[] = { &info, NULL };
	if (pdev->vendor == PCI_VENDOR_ID_ATI) {
		int rc = pcim_enable_device(pdev);
		if (rc < 0)
			return rc;
		pcim_pin_device(pdev);
	}
	return ata_pci_sff_init_one(pdev, ppi, &pacpi_sht, NULL);
}

static const struct pci_device_id pacpi_pci_tbl[] = {
	{ PCI_ANY_ID,		PCI_ANY_ID,			   PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_STORAGE_IDE << 8, 0xFFFFFF00UL, 1},
	{ }	/* terminate list */
};

static struct pci_driver pacpi_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= pacpi_pci_tbl,
	.probe			= pacpi_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= ata_pci_device_suspend,
	.resume			= ata_pci_device_resume,
#endif
};

static int __init pacpi_init(void)
{
	return pci_register_driver(&pacpi_pci_driver);
}

static void __exit pacpi_exit(void)
{
	pci_unregister_driver(&pacpi_pci_driver);
}

module_init(pacpi_init);
module_exit(pacpi_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("SCSI low-level driver for ATA in ACPI mode");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pacpi_pci_tbl);
MODULE_VERSION(DRV_VERSION);

