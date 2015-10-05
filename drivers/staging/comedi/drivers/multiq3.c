/*
 * multiq3.c
 * Hardware driver for Quanser Consulting MultiQ-3 board
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
 * Driver: multiq3
 * Description: Quanser Consulting MultiQ-3
 * Devices: [Quanser Consulting] MultiQ-3 (multiq3)
 * Author: Anders Blomdell <anders.blomdell@control.lth.se>
 * Status: works
 *
 * Configuration Options:
 *  [0] - I/O port base address
 *  [1] - IRQ (not used)
 *  [2] - Number of optional encoder chips installed on board
 *	  0 = none
 *	  1 = 2 inputs (Model -2E)
 *	  2 = 4 inputs (Model -4E)
 *	  3 = 6 inputs (Model -6E)
 *	  4 = 8 inputs (Model -8E)
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include "../comedidev.h"

/*
 * Register map
 */
#define MULTIQ3_DI_REG			0x00
#define MULTIQ3_DO_REG			0x00
#define MULTIQ3_AO_REG			0x02
#define MULTIQ3_AI_REG			0x04
#define MULTIQ3_AI_CONV_REG		0x04
#define MULTIQ3_STATUS_REG		0x06
#define MULTIQ3_STATUS_EOC		BIT(3)
#define MULTIQ3_STATUS_EOC_I		BIT(4)
#define MULTIQ3_CTRL_REG		0x06
#define MULTIQ3_CLK_REG			0x08
#define MULTIQ3_ENC_DATA_REG		0x0c
#define MULTIQ3_ENC_CTRL_REG		0x0e

/*
 * flags for CONTROL register
 */
#define MULTIQ3_AD_MUX_EN      0x0040
#define MULTIQ3_AD_AUTOZ       0x0080
#define MULTIQ3_AD_AUTOCAL     0x0100
#define MULTIQ3_AD_SH          0x0200
#define MULTIQ3_AD_CLOCK_4M    0x0400
#define MULTIQ3_DA_LOAD                0x1800

/*
 * flags for encoder control
 */
#define MULTIQ3_CLOCK_DATA      0x00
#define MULTIQ3_CLOCK_SETUP     0x18
#define MULTIQ3_INPUT_SETUP     0x41
#define MULTIQ3_QUAD_X4         0x38
#define MULTIQ3_BP_RESET        0x01
#define MULTIQ3_CNTR_RESET      0x02
#define MULTIQ3_TRSFRPR_CTR     0x08
#define MULTIQ3_TRSFRCNTR_OL    0x10
#define MULTIQ3_EFLAG_RESET     0x06

#define MULTIQ3_TIMEOUT 30

static void multiq3_set_ctrl(struct comedi_device *dev, unsigned int bits)
{
	/*
	 * According to the programming manual, the SH and CLK bits should
	 * be kept high at all times.
	 */
	outw(MULTIQ3_AD_SH | MULTIQ3_AD_CLOCK_4M | bits,
	     dev->iobase + MULTIQ3_CTRL_REG);
}

static int multiq3_ai_status(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + MULTIQ3_STATUS_REG);
	if (status & context)
		return 0;
	return -EBUSY;
}

static int multiq3_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val;
	int ret;
	int i;

	multiq3_set_ctrl(dev, MULTIQ3_AD_MUX_EN | (chan << 3));

	ret = comedi_timeout(dev, s, insn, multiq3_ai_status,
			     MULTIQ3_STATUS_EOC);
	if (ret)
		return ret;

	for (i = 0; i < insn->n; i++) {
		outw(0, dev->iobase + MULTIQ3_AI_CONV_REG);

		ret = comedi_timeout(dev, s, insn, multiq3_ai_status,
				     MULTIQ3_STATUS_EOC_I);
		if (ret)
			return ret;

		/* get a 16-bit sample; mask it to the subdevice resolution */
		val = inb(dev->iobase + MULTIQ3_AI_REG) << 8;
		val |= inb(dev->iobase + MULTIQ3_AI_REG);
		val &= s->maxdata;

		/* munge the 2's complement value to offset binary */
		data[i] = comedi_offset_munge(s, val);
	}

	return insn->n;
}

static int multiq3_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		multiq3_set_ctrl(dev, MULTIQ3_DA_LOAD | chan);
		outw(val, dev->iobase + MULTIQ3_AO_REG);
		multiq3_set_ctrl(dev, 0);
	}
	s->readback[chan] = val;

	return insn->n;
}

static int multiq3_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	data[1] = inw(dev->iobase + MULTIQ3_DI_REG);

	return insn->n;
}

static int multiq3_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + MULTIQ3_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static int multiq3_encoder_insn_read(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	int value;
	int n;

	for (n = 0; n < insn->n; n++) {
		multiq3_set_ctrl(dev, MULTIQ3_AD_MUX_EN | (chan << 3));
		outb(MULTIQ3_BP_RESET, dev->iobase + MULTIQ3_ENC_CTRL_REG);
		outb(MULTIQ3_TRSFRCNTR_OL, dev->iobase + MULTIQ3_ENC_CTRL_REG);
		value = inb(dev->iobase + MULTIQ3_ENC_DATA_REG);
		value |= (inb(dev->iobase + MULTIQ3_ENC_DATA_REG) << 8);
		value |= (inb(dev->iobase + MULTIQ3_ENC_DATA_REG) << 16);
		data[n] = (value + 0x800000) & 0xffffff;
	}

	return n;
}

static void encoder_reset(struct comedi_device *dev)
{
	struct comedi_subdevice *s = &dev->subdevices[4];
	int chan;

	for (chan = 0; chan < s->n_chan; chan++) {
		multiq3_set_ctrl(dev, MULTIQ3_AD_MUX_EN | (chan << 3));
		outb(MULTIQ3_EFLAG_RESET, dev->iobase + MULTIQ3_ENC_CTRL_REG);
		outb(MULTIQ3_BP_RESET, dev->iobase + MULTIQ3_ENC_CTRL_REG);
		outb(MULTIQ3_CLOCK_DATA, dev->iobase + MULTIQ3_ENC_DATA_REG);
		outb(MULTIQ3_CLOCK_SETUP, dev->iobase + MULTIQ3_ENC_CTRL_REG);
		outb(MULTIQ3_INPUT_SETUP, dev->iobase + MULTIQ3_ENC_CTRL_REG);
		outb(MULTIQ3_QUAD_X4, dev->iobase + MULTIQ3_ENC_CTRL_REG);
		outb(MULTIQ3_CNTR_RESET, dev->iobase + MULTIQ3_ENC_CTRL_REG);
	}
}

static int multiq3_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 5);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND;
	s->n_chan	= 8;
	s->maxdata	= 0x1fff;
	s->range_table	= &range_bipolar5;
	s->insn_read	= multiq3_ai_insn_read;

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 0x0fff;
	s->range_table	= &range_bipolar5;
	s->insn_write	= multiq3_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= multiq3_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= multiq3_do_insn_bits;

	/* Encoder (Counter) subdevice */
	s = &dev->subdevices[4];
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_READABLE | SDF_LSAMPL;
	s->n_chan	= it->options[2] * 2;
	s->maxdata	= 0x00ffffff;
	s->range_table	= &range_unknown;
	s->insn_read	= multiq3_encoder_insn_read;

	encoder_reset(dev);

	return 0;
}

static struct comedi_driver multiq3_driver = {
	.driver_name	= "multiq3",
	.module		= THIS_MODULE,
	.attach		= multiq3_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(multiq3_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
