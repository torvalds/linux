/*
 * comedi/drivers/rti800.c
 * Hardware driver for Analog Devices RTI-800/815 board
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998 David A. Schleef <ds@schleef.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Driver: rti800
 * Description: Analog Devices RTI-800/815
 * Devices: (Analog Devices) RTI-800 [rti800]
 *	    (Analog Devices) RTI-815 [rti815]
 * Author: David A. Schleef <ds@schleef.org>
 * Status: unknown
 * Updated: Fri, 05 Sep 2008 14:50:44 +0100
 *
 * Configuration options:
 *   [0] - I/O port base address
 *   [1] - IRQ (not supported / unused)
 *   [2] - A/D mux/reference (number of channels)
 *	   0 = differential
 *	   1 = pseudodifferential (common)
 *	   2 = single-ended
 *   [3] - A/D range
 *	   0 = [-10,10]
 *	   1 = [-5,5]
 *	   2 = [0,10]
 *   [4] - A/D encoding
 *	   0 = two's complement
 *	   1 = straight binary
 *   [5] - DAC 0 range
 *	   0 = [-10,10]
 *	   1 = [0,10]
 *   [6] - DAC 0 encoding
 *	   0 = two's complement
 *	   1 = straight binary
 *   [7] - DAC 1 range (same as DAC 0)
 *   [8] - DAC 1 encoding (same as DAC 0)
 */

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/ioport.h>

/*
 * Register map
 */
#define RTI800_CSR		0x00
#define RTI800_CSR_BUSY		(1 << 7)
#define RTI800_CSR_DONE		(1 << 6)
#define RTI800_CSR_OVERRUN	(1 << 5)
#define RTI800_CSR_TCR		(1 << 4)
#define RTI800_CSR_DMA_ENAB	(1 << 3)
#define RTI800_CSR_INTR_TC	(1 << 2)
#define RTI800_CSR_INTR_EC	(1 << 1)
#define RTI800_CSR_INTR_OVRN	(1 << 0)
#define RTI800_MUXGAIN		0x01
#define RTI800_CONVERT		0x02
#define RTI800_ADCLO		0x03
#define RTI800_ADCHI		0x04
#define RTI800_DAC0LO		0x05
#define RTI800_DAC0HI		0x06
#define RTI800_DAC1LO		0x07
#define RTI800_DAC1HI		0x08
#define RTI800_CLRFLAGS		0x09
#define RTI800_DI		0x0a
#define RTI800_DO		0x0b
#define RTI800_9513A_DATA	0x0c
#define RTI800_9513A_CNTRL	0x0d
#define RTI800_9513A_STATUS	0x0d

#define RTI800_IOSIZE		0x10

#define RTI800_AI_TIMEOUT	100

static const struct comedi_lrange range_rti800_ai_10_bipolar = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(1),
		BIP_RANGE(0.1),
		BIP_RANGE(0.02)
	}
};

static const struct comedi_lrange range_rti800_ai_5_bipolar = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(0.5),
		BIP_RANGE(0.05),
		BIP_RANGE(0.01)
	}
};

static const struct comedi_lrange range_rti800_ai_unipolar = {
	4, {
		UNI_RANGE(10),
		UNI_RANGE(1),
		UNI_RANGE(0.1),
		UNI_RANGE(0.02)
	}
};

static const struct comedi_lrange *const rti800_ai_ranges[] = {
	&range_rti800_ai_10_bipolar,
	&range_rti800_ai_5_bipolar,
	&range_rti800_ai_unipolar,
};

static const struct comedi_lrange *const rti800_ao_ranges[] = {
	&range_bipolar10,
	&range_unipolar10,
};

struct rti800_board {
	const char *name;
	int has_ao;
};

static const struct rti800_board rti800_boardtypes[] = {
	{
		.name		= "rti800",
	}, {
		.name		= "rti815",
		.has_ao		= 1,
	},
};

struct rti800_private {
	bool adc_2comp;
	bool dac_2comp[2];
	const struct comedi_lrange *ao_range_type_list[2];
	unsigned int ao_readback[2];
	unsigned char muxgain_bits;
};

static int rti800_ai_wait_for_conversion(struct comedi_device *dev,
					 int timeout)
{
	unsigned char status;
	int i;

	for (i = 0; i < timeout; i++) {
		status = inb(dev->iobase + RTI800_CSR);
		if (status & RTI800_CSR_OVERRUN) {
			outb(0, dev->iobase + RTI800_CLRFLAGS);
			return -EIO;
		}
		if (status & RTI800_CSR_DONE)
			return 0;
		udelay(1);
	}
	return -ETIME;
}

static int rti800_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct rti800_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int gain = CR_RANGE(insn->chanspec);
	unsigned char muxgain_bits;
	int ret;
	int i;

	inb(dev->iobase + RTI800_ADCHI);
	outb(0, dev->iobase + RTI800_CLRFLAGS);

	muxgain_bits = chan | (gain << 5);
	if (muxgain_bits != devpriv->muxgain_bits) {
		devpriv->muxgain_bits = muxgain_bits;
		outb(devpriv->muxgain_bits, dev->iobase + RTI800_MUXGAIN);
		/*
		 * Without a delay here, the RTI_CSR_OVERRUN bit
		 * gets set, and you will have an error.
		 */
		if (insn->n > 0) {
			int delay = (gain == 0) ? 10 :
				    (gain == 1) ? 20 :
				    (gain == 2) ? 40 : 80;

			udelay(delay);
		}
	}

	for (i = 0; i < insn->n; i++) {
		outb(0, dev->iobase + RTI800_CONVERT);
		ret = rti800_ai_wait_for_conversion(dev, RTI800_AI_TIMEOUT);
		if (ret)
			return ret;

		data[i] = inb(dev->iobase + RTI800_ADCLO);
		data[i] |= (inb(dev->iobase + RTI800_ADCHI) & 0xf) << 8;

		if (devpriv->adc_2comp)
			data[i] ^= 0x800;
	}

	return insn->n;
}

static int rti800_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct rti800_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return insn->n;
}

static int rti800_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct rti800_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int reg_lo = chan ? RTI800_DAC1LO : RTI800_DAC0LO;
	int reg_hi = chan ? RTI800_DAC1HI : RTI800_DAC0HI;
	int val = devpriv->ao_readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		if (devpriv->dac_2comp[chan])
			val ^= 0x800;

		outb(val & 0xff, dev->iobase + reg_lo);
		outb((val >> 8) & 0xff, dev->iobase + reg_hi);
	}

	devpriv->ao_readback[chan] = val;

	return insn->n;
}

static int rti800_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	data[1] = inb(dev->iobase + RTI800_DI);
	return insn->n;
}

static int rti800_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		/* Outputs are inverted... */
		outb(s->state ^ 0xff, dev->iobase + RTI800_DO);
	}

	data[1] = s->state;

	return insn->n;
}

static int rti800_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct rti800_board *board = comedi_board(dev);
	struct rti800_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], RTI800_IOSIZE);
	if (ret)
		return ret;

	outb(0, dev->iobase + RTI800_CSR);
	inb(dev->iobase + RTI800_ADCHI);
	outb(0, dev->iobase + RTI800_CLRFLAGS);

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	devpriv->adc_2comp = (it->options[4] == 0);
	devpriv->dac_2comp[0] = (it->options[6] == 0);
	devpriv->dac_2comp[1] = (it->options[8] == 0);
	/* invalid, forces the MUXGAIN register to be set when first used */
	devpriv->muxgain_bits = 0xff;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* ai subdevice */
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND;
	s->n_chan	= (it->options[2] ? 16 : 8);
	s->insn_read	= rti800_ai_insn_read;
	s->maxdata	= 0x0fff;
	s->range_table	= (it->options[3] < ARRAY_SIZE(rti800_ai_ranges))
				? rti800_ai_ranges[it->options[3]]
				: &range_unknown;

	s = &dev->subdevices[1];
	if (board->has_ao) {
		/* ao subdevice (only on rti815) */
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= 2;
		s->insn_read	= rti800_ao_insn_read;
		s->insn_write	= rti800_ao_insn_write;
		s->maxdata	= 0x0fff;
		s->range_table_list = devpriv->ao_range_type_list;
		devpriv->ao_range_type_list[0] =
			(it->options[5] < ARRAY_SIZE(rti800_ao_ranges))
				? rti800_ao_ranges[it->options[5]]
				: &range_unknown;
		devpriv->ao_range_type_list[1] =
			(it->options[7] < ARRAY_SIZE(rti800_ao_ranges))
				? rti800_ao_ranges[it->options[7]]
				: &range_unknown;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[2];
	/* di */
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 8;
	s->insn_bits	= rti800_di_insn_bits;
	s->maxdata	= 1;
	s->range_table	= &range_digital;

	s = &dev->subdevices[3];
	/* do */
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 8;
	s->insn_bits	= rti800_do_insn_bits;
	s->maxdata	= 1;
	s->range_table	= &range_digital;

	/*
	 * There is also an Am9513 timer on these boards. This subdevice
	 * is not currently supported.
	 */

	return 0;
}

static struct comedi_driver rti800_driver = {
	.driver_name	= "rti800",
	.module		= THIS_MODULE,
	.attach		= rti800_attach,
	.detach		= comedi_legacy_detach,
	.num_names	= ARRAY_SIZE(rti800_boardtypes),
	.board_name	= &rti800_boardtypes[0].name,
	.offset		= sizeof(struct rti800_board),
};
module_comedi_driver(rti800_driver);

MODULE_DESCRIPTION("Comedi: RTI-800 Multifunction Analog/Digital board");
MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_LICENSE("GPL");
