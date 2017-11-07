// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi/drivers/dt2815.c
 * Hardware driver for Data Translation DT2815
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1999 Anders Blomdell <anders.blomdell@control.lth.se>
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
 * Driver: dt2815
 * Description: Data Translation DT2815
 * Author: ds
 * Status: mostly complete, untested
 * Devices: [Data Translation] DT2815 (dt2815)
 *
 * I'm not sure anyone has ever tested this board.  If you have information
 * contrary, please update.
 *
 * Configuration options:
 * [0] - I/O port base base address
 * [1] - IRQ (unused)
 * [2] - Voltage unipolar/bipolar configuration
 *	0 == unipolar 5V  (0V -- +5V)
 *	1 == bipolar 5V  (-5V -- +5V)
 * [3] - Current offset configuration
 *	0 == disabled  (0mA -- +32mAV)
 *	1 == enabled  (+4mA -- +20mAV)
 * [4] - Firmware program configuration
 *	0 == program 1 (see manual table 5-4)
 *	1 == program 2 (see manual table 5-4)
 *	2 == program 3 (see manual table 5-4)
 *	3 == program 4 (see manual table 5-4)
 * [5] - Analog output 0 range configuration
 *	0 == voltage
 *	1 == current
 * [6] - Analog output 1 range configuration (same options)
 * [7] - Analog output 2 range configuration (same options)
 * [8] - Analog output 3 range configuration (same options)
 * [9] - Analog output 4 range configuration (same options)
 * [10] - Analog output 5 range configuration (same options)
 * [11] - Analog output 6 range configuration (same options)
 * [12] - Analog output 7 range configuration (same options)
 */

#include <linux/module.h>
#include "../comedidev.h"

#include <linux/delay.h>

#define DT2815_DATA 0
#define DT2815_STATUS 1

struct dt2815_private {
	const struct comedi_lrange *range_type_list[8];
	unsigned int ao_readback[8];
};

static int dt2815_ao_status(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn,
			    unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DT2815_STATUS);
	if (status == context)
		return 0;
	return -EBUSY;
}

static int dt2815_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct dt2815_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

static int dt2815_ao_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	struct dt2815_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);
	unsigned int lo, hi;
	int ret;

	for (i = 0; i < insn->n; i++) {
		lo = ((data[i] & 0x0f) << 4) | (chan << 1) | 0x01;
		hi = (data[i] & 0xff0) >> 4;

		ret = comedi_timeout(dev, s, insn, dt2815_ao_status, 0x00);
		if (ret)
			return ret;

		outb(lo, dev->iobase + DT2815_DATA);

		ret = comedi_timeout(dev, s, insn, dt2815_ao_status, 0x10);
		if (ret)
			return ret;

		devpriv->ao_readback[chan] = data[i];
	}
	return i;
}

/*
 * options[0]   Board base address
 * options[1]   IRQ (not applicable)
 * options[2]   Voltage unipolar/bipolar configuration
 *		0 == unipolar 5V  (0V -- +5V)
 *		1 == bipolar 5V  (-5V -- +5V)
 * options[3]   Current offset configuration
 *		0 == disabled  (0mA -- +32mAV)
 *		1 == enabled  (+4mA -- +20mAV)
 * options[4]   Firmware program configuration
 *		0 == program 1 (see manual table 5-4)
 *		1 == program 2 (see manual table 5-4)
 *		2 == program 3 (see manual table 5-4)
 *		3 == program 4 (see manual table 5-4)
 * options[5]   Analog output 0 range configuration
 *		0 == voltage
 *		1 == current
 * options[6]   Analog output 1 range configuration
 * ...
 * options[12]   Analog output 7 range configuration
 *		0 == voltage
 *		1 == current
 */

static int dt2815_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct dt2815_private *devpriv;
	struct comedi_subdevice *s;
	int i;
	const struct comedi_lrange *current_range_type, *voltage_range_type;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x2);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	s = &dev->subdevices[0];
	/* ao subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->maxdata = 0xfff;
	s->n_chan = 8;
	s->insn_write = dt2815_ao_insn;
	s->insn_read = dt2815_ao_insn_read;
	s->range_table_list = devpriv->range_type_list;

	current_range_type = (it->options[3])
	    ? &range_4_20mA : &range_0_32mA;
	voltage_range_type = (it->options[2])
	    ? &range_bipolar5 : &range_unipolar5;
	for (i = 0; i < 8; i++) {
		devpriv->range_type_list[i] = (it->options[5 + i])
		    ? current_range_type : voltage_range_type;
	}

	/* Init the 2815 */
	outb(0x00, dev->iobase + DT2815_STATUS);
	for (i = 0; i < 100; i++) {
		/* This is incredibly slow (approx 20 ms) */
		unsigned int status;

		usleep_range(1000, 3000);
		status = inb(dev->iobase + DT2815_STATUS);
		if (status == 4) {
			unsigned int program;

			program = (it->options[4] & 0x3) << 3 | 0x7;
			outb(program, dev->iobase + DT2815_DATA);
			dev_dbg(dev->class_dev, "program: 0x%x (@t=%d)\n",
				program, i);
			break;
		} else if (status != 0x00) {
			dev_dbg(dev->class_dev,
				"unexpected status 0x%x (@t=%d)\n",
				status, i);
			if (status & 0x60)
				outb(0x00, dev->iobase + DT2815_STATUS);
		}
	}

	return 0;
}

static struct comedi_driver dt2815_driver = {
	.driver_name	= "dt2815",
	.module		= THIS_MODULE,
	.attach		= dt2815_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(dt2815_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
