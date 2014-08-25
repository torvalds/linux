/*
 * pcmda12.c
 * Driver for Winsystems PC-104 based PCM-D/A-12 8-channel AO board.
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2006 Calin A. Culianu <calin@ajvar.org>
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
 * Driver: pcmda12
 * Description: A driver for the Winsystems PCM-D/A-12
 * Devices: (Winsystems) PCM-D/A-12 [pcmda12]
 * Author: Calin Culianu <calin@ajvar.org>
 * Updated: Fri, 13 Jan 2006 12:01:01 -0500
 * Status: works
 *
 * A driver for the relatively straightforward-to-program PCM-D/A-12.
 * This board doesn't support commands, and the only way to set its
 * analog output range is to jumper the board. As such,
 * comedi_data_write() ignores the range value specified.
 *
 * The board uses 16 consecutive I/O addresses starting at the I/O port
 * base address. Each address corresponds to the LSB then MSB of a
 * particular channel from 0-7.
 *
 * Note that the board is not ISA-PNP capable and thus needs the I/O
 * port comedi_config parameter.
 *
 * Note that passing a nonzero value as the second config option will
 * enable "simultaneous xfer" mode for this board, in which AO writes
 * will not take effect until a subsequent read of any AO channel. This
 * is so that one can speed up programming by preloading all AO registers
 * with values before simultaneously setting them to take effect with one
 * read command.
 *
 * Configuration Options:
 *   [0] - I/O port base address
 *   [1] - Do Simultaneous Xfer (see description)
 */

#include <linux/module.h>
#include "../comedidev.h"

/* AI range is not configurable, it's set by jumpers on the board */
static const struct comedi_lrange pcmda12_ranges = {
	3, {
		UNI_RANGE(5),
		UNI_RANGE(10),
		BIP_RANGE(5)
	}
};

struct pcmda12_private {
	int simultaneous_xfer_mode;
};

static int pcmda12_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct pcmda12_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	unsigned long ioreg = dev->iobase + (chan * 2);
	int i;

	for (i = 0; i < insn->n; ++i) {
		val = data[i];
		outb(val & 0xff, ioreg);
		outb((val >> 8) & 0xff, ioreg + 1);

		/*
		 * Initiate transfer if not in simultaneaous xfer
		 * mode by reading one of the AO registers.
		 */
		if (!devpriv->simultaneous_xfer_mode)
			inb(ioreg);
	}
	s->readback[chan] = val;

	return insn->n;
}

static int pcmda12_ao_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct pcmda12_private *devpriv = dev->private;

	/*
	 * Initiate simultaneaous xfer mode by reading one of the
	 * AO registers. All analog outputs will then be updated.
	 */
	if (devpriv->simultaneous_xfer_mode)
		inb(dev->iobase);

	return comedi_readback_insn_read(dev, s, insn, data);
}

static void pcmda12_ao_reset(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	int i;

	for (i = 0; i < s->n_chan; ++i) {
		outb(0, dev->iobase + (i * 2));
		outb(0, dev->iobase + (i * 2) + 1);
	}
	/* Initiate transfer by reading one of the AO registers. */
	inb(dev->iobase);
}

static int pcmda12_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct pcmda12_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	devpriv->simultaneous_xfer_mode = it->options[1];

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 0x0fff;
	s->range_table	= &pcmda12_ranges;
	s->insn_write	= pcmda12_ao_insn_write;
	s->insn_read	= pcmda12_ao_insn_read;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	pcmda12_ao_reset(dev, s);

	return 0;
}

static struct comedi_driver pcmda12_driver = {
	.driver_name	= "pcmda12",
	.module		= THIS_MODULE,
	.attach		= pcmda12_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(pcmda12_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
