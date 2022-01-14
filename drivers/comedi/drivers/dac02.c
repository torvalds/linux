// SPDX-License-Identifier: GPL-2.0+
/*
 * dac02.c
 * Comedi driver for DAC02 compatible boards
 * Copyright (C) 2014 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on the poc driver
 * Copyright (C) 2000 Frank Mori Hess <fmhess@users.sourceforge.net>
 * Copyright (C) 2001 David A. Schleef <ds@schleef.org>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: dac02
 * Description: Comedi driver for DAC02 compatible boards
 * Devices: [Keithley Metrabyte] DAC-02 (dac02)
 * Author: H Hartley Sweeten <hsweeten@visionengravers.com>
 * Updated: Tue, 11 Mar 2014 11:27:19 -0700
 * Status: unknown
 *
 * Configuration options:
 *	[0] - I/O port base
 */

#include <linux/module.h>
#include <linux/comedi/comedidev.h>

/*
 * The output range is selected by jumpering pins on the I/O connector.
 *
 *	    Range      Chan #   Jumper pins        Output
 *	-------------  ------  -------------  -----------------
 *	   0 to 5V       0        21 to 22      24
 *	                 1        15 to 16      18
 *	   0 to 10V      0        20 to 22      24
 *	                 1        14 to 16      18
 *	    +/-5V        0        21 to 22      23
 *	                 1        15 to 16      17
 *	    +/-10V       0        20 to 22      23
 *	                 1        14 to 16      17
 *	  4 to 20mA      0        21 to 22      25
 *	                 1        15 to 16      19
 *	AC reference     0      In on pin 22    24 (2-quadrant)
 *	                        In on pin 22    23 (4-quadrant)
 *	                 1      In on pin 16    18 (2-quadrant)
 *	                        In on pin 16    17 (4-quadrant)
 */
static const struct comedi_lrange das02_ao_ranges = {
	6, {
		UNI_RANGE(5),
		UNI_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(10),
		RANGE_mA(4, 20),
		RANGE_ext(0, 1)
	}
};

/*
 * Register I/O map
 */
#define DAC02_AO_LSB(x)		(0x00 + ((x) * 2))
#define DAC02_AO_MSB(x)		(0x01 + ((x) * 2))

static int dac02_ao_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val;
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];

		s->readback[chan] = val;

		/*
		 * Unipolar outputs are true binary encoding.
		 * Bipolar outputs are complementary offset binary
		 * (that is, 0 = +full scale, maxdata = -full scale).
		 */
		if (comedi_range_is_bipolar(s, range))
			val = s->maxdata - val;

		/*
		 * DACs are double-buffered.
		 * Write LSB then MSB to latch output.
		 */
		outb((val << 4) & 0xf0, dev->iobase + DAC02_AO_LSB(chan));
		outb((val >> 4) & 0xff, dev->iobase + DAC02_AO_MSB(chan));
	}

	return insn->n;
}

static int dac02_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x08);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	/* Analog Output subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 2;
	s->maxdata	= 0x0fff;
	s->range_table	= &das02_ao_ranges;
	s->insn_write	= dac02_ao_insn_write;

	return comedi_alloc_subdev_readback(s);
}

static struct comedi_driver dac02_driver = {
	.driver_name	= "dac02",
	.module		= THIS_MODULE,
	.attach		= dac02_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(dac02_driver);

MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_DESCRIPTION("Comedi driver for DAC02 compatible boards");
MODULE_LICENSE("GPL");
