/*
    comedi/drivers/mpc624.c
    Hardware driver for a Micro/sys inc. MPC-624 PC/104 board

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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
/*
Driver: mpc624
Description: Micro/sys MPC-624 PC/104 board
Devices: [Micro/sys] MPC-624 (mpc624)
Author: Stanislaw Raczynski <sraczynski@op.pl>
Updated: Thu, 15 Sep 2005 12:01:18 +0200
Status: working

    The Micro/sys MPC-624 board is based on the LTC2440 24-bit sigma-delta
    ADC chip.

    Subdevices supported by the driver:
    - Analog In:   supported
    - Digital I/O: not supported
    - LEDs:        not supported
    - EEPROM:      not supported

Configuration Options:
  [0] - I/O base address
  [1] - conversion rate
	Conversion rate  RMS noise  Effective Number Of Bits
	0      3.52kHz        23uV                17
	1      1.76kHz       3.5uV                20
	2       880Hz         2uV                21.3
	3       440Hz        1.4uV               21.8
	4       220Hz         1uV                22.4
	5       110Hz        750uV               22.9
	6       55Hz         510nV               23.4
	7      27.5Hz        375nV                24
	8      13.75Hz       250nV               24.4
	9      6.875Hz       200nV               24.6
  [2] - voltage range
	0      -1.01V .. +1.01V
	1      -10.1V .. +10.1V
*/

#include "../comedidev.h"

#include <linux/ioport.h>
#include <linux/delay.h>

/* Consecutive I/O port addresses */
#define MPC624_SIZE             16

/* Offsets of different ports */
#define MPC624_MASTER_CONTROL	0 /* not used */
#define MPC624_GNMUXCH          1 /* Gain, Mux, Channel of ADC */
#define MPC624_ADC              2 /* read/write to/from ADC */
#define MPC624_EE               3 /* read/write to/from serial EEPROM via I2C */
#define MPC624_LEDS             4 /* write to LEDs */
#define MPC624_DIO              5 /* read/write to/from digital I/O ports */
#define MPC624_IRQ_MASK         6 /* IRQ masking enable/disable */

/* Register bits' names */
#define MPC624_ADBUSY           (1<<5)
#define MPC624_ADSDO            (1<<4)
#define MPC624_ADFO             (1<<3)
#define MPC624_ADCS             (1<<2)
#define MPC624_ADSCK            (1<<1)
#define MPC624_ADSDI            (1<<0)

/* SDI Speed/Resolution Programming bits */
#define MPC624_OSR4             (1<<31)
#define MPC624_OSR3             (1<<30)
#define MPC624_OSR2             (1<<29)
#define MPC624_OSR1             (1<<28)
#define MPC624_OSR0             (1<<27)

/* 32-bit output value bits' names */
#define MPC624_EOC_BIT          (1<<31)
#define MPC624_DMY_BIT          (1<<30)
#define MPC624_SGN_BIT          (1<<29)

/* Conversion speeds */
/* OSR4 OSR3 OSR2 OSR1 OSR0  Conversion rate  RMS noise  ENOB^
 *  X    0    0    0    1        3.52kHz        23uV      17
 *  X    0    0    1    0        1.76kHz       3.5uV      20
 *  X    0    0    1    1         880Hz         2uV      21.3
 *  X    0    1    0    0         440Hz        1.4uV     21.8
 *  X    0    1    0    1         220Hz         1uV      22.4
 *  X    0    1    1    0         110Hz        750uV     22.9
 *  X    0    1    1    1          55Hz        510nV     23.4
 *  X    1    0    0    0         27.5Hz       375nV      24
 *  X    1    0    0    1        13.75Hz       250nV     24.4
 *  X    1    1    1    1        6.875Hz       200nV     24.6
 *
 * ^ - Effective Number Of Bits
 */

#define MPC624_SPEED_3_52_kHz (MPC624_OSR4 | MPC624_OSR0)
#define MPC624_SPEED_1_76_kHz (MPC624_OSR4 | MPC624_OSR1)
#define MPC624_SPEED_880_Hz   (MPC624_OSR4 | MPC624_OSR1 | MPC624_OSR0)
#define MPC624_SPEED_440_Hz   (MPC624_OSR4 | MPC624_OSR2)
#define MPC624_SPEED_220_Hz   (MPC624_OSR4 | MPC624_OSR2 | MPC624_OSR0)
#define MPC624_SPEED_110_Hz   (MPC624_OSR4 | MPC624_OSR2 | MPC624_OSR1)
#define MPC624_SPEED_55_Hz \
	(MPC624_OSR4 | MPC624_OSR2 | MPC624_OSR1 | MPC624_OSR0)
#define MPC624_SPEED_27_5_Hz  (MPC624_OSR4 | MPC624_OSR3)
#define MPC624_SPEED_13_75_Hz (MPC624_OSR4 | MPC624_OSR3 | MPC624_OSR0)
#define MPC624_SPEED_6_875_Hz \
	(MPC624_OSR4 | MPC624_OSR3 | MPC624_OSR2 | MPC624_OSR1 | MPC624_OSR0)
/* -------------------------------------------------------------------------- */
struct mpc624_private {

	/*  set by mpc624_attach() from driver's parameters */
	unsigned long int ulConvertionRate;
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

/* Timeout 200ms */
#define TIMEOUT 200

static int mpc624_ai_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	struct mpc624_private *devpriv = dev->private;
	int n, i;
	unsigned long int data_in, data_out;
	unsigned char ucPort;

	/*
	 *  WARNING:
	 *  We always write 0 to GNSWA bit, so the channel range is +-/10.1Vdc
	 */
	outb(insn->chanspec, dev->iobase + MPC624_GNMUXCH);
/* printk("Channel %d:\n", insn->chanspec); */
	if (!insn->n) {
		printk(KERN_INFO "MPC624: Warning, no data to acquire\n");
		return 0;
	}

	for (n = 0; n < insn->n; n++) {
		/*  Trigger the conversion */
		outb(MPC624_ADSCK, dev->iobase + MPC624_ADC);
		udelay(1);
		outb(MPC624_ADCS | MPC624_ADSCK, dev->iobase + MPC624_ADC);
		udelay(1);
		outb(0, dev->iobase + MPC624_ADC);
		udelay(1);

		/*  Wait for the conversion to end */
		for (i = 0; i < TIMEOUT; i++) {
			ucPort = inb(dev->iobase + MPC624_ADC);
			if (ucPort & MPC624_ADBUSY)
				udelay(1000);
			else
				break;
		}
		if (i == TIMEOUT) {
			printk(KERN_ERR "MPC624: timeout (%dms)\n", TIMEOUT);
			data[n] = 0;
			return -ETIMEDOUT;
		}
		/*  Start reading data */
		data_in = 0;
		data_out = devpriv->ulConvertionRate;
		udelay(1);
		for (i = 0; i < 32; i++) {
			/*  Set the clock low */
			outb(0, dev->iobase + MPC624_ADC);
			udelay(1);

			if (data_out & (1 << 31)) { /*  the next bit is a 1 */
				/*  Set the ADSDI line (send to MPC624) */
				outb(MPC624_ADSDI, dev->iobase + MPC624_ADC);
				udelay(1);
				/*  Set the clock high */
				outb(MPC624_ADSCK | MPC624_ADSDI,
				     dev->iobase + MPC624_ADC);
			} else {	/*  the next bit is a 0 */

				/*  Set the ADSDI line (send to MPC624) */
				outb(0, dev->iobase + MPC624_ADC);
				udelay(1);
				/*  Set the clock high */
				outb(MPC624_ADSCK, dev->iobase + MPC624_ADC);
			}
			/*  Read ADSDO on high clock (receive from MPC624) */
			udelay(1);
			data_in <<= 1;
			data_in |=
			    (inb(dev->iobase + MPC624_ADC) & MPC624_ADSDO) >> 4;
			udelay(1);

			data_out <<= 1;
		}

		/*
		 *  Received 32-bit long value consist of:
		 *    31: EOC -
		 *          (End Of Transmission) bit - should be 0
		 *    30: DMY
		 *          (Dummy) bit - should be 0
		 *    29: SIG
		 *          (Sign) bit- 1 if the voltage is positive,
		 *                      0 if negative
		 *    28: MSB
		 *          (Most Significant Bit) - the first bit of
		 *                                   the conversion result
		 *    ....
		 *    05: LSB
		 *          (Least Significant Bit)- the last bit of the
		 *                                   conversion result
		 *    04-00: sub-LSB
		 *          - sub-LSBs are basically noise, but when
		 *            averaged properly, they can increase conversion
		 *            precision up to 29 bits; they can be discarded
		 *            without loss of resolution.
		 */

		if (data_in & MPC624_EOC_BIT)
			printk(KERN_INFO "MPC624:EOC bit is set (data_in=%lu)!",
			       data_in);
		if (data_in & MPC624_DMY_BIT)
			printk(KERN_INFO "MPC624:DMY bit is set (data_in=%lu)!",
			       data_in);
		if (data_in & MPC624_SGN_BIT) {	/* Volatge is positive */
			/*
			 * comedi operates on unsigned numbers, so mask off EOC
			 * and DMY and don't clear the SGN bit
			 */
			data_in &= 0x3FFFFFFF;
			data[n] = data_in;
		} else { /*  The voltage is negative */
			/*
			 * data_in contains a number in 30-bit two's complement
			 * code and we must deal with it
			 */
			data_in |= MPC624_SGN_BIT;
			data_in = ~data_in;
			data_in += 1;
			data_in &= ~(MPC624_EOC_BIT | MPC624_DMY_BIT);
			/*  clear EOC and DMY bits */
			data_in = 0x20000000 - data_in;
			data[n] = data_in;
		}
	}

	/*  Return the number of samples read/written */
	return n;
}

static int mpc624_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct mpc624_private *devpriv;
	struct comedi_subdevice *s;
	unsigned long iobase;
	int ret;

	iobase = it->options[0];
	printk(KERN_INFO "comedi%d: mpc624 [0x%04lx, ", dev->minor, iobase);
	if (request_region(iobase, MPC624_SIZE, "mpc624") == NULL) {
		printk(KERN_ERR "I/O port(s) in use\n");
		return -EIO;
	}

	dev->iobase = iobase;
	dev->board_name = "mpc624";

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	switch (it->options[1]) {
	case 0:
		devpriv->ulConvertionRate = MPC624_SPEED_3_52_kHz;
		printk(KERN_INFO "3.52 kHz, ");
		break;
	case 1:
		devpriv->ulConvertionRate = MPC624_SPEED_1_76_kHz;
		printk(KERN_INFO "1.76 kHz, ");
		break;
	case 2:
		devpriv->ulConvertionRate = MPC624_SPEED_880_Hz;
		printk(KERN_INFO "880 Hz, ");
		break;
	case 3:
		devpriv->ulConvertionRate = MPC624_SPEED_440_Hz;
		printk(KERN_INFO "440 Hz, ");
		break;
	case 4:
		devpriv->ulConvertionRate = MPC624_SPEED_220_Hz;
		printk(KERN_INFO "220 Hz, ");
		break;
	case 5:
		devpriv->ulConvertionRate = MPC624_SPEED_110_Hz;
		printk(KERN_INFO "110 Hz, ");
		break;
	case 6:
		devpriv->ulConvertionRate = MPC624_SPEED_55_Hz;
		printk(KERN_INFO "55 Hz, ");
		break;
	case 7:
		devpriv->ulConvertionRate = MPC624_SPEED_27_5_Hz;
		printk(KERN_INFO "27.5 Hz, ");
		break;
	case 8:
		devpriv->ulConvertionRate = MPC624_SPEED_13_75_Hz;
		printk(KERN_INFO "13.75 Hz, ");
		break;
	case 9:
		devpriv->ulConvertionRate = MPC624_SPEED_6_875_Hz;
		printk(KERN_INFO "6.875 Hz, ");
		break;
	default:
		printk
		    (KERN_ERR "illegal conversion rate setting!"
			" Valid numbers are 0..9. Using 9 => 6.875 Hz, ");
		devpriv->ulConvertionRate = MPC624_SPEED_3_52_kHz;
	}

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_DIFF;
	s->n_chan = 8;
	switch (it->options[1]) {
	default:
		s->maxdata = 0x3FFFFFFF;
		printk(KERN_INFO "30 bit, ");
	}

	switch (it->options[1]) {
	case 0:
		s->range_table = &range_mpc624_bipolar1;
		printk(KERN_INFO "1.01V]: ");
		break;
	default:
		s->range_table = &range_mpc624_bipolar10;
		printk(KERN_INFO "10.1V]: ");
	}
	s->len_chanlist = 1;
	s->insn_read = mpc624_ai_rinsn;

	printk(KERN_INFO "attached\n");

	return 1;
}

static void mpc624_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		release_region(dev->iobase, MPC624_SIZE);
}

static struct comedi_driver mpc624_driver = {
	.driver_name	= "mpc624",
	.module		= THIS_MODULE,
	.attach		= mpc624_attach,
	.detach		= mpc624_detach
};
module_comedi_driver(mpc624_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
