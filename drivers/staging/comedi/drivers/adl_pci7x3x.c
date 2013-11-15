/*
 * COMEDI driver for the ADLINK PCI-723x/743x series boards.
 * Copyright (C) 2012 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on the adl_pci7230 driver written by:
 *	David Fernandez <dfcastelao@gmail.com>
 * and the adl_pci7432 driver written by:
 *	Michel Lachaine <mike@mikelachaine.ca>
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
Driver: adl_pci7x3x
Description: 32/64-Channel Isolated Digital I/O Boards
Devices: (ADLink) PCI-7230 [adl_pci7230] - 16 input / 16 output
	 (ADLink) PCI-7233 [adl_pci7233] - 32 input
	 (ADLink) PCI-7234 [adl_pci7234] - 32 output
	 (ADLink) PCI-7432 [adl_pci7432] - 32 input / 32 output
	 (ADLink) PCI-7433 [adl_pci7433] - 64 input
	 (ADLink) PCI-7434 [adl_pci7434] - 64 output
Author: H Hartley Sweeten <hsweeten@visionengravers.com>
Updated: Thu, 02 Aug 2012 14:27:46 -0700
Status: untested

The PCI-7230, PCI-7432 and PCI-7433 boards also support external
interrupt signals on digital input channels 0 and 1. The PCI-7233
has dual-interrupt sources for change-of-state (COS) on any 16
digital input channels of LSB and for COS on any 16 digital input
lines of MSB. Interrupts are not currently supported by this
driver.

Configuration Options: not applicable, uses comedi PCI auto config
*/

#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"

/*
 * Register I/O map (32-bit access only)
 */
#define PCI7X3X_DIO_REG		0x00
#define PCI743X_DIO_REG		0x04

enum apci1516_boardid {
	BOARD_PCI7230,
	BOARD_PCI7233,
	BOARD_PCI7234,
	BOARD_PCI7432,
	BOARD_PCI7433,
	BOARD_PCI7434,
};

struct adl_pci7x3x_boardinfo {
	const char *name;
	int nsubdevs;
	int di_nchan;
	int do_nchan;
};

static const struct adl_pci7x3x_boardinfo adl_pci7x3x_boards[] = {
	[BOARD_PCI7230] = {
		.name		= "adl_pci7230",
		.nsubdevs	= 2,
		.di_nchan	= 16,
		.do_nchan	= 16,
	},
	[BOARD_PCI7233] = {
		.name		= "adl_pci7233",
		.nsubdevs	= 1,
		.di_nchan	= 32,
	},
	[BOARD_PCI7234] = {
		.name		= "adl_pci7234",
		.nsubdevs	= 1,
		.do_nchan	= 32,
	},
	[BOARD_PCI7432] = {
		.name		= "adl_pci7432",
		.nsubdevs	= 2,
		.di_nchan	= 32,
		.do_nchan	= 32,
	},
	[BOARD_PCI7433] = {
		.name		= "adl_pci7433",
		.nsubdevs	= 2,
		.di_nchan	= 64,
	},
	[BOARD_PCI7434] = {
		.name		= "adl_pci7434",
		.nsubdevs	= 2,
		.do_nchan	= 64,
	}
};

static int adl_pci7x3x_do_insn_bits(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	unsigned long reg = (unsigned long)s->private;
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outl(s->state, dev->iobase + reg);
	}

	/*
	 * NOTE: The output register is not readable.
	 * This returned state will not be correct until all the
	 * outputs have been updated.
	 */
	data[1] = s->state;

	return insn->n;
}

static int adl_pci7x3x_di_insn_bits(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	unsigned long reg = (unsigned long)s->private;

	data[1] = inl(dev->iobase + reg);

	return insn->n;
}

static int adl_pci7x3x_auto_attach(struct comedi_device *dev,
				   unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct adl_pci7x3x_boardinfo *board = NULL;
	struct comedi_subdevice *s;
	int subdev;
	int nchan;
	int ret;

	if (context < ARRAY_SIZE(adl_pci7x3x_boards))
		board = &adl_pci7x3x_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 2);

	/*
	 * One or two subdevices are setup by this driver depending on
	 * the number of digital inputs and/or outputs provided by the
	 * board. Each subdevice has a maximum of 32 channels.
	 *
	 *	PCI-7230 - 2 subdevices: 0 - 16 input, 1 - 16 output
	 *	PCI-7233 - 1 subdevice: 0 - 32 input
	 *	PCI-7234 - 1 subdevice: 0 - 32 output
	 *	PCI-7432 - 2 subdevices: 0 - 32 input, 1 - 32 output
	 *	PCI-7433 - 2 subdevices: 0 - 32 input, 1 - 32 input
	 *	PCI-7434 - 2 subdevices: 0 - 32 output, 1 - 32 output
	 */
	ret = comedi_alloc_subdevices(dev, board->nsubdevs);
	if (ret)
		return ret;

	subdev = 0;

	if (board->di_nchan) {
		nchan = min(board->di_nchan, 32);

		s = &dev->subdevices[subdev];
		/* Isolated digital inputs 0 to 15/31 */
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= nchan;
		s->maxdata	= 1;
		s->insn_bits	= adl_pci7x3x_di_insn_bits;
		s->range_table	= &range_digital;

		s->private	= (void *)PCI7X3X_DIO_REG;

		subdev++;

		nchan = board->di_nchan - nchan;
		if (nchan) {
			s = &dev->subdevices[subdev];
			/* Isolated digital inputs 32 to 63 */
			s->type		= COMEDI_SUBD_DI;
			s->subdev_flags	= SDF_READABLE;
			s->n_chan	= nchan;
			s->maxdata	= 1;
			s->insn_bits	= adl_pci7x3x_di_insn_bits;
			s->range_table	= &range_digital;

			s->private	= (void *)PCI743X_DIO_REG;

			subdev++;
		}
	}

	if (board->do_nchan) {
		nchan = min(board->do_nchan, 32);

		s = &dev->subdevices[subdev];
		/* Isolated digital outputs 0 to 15/31 */
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= nchan;
		s->maxdata	= 1;
		s->insn_bits	= adl_pci7x3x_do_insn_bits;
		s->range_table	= &range_digital;

		s->private	= (void *)PCI7X3X_DIO_REG;

		subdev++;

		nchan = board->do_nchan - nchan;
		if (nchan) {
			s = &dev->subdevices[subdev];
			/* Isolated digital outputs 32 to 63 */
			s->type		= COMEDI_SUBD_DO;
			s->subdev_flags	= SDF_WRITABLE;
			s->n_chan	= nchan;
			s->maxdata	= 1;
			s->insn_bits	= adl_pci7x3x_do_insn_bits;
			s->range_table	= &range_digital;

			s->private	= (void *)PCI743X_DIO_REG;

			subdev++;
		}
	}

	dev_info(dev->class_dev, "%s attached (%d inputs/%d outputs)\n",
		dev->board_name, board->di_nchan, board->do_nchan);

	return 0;
}

static struct comedi_driver adl_pci7x3x_driver = {
	.driver_name	= "adl_pci7x3x",
	.module		= THIS_MODULE,
	.auto_attach	= adl_pci7x3x_auto_attach,
	.detach		= comedi_pci_disable,
};

static int adl_pci7x3x_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &adl_pci7x3x_driver,
				      id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(adl_pci7x3x_pci_table) = {
	{ PCI_VDEVICE(ADLINK, 0x7230), BOARD_PCI7230 },
	{ PCI_VDEVICE(ADLINK, 0x7233), BOARD_PCI7233 },
	{ PCI_VDEVICE(ADLINK, 0x7234), BOARD_PCI7234 },
	{ PCI_VDEVICE(ADLINK, 0x7432), BOARD_PCI7432 },
	{ PCI_VDEVICE(ADLINK, 0x7433), BOARD_PCI7433 },
	{ PCI_VDEVICE(ADLINK, 0x7434), BOARD_PCI7434 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adl_pci7x3x_pci_table);

static struct pci_driver adl_pci7x3x_pci_driver = {
	.name		= "adl_pci7x3x",
	.id_table	= adl_pci7x3x_pci_table,
	.probe		= adl_pci7x3x_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(adl_pci7x3x_driver, adl_pci7x3x_pci_driver);

MODULE_DESCRIPTION("ADLINK PCI-723x/743x Isolated Digital I/O boards");
MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_LICENSE("GPL");
