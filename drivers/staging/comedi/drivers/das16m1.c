/*
 * Comedi driver for CIO-DAS16/M1
 * Author: Frank Mori Hess, based on code from the das16 driver.
 * Copyright (C) 2001 Frank Mori Hess <fmhess@users.sourceforge.net>
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
 * Driver: das16m1
 * Description: CIO-DAS16/M1
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Devices: [Measurement Computing] CIO-DAS16/M1 (das16m1)
 * Status: works
 *
 * This driver supports a single board - the CIO-DAS16/M1. As far as I know,
 * there are no other boards that have the same register layout. Even the
 * CIO-DAS16/M1/16 is significantly different.
 *
 * I was _barely_ able to reach the full 1 MHz capability of this board, using
 * a hard real-time interrupt (set the TRIG_RT flag in your struct comedi_cmd
 * and use rtlinux or RTAI). The board can't do dma, so the bottleneck is
 * pulling the data across the ISA bus. I timed the interrupt handler, and it
 * took my computer ~470 microseconds to pull 512 samples from the board. So
 * at 1 Mhz sampling rate, expect your CPU to be spending almost all of its
 * time in the interrupt handler.
 *
 * This board has some unusual restrictions for its channel/gain list.  If the
 * list has 2 or more channels in it, then two conditions must be satisfied:
 * (1) - even/odd channels must appear at even/odd indices in the list
 * (2) - the list must have an even number of entries.
 *
 * Configuration options:
 *   [0] - base io address
 *   [1] - irq (optional, but you probably want it)
 *
 * irq can be omitted, although the cmd interface will not work without it.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include "../comedidev.h"

#include "8255.h"
#include "comedi_8254.h"

/*
 * Register map (dev->iobase)
 */
#define DAS16M1_AI_REG			0x00	/* 16-bit register */
#define DAS16M1_AI_TO_CHAN(x)		(((x) >> 0) & 0xf)
#define DAS16M1_AI_TO_SAMPLE(x)		(((x) >> 4) & 0xfff)
#define DAS16M1_CS_REG			0x02
#define DAS16M1_CS_EXT_TRIG		BIT(0)
#define DAS16M1_CS_OVRUN		BIT(5)
#define DAS16M1_CS_IRQDATA		BIT(7)
#define DAS16M1_DI_REG			0x03
#define DAS16M1_DO_REG			0x03
#define DAS16M1_CLR_INTR_REG		0x04
#define DAS16M1_INTR_CTRL_REG		0x05
#define DAS16M1_INTR_CTRL_PACER(x)	(((x) & 0x3) << 0)
#define DAS16M1_INTR_CTRL_PACER_EXT	DAS16M1_INTR_CTRL_PACER(2)
#define DAS16M1_INTR_CTRL_PACER_INT	DAS16M1_INTR_CTRL_PACER(3)
#define DAS16M1_INTR_CTRL_PACER_MASK	DAS16M1_INTR_CTRL_PACER(3)
#define DAS16M1_INTR_CTRL_IRQ(x)	(((x) & 0x7) << 4)
#define DAS16M1_INTR_CTRL_INTE		BIT(7)
#define DAS16M1_Q_ADDR_REG		0x06
#define DAS16M1_Q_REG			0x07
#define DAS16M1_Q_CHAN(x)              (((x) & 0x7) << 0)
#define DAS16M1_Q_RANGE(x)             (((x) & 0xf) << 4)
#define DAS16M1_8254_IOBASE1		0x08
#define DAS16M1_8254_IOBASE2		0x0c
#define DAS16M1_8255_IOBASE		0x400
#define DAS16M1_8254_IOBASE3		0x404

#define DAS16M1_SIZE2			0x08

#define DAS16M1_AI_FIFO_SZ		1024	/* # samples */

static const struct comedi_lrange range_das16m1 = {
	9, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25),
		BIP_RANGE(10)
	}
};

struct das16m1_private {
	struct comedi_8254 *counter;
	unsigned int intr_ctrl;
	unsigned int adc_count;
	u16 initial_hw_count;
	unsigned short ai_buffer[DAS16M1_AI_FIFO_SZ];
	unsigned long extra_iobase;
};

static void das16m1_ai_set_queue(struct comedi_device *dev,
				 unsigned int *chanspec, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++) {
		unsigned int chan = CR_CHAN(chanspec[i]);
		unsigned int range = CR_RANGE(chanspec[i]);

		outb(i, dev->iobase + DAS16M1_Q_ADDR_REG);
		outb(DAS16M1_Q_CHAN(chan) | DAS16M1_Q_RANGE(range),
		     dev->iobase + DAS16M1_Q_REG);
	}
}

static void das16m1_ai_munge(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     void *data, unsigned int num_bytes,
			     unsigned int start_chan_index)
{
	unsigned short *array = data;
	unsigned int nsamples = comedi_bytes_to_samples(s, num_bytes);
	unsigned int i;

	/*
	 * The fifo values have the channel number in the lower 4-bits and
	 * the sample in the upper 12-bits. This just shifts the values
	 * to remove the channel numbers.
	 */
	for (i = 0; i < nsamples; i++)
		array[i] = DAS16M1_AI_TO_SAMPLE(array[i]);
}

static int das16m1_ai_check_chanlist(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_cmd *cmd)
{
	int i;

	if (cmd->chanlist_len == 1)
		return 0;

	if ((cmd->chanlist_len % 2) != 0) {
		dev_dbg(dev->class_dev,
			"chanlist must be of even length or length 1\n");
		return -EINVAL;
	}

	for (i = 0; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);

		if ((i % 2) != (chan % 2)) {
			dev_dbg(dev->class_dev,
				"even/odd channels must go have even/odd chanlist indices\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int das16m1_ai_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_FOLLOW)	/* internal trigger */
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	if (cmd->convert_src == TRIG_TIMER)
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg, 1000);

	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		unsigned int arg = cmd->convert_arg;

		comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= das16m1_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int das16m1_ai_cmd(struct comedi_device *dev,
			  struct comedi_subdevice *s)
{
	struct das16m1_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int byte;

	/*  set software count */
	devpriv->adc_count = 0;

	/*
	 * Initialize lower half of hardware counter, used to determine how
	 * many samples are in fifo.  Value doesn't actually load into counter
	 * until counter's next clock (the next a/d conversion).
	 */
	comedi_8254_set_mode(devpriv->counter, 1, I8254_MODE2 | I8254_BINARY);
	comedi_8254_write(devpriv->counter, 1, 0);

	/*
	 * Remember current reading of counter so we know when counter has
	 * actually been loaded.
	 */
	devpriv->initial_hw_count = comedi_8254_read(devpriv->counter, 1);

	das16m1_ai_set_queue(dev, cmd->chanlist, cmd->chanlist_len);

	/* enable interrupts and set internal pacer counter mode and counts */
	devpriv->intr_ctrl &= ~DAS16M1_INTR_CTRL_PACER_MASK;
	if (cmd->convert_src == TRIG_TIMER) {
		comedi_8254_update_divisors(dev->pacer);
		comedi_8254_pacer_enable(dev->pacer, 1, 2, true);
		devpriv->intr_ctrl |= DAS16M1_INTR_CTRL_PACER_INT;
	} else {	/* TRIG_EXT */
		devpriv->intr_ctrl |= DAS16M1_INTR_CTRL_PACER_EXT;
	}

	/*  set control & status register */
	byte = 0;
	/*
	 * If we are using external start trigger (also board dislikes having
	 * both start and conversion triggers external simultaneously).
	 */
	if (cmd->start_src == TRIG_EXT && cmd->convert_src != TRIG_EXT)
		byte |= DAS16M1_CS_EXT_TRIG;

	outb(byte, dev->iobase + DAS16M1_CS_REG);

	/* clear interrupt */
	outb(0, dev->iobase + DAS16M1_CLR_INTR_REG);

	devpriv->intr_ctrl |= DAS16M1_INTR_CTRL_INTE;
	outb(devpriv->intr_ctrl, dev->iobase + DAS16M1_INTR_CTRL_REG);

	return 0;
}

static int das16m1_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct das16m1_private *devpriv = dev->private;

	/* disable interrupts and pacer */
	devpriv->intr_ctrl &= ~(DAS16M1_INTR_CTRL_INTE |
				DAS16M1_INTR_CTRL_PACER_MASK);
	outb(devpriv->intr_ctrl, dev->iobase + DAS16M1_INTR_CTRL_REG);

	return 0;
}

static int das16m1_ai_eoc(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DAS16M1_CS_REG);
	if (status & DAS16M1_CS_IRQDATA)
		return 0;
	return -EBUSY;
}

static int das16m1_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	int ret;
	int i;

	das16m1_ai_set_queue(dev, &insn->chanspec, 1);

	for (i = 0; i < insn->n; i++) {
		unsigned short val;

		/* clear interrupt */
		outb(0, dev->iobase + DAS16M1_CLR_INTR_REG);
		/* trigger conversion */
		outb(0, dev->iobase + DAS16M1_AI_REG);

		ret = comedi_timeout(dev, s, insn, das16m1_ai_eoc, 0);
		if (ret)
			return ret;

		val = inw(dev->iobase + DAS16M1_AI_REG);
		data[i] = DAS16M1_AI_TO_SAMPLE(val);
	}

	return insn->n;
}

static int das16m1_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	data[1] = inb(dev->iobase + DAS16M1_DI_REG) & 0xf;

	return insn->n;
}

static int das16m1_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + DAS16M1_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static void das16m1_handler(struct comedi_device *dev, unsigned int status)
{
	struct das16m1_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	u16 num_samples;
	u16 hw_counter;

	/* figure out how many samples are in fifo */
	hw_counter = comedi_8254_read(devpriv->counter, 1);
	/*
	 * Make sure hardware counter reading is not bogus due to initial
	 * value not having been loaded yet.
	 */
	if (devpriv->adc_count == 0 &&
	    hw_counter == devpriv->initial_hw_count) {
		num_samples = 0;
	} else {
		/*
		 * The calculation of num_samples looks odd, but it uses the
		 * following facts. 16 bit hardware counter is initialized with
		 * value of zero (which really means 0x1000).  The counter
		 * decrements by one on each conversion (when the counter
		 * decrements from zero it goes to 0xffff).  num_samples is a
		 * 16 bit variable, so it will roll over in a similar fashion
		 * to the hardware counter.  Work it out, and this is what you
		 * get.
		 */
		num_samples = -hw_counter - devpriv->adc_count;
	}
	/*  check if we only need some of the points */
	if (cmd->stop_src == TRIG_COUNT) {
		if (num_samples > cmd->stop_arg * cmd->chanlist_len)
			num_samples = cmd->stop_arg * cmd->chanlist_len;
	}
	/*  make sure we dont try to get too many points if fifo has overrun */
	if (num_samples > DAS16M1_AI_FIFO_SZ)
		num_samples = DAS16M1_AI_FIFO_SZ;
	insw(dev->iobase, devpriv->ai_buffer, num_samples);
	comedi_buf_write_samples(s, devpriv->ai_buffer, num_samples);
	devpriv->adc_count += num_samples;

	if (cmd->stop_src == TRIG_COUNT) {
		if (devpriv->adc_count >= cmd->stop_arg * cmd->chanlist_len) {
			/* end of acquisition */
			async->events |= COMEDI_CB_EOA;
		}
	}

	/*
	 * This probably won't catch overruns since the card doesn't generate
	 * overrun interrupts, but we might as well try.
	 */
	if (status & DAS16M1_CS_OVRUN) {
		async->events |= COMEDI_CB_ERROR;
		dev_err(dev->class_dev, "fifo overflow\n");
	}

	comedi_handle_events(dev, s);
}

static int das16m1_ai_poll(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	unsigned long flags;
	unsigned int status;

	/*  prevent race with interrupt handler */
	spin_lock_irqsave(&dev->spinlock, flags);
	status = inb(dev->iobase + DAS16M1_CS_REG);
	das16m1_handler(dev, status);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return comedi_buf_n_bytes_ready(s);
}

static irqreturn_t das16m1_interrupt(int irq, void *d)
{
	int status;
	struct comedi_device *dev = d;

	if (!dev->attached) {
		dev_err(dev->class_dev, "premature interrupt\n");
		return IRQ_HANDLED;
	}
	/*  prevent race with comedi_poll() */
	spin_lock(&dev->spinlock);

	status = inb(dev->iobase + DAS16M1_CS_REG);

	if ((status & (DAS16M1_CS_IRQDATA | DAS16M1_CS_OVRUN)) == 0) {
		dev_err(dev->class_dev, "spurious interrupt\n");
		spin_unlock(&dev->spinlock);
		return IRQ_NONE;
	}

	das16m1_handler(dev, status);

	/* clear interrupt */
	outb(0, dev->iobase + DAS16M1_CLR_INTR_REG);

	spin_unlock(&dev->spinlock);
	return IRQ_HANDLED;
}

static int das16m1_irq_bits(unsigned int irq)
{
	switch (irq) {
	case 10:
		return 0x0;
	case 11:
		return 0x1;
	case 12:
		return 0x2;
	case 15:
		return 0x3;
	case 2:
		return 0x4;
	case 3:
		return 0x5;
	case 5:
		return 0x6;
	case 7:
		return 0x7;
	default:
		return 0x0;
	}
}

static int das16m1_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct das16m1_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;
	/* Request an additional region for the 8255 and 3rd 8254 */
	ret = __comedi_request_region(dev, dev->iobase + DAS16M1_8255_IOBASE,
				      DAS16M1_SIZE2);
	if (ret)
		return ret;
	devpriv->extra_iobase = dev->iobase + DAS16M1_8255_IOBASE;

	/* only irqs 2, 3, 4, 5, 6, 7, 10, 11, 12, 14, and 15 are valid */
	if ((1 << it->options[1]) & 0xdcfc) {
		ret = request_irq(it->options[1], das16m1_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	dev->pacer = comedi_8254_init(dev->iobase + DAS16M1_8254_IOBASE2,
				      I8254_OSC_BASE_10MHZ, I8254_IO8, 0);
	if (!dev->pacer)
		return -ENOMEM;

	devpriv->counter = comedi_8254_init(dev->iobase + DAS16M1_8254_IOBASE1,
					    0, I8254_IO8, 0);
	if (!devpriv->counter)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_DIFF;
	s->n_chan	= 8;
	s->maxdata	= 0x0fff;
	s->range_table	= &range_das16m1;
	s->insn_read	= das16m1_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= 256;
		s->do_cmdtest	= das16m1_ai_cmdtest;
		s->do_cmd	= das16m1_ai_cmd;
		s->cancel	= das16m1_ai_cancel;
		s->poll		= das16m1_ai_poll;
		s->munge	= das16m1_ai_munge;
	}

	/* Digital Input subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das16m1_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das16m1_do_insn_bits;

	/* Digital I/O subdevice (8255) */
	s = &dev->subdevices[3];
	ret = subdev_8255_init(dev, s, NULL, DAS16M1_8255_IOBASE);
	if (ret)
		return ret;

	/*  initialize digital output lines */
	outb(0, dev->iobase + DAS16M1_DO_REG);

	/* set the interrupt level */
	devpriv->intr_ctrl = DAS16M1_INTR_CTRL_IRQ(das16m1_irq_bits(dev->irq));
	outb(devpriv->intr_ctrl, dev->iobase + DAS16M1_INTR_CTRL_REG);

	return 0;
}

static void das16m1_detach(struct comedi_device *dev)
{
	struct das16m1_private *devpriv = dev->private;

	if (devpriv) {
		if (devpriv->extra_iobase)
			release_region(devpriv->extra_iobase, DAS16M1_SIZE2);
		kfree(devpriv->counter);
	}
	comedi_legacy_detach(dev);
}

static struct comedi_driver das16m1_driver = {
	.driver_name	= "das16m1",
	.module		= THIS_MODULE,
	.attach		= das16m1_attach,
	.detach		= das16m1_detach,
};
module_comedi_driver(das16m1_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for CIO-DAS16/M1 ISA cards");
MODULE_LICENSE("GPL");
