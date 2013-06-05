/*
 * pcmad.c
 * Hardware driver for Winsystems PCM-A/D12 and PCM-A/D16
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000,2001 David A. Schleef <ds@schleef.org>
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
 * Driver: pcmad
 * Description: Winsystems PCM-A/D12, PCM-A/D16
 * Devices: (Winsystems) PCM-A/D12 [pcmad12]
 *	    (Winsystems) PCM-A/D16 [pcmad16]
 * Author: ds
 * Status: untested
 *
 * This driver was written on a bet that I couldn't write a driver
 * in less than 2 hours.  I won the bet, but never got paid.  =(
 *
 * Configuration options:
 *   [0] - I/O port base
 *   [1] - IRQ (unused)
 *   [2] - Analog input reference
 *	   0 = single ended
 *	   1 = differential
 *   [3] - Analog input encoding (must match jumpers)
 *	   0 = straight binary
 *	   1 = two's complement
 */

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/ioport.h>

#define PCMAD_STATUS		0
#define PCMAD_LSB		1
#define PCMAD_MSB		2
#define PCMAD_CONVERT		1

struct pcmad_board_struct {
	const char *name;
	unsigned int ai_maxdata;
};

static const struct pcmad_board_struct pcmad_boards[] = {
	{
		.name		= "pcmad12",
		.ai_maxdata	= 0x0fff,
	}, {
		.name		= "pcmad16",
		.ai_maxdata	= 0xffff,
	},
};

struct pcmad_priv_struct {
	int differential;
	int twos_comp;
};

#define TIMEOUT	100

static int pcmad_ai_wait_for_eoc(struct comedi_device *dev,
				 int timeout)
{
	int i;

	for (i = 0; i < timeout; i++) {
		if ((inb(dev->iobase + PCMAD_STATUS) & 0x3) == 0x3)
			return 0;
	}
	return -ETIME;
}

static int pcmad_ai_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct pcmad_priv_struct *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val;
	int ret;
	int i;

	for (i = 0; i < insn->n; i++) {
		outb(chan, dev->iobase + PCMAD_CONVERT);

		ret = pcmad_ai_wait_for_eoc(dev, TIMEOUT);
		if (ret)
			return ret;

		val = inb(dev->iobase + PCMAD_LSB) |
		      (inb(dev->iobase + PCMAD_MSB) << 8);

		if (devpriv->twos_comp)
			val ^= ((s->maxdata + 1) >> 1);

		data[i] = val;
	}

	return insn->n;
}

static int pcmad_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pcmad_board_struct *board = comedi_board(dev);
	struct pcmad_priv_struct *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x04);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | AREF_GROUND;
	s->n_chan	= 16;
	s->len_chanlist	= 1;
	s->maxdata	= board->ai_maxdata;
	s->range_table	= &range_unknown;
	s->insn_read	= pcmad_ai_insn_read;

	return 0;
}

static struct comedi_driver pcmad_driver = {
	.driver_name	= "pcmad",
	.module		= THIS_MODULE,
	.attach		= pcmad_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &pcmad_boards[0].name,
	.num_names	= ARRAY_SIZE(pcmad_boards),
	.offset		= sizeof(pcmad_boards[0]),
};
module_comedi_driver(pcmad_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
