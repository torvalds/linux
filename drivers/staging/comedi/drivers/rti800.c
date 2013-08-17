/*
   comedi/drivers/rti800.c
   Hardware driver for Analog Devices RTI-800/815 board

   COMEDI - Linux Control and Measurement Device Interface
   Copyright (C) 1998 David A. Schleef <ds@schleef.org>

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
Driver: rti800
Description: Analog Devices RTI-800/815
Author: ds
Status: unknown
Updated: Fri, 05 Sep 2008 14:50:44 +0100
Devices: [Analog Devices] RTI-800 (rti800), RTI-815 (rti815)

Configuration options:
  [0] - I/O port base address
  [1] - IRQ
  [2] - A/D reference
	0 = differential
	1 = pseudodifferential (common)
	2 = single-ended
  [3] - A/D range
	0 = [-10,10]
	1 = [-5,5]
	2 = [0,10]
  [4] - A/D encoding
	0 = two's complement
	1 = straight binary
  [5] - DAC 0 range
	0 = [-10,10]
	1 = [0,10]
  [6] - DAC 0 encoding
	0 = two's complement
	1 = straight binary
  [7] - DAC 1 range (same as DAC 0)
  [8] - DAC 1 encoding (same as DAC 0)
*/

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/ioport.h>

#define RTI800_SIZE 16

#define RTI800_CSR 0
#define RTI800_MUXGAIN 1
#define RTI800_CONVERT 2
#define RTI800_ADCLO 3
#define RTI800_ADCHI 4
#define RTI800_DAC0LO 5
#define RTI800_DAC0HI 6
#define RTI800_DAC1LO 7
#define RTI800_DAC1HI 8
#define RTI800_CLRFLAGS 9
#define RTI800_DI 10
#define RTI800_DO 11
#define RTI800_9513A_DATA 12
#define RTI800_9513A_CNTRL 13
#define RTI800_9513A_STATUS 13

/*
 * flags for CSR register
 */

#define RTI800_BUSY		0x80
#define RTI800_DONE		0x40
#define RTI800_OVERRUN		0x20
#define RTI800_TCR		0x10
#define RTI800_DMA_ENAB		0x08
#define RTI800_INTR_TC		0x04
#define RTI800_INTR_EC		0x02
#define RTI800_INTR_OVRN	0x01

#define Am9513_8BITBUS

#define Am9513_output_control(a)	outb(a, dev->iobase+RTI800_9513A_CNTRL)
#define Am9513_output_data(a)		outb(a, dev->iobase+RTI800_9513A_DATA)
#define Am9513_input_data()		inb(dev->iobase+RTI800_9513A_DATA)
#define Am9513_input_status()		inb(dev->iobase+RTI800_9513A_STATUS)

#include "am9513.h"

static const struct comedi_lrange range_rti800_ai_10_bipolar = { 4, {
								     BIP_RANGE
								     (10),
								     BIP_RANGE
								     (1),
								     BIP_RANGE
								     (0.1),
								     BIP_RANGE
								     (0.02)
								     }
};

static const struct comedi_lrange range_rti800_ai_5_bipolar = { 4, {
								    BIP_RANGE
								    (5),
								    BIP_RANGE
								    (0.5),
								    BIP_RANGE
								    (0.05),
								    BIP_RANGE
								    (0.01)
								    }
};

static const struct comedi_lrange range_rti800_ai_unipolar = { 4, {
								   UNI_RANGE
								   (10),
								   UNI_RANGE(1),
								   UNI_RANGE
								   (0.1),
								   UNI_RANGE
								   (0.02)
								   }
};

struct rti800_board {

	const char *name;
	int has_ao;
};

static const struct rti800_board boardtypes[] = {
	{"rti800", 0},
	{"rti815", 1},
};

#define this_board ((const struct rti800_board *)dev->board_ptr)

static int rti800_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it);
static int rti800_detach(struct comedi_device *dev);
static struct comedi_driver driver_rti800 = {
	.driver_name = "rti800",
	.module = THIS_MODULE,
	.attach = rti800_attach,
	.detach = rti800_detach,
	.num_names = ARRAY_SIZE(boardtypes),
	.board_name = &boardtypes[0].name,
	.offset = sizeof(struct rti800_board),
};

static int __init driver_rti800_init_module(void)
{
	return comedi_driver_register(&driver_rti800);
}

static void __exit driver_rti800_cleanup_module(void)
{
	comedi_driver_unregister(&driver_rti800);
}

module_init(driver_rti800_init_module);
module_exit(driver_rti800_cleanup_module);

static irqreturn_t rti800_interrupt(int irq, void *dev);

struct rti800_private {
	enum {
		adc_diff, adc_pseudodiff, adc_singleended
	} adc_mux;
	enum {
		adc_bipolar10, adc_bipolar5, adc_unipolar10
	} adc_range;
	enum {
		adc_2comp, adc_straight
	} adc_coding;
	enum {
		dac_bipolar10, dac_unipolar10
	} dac0_range, dac1_range;
	enum {
		dac_2comp, dac_straight
	} dac0_coding, dac1_coding;
	const struct comedi_lrange *ao_range_type_list[2];
	unsigned int ao_readback[2];
	int muxgain_bits;
};

#define devpriv ((struct rti800_private *)dev->private)

#define RTI800_TIMEOUT 100

static irqreturn_t rti800_interrupt(int irq, void *dev)
{
	return IRQ_HANDLED;
}

/* settling delay times in usec for different gains */
static const int gaindelay[] = { 10, 20, 40, 80 };

static int rti800_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int i, t;
	int status;
	int chan = CR_CHAN(insn->chanspec);
	unsigned gain = CR_RANGE(insn->chanspec);
	unsigned muxgain_bits;

	inb(dev->iobase + RTI800_ADCHI);
	outb(0, dev->iobase + RTI800_CLRFLAGS);

	muxgain_bits = chan | (gain << 5);
	if (muxgain_bits != devpriv->muxgain_bits) {
		devpriv->muxgain_bits = muxgain_bits;
		outb(devpriv->muxgain_bits, dev->iobase + RTI800_MUXGAIN);
		/* without a delay here, the RTI_OVERRUN bit
		 * gets set, and you will have an error. */
		if (insn->n > 0) {
			BUG_ON(gain >= ARRAY_SIZE(gaindelay));
			udelay(gaindelay[gain]);
		}
	}

	for (i = 0; i < insn->n; i++) {
		outb(0, dev->iobase + RTI800_CONVERT);
		for (t = RTI800_TIMEOUT; t; t--) {
			status = inb(dev->iobase + RTI800_CSR);
			if (status & RTI800_OVERRUN) {
				printk(KERN_WARNING "rti800: a/d overrun\n");
				outb(0, dev->iobase + RTI800_CLRFLAGS);
				return -EIO;
			}
			if (status & RTI800_DONE)
				break;
			udelay(1);
		}
		if (t == 0) {
			printk(KERN_WARNING "rti800: timeout\n");
			return -ETIME;
		}
		data[i] = inb(dev->iobase + RTI800_ADCLO);
		data[i] |= (0xf & inb(dev->iobase + RTI800_ADCHI)) << 8;

		if (devpriv->adc_coding == adc_2comp)
			data[i] ^= 0x800;
	}

	return i;
}

static int rti800_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

static int rti800_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	int d;
	int i;

	for (i = 0; i < insn->n; i++) {
		devpriv->ao_readback[chan] = d = data[i];
		if (devpriv->dac0_coding == dac_2comp)
			d ^= 0x800;

		outb(d & 0xff,
		     dev->iobase + (chan ? RTI800_DAC1LO : RTI800_DAC0LO));
		outb(d >> 8,
		     dev->iobase + (chan ? RTI800_DAC1HI : RTI800_DAC0HI));
	}
	return i;
}

static int rti800_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;
	data[1] = inb(dev->iobase + RTI800_DI);
	return 2;
}

static int rti800_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
		/* Outputs are inverted... */
		outb(s->state ^ 0xff, dev->iobase + RTI800_DO);
	}

	data[1] = s->state;

	return 2;
}

/*
   options[0] - I/O port
   options[1] - irq
   options[2] - a/d mux
	0=differential, 1=pseudodiff, 2=single
   options[3] - a/d range
	0=bipolar10, 1=bipolar5, 2=unipolar10
   options[4] - a/d coding
	0=2's comp, 1=straight binary
   options[5] - dac0 range
	0=bipolar10, 1=unipolar10
   options[6] - dac0 coding
	0=2's comp, 1=straight binary
   options[7] - dac1 range
   options[8] - dac1 coding
 */

static int rti800_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	unsigned int irq;
	unsigned long iobase;
	int ret;
	struct comedi_subdevice *s;

	iobase = it->options[0];
	printk(KERN_INFO "comedi%d: rti800: 0x%04lx\n", dev->minor, iobase);
	if (!request_region(iobase, RTI800_SIZE, "rti800")) {
		printk(KERN_WARNING "I/O port conflict\n");
		return -EIO;
	}
	dev->iobase = iobase;

#ifdef DEBUG
	printk(KERN_DEBUG "fingerprint=%x,%x,%x,%x,%x ",
	       inb(dev->iobase + 0),
	       inb(dev->iobase + 1),
	       inb(dev->iobase + 2),
	       inb(dev->iobase + 3), inb(dev->iobase + 4));
#endif

	outb(0, dev->iobase + RTI800_CSR);
	inb(dev->iobase + RTI800_ADCHI);
	outb(0, dev->iobase + RTI800_CLRFLAGS);

	irq = it->options[1];
	if (irq) {
		printk(KERN_INFO "( irq = %u )\n", irq);
		ret = request_irq(irq, rti800_interrupt, 0, "rti800", dev);
		if (ret < 0) {
			printk(KERN_WARNING " Failed to allocate IRQ\n");
			return ret;
		}
		dev->irq = irq;
	} else {
		printk(KERN_INFO "( no irq )\n");
	}

	dev->board_name = this_board->name;

	ret = alloc_subdevices(dev, 4);
	if (ret < 0)
		return ret;

	ret = alloc_private(dev, sizeof(struct rti800_private));
	if (ret < 0)
		return ret;

	devpriv->adc_mux = it->options[2];
	devpriv->adc_range = it->options[3];
	devpriv->adc_coding = it->options[4];
	devpriv->dac0_range = it->options[5];
	devpriv->dac0_coding = it->options[6];
	devpriv->dac1_range = it->options[7];
	devpriv->dac1_coding = it->options[8];
	devpriv->muxgain_bits = -1;

	s = dev->subdevices + 0;
	/* ai subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = (devpriv->adc_mux ? 16 : 8);
	s->insn_read = rti800_ai_insn_read;
	s->maxdata = 0xfff;
	switch (devpriv->adc_range) {
	case adc_bipolar10:
		s->range_table = &range_rti800_ai_10_bipolar;
		break;
	case adc_bipolar5:
		s->range_table = &range_rti800_ai_5_bipolar;
		break;
	case adc_unipolar10:
		s->range_table = &range_rti800_ai_unipolar;
		break;
	}

	s++;
	if (this_board->has_ao) {
		/* ao subdevice (only on rti815) */
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITABLE;
		s->n_chan = 2;
		s->insn_read = rti800_ao_insn_read;
		s->insn_write = rti800_ao_insn_write;
		s->maxdata = 0xfff;
		s->range_table_list = devpriv->ao_range_type_list;
		switch (devpriv->dac0_range) {
		case dac_bipolar10:
			devpriv->ao_range_type_list[0] = &range_bipolar10;
			break;
		case dac_unipolar10:
			devpriv->ao_range_type_list[0] = &range_unipolar10;
			break;
		}
		switch (devpriv->dac1_range) {
		case dac_bipolar10:
			devpriv->ao_range_type_list[1] = &range_bipolar10;
			break;
		case dac_unipolar10:
			devpriv->ao_range_type_list[1] = &range_unipolar10;
			break;
		}
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s++;
	/* di */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 8;
	s->insn_bits = rti800_di_insn_bits;
	s->maxdata = 1;
	s->range_table = &range_digital;

	s++;
	/* do */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 8;
	s->insn_bits = rti800_do_insn_bits;
	s->maxdata = 1;
	s->range_table = &range_digital;

/* don't yet know how to deal with counter/timers */
#if 0
	s++;
	/* do */
	s->type = COMEDI_SUBD_TIMER;
#endif

	return 0;
}

static int rti800_detach(struct comedi_device *dev)
{
	printk(KERN_INFO "comedi%d: rti800: remove\n", dev->minor);

	if (dev->iobase)
		release_region(dev->iobase, RTI800_SIZE);

	if (dev->irq)
		free_irq(dev->irq, dev);

	return 0;
}

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
