/*
    dmx3191d.c - driver for the Domex DMX3191D SCSI card.
    Copyright (C) 2000 by Massimo Piccioni <dafastidio@libero.it>
    Portions Copyright (C) 2004 by Christoph Hellwig <hch@lst.de>

    Based on the generic NCR5380 driver by Drew Eckhardt et al.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
 * Defintions for the generic 5380 driver.
 */
#define AUTOSENSE

#define NCR5380_read(reg)		inb(port + reg)
#define NCR5380_write(reg, value)	outb(value, port + reg)

#define NCR5380_implementation_fields	unsigned int port
#define NCR5380_local_declare()		NCR5380_implementation_fields
#define NCR5380_setup(instance)		port = instance->io_port

/*
 * Includes needed for NCR5380.[ch] (XXX: Move them to NCR5380.h)
 */
#include <linux/delay.h>
#include "scsi.h"

#include "NCR5380.h"
#include "NCR5380.c"

#define DMX3191D_DRIVER_NAME	"dmx3191d"
#define DMX3191D_REGION_LEN	8


static struct scsi_host_template dmx3191d_driver_template = {
	.proc_name		= DMX3191D_DRIVER_NAME,
	.name			= "Domex DMX3191D",
	.queuecommand		= NCR5380_queue_command,
	.eh_abort_handler	= NCR5380_abort,
	.eh_bus_reset_handler	= NCR5380_bus_reset,
	.eh_device_reset_handler= NCR5380_device_reset,
	.eh_host_reset_handler	= NCR5380_host_reset,
	.can_queue		= 32,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 2,
	.use_clustering		= DISABLE_CLUSTERING,
};

static int __devinit dmx3191d_probe_one(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	struct Scsi_Host *shost;
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
	shost->io_port = io;
	shost->irq = pdev->irq;

	NCR5380_init(shost, FLAG_NO_PSEUDO_DMA | FLAG_DTC3181E);

	if (request_irq(pdev->irq, NCR5380_intr, SA_SHIRQ,
				DMX3191D_DRIVER_NAME, shost)) {
		/*
		 * Steam powered scsi controllers run without an IRQ anyway
		 */
		printk(KERN_WARNING "dmx3191: IRQ %d not available - "
				    "switching to polled mode.\n", pdev->irq);
		shost->irq = SCSI_IRQ_NONE;
	}

	pci_set_drvdata(pdev, shost);

	error = scsi_add_host(shost, &pdev->dev);
	if (error)
		goto out_free_irq;

	scsi_scan_host(shost);
	return 0;

 out_free_irq:
	free_irq(shost->irq, shost);
 out_release_region:
	release_region(shost->io_port, DMX3191D_REGION_LEN);
 out_disable_device:
	pci_disable_device(pdev);
 out:
	return error;
}

static void __devexit dmx3191d_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);

	scsi_remove_host(shost);

	NCR5380_exit(shost);

	if (shost->irq != SCSI_IRQ_NONE)
		free_irq(shost->irq, shost);
	release_region(shost->io_port, DMX3191D_REGION_LEN);
	pci_disable_device(pdev);

	scsi_host_put(shost);
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
	.remove		= __devexit_p(dmx3191d_remove_one),
};

static int __init dmx3191d_init(void)
{
	return pci_module_init(&dmx3191d_pci_driver);
}

static void __exit dmx3191d_exit(void)
{
	pci_unregister_driver(&dmx3191d_pci_driver);
}

module_init(dmx3191d_init);
module_exit(dmx3191d_exit);

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("Domex DMX3191D SCSI driver");
MODULE_LICENSE("GPL");
