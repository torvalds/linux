/*
   comedi/drivers/rti802.c
   Hardware driver for Analog Devices RTI-802 board

   COMEDI - Linux Control and Measurement Device Interface
   Copyright (C) 1999 Anders Blomdell <anders.blomdell@control.lth.se>

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
Driver: rti802
Description: Analog Devices RTI-802
Author: Anders Blomdell <anders.blomdell@control.lth.se>
Devices: [Analog Devices] RTI-802 (rti802)
Status: works

Configuration Options:
    [0] - i/o base
    [1] - unused
    [2] - dac#0  0=two's comp, 1=straight
    [3] - dac#0  0=bipolar, 1=unipolar
    [4] - dac#1 ...
    ...
    [17] - dac#7 ...
*/

#include "../comedidev.h"

#include <linux/ioport.h>

#define RTI802_SIZE 4

#define RTI802_SELECT 0
#define RTI802_DATALOW 1
#define RTI802_DATAHIGH 2

struct rti802_private {
	enum {
		dac_2comp, dac_straight
	} dac_coding[8];
	const struct comedi_lrange *range_type_list[8];
	unsigned int ao_readback[8];
};

static int rti802_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct rti802_private *devpriv = dev->private;
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[CR_CHAN(insn->chanspec)];

	return i;
}

static int rti802_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	struct rti802_private *devpriv = dev->private;
	int i, d;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++) {
		d = devpriv->ao_readback[chan] = data[i];
		if (devpriv->dac_coding[chan] == dac_2comp)
			d ^= 0x800;
		outb(chan, dev->iobase + RTI802_SELECT);
		outb(d & 0xff, dev->iobase + RTI802_DATALOW);
		outb(d >> 8, dev->iobase + RTI802_DATAHIGH);
	}
	return i;
}

static int rti802_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct rti802_private *devpriv;
	struct comedi_subdevice *s;
	int i;
	int ret;

	ret = comedi_request_region(dev, it->options[0], RTI802_SIZE);
	if (ret)
		return ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* ao subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->maxdata = 0xfff;
	s->n_chan = 8;
	s->insn_read = rti802_ao_insn_read;
	s->insn_write = rti802_ao_insn_write;
	s->range_table_list = devpriv->range_type_list;

	for (i = 0; i < 8; i++) {
		devpriv->dac_coding[i] = (it->options[3 + 2 * i])
		    ? (dac_straight)
		    : (dac_2comp);
		devpriv->range_type_list[i] = (it->options[2 + 2 * i])
		    ? &range_unipolar10 : &range_bipolar10;
	}

	return 0;
}

static struct comedi_driver rti802_driver = {
	.driver_name	= "rti802",
	.module		= THIS_MODULE,
	.attach		= rti802_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(rti802_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
