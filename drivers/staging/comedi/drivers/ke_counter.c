/*
    comedi/drivers/ke_counter.c
    Comedi driver for Kolter-Electronic PCI Counter 1 Card

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
Driver: ke_counter
Description: Driver for Kolter Electronic Counter Card
Devices: [Kolter Electronic] PCI Counter Card (ke_counter)
Author: Michael Hillmann
Updated: Mon, 14 Apr 2008 15:42:42 +0100
Status: tested

Configuration Options: not applicable, uses PCI auto config

This driver is a simple driver to read the counter values from
Kolter Electronic PCI Counter Card.
*/

#include <linux/pci.h>

#include "../comedidev.h"

#define CNT_CARD_DEVICE_ID      0x0014

/*-- counter write ----------------------------------------------------------*/

/* This should be used only for resetting the counters; maybe it is better
   to make a special command 'reset'. */
static int cnt_winsn(struct comedi_device *dev,
		     struct comedi_subdevice *s, struct comedi_insn *insn,
		     unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);

	outb((unsigned char)((data[0] >> 24) & 0xff),
	     dev->iobase + chan * 0x20 + 0x10);
	outb((unsigned char)((data[0] >> 16) & 0xff),
	     dev->iobase + chan * 0x20 + 0x0c);
	outb((unsigned char)((data[0] >> 8) & 0xff),
	     dev->iobase + chan * 0x20 + 0x08);
	outb((unsigned char)((data[0] >> 0) & 0xff),
	     dev->iobase + chan * 0x20 + 0x04);

	/* return the number of samples written */
	return 1;
}

/*-- counter read -----------------------------------------------------------*/

static int cnt_rinsn(struct comedi_device *dev,
		     struct comedi_subdevice *s, struct comedi_insn *insn,
		     unsigned int *data)
{
	unsigned char a0, a1, a2, a3, a4;
	int chan = CR_CHAN(insn->chanspec);
	int result;

	a0 = inb(dev->iobase + chan * 0x20);
	a1 = inb(dev->iobase + chan * 0x20 + 0x04);
	a2 = inb(dev->iobase + chan * 0x20 + 0x08);
	a3 = inb(dev->iobase + chan * 0x20 + 0x0c);
	a4 = inb(dev->iobase + chan * 0x20 + 0x10);

	result = (a1 + (a2 * 256) + (a3 * 65536));
	if (a4 > 0)
		result = result - s->maxdata;

	*data = (unsigned int)result;

	/* return the number of samples read */
	return 1;
}

static int cnt_auto_attach(struct comedi_device *dev,
				     unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct comedi_subdevice *s;
	int ret;

	dev->board_name = dev->driver->driver_name;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 0);

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	dev->read_subdev = s;

	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_READABLE /* | SDF_COMMON */ ;
	s->n_chan = 3;
	s->maxdata = 0x00ffffff;
	s->insn_read = cnt_rinsn;
	s->insn_write = cnt_winsn;

	/*  select 20MHz clock */
	outb(3, dev->iobase + 248);

	/*  reset all counters */
	outb(0, dev->iobase);
	outb(0, dev->iobase + 0x20);
	outb(0, dev->iobase + 0x40);

	dev_info(dev->class_dev, "%s: %s attached\n",
		dev->driver->driver_name, dev->board_name);

	return 0;
}

static void cnt_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver ke_counter_driver = {
	.driver_name	= "ke_counter",
	.module		= THIS_MODULE,
	.auto_attach	= cnt_auto_attach,
	.detach		= cnt_detach,
};

static int ke_counter_pci_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &ke_counter_driver,
				      id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(ke_counter_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_KOLTER, CNT_CARD_DEVICE_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ke_counter_pci_table);

static struct pci_driver ke_counter_pci_driver = {
	.name		= "ke_counter",
	.id_table	= ke_counter_pci_table,
	.probe		= ke_counter_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(ke_counter_driver, ke_counter_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
