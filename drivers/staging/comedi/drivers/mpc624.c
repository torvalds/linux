/*
 * mpc624.c
 * Hardware driver for a Micro/sys inc. MPC-624 PC/104 board
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
 * Driver: mpc624
 * Description: Micro/sys MPC-624 PC/104 board
 * Devices: [Micro/sys] MPC-624 (mpc624)
 * Author: Stanislaw Raczynski <sraczynski@op.pl>
 * Updated: Thu, 15 Sep 2005 12:01:18 +0200
 * Status: working
 *
 * The Micro/sys MPC-624 board is based on the LTC2440 24-bit sigma-delta
 * ADC chip.
 *
 * Subdevices supported by the driver:
 * - Analog In:   supported
 * - Digital I/O: not supported
 * - LEDs:        not supported
 * - EEPROM:      not supported
 *
 * Configuration Options:
 *   [0] - I/O base address
 *   [1] - conversion rate
 *	   Conversion rate   RMS noise	Effective Number Of Bits
 *	   0	3.52kHz		23uV		17
 *	   1	1.76kHz		3.5uV		20
 *	   2	880Hz		2uV		21.3
 *	   3	440Hz		1.4uV		21.8
 *	   4	220Hz		1uV		22.4
 *	   5	110Hz		750uV		22.9
 *	   6	55Hz		510nV		23.4
 *	   7	27.5Hz		375nV		24
 *	   8	13.75Hz		250nV		24.4
 *	   9	6.875Hz		200nV		24.6
 *   [2] - voltage range
 *	   0	-1.01V .. +1.01V
 *	   1	-10.1V .. +10.1V
 */

#include <linux/module.h>
#include "../comedidev.h"

#include <linux/delay.h>

/* Offsets of different ports */
#define MPC624_MASTER_CONTROL	0 /* not used */
#define MPC624_GNMUXCH		1 /* Gain, Mux, Channel of ADC */
#define MPC624_ADC		2 /* read/write to/from ADC */
#define MPC624_EE		3 /* read/write to/from serial EEPROM via I2C */
#define MPC624_LEDS		4 /* write to LEDs */
#define MPC624_DIO		5 /* read/write to/from digital I/O ports */
#define MPC624_IRQ_MASK		6 /* IRQ masking enable/disable */

/* Register bits' names */
#define MPC624_ADBUSY		BIT(5)
#define MPC624_ADSDO		BIT(4)
#define MPC624_ADFO		BIT(3)
#define MPC624_ADCS		BIT(2)
#define MPC624_ADSCK		BIT(1)
#define MPC624_ADSDI		BIT(0)

/* 32-bit output value bits' names */
#define MPC624_EOC_BIT		BIT(31)
#define MPC624_DMY_BIT		BIT(30)
#define MPC624_SGN_BIT		BIT(29)

/* SDI Speed/Resolution Programming bits */
#define MPC624_OSR(x)		(((x) & 0x1f) << 27)
#define MPC624_SPEED_3_52_KHZ	MPC624_OSR(0x11)
#define MPC624_SPEED_1_76_KHZ	MPC624_OSR(0x12)
#define MPC624_SPEED_880_HZ	MPC624_OSR(0x13)
#define MPC624_SPEED_440_HZ	MPC624_OSR(0x14)
#define MPC624_SPEED_220_HZ	MPC624_OSR(0x15)
#define MPC624_SPEED_110_HZ	MPC624_OSR(0x16)
#define MPC624_SPEED_55_HZ	MPC624_OSR(0x17)
#define MPC624_SPEED_27_5_HZ	MPC624_OSR(0x18)
#define MPC624_SPEED_13_75_HZ	MPC624_OSR(0x19)
#define MPC624_SPEED_6_875_HZ	MPC624_OSR(0x1f)

struct mpc624_private {
	unsigned int ai_speed;
};

/* -------------------------------------------------------------------------- */
static const struct comedi_lrange range_mpc624_bipolar1 = {
	1,
	{
/* BIP_RANGE(1.01)  this is correct, */
	 /*  but my MPC-624 actually seems to have a range of 2.02 */
	 BIP_RANGE(2.02)
	}
};

static const struct comedi_lrange range_mpc624_bipolar10 = {
	1,
	{
/* BIP_RANGE(10.1)   this is correct, */
	 /*  but my MPC-624 actually seems to have a range of 20.2 */
	 BIP_RANGE(20.2)
	}
};

static unsigned int mpc624_ai_get_sample(struct comedi_device *dev,
					 struct comedi_subdevice *s)
{
	struct mpc624_private *devpriv = dev->private;
	unsigned int data_out = devpriv->ai_speed;
	unsigned int data_in = 0;
	unsigned int bit;
	int i;

	/* Start reading data */
	udelay(1);
	for (i = 0; i < 32; i++) {
		/* Set the clock low */
		outb(0, dev->iobase + MPC624_ADC);
		udelay(1);

		/* Set the ADSDI line for the next bit (send to MPC624) */
		bit = (data_out & BIT(31)) ? MPC624_ADSDI : 0;
		outb(bit, dev->iobase + MPC624_ADC);
		udelay(1);

		/* Set the clock high */
		outb(MPC624_ADSCK | bit, dev->iobase + MPC624_ADC);
		udelay(1);

		/* Read ADSDO on high clock (receive from MPC624) */
		data_in <<= 1;
		data_in |= (inb(dev->iobase + MPC624_ADC) & MPC624_ADSDO) >> 4;
		udelay(1);

		data_out <<= 1;
	}

	/*
	 * Received 32-bit long value consist of:
	 *	31: EOC - (End Of Transmission) bit - should be 0
	 *	30: DMY - (Dummy) bit - should be 0
	 *	29: SIG - (Sign) bit - 1 if positive, 0 if negative
	 *	28: MSB - (Most Significant Bit) - the first bit of the
	 *					   conversion result
	 *	....
	 *	05: LSB - (Least Significant Bit)- the last bit of the
	 *					   conversion result
	 *	04-00: sub-LSB - sub-LSBs are basically noise, but when
	 *			 averaged properly, they can increase
	 *			 conversion precision up to 29 bits;
	 *			 they can be discarded without loss of
	 *			 resolution.
	 */
	if (data_in & MPC624_EOC_BIT)
		dev_dbg(dev->class_dev, "EOC bit is set!");
	if (data_in & MPC624_DMY_BIT)
		dev_dbg(dev->class_dev, "DMY bit is set!");

	if (data_in & MPC624_SGN_BIT) {
		/*
		 * Voltage is positive
		 *
		 * comedi operates on unsigned numbers, so mask off EOC
		 * and DMY and don't clear the SGN bit
		 */
		data_in &= 0x3fffffff;
	} else {
		/*
		 * The voltage is negative
		 *
		 * data_in contains a number in 30-bit two's complement
		 * code and we must deal with it
		 */
		data_in |= MPC624_SGN_BIT;
		data_in = ~data_in;
		data_in += 1;
		/* clear EOC and DMY bits */
		data_in &= ~(MPC624_EOC_BIT | MPC624_DMY_BIT);
		data_in = 0x20000000 - data_in;
	}
	return data_in;
}

static int mpc624_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned char status;

	status = inb(dev->iobase + MPC624_ADC);
	if ((status & MPC624_ADBUSY) == 0)
		return 0;
	return -EBUSY;
}

static int mpc624_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	int ret;
	int i;

	/*
	 *  WARNING:
	 *  We always write 0 to GNSWA bit, so the channel range is +-/10.1Vdc
	 */
	outb(insn->chanspec, dev->iobase + MPC624_GNMUXCH);

	for (i = 0; i < insn->n; i++) {
		/*  Trigger the conversion */
		outb(MPC624_ADSCK, dev->iobase + MPC624_ADC);
		udelay(1);
		outb(MPC624_ADCS | MPC624_ADSCK, dev->iobase + MPC624_ADC);
		udelay(1);
		outb(0, dev->iobase + MPC624_ADC);
		udelay(1);

		/*  Wait for the conversion to end */
		ret = comedi_timeout(dev, s, insn, mpc624_ai_eoc, 0);
		if (ret)
			return ret;

		data[i] = mpc624_ai_get_sample(dev, s);
	}

	return insn->n;
}

static int mpc624_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct mpc624_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	switch (it->options[1]) {
	case 0:
		devpriv->ai_speed = MPC624_SPEED_3_52_KHZ;
		break;
	case 1:
		devpriv->ai_speed = MPC624_SPEED_1_76_KHZ;
		break;
	case 2:
		devpriv->ai_speed = MPC624_SPEED_880_HZ;
		break;
	case 3:
		devpriv->ai_speed = MPC624_SPEED_440_HZ;
		break;
	case 4:
		devpriv->ai_speed = MPC624_SPEED_220_HZ;
		break;
	case 5:
		devpriv->ai_speed = MPC624_SPEED_110_HZ;
		break;
	case 6:
		devpriv->ai_speed = MPC624_SPEED_55_HZ;
		break;
	case 7:
		devpriv->ai_speed = MPC624_SPEED_27_5_HZ;
		break;
	case 8:
		devpriv->ai_speed = MPC624_SPEED_13_75_HZ;
		break;
	case 9:
		devpriv->ai_speed = MPC624_SPEED_6_875_HZ;
		break;
	default:
		devpriv->ai_speed = MPC624_SPEED_3_52_KHZ;
	}

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_DIFF;
	s->n_chan	= 4;
	s->maxdata	= 0x3fffffff;
	s->range_table	= (it->options[1] == 0) ? &range_mpc624_bipolar1
						: &range_mpc624_bipolar10;
	s->insn_read	= mpc624_ai_insn_read;

	return 0;
}

static struct comedi_driver mpc624_driver = {
	.driver_name	= "mpc624",
	.module		= THIS_MODULE,
	.attach		= mpc624_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(mpc624_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Micro/sys MPC-624 PC/104 board");
MODULE_LICENSE("GPL");
