/*
    comedi/drivers/mpc8260.c
    driver for digital I/O pins on the MPC 8260 CPM module

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000,2001 David A. Schleef <ds@schleef.org>

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
Driver: mpc8260cpm
Description: MPC8260 CPM module generic digital I/O lines
Devices: [Motorola] MPC8260 CPM (mpc8260cpm)
Author: ds
Status: experimental
Updated: Sat, 16 Mar 2002 17:34:48 -0800

This driver is specific to the Motorola MPC8260 processor, allowing
you to access the processor's generic digital I/O lines.

It is apparently missing some code.
*/

#include "../comedidev.h"

extern unsigned long mpc8260_dio_reserved[4];

struct mpc8260cpm_private {

	int data;

};

#define devpriv ((struct mpc8260cpm_private *)dev->private)

static int mpc8260cpm_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int mpc8260cpm_detach(struct comedi_device *dev);
static struct comedi_driver driver_mpc8260cpm = {
      driver_name:"mpc8260cpm",
      module:THIS_MODULE,
      attach:mpc8260cpm_attach,
      detach:mpc8260cpm_detach,
};

COMEDI_INITCLEANUP(driver_mpc8260cpm);

static int mpc8260cpm_dio_config(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data);
static int mpc8260cpm_dio_bits(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data);

static int mpc8260cpm_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int i;

	printk("comedi%d: mpc8260cpm: ", dev->minor);

	dev->board_ptr = mpc8260cpm_boards + dev->board;

	dev->board_name = thisboard->name;

	if (alloc_private(dev, sizeof(struct mpc8260cpm_private)) < 0)
		return -ENOMEM;

	if (alloc_subdevices(dev, 4) < 0)
		return -ENOMEM;

	for (i = 0; i < 4; i++) {
		s = dev->subdevices + i;
		s->type = COMEDI_SUBD_DIO;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->n_chan = 32;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_config = mpc8260cpm_dio_config;
		s->insn_bits = mpc8260cpm_dio_bits;
	}

	return 1;
}

static int mpc8260cpm_detach(struct comedi_device *dev)
{
	printk("comedi%d: mpc8260cpm: remove\n", dev->minor);

	return 0;
}

static unsigned long *cpm_pdat(int port)
{
	switch (port) {
	case 0:
		return &io->iop_pdata;
	case 1:
		return &io->iop_pdatb;
	case 2:
		return &io->iop_pdatc;
	case 3:
		return &io->iop_pdatd;
	}
}

static int mpc8260cpm_dio_config(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	int n;
	unsigned int d;
	unsigned int mask;
	int port;

	port = (int)s->private;
	mask = 1 << CR_CHAN(insn->chanspec);
	if (mask & cpm_reserved_bits[port]) {
		return -EINVAL;
	}

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= mask;
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~mask;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & mask) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
	}

	switch (port) {
	case 0:
		return &io->iop_pdira;
	case 1:
		return &io->iop_pdirb;
	case 2:
		return &io->iop_pdirc;
	case 3:
		return &io->iop_pdird;
	}

	return 1;
}

static int mpc8260cpm_dio_bits(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	int port;
	unsigned long *p;

	p = cpm_pdat((int)s->private);

	return 2;
}
