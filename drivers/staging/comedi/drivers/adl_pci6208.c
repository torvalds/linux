/*
    comedi/drivers/adl_pci6208.c

    Hardware driver for ADLink 6208 series cards:
	card	     | voltage output    | current output
	-------------+-------------------+---------------
	PCI-6208V    |  8 channels       | -
	PCI-6216V    | 16 channels       | -
	PCI-6208A    |  8 channels       | 8 channels

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

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
Driver: adl_pci6208
Description: ADLink PCI-6208A
Devices: [ADLink] PCI-6208A (adl_pci6208)
Author: nsyeow <nsyeow@pd.jaring.my>
Updated: Fri, 30 Jan 2004 14:44:27 +0800
Status: untested

Configuration Options:
  none

References:
	- ni_660x.c
	- adl_pci9111.c		copied the entire pci setup section
	- adl_pci9118.c
*/

#include "../comedidev.h"

/*
 * PCI-6208/6216-GL register map
 */
#define PCI6208_AO_CONTROL(x)		(0x00 + (2 * (x)))
#define PCI6208_AO_STATUS		0x00
#define PCI6208_AO_STATUS_DATA_SEND	(1 << 0)
#define PCI6208_DIO			0x40
#define PCI6208_DIO_DO_MASK		(0x0f)
#define PCI6208_DIO_DO_SHIFT		(0)
#define PCI6208_DIO_DI_MASK		(0xf0)
#define PCI6208_DIO_DI_SHIFT		(4)

#define PCI6208_MAX_AO_CHANNELS		8

struct pci6208_board {
	const char *name;
	unsigned short dev_id;
	int ao_chans;
};

static const struct pci6208_board pci6208_boards[] = {
	{
		.name		= "pci6208a",
		.dev_id		= 0x6208,
		.ao_chans	= 8,
	},
};

struct pci6208_private {
	struct pci_dev *pci_dev;
	unsigned int ao_readback[PCI6208_MAX_AO_CHANNELS];
};

static int pci6208_ao_winsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct pci6208_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	unsigned long invert = 1 << (16 - 1);
	unsigned long value = 0;
	unsigned short status;
	int i;

	for (i = 0; i < insn->n; i++) {
		value = data[i] ^ invert;

		do {
			status = inw(dev->iobase + PCI6208_AO_STATUS);
		} while (status & PCI6208_AO_STATUS_DATA_SEND);

		outw(value, dev->iobase + PCI6208_AO_CONTROL(chan));
	}
	devpriv->ao_readback[chan] = value;

	return insn->n;
}

static int pci6208_ao_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct pci6208_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return insn->n;
}

static int pci6208_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int mask = data[0] & PCI6208_DIO_DO_MASK;
	unsigned int bits = data[1];

	if (mask) {
		s->state &= ~mask;
		s->state |= bits & mask;

		outw(s->state, dev->iobase + PCI6208_DIO);
	}

	s->state = inw(dev->iobase + PCI6208_DIO);
	data[1] = s->state;

	return insn->n;
}

static int pci6208_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	unsigned int mask = 1 << chan;

	switch (data[0]) {
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & mask) ? COMEDI_OUTPUT : COMEDI_INPUT;
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static struct pci_dev *pci6208_find_device(struct comedi_device *dev,
					   struct comedi_devconfig *it)
{
	const struct pci6208_board *thisboard;
	struct pci_dev *pci_dev = NULL;
	int bus = it->options[0];
	int slot = it->options[1];
	int i;

	for_each_pci_dev(pci_dev) {
		if (pci_dev->vendor != PCI_VENDOR_ID_ADLINK)
			continue;
		for (i = 0; i < ARRAY_SIZE(pci6208_boards); i++) {
			thisboard = &pci6208_boards[i];
			if (thisboard->dev_id != pci_dev->device)
				continue;
			/* was a particular bus/slot requested? */
			if (bus || slot) {
				/* are we on the wrong bus/slot? */
				if (pci_dev->bus->number != bus ||
				    PCI_SLOT(pci_dev->devfn) != slot)
					continue;
			}
			dev_dbg(dev->class_dev,
				"Found %s on bus %d, slot, %d, irq=%d\n",
				thisboard->name,
				pci_dev->bus->number,
				PCI_SLOT(pci_dev->devfn),
				pci_dev->irq);
			dev->board_ptr = thisboard;
			return pci_dev;
		}
	}
	dev_err(dev->class_dev,
		"No supported board found! (req. bus %d, slot %d)\n",
		bus, slot);
	return NULL;
}

static int pci6208_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	const struct pci6208_board *thisboard;
	struct pci6208_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = alloc_private(dev, sizeof(*devpriv));
	if (ret < 0)
		return ret;
	devpriv = dev->private;

	devpriv->pci_dev = pci6208_find_device(dev, it);
	if (!devpriv->pci_dev)
		return -EIO;
	thisboard = comedi_board(dev);

	dev->board_name = thisboard->name;

	ret = comedi_pci_enable(devpriv->pci_dev, dev->driver->driver_name);
	if (ret) {
		dev_err(dev->class_dev,
			"Failed to enable PCI device and request regions\n");
		return ret;
	}
	dev->iobase = pci_resource_start(devpriv->pci_dev, 2);

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = dev->subdevices + 0;
	/* analog output subdevice */
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= thisboard->ao_chans;
	s->maxdata	= 0xffff;
	s->range_table	= &range_bipolar10;
	s->insn_write	= pci6208_ao_winsn;
	s->insn_read	= pci6208_ao_rinsn;

	s = dev->subdevices + 1;
	/* digital i/o subdevice */
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci6208_dio_insn_bits;
	s->insn_config	= pci6208_dio_insn_config;

	s->io_bits	= 0x0f;
	s->state	= inw(dev->iobase + PCI6208_DIO);

	dev_info(dev->class_dev, "%s: %s, I/O base=0x%04lx\n",
		dev->driver->driver_name, dev->board_name, dev->iobase);

	return 0;
}

static void pci6208_detach(struct comedi_device *dev)
{
	struct pci6208_private *devpriv = dev->private;

	if (devpriv && devpriv->pci_dev) {
		if (dev->iobase)
			comedi_pci_disable(devpriv->pci_dev);
		pci_dev_put(devpriv->pci_dev);
	}
}

static struct comedi_driver adl_pci6208_driver = {
	.driver_name	= "adl_pci6208",
	.module		= THIS_MODULE,
	.attach		= pci6208_attach,
	.detach		= pci6208_detach,
};

static int __devinit adl_pci6208_pci_probe(struct pci_dev *dev,
					   const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &adl_pci6208_driver);
}

static void __devexit adl_pci6208_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(adl_pci6208_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, 0x6208) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adl_pci6208_pci_table);

static struct pci_driver adl_pci6208_pci_driver = {
	.name		= "adl_pci6208",
	.id_table	= adl_pci6208_pci_table,
	.probe		= adl_pci6208_pci_probe,
	.remove		= __devexit_p(adl_pci6208_pci_remove),
};
module_comedi_pci_driver(adl_pci6208_driver, adl_pci6208_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
