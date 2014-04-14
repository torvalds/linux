/*
 *    pata_jmicron.c - JMicron ATA driver for non AHCI mode. This drives the
 *			PATA port of the controller. The SATA ports are
 *			driven by AHCI in the usual configuration although
 *			this driver can handle other setups if we need it.
 *
 *	(c) 2006 Red Hat
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

#define DRV_NAME	"pata_jmicron"
#define DRV_VERSION	"0.1.5"

typedef enum {
	PORT_PATA0 = 0,
	PORT_PATA1 = 1,
	PORT_SATA = 2,
} port_type;

/**
 *	jmicron_pre_reset	-	check for 40/80 pin
 *	@link: ATA link
 *	@deadline: deadline jiffies for the operation
 *
 *	Perform the PATA port setup we need.
 *
 *	On the Jmicron 361/363 there is a single PATA port that can be mapped
 *	either as primary or secondary (or neither). We don't do any policy
 *	and setup here. We assume that has been done by init_one and the
 *	BIOS.
 */
static int jmicron_pre_reset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u32 control;
	u32 control5;
	int port_mask = 1<< (4 * ap->port_no);
	int port = ap->port_no;
	port_type port_map[2];

	/* Check if our port is enabled */
	pci_read_config_dword(pdev, 0x40, &control);
	if ((control & port_mask) == 0)
		return -ENOENT;

	/* There are two basic mappings. One has the two SATA ports merged
	   as master/slave and the secondary as PATA, the other has only the
	   SATA port mapped */
	if (control & (1 << 23)) {
		port_map[0] = PORT_SATA;
		port_map[1] = PORT_PATA0;
	} else {
		port_map[0] = PORT_SATA;
		port_map[1] = PORT_SATA;
	}

	/* The 365/366 may have this bit set to map the second PATA port
	   as the internal primary channel */
	pci_read_config_dword(pdev, 0x80, &control5);
	if (control5 & (1<<24))
		port_map[0] = PORT_PATA1;

	/* The two ports may then be logically swapped by the firmware */
	if (control & (1 << 22))
		port = port ^ 1;

	/*
	 *	Now we know which physical port we are talking about we can
	 *	actually do our cable checking etc. Thankfully we don't need
	 *	to do the plumbing for other cases.
	 */
	switch (port_map[port]) {
	case PORT_PATA0:
		if ((control & (1 << 5)) == 0)
			return -ENOENT;
		if (control & (1 << 3))	/* 40/80 pin primary */
			ap->cbl = ATA_CBL_PATA40;
		else
			ap->cbl = ATA_CBL_PATA80;
		break;
	case PORT_PATA1:
		/* Bit 21 is set if the port is enabled */
		if ((control5 & (1 << 21)) == 0)
			return -ENOENT;
		if (control5 & (1 << 19))	/* 40/80 pin secondary */
			ap->cbl = ATA_CBL_PATA40;
		else
			ap->cbl = ATA_CBL_PATA80;
		break;
	case PORT_SATA:
		ap->cbl = ATA_CBL_SATA;
		break;
	}
	return ata_sff_prereset(link, deadline);
}

/* No PIO or DMA methods needed for this device */

static struct scsi_host_template jmicron_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations jmicron_ops = {
	.inherits		= &ata_bmdma_port_ops,
	.prereset		= jmicron_pre_reset,
};


/**
 *	jmicron_init_one - Register Jmicron ATA PCI device with kernel services
 *	@pdev: PCI device to register
 *	@ent: Entry in jmicron_pci_tbl matching with @pdev
 *
 *	Called from kernel PCI layer.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, or -ERRNO value.
 */

static int jmicron_init_one (struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.flags	= ATA_FLAG_SLAVE_POSS,

		.pio_mask	= ATA_PIO4,
		.mwdma_mask	= ATA_MWDMA2,
		.udma_mask 	= ATA_UDMA5,

		.port_ops	= &jmicron_ops,
	};
	const struct ata_port_info *ppi[] = { &info, NULL };

	return ata_pci_bmdma_init_one(pdev, ppi, &jmicron_sht, NULL, 0);
}

static const struct pci_device_id jmicron_pci_tbl[] = {
	{ PCI_VENDOR_ID_JMICRON, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_STORAGE_IDE << 8, 0xffff00, 0 },
	{ }	/* terminate list */
};

static struct pci_driver jmicron_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= jmicron_pci_tbl,
	.probe			= jmicron_init_one,
	.remove			= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend		= ata_pci_device_suspend,
	.resume			= ata_pci_device_resume,
#endif
};

module_pci_driver(jmicron_pci_driver);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("SCSI low-level driver for Jmicron PATA ports");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, jmicron_pci_tbl);
MODULE_VERSION(DRV_VERSION);

