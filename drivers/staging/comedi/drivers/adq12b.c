/*
    comedi/drivers/adq12b.c
    driver for MicroAxial ADQ12-B data acquisition and control card

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
Driver: adq12b
Description: driver for MicroAxial ADQ12-B data acquisition and control card
Devices: [MicroAxial] ADQ12-B (adq12b)
Author: jeremy theler <thelerg@ib.cnea.gov.ar>
Updated: Thu, 21 Feb 2008 02:56:27 -0300
Status: works

Driver for the acquisition card ADQ12-B (without any add-on).

 - Analog input is subdevice 0 (16 channels single-ended or 8 differential)
 - Digital input is subdevice 1 (5 channels)
 - Digital output is subdevice 1 (8 channels)
 - The PACER is not supported in this version

If you do not specify any options, they will default to

  # comedi_config /dev/comedi0 adq12b 0x300,0,0

  option 1: I/O base address. The following table is provided as a help
   of the hardware jumpers.

	 address            jumper JADR
	  0x300                 1 (factory default)
	  0x320                 2
	  0x340                 3
	  0x360                 4
	  0x380                 5
	  0x3A0                 6

  option 2: unipolar/bipolar ADC selection: 0 -> bipolar, 1 -> unipolar

	selection         comedi_config option            JUB
	 bipolar                0                         2-3 (factory default)
	 unipolar               1                         1-2

  option 3: single-ended/differential AI selection: 0 -> SE, 1 -> differential

	selection         comedi_config option     JCHA    JCHB
       single-ended             0                  1-2     1-2 (factory default)
       differential             1                  2-3     2-3

   written by jeremy theler <thelerg@ib.cnea.gov.ar>

   instituto balseiro
   commission nacional de energia atomica
   universidad nacional de cuyo
   argentina

   21-feb-2008
     + changed supported devices string (missused the [] and ())

   13-oct-2007
     + first try
*/

#include <linux/module.h>
#include <linux/delay.h>

#include "../comedidev.h"

/* address scheme (page 2.17 of the manual) */
#define ADQ12B_CTREG		0x00
#define ADQ12B_CTREG_MSKP	(1 << 7)	/* enable pacer interrupt */
#define ADQ12B_CTREG_GTP	(1 << 6)	/* enable pacer */
#define ADQ12B_CTREG_RANGE(x)	((x) << 4)
#define ADQ12B_CTREG_CHAN(x)	((x) << 0)
#define ADQ12B_STINR		0x00
#define ADQ12B_STINR_OUT2	(1 << 7)	/* timer 2 output state */
#define ADQ12B_STINR_OUTP	(1 << 6)	/* pacer output state */
#define ADQ12B_STINR_EOC	(1 << 5)	/* A/D end-of-conversion */
#define ADQ12B_STINR_IN_MASK	(0x1f << 0)
#define ADQ12B_OUTBR		0x04
#define ADQ12B_ADLOW		0x08
#define ADQ12B_ADHIG		0x09
#define ADQ12B_TIMER_BASE	0x0c

/* available ranges through the PGA gains */
static const struct comedi_lrange range_adq12b_ai_bipolar = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(2),
		BIP_RANGE(1),
		BIP_RANGE(0.5)
	}
};

static const struct comedi_lrange range_adq12b_ai_unipolar = {
	4, {
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1),
		UNI_RANGE(0.5)
	}
};

struct adq12b_private {
	unsigned int last_ctreg;
};

static int adq12b_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned char status;

	status = inb(dev->iobase + ADQ12B_STINR);
	if (status & ADQ12B_STINR_EOC)
		return 0;
	return -EBUSY;
}

static int adq12b_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct adq12b_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val;
	int ret;
	int i;

	/* change channel and range only if it is different from the previous */
	val = ADQ12B_CTREG_RANGE(range) | ADQ12B_CTREG_CHAN(chan);
	if (val != devpriv->last_ctreg) {
		outb(val, dev->iobase + ADQ12B_CTREG);
		devpriv->last_ctreg = val;
		udelay(50);	/* wait for the mux to settle */
	}

	val = inb(dev->iobase + ADQ12B_ADLOW);	/* trigger A/D */

	for (i = 0; i < insn->n; i++) {
		ret = comedi_timeout(dev, s, insn, adq12b_ai_eoc, 0);
		if (ret)
			return ret;

		val = inb(dev->iobase + ADQ12B_ADHIG) << 8;
		val |= inb(dev->iobase + ADQ12B_ADLOW);	/* retriggers A/D */

		data[i] = val;
	}

	return insn->n;
}

static int adq12b_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	/* only bits 0-4 have information about digital inputs */
	data[1] = (inb(dev->iobase + ADQ12B_STINR) & ADQ12B_STINR_IN_MASK);

	return insn->n;
}

static int adq12b_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int mask;
	unsigned int chan;
	unsigned int val;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		for (chan = 0; chan < 8; chan++) {
			if ((mask >> chan) & 0x01) {
				val = (s->state >> chan) & 0x01;
				outb((val << 3) | chan,
				     dev->iobase + ADQ12B_OUTBR);
			}
		}
	}

	data[1] = s->state;

	return insn->n;
}

static int adq12b_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct adq12b_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	devpriv->last_ctreg = -1;	/* force ctreg update */

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	if (it->options[2]) {
		s->subdev_flags	= SDF_READABLE | SDF_DIFF;
		s->n_chan	= 8;
	} else {
		s->subdev_flags	= SDF_READABLE | SDF_GROUND;
		s->n_chan	= 16;
	}
	s->maxdata	= 0xfff;
	s->range_table	= it->options[1] ? &range_adq12b_ai_unipolar
					 : &range_adq12b_ai_bipolar;
	s->insn_read	= adq12b_ai_insn_read;

	/* Digital Input subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 5;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= adq12b_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= adq12b_do_insn_bits;

	return 0;
}

static struct comedi_driver adq12b_driver = {
	.driver_name	= "adq12b",
	.module		= THIS_MODULE,
	.attach		= adq12b_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(adq12b_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
