/*
    comedi/drivers/poc.c
    Mini-drivers for POC (Piece of Crap) boards
    Copyright (C) 2000 Frank Mori Hess <fmhess@users.sourceforge.net>
    Copyright (C) 2001 David A. Schleef <ds@schleef.org>

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
Driver: poc
Description: Generic driver for very simple devices
Author: ds
Devices: [Keithley Metrabyte] DAC-02 (dac02)
Updated: Sat, 16 Mar 2002 17:34:48 -0800
Status: unknown

This driver is indended to support very simple ISA-based devices,
including:
  dac02 - Keithley DAC-02 analog output board

Configuration options:
  [0] - I/O port base
*/

#include <linux/module.h>
#include "../comedidev.h"

struct boarddef_struct {
	const char *name;
	unsigned int iosize;
	int (*setup) (struct comedi_device *);
	int type;
	int n_chan;
	int n_bits;
	int (*winsn) (struct comedi_device *, struct comedi_subdevice *,
		      struct comedi_insn *, unsigned int *);
	int (*rinsn) (struct comedi_device *, struct comedi_subdevice *,
		      struct comedi_insn *, unsigned int *);
	int (*insnbits) (struct comedi_device *, struct comedi_subdevice *,
			 struct comedi_insn *, unsigned int *);
	const struct comedi_lrange *range;
};

struct poc_private {
	unsigned int ao_readback[32];
};

static int readback_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	struct poc_private *devpriv = dev->private;
	int chan;

	chan = CR_CHAN(insn->chanspec);
	data[0] = devpriv->ao_readback[chan];

	return 1;
}

/* DAC-02 registers */
#define DAC02_LSB(a)	(2 * a)
#define DAC02_MSB(a)	(2 * a + 1)

static int dac02_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	struct poc_private *devpriv = dev->private;
	int temp;
	int chan;
	int output;

	chan = CR_CHAN(insn->chanspec);
	devpriv->ao_readback[chan] = data[0];
	output = data[0];
#ifdef wrong
	/*  convert to complementary binary if range is bipolar */
	if ((CR_RANGE(insn->chanspec) & 0x2) == 0)
		output = ~output;
#endif
	temp = (output << 4) & 0xf0;
	outb(temp, dev->iobase + DAC02_LSB(chan));
	temp = (output >> 4) & 0xff;
	outb(temp, dev->iobase + DAC02_MSB(chan));

	return 1;
}

static int poc_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct boarddef_struct *board = comedi_board(dev);
	struct poc_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], board->iosize);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	/* analog output subdevice */
	s = &dev->subdevices[0];
	s->type = board->type;
	s->n_chan = board->n_chan;
	s->maxdata = (1 << board->n_bits) - 1;
	s->range_table = board->range;
	s->insn_write = board->winsn;
	s->insn_read = board->rinsn;
	s->insn_bits = board->insnbits;
	if (s->type == COMEDI_SUBD_AO || s->type == COMEDI_SUBD_DO)
		s->subdev_flags = SDF_WRITABLE;

	return 0;
}

static const struct boarddef_struct boards[] = {
	{
		.name		= "dac02",
		.iosize		= 8,
		/* .setup	= dac02_setup, */
		.type		= COMEDI_SUBD_AO,
		.n_chan		= 2,
		.n_bits		= 12,
		.winsn		= dac02_ao_winsn,
		.rinsn		= readback_insn,
		.range		= &range_unknown,
	},
};

static struct comedi_driver poc_driver = {
	.driver_name	= "poc",
	.module		= THIS_MODULE,
	.attach		= poc_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &boards[0].name,
	.num_names	= ARRAY_SIZE(boards),
	.offset		= sizeof(boards[0]),
};
module_comedi_driver(poc_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
