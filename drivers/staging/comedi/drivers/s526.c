/*
 * s526.c
 * Sensoray s526 Comedi driver
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
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
 * Driver: s526
 * Description: Sensoray 526 driver
 * Devices: [Sensoray] 526 (s526)
 * Author: Richie
 *	   Everett Wang <everett.wang@everteq.com>
 * Updated: Thu, 14 Sep. 2006
 * Status: experimental
 *
 * Encoder works
 * Analog input works
 * Analog output works
 * PWM output works
 * Commands are not supported yet.
 *
 * Configuration Options:
 *   [0] - I/O port base address
 */

#include <linux/module.h>
#include "../comedidev.h"
#include <asm/byteorder.h>

#define S526_START_AI_CONV	0
#define S526_AI_READ		0

/* Ports */
#define S526_NUM_PORTS 27

/* registers */
#define S526_TIMER_REG		0x00
#define S526_TIMER_LOAD(x)	(((x) & 0xff) << 8)
#define S526_TIMER_MODE		((x) << 1)
#define S526_TIMER_MANUAL	S526_TIMER_MODE(0)
#define S526_TIMER_AUTO		S526_TIMER_MODE(1)
#define S526_TIMER_RESTART	BIT(0)
#define REG_WDC 0x02
#define REG_DAC 0x04
#define REG_ADC 0x06
#define REG_ADD 0x08
#define REG_DIO 0x0A
#define REG_IER 0x0C
#define REG_ISR 0x0E
#define REG_MSC 0x10

#define S526_GPCT_LSB_REG(x)	(0x12 + ((x) * 8))
#define S526_GPCT_MSB_REG(x)	(0x14 + ((x) * 8))
#define S526_GPCT_MODE_REG(x)	(0x16 + ((x) * 8))
#define S526_GPCT_CTRL_REG(x)	(0x18 + ((x) * 8))

#define REG_EED 0x32
#define REG_EEC 0x34

struct counter_mode_register_t {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned short coutSource:1;
	unsigned short coutPolarity:1;
	unsigned short autoLoadResetRcap:3;
	unsigned short hwCtEnableSource:2;
	unsigned short ctEnableCtrl:2;
	unsigned short clockSource:2;
	unsigned short countDir:1;
	unsigned short countDirCtrl:1;
	unsigned short outputRegLatchCtrl:1;
	unsigned short preloadRegSel:1;
	unsigned short reserved:1;
 #elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned short reserved:1;
	unsigned short preloadRegSel:1;
	unsigned short outputRegLatchCtrl:1;
	unsigned short countDirCtrl:1;
	unsigned short countDir:1;
	unsigned short clockSource:2;
	unsigned short ctEnableCtrl:2;
	unsigned short hwCtEnableSource:2;
	unsigned short autoLoadResetRcap:3;
	unsigned short coutPolarity:1;
	unsigned short coutSource:1;
#else
#error Unknown bit field order
#endif
};

union cmReg {
	struct counter_mode_register_t reg;
	unsigned short value;
};

struct s526_private {
	unsigned int gpct_config[4];
	unsigned short ai_config;
};

static void s526_gpct_write(struct comedi_device *dev,
			    unsigned int chan, unsigned int val)
{
	/* write high word then low word */
	outw((val >> 16) & 0xffff, dev->iobase + S526_GPCT_MSB_REG(chan));
	outw(val & 0xffff, dev->iobase + S526_GPCT_LSB_REG(chan));
}

static unsigned int s526_gpct_read(struct comedi_device *dev,
				   unsigned int chan)
{
	unsigned int val;

	/* read the low word then high word */
	val = inw(dev->iobase + S526_GPCT_LSB_REG(chan)) & 0xffff;
	val |= (inw(dev->iobase + S526_GPCT_MSB_REG(chan)) & 0xff) << 16;

	return val;
}

static int s526_gpct_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = s526_gpct_read(dev, chan);

	return insn->n;
}

static int s526_gpct_insn_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct s526_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val;
	union cmReg cmReg;

	/*  Check what type of Counter the user requested, data[0] contains */
	/*  the Application type */
	switch (data[0]) {
	case INSN_CONFIG_GPCT_QUADRATURE_ENCODER:
		/*
		   data[0]: Application Type
		   data[1]: Counter Mode Register Value
		   data[2]: Pre-load Register Value
		   data[3]: Conter Control Register
		 */
		devpriv->gpct_config[chan] = data[0];

#if 0
		/*  Example of Counter Application */
		/* One-shot (software trigger) */
		cmReg.reg.coutSource = 0;	/*  out RCAP */
		cmReg.reg.coutPolarity = 1;	/*  Polarity inverted */
		cmReg.reg.autoLoadResetRcap = 0;/*  Auto load disabled */
		cmReg.reg.hwCtEnableSource = 3;	/*  NOT RCAP */
		cmReg.reg.ctEnableCtrl = 2;	/*  Hardware */
		cmReg.reg.clockSource = 2;	/*  Internal */
		cmReg.reg.countDir = 1;	/*  Down */
		cmReg.reg.countDirCtrl = 1;	/*  Software */
		cmReg.reg.outputRegLatchCtrl = 0;	/*  latch on read */
		cmReg.reg.preloadRegSel = 0;	/*  PR0 */
		cmReg.reg.reserved = 0;

		outw(cmReg.value, dev->iobase + S526_GPCT_MODE_REG(chan));

		s526_gpct_write(dev, chan, 0x0013c68);

		/*  Reset the counter */
		outw(0x8000, dev->iobase + S526_GPCT_CTRL_REG(chan));
		/*  Load the counter from PR0 */
		outw(0x4000, dev->iobase + S526_GPCT_CTRL_REG(chan));

		/*  Reset RCAP (fires one-shot) */
		outw(0x0008, dev->iobase + S526_GPCT_CTRL_REG(chan));

#endif

#if 1
		/*  Set Counter Mode Register */
		cmReg.value = data[1] & 0xffff;
		outw(cmReg.value, dev->iobase + S526_GPCT_MODE_REG(chan));

		/*  Reset the counter if it is software preload */
		if (cmReg.reg.autoLoadResetRcap == 0) {
			/*  Reset the counter */
			outw(0x8000, dev->iobase + S526_GPCT_CTRL_REG(chan));
			/* Load the counter from PR0
			 * outw(0x4000, dev->iobase + S526_GPCT_CTRL_REG(chan));
			 */
		}
#else
		/*  0 quadrature, 1 software control */
		cmReg.reg.countDirCtrl = 0;

		/*  data[1] contains GPCT_X1, GPCT_X2 or GPCT_X4 */
		if (data[1] == GPCT_X2)
			cmReg.reg.clockSource = 1;
		else if (data[1] == GPCT_X4)
			cmReg.reg.clockSource = 2;
		else
			cmReg.reg.clockSource = 0;

		/*  When to take into account the indexpulse: */
		/*if (data[2] == GPCT_IndexPhaseLowLow) {
		} else if (data[2] == GPCT_IndexPhaseLowHigh) {
		} else if (data[2] == GPCT_IndexPhaseHighLow) {
		} else if (data[2] == GPCT_IndexPhaseHighHigh) {
		}*/
		/*  Take into account the index pulse? */
		if (data[3] == GPCT_RESET_COUNTER_ON_INDEX)
			/*  Auto load with INDEX^ */
			cmReg.reg.autoLoadResetRcap = 4;

		/*  Set Counter Mode Register */
		cmReg.value = data[1] & 0xffff;
		outw(cmReg.value, dev->iobase + S526_GPCT_MODE_REG(chan));

		/*  Load the pre-load register */
		s526_gpct_write(dev, chan, data[2]);

		/*  Write the Counter Control Register */
		if (data[3])
			outw(data[3] & 0xffff,
			     dev->iobase + S526_GPCT_CTRL_REG(chan));

		/*  Reset the counter if it is software preload */
		if (cmReg.reg.autoLoadResetRcap == 0) {
			/*  Reset the counter */
			outw(0x8000, dev->iobase + S526_GPCT_CTRL_REG(chan));
			/*  Load the counter from PR0 */
			outw(0x4000, dev->iobase + S526_GPCT_CTRL_REG(chan));
		}
#endif
		break;

	case INSN_CONFIG_GPCT_SINGLE_PULSE_GENERATOR:
		/*
		   data[0]: Application Type
		   data[1]: Counter Mode Register Value
		   data[2]: Pre-load Register 0 Value
		   data[3]: Pre-load Register 1 Value
		   data[4]: Conter Control Register
		 */
		devpriv->gpct_config[chan] = data[0];

		/*  Set Counter Mode Register */
		cmReg.value = data[1] & 0xffff;
		cmReg.reg.preloadRegSel = 0;	/*  PR0 */
		outw(cmReg.value, dev->iobase + S526_GPCT_MODE_REG(chan));

		/* Load the pre-load register 0 */
		s526_gpct_write(dev, chan, data[2]);

		/*  Set Counter Mode Register */
		cmReg.value = data[1] & 0xffff;
		cmReg.reg.preloadRegSel = 1;	/*  PR1 */
		outw(cmReg.value, dev->iobase + S526_GPCT_MODE_REG(chan));

		/* Load the pre-load register 1 */
		s526_gpct_write(dev, chan, data[3]);

		/*  Write the Counter Control Register */
		if (data[4]) {
			val = data[4] & 0xffff;
			outw(val, dev->iobase + S526_GPCT_CTRL_REG(chan));
		}
		break;

	case INSN_CONFIG_GPCT_PULSE_TRAIN_GENERATOR:
		/*
		   data[0]: Application Type
		   data[1]: Counter Mode Register Value
		   data[2]: Pre-load Register 0 Value
		   data[3]: Pre-load Register 1 Value
		   data[4]: Conter Control Register
		 */
		devpriv->gpct_config[chan] = data[0];

		/*  Set Counter Mode Register */
		cmReg.value = data[1] & 0xffff;
		cmReg.reg.preloadRegSel = 0;	/*  PR0 */
		outw(cmReg.value, dev->iobase + S526_GPCT_MODE_REG(chan));

		/* Load the pre-load register 0 */
		s526_gpct_write(dev, chan, data[2]);

		/*  Set Counter Mode Register */
		cmReg.value = data[1] & 0xffff;
		cmReg.reg.preloadRegSel = 1;	/*  PR1 */
		outw(cmReg.value, dev->iobase + S526_GPCT_MODE_REG(chan));

		/* Load the pre-load register 1 */
		s526_gpct_write(dev, chan, data[3]);

		/*  Write the Counter Control Register */
		if (data[4]) {
			val = data[4] & 0xffff;
			outw(val, dev->iobase + S526_GPCT_CTRL_REG(chan));
		}
		break;

	default:
		return -EINVAL;
	}

	return insn->n;
}

static int s526_gpct_winsn(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned int *data)
{
	struct s526_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);

	inw(dev->iobase + S526_GPCT_MODE_REG(chan));	/* Is this required? */

	/*  Check what Application of Counter this channel is configured for */
	switch (devpriv->gpct_config[chan]) {
	case INSN_CONFIG_GPCT_PULSE_TRAIN_GENERATOR:
		/* data[0] contains the PULSE_WIDTH
		   data[1] contains the PULSE_PERIOD
		   @pre PULSE_PERIOD > PULSE_WIDTH > 0
		   The above periods must be expressed as a multiple of the
		   pulse frequency on the selected source
		 */
		if ((data[1] <= data[0]) || !data[0])
			return -EINVAL;

		/* Fall thru to write the PULSE_WIDTH */

	case INSN_CONFIG_GPCT_QUADRATURE_ENCODER:
	case INSN_CONFIG_GPCT_SINGLE_PULSE_GENERATOR:
		s526_gpct_write(dev, chan, data[0]);
		break;

	default:
		return -EINVAL;
	}

	return insn->n;
}

#define ISR_ADC_DONE 0x4
static int s526_ai_insn_config(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct s526_private *devpriv = dev->private;
	int result = -EINVAL;

	if (insn->n < 1)
		return result;

	result = insn->n;

	/* data[0] : channels was set in relevant bits.
	   data[1] : delay
	 */
	/* COMMENT: abbotti 2008-07-24: I don't know why you'd want to
	 * enable channels here.  The channel should be enabled in the
	 * INSN_READ handler. */

	/*  Enable ADC interrupt */
	outw(ISR_ADC_DONE, dev->iobase + REG_IER);
	devpriv->ai_config = (data[0] & 0x3ff) << 5;
	if (data[1] > 0)
		devpriv->ai_config |= 0x8000;	/* set the delay */

	devpriv->ai_config |= 0x0001;		/* ADC start bit */

	return result;
}

static int s526_ai_eoc(struct comedi_device *dev,
		       struct comedi_subdevice *s,
		       struct comedi_insn *insn,
		       unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + REG_ISR);
	if (status & ISR_ADC_DONE)
		return 0;
	return -EBUSY;
}

static int s526_ai_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	struct s526_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int n;
	unsigned short value;
	unsigned int d;
	int ret;

	/* Set configured delay, enable channel for this channel only,
	 * select "ADC read" channel, set "ADC start" bit. */
	value = (devpriv->ai_config & 0x8000) |
		((1 << 5) << chan) | (chan << 1) | 0x0001;

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		outw(value, dev->iobase + REG_ADC);

		/* wait for conversion to end */
		ret = comedi_timeout(dev, s, insn, s526_ai_eoc, 0);
		if (ret)
			return ret;

		outw(ISR_ADC_DONE, dev->iobase + REG_ISR);

		/* read data */
		d = inw(dev->iobase + REG_ADD);

		/* munge data */
		data[n] = d ^ 0x8000;
	}

	/* return the number of samples read/written */
	return n;
}

static int s526_ao_insn_write(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	outw(chan << 1, dev->iobase + REG_DAC);

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outw(val, dev->iobase + REG_ADD);
		/* starts the D/A conversion */
		outw((chan << 1) | 1, dev->iobase + REG_DAC);
	}
	s->readback[chan] = val;

	return insn->n;
}

static int s526_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + REG_DIO);

	data[1] = inw(dev->iobase + REG_DIO) & 0xff;

	return insn->n;
}

static int s526_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	if (chan < 4)
		mask = 0x0f;
	else
		mask = 0xf0;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	/* bit 10/11 set the group 1/2's mode */
	if (s->io_bits & 0x0f)
		s->state |= (1 << 10);
	else
		s->state &= ~(1 << 10);
	if (s->io_bits & 0xf0)
		s->state |= (1 << 11);
	else
		s->state &= ~(1 << 11);

	outw(s->state, dev->iobase + REG_DIO);

	return insn->n;
}

static int s526_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct s526_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x40);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* GENERAL-PURPOSE COUNTER/TIME (GPCT) */
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_LSAMPL;
	s->n_chan = 4;
	s->maxdata = 0x00ffffff;	/* 24 bit counter */
	s->insn_read = s526_gpct_rinsn;
	s->insn_config = s526_gpct_insn_config;
	s->insn_write = s526_gpct_winsn;

	s = &dev->subdevices[1];
	/* analog input subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_DIFF;
	/* channels 0 to 7 are the regular differential inputs */
	/* channel 8 is "reference 0" (+10V), channel 9 is "reference 1" (0V) */
	s->n_chan = 10;
	s->maxdata = 0xffff;
	s->range_table = &range_bipolar10;
	s->len_chanlist = 16;
	s->insn_read = s526_ai_rinsn;
	s->insn_config = s526_ai_insn_config;

	s = &dev->subdevices[2];
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 4;
	s->maxdata = 0xffff;
	s->range_table = &range_bipolar10;
	s->insn_write = s526_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	s = &dev->subdevices[3];
	/* digital i/o subdevice */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 8;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = s526_dio_insn_bits;
	s->insn_config = s526_dio_insn_config;

	return 0;
}

static struct comedi_driver s526_driver = {
	.driver_name	= "s526",
	.module		= THIS_MODULE,
	.attach		= s526_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(s526_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
