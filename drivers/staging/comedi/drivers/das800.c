// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi/drivers/das800.c
 * Driver for Keitley das800 series boards and compatibles
 * Copyright (C) 2000 Frank Mori Hess <fmhess@users.sourceforge.net>
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
 * Driver: das800
 * Description: Keithley Metrabyte DAS800 (& compatibles)
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Devices: [Keithley Metrabyte] DAS-800 (das-800), DAS-801 (das-801),
 * DAS-802 (das-802),
 * [Measurement Computing] CIO-DAS800 (cio-das800),
 * CIO-DAS801 (cio-das801), CIO-DAS802 (cio-das802),
 * CIO-DAS802/16 (cio-das802/16)
 * Status: works, cio-das802/16 untested - email me if you have tested it
 *
 * Configuration options:
 * [0] - I/O port base address
 * [1] - IRQ (optional, required for timed or externally triggered conversions)
 *
 * Notes:
 *	IRQ can be omitted, although the cmd interface will not work without it.
 *
 *	All entries in the channel/gain list must use the same gain and be
 *	consecutive channels counting upwards in channel number (these are
 *	hardware limitations.)
 *
 *	I've never tested the gain setting stuff since I only have a
 *	DAS-800 board with fixed gain.
 *
 *	The cio-das802/16 does not have a fifo-empty status bit!  Therefore
 *	only fifo-half-full transfers are possible with this card.
 *
 * cmd triggers supported:
 *	start_src:      TRIG_NOW | TRIG_EXT
 *	scan_begin_src: TRIG_FOLLOW
 *	scan_end_src:   TRIG_COUNT
 *	convert_src:    TRIG_TIMER | TRIG_EXT
 *	stop_src:       TRIG_NONE | TRIG_COUNT
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include "../comedidev.h"

#include "comedi_8254.h"

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
#define   CIO_FFOV              0x8   /* cio-das802/16 fifo overflow */
#define   CIO_ENHF              0x90  /* cio-das802/16 fifo half full int ena */
#define   CONTROL1              0x80
#define   CONV_CONTROL          0xa0
#define   SCAN_LIMITS           0xc0
#define   ID                    0xe0
#define DAS800_8254           4
#define DAS800_STATUS2        7
#define   STATUS2_HCEN          0x80
#define   STATUS2_INTE          0X20
#define DAS800_ID             7

#define DAS802_16_HALF_FIFO_SZ	128

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
	unsigned int do_bits;	/* digital output bits */
};

static void das800_ind_write(struct comedi_device *dev,
			     unsigned int val, unsigned int reg)
{
	/*
	 * Select dev->iobase + 2 to be desired register
	 * then write to that register.
	 */
	outb(reg, dev->iobase + DAS800_GAIN);
	outb(val, dev->iobase + 2);
}

static unsigned int das800_ind_read(struct comedi_device *dev, unsigned int reg)
{
	/*
	 * Select dev->iobase + 7 to be desired register
	 * then read from that register.
	 */
	outb(reg, dev->iobase + DAS800_GAIN);
	return inb(dev->iobase + 7);
}

static void das800_enable(struct comedi_device *dev)
{
	const struct das800_board *board = dev->board_ptr;
	struct das800_private *devpriv = dev->private;
	unsigned long irq_flags;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	/*  enable fifo-half full interrupts for cio-das802/16 */
	if (board->resolution == 16)
		outb(CIO_ENHF, dev->iobase + DAS800_GAIN);
	/* enable hardware triggering */
	das800_ind_write(dev, CONV_HCEN, CONV_CONTROL);
	/* enable card's interrupt */
	das800_ind_write(dev, CONTROL1_INTE | devpriv->do_bits, CONTROL1);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);
}

static void das800_disable(struct comedi_device *dev)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	/* disable hardware triggering of conversions */
	das800_ind_write(dev, 0x0, CONV_CONTROL);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);
}

static int das800_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	das800_disable(dev);
	return 0;
}

static int das800_ai_check_chanlist(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_cmd *cmd)
{
	unsigned int chan0 = CR_CHAN(cmd->chanlist[0]);
	unsigned int range0 = CR_RANGE(cmd->chanlist[0]);
	int i;

	for (i = 1; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);
		unsigned int range = CR_RANGE(cmd->chanlist[i]);

		if (chan != (chan0 + i) % s->n_chan) {
			dev_dbg(dev->class_dev,
				"chanlist must be consecutive, counting upwards\n");
			return -EINVAL;
		}

		if (range != range0) {
			dev_dbg(dev->class_dev,
				"chanlist must all have the same gain\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int das800_ai_do_cmdtest(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_cmd *cmd)
{
	const struct das800_board *board = dev->board_ptr;
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

	if (cmd->convert_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
						    board->ai_speed);
	}

	err |= comedi_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		unsigned int arg = cmd->convert_arg;

		comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= das800_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int das800_ai_do_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	const struct das800_board *board = dev->board_ptr;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int gain = CR_RANGE(cmd->chanlist[0]);
	unsigned int start_chan = CR_CHAN(cmd->chanlist[0]);
	unsigned int end_chan = (start_chan + cmd->chanlist_len - 1) % 8;
	unsigned int scan_chans = (end_chan << 3) | start_chan;
	int conv_bits;
	unsigned long irq_flags;

	das800_disable(dev);

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	/* set scan limits */
	das800_ind_write(dev, scan_chans, SCAN_LIMITS);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	/* set gain */
	if (board->resolution == 12 && gain > 0)
		gain += 0x7;
	gain &= 0xf;
	outb(gain, dev->iobase + DAS800_GAIN);

	/* enable auto channel scan, send interrupts on end of conversion
	 * and set clock source to internal or external
	 */
	conv_bits = 0;
	conv_bits |= EACS | IEOC;
	if (cmd->start_src == TRIG_EXT)
		conv_bits |= DTEN;
	if (cmd->convert_src == TRIG_TIMER) {
		conv_bits |= CASC | ITE;
		comedi_8254_update_divisors(dev->pacer);
		comedi_8254_pacer_enable(dev->pacer, 1, 2, true);
	}

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	das800_ind_write(dev, conv_bits, CONV_CONTROL);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	das800_enable(dev);
	return 0;
}

static unsigned int das800_ai_get_sample(struct comedi_device *dev)
{
	unsigned int lsb = inb(dev->iobase + DAS800_LSB);
	unsigned int msb = inb(dev->iobase + DAS800_MSB);

	return (msb << 8) | lsb;
}

static irqreturn_t das800_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct das800_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async;
	struct comedi_cmd *cmd;
	unsigned long irq_flags;
	unsigned int status;
	unsigned int val;
	bool fifo_empty;
	bool fifo_overflow;
	int i;

	status = inb(dev->iobase + DAS800_STATUS);
	if (!(status & IRQ))
		return IRQ_NONE;
	if (!dev->attached)
		return IRQ_HANDLED;

	async = s->async;
	cmd = &async->cmd;

	spin_lock_irqsave(&dev->spinlock, irq_flags);
	status = das800_ind_read(dev, CONTROL1) & STATUS2_HCEN;
	/*
	 * Don't release spinlock yet since we want to make sure
	 * no one else disables hardware conversions.
	 */

	/* if hardware conversions are not enabled, then quit */
	if (status == 0) {
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		return IRQ_HANDLED;
	}

	for (i = 0; i < DAS802_16_HALF_FIFO_SZ; i++) {
		val = das800_ai_get_sample(dev);
		if (s->maxdata == 0x0fff) {
			fifo_empty = !!(val & FIFO_EMPTY);
			fifo_overflow = !!(val & FIFO_OVF);
		} else {
			/* cio-das802/16 has no fifo empty status bit */
			fifo_empty = false;
			fifo_overflow = !!(inb(dev->iobase + DAS800_GAIN) &
						CIO_FFOV);
		}
		if (fifo_empty || fifo_overflow)
			break;

		if (s->maxdata == 0x0fff)
			val >>= 4;	/* 12-bit sample */

		val &= s->maxdata;
		comedi_buf_write_samples(s, &val, 1);

		if (cmd->stop_src == TRIG_COUNT &&
		    async->scans_done >= cmd->stop_arg) {
			async->events |= COMEDI_CB_EOA;
			break;
		}
	}

	if (fifo_overflow) {
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		async->events |= COMEDI_CB_ERROR;
		comedi_handle_events(dev, s);
		return IRQ_HANDLED;
	}

	if (!(async->events & COMEDI_CB_CANCEL_MASK)) {
		/*
		 * Re-enable card's interrupt.
		 * We already have spinlock, so indirect addressing is safe
		 */
		das800_ind_write(dev, CONTROL1_INTE | devpriv->do_bits,
				 CONTROL1);
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
	} else {
		/* otherwise, stop taking data */
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		das800_disable(dev);
	}
	comedi_handle_events(dev, s);
	return IRQ_HANDLED;
}

static int das800_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DAS800_STATUS);
	if ((status & BUSY) == 0)
		return 0;
	return -EBUSY;
}

static int das800_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct das800_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned long irq_flags;
	unsigned int val;
	int ret;
	int i;

	das800_disable(dev);

	/* set multiplexer */
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	das800_ind_write(dev, chan | devpriv->do_bits, CONTROL1);
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	/* set gain / range */
	if (s->maxdata == 0x0fff && range)
		range += 0x7;
	range &= 0xf;
	outb(range, dev->iobase + DAS800_GAIN);

	udelay(5);

	for (i = 0; i < insn->n; i++) {
		/* trigger conversion */
		outb_p(0, dev->iobase + DAS800_MSB);

		ret = comedi_timeout(dev, s, insn, das800_ai_eoc, 0);
		if (ret)
			return ret;

		val = das800_ai_get_sample(dev);
		if (s->maxdata == 0x0fff)
			val >>= 4;	/* 12-bit sample */
		data[i] = val & s->maxdata;
	}

	return insn->n;
}

static int das800_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	data[1] = (inb(dev->iobase + DAS800_STATUS) >> 4) & 0x7;

	return insn->n;
}

static int das800_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct das800_private *devpriv = dev->private;
	unsigned long irq_flags;

	if (comedi_dio_update_state(s, data)) {
		devpriv->do_bits = s->state << 4;

		spin_lock_irqsave(&dev->spinlock, irq_flags);
		das800_ind_write(dev, CONTROL1_INTE | devpriv->do_bits,
				 CONTROL1);
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
	}

	data[1] = s->state;

	return insn->n;
}

static const struct das800_board *das800_probe(struct comedi_device *dev)
{
	const struct das800_board *board = dev->board_ptr;
	int index = board ? board - das800_boards : -EINVAL;
	int id_bits;
	unsigned long irq_flags;

	/*
	 * The dev->board_ptr will be set by comedi_device_attach() if the
	 * board name provided by the user matches a board->name in this
	 * driver. If so, this function sanity checks the id_bits to verify
	 * that the board is correct.
	 *
	 * If the dev->board_ptr is not set, the user is trying to attach
	 * an unspecified board to this driver. In this case the id_bits
	 * are used to 'probe' for the correct dev->board_ptr.
	 */
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	id_bits = das800_ind_read(dev, ID) & 0x3;
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	switch (id_bits) {
	case 0x0:
		if (index == BOARD_DAS800 || index == BOARD_CIODAS800)
			return board;
		index = BOARD_DAS800;
		break;
	case 0x2:
		if (index == BOARD_DAS801 || index == BOARD_CIODAS801)
			return board;
		index = BOARD_DAS801;
		break;
	case 0x3:
		if (index == BOARD_DAS802 || index == BOARD_CIODAS802 ||
		    index == BOARD_CIODAS80216)
			return board;
		index = BOARD_DAS802;
		break;
	default:
		dev_dbg(dev->class_dev, "Board model: 0x%x (unknown)\n",
			id_bits);
		return NULL;
	}
	dev_dbg(dev->class_dev, "Board model (probed): %s series\n",
		das800_boards[index].name);

	return &das800_boards[index];
}

static int das800_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct das800_board *board;
	struct das800_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int irq = it->options[1];
	unsigned long irq_flags;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x8);
	if (ret)
		return ret;

	board = das800_probe(dev);
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	if (irq > 1 && irq <= 7) {
		ret = request_irq(irq, das800_interrupt, 0, dev->board_name,
				  dev);
		if (ret == 0)
			dev->irq = irq;
	}

	dev->pacer = comedi_8254_init(dev->iobase + DAS800_8254,
				      I8254_OSC_BASE_1MHZ, I8254_IO8, 0);
	if (!dev->pacer)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND;
	s->n_chan	= 8;
	s->maxdata	= (1 << board->resolution) - 1;
	s->range_table	= board->ai_range;
	s->insn_read	= das800_ai_insn_read;
	if (dev->irq) {
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= 8;
		s->do_cmdtest	= das800_ai_do_cmdtest;
		s->do_cmd	= das800_ai_do_cmd;
		s->cancel	= das800_cancel;
	}

	/* Digital Input subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 3;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das800_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das800_do_insn_bits;

	das800_disable(dev);

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
