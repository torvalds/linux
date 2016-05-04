/*
 * Comedi driver for Data Translation DT2811
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * History:
 * Base Version  - David A. Schleef <ds@schleef.org>
 * December 1998 - Updated to work.  David does not have a DT2811
 * board any longer so this was suffering from bitrot.
 * Updated performed by ...
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
 *   [1] - IRQ, although this is currently unused
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
#include "../comedidev.h"

/*
 * Register I/O map
 */
#define DT2811_ADCSR_REG		0x00	/* r/w  A/D Control/Status */
#define DT2811_ADCSR_ADDONE		BIT(7)	/* r      1=A/D conv done */
#define DT2811_ADCSR_ADERROR		BIT(6)	/* r      1=A/D error */
#define DT2811_ADCSR_ADBUSY		BIT(5)	/* r      1=A/D busy */
#define DT2811_ADCSR_CLRERROR		BIT(4)
#define DT2811_ADCSR_INTENB		BIT(2)	/* r/w	  1=interupts ena */
#define DT2811_ADCSR_ADMODE(x)		(((x) & 0x3) << 0)
/* single conversion on ADGCR load */
#define DT2811_ADCSR_ADMODE_SINGLE	DT2811_ADCSR_ADMODE(0)
/* continuous conversion, internal clock, (clock enabled on ADGCR load) */
#define DT2811_ADCSR_ADMODE_CONT	DT2811_ADCSR_ADMODE(1)
/* continuous conversion, internal clock, external trigger */
#define DT2811_ADCSR_ADMODE_EXT_TRIG	DT2811_ADCSR_ADMODE(2)
/* continuous conversion, external clock, external trigger */
#define DT2811_ADCSR_ADMODE_EXT		DT2811_ADCSR_ADMODE(3)

#define DT2811_ADGCR_REG		0x01	/* r/w  A/D Gain/Channel */
#define DT2811_ADGCR_GAIN(x)		(((x) & 0x3) << 6)
#define DT2811_ADGCR_CHAN(x)		(((x) & 0xf) << 0)

#define DT2811_ADDATA_LO_REG		0x02	/* r   A/D Data low byte */
#define DT2811_ADDATA_HI_REG		0x03	/* r   A/D Data high byte */

#define DT2811_DADATA_LO_REG(x)		(0x02 + ((x) * 2)) /* w D/A Data low */
#define DT2811_DADATA_HI_REG(x)		(0x03 + ((x) * 2)) /* w D/A Data high */

#define DT2811_DI_REG			0x06	/* r   Digital Input Port 0 */
#define DT2811_DO_REG			0x06	/* w   Digital Output Port 1 */

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
#define DT2811_TMRCTR_REG		0x07	/* r/w  Timer/Counter */
#define DT2811_TMRCTR_MANTISSA(x)	(((x) & 0x7) << 3)
#define DT2811_TMRCTR_EXPONENT(x)	(((x) & 0x7) << 0)

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

#define TIMEOUT 10000

struct dt2811_board {
	const char *name;
	unsigned int is_pgh:1;
};

static const struct dt2811_board boardtypes[] = {
	{
		.name		= "dt2811-pgh",
		.is_pgh		= 1,
	}, {
		.name		= "dt2811-pgl",
	},
};

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

static int dt2811_ai_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	int ret;
	int i;

	for (i = 0; i < insn->n; i++) {
		unsigned int val;

		/* select channel/gain and trigger conversion */
		outb(DT2811_ADGCR_CHAN(chan) | DT2811_ADGCR_GAIN(range),
		     dev->iobase + DT2811_ADGCR_REG);

		ret = comedi_timeout(dev, s, insn, dt2811_ai_eoc, 0);
		if (ret)
			return ret;

		val = inb(dev->iobase + DT2811_ADDATA_LO_REG) |
		      (inb(dev->iobase + DT2811_ADDATA_HI_REG) << 8);
		val &= s->maxdata;

		data[i] = val;
	}

	return i;
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

static int dt2811_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct dt2811_board *board = dev->board_ptr;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x8);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* initialize the ADC subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE |
			  (it->options[2] == 1) ? SDF_DIFF :
			  (it->options[2] == 2) ? SDF_COMMON : SDF_GROUND;
	s->n_chan = (it->options[2] == 1) ? 8 : 16;
	s->insn_read = dt2811_ai_insn;
	s->maxdata = 0xfff;
	s->range_table	= board->is_pgh ? &dt2811_pgh_ai_ranges
					: &dt2811_pgl_ai_ranges;

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
	.board_name	= &boardtypes[0].name,
	.num_names	= ARRAY_SIZE(boardtypes),
	.offset		= sizeof(struct dt2811_board),
};
module_comedi_driver(dt2811_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
