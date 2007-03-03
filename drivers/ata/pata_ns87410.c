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
#define DRV_VERSION "0.4.3"

/**
 *	ns87410_pre_reset		-	probe begin
 *	@ap: ATA port
 *
 *	Set up cable type and use generic probe init
 */

static int ns87410_pre_reset(struct ata_port *ap)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	static const struct pci_bits ns87410_enable_bits[] = {
		{ 0x43, 1, 0x08, 0x08 },
		{ 0x47, 1, 0x08, 0x08 }
	};

	if (!pci_test_config_bits(pdev, &ns87410_enable_bits[ap->port_no]))
		return -ENOENT;
	ap->cbl = ATA_CBL_PATA40;
	return ata_std_prereset(ap);
}

/**
 *	ns87410_error_handler		-	probe reset
 *	@ap: ATA port
 *
 *	Perform the ATA probe and bus reset sequence plus specific handling
 *	for this hardware. The MPIIX has the enable bits in a different place
 *	to PIIX4 and friends. As a pure PIO device it has no cable detect
 */

static void ns87410_error_handler(struct ata_port *ap)
{
	ata_bmdma_drive_eh(ap, ns87410_pre_reset, ata_std_softreset, NULL, ata_std_postreset);
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
 *	ns87410_qc_issue_prot	-	command issue
 *	@qc: command pending
 *
 *	Called when the libata layer is about to issue a command. We wrap
 *	this interface so that we can load the correct ATA timings if
 *	neccessary.
 */

static unsigned int ns87410_qc_issue_prot(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;

	/* If modes have been configured and the channel data is not loaded
	   then load it. We have to check if pio_mode is set as the core code
	   does not set adev->pio_mode to XFER_PIO_0 while probing as would be
	   logical */

	if (adev->pio_mode && adev != ap->private_data)
		ns87410_set_piomode(ap, adev);

	return ata_qc_issue_prot(qc);
}

static struct scsi_host_template ns87410_sht = {
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
	.bios_param		= ata_std_bios_param,
#ifdef CONFIG_PM
	.resume			= ata_scsi_device_resume,
	.suspend		= ata_scsi_device_suspend,
#endif
};

static struct ata_port_operations ns87410_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= ns87410_set_piomode,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ns87410_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= ns87410_qc_issue_prot,

	.data_xfer	= ata_data_xfer,

	.irq_handler	= ata_interrupt,
	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

static int ns87410_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	static struct ata_port_info info = {
		.sht = &ns87410_sht,
		.flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_SRST,
		.pio_mask = 0x0F,
		.port_ops = &ns87410_port_ops
	};
	static struct ata_port_info *port_info[2] = {&info, &info};
	return ata_pci_init_one(dev, port_info, 2);
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
