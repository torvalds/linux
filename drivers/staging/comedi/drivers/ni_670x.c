/*
 * Comedi driver for NI 670x devices
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2001 David A. Schleef <ds@schleef.org>
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
 * Driver: ni_670x
 * Description: National Instruments 670x
 * Author: Bart Joris <bjoris@advalvas.be>
 * Updated: Wed, 11 Dec 2002 18:25:35 -0800
 * Devices: [National Instruments] PCI-6703 (ni_670x), PCI-6704
 * Status: unknown
 *
 * Commands are not supported.
 *
 * Manuals:
 *   322110a.pdf	PCI/PXI-6704 User Manual
 *   322110b.pdf	PCI/PXI-6703/6704 User Manual
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include "../comedi_pci.h"

#define AO_VALUE_OFFSET			0x00
#define	AO_CHAN_OFFSET			0x0c
#define	AO_STATUS_OFFSET		0x10
#define AO_CONTROL_OFFSET		0x10
#define	DIO_PORT0_DIR_OFFSET	0x20
#define	DIO_PORT0_DATA_OFFSET	0x24
#define	DIO_PORT1_DIR_OFFSET	0x28
#define	DIO_PORT1_DATA_OFFSET	0x2c
#define	MISC_STATUS_OFFSET		0x14
#define	MISC_CONTROL_OFFSET		0x14

enum ni_670x_boardid {
	BOARD_PCI6703,
	BOARD_PXI6704,
	BOARD_PCI6704,
};

struct ni_670x_board {
	const char *name;
	unsigned short ao_chans;
};

static const struct ni_670x_board ni_670x_boards[] = {
	[BOARD_PCI6703] = {
		.name		= "PCI-6703",
		.ao_chans	= 16,
	},
	[BOARD_PXI6704] = {
		.name		= "PXI-6704",
		.ao_chans	= 32,
	},
	[BOARD_PCI6704] = {
		.name		= "PCI-6704",
		.ao_chans	= 32,
	},
};

struct ni_670x_private {
	int boardtype;
	int dio;
};

static int ni_670x_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	/*
	 * Channel number mapping:
	 *
	 * NI 6703/ NI 6704 | NI 6704 Only
	 * -------------------------------
	 * vch(0)  :  0     | ich(16) :  1
	 * vch(1)  :  2     | ich(17) :  3
	 * ...              | ...
	 * vch(15) : 30     | ich(31) : 31
	 */
	for (i = 0; i < insn->n; i++) {
		val = data[i];
		/* First write in channel register which channel to use */
		writel(((chan & 15) << 1) | ((chan & 16) >> 4),
		       dev->mmio + AO_CHAN_OFFSET);
		/* write channel value */
		writel(val, dev->mmio + AO_VALUE_OFFSET);
	}
	s->readback[chan] = val;

	return insn->n;
}

static int ni_670x_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		writel(s->state, dev->mmio + DIO_PORT0_DATA_OFFSET);

	data[1] = readl(dev->mmio + DIO_PORT0_DATA_OFFSET);

	return insn->n;
}

static int ni_670x_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	writel(s->io_bits, dev->mmio + DIO_PORT0_DIR_OFFSET);

	return insn->n;
}

/* ripped from mite.h and mite_setup2() to avoid mite dependency */
#define MITE_IODWBSR	0xc0	 /* IO Device Window Base Size Register */
#define WENAB		(1 << 7) /* window enable */

static int ni_670x_mite_init(struct pci_dev *pcidev)
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

static int ni_670x_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct ni_670x_board *board = NULL;
	struct ni_670x_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	int i;

	if (context < ARRAY_SIZE(ni_670x_boards))
		board = &ni_670x_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = ni_670x_mite_init(pcidev);
	if (ret)
		return ret;

	dev->mmio = pci_ioremap_bar(pcidev, 1);
	if (!dev->mmio)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = board->ao_chans;
	s->maxdata = 0xffff;
	if (s->n_chan == 32) {
		const struct comedi_lrange **range_table_list;

		range_table_list = kmalloc_array(32,
						 sizeof(struct comedi_lrange *),
						 GFP_KERNEL);
		if (!range_table_list)
			return -ENOMEM;
		s->range_table_list = range_table_list;
		for (i = 0; i < 16; i++) {
			range_table_list[i] = &range_bipolar10;
			range_table_list[16 + i] = &range_0_20mA;
		}
	} else {
		s->range_table = &range_bipolar10;
	}
	s->insn_write = ni_670x_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	s = &dev->subdevices[1];
	/* digital i/o subdevice */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 8;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = ni_670x_dio_insn_bits;
	s->insn_config = ni_670x_dio_insn_config;

	/* Config of misc registers */
	writel(0x10, dev->mmio + MISC_CONTROL_OFFSET);
	/* Config of ao registers */
	writel(0x00, dev->mmio + AO_CONTROL_OFFSET);

	return 0;
}

static void ni_670x_detach(struct comedi_device *dev)
{
	struct comedi_subdevice *s;

	comedi_pci_detach(dev);
	if (dev->n_subdevices) {
		s = &dev->subdevices[0];
		if (s)
			kfree(s->range_table_list);
	}
}

static struct comedi_driver ni_670x_driver = {
	.driver_name	= "ni_670x",
	.module		= THIS_MODULE,
	.auto_attach	= ni_670x_auto_attach,
	.detach		= ni_670x_detach,
};

static int ni_670x_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &ni_670x_driver, id->driver_data);
}

static const struct pci_device_id ni_670x_pci_table[] = {
	{ PCI_VDEVICE(NI, 0x1290), BOARD_PCI6704 },
	{ PCI_VDEVICE(NI, 0x1920), BOARD_PXI6704 },
	{ PCI_VDEVICE(NI, 0x2c90), BOARD_PCI6703 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ni_670x_pci_table);

static struct pci_driver ni_670x_pci_driver = {
	.name		= "ni_670x",
	.id_table	= ni_670x_pci_table,
	.probe		= ni_670x_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(ni_670x_driver, ni_670x_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
