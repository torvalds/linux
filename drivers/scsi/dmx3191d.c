// SPDX-License-Identifier: GPL-2.0-or-later
/*
    dmx3191d.c - driver for the Domex DMX3191D SCSI card.
    Copyright (C) 2000 by Massimo Piccioni <dafastidio@libero.it>
    Portions Copyright (C) 2004 by Christoph Hellwig <hch@lst.de>

    Based on the generic NCR5380 driver by Drew Eckhardt et al.

*/

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#include <scsi/scsi_host.h>

/*
 * Definitions for the generic 5380 driver.
 */

#define NCR5380_read(reg)		inb(hostdata->base + (reg))
#define NCR5380_write(reg, value)	outb(value, hostdata->base + (reg))

#define NCR5380_dma_xfer_len		NCR5380_dma_xfer_none
#define NCR5380_dma_recv_setup		NCR5380_dma_setup_none
#define NCR5380_dma_send_setup		NCR5380_dma_setup_none
#define NCR5380_dma_residual		NCR5380_dma_residual_none

#define NCR5380_implementation_fields	/* none */

#include "NCR5380.h"
#include "NCR5380.c"

#define DMX3191D_DRIVER_NAME	"dmx3191d"
#define DMX3191D_REGION_LEN	8


static struct scsi_host_template dmx3191d_driver_template = {
	.module			= THIS_MODULE,
	.proc_name		= DMX3191D_DRIVER_NAME,
	.name			= "Domex DMX3191D",
	.info			= NCR5380_info,
	.queuecommand		= NCR5380_queue_command,
	.eh_abort_handler	= NCR5380_abort,
	.eh_host_reset_handler	= NCR5380_host_reset,
	.can_queue		= 32,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 2,
	.dma_boundary		= PAGE_SIZE - 1,
	.cmd_size		= NCR5380_CMD_SIZE,
};

static int dmx3191d_probe_one(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct Scsi_Host *shost;
	struct NCR5380_hostdata *hostdata;
	unsigned long io;
	int error = -ENODEV;

	if (pci_enable_device(pdev))
		goto out;

	io = pci_resource_start(pdev, 0);
	if (!request_region(io, DMX3191D_REGION_LEN, DMX3191D_DRIVER_NAME)) {
		printk(KERN_ERR "dmx3191: region 0x%lx-0x%lx already reserved\n",
				io, io + DMX3191D_REGION_LEN);
		goto out_disable_device;
	}

	shost = scsi_host_alloc(&dmx3191d_driver_template,
			sizeof(struct NCR5380_hostdata));
	if (!shost)
		goto out_release_region;       

	hostdata = shost_priv(shost);
	hostdata->base = io;

	/* This card does not seem to raise an interrupt on pdev->irq.
	 * Steam-powered SCSI controllers run without an IRQ anyway.
	 */
	shost->irq = NO_IRQ;

	error = NCR5380_init(shost, 0);
	if (error)
		goto out_host_put;

	NCR5380_maybe_reset_bus(shost);

	pci_set_drvdata(pdev, shost);

	error = scsi_add_host(shost, &pdev->dev);
	if (error)
		goto out_exit;

	scsi_scan_host(shost);
	return 0;

out_exit:
	NCR5380_exit(shost);
out_host_put:
	scsi_host_put(shost);
 out_release_region:
	release_region(io, DMX3191D_REGION_LEN);
 out_disable_device:
	pci_disable_device(pdev);
 out:
	return error;
}

static void dmx3191d_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct NCR5380_hostdata *hostdata = shost_priv(shost);
	unsigned long io = hostdata->base;

	scsi_remove_host(shost);

	NCR5380_exit(shost);
	scsi_host_put(shost);
	release_region(io, DMX3191D_REGION_LEN);
	pci_disable_device(pdev);
}

static struct pci_device_id dmx3191d_pci_tbl[] = {
	{PCI_VENDOR_ID_DOMEX, PCI_DEVICE_ID_DOMEX_DMX3191D,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ }
};
MODULE_DEVICE_TABLE(pci, dmx3191d_pci_tbl);

static struct pci_driver dmx3191d_pci_driver = {
	.name		= DMX3191D_DRIVER_NAME,
	.id_table	= dmx3191d_pci_tbl,
	.probe		= dmx3191d_probe_one,
	.remove		= dmx3191d_remove_one,
};

module_pci_driver(dmx3191d_pci_driver);

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("Domex DMX3191D SCSI driver");
MODULE_LICENSE("GPL");
