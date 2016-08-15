/*
 * Comedi driver for Data Translation DT2811
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) David A. Schleef <ds@schleef.org>
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
 * Driver: dt2811
 * Description: Data Translation DT2811
 * Author: ds
 * Devices: [Data Translation] DT2811-PGL (dt2811-pgl), DT2811-PGH (dt2811-pgh)
 * Status: works
 *
 * Configuration options:
 *   [0] - I/O port base address
 *   [1] - IRQ (optional, needed for async command support)
 *   [2] - A/D reference (# of analog inputs)
 *	   0 = single-ended (16 channels)
 *	   1 = differential (8 channels)
 *	   2 = pseudo-differential (16 channels)
 *   [3] - A/D range (deprecated, see below)
 *   [4] - D/A 0 range (deprecated, see below)
 *   [5] - D/A 1 range (deprecated, see below)
 *
 * Notes:
 *   - A/D ranges are not programmable but the gain is. The AI subdevice has
 *     a range_table containing all the possible analog input range/gain
 *     options for the dt2811-pgh or dt2811-pgl. Use the range that matches
 *     your board configuration and the desired gain to correctly convert
 *     between data values and physical units and to set the correct output
 *     gain.
 *   - D/A ranges are not programmable. The AO subdevice has a range_table
 *     containing all the possible analog output ranges. Use the range
 *     that matches your board configuration to convert between data
 *     values and physical units.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include "../comedidev.h"

/*
 * Register I/O map
 */
#define DT2811_ADCSR_REG		0x00	/* r/w  A/D Control/Status */
#define DT2811_ADCSR_ADDONE		BIT(7)	/* r      1=A/D conv done */
#define DT2811_ADCSR_ADERROR		BIT(6)	/* r      1=A/D error */
#define DT2811_ADCSR_ADBUSY		BIT(5)	/* r      1=A/D busy */
#define DT2811_ADCSR_CLRERROR		BIT(4)
#define DT2811_ADCSR_DMAENB		BIT(3)	/* r/w    1=dma ena */
#define DT2811_ADCSR_INTENB		BIT(2)	/* r/w    1=interupts ena */
#define DT2811_ADCSR_ADMODE(x)		(((x) & 0x3) << 0)

#define DT2811_ADGCR_REG		0x01	/* r/w  A/D Gain/Channel */
#define DT2811_ADGCR_GAIN(x)		(((x) & 0x3) << 6)
#define DT2811_ADGCR_CHAN(x)		(((x) & 0xf) << 0)

#define DT2811_ADDATA_LO_REG		0x02	/* r   A/D Data low byte */
#define DT2811_ADDATA_HI_REG		0x03	/* r   A/D Data high byte */

#define DT2811_DADATA_LO_REG(x)		(0x02 + ((x) * 2)) /* w D/A Data low */
#define DT2811_DADATA_HI_REG(x)		(0x03 + ((x) * 2)) /* w D/A Data high */

#define DT2811_DI_REG			0x06	/* r   Digital Input Port 0 */
#define DT2811_DO_REG			0x06	/* w   Digital Output Port 1 */

#define DT2811_TMRCTR_REG		0x07	/* r/w  Timer/Counter */
#define DT2811_TMRCTR_MANTISSA(x)	(((x) & 0x7) << 3)
#define DT2811_TMRCTR_EXPONENT(x)	(((x) & 0x7) << 0)

#define DT2811_OSC_BASE			1666	/* 600 kHz = 1666.6667ns */

/*
 * Timer frequency control:
 *   DT2811_TMRCTR_MANTISSA	DT2811_TMRCTR_EXPONENT
 *   val  divisor  frequency	val  multiply divisor/divide frequency by
 *    0      1      600 kHz	 0   1
 *    1     10       60 kHz	 1   10
 *    2      2      300 kHz	 2   100
 *    3      3      200 kHz	 3   1000
 *    4      4      150 kHz	 4   10000
 *    5      5      120 kHz	 5   100000
 *    6      6      100 kHz	 6   1000000
 *    7     12       50 kHz	 7   10000000
 */
const unsigned int dt2811_clk_dividers[] = {
	1, 10, 2, 3, 4, 5, 6, 12
};

const unsigned int dt2811_clk_multipliers[] = {
	1, 10, 100, 1000, 10000, 100000, 1000000, 10000000
};

/*
 * The Analog Input range is set using jumpers on the board.
 *
 * Input Range		W9  W10
 * -5V to +5V		In  Out
 * -2.5V to +2.5V	In  In
 * 0V to +5V		Out In
 *
 * The gain may be set to 1, 2, 4, or 8 (on the dt2811-pgh) or to
 * 1, 10, 100, 500 (on the dt2811-pgl).
 */
static const struct comedi_lrange dt2811_pgh_ai_ranges = {
	12, {
		BIP_RANGE(5),		/* range 0: gain=1 */
		BIP_RANGE(2.5),		/* range 1: gain=2 */
		BIP_RANGE(1.25),	/* range 2: gain=4 */
		BIP_RANGE(0.625),	/* range 3: gain=8 */

		BIP_RANGE(2.5),		/* range 0+4: gain=1 */
		BIP_RANGE(1.25),	/* range 1+4: gain=2 */
		BIP_RANGE(0.625),	/* range 2+4: gain=4 */
		BIP_RANGE(0.3125),	/* range 3+4: gain=8 */

		UNI_RANGE(5),		/* range 0+8: gain=1 */
		UNI_RANGE(2.5),		/* range 1+8: gain=2 */
		UNI_RANGE(1.25),	/* range 2+8: gain=4 */
		UNI_RANGE(0.625)	/* range 3+8: gain=8 */
	}
};

static const struct comedi_lrange dt2811_pgl_ai_ranges = {
	12, {
		BIP_RANGE(5),		/* range 0: gain=1 */
		BIP_RANGE(0.5),		/* range 1: gain=10 */
		BIP_RANGE(0.05),	/* range 2: gain=100 */
		BIP_RANGE(0.01),	/* range 3: gain=500 */

		BIP_RANGE(2.5),		/* range 0+4: gain=1 */
		BIP_RANGE(0.25),	/* range 1+4: gain=10 */
		BIP_RANGE(0.025),	/* range 2+4: gain=100 */
		BIP_RANGE(0.005),	/* range 3+4: gain=500 */

		UNI_RANGE(5),		/* range 0+8: gain=1 */
		UNI_RANGE(0.5),		/* range 1+8: gain=10 */
		UNI_RANGE(0.05),	/* range 2+8: gain=100 */
		UNI_RANGE(0.01)		/* range 3+8: gain=500 */
	}
};

/*
 * The Analog Output range is set per-channel using jumpers on the board.
 *
 *			DAC0 Jumpers		DAC1 Jumpers
 * Output Range		W5  W6  W7  W8		W1  W2  W3  W4
 * -5V to +5V		In  Out In  Out		In  Out In  Out
 * -2.5V to +2.5V	In  Out Out In		In  Out Out In
 * 0 to +5V		Out In  Out In		Out In  Out In
 */
static const struct comedi_lrange dt2811_ao_ranges = {
	3, {
		BIP_RANGE(5),	/* default setting from factory */
		BIP_RANGE(2.5),
		UNI_RANGE(5)
	}
};

struct dt2811_board {
	const char *name;
	unsigned int is_pgh:1;
};

static const struct dt2811_board dt2811_boards[] = {
	{
		.name		= "dt2811-pgh",
		.is_pgh		= 1,
	}, {
		.name		= "dt2811-pgl",
	},
};

struct dt2811_private {
	unsigned int ai_divisor;
};

static unsigned int dt2811_ai_read_sample(struct comedi_device *dev,
					  struct comedi_subdevice *s)
{
	unsigned int val;

	val = inb(dev->iobase + DT2811_ADDATA_LO_REG) |
	      (inb(dev->iobase + DT2811_ADDATA_HI_REG) << 8);

	return val & s->maxdata;
}

static irqreturn_t dt2811_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int status;

	if (!dev->attached)
		return IRQ_NONE;

	status = inb(dev->iobase + DT2811_ADCSR_REG);

	if (status & DT2811_ADCSR_ADERROR) {
		async->events |= COMEDI_CB_OVERFLOW;

		outb(status | DT2811_ADCSR_CLRERROR,
		     dev->iobase + DT2811_ADCSR_REG);
	}

	if (status & DT2811_ADCSR_ADDONE) {
		unsigned short val;

		val = dt2811_ai_read_sample(dev, s);
		comedi_buf_write_samples(s, &val, 1);
	}

	if (cmd->stop_src == TRIG_COUNT && async->scans_done >= cmd->stop_arg)
		async->events |= COMEDI_CB_EOA;

	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

static int dt2811_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	/*
	 * Mode 0
	 * Single conversion
	 *
	 * Loading a chanspec will trigger a conversion.
	 */
	outb(DT2811_ADCSR_ADMODE(0), dev->iobase + DT2811_ADCSR_REG);

	return 0;
}

static void dt2811_ai_set_chanspec(struct comedi_device *dev,
				   unsigned int chanspec)
{
	unsigned int chan = CR_CHAN(chanspec);
	unsigned int range = CR_RANGE(chanspec);

	outb(DT2811_ADGCR_CHAN(chan) | DT2811_ADGCR_GAIN(range),
	     dev->iobase + DT2811_ADGCR_REG);
}

static int dt2811_ai_cmd(struct comedi_device *dev,
			 struct comedi_subdevice *s)
{
	struct dt2811_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int mode;

	if (cmd->start_src == TRIG_NOW) {
		/*
		 * Mode 1
		 * Continuous conversion, internal trigger and clock
		 *
		 * This resets the trigger flip-flop, disabling A/D strobes.
		 * The timer/counter register is loaded with the division
		 * ratio which will give the required sample rate.
		 *
		 * Loading the first chanspec sets the trigger flip-flop,
		 * enabling the timer/counter. A/D strobes are then generated
		 * at the rate set by the internal clock/divider.
		 */
		mode = DT2811_ADCSR_ADMODE(1);
	} else { /* TRIG_EXT */
		if (cmd->convert_src == TRIG_TIMER) {
			/*
			 * Mode 2
			 * Continuous conversion, external trigger
			 *
			 * Similar to Mode 1, with the exception that the
			 * trigger flip-flop must be set by a negative edge
			 * on the external trigger input.
			 */
			mode = DT2811_ADCSR_ADMODE(2);
		} else { /* TRIG_EXT */
			/*
			 * Mode 3
			 * Continuous conversion, external trigger, clock
			 *
			 * Similar to Mode 2, with the exception that the
			 * conversion rate is set by the frequency on the
			 * external clock/divider.
			 */
			mode = DT2811_ADCSR_ADMODE(3);
		}
	}
	outb(mode | DT2811_ADCSR_INTENB, dev->iobase + DT2811_ADCSR_REG);

	/* load timer */
	outb(devpriv->ai_divisor, dev->iobase + DT2811_TMRCTR_REG);

	/* load chanspec - enables timer */
	dt2811_ai_set_chanspec(dev, cmd->chanlist[0]);

	return 0;
}

static unsigned int dt2811_ns_to_timer(unsigned int *nanosec,
				       unsigned int flags)
{
	unsigned long long ns = *nanosec;
	unsigned int ns_lo = COMEDI_MIN_SPEED;
	unsigned int ns_hi = 0;
	unsigned int divisor_hi = 0;
	unsigned int divisor_lo = 0;
	unsigned int _div;
	unsigned int _mult;

	/*
	 * Work through all the divider/multiplier values to find the two
	 * closest divisors to generate the requested nanosecond timing.
	 */
	for (_div = 0; _div <= 7; _div++) {
		for (_mult = 0; _mult <= 7; _mult++) {
			unsigned int div = dt2811_clk_dividers[_div];
			unsigned int mult = dt2811_clk_multipliers[_mult];
			unsigned long long divider = div * mult;
			unsigned int divisor = DT2811_TMRCTR_MANTISSA(_div) |
					       DT2811_TMRCTR_EXPONENT(_mult);

			/*
			 * The timer can be configured to run at a slowest
			 * speed of 0.005hz (600 Khz/120000000), which requires
			 * 37-bits to represent the nanosecond value. Limit the
			 * slowest timing to what comedi handles (32-bits).
			 */
			ns = divider * DT2811_OSC_BASE;
			if (ns > COMEDI_MIN_SPEED)
				continue;

			/* Check for fastest found timing */
			if (ns <= *nanosec && ns > ns_hi) {
				ns_hi = ns;
				divisor_hi = divisor;
			}
			/* Check for slowest found timing */
			if (ns >= *nanosec && ns < ns_lo) {
				ns_lo = ns;
				divisor_lo = divisor;
			}
		}
	}

	/*
	 * The slowest found timing will be invalid if the requested timing
	 * is faster than what can be generated by the timer. Fix it so that
	 * CMDF_ROUND_UP returns valid timing.
	 */
	if (ns_lo == COMEDI_MIN_SPEED) {
		ns_lo = ns_hi;
		divisor_lo = divisor_hi;
	}
	/*
	 * The fastest found timing will be invalid if the requested timing
	 * is less than what can be generated by the timer. Fix it so that
	 * CMDF_ROUND_NEAREST and CMDF_ROUND_DOWN return valid timing.
	 */
	if (ns_hi == 0) {
		ns_hi = ns_lo;
		divisor_hi = divisor_lo;
	}

	switch (flags & CMDF_ROUND_MASK) {
	case CMDF_ROUND_NEAREST:
	default:
		if (ns_hi - *nanosec < *nanosec - ns_lo) {
			*nanosec = ns_lo;
			return divisor_lo;
		}
		*nanosec = ns_hi;
		return divisor_hi;
	case CMDF_ROUND_UP:
		*nanosec = ns_lo;
		return divisor_lo;
	case CMDF_ROUND_DOWN:
		*nanosec = ns_hi;
		return divisor_hi;
	}
}

static int dt2811_ai_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_cmd *cmd)
{
	struct dt2811_private *devpriv = dev->private;
	unsigned int arg;
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (cmd->convert_src == TRIG_EXT && cmd->start_src != TRIG_EXT)
		err |= -EINVAL;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	if (cmd->convert_src == TRIG_TIMER)
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg, 12500);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);
	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* Step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		arg = cmd->convert_arg;
		devpriv->ai_divisor = dt2811_ns_to_timer(&arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);
	} else { /* TRIG_EXT */
		/* The convert_arg is used to set the divisor. */
		devpriv->ai_divisor = cmd->convert_arg;
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */

	return 0;
}

static int dt2811_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DT2811_ADCSR_REG);
	if ((status & DT2811_ADCSR_ADBUSY) == 0)
		return 0;
	return -EBUSY;
}

static int dt2811_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	int ret;
	int i;

	/* We will already be in Mode 0 */
	for (i = 0; i < insn->n; i++) {
		/* load chanspec and trigger conversion */
		dt2811_ai_set_chanspec(dev, insn->chanspec);

		ret = comedi_timeout(dev, s, insn, dt2811_ai_eoc, 0);
		if (ret)
			return ret;

		data[i] = dt2811_ai_read_sample(dev, s);
	}

	return insn->n;
}

static int dt2811_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outb(val & 0xff, dev->iobase + DT2811_DADATA_LO_REG(chan));
		outb((val >> 8) & 0xff,
		     dev->iobase + DT2811_DADATA_HI_REG(chan));
	}
	s->readback[chan] = val;

	return insn->n;
}

static int dt2811_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	data[1] = inb(dev->iobase + DT2811_DI_REG);

	return insn->n;
}

static int dt2811_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + DT2811_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static void dt2811_reset(struct comedi_device *dev)
{
	/* This is the initialization sequence from the users manual */
	outb(DT2811_ADCSR_ADMODE(0), dev->iobase + DT2811_ADCSR_REG);
	usleep_range(100, 1000);
	inb(dev->iobase + DT2811_ADDATA_LO_REG);
	inb(dev->iobase + DT2811_ADDATA_HI_REG);
	outb(DT2811_ADCSR_ADMODE(0) | DT2811_ADCSR_CLRERROR,
	     dev->iobase + DT2811_ADCSR_REG);
}

static int dt2811_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct dt2811_board *board = dev->board_ptr;
	struct dt2811_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x8);
	if (ret)
		return ret;

	dt2811_reset(dev);

	/* IRQ's 2,3,5,7 are valid for async command support */
	if (it->options[1] <= 7  && (BIT(it->options[1]) & 0xac)) {
		ret = request_irq(it->options[1], dt2811_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE |
			  (it->options[2] == 1) ? SDF_DIFF :
			  (it->options[2] == 2) ? SDF_COMMON : SDF_GROUND;
	s->n_chan	= (it->options[2] == 1) ? 8 : 16;
	s->maxdata	= 0x0fff;
	s->range_table	= board->is_pgh ? &dt2811_pgh_ai_ranges
					: &dt2811_pgl_ai_ranges;
	s->insn_read	= dt2811_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= 1;
		s->do_cmdtest	= dt2811_ai_cmdtest;
		s->do_cmd	= dt2811_ai_cmd;
		s->cancel	= dt2811_ai_cancel;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 2;
	s->maxdata	= 0x0fff;
	s->range_table	= &dt2811_ao_ranges;
	s->insn_write	= dt2811_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= dt2811_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= dt2811_do_insn_bits;

	return 0;
}

static struct comedi_driver dt2811_driver = {
	.driver_name	= "dt2811",
	.module		= THIS_MODULE,
	.attach		= dt2811_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &dt2811_boards[0].name,
	.num_names	= ARRAY_SIZE(dt2811_boards),
	.offset		= sizeof(struct dt2811_board),
};
module_comedi_driver(dt2811_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Data Translation DT2811 series boards");
MODULE_LICENSE("GPL");
