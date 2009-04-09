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

#include "../comedidev.h"

#include "mite.h"

#define PCI_VENDOR_ID_NATINST	0x1093

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
	unsigned short dev_id;
	const char *name;
	unsigned short ao_chans;
	unsigned short ao_bits;
};

static const struct ni_670x_board ni_670x_boards[] = {
	{
	      dev_id:	0x2c90,
	      name:	"PCI-6703",
	      ao_chans:16,
	      ao_bits:	16,
		},
	{
	      dev_id:	0x1920,
	      name:	"PXI-6704",
	      ao_chans:32,
	      ao_bits:	16,
		},
	{
	      dev_id:	0x1290,
	      name:	"PCI-6704",
	      ao_chans:32,
	      ao_bits:	16,
		},
};

static DEFINE_PCI_DEVICE_TABLE(ni_670x_pci_table) = {
	{PCI_VENDOR_ID_NATINST, 0x2c90, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_NATINST, 0x1920, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	/* { PCI_VENDOR_ID_NATINST, 0x0000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, */
	{0}
};

MODULE_DEVICE_TABLE(pci, ni_670x_pci_table);

#define thisboard ((struct ni_670x_board *)dev->board_ptr)

struct ni_670x_private {

	struct mite_struct *mite;
	int boardtype;
	int dio;
	unsigned int ao_readback[32];
};


#define devpriv ((struct ni_670x_private *)dev->private)
#define n_ni_670x_boards (sizeof(ni_670x_boards)/sizeof(ni_670x_boards[0]))

static int ni_670x_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int ni_670x_detach(struct comedi_device *dev);

static struct comedi_driver driver_ni_670x = {
      driver_name:"ni_670x",
      module:THIS_MODULE,
      attach:ni_670x_attach,
      detach:ni_670x_detach,
};

COMEDI_PCI_INITCLEANUP(driver_ni_670x, ni_670x_pci_table);

static struct comedi_lrange range_0_20mA = { 1, {RANGE_mA(0, 20)} };

static int ni_670x_find_device(struct comedi_device *dev, int bus, int slot);

static int ni_670x_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data);
static int ni_670x_ao_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data);
static int ni_670x_dio_insn_bits(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data);
static int ni_670x_dio_insn_config(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data);

static int ni_670x_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;
	int i;

	printk("comedi%d: ni_670x: ", dev->minor);

	if ((ret = alloc_private(dev, sizeof(struct ni_670x_private))) < 0)
		return ret;

	ret = ni_670x_find_device(dev, it->options[0], it->options[1]);
	if (ret < 0)
		return ret;

	ret = mite_setup(devpriv->mite);
	if (ret < 0) {
		printk("error setting up mite\n");
		return ret;
	}
	dev->board_name = thisboard->name;
	dev->irq = mite_irq(devpriv->mite);
	printk(" %s", dev->board_name);

	if (alloc_subdevices(dev, 2) < 0)
		return -ENOMEM;

	s = dev->subdevices + 0;
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

	s = dev->subdevices + 1;
	/* digital i/o subdevice */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 8;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = ni_670x_dio_insn_bits;
	s->insn_config = ni_670x_dio_insn_config;

	writel(0x10, devpriv->mite->daq_io_addr + MISC_CONTROL_OFFSET);	/* Config of misc registers */
	writel(0x00, devpriv->mite->daq_io_addr + AO_CONTROL_OFFSET);	/* Config of ao registers */

	printk("attached\n");

	return 1;
}

static int ni_670x_detach(struct comedi_device *dev)
{
	printk("comedi%d: ni_670x: remove\n", dev->minor);

	if (dev->subdevices[0].range_table_list) {
		kfree(dev->subdevices[0].range_table_list);
	}
	if (dev->private && devpriv->mite)
		mite_unsetup(devpriv->mite);

	if (dev->irq)
		comedi_free_irq(dev->irq, dev);

	return 0;
}

static int ni_670x_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
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
		writel(((chan & 15) << 1) | ((chan & 16) >> 4), devpriv->mite->daq_io_addr + AO_CHAN_OFFSET);	/* First write in channel register which channel to use */
		writel(data[i], devpriv->mite->daq_io_addr + AO_VALUE_OFFSET);	/* write channel value */
		devpriv->ao_readback[chan] = data[i];
	}

	return i;
}

static int ni_670x_ao_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

static int ni_670x_dio_insn_bits(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
		writel(s->state,
			devpriv->mite->daq_io_addr + DIO_PORT0_DATA_OFFSET);
	}

	/* on return, data[1] contains the value of the digital
	 * input lines. */
	data[1] = readl(devpriv->mite->daq_io_addr + DIO_PORT0_DATA_OFFSET);

	return 2;
}

static int ni_670x_dio_insn_config(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
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
			(s->
			io_bits & (1 << chan)) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
		break;
	}
	writel(s->io_bits, devpriv->mite->daq_io_addr + DIO_PORT0_DIR_OFFSET);

	return insn->n;
}

static int ni_670x_find_device(struct comedi_device *dev, int bus, int slot)
{
	struct mite_struct *mite;
	int i;

	for (mite = mite_devices; mite; mite = mite->next) {
		if (mite->used)
			continue;
		if (bus || slot) {
			if (bus != mite->pcidev->bus->number
				|| slot != PCI_SLOT(mite->pcidev->devfn))
				continue;
		}

		for (i = 0; i < n_ni_670x_boards; i++) {
			if (mite_device_id(mite) == ni_670x_boards[i].dev_id) {
				dev->board_ptr = ni_670x_boards + i;
				devpriv->mite = mite;

				return 0;
			}
		}
	}
	printk("no device found\n");
	mite_list_devices();
	return -EIO;
}
