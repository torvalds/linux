/*
   comedi/drivers/multiq3.c
   Hardware driver for Quanser Consulting MultiQ-3 board

   COMEDI - Linux Control and Measurement Device Interface
   Copyright (C) 1999 Anders Blomdell <anders.blomdell@control.lth.se>

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
Driver: multiq3
Description: Quanser Consulting MultiQ-3
Author: Anders Blomdell <anders.blomdell@control.lth.se>
Status: works
Devices: [Quanser Consulting] MultiQ-3 (multiq3)

*/

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/ioport.h>

#define MULTIQ3_SIZE 16

/*
 * MULTIQ-3 port offsets
 */
#define MULTIQ3_DIGIN_PORT 0
#define MULTIQ3_DIGOUT_PORT 0
#define MULTIQ3_DAC_DATA 2
#define MULTIQ3_AD_DATA 4
#define MULTIQ3_AD_CS 4
#define MULTIQ3_STATUS 6
#define MULTIQ3_CONTROL 6
#define MULTIQ3_CLK_DATA 8
#define MULTIQ3_ENC_DATA 12
#define MULTIQ3_ENC_CONTROL 14

/*
 * flags for CONTROL register
 */
#define MULTIQ3_AD_MUX_EN      0x0040
#define MULTIQ3_AD_AUTOZ       0x0080
#define MULTIQ3_AD_AUTOCAL     0x0100
#define MULTIQ3_AD_SH          0x0200
#define MULTIQ3_AD_CLOCK_4M    0x0400
#define MULTIQ3_DA_LOAD                0x1800

#define MULTIQ3_CONTROL_MUST    0x0600

/*
 * flags for STATUS register
 */
#define MULTIQ3_STATUS_EOC      0x008
#define MULTIQ3_STATUS_EOC_I    0x010

/*
 * flags for encoder control
 */
#define MULTIQ3_CLOCK_DATA      0x00
#define MULTIQ3_CLOCK_SETUP     0x18
#define MULTIQ3_INPUT_SETUP     0x41
#define MULTIQ3_QUAD_X4         0x38
#define MULTIQ3_BP_RESET        0x01
#define MULTIQ3_CNTR_RESET      0x02
#define MULTIQ3_TRSFRPR_CTR     0x08
#define MULTIQ3_TRSFRCNTR_OL    0x10
#define MULTIQ3_EFLAG_RESET     0x06

#define MULTIQ3_TIMEOUT 30

static int multiq3_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it);
static int multiq3_detach(struct comedi_device *dev);
static struct comedi_driver driver_multiq3 = {
	.driver_name = "multiq3",
	.module = THIS_MODULE,
	.attach = multiq3_attach,
	.detach = multiq3_detach,
};

static int __init driver_multiq3_init_module(void)
{
	return comedi_driver_register(&driver_multiq3);
}

static void __exit driver_multiq3_cleanup_module(void)
{
	comedi_driver_unregister(&driver_multiq3);
}

module_init(driver_multiq3_init_module);
module_exit(driver_multiq3_cleanup_module);

struct multiq3_private {
	unsigned int ao_readback[2];
};
#define devpriv ((struct multiq3_private *)dev->private)

static int multiq3_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int i, n;
	int chan;
	unsigned int hi, lo;

	chan = CR_CHAN(insn->chanspec);
	outw(MULTIQ3_CONTROL_MUST | MULTIQ3_AD_MUX_EN | (chan << 3),
	     dev->iobase + MULTIQ3_CONTROL);

	for (i = 0; i < MULTIQ3_TIMEOUT; i++) {
		if (inw(dev->iobase + MULTIQ3_STATUS) & MULTIQ3_STATUS_EOC)
			break;
	}
	if (i == MULTIQ3_TIMEOUT)
		return -ETIMEDOUT;

	for (n = 0; n < insn->n; n++) {
		outw(0, dev->iobase + MULTIQ3_AD_CS);
		for (i = 0; i < MULTIQ3_TIMEOUT; i++) {
			if (inw(dev->iobase +
				MULTIQ3_STATUS) & MULTIQ3_STATUS_EOC_I)
				break;
		}
		if (i == MULTIQ3_TIMEOUT)
			return -ETIMEDOUT;

		hi = inb(dev->iobase + MULTIQ3_AD_CS);
		lo = inb(dev->iobase + MULTIQ3_AD_CS);
		data[n] = (((hi << 8) | lo) + 0x1000) & 0x1fff;
	}

	return n;
}

static int multiq3_ao_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

static int multiq3_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++) {
		outw(MULTIQ3_CONTROL_MUST | MULTIQ3_DA_LOAD | chan,
		     dev->iobase + MULTIQ3_CONTROL);
		outw(data[i], dev->iobase + MULTIQ3_DAC_DATA);
		outw(MULTIQ3_CONTROL_MUST, dev->iobase + MULTIQ3_CONTROL);

		devpriv->ao_readback[chan] = data[i];
	}

	return i;
}

static int multiq3_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	data[1] = inw(dev->iobase + MULTIQ3_DIGIN_PORT);

	return 2;
}

static int multiq3_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	s->state &= ~data[0];
	s->state |= (data[0] & data[1]);
	outw(s->state, dev->iobase + MULTIQ3_DIGOUT_PORT);

	data[1] = s->state;

	return 2;
}

static int multiq3_encoder_insn_read(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	int n;
	int chan = CR_CHAN(insn->chanspec);
	int control = MULTIQ3_CONTROL_MUST | MULTIQ3_AD_MUX_EN | (chan << 3);

	for (n = 0; n < insn->n; n++) {
		int value;
		outw(control, dev->iobase + MULTIQ3_CONTROL);
		outb(MULTIQ3_BP_RESET, dev->iobase + MULTIQ3_ENC_CONTROL);
		outb(MULTIQ3_TRSFRCNTR_OL, dev->iobase + MULTIQ3_ENC_CONTROL);
		value = inb(dev->iobase + MULTIQ3_ENC_DATA);
		value |= (inb(dev->iobase + MULTIQ3_ENC_DATA) << 8);
		value |= (inb(dev->iobase + MULTIQ3_ENC_DATA) << 16);
		data[n] = (value + 0x800000) & 0xffffff;
	}

	return n;
}

static void encoder_reset(struct comedi_device *dev)
{
	int chan;
	for (chan = 0; chan < dev->subdevices[4].n_chan; chan++) {
		int control =
		    MULTIQ3_CONTROL_MUST | MULTIQ3_AD_MUX_EN | (chan << 3);
		outw(control, dev->iobase + MULTIQ3_CONTROL);
		outb(MULTIQ3_EFLAG_RESET, dev->iobase + MULTIQ3_ENC_CONTROL);
		outb(MULTIQ3_BP_RESET, dev->iobase + MULTIQ3_ENC_CONTROL);
		outb(MULTIQ3_CLOCK_DATA, dev->iobase + MULTIQ3_ENC_DATA);
		outb(MULTIQ3_CLOCK_SETUP, dev->iobase + MULTIQ3_ENC_CONTROL);
		outb(MULTIQ3_INPUT_SETUP, dev->iobase + MULTIQ3_ENC_CONTROL);
		outb(MULTIQ3_QUAD_X4, dev->iobase + MULTIQ3_ENC_CONTROL);
		outb(MULTIQ3_CNTR_RESET, dev->iobase + MULTIQ3_ENC_CONTROL);
	}
}

/*
   options[0] - I/O port
   options[1] - irq
   options[2] - number of encoder chips installed
 */

static int multiq3_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	int result = 0;
	unsigned long iobase;
	unsigned int irq;
	struct comedi_subdevice *s;

	iobase = it->options[0];
	printk(KERN_INFO "comedi%d: multiq3: 0x%04lx ", dev->minor, iobase);
	if (!request_region(iobase, MULTIQ3_SIZE, "multiq3")) {
		printk(KERN_ERR "comedi%d: I/O port conflict\n", dev->minor);
		return -EIO;
	}

	dev->iobase = iobase;

	irq = it->options[1];
	if (irq)
		printk(KERN_WARNING "comedi%d: irq = %u ignored\n",
			dev->minor, irq);
	else
		printk(KERN_WARNING "comedi%d: no irq\n", dev->minor);
	dev->board_name = "multiq3";
	result = alloc_subdevices(dev, 5);
	if (result < 0)
		return result;

	result = alloc_private(dev, sizeof(struct multiq3_private));
	if (result < 0)
		return result;

	s = dev->subdevices + 0;
	/* ai subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = 8;
	s->insn_read = multiq3_ai_insn_read;
	s->maxdata = 0x1fff;
	s->range_table = &range_bipolar5;

	s = dev->subdevices + 1;
	/* ao subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 8;
	s->insn_read = multiq3_ao_insn_read;
	s->insn_write = multiq3_ao_insn_write;
	s->maxdata = 0xfff;
	s->range_table = &range_bipolar5;

	s = dev->subdevices + 2;
	/* di subdevice */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 16;
	s->insn_bits = multiq3_di_insn_bits;
	s->maxdata = 1;
	s->range_table = &range_digital;

	s = dev->subdevices + 3;
	/* do subdevice */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 16;
	s->insn_bits = multiq3_do_insn_bits;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->state = 0;

	s = dev->subdevices + 4;
	/* encoder (counter) subdevice */
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_READABLE | SDF_LSAMPL;
	s->n_chan = it->options[2] * 2;
	s->insn_read = multiq3_encoder_insn_read;
	s->maxdata = 0xffffff;
	s->range_table = &range_unknown;

	encoder_reset(dev);

	return 0;
}

static int multiq3_detach(struct comedi_device *dev)
{
	printk(KERN_INFO "comedi%d: multiq3: remove\n", dev->minor);

	if (dev->iobase)
		release_region(dev->iobase, MULTIQ3_SIZE);
	if (dev->irq)
		free_irq(dev->irq, dev);

	return 0;
}

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
