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

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
          0 = [-5,5]
	  1 = [-2.5,2.5]
	  2 = [0,5]
  [4] - D/A 0 range (same choices)
  [4] - D/A 1 range (same choices)
*/

#include "../comedidev.h"

#include <linux/ioport.h>

static const char *driver_name = "dt2811";

static const struct comedi_lrange range_dt2811_pgh_ai_5_unipolar = { 4, {
			RANGE(0, 5),
			RANGE(0, 2.5),
			RANGE(0, 1.25),
			RANGE(0, 0.625)
	}
};
static const struct comedi_lrange range_dt2811_pgh_ai_2_5_bipolar = { 4, {
			RANGE(-2.5, 2.5),
			RANGE(-1.25, 1.25),
			RANGE(-0.625, 0.625),
			RANGE(-0.3125, 0.3125)
	}
};
static const struct comedi_lrange range_dt2811_pgh_ai_5_bipolar = { 4, {
			RANGE(-5, 5),
			RANGE(-2.5, 2.5),
			RANGE(-1.25, 1.25),
			RANGE(-0.625, 0.625)
	}
};
static const struct comedi_lrange range_dt2811_pgl_ai_5_unipolar = { 4, {
			RANGE(0, 5),
			RANGE(0, 0.5),
			RANGE(0, 0.05),
			RANGE(0, 0.01)
	}
};
static const struct comedi_lrange range_dt2811_pgl_ai_2_5_bipolar = { 4, {
			RANGE(-2.5, 2.5),
			RANGE(-0.25, 0.25),
			RANGE(-0.025, 0.025),
			RANGE(-0.005, 0.005)
	}
};
static const struct comedi_lrange range_dt2811_pgl_ai_5_bipolar = { 4, {
			RANGE(-5, 5),
			RANGE(-0.5, 0.5),
			RANGE(-0.05, 0.05),
			RANGE(-0.01, 0.01)
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

#define DT2811_SIZE 8

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

static const struct dt2811_board boardtypes[] = {
	{"dt2811-pgh",
			&range_dt2811_pgh_ai_5_bipolar,
			&range_dt2811_pgh_ai_2_5_bipolar,
			&range_dt2811_pgh_ai_5_unipolar,
		},
	{"dt2811-pgl",
			&range_dt2811_pgl_ai_5_bipolar,
			&range_dt2811_pgl_ai_2_5_bipolar,
			&range_dt2811_pgl_ai_5_unipolar,
		},
};

#define this_board ((const struct dt2811_board *)dev->board_ptr)

static int dt2811_attach(struct comedi_device * dev, struct comedi_devconfig * it);
static int dt2811_detach(struct comedi_device * dev);
static struct comedi_driver driver_dt2811 = {
      driver_name:"dt2811",
      module:THIS_MODULE,
      attach:dt2811_attach,
      detach:dt2811_detach,
      board_name:&boardtypes[0].name,
      num_names:sizeof(boardtypes) / sizeof(struct dt2811_board),
      offset:sizeof(struct dt2811_board),
};

COMEDI_INITCLEANUP(driver_dt2811);

static int dt2811_ai_insn(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data);
static int dt2811_ao_insn(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data);
static int dt2811_ao_insn_read(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data);
static int dt2811_di_insn_bits(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data);
static int dt2811_do_insn_bits(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data);

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
	unsigned int ao_readback[2];
};

#define devpriv ((struct dt2811_private *)dev->private)

static const struct comedi_lrange *dac_range_types[] = {
	&range_bipolar5,
	&range_bipolar2_5,
	&range_unipolar5
};

#define DT2811_TIMEOUT 5

#if 0
static irqreturn_t dt2811_interrupt(int irq, void *d PT_REGS_ARG)
{
	int lo, hi;
	int data;
	struct comedi_device *dev = d;

	if (!dev->attached) {
		comedi_error(dev, "spurious interrupt");
		return IRQ_HANDLED;
	}

	lo = inb(dev->iobase + DT2811_ADDATLO);
	hi = inb(dev->iobase + DT2811_ADDATHI);

	data = lo + (hi << 8);

	if (!(--devpriv->ntrig)) {
		/* how to turn off acquisition */
		s->async->events |= COMEDI_SB_EOA;
	}
	comedi_event(dev, s);
	return IRQ_HANDLED;
}
#endif

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

static int dt2811_attach(struct comedi_device * dev, struct comedi_devconfig * it)
{
	//int i, irq;
	//unsigned long irqs;
	//long flags;
	int ret;
	struct comedi_subdevice *s;
	unsigned long iobase;

	iobase = it->options[0];

	printk("comedi%d: dt2811: base=0x%04lx\n", dev->minor, iobase);

	if (!request_region(iobase, DT2811_SIZE, driver_name)) {
		printk("I/O port conflict\n");
		return -EIO;
	}

	dev->iobase = iobase;
	dev->board_name = this_board->name;

#if 0
	outb(0, dev->iobase + DT2811_ADCSR);
	comedi_udelay(100);
	i = inb(dev->iobase + DT2811_ADDATLO);
	i = inb(dev->iobase + DT2811_ADDATHI);
#endif

#if 0
	irq = it->options[1];
	if (irq < 0) {
		save_flags(flags);
		sti();
		irqs = probe_irq_on();

		outb(DT2811_CLRERROR | DT2811_INTENB,
			dev->iobase + DT2811_ADCSR);
		outb(0, dev->iobase + DT2811_ADGCR);

		comedi_udelay(100);

		irq = probe_irq_off(irqs);
		restore_flags(flags);

		/*outb(DT2811_CLRERROR|DT2811_INTENB,dev->iobase+DT2811_ADCSR); */

		if (inb(dev->iobase + DT2811_ADCSR) & DT2811_ADERROR) {
			printk("error probing irq (bad) \n");
		}
		dev->irq = 0;
		if (irq > 0) {
			i = inb(dev->iobase + DT2811_ADDATLO);
			i = inb(dev->iobase + DT2811_ADDATHI);
			printk("(irq = %d)\n", irq);
			ret = comedi_request_irq(irq, dt2811_interrupt, 0,
				driver_name, dev);
			if (ret < 0)
				return -EIO;
			dev->irq = irq;
		} else if (irq == 0) {
			printk("(no irq)\n");
		} else {
			printk("( multiple irq's -- this is bad! )\n");
		}
	}
#endif

	if ((ret = alloc_subdevices(dev, 4)) < 0)
		return ret;
	if ((ret = alloc_private(dev, sizeof(struct dt2811_private))) < 0)
		return ret;
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

	s = dev->subdevices + 0;
	/* initialize the ADC subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = devpriv->adc_mux == adc_diff ? 8 : 16;
	s->insn_read = dt2811_ai_insn;
	s->maxdata = 0xfff;
	switch (it->options[3]) {
	case 0:
	default:
		s->range_table = this_board->bip_5;
		break;
	case 1:
		s->range_table = this_board->bip_2_5;
		break;
	case 2:
		s->range_table = this_board->unip_5;
		break;
	}

	s = dev->subdevices + 1;
	/* ao subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 2;
	s->insn_write = dt2811_ao_insn;
	s->insn_read = dt2811_ao_insn_read;
	s->maxdata = 0xfff;
	s->range_table_list = devpriv->range_type_list;
	devpriv->range_type_list[0] = dac_range_types[devpriv->dac_range[0]];
	devpriv->range_type_list[1] = dac_range_types[devpriv->dac_range[1]];

	s = dev->subdevices + 2;
	/* di subdevice */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 8;
	s->insn_bits = dt2811_di_insn_bits;
	s->maxdata = 1;
	s->range_table = &range_digital;

	s = dev->subdevices + 3;
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

static int dt2811_detach(struct comedi_device * dev)
{
	printk("comedi%d: dt2811: remove\n", dev->minor);

	if (dev->irq) {
		comedi_free_irq(dev->irq, dev);
	}
	if (dev->iobase) {
		release_region(dev->iobase, DT2811_SIZE);
	}

	return 0;
}

static int dt2811_ai_insn(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	int chan = CR_CHAN(insn->chanspec);
	int timeout = DT2811_TIMEOUT;
	int i;

	for (i = 0; i < insn->n; i++) {
		outb(chan, dev->iobase + DT2811_ADGCR);

		while (timeout
			&& inb(dev->iobase + DT2811_ADCSR) & DT2811_ADBUSY)
			timeout--;
		if (!timeout)
			return -ETIME;

		data[i] = inb(dev->iobase + DT2811_ADDATLO);
		data[i] |= inb(dev->iobase + DT2811_ADDATHI) << 8;
		data[i] &= 0xfff;
	}

	return i;
}

#if 0
/* Wow.  This is code from the Comedi stone age.  But it hasn't been
 * replaced, so I'll let it stay. */
int dt2811_adtrig(kdev_t minor, comedi_adtrig * adtrig)
{
	struct comedi_device *dev = comedi_devices + minor;

	if (adtrig->n < 1)
		return 0;
	dev->curadchan = adtrig->chan;
	switch (dev->i_admode) {
	case COMEDI_MDEMAND:
		dev->ntrig = adtrig->n - 1;
		/*printk("dt2811: AD soft trigger\n"); */
		/*outb(DT2811_CLRERROR|DT2811_INTENB,dev->iobase+DT2811_ADCSR); *//* not neccessary */
		outb(dev->curadchan, dev->iobase + DT2811_ADGCR);
		do_gettimeofday(&trigtime);
		break;
	case COMEDI_MCONTS:
		dev->ntrig = adtrig->n;
		break;
	}

	return 0;
}
#endif

static int dt2811_ao_insn(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	int i;
	int chan;

	chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++) {
		outb(data[i] & 0xff, dev->iobase + DT2811_DADAT0LO + 2 * chan);
		outb((data[i] >> 8) & 0xff,
			dev->iobase + DT2811_DADAT0HI + 2 * chan);
		devpriv->ao_readback[chan] = data[i];
	}

	return i;
}

static int dt2811_ao_insn_read(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	int i;
	int chan;

	chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++) {
		data[i] = devpriv->ao_readback[chan];
	}

	return i;
}

static int dt2811_di_insn_bits(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	if (insn->n != 2)
		return -EINVAL;

	data[1] = inb(dev->iobase + DT2811_DIO);

	return 2;
}

static int dt2811_do_insn_bits(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	if (insn->n != 2)
		return -EINVAL;

	s->state &= ~data[0];
	s->state |= data[0] & data[1];
	outb(s->state, dev->iobase + DT2811_DIO);

	data[1] = s->state;

	return 2;
}
