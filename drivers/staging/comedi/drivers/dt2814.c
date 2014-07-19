/*
    comedi/drivers/dt2814.c
    Hardware driver for Data Translation DT2814

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
*/
/*
Driver: dt2814
Description: Data Translation DT2814
Author: ds
Status: complete
Devices: [Data Translation] DT2814 (dt2814)

Configuration options:
  [0] - I/O port base address
  [1] - IRQ

This card has 16 analog inputs multiplexed onto a 12 bit ADC.  There
is a minimally useful onboard clock.  The base frequency for the
clock is selected by jumpers, and the clock divider can be selected
via programmed I/O.  Unfortunately, the clock divider can only be
a power of 10, from 1 to 10^7, of which only 3 or 4 are useful.  In
addition, the clock does not seem to be very accurate.
*/

#include <linux/module.h>
#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/delay.h>

#include "comedi_fc.h"

#define DT2814_CSR 0
#define DT2814_DATA 1

/*
 * flags
 */

#define DT2814_FINISH 0x80
#define DT2814_ERR 0x40
#define DT2814_BUSY 0x20
#define DT2814_ENB 0x10
#define DT2814_CHANMASK 0x0f

struct dt2814_private {

	int ntrig;
	int curadchan;
};

#define DT2814_TIMEOUT 10
#define DT2814_MAX_SPEED 100000	/* Arbitrary 10 khz limit */

static int dt2814_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DT2814_CSR);
	if (status & DT2814_FINISH)
		return 0;
	return -EBUSY;
}

static int dt2814_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int n, hi, lo;
	int chan;
	int ret;

	for (n = 0; n < insn->n; n++) {
		chan = CR_CHAN(insn->chanspec);

		outb(chan, dev->iobase + DT2814_CSR);

		ret = comedi_timeout(dev, s, insn, dt2814_ai_eoc, 0);
		if (ret)
			return ret;

		hi = inb(dev->iobase + DT2814_DATA);
		lo = inb(dev->iobase + DT2814_DATA);

		data[n] = (hi << 4) | (lo >> 4);
	}

	return n;
}

static int dt2814_ns_to_timer(unsigned int *ns, unsigned int flags)
{
	int i;
	unsigned int f;

	/* XXX ignores flags */

	f = 10000;		/* ns */
	for (i = 0; i < 8; i++) {
		if ((2 * (*ns)) < (f * 11))
			break;
		f *= 10;
	}

	*ns = f;

	return i;
}

static int dt2814_ai_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_TIMER);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg, 1000000000);
	err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
					 DT2814_MAX_SPEED);

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 2);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	arg = cmd->scan_begin_arg;
	dt2814_ns_to_timer(&arg, cmd->flags);
	err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, arg);

	if (err)
		return 4;

	return 0;
}

static int dt2814_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct dt2814_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int chan;
	int trigvar;

	trigvar = dt2814_ns_to_timer(&cmd->scan_begin_arg, cmd->flags);

	chan = CR_CHAN(cmd->chanlist[0]);

	devpriv->ntrig = cmd->stop_arg;
	outb(chan | DT2814_ENB | (trigvar << 5), dev->iobase + DT2814_CSR);

	return 0;

}

static irqreturn_t dt2814_interrupt(int irq, void *d)
{
	int lo, hi;
	struct comedi_device *dev = d;
	struct dt2814_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	int data;

	if (!dev->attached) {
		dev_err(dev->class_dev, "spurious interrupt\n");
		return IRQ_HANDLED;
	}

	hi = inb(dev->iobase + DT2814_DATA);
	lo = inb(dev->iobase + DT2814_DATA);

	data = (hi << 4) | (lo >> 4);

	if (!(--devpriv->ntrig)) {
		int i;

		outb(0, dev->iobase + DT2814_CSR);
		/* note: turning off timed mode triggers another
		   sample. */

		for (i = 0; i < DT2814_TIMEOUT; i++) {
			if (inb(dev->iobase + DT2814_CSR) & DT2814_FINISH)
				break;
		}
		inb(dev->iobase + DT2814_DATA);
		inb(dev->iobase + DT2814_DATA);

		s->async->events |= COMEDI_CB_EOA;
	}
	comedi_event(dev, s);
	return IRQ_HANDLED;
}

static int dt2814_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct dt2814_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	int i;

	ret = comedi_request_region(dev, it->options[0], 0x2);
	if (ret)
		return ret;

	outb(0, dev->iobase + DT2814_CSR);
	udelay(100);
	if (inb(dev->iobase + DT2814_CSR) & DT2814_ERR) {
		dev_err(dev->class_dev, "reset error (fatal)\n");
		return -EIO;
	}
	i = inb(dev->iobase + DT2814_DATA);
	i = inb(dev->iobase + DT2814_DATA);

	if (it->options[1]) {
		ret = request_irq(it->options[1], dt2814_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = 16;		/* XXX */
	s->insn_read = dt2814_ai_insn_read;
	s->maxdata = 0xfff;
	s->range_table = &range_unknown;	/* XXX */
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags |= SDF_CMD_READ;
		s->len_chanlist = 1;
		s->do_cmd = dt2814_ai_cmd;
		s->do_cmdtest = dt2814_ai_cmdtest;
	}

	return 0;
}

static struct comedi_driver dt2814_driver = {
	.driver_name	= "dt2814",
	.module		= THIS_MODULE,
	.attach		= dt2814_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(dt2814_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
