// SPDX-License-Identifier: GPL-2.0+
/*
 * rti802.c
 * Comedi driver for Analog Devices RTI-802 board
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1999 Anders Blomdell <anders.blomdell@control.lth.se>
 */

/*
 * Driver: rti802
 * Description: Analog Devices RTI-802
 * Author: Anders Blomdell <anders.blomdell@control.lth.se>
 * Devices: [Analog Devices] RTI-802 (rti802)
 * Status: works
 *
 * Configuration Options:
 *   [0] - i/o base
 *   [1] - unused
 *   [2,4,6,8,10,12,14,16] - dac#[0-7]  0=two's comp, 1=straight
 *   [3,5,7,9,11,13,15,17] - dac#[0-7]  0=bipolar, 1=unipolar
 */

#include <linux/module.h>
#include "../comedidev.h"

/*
 * Register I/O map
 */
#define RTI802_SELECT		0x00
#define RTI802_DATALOW		0x01
#define RTI802_DATAHIGH		0x02

struct rti802_private {
	enum {
		dac_2comp, dac_straight
	} dac_coding[8];
	const struct comedi_lrange *range_type_list[8];
};

static int rti802_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct rti802_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	outb(chan, dev->iobase + RTI802_SELECT);

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];

		s->readback[chan] = val;

		/* munge offset binary to two's complement if needed */
		if (devpriv->dac_coding[chan] == dac_2comp)
			val = comedi_offset_munge(s, val);

		outb(val & 0xff, dev->iobase + RTI802_DATALOW);
		outb((val >> 8) & 0xff, dev->iobase + RTI802_DATAHIGH);
	}

	return insn->n;
}

static int rti802_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct rti802_private *devpriv;
	struct comedi_subdevice *s;
	int i;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x04);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	/* Analog Output subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->maxdata	= 0xfff;
	s->n_chan	= 8;
	s->insn_write	= rti802_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	s->range_table_list = devpriv->range_type_list;
	for (i = 0; i < 8; i++) {
		devpriv->dac_coding[i] = (it->options[3 + 2 * i])
			? (dac_straight) : (dac_2comp);
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

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Analog Devices RTI-802 board");
MODULE_LICENSE("GPL");
