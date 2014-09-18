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

#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"

#include "8255.h"

enum pci_8255_boardid {
	BOARD_ADLINK_PCI7224,
	BOARD_ADLINK_PCI7248,
	BOARD_ADLINK_PCI7296,
	BOARD_CB_PCIDIO24,
	BOARD_CB_PCIDIO24H,
	BOARD_CB_PCIDIO48H_OLD,
	BOARD_CB_PCIDIO48H_NEW,
	BOARD_CB_PCIDIO96H,
	BOARD_NI_PCIDIO96,
	BOARD_NI_PCIDIO96B,
	BOARD_NI_PXI6508,
	BOARD_NI_PCI6503,
	BOARD_NI_PCI6503B,
	BOARD_NI_PCI6503X,
	BOARD_NI_PXI_6503,
};

struct pci_8255_boardinfo {
	const char *name;
	int dio_badr;
	int n_8255;
	unsigned int has_mite:1;
};

static const struct pci_8255_boardinfo pci_8255_boards[] = {
	[BOARD_ADLINK_PCI7224] = {
		.name		= "adl_pci-7224",
		.dio_badr	= 2,
		.n_8255		= 1,
	},
	[BOARD_ADLINK_PCI7248] = {
		.name		= "adl_pci-7248",
		.dio_badr	= 2,
		.n_8255		= 2,
	},
	[BOARD_ADLINK_PCI7296] = {
		.name		= "adl_pci-7296",
		.dio_badr	= 2,
		.n_8255		= 4,
	},
	[BOARD_CB_PCIDIO24] = {
		.name		= "cb_pci-dio24",
		.dio_badr	= 2,
		.n_8255		= 1,
	},
	[BOARD_CB_PCIDIO24H] = {
		.name		= "cb_pci-dio24h",
		.dio_badr	= 2,
		.n_8255		= 1,
	},
	[BOARD_CB_PCIDIO48H_OLD] = {
		.name		= "cb_pci-dio48h",
		.dio_badr	= 1,
		.n_8255		= 2,
	},
	[BOARD_CB_PCIDIO48H_NEW] = {
		.name		= "cb_pci-dio48h",
		.dio_badr	= 2,
		.n_8255		= 2,
	},
	[BOARD_CB_PCIDIO96H] = {
		.name		= "cb_pci-dio96h",
		.dio_badr	= 2,
		.n_8255		= 4,
	},
	[BOARD_NI_PCIDIO96] = {
		.name		= "ni_pci-dio-96",
		.dio_badr	= 1,
		.n_8255		= 4,
		.has_mite	= 1,
	},
	[BOARD_NI_PCIDIO96B] = {
		.name		= "ni_pci-dio-96b",
		.dio_badr	= 1,
		.n_8255		= 4,
		.has_mite	= 1,
	},
	[BOARD_NI_PXI6508] = {
		.name		= "ni_pxi-6508",
		.dio_badr	= 1,
		.n_8255		= 4,
		.has_mite	= 1,
	},
	[BOARD_NI_PCI6503] = {
		.name		= "ni_pci-6503",
		.dio_badr	= 1,
		.n_8255		= 1,
		.has_mite	= 1,
	},
	[BOARD_NI_PCI6503B] = {
		.name		= "ni_pci-6503b",
		.dio_badr	= 1,
		.n_8255		= 1,
		.has_mite	= 1,
	},
	[BOARD_NI_PCI6503X] = {
		.name		= "ni_pci-6503x",
		.dio_badr	= 1,
		.n_8255		= 1,
		.has_mite	= 1,
	},
	[BOARD_NI_PXI_6503] = {
		.name		= "ni_pxi-6503",
		.dio_badr	= 1,
		.n_8255		= 1,
		.has_mite	= 1,
	},
};

/* ripped from mite.h and mite_setup2() to avoid mite dependancy */
#define MITE_IODWBSR	0xc0	 /* IO Device Window Base Size Register */
#define WENAB		(1 << 7) /* window enable */

static int pci_8255_mite_init(struct pci_dev *pcidev)
{
	void __iomem *mite_base;
	u32 main_phys_addr;

	/* ioremap the MITE registers (BAR 0) temporarily */
	mite_base = pci_ioremap_bar(pcidev, 0);
	if (!mite_base)
		return -ENOMEM;

	/* set data window to main registers (BAR 1) */
	main_phys_addr = pci_resource_start(pcidev, 1);
	writel(main_phys_addr | WENAB, mite_base + MITE_IODWBSR);

	/* finished with MITE registers */
	iounmap(mite_base);
	return 0;
}

static int pci_8255_mmio(int dir, int port, int data, unsigned long iobase)
{
	void __iomem *mmio_base = (void __iomem *)iobase;

	if (dir) {
		writeb(data, mmio_base + port);
		return 0;
	}
	return readb(mmio_base  + port);
}

static int pci_8255_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct pci_8255_boardinfo *board = NULL;
	struct comedi_subdevice *s;
	bool is_mmio;
	int ret;
	int i;

	if (context < ARRAY_SIZE(pci_8255_boards))
		board = &pci_8255_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	if (board->has_mite) {
		ret = pci_8255_mite_init(pcidev);
		if (ret)
			return ret;
	}

	is_mmio = (pci_resource_flags(pcidev, board->dio_badr) &
		   IORESOURCE_MEM) != 0;
	if (is_mmio) {
		dev->mmio = pci_ioremap_bar(pcidev, board->dio_badr);
		if (!dev->mmio)
			return -ENOMEM;
	} else {
		dev->iobase = pci_resource_start(pcidev, board->dio_badr);
	}

	/*
	 * One, two, or four subdevices are setup by this driver depending
	 * on the number of channels provided by the board. Each subdevice
	 * has 24 channels supported by the 8255 module.
	 */
	ret = comedi_alloc_subdevices(dev, board->n_8255);
	if (ret)
		return ret;

	for (i = 0; i < board->n_8255; i++) {
		unsigned long iobase;

		s = &dev->subdevices[i];
		if (is_mmio) {
			iobase = (unsigned long)(dev->mmio + (i * 4));
			ret = subdev_8255_init(dev, s, pci_8255_mmio, iobase);
		} else {
			iobase = dev->iobase + (i * 4);
			ret = subdev_8255_init(dev, s, NULL, iobase);
		}
		if (ret)
			return ret;
	}

	return 0;
}

static void pci_8255_detach(struct comedi_device *dev)
{
	if (dev->mmio)
		iounmap(dev->mmio);
	comedi_pci_disable(dev);
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

static const struct pci_device_id pci_8255_pci_table[] = {
	{ PCI_VDEVICE(ADLINK, 0x7224), BOARD_ADLINK_PCI7224 },
	{ PCI_VDEVICE(ADLINK, 0x7248), BOARD_ADLINK_PCI7248 },
	{ PCI_VDEVICE(ADLINK, 0x7296), BOARD_ADLINK_PCI7296 },
	{ PCI_VDEVICE(CB, 0x0028), BOARD_CB_PCIDIO24 },
	{ PCI_VDEVICE(CB, 0x0014), BOARD_CB_PCIDIO24H },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CB, 0x000b, 0x0000, 0x0000),
	  .driver_data = BOARD_CB_PCIDIO48H_OLD },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CB, 0x000b, PCI_VENDOR_ID_CB, 0x000b),
	  .driver_data = BOARD_CB_PCIDIO48H_NEW },
	{ PCI_VDEVICE(CB, 0x0017), BOARD_CB_PCIDIO96H },
	{ PCI_VDEVICE(NI, 0x0160), BOARD_NI_PCIDIO96 },
	{ PCI_VDEVICE(NI, 0x1630), BOARD_NI_PCIDIO96B },
	{ PCI_VDEVICE(NI, 0x13c0), BOARD_NI_PXI6508 },
	{ PCI_VDEVICE(NI, 0x0400), BOARD_NI_PCI6503 },
	{ PCI_VDEVICE(NI, 0x1250), BOARD_NI_PCI6503B },
	{ PCI_VDEVICE(NI, 0x17d0), BOARD_NI_PCI6503X },
	{ PCI_VDEVICE(NI, 0x1800), BOARD_NI_PXI_6503 },
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
