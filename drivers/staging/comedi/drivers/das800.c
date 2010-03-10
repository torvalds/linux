/*
    comedi/drivers/das800.c
    Driver for Keitley das800 series boards and compatibles
    Copyright (C) 2000 Frank Mori Hess <fmhess@users.sourceforge.net>

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
Driver: das800
Description: Keithley Metrabyte DAS800 (& compatibles)
Author: Frank Mori Hess <fmhess@users.sourceforge.net>
Devices: [Keithley Metrabyte] DAS-800 (das-800), DAS-801 (das-801),
  DAS-802 (das-802),
  [Measurement Computing] CIO-DAS800 (cio-das800),
  CIO-DAS801 (cio-das801), CIO-DAS802 (cio-das802),
  CIO-DAS802/16 (cio-das802/16)
Status: works, cio-das802/16 untested - email me if you have tested it

Configuration options:
  [0] - I/O port base address
  [1] - IRQ (optional, required for timed or externally triggered conversions)

Notes:
	IRQ can be omitted, although the cmd interface will not work without it.

	All entries in the channel/gain list must use the same gain and be
	consecutive channels counting upwards in channel number (these are
	hardware limitations.)

	I've never tested the gain setting stuff since I only have a
	DAS-800 board with fixed gain.

	The cio-das802/16 does not have a fifo-empty status bit!  Therefore
	only fifo-half-full transfers are possible with this card.
*/
/*

cmd triggers supported:
	start_src:      TRIG_NOW | TRIG_EXT
	scan_begin_src: TRIG_FOLLOW
	scan_end_src:   TRIG_COUNT
	convert_src:    TRIG_TIMER | TRIG_EXT
	stop_src:       TRIG_NONE | TRIG_COUNT


*/

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/ioport.h>
#include <linux/delay.h>

#include "8253.h"
#include "comedi_fc.h"

#define DAS800_SIZE           8
#define TIMER_BASE            1000
#define N_CHAN_AI             8	/*  number of analog input channels */

/* Registers for the das800 */

#define DAS800_LSB            0
#define   FIFO_EMPTY            0x1
#define   FIFO_OVF              0x2
#define DAS800_MSB            1
#define DAS800_CONTROL1       2
#define   CONTROL1_INTE         0x8
#define DAS800_CONV_CONTROL   2
#define   ITE                   0x1
#define   CASC                  0x2
#define   DTEN                  0x4
#define   IEOC                  0x8
#define   EACS                  0x10
#define   CONV_HCEN             0x80
#define DAS800_SCAN_LIMITS    2
#define DAS800_STATUS         2
#define   IRQ                   0x8
#define   BUSY                  0x80
#define DAS800_GAIN           3
#define   CIO_FFOV              0x8	/*  fifo overflow for cio-das802/16 */
#define   CIO_ENHF              0x90	/*  interrupt fifo half full for cio-das802/16 */
#define   CONTROL1              0x80
#define   CONV_CONTROL          0xa0
#define   SCAN_LIMITS           0xc0
#define   ID                    0xe0
#define DAS800_8254           4
#define DAS800_STATUS2        7
#define   STATUS2_HCEN          0x80
#define   STATUS2_INTE          0X20
#define DAS800_ID             7

struct das800_board {
	const char *name;
	int ai_speed;
	const struct comedi_lrange *ai_range;
	int resolution;
};

/* analog input ranges */
static const struct comedi_lrange range_das800_ai = {
	1,
	{
	 RANGE(-5, 5),
	 }
};

static const struct comedi_lrange range_das801_ai = {
	9,
	{
	 RANGE(-5, 5),
	 RANGE(-10, 10),
	 RANGE(0, 10),
	 RANGE(-0.5, 0.5),
	 RANGE(0, 1),
	 RANGE(-0.05, 0.05),
	 RANGE(0, 0.1),
	 RANGE(-0.01, 0.01),
	 RANGE(0, 0.02),
	 }
};

static const struct comedi_lrange range_cio_das801_ai = {
	9,
	{
	 RANGE(-5, 5),
	 RANGE(-10, 10),
	 RANGE(0, 10),
	 RANGE(-0.5, 0.5),
	 RANGE(0, 1),
	 RANGE(-0.05, 0.05),
	 RANGE(0, 0.1),
	 RANGE(-0.005, 0.005),
	 RANGE(0, 0.01),
	 }
};

static const struct comedi_lrange range_das802_ai = {
	9,
	{
	 RANGE(-5, 5),
	 RANGE(-10, 10),
	 RANGE(0, 10),
	 RANGE(-2.5, 2.5),
	 RANGE(0, 5),
	 RANGE(-1.25, 1.25),
	 RANGE(0, 2.5),
	 RANGE(-0.625, 0.625),
	 RANGE(0, 1.25),
	 }
};

static const struct comedi_lrange range_das80216_ai = {
	8,
	{
	 RANGE(-10, 10),
	 RANGE(0, 10),
	 RANGE(-5, 5),
	 RANGE(0, 5),
	 RANGE(-2.5, 2.5),
	 RANGE(0, 2.5),
	 RANGE(-1.25, 1.25),
	 RANGE(0, 1.25),
	 }
};

enum { das800, ciodas800, das801, ciodas801, das802, ciodas802, ciodas80216 };

static const struct das800_board das800_boards[] = {
	{
	 .name = "das-800",
	 .ai_speed = 25000,
	 .ai_range = &range_das800_ai,
	 .resolution = 12,
	 },
	{
	 .name = "cio-das800",
	 .ai_speed = 20000,
	 .ai_range = &range_das800_ai,
	 .resolution = 12,
	 },
	{
	 .name = "das-801",
	 .ai_speed = 25000,
	 .ai_range = &range_das801_ai,
	 .resolution = 12,
	 },
	{
	 .name = "cio-das801",
	 .ai_speed = 20000,
	 .ai_range = &range_cio_das801_ai,
	 .resolution = 12,
	 },
	{
	 .name = "das-802",
	 .ai_speed = 25000,
	 .ai_range = &range_das802_ai,
	 .resolution = 12,
	 },
	{
	 .name = "cio-das802",
	 .ai_speed = 20000,
	 .ai_range = &range_das802_ai,
	 .resolution = 12,
	 },
	{
	 .name = "cio-das802/16",
	 .ai_speed = 10000,
	 .ai_range = &range_das80216_ai,
	 .resolution = 16,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct das800_board *)dev->board_ptr)

struct das800_private {
	volatile unsigned int count;	/* number of data points left to be taken */
	volatile int forever;	/* flag indicating whether we should take data forever */
	unsigned int divisor1;	/* value to load into board's counter 1 for timed conversions */
	unsigned int divisor2;	/* value to load into board's counter 2 for timed conversions */
	volatile int do_bits;	/* digital output bits */
};

#define devpriv ((struct das800_private *)dev->private)

static int das800_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it);
static int das800_detach(struct comedi_device *dev);
static int das800_cancel(struct comedi_device *dev, struct comedi_subdevice *s);

static struct comedi_driver driver_das800 = {
	.driver_name = "das800",
	.module = THIS_MODULE,
	.attach = das800_attach,
	.detach = das800_detach,
	.num_names = ARRAY_SIZE(das800_boards),
	.board_name = &das800_boards[0].name,
	.offset = sizeof(struct das800_board),
};

static irqreturn_t das800_interrupt(int irq, void *d);
static void enable_das800(struct comedi_device *dev);
static void disable_das800(struct comedi_device *dev);
static int das800_ai_do_cmdtest(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_cmd *cmd);
static int das800_ai_do_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s);
static int das800_ai_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data);
static int das800_di_rbits(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data);
static int das800_do_wbits(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data);
static int das800_probe(struct comedi_device *dev);
static int das800_set_frequency(struct comedi_device *dev);

/* checks and probes das-800 series board type */
static int das800_probe(struct comedi_device *dev)
{
	int id_bits;
	unsigned long irq_flags;
	int board;

	/*  'comedi spin lock irqsave' disables even rt interrupts, we use them to protect indirect addressing */
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(ID, dev->iobase + DAS800_GAIN);	/* select base address + 7 to be ID register */
	id_bits = inb(dev->iobase + DAS800_ID) & 0x3;	/* get id bits */
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	board = thisboard - das800_boards;

	switch (id_bits) {
	case 0x0:
		if (board == das800) {
			printk(" Board model: DAS-800\n");
			return board;
		}
		if (board == ciodas800) {
			printk(" Board model: CIO-DAS800\n");
			return board;
		}
		printk(" Board model (probed): DAS-800\n");
		return das800;
		break;
	case 0x2:
		if (board == das801) {
			printk(" Board model: DAS-801\n");
			return board;
		}
		if (board == ciodas801) {
			printk(" Board model: CIO-DAS801\n");
			return board;
		}
		printk(" Board model (probed): DAS-801\n");
		return das801;
		break;
	case 0x3:
		if (board == das802) {
			printk(" Board model: DAS-802\n");
			return board;
		}
		if (board == ciodas802) {
			printk(" Board model: CIO-DAS802\n");
			return board;
		}
		if (board == ciodas80216) {
			printk(" Board model: CIO-DAS802/16\n");
			return board;
		}
		printk(" Board model (probed): DAS-802\n");
		return das802;
		break;
	default:
		printk(" Board model: probe returned 0x%x (unknown)\n",
		       id_bits);
		return board;
		break;
	}
	return -1;
}

/*
 * A convenient macro that defines init_module() and cleanup_module(),
 * as necessary.
 */
COMEDI_INITCLEANUP(driver_das800);

/* interrupt service routine */
static irqreturn_t das800_interrupt(int irq, void *d)
{
	short i;		/* loop index */
	short dataPoint = 0;
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;	/* analog input subdevice */
	struct comedi_async *async;
	int status;
	unsigned long irq_flags;
	static const int max_loops = 128;	/*  half-fifo size for cio-das802/16 */
	/*  flags */
	int fifo_empty = 0;
	int fifo_overflow = 0;

	status = inb(dev->iobase + DAS800_STATUS);
	/* if interrupt was not generated by board or driver not attached, quit */
	if (!(status & IRQ))
		return IRQ_NONE;
	if (!(dev->attached))
		return IRQ_HANDLED;

	/* wait until here to initialize async, since we will get null dereference
	 * if interrupt occurs before driver is fully attached!
	 */
	async = s->async;

	/*  if hardware conversions are not enabled, then quit */
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(CONTROL1, dev->iobase + DAS800_GAIN);	/* select base address + 7 to be STATUS2 register */
	status = inb(dev->iobase + DAS800_STATUS2) & STATUS2_HCEN;
	/* don't release spinlock yet since we want to make sure noone else disables hardware conversions */
	if (status == 0) {
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		return IRQ_HANDLED;
	}

	/* loop while card's fifo is not empty (and limit to half fifo for cio-das802/16) */
	for (i = 0; i < max_loops; i++) {
		/* read 16 bits from dev->iobase and dev->iobase + 1 */
		dataPoint = inb(dev->iobase + DAS800_LSB);
		dataPoint += inb(dev->iobase + DAS800_MSB) << 8;
		if (thisboard->resolution == 12) {
			fifo_empty = dataPoint & FIFO_EMPTY;
			fifo_overflow = dataPoint & FIFO_OVF;
			if (fifo_overflow)
				break;
		} else {
			fifo_empty = 0;	/*  cio-das802/16 has no fifo empty status bit */
		}
		if (fifo_empty)
			break;
		/* strip off extraneous bits for 12 bit cards */
		if (thisboard->resolution == 12)
			dataPoint = (dataPoint >> 4) & 0xfff;
		/* if there are more data points to collect */
		if (devpriv->count > 0 || devpriv->forever == 1) {
			/* write data point to buffer */
			cfc_write_to_buffer(s, dataPoint);
			if (devpriv->count > 0)
				devpriv->count--;
		}
	}
	async->events |= COMEDI_CB_BLOCK;
	/* check for fifo overflow */
	if (thisboard->resolution == 12) {
		fifo_overflow = dataPoint & FIFO_OVF;
		/*  else cio-das802/16 */
	} else {
		fifo_overflow = inb(dev->iobase + DAS800_GAIN) & CIO_FFOV;
	}
	if (fifo_overflow) {
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		comedi_error(dev, "DAS800 FIFO overflow");
		das800_cancel(dev, dev->subdevices + 0);
		async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		comedi_event(dev, s);
		async->events = 0;
		return IRQ_HANDLED;
	}
	if (devpriv->count > 0 || devpriv->forever == 1) {
		/* Re-enable card's interrupt.
		 * We already have spinlock, so indirect addressing is safe */
		outb(CONTROL1, dev->iobase + DAS800_GAIN);	/* select dev->iobase + 2 to be control register 1 */
		outb(CONTROL1_INTE | devpriv->do_bits,
		     dev->iobase + DAS800_CONTROL1);
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		/* otherwise, stop taking data */
	} else {
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		disable_das800(dev);	/* diable hardware triggered conversions */
		async->events |= COMEDI_CB_EOA;
	}
	comedi_event(dev, s);
	async->events = 0;
	return IRQ_HANDLED;
}

static int das800_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase = it->options[0];
	unsigned int irq = it->options[1];
	unsigned long irq_flags;
	int board;

	printk("comedi%d: das800: io 0x%lx", dev->minor, iobase);
	if (irq)
		printk(", irq %u", irq);
	printk("\n");

	/* allocate and initialize dev->private */
	if (alloc_private(dev, sizeof(struct das800_private)) < 0)
		return -ENOMEM;

	if (iobase == 0) {
		printk("io base address required for das800\n");
		return -EINVAL;
	}

	/* check if io addresses are available */
	if (!request_region(iobase, DAS800_SIZE, "das800")) {
		printk("I/O port conflict\n");
		return -EIO;
	}
	dev->iobase = iobase;

	board = das800_probe(dev);
	if (board < 0) {
		printk("unable to determine board type\n");
		return -ENODEV;
	}
	dev->board_ptr = das800_boards + board;

	/* grab our IRQ */
	if (irq == 1 || irq > 7) {
		printk("irq out of range\n");
		return -EINVAL;
	}
	if (irq) {
		if (request_irq(irq, das800_interrupt, 0, "das800", dev)) {
			printk("unable to allocate irq %u\n", irq);
			return -EINVAL;
		}
	}
	dev->irq = irq;

	dev->board_name = thisboard->name;

	if (alloc_subdevices(dev, 3) < 0)
		return -ENOMEM;

	/* analog input subdevice */
	s = dev->subdevices + 0;
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_CMD_READ;
	s->n_chan = 8;
	s->len_chanlist = 8;
	s->maxdata = (1 << thisboard->resolution) - 1;
	s->range_table = thisboard->ai_range;
	s->do_cmd = das800_ai_do_cmd;
	s->do_cmdtest = das800_ai_do_cmdtest;
	s->insn_read = das800_ai_rinsn;
	s->cancel = das800_cancel;

	/* di */
	s = dev->subdevices + 1;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 3;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das800_di_rbits;

	/* do */
	s = dev->subdevices + 2;
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 4;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das800_do_wbits;

	disable_das800(dev);

	/* initialize digital out channels */
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(CONTROL1, dev->iobase + DAS800_GAIN);	/* select dev->iobase + 2 to be control register 1 */
	outb(CONTROL1_INTE | devpriv->do_bits, dev->iobase + DAS800_CONTROL1);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	return 0;
};

static int das800_detach(struct comedi_device *dev)
{
	printk("comedi%d: das800: remove\n", dev->minor);

	/* only free stuff if it has been allocated by _attach */
	if (dev->iobase)
		release_region(dev->iobase, DAS800_SIZE);
	if (dev->irq)
		free_irq(dev->irq, dev);
	return 0;
};

static int das800_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	devpriv->forever = 0;
	devpriv->count = 0;
	disable_das800(dev);
	return 0;
}

/* enable_das800 makes the card start taking hardware triggered conversions */
static void enable_das800(struct comedi_device *dev)
{
	unsigned long irq_flags;
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	/*  enable fifo-half full interrupts for cio-das802/16 */
	if (thisboard->resolution == 16)
		outb(CIO_ENHF, dev->iobase + DAS800_GAIN);
	outb(CONV_CONTROL, dev->iobase + DAS800_GAIN);	/* select dev->iobase + 2 to be conversion control register */
	outb(CONV_HCEN, dev->iobase + DAS800_CONV_CONTROL);	/* enable hardware triggering */
	outb(CONTROL1, dev->iobase + DAS800_GAIN);	/* select dev->iobase + 2 to be control register 1 */
	outb(CONTROL1_INTE | devpriv->do_bits, dev->iobase + DAS800_CONTROL1);	/* enable card's interrupt */
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);
}

/* disable_das800 stops hardware triggered conversions */
static void disable_das800(struct comedi_device *dev)
{
	unsigned long irq_flags;
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(CONV_CONTROL, dev->iobase + DAS800_GAIN);	/* select dev->iobase + 2 to be conversion control register */
	outb(0x0, dev->iobase + DAS800_CONV_CONTROL);	/* disable hardware triggering of conversions */
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);
}

static int das800_ai_do_cmdtest(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;
	int gain, startChan;
	int i;

	/* step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW | TRIG_EXT;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_FOLLOW;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER | TRIG_EXT;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique and mutually compatible */

	if (cmd->start_src != TRIG_NOW && cmd->start_src != TRIG_EXT)
		err++;
	if (cmd->convert_src != TRIG_TIMER && cmd->convert_src != TRIG_EXT)
		err++;
	if (cmd->stop_src != TRIG_COUNT && cmd->stop_src != TRIG_NONE)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < thisboard->ai_speed) {
			cmd->convert_arg = thisboard->ai_speed;
			err++;
		}
	}
	if (!cmd->chanlist_len) {
		cmd->chanlist_len = 1;
		err++;
	}
	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		if (!cmd->stop_arg) {
			cmd->stop_arg = 1;
			err++;
		}
	} else {		/* TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		/* calculate counter values that give desired timing */
		i8253_cascade_ns_to_timer_2div(TIMER_BASE, &(devpriv->divisor1),
					       &(devpriv->divisor2),
					       &(cmd->convert_arg),
					       cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			err++;
	}

	if (err)
		return 4;

	/*  check channel/gain list against card's limitations */
	if (cmd->chanlist) {
		gain = CR_RANGE(cmd->chanlist[0]);
		startChan = CR_CHAN(cmd->chanlist[0]);
		for (i = 1; i < cmd->chanlist_len; i++) {
			if (CR_CHAN(cmd->chanlist[i]) !=
			    (startChan + i) % N_CHAN_AI) {
				comedi_error(dev,
					     "entries in chanlist must be consecutive channels, counting upwards\n");
				err++;
			}
			if (CR_RANGE(cmd->chanlist[i]) != gain) {
				comedi_error(dev,
					     "entries in chanlist must all have the same gain\n");
				err++;
			}
		}
	}

	if (err)
		return 5;

	return 0;
}

static int das800_ai_do_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	int startChan, endChan, scan, gain;
	int conv_bits;
	unsigned long irq_flags;
	struct comedi_async *async = s->async;

	if (!dev->irq) {
		comedi_error(dev,
			     "no irq assigned for das-800, cannot do hardware conversions");
		return -1;
	}

	disable_das800(dev);

	/* set channel scan limits */
	startChan = CR_CHAN(async->cmd.chanlist[0]);
	endChan = (startChan + async->cmd.chanlist_len - 1) % 8;
	scan = (endChan << 3) | startChan;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(SCAN_LIMITS, dev->iobase + DAS800_GAIN);	/* select base address + 2 to be scan limits register */
	outb(scan, dev->iobase + DAS800_SCAN_LIMITS);	/* set scan limits */
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	/* set gain */
	gain = CR_RANGE(async->cmd.chanlist[0]);
	if (thisboard->resolution == 12 && gain > 0)
		gain += 0x7;
	gain &= 0xf;
	outb(gain, dev->iobase + DAS800_GAIN);

	switch (async->cmd.stop_src) {
	case TRIG_COUNT:
		devpriv->count = async->cmd.stop_arg * async->cmd.chanlist_len;
		devpriv->forever = 0;
		break;
	case TRIG_NONE:
		devpriv->forever = 1;
		devpriv->count = 0;
		break;
	default:
		break;
	}

	/* enable auto channel scan, send interrupts on end of conversion
	 * and set clock source to internal or external
	 */
	conv_bits = 0;
	conv_bits |= EACS | IEOC;
	if (async->cmd.start_src == TRIG_EXT)
		conv_bits |= DTEN;
	switch (async->cmd.convert_src) {
	case TRIG_TIMER:
		conv_bits |= CASC | ITE;
		/* set conversion frequency */
		i8253_cascade_ns_to_timer_2div(TIMER_BASE, &(devpriv->divisor1),
					       &(devpriv->divisor2),
					       &(async->cmd.convert_arg),
					       async->cmd.
					       flags & TRIG_ROUND_MASK);
		if (das800_set_frequency(dev) < 0) {
			comedi_error(dev, "Error setting up counters");
			return -1;
		}
		break;
	case TRIG_EXT:
		break;
	default:
		break;
	}

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(CONV_CONTROL, dev->iobase + DAS800_GAIN);	/* select dev->iobase + 2 to be conversion control register */
	outb(conv_bits, dev->iobase + DAS800_CONV_CONTROL);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);
	async->events = 0;
	enable_das800(dev);
	return 0;
}

static int das800_ai_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	int i, n;
	int chan;
	int range;
	int lsb, msb;
	int timeout = 1000;
	unsigned long irq_flags;

	disable_das800(dev);	/* disable hardware conversions (enables software conversions) */

	/* set multiplexer */
	chan = CR_CHAN(insn->chanspec);

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(CONTROL1, dev->iobase + DAS800_GAIN);	/* select dev->iobase + 2 to be control register 1 */
	outb(chan | devpriv->do_bits, dev->iobase + DAS800_CONTROL1);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	/* set gain / range */
	range = CR_RANGE(insn->chanspec);
	if (thisboard->resolution == 12 && range)
		range += 0x7;
	range &= 0xf;
	outb(range, dev->iobase + DAS800_GAIN);

	udelay(5);

	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		outb_p(0, dev->iobase + DAS800_MSB);

		for (i = 0; i < timeout; i++) {
			if (!(inb(dev->iobase + DAS800_STATUS) & BUSY))
				break;
		}
		if (i == timeout) {
			comedi_error(dev, "timeout");
			return -ETIME;
		}
		lsb = inb(dev->iobase + DAS800_LSB);
		msb = inb(dev->iobase + DAS800_MSB);
		if (thisboard->resolution == 12) {
			data[n] = (lsb >> 4) & 0xff;
			data[n] |= (msb << 4);
		} else {
			data[n] = (msb << 8) | lsb;
		}
	}

	return n;
}

static int das800_di_rbits(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	unsigned int bits;

	bits = inb(dev->iobase + DAS800_STATUS) >> 4;
	bits &= 0x7;
	data[1] = bits;
	data[0] = 0;

	return 2;
}

static int das800_do_wbits(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	int wbits;
	unsigned long irq_flags;

	/*  only set bits that have been masked */
	data[0] &= 0xf;
	wbits = devpriv->do_bits >> 4;
	wbits &= ~data[0];
	wbits |= data[0] & data[1];
	devpriv->do_bits = wbits << 4;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(CONTROL1, dev->iobase + DAS800_GAIN);	/* select dev->iobase + 2 to be control register 1 */
	outb(devpriv->do_bits | CONTROL1_INTE, dev->iobase + DAS800_CONTROL1);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	data[1] = wbits;

	return 2;
}

/* loads counters with divisor1, divisor2 from private structure */
static int das800_set_frequency(struct comedi_device *dev)
{
	int err = 0;

	if (i8254_load(dev->iobase + DAS800_8254, 0, 1, devpriv->divisor1, 2))
		err++;
	if (i8254_load(dev->iobase + DAS800_8254, 0, 2, devpriv->divisor2, 2))
		err++;
	if (err)
		return -1;

	return 0;
}
