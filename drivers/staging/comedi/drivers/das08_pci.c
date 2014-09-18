/*
 *  das08_pci.c
 *  comedi driver for DAS08 PCI boards
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 *  Copyright (C) 2001,2002,2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 *  Copyright (C) 2004 Salvador E. Tropea <set@users.sf.net> <set@ieee.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * Driver: das08_pci
 * Description: DAS-08 PCI compatible boards
 * Devices: (ComputerBoards) PCI-DAS08 [pci-das08]
 * Author: Warren Jasper, ds, Frank Hess
 * Updated: Fri, 31 Aug 2012 19:19:06 +0100
 * Status: works
 *
 * This is the PCI-specific support split off from the das08 driver.
 *
 * Configuration Options: not applicable, uses PCI auto config
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"

#include "das08.h"

static const struct das08_board_struct das08_pci_boards[] = {
	{
		.name		= "pci-das08",
		.ai_nbits	= 12,
		.ai_pg		= das08_bipolar5,
		.ai_encoding	= das08_encode12,
		.di_nchan	= 3,
		.do_nchan	= 4,
		.i8254_offset	= 4,
		.iosize		= 8,
	},
};

static int das08_pci_auto_attach(struct comedi_device *dev,
				 unsigned long context_unused)
{
	struct pci_dev *pdev = comedi_to_pci_dev(dev);
	struct das08_private_struct *devpriv;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	/* The das08 driver needs the board_ptr */
	dev->board_ptr = &das08_pci_boards[0];

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pdev, 2);

	return das08_common_attach(dev, dev->iobase);
}

static struct comedi_driver das08_pci_comedi_driver = {
	.driver_name	= "pci-das08",
	.module		= THIS_MODULE,
	.auto_attach	= das08_pci_auto_attach,
	.detach		= comedi_pci_disable,
};

static int das08_pci_probe(struct pci_dev *dev,
			   const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &das08_pci_comedi_driver,
				      id->driver_data);
}

static const struct pci_device_id das08_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0029) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, das08_pci_table);

static struct pci_driver das08_pci_driver = {
	.name		= "pci-das08",
	.id_table	= das08_pci_table,
	.probe		= das08_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(das08_pci_comedi_driver, das08_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
