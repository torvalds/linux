/*
    comedi/drivers/ni_670x.c
    Hardware driver for NI 670x devices

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2001 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/
/*
Driver: ni_670x
Description: National Instruments 670x
Author: Bart Joris <bjoris@advalvas.be>
Updated: Wed, 11 Dec 2002 18:25:35 -0800
Devices: [National Instruments] PCI-6703 (ni_670x), PCI-6704
Status: unknown

Commands are not supported.
*/

/*
	Bart Joris <bjoris@advalvas.be> Last updated on 20/08/2001

	Manuals:

	322110a.pdf	PCI/PXI-6704 User Manual
	322110b.pdf	PCI/PXI-6703/6704 User Manual

*/

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "mite.h"

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

	struct mite_struct *mite;
	int boardtype;
	int dio;
	unsigned int ao_readback[32];
};

static int ni_670x_ao_winsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct ni_670x_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);

	/* Channel number mapping :

	   NI 6703/ NI 6704     | NI 6704 Only
	   ----------------------------------------------------
	   vch(0)       :       0       | ich(16)       :       1
	   vch(1)       :       2       | ich(17)       :       3
	   .    :       .       |   .                   .
	   .    :       .       |   .                   .
	   .    :       .       |   .                   .
	   vch(15)      :       30      | ich(31)       :       31      */

	for (i = 0; i < insn->n; i++) {
		/* First write in channel register which channel to use */
		writel(((chan & 15) << 1) | ((chan & 16) >> 4),
		       devpriv->mite->daq_io_addr + AO_CHAN_OFFSET);
		/* write channel value */
		writel(data[i], devpriv->mite->daq_io_addr + AO_VALUE_OFFSET);
		devpriv->ao_readback[chan] = data[i];
	}

	return i;
}

static int ni_670x_ao_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct ni_670x_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

static int ni_670x_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct ni_670x_private *devpriv = dev->private;
	void __iomem *io_addr = devpriv->mite->daq_io_addr +
					DIO_PORT0_DATA_OFFSET;

	if (comedi_dio_update_state(s, data))
		writel(s->state, io_addr);

	data[1] = readl(io_addr);

	return insn->n;
}

static int ni_670x_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct ni_670x_private *devpriv = dev->private;
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	writel(s->io_bits, devpriv->mite->daq_io_addr + DIO_PORT0_DIR_OFFSET);

	return insn->n;
}

static int ni_670x_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct ni_670x_board *thisboard = NULL;
	struct ni_670x_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	int i;

	if (context < ARRAY_SIZE(ni_670x_boards))
		thisboard = &ni_670x_boards[context];
	if (!thisboard)
		return -ENODEV;
	dev->board_ptr = thisboard;
	dev->board_name = thisboard->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	devpriv->mite = mite_alloc(pcidev);
	if (!devpriv->mite)
		return -ENOMEM;

	ret = mite_setup(devpriv->mite);
	if (ret < 0) {
		dev_warn(dev->class_dev, "error setting up mite\n");
		return ret;
	}

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = thisboard->ao_chans;
	s->maxdata = 0xffff;
	if (s->n_chan == 32) {
		const struct comedi_lrange **range_table_list;

		range_table_list = kmalloc(sizeof(struct comedi_lrange *) * 32,
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
	s->insn_write = &ni_670x_ao_winsn;
	s->insn_read = &ni_670x_ao_rinsn;

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
	writel(0x10, devpriv->mite->daq_io_addr + MISC_CONTROL_OFFSET);
	/* Config of ao registers */
	writel(0x00, devpriv->mite->daq_io_addr + AO_CONTROL_OFFSET);

	return 0;
}

static void ni_670x_detach(struct comedi_device *dev)
{
	struct ni_670x_private *devpriv = dev->private;
	struct comedi_subdevice *s;

	if (dev->n_subdevices) {
		s = &dev->subdevices[0];
		if (s)
			kfree(s->range_table_list);
	}
	if (devpriv && devpriv->mite) {
		mite_unsetup(devpriv->mite);
		mite_free(devpriv->mite);
	}
	comedi_pci_disable(dev);
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
