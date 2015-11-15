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

/*
 * Register I/O map
 */
#define S526_TIMER_REG		0x00
#define S526_TIMER_LOAD(x)	(((x) & 0xff) << 8)
#define S526_TIMER_MODE		((x) << 1)
#define S526_TIMER_MANUAL	S526_TIMER_MODE(0)
#define S526_TIMER_AUTO		S526_TIMER_MODE(1)
#define S526_TIMER_RESTART	BIT(0)
#define S526_WDOG_REG		0x02
#define S526_WDOG_INVERTED	BIT(4)
#define S526_WDOG_ENA		BIT(3)
#define S526_WDOG_INTERVAL(x)	(((x) & 0x7) << 0)
#define S526_AO_CTRL_REG	0x04
#define S526_AO_CTRL_RESET	BIT(3)
#define S526_AO_CTRL_CHAN(x)	(((x) & 0x3) << 1)
#define S526_AO_CTRL_START	BIT(0)
#define S526_AI_CTRL_REG	0x06
#define S526_AI_CTRL_DELAY	BIT(15)
#define S526_AI_CTRL_CONV(x)	(1 << (5 + ((x) & 0x9)))
#define S526_AI_CTRL_READ(x)	(((x) & 0xf) << 1)
#define S526_AI_CTRL_START	BIT(0)
#define S526_AO_REG		0x08
#define S526_AI_REG		0x08
#define S526_DIO_CTRL_REG	0x0a
#define S526_DIO_CTRL_DIO3_NEG	BIT(15)	/* irq on DIO3 neg/pos edge */
#define S526_DIO_CTRL_DIO2_NEG	BIT(14)	/* irq on DIO2 neg/pos edge */
#define S526_DIO_CTRL_DIO1_NEG	BIT(13)	/* irq on DIO1 neg/pos edge */
#define S526_DIO_CTRL_DIO0_NEG	BIT(12)	/* irq on DIO0 neg/pos edge */
#define S526_DIO_CTRL_GRP2_OUT	BIT(11)
#define S526_DIO_CTRL_GRP1_OUT	BIT(10)
#define S526_DIO_CTRL_GRP2_NEG	BIT(8)	/* irq on DIO[4-7] neg/pos edge */
#define S526_INT_ENA_REG	0x0c
#define S526_INT_STATUS_REG	0x0e
#define S526_INT_DIO(x)		BIT(8 + ((x) & 0x7))
#define S526_INT_EEPROM		BIT(7)	/* status only */
#define S526_INT_CNTR(x)	BIT(3 + (3 - ((x) & 0x3)))
#define S526_INT_AI		BIT(2)
#define S526_INT_AO		BIT(1)
#define S526_INT_TIMER		BIT(0)
#define S526_MISC_REG		0x10
#define S526_MISC_LED_OFF	BIT(0)
#define S526_GPCT_LSB_REG(x)	(0x12 + ((x) * 8))
#define S526_GPCT_MSB_REG(x)	(0x14 + ((x) * 8))
#define S526_GPCT_MODE_REG(x)	(0x16 + ((x) * 8))
#define S526_GPCT_CTRL_REG(x)	(0x18 + ((x) * 8))
#define S526_EEPROM_DATA_REG	0x32
#define S526_EEPROM_CTRL_REG	0x34
#define S526_EEPROM_CTRL_ADDR(x) (((x) & 0x3f) << 3)
#define S526_EEPROM_CTRL(x)	(((x) & 0x3) << 1)
#define S526_EEPROM_CTRL_READ	S526_EEPROM_CTRL(2)
#define S526_EEPROM_CTRL_START	BIT(0)

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
	unsigned short ai_ctrl;
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

	/*
	 * Check what type of Counter the user requested
	 * data[0] contains the Application type
	 */
	switch (data[0]) {
	case INSN_CONFIG_GPCT_QUADRATURE_ENCODER:
		/*
		 * data[0]: Application Type
		 * data[1]: Counter Mode Register Value
		 * data[2]: Pre-load Register Value
		 * data[3]: Conter Control Register
		 */
		devpriv->gpct_config[chan] = data[0];

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
		/*
		 * if (data[2] == GPCT_IndexPhaseLowLow) {
		 * } else if (data[2] == GPCT_IndexPhaseLowHigh) {
		 * } else if (data[2] == GPCT_IndexPhaseHighLow) {
		 * } else if (data[2] == GPCT_IndexPhaseHighHigh) {
		 * }
		 */
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
		 * data[0]: Application Type
		 * data[1]: Counter Mode Register Value
		 * data[2]: Pre-load Register 0 Value
		 * data[3]: Pre-load Register 1 Value
		 * data[4]: Conter Control Register
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
		 * data[0]: Application Type
		 * data[1]: Counter Mode Register Value
		 * data[2]: Pre-load Register 0 Value
		 * data[3]: Pre-load Register 1 Value
		 * data[4]: Conter Control Register
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
		/*
		 * data[0] contains the PULSE_WIDTH
		 * data[1] contains the PULSE_PERIOD
		 * @pre PULSE_PERIOD > PULSE_WIDTH > 0
		 * The above periods must be expressed as a multiple of the
		 * pulse frequency on the selected source
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

static int s526_eoc(struct comedi_device *dev,
		    struct comedi_subdevice *s,
		    struct comedi_insn *insn,
		    unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + S526_INT_STATUS_REG);
	if (status & context) {
		/* we got our eoc event, clear it */
		outw(context, dev->iobase + S526_INT_STATUS_REG);
		return 0;
	}
	return -EBUSY;
}

static int s526_ai_insn_read(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	struct s526_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int ctrl;
	unsigned int val;
	int ret;
	int i;

	ctrl = S526_AI_CTRL_CONV(chan) | S526_AI_CTRL_READ(chan) |
	       S526_AI_CTRL_START;
	if (ctrl != devpriv->ai_ctrl) {
		/*
		 * The multiplexor needs to change, enable the 15us
		 * delay for the first sample.
		 */
		devpriv->ai_ctrl = ctrl;
		ctrl |= S526_AI_CTRL_DELAY;
	}

	for (i = 0; i < insn->n; i++) {
		/* trigger conversion */
		outw(ctrl, dev->iobase + S526_AI_CTRL_REG);
		ctrl &= ~S526_AI_CTRL_DELAY;

		/* wait for conversion to end */
		ret = comedi_timeout(dev, s, insn, s526_eoc, S526_INT_AI);
		if (ret)
			return ret;

		val = inw(dev->iobase + S526_AI_REG);
		data[i] = comedi_offset_munge(s, val);
	}

	return insn->n;
}

static int s526_ao_insn_write(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int ctrl = S526_AO_CTRL_CHAN(chan);
	unsigned int val = s->readback[chan];
	int ret;
	int i;

	outw(ctrl, dev->iobase + S526_AO_CTRL_REG);
	ctrl |= S526_AO_CTRL_START;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outw(val, dev->iobase + S526_AO_REG);
		outw(ctrl, dev->iobase + S526_AO_CTRL_REG);

		/* wait for conversion to end */
		ret = comedi_timeout(dev, s, insn, s526_eoc, S526_INT_AO);
		if (ret)
			return ret;
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
		outw(s->state, dev->iobase + S526_DIO_CTRL_REG);

	data[1] = inw(dev->iobase + S526_DIO_CTRL_REG) & 0xff;

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

	/*
	 * Digital I/O can be configured as inputs or outputs in
	 * groups of 4; DIO group 1 (DIO0-3) and DIO group 2 (DIO4-7).
	 */
	if (chan < 4)
		mask = 0x0f;
	else
		mask = 0xf0;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	if (s->io_bits & 0x0f)
		s->state |= S526_DIO_CTRL_GRP1_OUT;
	else
		s->state &= ~S526_DIO_CTRL_GRP1_OUT;
	if (s->io_bits & 0xf0)
		s->state |= S526_DIO_CTRL_GRP2_OUT;
	else
		s->state &= ~S526_DIO_CTRL_GRP2_OUT;

	outw(s->state, dev->iobase + S526_DIO_CTRL_REG);

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

	/* General-Purpose Counter/Timer (GPCT) */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_LSAMPL;
	s->n_chan	= 4;
	s->maxdata	= 0x00ffffff;
	s->insn_read	= s526_gpct_rinsn;
	s->insn_config	= s526_gpct_insn_config;
	s->insn_write	= s526_gpct_winsn;

	/*
	 * Analog Input subdevice
	 * channels 0 to 7 are the regular differential inputs
	 * channel 8 is "reference 0" (+10V)
	 * channel 9 is "reference 1" (0V)
	 */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_DIFF;
	s->n_chan	= 10;
	s->maxdata	= 0xffff;
	s->range_table	= &range_bipolar10;
	s->len_chanlist	= 16;
	s->insn_read	= s526_ai_insn_read;

	/* Analog Output subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 0xffff;
	s->range_table	= &range_bipolar10;
	s->insn_write	= s526_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital I/O subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= s526_dio_insn_bits;
	s->insn_config	= s526_dio_insn_config;

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
