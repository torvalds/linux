/*
 * pata_serverworks.c 	- Serverworks PATA for new ATA layer
 *			  (C) 2005 Red Hat Inc
 *
 * based upon
 *
 * serverworks.c
 *
 * Copyright (C) 1998-2000 Michel Aubry
 * Copyright (C) 1998-2000 Andrzej Krzysztofowicz
 * Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
 * Portions copyright (c) 2001 Sun Microsystems
 *
 *
 * RCC/ServerWorks IDE driver for Linux
 *
 *   OSB4: `Open South Bridge' IDE Interface (fn 1)
 *         supports UDMA mode 2 (33 MB/s)
 *
 *   CSB5: `Champion South Bridge' IDE Interface (fn 1)
 *         all revisions support UDMA mode 4 (66 MB/s)
 *         revision A2.0 and up support UDMA mode 5 (100 MB/s)
 *
 *         *** The CSB5 does not provide ANY register ***
 *         *** to detect 80-conductor cable presence. ***
 *
 *   CSB6: `Champion South Bridge' IDE Interface (optional: third channel)
 *
 * Documentation:
 *	Available under NDA only. Errata info very hard to get.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_serverworks"
#define DRV_VERSION "0.4.3"

#define SVWKS_CSB5_REVISION_NEW	0x92 /* min PCI_REVISION_ID for UDMA5 (A2.0) */
#define SVWKS_CSB6_REVISION	0xa0 /* min PCI_REVISION_ID for UDMA4 (A1.0) */

/* Seagate Barracuda ATA IV Family drives in UDMA mode 5
 * can overrun their FIFOs when used with the CSB5 */

static const char *csb_bad_ata100[] = {
	"ST320011A",
	"ST340016A",
	"ST360021A",
	"ST380021A",
	NULL
};

/**
 *	dell_cable	-	Dell serverworks cable detection
 *	@ap: ATA port to do cable detect
 *
 *	Dell hide the 40/80 pin select for their interfaces in the top two
 *	bits of the subsystem ID.
 */

static int dell_cable(struct ata_port *ap) {
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (pdev->subsystem_device & (1 << (ap->port_no + 14)))
		return ATA_CBL_PATA80;
	return ATA_CBL_PATA40;
}

/**
 *	sun_cable	-	Sun Cobalt 'Alpine' cable detection
 *	@ap: ATA port to do cable select
 *
 *	Cobalt CSB5 IDE hides the 40/80pin in the top two bits of the
 *	subsystem ID the same as dell. We could use one function but we may
 *	need to extend the Dell one in future
 */

static int sun_cable(struct ata_port *ap) {
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	if (pdev->subsystem_device & (1 << (ap->port_no + 14)))
		return ATA_CBL_PATA80;
	return ATA_CBL_PATA40;
}

/**
 *	osb4_cable	-	OSB4 cable detect
 *	@ap: ATA port to check
 *
 *	The OSB4 isn't UDMA66 capable so this is easy
 */

static int osb4_cable(struct ata_port *ap) {
	return ATA_CBL_PATA40;
}

/**
 *	csb_cable	-	CSB5/6 cable detect
 *	@ap: ATA port to check
 *
 *	Serverworks default arrangement is to use the drive side detection
 *	only.
 */

static int csb_cable(struct ata_port *ap) {
	return ATA_CBL_PATA_UNK;
}

struct sv_cable_table {
	int device;
	int subvendor;
	int (*cable_detect)(struct ata_port *ap);
};

/*
 *	Note that we don't copy the old serverworks code because the old
 *	code contains obvious mistakes
 */

static struct sv_cable_table cable_detect[] = {
	{ PCI_DEVICE_ID_SERVERWORKS_CSB5IDE, PCI_VENDOR_ID_DELL, dell_cable },
	{ PCI_DEVICE_ID_SERVERWORKS_CSB6IDE, PCI_VENDOR_ID_DELL, dell_cable },
	{ PCI_DEVICE_ID_SERVERWORKS_CSB5IDE, PCI_VENDOR_ID_SUN,  sun_cable },
	{ PCI_DEVICE_ID_SERVERWORKS_OSB4IDE, PCI_ANY_ID, osb4_cable },
	{ PCI_DEVICE_ID_SERVERWORKS_CSB5IDE, PCI_ANY_ID, csb_cable },
	{ PCI_DEVICE_ID_SERVERWORKS_CSB6IDE, PCI_ANY_ID, csb_cable },
	{ PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2, PCI_ANY_ID, csb_cable },
	{ PCI_DEVICE_ID_SERVERWORKS_HT1000IDE, PCI_ANY_ID, csb_cable },
	{ }
};

/**
 *	serverworks_cable_detect	-	cable detection
 *	@ap: ATA port
 *
 *	Perform cable detection according to the device and subvendor
 *	identifications
 */

static int serverworks_cable_detect(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	struct sv_cable_table *cb = cable_detect;

	while(cb->device) {
		if (cb->device == pdev->device &&
		    (cb->subvendor == pdev->subsystem_vendor ||
		      cb->subvendor == PCI_ANY_ID)) {
			return cb->cable_detect(ap);
		}
		cb++;
	}

	BUG();
	return -1;	/* kill compiler warning */
}

/**
 *	serverworks_is_csb	-	Check for CSB or OSB
 *	@pdev: PCI device to check
 *
 *	Returns true if the device being checked is known to be a CSB
 *	series device.
 */

static u8 serverworks_is_csb(struct pci_dev *pdev)
{
	switch (pdev->device) {
		case PCI_DEVICE_ID_SERVERWORKS_CSB5IDE:
		case PCI_DEVICE_ID_SERVERWORKS_CSB6IDE:
		case PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2:
		case PCI_DEVICE_ID_SERVERWORKS_HT1000IDE:
			return 1;
		default:
			break;
	}
	return 0;
}

/**
 *	serverworks_osb4_filter	-	mode selection filter
 *	@adev: ATA device
 *	@mask: Mask of proposed modes
 *
 *	Filter the offered modes for the device to apply controller
 *	specific rules. OSB4 requires no UDMA for disks due to a FIFO
 *	bug we hit.
 */

static unsigned long serverworks_osb4_filter(struct ata_device *adev, unsigned long mask)
{
	if (adev->class == ATA_DEV_ATA)
		mask &= ~ATA_MASK_UDMA;
	return ata_bmdma_mode_filter(adev, mask);
}


/**
 *	serverworks_csb_filter	-	mode selection filter
 *	@adev: ATA device
 *	@mask: Mask of proposed modes
 *
 *	Check the blacklist and disable UDMA5 if matched
 */

static unsigned long serverworks_csb_filter(struct ata_device *adev, unsigned long mask)
{
	const char *p;
	char model_num[ATA_ID_PROD_LEN + 1];
	int i;

	/* Disk, UDMA */
	if (adev->class != ATA_DEV_ATA)
		return ata_bmdma_mode_filter(adev, mask);

	/* Actually do need to check */
	ata_id_c_string(adev->id, model_num, ATA_ID_PROD, sizeof(model_num));

	for (i = 0; (p = csb_bad_ata100[i]) != NULL; i++) {
		if (!strcmp(p, model_num))
			mask &= ~(0xE0 << ATA_SHIFT_UDMA);
	}
	return ata_bmdma_mode_filter(adev, mask);
}

/**
 *	serverworks_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program the OSB4/CSB5 timing registers for PIO. The PIO register
 *	load is done as a simple lookup.
 */
static void serverworks_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	static const u8 pio_mode[] = { 0x5d, 0x47, 0x34, 0x22, 0x20 };
	int offset = 1 + 2 * ap->port_no - adev->devno;
	int devbits = (2 * ap->port_no + adev->devno) * 4;
	u16 csb5_pio;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int pio = adev->pio_mode - XFER_PIO_0;

	pci_write_config_byte(pdev, 0x40 + offset, pio_mode[pio]);

	/* The OSB4 just requires the timing but the CSB series want the
	   mode number as well */
	if (serverworks_is_csb(pdev)) {
		pci_read_config_word(pdev, 0x4A, &csb5_pio);
		csb5_pio &= ~(0x0F << devbits);
		pci_write_config_byte(pdev, 0x4A, csb5_pio | (pio << devbits));
	}
}

/**
 *	serverworks_set_dmamode	-	set initial DMA mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program the MWDMA/UDMA modes for the serverworks OSB4/CSB5
 *	chipset. The MWDMA mode values are pulled from a lookup table
 *	while the chipset uses mode number for UDMA.
 */

static void serverworks_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	static const u8 dma_mode[] = { 0x77, 0x21, 0x20 };
	int offset = 1 + 2 * ap->port_no - adev->devno;
	int devbits = 2 * ap->port_no + adev->devno;
	u8 ultra;
	u8 ultra_cfg;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);

	pci_read_config_byte(pdev, 0x54, &ultra_cfg);
	pci_read_config_byte(pdev, 0x56 + ap->port_no, &ultra);
	ultra &= ~(0x0F << (adev->devno * 4));

	if (adev->dma_mode >= XFER_UDMA_0) {
		pci_write_config_byte(pdev, 0x44 + offset,  0x20);

		ultra |= (adev->dma_mode - XFER_UDMA_0)
					<< (adev->devno * 4);
		ultra_cfg |=  (1 << devbits);
	} else {
		pci_write_config_byte(pdev, 0x44 + offset,
			dma_mode[adev->dma_mode - XFER_MW_DMA_0]);
		ultra_cfg &= ~(1 << devbits);
	}
	pci_write_config_byte(pdev, 0x56 + ap->port_no, ultra);
	pci_write_config_byte(pdev, 0x54, ultra_cfg);
}

static struct scsi_host_template serverworks_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations serverworks_osb4_port_ops = {
	.inherits	= &ata_bmdma_port_ops,
	.cable_detect	= serverworks_cable_detect,
	.mode_filter	= serverworks_osb4_filter,
	.set_piomode	= serverworks_set_piomode,
	.set_dmamode	= serverworks_set_dmamode,
};

static struct ata_port_operations serverworks_csb_port_ops = {
	.inherits	= &serverworks_osb4_port_ops,
	.mode_filter	= serverworks_csb_filter,
};

static int serverworks_fixup_osb4(struct pci_dev *pdev)
{
	u32 reg;
	struct pci_dev *isa_dev = pci_get_device(PCI_VENDOR_ID_SERVERWORKS,
		  PCI_DEVICE_ID_SERVERWORKS_OSB4, NULL);
	if (isa_dev) {
		pci_read_config_dword(isa_dev, 0x64, &reg);
		reg &= ~0x00002000; /* disable 600ns interrupt mask */
		if (!(reg & 0x00004000))
			printk(KERN_DEBUG DRV_NAME ": UDMA not BIOS enabled.\n");
		reg |=  0x00004000; /* enable UDMA/33 support */
		pci_write_config_dword(isa_dev, 0x64, reg);
		pci_dev_put(isa_dev);
		return 0;
	}
	printk(KERN_WARNING "ata_serverworks: Unable to find bridge.\n");
	return -ENODEV;
}

static int serverworks_fixup_csb(struct pci_dev *pdev)
{
	u8 btr;

	/* Third Channel Test */
	if (!(PCI_FUNC(pdev->devfn) & 1)) {
		struct pci_dev * findev = NULL;
		u32 reg4c = 0;
		findev = pci_get_device(PCI_VENDOR_ID_SERVERWORKS,
			PCI_DEVICE_ID_SERVERWORKS_CSB5, NULL);
		if (findev) {
			pci_read_config_dword(findev, 0x4C, &reg4c);
			reg4c &= ~0x000007FF;
			reg4c |=  0x00000040;
			reg4c |=  0x00000020;
			pci_write_config_dword(findev, 0x4C, reg4c);
			pci_dev_put(findev);
		}
	} else {
		struct pci_dev * findev = NULL;
		u8 reg41 = 0;

		findev = pci_get_device(PCI_VENDOR_ID_SERVERWORKS,
				PCI_DEVICE_ID_SERVERWORKS_CSB6, NULL);
		if (findev) {
			pci_read_config_byte(findev, 0x41, &reg41);
			reg41 &= ~0x40;
			pci_write_config_byte(findev, 0x41, reg41);
			pci_dev_put(findev);
		}
	}
	/* setup the UDMA Control register
	 *
	 * 1. clear bit 6 to enable DMA
	 * 2. enable DMA modes with bits 0-1
	 * 	00 : legacy
	 * 	01 : udma2
	 * 	10 : udma2/udma4
	 * 	11 : udma2/udma4/udma5
	 */
	pci_read_config_byte(pdev, 0x5A, &btr);
	btr &= ~0x40;
	if (!(PCI_FUNC(pdev->devfn) & 1))
		btr |= 0x2;
	else
		btr |= (pdev->revision >= SVWKS_CSB5_REVISION_NEW) ? 0x3 : 0x2;
	pci_write_config_byte(pdev, 0x5A, btr);

	return btr;
}

static void serverworks_fixup_ht1000(struct pci_dev *pdev)
{
	u8 btr;
	/* Setup HT1000 SouthBridge Controller - Single Channel Only */
	pci_read_config_byte(pdev, 0x5A, &btr);
	btr &= ~0x40;
	btr |= 0x3;
	pci_write_config_byte(pdev, 0x5A, btr);
}


static int serverworks_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct ata_port_info info[4] = {
		{ /* OSB4 */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x07,
			.port_ops = &serverworks_osb4_port_ops
		}, { /* OSB4 no UDMA */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = 0x00,
			.port_ops = &serverworks_osb4_port_ops
		}, { /* CSB5 */
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = ATA_UDMA4,
			.port_ops = &serverworks_csb_port_ops
		}, { /* CSB5 - later revisions*/
			.flags = ATA_FLAG_SLAVE_POSS,
			.pio_mask = 0x1f,
			.mwdma_mask = 0x07,
			.udma_mask = ATA_UDMA5,
			.port_ops = &serverworks_csb_port_ops
		}
	};
	const struct ata_port_info *ppi[] = { &info[id->driver_data], NULL };
	int rc;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	/* Force master latency timer to 64 PCI clocks */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x40);

	/* OSB4 : South Bridge and IDE */
	if (pdev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4IDE) {
		/* Select non UDMA capable OSB4 if we can't do fixups */
		if ( serverworks_fixup_osb4(pdev) < 0)
			ppi[0] = &info[1];
	}
	/* setup CSB5/CSB6 : South Bridge and IDE option RAID */
	else if ((pdev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE) ||
		 (pdev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE) ||
		 (pdev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2)) {

		 /* If the returned btr is the newer revision then
		    select the right info block */
		 if (serverworks_fixup_csb(pdev) == 3)
		 	ppi[0] = &info[3];

		/* Is this the 3rd channel CSB6 IDE ? */
		if (pdev->device == PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2)
			ppi[1] = &ata_dummy_port_info;
	}
	/* setup HT1000E */
	else if (pdev->device == PCI_DEVICE_ID_SERVERWORKS_HT1000IDE)
		serverworks_fixup_ht1000(pdev);

	if (pdev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE)
		ata_pci_bmdma_clear_simplex(pdev);

	return ata_pci_sff_init_one(pdev, ppi, &serverworks_sht, NULL);
}

#ifdef CONFIG_PM
static int serverworks_reinit_one(struct pci_dev *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int rc;

	rc = ata_pci_device_do_resume(pdev);
	if (rc)
		return rc;

	/* Force master latency timer to 64 PCI clocks */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x40);

	switch (pdev->device) {
		case PCI_DEVICE_ID_SERVERWORKS_OSB4IDE:
			serverworks_fixup_osb4(pdev);
			break;
		case PCI_DEVICE_ID_SERVERWORKS_CSB5IDE:
			ata_pci_bmdma_clear_simplex(pdev);
			/* fall through */
		case PCI_DEVICE_ID_SERVERWORKS_CSB6IDE:
		case PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2:
			serverworks_fixup_csb(pdev);
			break;
		case PCI_DEVICE_ID_SERVERWORKS_HT1000IDE:
			serverworks_fixup_ht1000(pdev);
			break;
	}

	ata_host_resume(host);
	return 0;
}
#endif

static const struct pci_device_id serverworks[] = {
	{ PCI_VDEVICE(SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_OSB4IDE), 0},
	{ PCI_VDEVICE(SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB5IDE), 2},
	{ PCI_VDEVICE(SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB6IDE), 2},
	{ PCI_VDEVICE(SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB6IDE2), 2},
	{ PCI_VDEVICE(SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_HT1000IDE), 2},

	{ },
};

static struct pci_driver serverworks_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= serverworks,
	.probe 		= serverworks_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= serverworks_reinit_one,
#endif
};

static int __init serverworks_init(void)
{
	return pci_register_driver(&serverworks_pci_driver);
}

static void __exit serverworks_exit(void)
{
	pci_unregister_driver(&serverworks_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Serverworks OSB4/CSB5/CSB6");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, serverworks);
MODULE_VERSION(DRV_VERSION);

module_init(serverworks_init);
module_exit(serverworks_exit);
