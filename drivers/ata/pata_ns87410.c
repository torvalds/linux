/*
 * pata_ns87410.c 	- National Semiconductor 87410 PATA for new ATA layer
 *			  (C) 2006 Red Hat Inc
 *			  Alan Cox <alan@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME "pata_ns87410"
#define DRV_VERSION "0.4.6"

/**
 *	ns87410_pre_reset		-	probe begin
 *	@link: ATA link
 *	@deadline: deadline jiffies for the operation
 *
 *	Check enabled ports
 */

static int ns87410_pre_reset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	static const struct pci_bits ns87410_enable_bits[] = {
		{ 0x43, 1, 0x08, 0x08 },
		{ 0x47, 1, 0x08, 0x08 }
	};

	if (!pci_test_config_bits(pdev, &ns87410_enable_bits[ap->port_no]))
		return -ENOENT;

	return ata_sff_prereset(link, deadline);
}

/**
 *	ns87410_set_piomode	-	set initial PIO mode data
 *	@ap: ATA interface
 *	@adev: ATA device
 *
 *	Program timing data. This is kept per channel not per device,
 *	and only affects the data port.
 */

static void ns87410_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	int port = 0x40 + 4 * ap->port_no;
	u8 idetcr, idefr;
	struct ata_timing at;

	static const u8 activebits[15] = {
		0, 1, 2, 3, 4,
		5, 5, 6, 6, 6,
		6, 7, 7, 7, 7
	};

	static const u8 recoverbits[12] = {
		0, 1, 2, 3, 4, 5, 6, 6, 7, 7, 7, 7
	};

	pci_read_config_byte(pdev, port + 3, &idefr);

	if (ata_pio_need_iordy(adev))
		idefr |= 0x04;	/* IORDY enable */
	else
		idefr &= ~0x04;

	if (ata_timing_compute(adev, adev->pio_mode, &at, 30303, 1) < 0) {
		dev_printk(KERN_ERR, &pdev->dev, "unknown mode %d.\n", adev->pio_mode);
		return;
	}

	at.active = FIT(at.active, 2, 16) - 2;
	at.setup = FIT(at.setup, 1, 4) - 1;
	at.recover = FIT(at.recover, 1, 12) - 1;

	idetcr = (at.setup << 6) | (recoverbits[at.recover] << 3) | activebits[at.active];

	pci_write_config_byte(pdev, port, idetcr);
	pci_write_config_byte(pdev, port + 3, idefr);
	/* We use ap->private_data as a pointer to the device currently
	   loaded for timing */
	ap->private_data = adev;
}

/**
 *	ns87410_qc_issue	-	command issue
 *	@qc: command pending
 *
 *	Called when the libata layer is about to issue a command. We wrap
 *	this interface so that we can load the correct ATA timings if
 *	necessary.
 */

static unsigned int ns87410_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;

	/* If modes have been configured and the channel data is not loaded
	   then load it. We have to check if pio_mode is set as the core code
	   does not set adev->pio_mode to XFER_PIO_0 while probing as would be
	   logical */

	if (adev->pio_mode && adev != ap->private_data)
		ns87410_set_piomode(ap, adev);

	return ata_sff_qc_issue(qc);
}

static struct scsi_host_template ns87410_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations ns87410_port_ops = {
	.inherits	= &ata_sff_port_ops,
	.qc_issue	= ns87410_qc_issue,
	.cable_detect	= ata_cable_40wire,
	.set_piomode	= ns87410_set_piomode,
	.prereset	= ns87410_pre_reset,
};

static int ns87410_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static const struct ata_port_info info = {
		.flags = ATA_FLAG_SLAVE_POSS,
		.pio_mask = 0x0F,
		.port_ops = &ns87410_port_ops
	};
	const struct ata_port_info *ppi[] = { &info, NULL };
	return ata_pci_sff_init_one(dev, ppi, &ns87410_sht, NULL);
}

static const struct pci_device_id ns87410[] = {
	{ PCI_VDEVICE(NS, PCI_DEVICE_ID_NS_87410), },

	{ },
};

static struct pci_driver ns87410_pci_driver = {
	.name 		= DRV_NAME,
	.id_table	= ns87410,
	.probe 		= ns87410_init_one,
	.remove		= ata_pci_remove_one,
#ifdef CONFIG_PM
	.suspend	= ata_pci_device_suspend,
	.resume		= ata_pci_device_resume,
#endif
};

static int __init ns87410_init(void)
{
	return pci_register_driver(&ns87410_pci_driver);
}

static void __exit ns87410_exit(void)
{
	pci_unregister_driver(&ns87410_pci_driver);
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Nat Semi 87410");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, ns87410);
MODULE_VERSION(DRV_VERSION);

module_init(ns87410_init);
module_exit(ns87410_exit);
