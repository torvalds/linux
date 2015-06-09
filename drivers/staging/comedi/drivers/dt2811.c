/*
   comedi/drivers/dt2811.c
   Hardware driver for Data Translation DT2811

   COMEDI - Linux Control and Measurement Device Interface
   History:
   Base Version  - David A. Schleef <ds@schleef.org>
   December 1998 - Updated to work.  David does not have a DT2811
   board any longer so this was suffering from bitrot.
   Updated performed by ...

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 */
/*
Driver: dt2811
Description: Data Translation DT2811
Author: ds
Devices: [Data Translation] DT2811-PGL (dt2811-pgl), DT2811-PGH (dt2811-pgh)
Status: works

Configuration options:
  [0] - I/O port base address
  [1] - IRQ, although this is currently unused
  [2] - A/D reference
	  0 = signle-ended
	  1 = differential
	  2 = pseudo-differential (common reference)
  [3] - A/D range
	  0 = [-5, 5]
	  1 = [-2.5, 2.5]
	  2 = [0, 5]
  [4] - D/A 0 range (same choices)
  [4] - D/A 1 range (same choices)
*/

#include <linux/module.h>
#include "../comedidev.h"

static const struct comedi_lrange range_dt2811_pgh_ai_5_unipolar = {
	4, {
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25),
		UNI_RANGE(0.625)
	}
};

static const struct comedi_lrange range_dt2811_pgh_ai_2_5_bipolar = {
	4, {
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		BIP_RANGE(0.3125)
	}
};

static const struct comedi_lrange range_dt2811_pgh_ai_5_bipolar = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625)
	}
};

static const struct comedi_lrange range_dt2811_pgl_ai_5_unipolar = {
	4, {
		UNI_RANGE(5),
		UNI_RANGE(0.5),
		UNI_RANGE(0.05),
		UNI_RANGE(0.01)
	}
};

static const struct comedi_lrange range_dt2811_pgl_ai_2_5_bipolar = {
	4, {
		BIP_RANGE(2.5),
		BIP_RANGE(0.25),
		BIP_RANGE(0.025),
		BIP_RANGE(0.005)
	}
};

static const struct comedi_lrange range_dt2811_pgl_ai_5_bipolar = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(0.5),
		BIP_RANGE(0.05),
		BIP_RANGE(0.01)
	}
};

/*

   0x00    ADCSR R/W  A/D Control/Status Register
   bit 7 - (R) 1 indicates A/D conversion done
   reading ADDAT clears bit
   (W) ignored
   bit 6 - (R) 1 indicates A/D error
   (W) ignored
   bit 5 - (R) 1 indicates A/D busy, cleared at end
   of conversion
   (W) ignored
   bit 4 - (R) 0
   (W)
   bit 3 - (R) 0
   bit 2 - (R/W) 1 indicates interrupts enabled
   bits 1,0 - (R/W) mode bits
   00  single conversion on ADGCR load
   01  continuous conversion, internal clock,
   (clock enabled on ADGCR load)
   10  continuous conversion, internal clock,
   external trigger
   11  continuous conversion, external clock,
   external trigger

   0x01    ADGCR R/W A/D Gain/Channel Register
   bit 6,7 - (R/W) gain select
   00  gain=1, both PGH, PGL models
   01  gain=2 PGH, 10 PGL
   10  gain=4 PGH, 100 PGL
   11  gain=8 PGH, 500 PGL
   bit 4,5 - reserved
   bit 3-0 - (R/W) channel select
   channel number from 0-15

   0x02,0x03 (R) ADDAT A/D Data Register
   (W) DADAT0 D/A Data Register 0
   0x02 low byte
   0x03 high byte

   0x04,0x05 (W) DADAT0 D/A Data Register 1

   0x06 (R) DIO0 Digital Input Port 0
   (W) DIO1 Digital Output Port 1

   0x07 TMRCTR (R/W) Timer/Counter Register
   bits 6,7 - reserved
   bits 5-3 - Timer frequency control (mantissa)
   543  divisor  freqency (kHz)
   000  1        600
   001  10       60
   010  2        300
   011  3        200
   100  4        150
   101  5        120
   110  6        100
   111  12       50
   bits 2-0 - Timer frequency control (exponent)
   210  multiply divisor/divide frequency by
   000  1
   001  10
   010  100
   011  1000
   100  10000
   101  100000
   110  1000000
   111  10000000

 */

#define TIMEOUT 10000

#define DT2811_ADCSR 0
#define DT2811_ADGCR 1
#define DT2811_ADDATLO 2
#define DT2811_ADDATHI 3
#define DT2811_DADAT0LO 2
#define DT2811_DADAT0HI 3
#define DT2811_DADAT1LO 4
#define DT2811_DADAT1HI 5
#define DT2811_DIO 6
#define DT2811_TMRCTR 7

/*
 * flags
 */

/* ADCSR */

#define DT2811_ADDONE   0x80
#define DT2811_ADERROR  0x40
#define DT2811_ADBUSY   0x20
#define DT2811_CLRERROR 0x10
#define DT2811_INTENB   0x04
#define DT2811_ADMODE   0x03

struct dt2811_board {
	const char *name;
	const struct comedi_lrange *bip_5;
	const struct comedi_lrange *bip_2_5;
	const struct comedi_lrange *unip_5;
};

enum { card_2811_pgh, card_2811_pgl };

struct dt2811_private {
	int ntrig;
	int curadchan;
	enum {
		adc_singleended, adc_diff, adc_pseudo_diff
	} adc_mux;
	enum {
		dac_bipolar_5, dac_bipolar_2_5, dac_unipolar_5
	} dac_range[2];
	const struct comedi_lrange *range_type_list[2];
};

static const struct comedi_lrange *dac_range_types[] = {
	&range_bipolar5,
	&range_bipolar2_5,
	&range_unipolar5
};

static int dt2811_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DT2811_ADCSR);
	if ((status & DT2811_ADBUSY) == 0)
		return 0;
	return -EBUSY;
}

static int dt2811_ai_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	int ret;
	int i;

	for (i = 0; i < insn->n; i++) {
		outb(chan, dev->iobase + DT2811_ADGCR);

		ret = comedi_timeout(dev, s, insn, dt2811_ai_eoc, 0);
		if (ret)
			return ret;

		data[i] = inb(dev->iobase + DT2811_ADDATLO);
		data[i] |= inb(dev->iobase + DT2811_ADDATHI) << 8;
		data[i] &= 0xfff;
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
		outb(val & 0xff, dev->iobase + DT2811_DADAT0LO + 2 * chan);
		outb((val >> 8) & 0xff,
		     dev->iobase + DT2811_DADAT0HI + 2 * chan);
	}
	s->readback[chan] = val;

	return insn->n;
}

static int dt2811_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	data[1] = inb(dev->iobase + DT2811_DIO);

	return insn->n;
}

static int dt2811_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + DT2811_DIO);

	data[1] = s->state;

	return insn->n;
}

/*
  options[0]   Board base address
  options[1]   IRQ
  options[2]   Input configuration
		 0 == single-ended
		 1 == differential
		 2 == pseudo-differential
  options[3]   Analog input range configuration
		 0 == bipolar 5  (-5V -- +5V)
		 1 == bipolar 2.5V  (-2.5V -- +2.5V)
		 2 == unipolar 5V  (0V -- +5V)
  options[4]   Analog output 0 range configuration
		 0 == bipolar 5  (-5V -- +5V)
		 1 == bipolar 2.5V  (-2.5V -- +2.5V)
		 2 == unipolar 5V  (0V -- +5V)
  options[5]   Analog output 1 range configuration
		 0 == bipolar 5  (-5V -- +5V)
		 1 == bipolar 2.5V  (-2.5V -- +2.5V)
		 2 == unipolar 5V  (0V -- +5V)
*/
static int dt2811_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	/* int i; */
	const struct dt2811_board *board = dev->board_ptr;
	struct dt2811_private *devpriv;
	int ret;
	struct comedi_subdevice *s;

	ret = comedi_request_region(dev, it->options[0], 0x8);
	if (ret)
		return ret;

#if 0
	outb(0, dev->iobase + DT2811_ADCSR);
	udelay(100);
	i = inb(dev->iobase + DT2811_ADDATLO);
	i = inb(dev->iobase + DT2811_ADDATHI);
#endif

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	switch (it->options[2]) {
	case 0:
		devpriv->adc_mux = adc_singleended;
		break;
	case 1:
		devpriv->adc_mux = adc_diff;
		break;
	case 2:
		devpriv->adc_mux = adc_pseudo_diff;
		break;
	default:
		devpriv->adc_mux = adc_singleended;
		break;
	}
	switch (it->options[4]) {
	case 0:
		devpriv->dac_range[0] = dac_bipolar_5;
		break;
	case 1:
		devpriv->dac_range[0] = dac_bipolar_2_5;
		break;
	case 2:
		devpriv->dac_range[0] = dac_unipolar_5;
		break;
	default:
		devpriv->dac_range[0] = dac_bipolar_5;
		break;
	}
	switch (it->options[5]) {
	case 0:
		devpriv->dac_range[1] = dac_bipolar_5;
		break;
	case 1:
		devpriv->dac_range[1] = dac_bipolar_2_5;
		break;
	case 2:
		devpriv->dac_range[1] = dac_unipolar_5;
		break;
	default:
		devpriv->dac_range[1] = dac_bipolar_5;
		break;
	}

	s = &dev->subdevices[0];
	/* initialize the ADC subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = devpriv->adc_mux == adc_diff ? 8 : 16;
	s->insn_read = dt2811_ai_insn;
	s->maxdata = 0xfff;
	switch (it->options[3]) {
	case 0:
	default:
		s->range_table = board->bip_5;
		break;
	case 1:
		s->range_table = board->bip_2_5;
		break;
	case 2:
		s->range_table = board->unip_5;
		break;
	}

	s = &dev->subdevices[1];
	/* ao subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 2;
	s->maxdata = 0xfff;
	s->range_table_list = devpriv->range_type_list;
	devpriv->range_type_list[0] = dac_range_types[devpriv->dac_range[0]];
	devpriv->range_type_list[1] = dac_range_types[devpriv->dac_range[1]];
	s->insn_write = dt2811_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	s = &dev->subdevices[2];
	/* di subdevice */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 8;
	s->insn_bits = dt2811_di_insn_bits;
	s->maxdata = 1;
	s->range_table = &range_digital;

	s = &dev->subdevices[3];
	/* do subdevice */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 8;
	s->insn_bits = dt2811_do_insn_bits;
	s->maxdata = 1;
	s->state = 0;
	s->range_table = &range_digital;

	return 0;
}

static const struct dt2811_board boardtypes[] = {
	{
		.name		= "dt2811-pgh",
		.bip_5		= &range_dt2811_pgh_ai_5_bipolar,
		.bip_2_5	= &range_dt2811_pgh_ai_2_5_bipolar,
		.unip_5		= &range_dt2811_pgh_ai_5_unipolar,
	}, {
		.name		= "dt2811-pgl",
		.bip_5		= &range_dt2811_pgl_ai_5_bipolar,
		.bip_2_5	= &range_dt2811_pgl_ai_2_5_bipolar,
		.unip_5		= &range_dt2811_pgl_ai_5_unipolar,
	},
};

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
