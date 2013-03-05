/*
 * COMEDI driver for generic PCI based 8255 digital i/o boards
 * Copyright (C) 2012 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on the tested adl_pci7296 driver written by:
 *	Jon Grierson <jd@renko.co.uk>
 * and the experimental cb_pcidio driver written by:
 *	Yoshiya Matsuzaka
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
Driver: 8255_pci
Description: Generic PCI based 8255 Digital I/O boards
Devices: (ADLink) PCI-7224 [adl_pci-7224] - 24 channels
	 (ADLink) PCI-7248 [adl_pci-7248] - 48 channels
	 (ADLink) PCI-7296 [adl_pci-7296] - 96 channels
	 (Measurement Computing) PCI-DIO24 [cb_pci-dio24] - 24 channels
	 (Measurement Computing) PCI-DIO24H [cb_pci-dio24h] - 24 channels
	 (Measurement Computing) PCI-DIO48H [cb_pci-dio48h] - 48 channels
	 (Measurement Computing) PCI-DIO96H [cb_pci-dio96h] - 96 channels
	 (National Instruments) PCI-DIO-96 [ni_pci-dio-96] - 96 channels
	 (National Instruments) PCI-DIO-96B [ni_pci-dio-96b] - 96 channels
	 (National Instruments) PXI-6508 [ni_pxi-6508] - 96 channels
	 (National Instruments) PCI-6503 [ni_pci-6503] - 24 channels
	 (National Instruments) PCI-6503B [ni_pci-6503b] - 24 channels
	 (National Instruments) PCI-6503X [ni_pci-6503x] - 24 channels
	 (National Instruments) PXI-6503 [ni_pxi-6503] - 24 channels
Author: H Hartley Sweeten <hsweeten@visionengravers.com>
Updated: Wed, 12 Sep 2012 11:52:01 -0700
Status: untested

Some of these boards also have an 8254 programmable timer/counter
chip. This chip is not currently supported by this driver.

Interrupt support for these boards is also not currently supported.

Configuration Options: not applicable, uses PCI auto config
*/

#include <linux/pci.h>

#include "../comedidev.h"

#include "8255.h"

/*
 * PCI Device ID's supported by this driver
 */
#define PCI_DEVICE_ID_ADLINK_PCI7224	0x7224
#define PCI_DEVICE_ID_ADLINK_PCI7248	0x7248
#define PCI_DEVICE_ID_ADLINK_PCI7296	0x7296

#define PCI_DEVICE_ID_CB_PCIDIO48H	0x000b
#define PCI_DEVICE_ID_CB_PCIDIO24H	0x0014
#define PCI_DEVICE_ID_CB_PCIDIO96H	0x0017
#define PCI_DEVICE_ID_CB_PCIDIO24	0x0028

#define PCI_DEVICE_ID_NI_PCIDIO96	0x0160
#define PCI_DEVICE_ID_NI_PCI6503	0x0400
#define PCI_DEVICE_ID_NI_PCI6503B	0x1250
#define PCI_DEVICE_ID_NI_PXI6508	0x13c0
#define PCI_DEVICE_ID_NI_PCIDIO96B	0x1630
#define PCI_DEVICE_ID_NI_PCI6503X	0x17d0
#define PCI_DEVICE_ID_NI_PXI_6503	0x1800

struct pci_8255_boardinfo {
	const char *name;
	unsigned short vendor;
	unsigned short device;
	int dio_badr;
	int is_mmio;
	int n_8255;
};

static const struct pci_8255_boardinfo pci_8255_boards[] = {
	{
		.name		= "adl_pci-7224",
		.vendor		= PCI_VENDOR_ID_ADLINK,
		.device		= PCI_DEVICE_ID_ADLINK_PCI7224,
		.dio_badr	= 2,
		.n_8255		= 1,
	}, {
		.name		= "adl_pci-7248",
		.vendor		= PCI_VENDOR_ID_ADLINK,
		.device		= PCI_DEVICE_ID_ADLINK_PCI7248,
		.dio_badr	= 2,
		.n_8255		= 2,
	}, {
		.name		= "adl_pci-7296",
		.vendor		= PCI_VENDOR_ID_ADLINK,
		.device		= PCI_DEVICE_ID_ADLINK_PCI7296,
		.dio_badr	= 2,
		.n_8255		= 4,
	}, {
		.name		= "cb_pci-dio24",
		.vendor		= PCI_VENDOR_ID_CB,
		.device		= PCI_DEVICE_ID_CB_PCIDIO24,
		.dio_badr	= 2,
		.n_8255		= 1,
	}, {
		.name		= "cb_pci-dio24h",
		.vendor		= PCI_VENDOR_ID_CB,
		.device		= PCI_DEVICE_ID_CB_PCIDIO24H,
		.dio_badr	= 2,
		.n_8255		= 1,
	}, {
		.name		= "cb_pci-dio48h",
		.vendor		= PCI_VENDOR_ID_CB,
		.device		= PCI_DEVICE_ID_CB_PCIDIO48H,
		.dio_badr	= 1,
		.n_8255		= 2,
	}, {
		.name		= "cb_pci-dio96h",
		.vendor		= PCI_VENDOR_ID_CB,
		.device		= PCI_DEVICE_ID_CB_PCIDIO96H,
		.dio_badr	= 2,
		.n_8255		= 4,
	}, {
		.name		= "ni_pci-dio-96",
		.vendor		= PCI_VENDOR_ID_NI,
		.device		= PCI_DEVICE_ID_NI_PCIDIO96,
		.dio_badr	= 1,
		.is_mmio	= 1,
		.n_8255		= 4,
	}, {
		.name		= "ni_pci-dio-96b",
		.vendor		= PCI_VENDOR_ID_NI,
		.device		= PCI_DEVICE_ID_NI_PCIDIO96B,
		.dio_badr	= 1,
		.is_mmio	= 1,
		.n_8255		= 4,
	}, {
		.name		= "ni_pxi-6508",
		.vendor		= PCI_VENDOR_ID_NI,
		.device		= PCI_DEVICE_ID_NI_PXI6508,
		.dio_badr	= 1,
		.is_mmio	= 1,
		.n_8255		= 4,
	}, {
		.name		= "ni_pci-6503",
		.vendor		= PCI_VENDOR_ID_NI,
		.device		= PCI_DEVICE_ID_NI_PCI6503,
		.dio_badr	= 1,
		.is_mmio	= 1,
		.n_8255		= 1,
	}, {
		.name		= "ni_pci-6503b",
		.vendor		= PCI_VENDOR_ID_NI,
		.device		= PCI_DEVICE_ID_NI_PCI6503B,
		.dio_badr	= 1,
		.is_mmio	= 1,
		.n_8255		= 1,
	}, {
		.name		= "ni_pci-6503x",
		.vendor		= PCI_VENDOR_ID_NI,
		.device		= PCI_DEVICE_ID_NI_PCI6503X,
		.dio_badr	= 1,
		.is_mmio	= 1,
		.n_8255		= 1,
	}, {
		.name		= "ni_pxi-6503",
		.vendor		= PCI_VENDOR_ID_NI,
		.device		= PCI_DEVICE_ID_NI_PXI_6503,
		.dio_badr	= 1,
		.is_mmio	= 1,
		.n_8255		= 1,
	},
};

struct pci_8255_private {
	void __iomem *mmio_base;
};

static int pci_8255_mmio(int dir, int port, int data, unsigned long iobase)
{
	void __iomem *mmio_base = (void __iomem *)iobase;

	if (dir) {
		writeb(data, mmio_base + port);
		return 0;
	} else {
		return readb(mmio_base  + port);
	}
}

static const void *pci_8255_find_boardinfo(struct comedi_device *dev,
					      struct pci_dev *pcidev)
{
	const struct pci_8255_boardinfo *board;
	int i;

	for (i = 0; i < ARRAY_SIZE(pci_8255_boards); i++) {
		board = &pci_8255_boards[i];
		if (pcidev->vendor == board->vendor &&
		    pcidev->device == board->device)
			return board;
	}
	return NULL;
}

static int pci_8255_auto_attach(struct comedi_device *dev,
					  unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct pci_8255_boardinfo *board;
	struct pci_8255_private *devpriv;
	struct comedi_subdevice *s;
	resource_size_t iobase;
	unsigned long len;
	int ret;
	int i;

	board = pci_8255_find_boardinfo(dev, pcidev);
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	iobase = pci_resource_start(pcidev, board->dio_badr);
	len = pci_resource_len(pcidev, board->dio_badr);

	if (board->is_mmio) {
		devpriv->mmio_base = ioremap(iobase, len);
		if (!devpriv->mmio_base)
			return -ENOMEM;
	}
	dev->iobase = iobase;

	/*
	 * One, two, or four subdevices are setup by this driver depending
	 * on the number of channels provided by the board. Each subdevice
	 * has 24 channels supported by the 8255 module.
	 */
	ret = comedi_alloc_subdevices(dev, board->n_8255);
	if (ret)
		return ret;

	for (i = 0; i < board->n_8255; i++) {
		s = &dev->subdevices[i];
		if (board->is_mmio) {
			iobase = (unsigned long)(devpriv->mmio_base + (i * 4));
			ret = subdev_8255_init(dev, s, pci_8255_mmio, iobase);
		} else {
			iobase = dev->iobase + (i * 4);
			ret = subdev_8255_init(dev, s, NULL, iobase);
		}
		if (ret)
			return ret;
	}

	dev_info(dev->class_dev, "%s attached (%d digital i/o channels)\n",
		dev->board_name, board->n_8255 * 24);

	return 0;
}

static void pci_8255_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct pci_8255_boardinfo *board = comedi_board(dev);
	struct pci_8255_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	int i;

	if (!board || !devpriv)
		return;
	if (dev->subdevices) {
		for (i = 0; i < board->n_8255; i++) {
			s = &dev->subdevices[i];
			subdev_8255_cleanup(dev, s);
		}
	}
	if (pcidev) {
		if (devpriv->mmio_base)
			iounmap(devpriv->mmio_base);
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver pci_8255_driver = {
	.driver_name	= "8255_pci",
	.module		= THIS_MODULE,
	.auto_attach	= pci_8255_auto_attach,
	.detach		= pci_8255_detach,
};

static int pci_8255_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &pci_8255_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(pci_8255_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI_DEVICE_ID_ADLINK_PCI7224) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI_DEVICE_ID_ADLINK_PCI7248) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI_DEVICE_ID_ADLINK_PCI7296) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_DEVICE_ID_CB_PCIDIO24) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_DEVICE_ID_CB_PCIDIO24H) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_DEVICE_ID_CB_PCIDIO48H) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_DEVICE_ID_CB_PCIDIO96H) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, PCI_DEVICE_ID_NI_PCIDIO96) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, PCI_DEVICE_ID_NI_PCIDIO96B) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, PCI_DEVICE_ID_NI_PXI6508) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, PCI_DEVICE_ID_NI_PCI6503) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, PCI_DEVICE_ID_NI_PCI6503B) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, PCI_DEVICE_ID_NI_PCI6503X) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, PCI_DEVICE_ID_NI_PXI_6503) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, pci_8255_pci_table);

static struct pci_driver pci_8255_pci_driver = {
	.name		= "8255_pci",
	.id_table	= pci_8255_pci_table,
	.probe		= pci_8255_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(pci_8255_driver, pci_8255_pci_driver);

MODULE_DESCRIPTION("COMEDI - Generic PCI based 8255 Digital I/O boards");
MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_LICENSE("GPL");
