/*
    comedi/drivers/das16m1.c
    CIO-DAS16/M1 driver
    Author: Frank Mori Hess, based on code from the das16
      driver.
    Copyright (C) 2001 Frank Mori Hess <fmhess@users.sourceforge.net>

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
*/
/*
Driver: das16m1
Description: CIO-DAS16/M1
Author: Frank Mori Hess <fmhess@users.sourceforge.net>
Devices: [Measurement Computing] CIO-DAS16/M1 (das16m1)
Status: works

This driver supports a single board - the CIO-DAS16/M1.
As far as I know, there are no other boards that have
the same register layout.  Even the CIO-DAS16/M1/16 is
significantly different.

I was _barely_ able to reach the full 1 MHz capability
of this board, using a hard real-time interrupt
(set the TRIG_RT flag in your struct comedi_cmd and use
rtlinux or RTAI).  The board can't do dma, so the bottleneck is
pulling the data across the ISA bus.  I timed the interrupt
handler, and it took my computer ~470 microseconds to pull 512
samples from the board.  So at 1 Mhz sampling rate,
expect your CPU to be spending almost all of its
time in the interrupt handler.

This board has some unusual restrictions for its channel/gain list.  If the
list has 2 or more channels in it, then two conditions must be satisfied:
(1) - even/odd channels must appear at even/odd indices in the list
(2) - the list must have an even number of entries.

Options:
	[0] - base io address
	[1] - irq (optional, but you probably want it)

irq can be omitted, although the cmd interface will not work without it.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include "../comedidev.h"

#include "8255.h"
#include "comedi_8254.h"

#define DAS16M1_SIZE2 8

#define FIFO_SIZE 1024		/*  1024 sample fifo */

/*
    CIO-DAS16_M1.pdf

    "cio-das16/m1"

  0		a/d bits 0-3, mux		start 12 bit
  1		a/d bits 4-11		unused
  2		status		control
  3		di 4 bit		do 4 bit
  4		unused			clear interrupt
  5		interrupt, pacer
  6		channel/gain queue address
  7		channel/gain queue data
  89ab		8254
  cdef		8254
  400		8255
  404-407	8254

*/

#define DAS16M1_AI             0	/*  16-bit wide register */
#define   AI_CHAN(x)             ((x) & 0xf)
#define DAS16M1_CS             2
#define   EXT_TRIG_BIT           0x1
#define   OVRUN                  0x20
#define   IRQDATA                0x80
#define DAS16M1_DIO            3
#define DAS16M1_CLEAR_INTR     4
#define DAS16M1_INTR_CONTROL   5
#define   EXT_PACER              0x2
#define   INT_PACER              0x3
#define   PACER_MASK             0x3
#define   INTE                   0x80
#define DAS16M1_QUEUE_ADDR     6
#define DAS16M1_QUEUE_DATA     7
#define   Q_CHAN(x)              ((x) & 0x7)
#define   Q_RANGE(x)             (((x) & 0xf) << 4)
#define   UNIPOLAR               0x40
#define DAS16M1_8254_FIRST             0x8
#define DAS16M1_8254_SECOND            0xc
#define DAS16M1_82C55                  0x400
#define DAS16M1_8254_THIRD             0x404

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

struct das16m1_private_struct {
	struct comedi_8254 *counter;
	unsigned int control_state;
	unsigned int adc_count;	/*  number of samples completed */
	/* initial value in lower half of hardware conversion counter,
	 * needed to keep track of whether new count has been loaded into
	 * counter yet (loaded by first sample conversion) */
	u16 initial_hw_count;
	unsigned short ai_buffer[FIFO_SIZE];
	unsigned long extra_iobase;
};

static inline unsigned short munge_sample(unsigned short data)
{
	return (data >> 4) & 0xfff;
}

static void munge_sample_array(unsigned short *array, unsigned int num_elements)
{
	unsigned int i;

	for (i = 0; i < num_elements; i++)
		array[i] = munge_sample(array[i]);
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

static int das16m1_cmd_test(struct comedi_device *dev,
			    struct comedi_subdevice *s, struct comedi_cmd *cmd)
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

static int das16m1_cmd_exec(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct das16m1_private_struct *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int byte, i;

	/* disable interrupts and internal pacer */
	devpriv->control_state &= ~INTE & ~PACER_MASK;
	outb(devpriv->control_state, dev->iobase + DAS16M1_INTR_CONTROL);

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

	/* setup channel/gain queue */
	for (i = 0; i < cmd->chanlist_len; i++) {
		outb(i, dev->iobase + DAS16M1_QUEUE_ADDR);
		byte =
		    Q_CHAN(CR_CHAN(cmd->chanlist[i])) |
		    Q_RANGE(CR_RANGE(cmd->chanlist[i]));
		outb(byte, dev->iobase + DAS16M1_QUEUE_DATA);
	}

	/* enable interrupts and set internal pacer counter mode and counts */
	devpriv->control_state &= ~PACER_MASK;
	if (cmd->convert_src == TRIG_TIMER) {
		comedi_8254_update_divisors(dev->pacer);
		comedi_8254_pacer_enable(dev->pacer, 1, 2, true);
		devpriv->control_state |= INT_PACER;
	} else {	/* TRIG_EXT */
		devpriv->control_state |= EXT_PACER;
	}

	/*  set control & status register */
	byte = 0;
	/* if we are using external start trigger (also board dislikes having
	 * both start and conversion triggers external simultaneously) */
	if (cmd->start_src == TRIG_EXT && cmd->convert_src != TRIG_EXT)
		byte |= EXT_TRIG_BIT;

	outb(byte, dev->iobase + DAS16M1_CS);
	/* clear interrupt bit */
	outb(0, dev->iobase + DAS16M1_CLEAR_INTR);

	devpriv->control_state |= INTE;
	outb(devpriv->control_state, dev->iobase + DAS16M1_INTR_CONTROL);

	return 0;
}

static int das16m1_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct das16m1_private_struct *devpriv = dev->private;

	devpriv->control_state &= ~INTE & ~PACER_MASK;
	outb(devpriv->control_state, dev->iobase + DAS16M1_INTR_CONTROL);

	return 0;
}

static int das16m1_ai_eoc(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DAS16M1_CS);
	if (status & IRQDATA)
		return 0;
	return -EBUSY;
}

static int das16m1_ai_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct das16m1_private_struct *devpriv = dev->private;
	int ret;
	int n;
	int byte;

	/* disable interrupts and internal pacer */
	devpriv->control_state &= ~INTE & ~PACER_MASK;
	outb(devpriv->control_state, dev->iobase + DAS16M1_INTR_CONTROL);

	/* setup channel/gain queue */
	outb(0, dev->iobase + DAS16M1_QUEUE_ADDR);
	byte =
	    Q_CHAN(CR_CHAN(insn->chanspec)) | Q_RANGE(CR_RANGE(insn->chanspec));
	outb(byte, dev->iobase + DAS16M1_QUEUE_DATA);

	for (n = 0; n < insn->n; n++) {
		/* clear IRQDATA bit */
		outb(0, dev->iobase + DAS16M1_CLEAR_INTR);
		/* trigger conversion */
		outb(0, dev->iobase);

		ret = comedi_timeout(dev, s, insn, das16m1_ai_eoc, 0);
		if (ret)
			return ret;

		data[n] = munge_sample(inw(dev->iobase));
	}

	return n;
}

static int das16m1_di_rbits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	unsigned int bits;

	bits = inb(dev->iobase + DAS16M1_DIO) & 0xf;
	data[1] = bits;
	data[0] = 0;

	return insn->n;
}

static int das16m1_do_wbits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn,
			    unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + DAS16M1_DIO);

	data[1] = s->state;

	return insn->n;
}

static void das16m1_handler(struct comedi_device *dev, unsigned int status)
{
	struct das16m1_private_struct *devpriv = dev->private;
	struct comedi_subdevice *s;
	struct comedi_async *async;
	struct comedi_cmd *cmd;
	u16 num_samples;
	u16 hw_counter;

	s = dev->read_subdev;
	async = s->async;
	cmd = &async->cmd;

	/* figure out how many samples are in fifo */
	hw_counter = comedi_8254_read(devpriv->counter, 1);
	/* make sure hardware counter reading is not bogus due to initial value
	 * not having been loaded yet */
	if (devpriv->adc_count == 0 &&
	    hw_counter == devpriv->initial_hw_count) {
		num_samples = 0;
	} else {
		/* The calculation of num_samples looks odd, but it uses the
		 * following facts. 16 bit hardware counter is initialized with
		 * value of zero (which really means 0x1000).  The counter
		 * decrements by one on each conversion (when the counter
		 * decrements from zero it goes to 0xffff).  num_samples is a
		 * 16 bit variable, so it will roll over in a similar fashion
		 * to the hardware counter.  Work it out, and this is what you
		 * get. */
		num_samples = -hw_counter - devpriv->adc_count;
	}
	/*  check if we only need some of the points */
	if (cmd->stop_src == TRIG_COUNT) {
		if (num_samples > cmd->stop_arg * cmd->chanlist_len)
			num_samples = cmd->stop_arg * cmd->chanlist_len;
	}
	/*  make sure we dont try to get too many points if fifo has overrun */
	if (num_samples > FIFO_SIZE)
		num_samples = FIFO_SIZE;
	insw(dev->iobase, devpriv->ai_buffer, num_samples);
	munge_sample_array(devpriv->ai_buffer, num_samples);
	comedi_buf_write_samples(s, devpriv->ai_buffer, num_samples);
	devpriv->adc_count += num_samples;

	if (cmd->stop_src == TRIG_COUNT) {
		if (devpriv->adc_count >= cmd->stop_arg * cmd->chanlist_len) {
			/* end of acquisition */
			async->events |= COMEDI_CB_EOA;
		}
	}

	/* this probably won't catch overruns since the card doesn't generate
	 * overrun interrupts, but we might as well try */
	if (status & OVRUN) {
		async->events |= COMEDI_CB_ERROR;
		dev_err(dev->class_dev, "fifo overflow\n");
	}

	comedi_handle_events(dev, s);
}

static int das16m1_poll(struct comedi_device *dev, struct comedi_subdevice *s)
{
	unsigned long flags;
	unsigned int status;

	/*  prevent race with interrupt handler */
	spin_lock_irqsave(&dev->spinlock, flags);
	status = inb(dev->iobase + DAS16M1_CS);
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

	status = inb(dev->iobase + DAS16M1_CS);

	if ((status & (IRQDATA | OVRUN)) == 0) {
		dev_err(dev->class_dev, "spurious interrupt\n");
		spin_unlock(&dev->spinlock);
		return IRQ_NONE;
	}

	das16m1_handler(dev, status);

	/* clear interrupt */
	outb(0, dev->iobase + DAS16M1_CLEAR_INTR);

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

/*
 * Options list:
 *   0  I/O base
 *   1  IRQ
 */
static int das16m1_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct das16m1_private_struct *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;
	/* Request an additional region for the 8255 */
	ret = __comedi_request_region(dev, dev->iobase + DAS16M1_82C55,
				      DAS16M1_SIZE2);
	if (ret)
		return ret;
	devpriv->extra_iobase = dev->iobase + DAS16M1_82C55;

	/* only irqs 2, 3, 4, 5, 6, 7, 10, 11, 12, 14, and 15 are valid */
	if ((1 << it->options[1]) & 0xdcfc) {
		ret = request_irq(it->options[1], das16m1_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	dev->pacer = comedi_8254_init(dev->iobase + DAS16M1_8254_SECOND,
				      I8254_OSC_BASE_10MHZ, I8254_IO8, 0);
	if (!dev->pacer)
		return -ENOMEM;

	devpriv->counter = comedi_8254_init(dev->iobase + DAS16M1_8254_FIRST,
					    0, I8254_IO8, 0);
	if (!devpriv->counter)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* ai */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_DIFF;
	s->n_chan = 8;
	s->maxdata = (1 << 12) - 1;
	s->range_table = &range_das16m1;
	s->insn_read = das16m1_ai_rinsn;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags |= SDF_CMD_READ;
		s->len_chanlist = 256;
		s->do_cmdtest = das16m1_cmd_test;
		s->do_cmd = das16m1_cmd_exec;
		s->cancel = das16m1_cancel;
		s->poll = das16m1_poll;
	}

	s = &dev->subdevices[1];
	/* di */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 4;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das16m1_di_rbits;

	s = &dev->subdevices[2];
	/* do */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 4;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das16m1_do_wbits;

	s = &dev->subdevices[3];
	/* 8255 */
	ret = subdev_8255_init(dev, s, NULL, DAS16M1_82C55);
	if (ret)
		return ret;

	/*  initialize digital output lines */
	outb(0, dev->iobase + DAS16M1_DIO);

	/* set the interrupt level */
	devpriv->control_state = das16m1_irq_bits(dev->irq) << 4;
	outb(devpriv->control_state, dev->iobase + DAS16M1_INTR_CONTROL);

	return 0;
}

static void das16m1_detach(struct comedi_device *dev)
{
	struct das16m1_private_struct *devpriv = dev->private;

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
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
