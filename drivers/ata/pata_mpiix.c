// SPDX-License-Identifier: GPL-2.0-only
/*
 * pata_mpiix.c 	- Intel MPIIX PATA for new ATA layer
 *			  (C) 2005-2006 Red Hat Inc
 *			  Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * The MPIIX is different enough to the PIIX4 and friends that we give it
 * a separate driver. The old ide/pci code handles this by just not tuning
 * MPIIX at all.
 *
 * The MPIIX also differs in another important way from the majority of PIIX
 * devices. The chip is a bridge (pardon the pun) between the old world of
 * ISA IDE and PCI IDE. Although the ATA timings are PCI configured the actual
 * IDE controller is not decoded in PCI space and the chip does not claim to
 * be IDE class PCI. This requires slightly non-standard probe logic compared
 * with PCI IDE and also that we do not disable the device when our driver is
 * unloaded (as it has many other functions).
 *
 * The driver consciously keeps this logic internally to avoid pushing quirky
 * PATA history into the clean libata layer.
 *
 * Thinkpad specific note: If you boot an MPIIX using a thinkpad with a PCMCIA
 * hard disk present this driver will not detect it. This is not a bug. In this
 * configuration the secondary port of the MPIIX is disabled and the addresses
 * are decoded by the PCMCIA bridge and therefore are for a generic IDE driver
 * to operate.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_mpiix"
#define DRV_VERSION "0.7.7"

enum {
	IDETIM = 0x6C,		/* IDE control register */
	IORDY = (1 << 1),
	PPE = (1 << 2),
	FTIM = (1 << 0),
	ENABLED = (1 << 15),
	SECONDARY = (1 << 14)
};

static int mpiix_pre_reset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	static const struct pci_bits mpiix_enable_bits = { 0x6D, 1, 0x80, 0x80 };

	if (!pci_test_config_bits(pdev, &mpiix_enable_bits))
		return -ENOENT;

	return ata_sff_prereset(link, deadline);
}

/**
 *	mpiix_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Called to do the PIO mode setup. The MPIIX allows us to program the
 *	IORDY sample point (2-5 clocks), recovery (1-4 clocks) and whether
 *	prefetching or IORDY are used.
 *
 *	This would get very ugly because we can only program timing for one
 *	device at a time, the other gets PIO0. Fortunately libata calls
 *	our qc_issue command before a command is issued so we can flip the
 *	timings back and forth to reduce the pain.
 */

static void mpiix_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	int control = 0;
	int pio = adev->pio_mode - XFER_PIO_0;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	u16 idetim;
	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	pci_read_config_word(pdev, IDETIM, &idetim);

	/* Mask the IORDY/TIME/PPE for this device */
	if (adev->class == ATA_DEV_ATA)
		control |= PPE;		/* Enable prefetch/posting for disk */
	if (ata_pio_need_iordy(adev))
		control |= IORDY;
	if (pio > 1)
		control |= FTIM;	/* This drive is on the fast timing bank */

	/* Mask out timing and clear both TIME bank selects */
	idetim &= 0xCCEE;
	idetim &= ~(0x07  << (4 * adev->devno));
	idetim |= control << (4 * adev->devno);

	idetim |= (timings[pio][0] << 12) | (timings[pio][1] << 8);
	pci_write_config_word(pdev, IDETIM, idetim);

	/* We use ap->private_data as a pointer to the device currently
	   loaded for timing */
	ap->private_data = adev;
}

/**
 *	mpiix_qc_issue		-	command issue
 *	@qc: command pending
 *
 *	Called when the libata layer is about to issue a command. We wrap
 *	this interface so that we can load the correct ATA timings if
 *	necessary. Our logic also clears TIME0/TIME1 for the other device so
 *	that, even if we get this wrong, cycles to the other device will
 *	be made PIO0.
 */

static unsigned int mpiix_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;

	/* If modes have been configured and the channel data is not loaded
	   then load it. We have to check if pio_mode is set as the core code
	   does not set adev->pio_mode to XFER_PIO_0 while probing as would be
	   logical */

	if (adev->pio_mode && adev != ap->private_data)
		mpiix_set_piomode(ap, adev);

	return ata_sff_qc_issue(qc);
}

static struct scsi_host_template mpiix_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations mpiix_port_ops = {
	.inherits	= &ata_sff_port_ops,
	.qc_issue	= mpiix_qc_issue,
	.cable_detect	= ata_cable_40wire,
	.set_piomode	= mpiix_set_piomode,
	.prereset	= mpiix_pre_reset,
	.sff_data_xfer	= ata_sff_data_xfer32,
};

static int mpiix_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	/* Single threaded by the PCI probe logic */
	struct ata_host *host;
	struct ata_port *ap;
	void __iomem *cmd_addr, *ctl_addr;
	u16 idetim;
	int cmd, ctl, irq;

	ata_print_version_once(&dev->dev, DRV_VERSION);

	host = ata_host_alloc(&dev->dev, 1);
	if (!host)
		return -ENOMEM;
	ap = host->ports[0];

	/* MPIIX has many functions which can be turned on or off according
	   to other devices present. Make sure IDE is enabled before we try
	   and use it */

	pci_read_config_word(dev, IDETIM, &idetim);
	if (!(idetim & ENABLED))
		return -ENODEV;

	/* See if it's primary or secondary channel... */
	if (!(idetim & SECONDARY)) {
		cmd = 0x1F0;
		ctl = 0x3F6;
		irq = 14;
	} else {
		cmd = 0x170;
		ctl = 0x376;
		irq = 15;
	}

	cmd_addr = devm_ioport_map(&dev->dev, cmd, 8);
	ctl_addr = devm_ioport_map(&dev->dev, ctl, 1);
	if (!cmd_addr || !ctl_addr)
		return -ENOMEM;

	ata_port_desc(ap, "cmd 0x%x ctl 0x%x", cmd, ctl);

	/* We do our own plumbing to avoid leaking special cases for whacko
	   ancient hardware into the core code. There are two issues to
	   worry about.  #1 The chip is a bridge so if in legacy mode and
	   without BARs set fools the setup.  #2 If you pci_disable_device
	   the MPIIX your box goes castors up */

	ap->ops = &mpiix_port_ops;
	ap->pio_mask = ATA_PIO4;
	ap->flags |= ATA_FLAG_SLAVE_POSS;

	ap->ioaddr.cmd_addr = cmd_addr;
	ap->ioaddr.ctl_addr = ctl_addr;
	ap->ioaddr.altstatus_addr = ctl_addr;

	/* Let libata fill in the port details */
	ata_sff_std_ports(&ap->ioaddr);

	/* activate host */
	return ata_host_activate(host, irq, ata_sff_interrupt, IRQF_SHARED,
				 &mpiix_sht);
}

static const struct pci_device_id mpiix[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_82371MX), },

	{ },
};

static struct pci_driver mpiix_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= mpiix,
	.probe 		= mpiix_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM_SLEEP
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

module_pci_driver(mpiix_pci_driver);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Intel MPIIX");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, mpiix);
MODULE_VERSION(DRV_VERSION);
