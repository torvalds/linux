/*
 *	ACPI PATA driver
 *
 *	(c) 2007 Red Hat  <alan@redhat.com>
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
#include <acpi/acnames.h>
#include <acpi/acnamesp.h>
#include <acpi/acparser.h>
#include <acpi/acexcep.h>
#include <acpi/acmacros.h>
#include <acpi/actypes.h>

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

	return ata_std_prereset(link, deadline);
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
 *	pacpi_error_handler - Setup and error handler
 *	@ap: Port to handle
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void pacpi_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, pacpi_pre_reset, ata_std_softreset, NULL,
			   ata_std_postreset);
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
	return ata_pci_default_filter(adev, mask & acpi->mask[adev->devno]);
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
 *	pacpi_qc_issue_prot	-	command issue
 *	@qc: command pending
 *
 *	Called when the libata layer is about to issue a command. We wrap
 *	this interface so that we can load the correct ATA timings if
 *	neccessary.
 */

static unsigned int pacpi_qc_issue_prot(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;
	struct pata_acpi *acpi = ap->private_data;

	if (acpi->gtm.flags & 0x10)
		return ata_qc_issue_prot(qc);

	if (adev != acpi->last) {
		pacpi_set_piomode(ap, adev);
		if (adev->dma_mode)
			pacpi_set_dmamode(ap, adev);
		acpi->last = adev;
	}
	return ata_qc_issue_prot(qc);
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
	/* Use standard CHS mapping rules */
	.bios_param		= ata_std_bios_param,
};

static const struct ata_port_operations pacpi_ops = {
	.set_piomode		= pacpi_set_piomode,
	.set_dmamode		= pacpi_set_dmamode,
	.mode_filter		= pacpi_mode_filter,

	/* Task file is PCI ATA format, use helpers */
	.tf_load		= ata_tf_load,
	.tf_read		= ata_tf_read,
	.check_status		= ata_check_status,
	.exec_command		= ata_exec_command,
	.dev_select		= ata_std_dev_select,

	.freeze			= ata_bmdma_freeze,
	.thaw			= ata_bmdma_thaw,
	.error_handler		= pacpi_error_handler,
	.post_internal_cmd	= ata_bmdma_post_internal_cmd,
	.cable_detect		= pacpi_cable_detect,

	/* BMDMA handling is PCI ATA format, use helpers */
	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= pacpi_qc_issue_prot,
	.data_xfer		= ata_data_xfer,

	/* Timeout handling */
	.irq_handler		= ata_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.irq_on			= ata_irq_on,

	/* Generic PATA PCI ATA helpers */
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
		.sht		= &pacpi_sht,
		.flags		= ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,

		.pio_mask	= 0x1f,
		.mwdma_mask	= 0x07,
		.udma_mask 	= 0x7f,

		.port_ops	= &pacpi_ops,
	};
	const struct ata_port_info *ppi[] = { &info, NULL };
	return ata_pci_init_one(pdev, ppi);
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

