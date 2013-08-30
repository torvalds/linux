/*
    comedi/drivers/amplc_pc263.c
    Driver for Amplicon PC263 and PCI263 relay boards.

    Copyright (C) 2002 MEV Ltd. <http://www.mev.co.uk/>

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
*/
/*
Driver: amplc_pc263
Description: Amplicon PC263
Author: Ian Abbott <abbotti@mev.co.uk>
Devices: [Amplicon] PC263 (pc263)
Updated: Fri, 12 Apr 2013 15:19:36 +0100
Status: works

Configuration options:
  [0] - I/O port base address

The board appears as one subdevice, with 16 digital outputs, each
connected to a reed-relay. Relay contacts are closed when output is 1.
The state of the outputs can be read.
*/

#include <linux/module.h>
#include "../comedidev.h"

#define PC263_DRIVER_NAME	"amplc_pc263"

/* PC263 registers */
#define PC263_IO_SIZE	2

/*
 * Board descriptions for Amplicon PC263.
 */

struct pc263_board {
	const char *name;
};

static const struct pc263_board pc263_boards[] = {
	{
		.name = "pc263",
	},
};

static int pc263_do_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
		/* Write out the new digital output lines */
		outb(s->state & 0xFF, dev->iobase);
		outb(s->state >> 8, dev->iobase + 1);
	}

	data[1] = s->state;

	return insn->n;
}

static int pc263_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], PC263_IO_SIZE);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* digital output subdevice */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = pc263_do_insn_bits;
	/* read initial relay state */
	s->state = inb(dev->iobase) | (inb(dev->iobase + 1) << 8);

	dev_info(dev->class_dev, "%s (base %#lx) attached\n", dev->board_name,
		 dev->iobase);
	return 0;
}

static struct comedi_driver amplc_pc263_driver = {
	.driver_name = PC263_DRIVER_NAME,
	.module = THIS_MODULE,
	.attach = pc263_attach,
	.detach = comedi_legacy_detach,
	.board_name = &pc263_boards[0].name,
	.offset = sizeof(struct pc263_board),
	.num_names = ARRAY_SIZE(pc263_boards),
};

module_comedi_driver(amplc_pc263_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Amplicon PC263 relay board");
MODULE_LICENSE("GPL");
