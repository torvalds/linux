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

static const struct comedi_lrange range_das801_ai = {
	9, {
		BIP_RANGE(5),
		BIP_RANGE(10),
		UNI_RANGE(10),
		BIP_RANGE(0.5),
		UNI_RANGE(1),
		BIP_RANGE(0.05),
		UNI_RANGE(0.1),
		BIP_RANGE(0.01),
		UNI_RANGE(0.02)
	}
};

static const struct comedi_lrange range_cio_das801_ai = {
	9, {
		BIP_RANGE(5),
		BIP_RANGE(10),
		UNI_RANGE(10),
		BIP_RANGE(0.5),
		UNI_RANGE(1),
		BIP_RANGE(0.05),
		UNI_RANGE(0.1),
		BIP_RANGE(0.005),
		UNI_RANGE(0.01)
	}
};

static const struct comedi_lrange range_das802_ai = {
	9, {
		BIP_RANGE(5),
		BIP_RANGE(10),
		UNI_RANGE(10),
		BIP_RANGE(2.5),
		UNI_RANGE(5),
		BIP_RANGE(1.25),
		UNI_RANGE(2.5),
		BIP_RANGE(0.625),
		UNI_RANGE(1.25)
	}
};

static const struct comedi_lrange range_das80216_ai = {
	8, {
		BIP_RANGE(10),
		UNI_RANGE(10),
		BIP_RANGE(5),
		UNI_RANGE(5),
		BIP_RANGE(2.5),
		UNI_RANGE(2.5),
		BIP_RANGE(1.25),
		UNI_RANGE(1.25)
	}
};

enum das800_boardinfo {
	BOARD_DAS800,
	BOARD_CIODAS800,
	BOARD_DAS801,
	BOARD_CIODAS801,
	BOARD_DAS802,
	BOARD_CIODAS802,
	BOARD_CIODAS80216,
};

static const struct das800_board das800_boards[] = {
	[BOARD_DAS800] = {
		.name		= "das-800",
		.ai_speed	= 25000,
		.ai_range	= &range_bipolar5,
		.resolution	= 12,
	},
	[BOARD_CIODAS800] = {
		.name		= "cio-das800",
		.ai_speed	= 20000,
		.ai_range	= &range_bipolar5,
		.resolution	= 12,
	},
	[BOARD_DAS801] = {
		.name		= "das-801",
		.ai_speed	= 25000,
		.ai_range	= &range_das801_ai,
		.resolution	= 12,
	},
	[BOARD_CIODAS801] = {
		.name		= "cio-das801",
		.ai_speed	= 20000,
		.ai_range	= &range_cio_das801_ai,
		.resolution	= 12,
	},
	[BOARD_DAS802] = {
		.name		= "das-802",
		.ai_speed	= 25000,
		.ai_range	= &range_das802_ai,
		.resolution	= 12,
	},
	[BOARD_CIODAS802] = {
		.name		= "cio-das802",
		.ai_speed	= 20000,
		.ai_range	= &range_das802_ai,
		.resolution	= 12,
	},
	[BOARD_CIODAS80216] = {
		.name		= "cio-das802/16",
		.ai_speed	= 10000,
		.ai_range	= &range_das80216_ai,
		.resolution	= 16,
	},
};

struct das800_private {
	unsigned int count;	/* number of data points left to be taken */
	int forever;		/* flag that we should take data forever */
	unsigned int divisor1;	/* counter 1 value for timed conversions */
	unsigned int divisor2;	/* counter 2 value for timed conversions */
	int do_bits;		/* digital output bits */
};

static void das800_ind_write(struct comedi_device *dev,
			     unsigned val, unsigned reg)
{
	/*
	 * Select dev->iobase + 2 to be desired register
	 * then write to that register.
	 */
	outb(reg, dev->iobase + DAS800_GAIN);
	outb(val, dev->iobase + 2);
}

static unsigned das800_ind_read(struct comedi_device *dev, unsigned reg)
{
	/*
	 * Select dev->iobase + 7 to be desired register
	 * then read from that register.
	 */
	outb(reg, dev->iobase + DAS800_GAIN);
	return inb(dev->iobase + 7);
}

/* enable_das800 makes the card start taking hardware triggered conversions */
static void enable_das800(struct comedi_device *dev)
{
	const struct das800_board *thisboard = comedi_board(dev);
	struct das800_private *devpriv = dev->private;
	unsigned long irq_flags;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	/*  enable fifo-half full interrupts for cio-das802/16 */
	if (thisboard->resolution == 16)
		outb(CIO_ENHF, dev->iobase + DAS800_GAIN);
	/* enable hardware triggering */
	das800_ind_write(dev, CONV_HCEN, CONV_CONTROL);
	/* enable card's interrupt */
	das800_ind_write(dev, CONTROL1_INTE | devpriv->do_bits, CONTROL1);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);
}

/* disable_das800 stops hardware triggered conversions */
static void disable_das800(struct comedi_device *dev)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	/* disable hardware triggering of conversions */
	das800_ind_write(dev, 0x0, CONV_CONTROL);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);
}

static int das800_set_frequency(struct comedi_device *dev)
{
	struct das800_private *devpriv = dev->private;
	int err = 0;

	if (i8254_load(dev->iobase + DAS800_8254, 0, 1, devpriv->divisor1, 2))
		err++;
	if (i8254_load(dev->iobase + DAS800_8254, 0, 2, devpriv->divisor2, 2))
		err++;
	if (err)
		return -1;

	return 0;
}

static int das800_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct das800_private *devpriv = dev->private;

	devpriv->forever = 0;
	devpriv->count = 0;
	disable_das800(dev);
	return 0;
}

static int das800_ai_do_cmdtest(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_cmd *cmd)
{
	const struct das800_board *thisboard = comedi_board(dev);
	struct das800_private *devpriv = dev->private;
	int err = 0;

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

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->convert_src == TRIG_TIMER)
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg,
						 thisboard->ai_speed);

	err |= cfc_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		int tmp = cmd->convert_arg;

		/* calculate counter values that give desired timing */
		i8253_cascade_ns_to_timer_2div(TIMER_BASE,
					       &devpriv->divisor1,
					       &devpriv->divisor2,
					       &cmd->convert_arg,
					       cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			err++;
	}

	if (err)
		return 4;

	/*  check channel/gain list against card's limitations */
	if (cmd->chanlist) {
		unsigned int chan = CR_CHAN(cmd->chanlist[0]);
		unsigned int range = CR_RANGE(cmd->chanlist[0]);
		unsigned int next;
		int i;

		for (i = 1; i < cmd->chanlist_len; i++) {
			next = cmd->chanlist[i];
			if (CR_CHAN(next) != (chan + i) % N_CHAN_AI) {
				dev_err(dev->class_dev,
					"chanlist must be consecutive, counting upwards\n");
				err++;
			}
			if (CR_RANGE(next) != range) {
				dev_err(dev->class_dev,
					"chanlist must all have the same gain\n");
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
	const struct das800_board *thisboard = comedi_board(dev);
	struct das800_private *devpriv = dev->private;
	int startChan, endChan, scan, gain;
	int conv_bits;
	unsigned long irq_flags;
	struct comedi_async *async = s->async;

	disable_das800(dev);

	/* set channel scan limits */
	startChan = CR_CHAN(async->cmd.chanlist[0]);
	endChan = (startChan + async->cmd.chanlist_len - 1) % 8;
	scan = (endChan << 3) | startChan;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	/* set scan limits */
	das800_ind_write(dev, scan, SCAN_LIMITS);
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
	das800_ind_write(dev, conv_bits, CONV_CONTROL);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	async->events = 0;
	enable_das800(dev);
	return 0;
}

static irqreturn_t das800_interrupt(int irq, void *d)
{
	short i;		/* loop index */
	short dataPoint = 0;
	struct comedi_device *dev = d;
	const struct das800_board *thisboard = comedi_board(dev);
	struct das800_private *devpriv = dev->private;
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
	status = das800_ind_read(dev, CONTROL1) & STATUS2_HCEN;
	/* don't release spinlock yet since we want to make sure no one else disables hardware conversions */
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
		das800_cancel(dev, s);
		async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		comedi_event(dev, s);
		async->events = 0;
		return IRQ_HANDLED;
	}
	if (devpriv->count > 0 || devpriv->forever == 1) {
		/* Re-enable card's interrupt.
		 * We already have spinlock, so indirect addressing is safe */
		das800_ind_write(dev, CONTROL1_INTE | devpriv->do_bits,
				 CONTROL1);
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		/* otherwise, stop taking data */
	} else {
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		disable_das800(dev);	/* disable hardware triggered conversions */
		async->events |= COMEDI_CB_EOA;
	}
	comedi_event(dev, s);
	async->events = 0;
	return IRQ_HANDLED;
}

static int das800_ai_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	const struct das800_board *thisboard = comedi_board(dev);
	struct das800_private *devpriv = dev->private;
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
	das800_ind_write(dev, chan | devpriv->do_bits, CONTROL1);
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

	return insn->n;
}

static int das800_do_wbits(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	struct das800_private *devpriv = dev->private;
	int wbits;
	unsigned long irq_flags;

	/*  only set bits that have been masked */
	data[0] &= 0xf;
	wbits = devpriv->do_bits >> 4;
	wbits &= ~data[0];
	wbits |= data[0] & data[1];
	devpriv->do_bits = wbits << 4;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	das800_ind_write(dev, CONTROL1_INTE | devpriv->do_bits, CONTROL1);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	data[1] = wbits;

	return insn->n;
}

static int das800_probe(struct comedi_device *dev)
{
	const struct das800_board *thisboard = comedi_board(dev);
	int board = thisboard ? thisboard - das800_boards : -EINVAL;
	int id_bits;
	unsigned long irq_flags;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	id_bits = das800_ind_read(dev, ID) & 0x3;
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	switch (id_bits) {
	case 0x0:
		if (board == BOARD_DAS800 || board == BOARD_CIODAS800)
			break;
		dev_dbg(dev->class_dev, "Board model (probed): DAS-800\n");
		board = BOARD_DAS800;
		break;
	case 0x2:
		if (board == BOARD_DAS801 || board == BOARD_CIODAS801)
			break;
		dev_dbg(dev->class_dev, "Board model (probed): DAS-801\n");
		board = BOARD_DAS801;
		break;
	case 0x3:
		if (board == BOARD_DAS802 || board == BOARD_CIODAS802 ||
		    board == BOARD_CIODAS80216)
			break;
		dev_dbg(dev->class_dev, "Board model (probed): DAS-802\n");
		board = BOARD_DAS802;
		break;
	default:
		dev_dbg(dev->class_dev, "Board model: 0x%x (unknown)\n",
			id_bits);
		board = -EINVAL;
		break;
	}
	return board;
}

static int das800_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct das800_board *thisboard = comedi_board(dev);
	struct das800_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int irq = it->options[1];
	unsigned long irq_flags;
	int board;
	int ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_request_region(dev, it->options[0], DAS800_SIZE);
	if (ret)
		return ret;

	board = das800_probe(dev);
	if (board < 0) {
		dev_dbg(dev->class_dev, "unable to determine board type\n");
		return -ENODEV;
	}
	dev->board_ptr = das800_boards + board;
	thisboard = comedi_board(dev);

	/* grab our IRQ */
	if (irq == 1 || irq > 7) {
		dev_err(dev->class_dev, "irq out of range\n");
		return -EINVAL;
	}
	if (irq) {
		if (request_irq(irq, das800_interrupt, 0, "das800", dev)) {
			dev_err(dev->class_dev, "unable to allocate irq %u\n",
				irq);
			return -EINVAL;
		}
	}
	dev->irq = irq;

	dev->board_name = thisboard->name;

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	/* analog input subdevice */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND;
	s->n_chan	= 8;
	s->maxdata	= (1 << thisboard->resolution) - 1;
	s->range_table	= thisboard->ai_range;
	s->insn_read	= das800_ai_rinsn;
	if (dev->irq) {
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= 8;
		s->do_cmdtest	= das800_ai_do_cmdtest;
		s->do_cmd	= das800_ai_do_cmd;
		s->cancel	= das800_cancel;
	}

	/* di */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 3;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das800_di_rbits;

	/* do */
	s = &dev->subdevices[2];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 4;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das800_do_wbits;

	disable_das800(dev);

	/* initialize digital out channels */
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	das800_ind_write(dev, CONTROL1_INTE | devpriv->do_bits, CONTROL1);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	return 0;
};

static struct comedi_driver driver_das800 = {
	.driver_name	= "das800",
	.module		= THIS_MODULE,
	.attach		= das800_attach,
	.detach		= comedi_legacy_detach,
	.num_names	= ARRAY_SIZE(das800_boards),
	.board_name	= &das800_boards[0].name,
	.offset		= sizeof(struct das800_board),
};
module_comedi_driver(driver_das800);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
