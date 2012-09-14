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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

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

#include <linux/interrupt.h>
#include <linux/slab.h>
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

/* Board description*/

struct ni_670x_board {
	const char *name;
	unsigned short dev_id;
	unsigned short ao_chans;
};

static const struct ni_670x_board ni_670x_boards[] = {
	{
		.name		= "PCI-6703",
		.dev_id		= 0x2c90,
		.ao_chans	= 16,
	}, {
		.name		= "PXI-6704",
		.dev_id		= 0x1920,
		.ao_chans	= 32,
	}, {
		.name		= "PCI-6704",
		.dev_id		= 0x1290,
		.ao_chans	= 32,
	},
};

struct ni_670x_private {

	struct mite_struct *mite;
	int boardtype;
	int dio;
	unsigned int ao_readback[32];
};

static struct comedi_lrange range_0_20mA = { 1, {RANGE_mA(0, 20)} };

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
				 struct comedi_insn *insn, unsigned int *data)
{
	struct ni_670x_private *devpriv = dev->private;
	void __iomem *io_addr = devpriv->mite->daq_io_addr +
					DIO_PORT0_DATA_OFFSET;
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		writel(s->state, io_addr);
	}

	data[1] = readl(io_addr);

	return insn->n;
}

static int ni_670x_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	struct ni_670x_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= 1 << chan;
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~(1 << chan);
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (s->io_bits & (1 << chan)) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
		break;
	}
	writel(s->io_bits, devpriv->mite->daq_io_addr + DIO_PORT0_DIR_OFFSET);

	return insn->n;
}

/* FIXME: remove this when dynamic MITE allocation implemented. */
static struct mite_struct *ni_670x_find_mite(struct pci_dev *pcidev)
{
	struct mite_struct *mite;

	for (mite = mite_devices; mite; mite = mite->next) {
		if (mite->used)
			continue;
		if (mite->pcidev == pcidev)
			return mite;
	}
	return NULL;
}

static const struct ni_670x_board *
ni_670x_find_boardinfo(struct pci_dev *pcidev)
{
	unsigned int dev_id = pcidev->device;
	unsigned int n;

	for (n = 0; n < ARRAY_SIZE(ni_670x_boards); n++) {
		const struct ni_670x_board *board = &ni_670x_boards[n];
		if (board->dev_id == dev_id)
			return board;
	}
	return NULL;
}

static int __devinit ni_670x_attach_pci(struct comedi_device *dev,
					struct pci_dev *pcidev)
{
	const struct ni_670x_board *thisboard;
	struct ni_670x_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	int i;

	ret = alloc_private(dev, sizeof(*devpriv));
	if (ret < 0)
		return ret;
	devpriv = dev->private;
	dev->board_ptr = ni_670x_find_boardinfo(pcidev);
	if (!dev->board_ptr)
		return -ENODEV;
	devpriv->mite = ni_670x_find_mite(pcidev);
	if (!devpriv->mite)
		return -ENODEV;
	thisboard = comedi_board(dev);

	ret = mite_setup(devpriv->mite);
	if (ret < 0) {
		dev_warn(dev->class_dev, "error setting up mite\n");
		return ret;
	}
	dev->board_name = thisboard->name;
	dev->irq = mite_irq(devpriv->mite);

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

	dev_info(dev->class_dev, "%s: %s attached\n",
		dev->driver->driver_name, dev->board_name);

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
	if (devpriv && devpriv->mite)
		mite_unsetup(devpriv->mite);
	if (dev->irq)
		free_irq(dev->irq, dev);
}

static struct comedi_driver ni_670x_driver = {
	.driver_name	= "ni_670x",
	.module		= THIS_MODULE,
	.attach_pci	= ni_670x_attach_pci,
	.detach		= ni_670x_detach,
};

static int __devinit ni_670x_pci_probe(struct pci_dev *dev,
				       const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &ni_670x_driver);
}

static void __devexit ni_670x_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(ni_670x_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, 0x2c90) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NI, 0x1920) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ni_670x_pci_table);

static struct pci_driver ni_670x_pci_driver = {
	.name		="ni_670x",
	.id_table	= ni_670x_pci_table,
	.probe		= ni_670x_pci_probe,
	.remove		= __devexit_p(ni_670x_pci_remove),
};
module_comedi_pci_driver(ni_670x_driver, ni_670x_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
