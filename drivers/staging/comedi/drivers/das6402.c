/*
 * das6402.c
 * Comedi driver for DAS6402 compatible boards
 * Copyright(c) 2014 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Rewrite of an experimental driver by:
 * Copyright (C) 1999 Oystein Svendsen <svendsen@pvv.org>
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
 * Driver: das6402
 * Description: Keithley Metrabyte DAS6402 (& compatibles)
 * Devices: (Keithley Metrabyte) DAS6402-12 (das6402-12)
 *	    (Keithley Metrabyte) DAS6402-16 (das6402-16)
 * Author: H Hartley Sweeten <hsweeten@visionengravers.com>
 * Updated: Fri, 14 Mar 2014 10:18:43 -0700
 * Status: unknown
 *
 * Configuration Options:
 *   [0] - I/O base address
 *   [1] - IRQ (optional, needed for async command support)
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedidev.h"
#include "8253.h"

/*
 * Register I/O map
 */
#define DAS6402_AI_DATA_REG		0x00
#define DAS6402_AI_MUX_REG		0x02
#define DAS6402_AI_MUX_LO(x)		(((x) & 0x3f) << 0)
#define DAS6402_AI_MUX_HI(x)		(((x) & 0x3f) << 8)
#define DAS6402_DI_DO_REG		0x03
#define DAS6402_AO_DATA_REG(x)		(0x04 + ((x) * 2))
#define DAS6402_AO_LSB_REG(x)		(0x04 + ((x) * 2))
#define DAS6402_AO_MSB_REG(x)		(0x05 + ((x) * 2))
#define DAS6402_STATUS_REG		0x08
#define DAS6402_STATUS_FFNE		(1 << 0)
#define DAS6402_STATUS_FHALF		(1 << 1)
#define DAS6402_STATUS_FFULL		(1 << 2)
#define DAS6402_STATUS_XINT		(1 << 3)
#define DAS6402_STATUS_INT		(1 << 4)
#define DAS6402_STATUS_XTRIG		(1 << 5)
#define DAS6402_STATUS_INDGT		(1 << 6)
#define DAS6402_STATUS_10MHZ		(1 << 7)
#define DAS6402_STATUS_W_CLRINT		(1 << 0)
#define DAS6402_STATUS_W_CLRXTR		(1 << 1)
#define DAS6402_STATUS_W_CLRXIN		(1 << 2)
#define DAS6402_STATUS_W_EXTEND		(1 << 4)
#define DAS6402_STATUS_W_ARMED		(1 << 5)
#define DAS6402_STATUS_W_POSTMODE	(1 << 6)
#define DAS6402_STATUS_W_10MHZ		(1 << 7)
#define DAS6402_CTRL_REG		0x09
#define DAS6402_CTRL_SOFT_TRIG		(0 << 0)
#define DAS6402_CTRL_EXT_FALL_TRIG	(1 << 0)
#define DAS6402_CTRL_EXT_RISE_TRIG	(2 << 0)
#define DAS6402_CTRL_PACER_TRIG		(3 << 0)
#define DAS6402_CTRL_BURSTEN		(1 << 2)
#define DAS6402_CTRL_XINTE		(1 << 3)
#define DAS6402_CTRL_IRQ(x)		((x) << 4)
#define DAS6402_CTRL_INTE		(1 << 7)
#define DAS6402_TRIG_REG		0x0a
#define DAS6402_TRIG_TGEN		(1 << 0)
#define DAS6402_TRIG_TGSEL		(1 << 1)
#define DAS6402_TRIG_TGPOL		(1 << 2)
#define DAS6402_TRIG_PRETRIG		(1 << 3)
#define DAS6402_AO_RANGE(_chan, _range)	((_range) << ((_chan) ? 6 : 4))
#define DAS6402_AO_RANGE_MASK(_chan)	(3 << ((_chan) ? 6 : 4))
#define DAS6402_MODE_REG		0x0b
#define DAS6402_MODE_RANGE(x)		((x) << 0)
#define DAS6402_MODE_POLLED		(0 << 2)
#define DAS6402_MODE_FIFONEPTY		(1 << 2)
#define DAS6402_MODE_FIFOHFULL		(2 << 2)
#define DAS6402_MODE_EOB		(3 << 2)
#define DAS6402_MODE_ENHANCED		(1 << 4)
#define DAS6402_MODE_SE			(1 << 5)
#define DAS6402_MODE_UNI		(1 << 6)
#define DAS6402_MODE_DMA1		(0 << 7)
#define DAS6402_MODE_DMA3		(1 << 7)
#define DAS6402_TIMER_BASE		0x0c

static const struct comedi_lrange das6402_ai_ranges = {
	8, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

/*
 * Analog output ranges are programmable on the DAS6402/12.
 * For the DAS6402/16 the range bits have no function, the
 * DAC ranges are selected by switches on the board.
 */
static const struct comedi_lrange das6402_ao_ranges = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(10)
	}
};

struct das6402_boardinfo {
	const char *name;
	unsigned int maxdata;
};

static struct das6402_boardinfo das6402_boards[] = {
	{
		.name		= "das6402-12",
		.maxdata	= 0x0fff,
	}, {
		.name		= "das6402-16",
		.maxdata	= 0xffff,
	},
};

struct das6402_private {
	unsigned int irq;

	unsigned int count;
	unsigned int divider1;
	unsigned int divider2;

	unsigned int ao_range;
	unsigned int ao_readback[2];
};

static void das6402_set_mode(struct comedi_device *dev,
			     unsigned int mode)
{
	outb(DAS6402_MODE_ENHANCED | mode, dev->iobase + DAS6402_MODE_REG);
}

static void das6402_set_extended(struct comedi_device *dev,
				 unsigned int val)
{
	outb(DAS6402_STATUS_W_EXTEND, dev->iobase + DAS6402_STATUS_REG);
	outb(DAS6402_STATUS_W_EXTEND | val, dev->iobase + DAS6402_STATUS_REG);
	outb(val, dev->iobase + DAS6402_STATUS_REG);
}

static void das6402_clear_all_interrupts(struct comedi_device *dev)
{
	outb(DAS6402_STATUS_W_CLRINT |
	     DAS6402_STATUS_W_CLRXTR |
	     DAS6402_STATUS_W_CLRXIN, dev->iobase + DAS6402_STATUS_REG);
}

static void das6402_ai_clear_eoc(struct comedi_device *dev)
{
	outb(DAS6402_STATUS_W_CLRINT, dev->iobase + DAS6402_STATUS_REG);
}

static void das6402_enable_counter(struct comedi_device *dev, bool load)
{
	struct das6402_private *devpriv = dev->private;
	unsigned long timer_iobase = dev->iobase + DAS6402_TIMER_BASE;

	if (load) {
		i8254_set_mode(timer_iobase, 0, 0, I8254_MODE0 | I8254_BINARY);
		i8254_set_mode(timer_iobase, 0, 1, I8254_MODE2 | I8254_BINARY);
		i8254_set_mode(timer_iobase, 0, 2, I8254_MODE2 | I8254_BINARY);

		i8254_write(timer_iobase, 0, 0, devpriv->count);
		i8254_write(timer_iobase, 0, 1, devpriv->divider1);
		i8254_write(timer_iobase, 0, 2, devpriv->divider2);

	} else {
		i8254_set_mode(timer_iobase, 0, 0, I8254_MODE0 | I8254_BINARY);
		i8254_set_mode(timer_iobase, 0, 1, I8254_MODE0 | I8254_BINARY);
		i8254_set_mode(timer_iobase, 0, 2, I8254_MODE0 | I8254_BINARY);
	}
}

static irqreturn_t das6402_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;

	das6402_clear_all_interrupts(dev);

	return IRQ_HANDLED;
}

static int das6402_ai_cmd(struct comedi_device *dev,
			  struct comedi_subdevice *s)
{
	return -EINVAL;
}

static int das6402_ai_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
	return -EINVAL;
}

static int das6402_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	outb(DAS6402_CTRL_SOFT_TRIG, dev->iobase + DAS6402_CTRL_REG);

	return 0;
}

static void das6402_ai_soft_trig(struct comedi_device *dev)
{
	outw(0, dev->iobase + DAS6402_AI_DATA_REG);
}

static int das6402_ai_eoc(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DAS6402_STATUS_REG);
	if (status & DAS6402_STATUS_FFNE)
		return 0;
	return -EBUSY;
}

static int das6402_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int aref = CR_AREF(insn->chanspec);
	unsigned int val;
	int ret;
	int i;

	val = DAS6402_MODE_RANGE(range) | DAS6402_MODE_POLLED;
	if (aref == AREF_DIFF) {
		if (chan > s->n_chan / 2)
			return -EINVAL;
	} else {
		val |= DAS6402_MODE_SE;
	}
	if (comedi_range_is_unipolar(s, range))
		val |= DAS6402_MODE_UNI;

	/* enable software conversion trigger */
	outb(DAS6402_CTRL_SOFT_TRIG, dev->iobase + DAS6402_CTRL_REG);

	das6402_set_mode(dev, val);

	/* load the mux for single channel conversion */
	outw(DAS6402_AI_MUX_HI(chan) | DAS6402_AI_MUX_LO(chan),
	     dev->iobase + DAS6402_AI_MUX_REG);

	for (i = 0; i < insn->n; i++) {
		das6402_ai_clear_eoc(dev);
		das6402_ai_soft_trig(dev);

		ret = comedi_timeout(dev, s, insn, das6402_ai_eoc, 0);
		if (ret)
			break;

		val = inw(dev->iobase + DAS6402_AI_DATA_REG);

		if (s->maxdata == 0x0fff)
			val >>= 4;

		data[i] = val;
	}

	das6402_ai_clear_eoc(dev);

	return insn->n;
}

static int das6402_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct das6402_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val;
	int i;

	/* set the range for this channel */
	val = devpriv->ao_range;
	val &= ~DAS6402_AO_RANGE_MASK(chan);
	val |= DAS6402_AO_RANGE(chan, range);
	if (val != devpriv->ao_range) {
		devpriv->ao_range = val;
		outb(val, dev->iobase + DAS6402_TRIG_REG);
	}

	/*
	 * The DAS6402/16 has a jumper to select either individual
	 * update (UPDATE) or simultaneous updating (XFER) of both
	 * DAC's. In UPDATE mode, when the MSB is written, that DAC
	 * is updated. In XFER mode, after both DAC's are loaded,
	 * a read cycle of any DAC register will update both DAC's
	 * simultaneously.
	 *
	 * If you have XFER mode enabled a (*insn_read) will need
	 * to be performed in order to update the DAC's with the
	 * last value written.
	 */
	for (i = 0; i < insn->n; i++) {
		val = data[i];

		devpriv->ao_readback[chan] = val;

		if (s->maxdata == 0x0fff) {
			/*
			 * DAS6402/12 has the two 8-bit DAC registers, left
			 * justified (the 4 LSB bits are don't care). Data
			 * can be written as one word.
			 */
			val <<= 4;
			outw(val, dev->iobase + DAS6402_AO_DATA_REG(chan));
		} else {
			/*
			 * DAS6402/16 uses both 8-bit DAC registers and needs
			 * to be written LSB then MSB.
			 */
			outb(val & 0xff,
			     dev->iobase + DAS6402_AO_LSB_REG(chan));
			outb((val >> 8) & 0xff,
			     dev->iobase + DAS6402_AO_LSB_REG(chan));
		}
	}

	return insn->n;
}

static int das6402_ao_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct das6402_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	/*
	 * If XFER mode is enabled, reading any DAC register
	 * will update both DAC's simultaneously.
	 */
	inw(dev->iobase + DAS6402_AO_LSB_REG(chan));

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return insn->n;
}

static int das6402_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	data[1] = inb(dev->iobase + DAS6402_DI_DO_REG);

	return insn->n;
}

static int das6402_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + DAS6402_DI_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static void das6402_reset(struct comedi_device *dev)
{
	struct das6402_private *devpriv = dev->private;

	/* enable "Enhanced" mode */
	outb(DAS6402_MODE_ENHANCED, dev->iobase + DAS6402_MODE_REG);

	/* enable 10MHz pacer clock */
	das6402_set_extended(dev, DAS6402_STATUS_W_10MHZ);

	/* enable software conversion trigger */
	outb(DAS6402_CTRL_SOFT_TRIG, dev->iobase + DAS6402_CTRL_REG);

	/* default ADC to single-ended unipolar 10V inputs */
	das6402_set_mode(dev, DAS6402_MODE_RANGE(0) |
			      DAS6402_MODE_POLLED |
			      DAS6402_MODE_SE |
			      DAS6402_MODE_UNI);

	/* default mux for single channel conversion (channel 0) */
	outw(DAS6402_AI_MUX_HI(0) | DAS6402_AI_MUX_LO(0),
	     dev->iobase + DAS6402_AI_MUX_REG);

	/* set both DAC's for unipolar 5V output range */
	devpriv->ao_range = DAS6402_AO_RANGE(0, 2) | DAS6402_AO_RANGE(1, 2);
	outb(devpriv->ao_range, dev->iobase + DAS6402_TRIG_REG);

	/* set both DAC's to 0V */
	outw(0, dev->iobase + DAS6402_AO_DATA_REG(0));
	outw(0, dev->iobase + DAS6402_AO_DATA_REG(0));
	inw(dev->iobase + DAS6402_AO_LSB_REG(0));

	das6402_enable_counter(dev, false);

	/* set all digital outputs low */
	outb(0, dev->iobase + DAS6402_DI_DO_REG);

	das6402_clear_all_interrupts(dev);
}

static int das6402_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	const struct das6402_boardinfo *board = comedi_board(dev);
	struct das6402_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	das6402_reset(dev);

	/* IRQs 2,3,5,6,7, 10,11,15 are valid for "enhanced" mode */
	if ((1 << it->options[1]) & 0x8cec) {
		ret = request_irq(it->options[1], das6402_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0) {
			dev->irq = it->options[1];

			switch (dev->irq) {
			case 10:
				devpriv->irq = 4;
				break;
			case 11:
				devpriv->irq = 1;
				break;
			case 15:
				devpriv->irq = 6;
				break;
			default:
				devpriv->irq = dev->irq;
				break;
			}
		}
	}

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan	= 64;
	s->maxdata	= board->maxdata;
	s->range_table	= &das6402_ai_ranges;
	s->insn_read	= das6402_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= s->n_chan;
		s->do_cmdtest	= das6402_ai_cmdtest;
		s->do_cmd	= das6402_ai_cmd;
		s->cancel	= das6402_ai_cancel;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 2;
	s->maxdata	= board->maxdata;
	s->range_table	= &das6402_ao_ranges;
	s->insn_write	= das6402_ao_insn_write;
	s->insn_read	= das6402_ao_insn_read;

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das6402_di_insn_bits;

	/* Digital Input subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das6402_do_insn_bits;

	return 0;
}

static struct comedi_driver das6402_driver = {
	.driver_name	= "das6402",
	.module		= THIS_MODULE,
	.attach		= das6402_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &das6402_boards[0].name,
	.num_names	= ARRAY_SIZE(das6402_boards),
	.offset		= sizeof(struct das6402_boardinfo),
};
module_comedi_driver(das6402_driver)

MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_DESCRIPTION("Comedi driver for DAS6402 compatible boards");
MODULE_LICENSE("GPL");
