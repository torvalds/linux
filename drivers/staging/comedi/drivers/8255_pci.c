/*
 * COMEDI driver for the ADLINK PCI-72xx series boards.
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
Driver: adl_pci7296
Description: 24/48/96-Channel Opto-22 Compatible Digital I/O Boards
Devices: (ADLink) PCI-7224 [adl_pci7224] - 24 channels
	 (ADLink) PCI-7248 [adl_pci7248] - 48 channels
	 (ADLink) PCI-7296 [adl_pci7296] - 96 channels
Author: Jon Grierson <jd@renko.co.uk>
Updated: Mon, 14 Apr 2008 15:05:56 +0100
Status: testing

This driver only attaches using the PCI PnP auto config support
in the comedi core. The module parameter 'comedi_autoconfig'
must be 1 (default) to enable this feature. The COMEDI_DEVCONFIG
ioctl, used by the comedi_config utility, is not supported by
this driver.

These boards also have an 8254 programmable timer/counter chip.
This chip is not currently supported by this driver.

Interrupt support for these boards is also not currently supported.

Configuration Options: not applicable
*/

#include "../comedidev.h"

#include "8255.h"

/*
 * PCI Device ID's supported by this driver
 */
#define PCI_DEVICE_ID_PCI7224	0x7224
#define PCI_DEVICE_ID_PCI7248	0x7248
#define PCI_DEVICE_ID_PCI7296	0x7296

struct adl_pci7296_boardinfo {
	const char *name;
	unsigned short device;
	int nsubdevs;
};

static const struct adl_pci7296_boardinfo adl_pci7296_boards[] = {
	{
		.name		= "adl_pci7224",
		.device		= PCI_DEVICE_ID_PCI7224,
		.nsubdevs	= 1,
	}, {
		.name		= "adl_pci7248",
		.device		= PCI_DEVICE_ID_PCI7248,
		.nsubdevs	= 2,
	}, {
		.name		= "adl_pci7296",
		.device		= PCI_DEVICE_ID_PCI7296,
		.nsubdevs	= 4,
	},
};

static const void *adl_pci7296_find_boardinfo(struct comedi_device *dev,
					      struct pci_dev *pcidev)
{
	const struct adl_pci7296_boardinfo *board;
	int i;

	for (i = 0; i < ARRAY_SIZE(adl_pci7296_boards); i++) {
		board = &adl_pci7296_boards[i];
		if (pcidev->device == board->device)
			return board;
	}
	return NULL;
}

static int adl_pci7296_attach_pci(struct comedi_device *dev,
				  struct pci_dev *pcidev)
{
	const struct adl_pci7296_boardinfo *board;
	struct comedi_subdevice *s;
	int ret;
	int i;

	comedi_set_hw_dev(dev, &pcidev->dev);

	board = adl_pci7296_find_boardinfo(dev, pcidev);
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 2);

	/*
	 * One, two, or four subdevices are setup by this driver depending
	 * on the number of channels provided by the board. Each subdevice
	 * has 24 channels supported by the 8255 module.
	 */
	ret = comedi_alloc_subdevices(dev, board->nsubdevs);
	if (ret)
		return ret;

	for (i = 0; i < board->nsubdevs; i++) {
		s = &dev->subdevices[i];
		ret = subdev_8255_init(dev, s, NULL, dev->iobase + (i * 4));
		if (ret)
			return ret;
	}

	dev_info(dev->class_dev, "%s attached (%d digital i/o channels)\n",
		dev->board_name, board->nsubdevs * 24);

	return 0;
}

static void adl_pci7296_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct adl_pci7296_boardinfo *board = comedi_board(dev);
	struct comedi_subdevice *s;
	int i;

	if (dev->subdevices) {
		for (i = 0; i < board->nsubdevs; i++) {
			s = &dev->subdevices[i];
			subdev_8255_cleanup(dev, s);
		}
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver adl_pci7296_driver = {
	.driver_name	= "adl_pci7296",
	.module		= THIS_MODULE,
	.attach_pci	= adl_pci7296_attach_pci,
	.detach		= adl_pci7296_detach,
};

static int __devinit adl_pci7296_pci_probe(struct pci_dev *dev,
					   const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &adl_pci7296_driver);
}

static void __devexit adl_pci7296_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(adl_pci7296_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI_DEVICE_ID_PCI7224) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI_DEVICE_ID_PCI7248) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI_DEVICE_ID_PCI7296) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adl_pci7296_pci_table);

static struct pci_driver adl_pci7296_pci_driver = {
	.name		= "adl_pci7296",
	.id_table	= adl_pci7296_pci_table,
	.probe		= adl_pci7296_pci_probe,
	.remove		= __devexit_p(adl_pci7296_pci_remove),
};
module_comedi_pci_driver(adl_pci7296_driver, adl_pci7296_pci_driver);

MODULE_DESCRIPTION("ADLINK PCI-72xx Opto-22 Compatible Digital I/O Boards");
MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_LICENSE("GPL");
