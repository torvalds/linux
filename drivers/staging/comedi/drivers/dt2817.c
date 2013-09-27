/*
    comedi/drivers/dt2817.c
    Hardware driver for Data Translation DT2817

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998 David A. Schleef <ds@schleef.org>

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
Driver: dt2817
Description: Data Translation DT2817
Author: ds
Status: complete
Devices: [Data Translation] DT2817 (dt2817)

A very simple digital I/O card.  Four banks of 8 lines, each bank
is configurable for input or output.  One wonders why it takes a
50 page manual to describe this thing.

The driver (which, btw, is much less than 50 pages) has 1 subdevice
with 32 channels, configurable in groups of 8.

Configuration options:
  [0] - I/O port base base address
*/

#include <linux/module.h>
#include "../comedidev.h"

#define DT2817_SIZE 5

#define DT2817_CR 0
#define DT2817_DATA 1

static int dt2817_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int oe = 0;
	unsigned int mask;
	int ret;

	if (chan < 8)
		mask = 0x000000ff;
	else if (chan < 16)
		mask = 0x0000ff00;
	else if (chan < 24)
		mask = 0x00ff0000;
	else
		mask = 0xff000000;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	if (s->io_bits & 0x000000ff)
		oe |= 0x1;
	if (s->io_bits & 0x0000ff00)
		oe |= 0x2;
	if (s->io_bits & 0x00ff0000)
		oe |= 0x4;
	if (s->io_bits & 0xff000000)
		oe |= 0x8;

	outb(oe, dev->iobase + DT2817_CR);

	return insn->n;
}

static int dt2817_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	unsigned int changed;

	/* It's questionable whether it is more important in
	 * a driver like this to be deterministic or fast.
	 * We choose fast. */

	if (data[0]) {
		changed = s->state;
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);
		changed ^= s->state;
		changed &= s->io_bits;
		if (changed & 0x000000ff)
			outb(s->state & 0xff, dev->iobase + DT2817_DATA + 0);
		if (changed & 0x0000ff00)
			outb((s->state >> 8) & 0xff,
			     dev->iobase + DT2817_DATA + 1);
		if (changed & 0x00ff0000)
			outb((s->state >> 16) & 0xff,
			     dev->iobase + DT2817_DATA + 2);
		if (changed & 0xff000000)
			outb((s->state >> 24) & 0xff,
			     dev->iobase + DT2817_DATA + 3);
	}
	data[1] = inb(dev->iobase + DT2817_DATA + 0);
	data[1] |= (inb(dev->iobase + DT2817_DATA + 1) << 8);
	data[1] |= (inb(dev->iobase + DT2817_DATA + 2) << 16);
	data[1] |= (inb(dev->iobase + DT2817_DATA + 3) << 24);

	return insn->n;
}

static int dt2817_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	int ret;
	struct comedi_subdevice *s;

	ret = comedi_request_region(dev, it->options[0], DT2817_SIZE);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];

	s->n_chan = 32;
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->range_table = &range_digital;
	s->maxdata = 1;
	s->insn_bits = dt2817_dio_insn_bits;
	s->insn_config = dt2817_dio_insn_config;

	s->state = 0;
	outb(0, dev->iobase + DT2817_CR);

	return 0;
}

static struct comedi_driver dt2817_driver = {
	.driver_name	= "dt2817",
	.module		= THIS_MODULE,
	.attach		= dt2817_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(dt2817_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
