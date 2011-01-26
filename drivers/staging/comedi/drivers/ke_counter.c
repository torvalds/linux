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

Configuration Options:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)
  If bus/slot is not specified, the first supported
  PCI device found will be used.

This driver is a simple driver to read the counter values from
Kolter Electronic PCI Counter Card.
*/

#include "../comedidev.h"

#include "comedi_pci.h"

#define CNT_DRIVER_NAME         "ke_counter"
#define PCI_VENDOR_ID_KOLTER    0x1001
#define CNT_CARD_DEVICE_ID      0x0014

/*-- function prototypes ----------------------------------------------------*/

static int cnt_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int cnt_detach(struct comedi_device *dev);

static DEFINE_PCI_DEVICE_TABLE(cnt_pci_table) = {
	{
	PCI_VENDOR_ID_KOLTER, CNT_CARD_DEVICE_ID, PCI_ANY_ID,
		    PCI_ANY_ID, 0, 0, 0}, {
	0}
};

MODULE_DEVICE_TABLE(pci, cnt_pci_table);

/*-- board specification structure ------------------------------------------*/

struct cnt_board_struct {

	const char *name;
	int device_id;
	int cnt_channel_nbr;
	int cnt_bits;
};

static const struct cnt_board_struct cnt_boards[] = {
	{
	 .name = CNT_DRIVER_NAME,
	 .device_id = CNT_CARD_DEVICE_ID,
	 .cnt_channel_nbr = 3,
	 .cnt_bits = 24}
};

#define cnt_board_nbr (sizeof(cnt_boards)/sizeof(struct cnt_board_struct))

/*-- device private structure -----------------------------------------------*/

struct cnt_device_private {

	struct pci_dev *pcidev;
};

#define devpriv ((struct cnt_device_private *)dev->private)

static struct comedi_driver cnt_driver = {
	.driver_name = CNT_DRIVER_NAME,
	.module = THIS_MODULE,
	.attach = cnt_attach,
	.detach = cnt_detach,
};

static int __devinit cnt_driver_pci_probe(struct pci_dev *dev,
					  const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, cnt_driver.driver_name);
}

static void __devexit cnt_driver_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver cnt_driver_pci_driver = {
	.id_table = cnt_pci_table,
	.probe = &cnt_driver_pci_probe,
	.remove = __devexit_p(&cnt_driver_pci_remove)
};

static int __init cnt_driver_init_module(void)
{
	int retval;

	retval = comedi_driver_register(&cnt_driver);
	if (retval < 0)
		return retval;

	cnt_driver_pci_driver.name = (char *)cnt_driver.driver_name;
	return pci_register_driver(&cnt_driver_pci_driver);
}

static void __exit cnt_driver_cleanup_module(void)
{
	pci_unregister_driver(&cnt_driver_pci_driver);
	comedi_driver_unregister(&cnt_driver);
}

module_init(cnt_driver_init_module);
module_exit(cnt_driver_cleanup_module);

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

/*-- attach -----------------------------------------------------------------*/

static int cnt_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *subdevice;
	struct pci_dev *pci_device = NULL;
	struct cnt_board_struct *board;
	unsigned long io_base;
	int error, i;

	/* allocate device private structure */
	error = alloc_private(dev, sizeof(struct cnt_device_private));
	if (error < 0)
		return error;

	/* Probe the device to determine what device in the series it is. */
	for_each_pci_dev(pci_device) {
		if (pci_device->vendor == PCI_VENDOR_ID_KOLTER) {
			for (i = 0; i < cnt_board_nbr; i++) {
				if (cnt_boards[i].device_id ==
				    pci_device->device) {
					/* was a particular bus/slot requested? */
					if ((it->options[0] != 0)
					    || (it->options[1] != 0)) {
						/* are we on the wrong bus/slot? */
						if (pci_device->bus->number !=
						    it->options[0]
						    ||
						    PCI_SLOT(pci_device->devfn)
						    != it->options[1]) {
							continue;
						}
					}

					dev->board_ptr = cnt_boards + i;
					board =
					    (struct cnt_board_struct *)
					    dev->board_ptr;
					goto found;
				}
			}
		}
	}
	printk(KERN_WARNING
	       "comedi%d: no supported board found! (req. bus/slot: %d/%d)\n",
	       dev->minor, it->options[0], it->options[1]);
	return -EIO;

found:
	printk(KERN_INFO
	       "comedi%d: found %s at PCI bus %d, slot %d\n", dev->minor,
	       board->name, pci_device->bus->number,
	       PCI_SLOT(pci_device->devfn));
	devpriv->pcidev = pci_device;
	dev->board_name = board->name;

	/* enable PCI device and request regions */
	error = comedi_pci_enable(pci_device, CNT_DRIVER_NAME);
	if (error < 0) {
		printk(KERN_WARNING "comedi%d: "
		       "failed to enable PCI device and request regions!\n",
		       dev->minor);
		return error;
	}

	/* read register base address [PCI_BASE_ADDRESS #0] */
	io_base = pci_resource_start(pci_device, 0);
	dev->iobase = io_base;

	/* allocate the subdevice structures */
	error = alloc_subdevices(dev, 1);
	if (error < 0)
		return error;

	subdevice = dev->subdevices + 0;
	dev->read_subdev = subdevice;

	subdevice->type = COMEDI_SUBD_COUNTER;
	subdevice->subdev_flags = SDF_READABLE /* | SDF_COMMON */ ;
	subdevice->n_chan = board->cnt_channel_nbr;
	subdevice->maxdata = (1 << board->cnt_bits) - 1;
	subdevice->insn_read = cnt_rinsn;
	subdevice->insn_write = cnt_winsn;

	/*  select 20MHz clock */
	outb(3, dev->iobase + 248);

	/*  reset all counters */
	outb(0, dev->iobase);
	outb(0, dev->iobase + 0x20);
	outb(0, dev->iobase + 0x40);

	printk(KERN_INFO "comedi%d: " CNT_DRIVER_NAME " attached.\n",
	       dev->minor);
	return 0;
}

/*-- detach -----------------------------------------------------------------*/

static int cnt_detach(struct comedi_device *dev)
{
	if (devpriv && devpriv->pcidev) {
		if (dev->iobase)
			comedi_pci_disable(devpriv->pcidev);
		pci_dev_put(devpriv->pcidev);
	}
	printk(KERN_INFO "comedi%d: " CNT_DRIVER_NAME " remove\n",
	       dev->minor);
	return 0;
}

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
