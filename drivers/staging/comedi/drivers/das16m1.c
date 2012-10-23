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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

************************************************************************
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

#include <linux/ioport.h>
#include <linux/interrupt.h>
#include "../comedidev.h"

#include "8255.h"
#include "8253.h"
#include "comedi_fc.h"

#define DAS16M1_SIZE 16
#define DAS16M1_SIZE2 8

#define DAS16M1_XTAL 100	/* 10 MHz master clock */

#define FIFO_SIZE 1024		/*  1024 sample fifo */

/*
    CIO-DAS16_M1.pdf

    "cio-das16/m1"

  0	a/d bits 0-3, mux		start 12 bit
  1	a/d bits 4-11		unused
  2	status		control
  3	di 4 bit		do 4 bit
  4	unused			clear interrupt
  5	interrupt, pacer
  6	channel/gain queue address
  7	channel/gain queue data
  89ab	8254
  cdef	8254
  400	8255
  404-407 	8254

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
#define DAS16M1_8254_FIRST_CNTRL       0xb
#define   TOTAL_CLEAR                    0x30
#define DAS16M1_8254_SECOND            0xc
#define DAS16M1_82C55                  0x400
#define DAS16M1_8254_THIRD             0x404

static const struct comedi_lrange range_das16m1 = { 9,
	{
	 BIP_RANGE(5),
	 BIP_RANGE(2.5),
	 BIP_RANGE(1.25),
	 BIP_RANGE(0.625),
	 UNI_RANGE(10),
	 UNI_RANGE(5),
	 UNI_RANGE(2.5),
	 UNI_RANGE(1.25),
	 BIP_RANGE(10),
	 }
};

struct das16m1_private_struct {
	unsigned int control_state;
	volatile unsigned int adc_count;	/*  number of samples completed */
	/* initial value in lower half of hardware conversion counter,
	 * needed to keep track of whether new count has been loaded into
	 * counter yet (loaded by first sample conversion) */
	u16 initial_hw_count;
	short ai_buffer[FIFO_SIZE];
	unsigned int do_bits;	/*  saves status of digital output bits */
	unsigned int divisor1;	/*  divides master clock to obtain conversion speed */
	unsigned int divisor2;	/*  divides master clock to obtain conversion speed */
};

static inline short munge_sample(short data)
{
	return (data >> 4) & 0xfff;
}

static void munge_sample_array(short *array, unsigned int num_elements)
{
	unsigned int i;

	for (i = 0; i < num_elements; i++)
		array[i] = munge_sample(array[i]);
}

static int das16m1_cmd_test(struct comedi_device *dev,
			    struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	struct das16m1_private_struct *devpriv = dev->private;
	unsigned int err = 0, tmp, i;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_FOLLOW);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */
	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}

	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		/* internal trigger */
		if (cmd->scan_begin_arg != 0) {
			cmd->scan_begin_arg = 0;
			err++;
		}
	}

	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < 1000) {
			cmd->convert_arg = 1000;
			err++;
		}
	}

	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}

	if (cmd->stop_src == TRIG_COUNT) {
		/* any count is allowed */
	} else {
		/* TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

	if (err)
		return 3;

	/* step 4: fix up arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		/* calculate counter values that give desired timing */
		i8253_cascade_ns_to_timer_2div(DAS16M1_XTAL,
					       &(devpriv->divisor1),
					       &(devpriv->divisor2),
					       &(cmd->convert_arg),
					       cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			err++;
	}

	if (err)
		return 4;

	/*  check chanlist against board's peculiarities */
	if (cmd->chanlist && cmd->chanlist_len > 1) {
		for (i = 0; i < cmd->chanlist_len; i++) {
			/*  even/odd channels must go into even/odd queue addresses */
			if ((i % 2) != (CR_CHAN(cmd->chanlist[i]) % 2)) {
				comedi_error(dev, "bad chanlist:\n"
					     " even/odd channels must go have even/odd chanlist indices");
				err++;
			}
		}
		if ((cmd->chanlist_len % 2) != 0) {
			comedi_error(dev,
				     "chanlist must be of even length or length 1");
			err++;
		}
	}

	if (err)
		return 5;

	return 0;
}

/* This function takes a time in nanoseconds and sets the     *
 * 2 pacer clocks to the closest frequency possible. It also  *
 * returns the actual sampling period.                        */
static unsigned int das16m1_set_pacer(struct comedi_device *dev,
				      unsigned int ns, int rounding_flags)
{
	struct das16m1_private_struct *devpriv = dev->private;

	i8253_cascade_ns_to_timer_2div(DAS16M1_XTAL, &(devpriv->divisor1),
				       &(devpriv->divisor2), &ns,
				       rounding_flags & TRIG_ROUND_MASK);

	/* Write the values of ctr1 and ctr2 into counters 1 and 2 */
	i8254_load(dev->iobase + DAS16M1_8254_SECOND, 0, 1, devpriv->divisor1,
		   2);
	i8254_load(dev->iobase + DAS16M1_8254_SECOND, 0, 2, devpriv->divisor2,
		   2);

	return ns;
}

static int das16m1_cmd_exec(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct das16m1_private_struct *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int byte, i;

	if (dev->irq == 0) {
		comedi_error(dev, "irq required to execute comedi_cmd");
		return -1;
	}

	/* disable interrupts and internal pacer */
	devpriv->control_state &= ~INTE & ~PACER_MASK;
	outb(devpriv->control_state, dev->iobase + DAS16M1_INTR_CONTROL);

	/*  set software count */
	devpriv->adc_count = 0;
	/* Initialize lower half of hardware counter, used to determine how
	 * many samples are in fifo.  Value doesn't actually load into counter
	 * until counter's next clock (the next a/d conversion) */
	i8254_load(dev->iobase + DAS16M1_8254_FIRST, 0, 1, 0, 2);
	/* remember current reading of counter so we know when counter has
	 * actually been loaded */
	devpriv->initial_hw_count =
	    i8254_read(dev->iobase + DAS16M1_8254_FIRST, 0, 1);
	/* setup channel/gain queue */
	for (i = 0; i < cmd->chanlist_len; i++) {
		outb(i, dev->iobase + DAS16M1_QUEUE_ADDR);
		byte =
		    Q_CHAN(CR_CHAN(cmd->chanlist[i])) |
		    Q_RANGE(CR_RANGE(cmd->chanlist[i]));
		outb(byte, dev->iobase + DAS16M1_QUEUE_DATA);
	}

	/* set counter mode and counts */
	cmd->convert_arg =
	    das16m1_set_pacer(dev, cmd->convert_arg,
			      cmd->flags & TRIG_ROUND_MASK);

	/*  set control & status register */
	byte = 0;
	/* if we are using external start trigger (also board dislikes having
	 * both start and conversion triggers external simultaneously) */
	if (cmd->start_src == TRIG_EXT && cmd->convert_src != TRIG_EXT)
		byte |= EXT_TRIG_BIT;

	outb(byte, dev->iobase + DAS16M1_CS);
	/* clear interrupt bit */
	outb(0, dev->iobase + DAS16M1_CLEAR_INTR);

	/* enable interrupts and internal pacer */
	devpriv->control_state &= ~PACER_MASK;
	if (cmd->convert_src == TRIG_TIMER)
		devpriv->control_state |= INT_PACER;
	else
		devpriv->control_state |= EXT_PACER;

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

static int das16m1_ai_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct das16m1_private_struct *devpriv = dev->private;
	int i, n;
	int byte;
	const int timeout = 1000;

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

		for (i = 0; i < timeout; i++) {
			if (inb(dev->iobase + DAS16M1_CS) & IRQDATA)
				break;
		}
		if (i == timeout) {
			comedi_error(dev, "timeout");
			return -ETIME;
		}
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
			    struct comedi_insn *insn, unsigned int *data)
{
	struct das16m1_private_struct *devpriv = dev->private;
	unsigned int wbits;

	/*  only set bits that have been masked */
	data[0] &= 0xf;
	wbits = devpriv->do_bits;
	/*  zero bits that have been masked */
	wbits &= ~data[0];
	/*  set masked bits */
	wbits |= data[0] & data[1];
	devpriv->do_bits = wbits;
	data[1] = wbits;

	outb(devpriv->do_bits, dev->iobase + DAS16M1_DIO);

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
	async->events = 0;
	cmd = &async->cmd;

	/*  figure out how many samples are in fifo */
	hw_counter = i8254_read(dev->iobase + DAS16M1_8254_FIRST, 0, 1);
	/* make sure hardware counter reading is not bogus due to initial value
	 * not having been loaded yet */
	if (devpriv->adc_count == 0 && hw_counter == devpriv->initial_hw_count) {
		num_samples = 0;
	} else {
		/* The calculation of num_samples looks odd, but it uses the following facts.
		 * 16 bit hardware counter is initialized with value of zero (which really
		 * means 0x1000).  The counter decrements by one on each conversion
		 * (when the counter decrements from zero it goes to 0xffff).  num_samples
		 * is a 16 bit variable, so it will roll over in a similar fashion to the
		 * hardware counter.  Work it out, and this is what you get. */
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
	cfc_write_array_to_buffer(s, devpriv->ai_buffer,
				  num_samples * sizeof(short));
	devpriv->adc_count += num_samples;

	if (cmd->stop_src == TRIG_COUNT) {
		if (devpriv->adc_count >= cmd->stop_arg * cmd->chanlist_len) {	/* end of acquisition */
			das16m1_cancel(dev, s);
			async->events |= COMEDI_CB_EOA;
		}
	}

	/* this probably won't catch overruns since the card doesn't generate
	 * overrun interrupts, but we might as well try */
	if (status & OVRUN) {
		das16m1_cancel(dev, s);
		async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
		comedi_error(dev, "fifo overflow");
	}

	comedi_event(dev, s);

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

	return s->async->buf_write_count - s->async->buf_read_count;
}

static irqreturn_t das16m1_interrupt(int irq, void *d)
{
	int status;
	struct comedi_device *dev = d;

	if (dev->attached == 0) {
		comedi_error(dev, "premature interrupt");
		return IRQ_HANDLED;
	}
	/*  prevent race with comedi_poll() */
	spin_lock(&dev->spinlock);

	status = inb(dev->iobase + DAS16M1_CS);

	if ((status & (IRQDATA | OVRUN)) == 0) {
		comedi_error(dev, "spurious interrupt");
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
	int ret;

	switch (irq) {
	case 10:
		ret = 0x0;
		break;
	case 11:
		ret = 0x1;
		break;
	case 12:
		ret = 0x2;
		break;
	case 15:
		ret = 0x3;
		break;
	case 2:
		ret = 0x4;
		break;
	case 3:
		ret = 0x5;
		break;
	case 5:
		ret = 0x6;
		break;
	case 7:
		ret = 0x7;
		break;
	default:
		return -1;
		break;
	}
	return ret << 4;
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
	unsigned int irq;
	unsigned long iobase;

	dev->board_name = dev->driver->driver_name;

	iobase = it->options[0];

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	if (!request_region(iobase, DAS16M1_SIZE, dev->board_name)) {
		comedi_error(dev, "I/O port conflict\n");
		return -EIO;
	}
	if (!request_region(iobase + DAS16M1_82C55, DAS16M1_SIZE2,
			    dev->board_name)) {
		release_region(iobase, DAS16M1_SIZE);
		comedi_error(dev, "I/O port conflict\n");
		return -EIO;
	}
	dev->iobase = iobase;

	/* now for the irq */
	irq = it->options[1];
	/*  make sure it is valid */
	if (das16m1_irq_bits(irq) >= 0) {
		ret = request_irq(irq, das16m1_interrupt, 0,
				  dev->driver->driver_name, dev);
		if (ret < 0)
			return ret;
		dev->irq = irq;
		printk
		    ("irq %u\n", irq);
	} else if (irq == 0) {
		printk
		    (", no irq\n");
	} else {
		comedi_error(dev, "invalid irq\n"
			     " valid irqs are 2, 3, 5, 7, 10, 11, 12, or 15\n");
		return -EINVAL;
	}

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	dev->read_subdev = s;
	/* ai */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_CMD_READ;
	s->n_chan = 8;
	s->subdev_flags = SDF_DIFF;
	s->len_chanlist = 256;
	s->maxdata = (1 << 12) - 1;
	s->range_table = &range_das16m1;
	s->insn_read = das16m1_ai_rinsn;
	s->do_cmdtest = das16m1_cmd_test;
	s->do_cmd = das16m1_cmd_exec;
	s->cancel = das16m1_cancel;
	s->poll = das16m1_poll;

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
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 4;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das16m1_do_wbits;

	s = &dev->subdevices[3];
	/* 8255 */
	subdev_8255_init(dev, s, NULL, dev->iobase + DAS16M1_82C55);

	/*  disable upper half of hardware conversion counter so it doesn't mess with us */
	outb(TOTAL_CLEAR, dev->iobase + DAS16M1_8254_FIRST_CNTRL);

	/*  initialize digital output lines */
	outb(devpriv->do_bits, dev->iobase + DAS16M1_DIO);

	/* set the interrupt level */
	if (dev->irq)
		devpriv->control_state = das16m1_irq_bits(dev->irq);
	else
		devpriv->control_state = 0;
	outb(devpriv->control_state, dev->iobase + DAS16M1_INTR_CONTROL);

	return 0;
}

static void das16m1_detach(struct comedi_device *dev)
{
	if (dev->subdevices)
		subdev_8255_cleanup(dev, &dev->subdevices[3]);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (dev->iobase) {
		release_region(dev->iobase, DAS16M1_SIZE);
		release_region(dev->iobase + DAS16M1_82C55, DAS16M1_SIZE2);
	}
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
