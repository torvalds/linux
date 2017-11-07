// SPDX-License-Identifier: GPL-2.0+
/*
 * aio_iiro_16.c
 * Comedi driver for Access I/O Products 104-IIRO-16 board
 * Copyright (C) 2006 C&C Technologies, Inc.
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
 * Driver: aio_iiro_16
 * Description: Access I/O Products PC/104 Isolated Input/Relay Output Board
 * Author: Zachary Ware <zach.ware@cctechnol.com>
 * Devices: [Access I/O] 104-IIRO-16 (aio_iiro_16)
 * Status: experimental
 *
 * Configuration Options:
 *   [0] - I/O port base address
 *   [1] - IRQ (optional)
 *
 * The board supports interrupts on change of state of the digital inputs.
 * The sample data returned by the async command indicates which inputs
 * changed state and the current state of the inputs:
 *
 *	Bit 23 - IRQ Enable (1) / Disable (0)
 *	Bit 17 - Input 8-15 Changed State (1 = Changed, 0 = No Change)
 *	Bit 16 - Input 0-7 Changed State (1 = Changed, 0 = No Change)
 *	Bit 15 - Digital input 15
 *	...
 *	Bit 0  - Digital input 0
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#define AIO_IIRO_16_RELAY_0_7		0x00
#define AIO_IIRO_16_INPUT_0_7		0x01
#define AIO_IIRO_16_IRQ			0x02
#define AIO_IIRO_16_RELAY_8_15		0x04
#define AIO_IIRO_16_INPUT_8_15		0x05
#define AIO_IIRO_16_STATUS		0x07
#define AIO_IIRO_16_STATUS_IRQE		BIT(7)
#define AIO_IIRO_16_STATUS_INPUT_8_15	BIT(1)
#define AIO_IIRO_16_STATUS_INPUT_0_7	BIT(0)

static unsigned int aio_iiro_16_read_inputs(struct comedi_device *dev)
{
	unsigned int val;

	val = inb(dev->iobase + AIO_IIRO_16_INPUT_0_7);
	val |= inb(dev->iobase + AIO_IIRO_16_INPUT_8_15) << 8;

	return val;
}

static irqreturn_t aio_iiro_16_cos(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int status;
	unsigned int val;

	status = inb(dev->iobase + AIO_IIRO_16_STATUS);
	if (!(status & AIO_IIRO_16_STATUS_IRQE))
		return IRQ_NONE;

	val = aio_iiro_16_read_inputs(dev);
	val |= (status << 16);

	comedi_buf_write_samples(s, &val, 1);
	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

static void aio_iiro_enable_irq(struct comedi_device *dev, bool enable)
{
	if (enable)
		inb(dev->iobase + AIO_IIRO_16_IRQ);
	else
		outb(0, dev->iobase + AIO_IIRO_16_IRQ);
}

static int aio_iiro_16_cos_cancel(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	aio_iiro_enable_irq(dev, false);

	return 0;
}

static int aio_iiro_16_cos_cmd(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	aio_iiro_enable_irq(dev, true);

	return 0;
}

static int aio_iiro_16_cos_cmdtest(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);
	err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* Step 4: fix up any arguments */

	/* Step 5: check channel list if it exists */

	return 0;
}

static int aio_iiro_16_do_insn_bits(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	if (comedi_dio_update_state(s, data)) {
		outb(s->state & 0xff, dev->iobase + AIO_IIRO_16_RELAY_0_7);
		outb((s->state >> 8) & 0xff,
		     dev->iobase + AIO_IIRO_16_RELAY_8_15);
	}

	data[1] = s->state;

	return insn->n;
}

static int aio_iiro_16_di_insn_bits(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	data[1] = aio_iiro_16_read_inputs(dev);

	return insn->n;
}

static int aio_iiro_16_attach(struct comedi_device *dev,
			      struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x8);
	if (ret)
		return ret;

	aio_iiro_enable_irq(dev, false);

	/*
	 * Digital input change of state interrupts are optionally supported
	 * using IRQ 2-7, 10-12, 14, or 15.
	 */
	if ((1 << it->options[1]) & 0xdcfc) {
		ret = request_irq(it->options[1], aio_iiro_16_cos, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	/* Digital Output subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= aio_iiro_16_do_insn_bits;

	/* get the initial state of the relays */
	s->state = inb(dev->iobase + AIO_IIRO_16_RELAY_0_7) |
		   (inb(dev->iobase + AIO_IIRO_16_RELAY_8_15) << 8);

	/* Digital Input subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= aio_iiro_16_di_insn_bits;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ | SDF_LSAMPL;
		s->len_chanlist	= 1;
		s->do_cmdtest	= aio_iiro_16_cos_cmdtest;
		s->do_cmd	= aio_iiro_16_cos_cmd;
		s->cancel	= aio_iiro_16_cos_cancel;
	}

	return 0;
}

static struct comedi_driver aio_iiro_16_driver = {
	.driver_name	= "aio_iiro_16",
	.module		= THIS_MODULE,
	.attach		= aio_iiro_16_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(aio_iiro_16_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Access I/O Products 104-IIRO-16 board");
MODULE_LICENSE("GPL");
