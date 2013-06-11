/*
 * comedi/drivers/ni_labpc_pci.c
 * Driver for National Instruments Lab-PC PCI-1200
 * Copyright (C) 2001, 2002, 2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Driver: ni_labpc_pci
 * Description: National Instruments Lab-PC PCI-1200
 * Devices: (National Instruments) PCI-1200 [ni_pci-1200]
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Status: works
 *
 * This is the PCI-specific support split off from the ni_labpc driver.
 *
 * Configuration Options: not applicable, uses PCI auto config
 *
 * NI manuals:
 * 340914a (pci-1200)
 */

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/pci.h>

#include "../comedidev.h"

#include "mite.h"
#include "ni_labpc.h"

enum labpc_pci_boardid {
	BOARD_NI_PCI1200,
};

static const struct labpc_boardinfo labpc_pci_boards[] = {
	[BOARD_NI_PCI1200] = {
		.name			= "ni_pci-1200",
		.ai_speed		= 10000,
		.ai_scan_up		= 1,
		.has_ao			= 1,
		.is_labpc1200		= 1,
		.has_mmio		= 1,
	},
};

static int labpc_pci_auto_attach(struct comedi_device *dev,
				 unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct labpc_boardinfo *board = NULL;
	struct labpc_private *devpriv;
	int ret;

	if (context < ARRAY_SIZE(labpc_pci_boards))
		board = &labpc_pci_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	devpriv->mite = mite_alloc(pcidev);
	if (!devpriv->mite)
		return -ENOMEM;
	ret = mite_setup(devpriv->mite);
	if (ret < 0)
		return ret;
	dev->iobase = (unsigned long)devpriv->mite->daq_io_addr;

	return labpc_common_attach(dev, mite_irq(devpriv->mite), IRQF_SHARED);
}

static void labpc_pci_detach(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;

	if (devpriv && devpriv->mite) {
		mite_unsetup(devpriv->mite);
		mite_free(devpriv->mite);
	}
	if (dev->irq)
		free_irq(dev->irq, dev);
	comedi_pci_disable(dev);
}

static struct comedi_driver labpc_pci_comedi_driver = {
	.driver_name	= "labpc_pci",
	.module		= THIS_MODULE,
	.auto_attach	= labpc_pci_auto_attach,
	.detach		= labpc_pci_detach,
};

static DEFINE_PCI_DEVICE_TABLE(labpc_pci_table) = {
	{ PCI_VDEVICE(NI, 0x161), BOARD_NI_PCI1200 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, labpc_pci_table);

static int labpc_pci_probe(struct pci_dev *dev,
			   const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &labpc_pci_comedi_driver,
				      id->driver_data);
}

static struct pci_driver labpc_pci_driver = {
	.name		= "labpc_pci",
	.id_table	= labpc_pci_table,
	.probe		= labpc_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(labpc_pci_comedi_driver, labpc_pci_driver);

MODULE_DESCRIPTION("Comedi: National Instruments Lab-PC PCI-1200 driver");
MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_LICENSE("GPL");
