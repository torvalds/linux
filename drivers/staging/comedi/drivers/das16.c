/*
 * das16.c
 * DAS16 driver
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2000 Chris R. Baugher <baugher@enteract.com>
 * Copyright (C) 2001,2002 Frank Mori Hess <fmhess@users.sourceforge.net>
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
 * Driver: das16
 * Description: DAS16 compatible boards
 * Author: Sam Moore, Warren Jasper, ds, Chris Baugher, Frank Hess, Roman Fietze
 * Devices: [Keithley Metrabyte] DAS-16 (das-16), DAS-16G (das-16g),
 *   DAS-16F (das-16f), DAS-1201 (das-1201), DAS-1202 (das-1202),
 *   DAS-1401 (das-1401), DAS-1402 (das-1402), DAS-1601 (das-1601),
 *   DAS-1602 (das-1602),
 *   [ComputerBoards] PC104-DAS16/JR (pc104-das16jr),
 *   PC104-DAS16JR/16 (pc104-das16jr/16), CIO-DAS16 (cio-das16),
 *   CIO-DAS16F (cio-das16/f), CIO-DAS16/JR (cio-das16/jr),
 *   CIO-DAS16JR/16 (cio-das16jr/16), CIO-DAS1401/12 (cio-das1401/12),
 *   CIO-DAS1402/12 (cio-das1402/12), CIO-DAS1402/16 (cio-das1402/16),
 *   CIO-DAS1601/12 (cio-das1601/12), CIO-DAS1602/12 (cio-das1602/12),
 *   CIO-DAS1602/16 (cio-das1602/16), CIO-DAS16/330 (cio-das16/330)
 * Status: works
 * Updated: 2003-10-12
 *
 * A rewrite of the das16 and das1600 drivers.
 *
 * Options:
 *	[0] - base io address
 *	[1] - irq (does nothing, irq is not used anymore)
 *	[2] - dma channel (optional, required for comedi_command support)
 *	[3] - master clock speed in MHz (optional, 1 or 10, ignored if
 *		board can probe clock, defaults to 1)
 *	[4] - analog input range lowest voltage in microvolts (optional,
 *		only useful if your board does not have software
 *		programmable gain)
 *	[5] - analog input range highest voltage in microvolts (optional,
 *		only useful if board does not have software programmable
 *		gain)
 *	[6] - analog output range lowest voltage in microvolts (optional)
 *	[7] - analog output range highest voltage in microvolts (optional)
 *
 * Passing a zero for an option is the same as leaving it unspecified.
 */

/*
 * Testing and debugging help provided by Daniel Koch.
 *
 * Keithley Manuals:
 *	2309.PDF (das16)
 *	4919.PDF (das1400, 1600)
 *	4922.PDF (das-1400)
 *	4923.PDF (das1200, 1400, 1600)
 *
 * Computer boards manuals also available from their website
 * www.measurementcomputing.com
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "comedi_isadma.h"
#include "comedi_fc.h"
#include "8253.h"
#include "8255.h"

#define DAS16_DMA_SIZE 0xff00	/*  size in bytes of allocated dma buffer */

/*
 * Register I/O map
 */
#define DAS16_TRIG_REG			0x00
#define DAS16_AI_LSB_REG		0x00
#define DAS16_AI_MSB_REG		0x01
#define DAS16_MUX_REG			0x02
#define DAS16_DIO_REG			0x03
#define DAS16_AO_LSB_REG(x)		((x) ? 0x06 : 0x04)
#define DAS16_AO_MSB_REG(x)		((x) ? 0x07 : 0x05)
#define DAS16_STATUS_REG		0x08
#define DAS16_STATUS_BUSY		(1 << 7)
#define DAS16_STATUS_UNIPOLAR		(1 << 6)
#define DAS16_STATUS_MUXBIT		(1 << 5)
#define DAS16_STATUS_INT		(1 << 4)
#define DAS16_CTRL_REG			0x09
#define DAS16_CTRL_INTE			(1 << 7)
#define DAS16_CTRL_IRQ(x)		(((x) & 0x7) << 4)
#define DAS16_CTRL_DMAE			(1 << 2)
#define DAS16_CTRL_PACING_MASK		(3 << 0)
#define DAS16_CTRL_INT_PACER		(3 << 0)
#define DAS16_CTRL_EXT_PACER		(2 << 0)
#define DAS16_CTRL_SOFT_PACER		(0 << 0)
#define DAS16_PACER_REG			0x0a
#define DAS16_PACER_BURST_LEN(x)	(((x) & 0xf) << 4)
#define DAS16_PACER_CTR0		(1 << 1)
#define DAS16_PACER_TRIG0		(1 << 0)
#define DAS16_GAIN_REG			0x0b
#define DAS16_TIMER_BASE_REG		0x0c	/* to 0x0f */

#define DAS1600_CONV_REG		0x404
#define DAS1600_CONV_DISABLE		(1 << 6)
#define DAS1600_BURST_REG		0x405
#define DAS1600_BURST_VAL		(1 << 6)
#define DAS1600_ENABLE_REG		0x406
#define DAS1600_ENABLE_VAL		(1 << 6)
#define DAS1600_STATUS_REG		0x407
#define DAS1600_STATUS_BME		(1 << 6)
#define DAS1600_STATUS_ME		(1 << 5)
#define DAS1600_STATUS_CD		(1 << 4)
#define DAS1600_STATUS_WS		(1 << 1)
#define DAS1600_STATUS_CLK_10MHZ	(1 << 0)

static const struct comedi_lrange range_das1x01_bip = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(1),
		BIP_RANGE(0.1),
		BIP_RANGE(0.01)
	}
};

static const struct comedi_lrange range_das1x01_unip = {
	4, {
		UNI_RANGE(10),
		UNI_RANGE(1),
		UNI_RANGE(0.1),
		UNI_RANGE(0.01)
	}
};

static const struct comedi_lrange range_das1x02_bip = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25)
	}
};

static const struct comedi_lrange range_das1x02_unip = {
	4, {
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

static const struct comedi_lrange range_das16jr = {
	9, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

static const struct comedi_lrange range_das16jr_16 = {
	8, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

static const int das16jr_gainlist[] = { 8, 0, 1, 2, 3, 4, 5, 6, 7 };
static const int das16jr_16_gainlist[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const int das1600_gainlist[] = { 0, 1, 2, 3 };

enum {
	das16_pg_none = 0,
	das16_pg_16jr,
	das16_pg_16jr_16,
	das16_pg_1601,
	das16_pg_1602,
};
static const int *const das16_gainlists[] = {
	NULL,
	das16jr_gainlist,
	das16jr_16_gainlist,
	das1600_gainlist,
	das1600_gainlist,
};

static const struct comedi_lrange *const das16_ai_uni_lranges[] = {
	&range_unknown,
	&range_das16jr,
	&range_das16jr_16,
	&range_das1x01_unip,
	&range_das1x02_unip,
};

static const struct comedi_lrange *const das16_ai_bip_lranges[] = {
	&range_unknown,
	&range_das16jr,
	&range_das16jr_16,
	&range_das1x01_bip,
	&range_das1x02_bip,
};

struct das16_board {
	const char *name;
	unsigned int ai_maxdata;
	unsigned int ai_speed;	/*  max conversion speed in nanosec */
	unsigned int ai_pg;
	unsigned int has_ao:1;
	unsigned int has_8255:1;

	unsigned int i8255_offset;

	unsigned int size;
	unsigned int id;
};

static const struct das16_board das16_boards[] = {
	{
		.name		= "das-16",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 15000,
		.ai_pg		= das16_pg_none,
		.has_ao		= 1,
		.has_8255	= 1,
		.i8255_offset	= 0x10,
		.size		= 0x14,
		.id		= 0x00,
	}, {
		.name		= "das-16g",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 15000,
		.ai_pg		= das16_pg_none,
		.has_ao		= 1,
		.has_8255	= 1,
		.i8255_offset	= 0x10,
		.size		= 0x14,
		.id		= 0x00,
	}, {
		.name		= "das-16f",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 8500,
		.ai_pg		= das16_pg_none,
		.has_ao		= 1,
		.has_8255	= 1,
		.i8255_offset	= 0x10,
		.size		= 0x14,
		.id		= 0x00,
	}, {
		.name		= "cio-das16",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 20000,
		.ai_pg		= das16_pg_none,
		.has_ao		= 1,
		.has_8255	= 1,
		.i8255_offset	= 0x10,
		.size		= 0x14,
		.id		= 0x80,
	}, {
		.name		= "cio-das16/f",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_none,
		.has_ao		= 1,
		.has_8255	= 1,
		.i8255_offset	= 0x10,
		.size		= 0x14,
		.id		= 0x80,
	}, {
		.name		= "cio-das16/jr",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 7692,
		.ai_pg		= das16_pg_16jr,
		.size		= 0x10,
		.id		= 0x00,
	}, {
		.name		= "pc104-das16jr",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 3300,
		.ai_pg		= das16_pg_16jr,
		.size		= 0x10,
		.id		= 0x00,
	}, {
		.name		= "cio-das16jr/16",
		.ai_maxdata	= 0xffff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_16jr_16,
		.size		= 0x10,
		.id		= 0x00,
	}, {
		.name		= "pc104-das16jr/16",
		.ai_maxdata	= 0xffff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_16jr_16,
		.size		= 0x10,
		.id		= 0x00,
	}, {
		.name		= "das-1201",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 20000,
		.ai_pg		= das16_pg_none,
		.has_8255	= 1,
		.i8255_offset	= 0x400,
		.size		= 0x408,
		.id		= 0x20,
	}, {
		.name		= "das-1202",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_none,
		.has_8255	= 1,
		.i8255_offset	= 0x400,
		.size		= 0x408,
		.id		= 0x20,
	}, {
		.name		= "das-1401",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_1601,
		.size		= 0x408,
		.id		= 0xc0,
	}, {
		.name		= "das-1402",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_1602,
		.size		= 0x408,
		.id		= 0xc0,
	}, {
		.name		= "das-1601",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_1601,
		.has_ao		= 1,
		.has_8255	= 1,
		.i8255_offset	= 0x400,
		.size		= 0x408,
		.id		= 0xc0,
	}, {
		.name		= "das-1602",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_1602,
		.has_ao		= 1,
		.has_8255	= 1,
		.i8255_offset	= 0x400,
		.size		= 0x408,
		.id		= 0xc0,
	}, {
		.name		= "cio-das1401/12",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 6250,
		.ai_pg		= das16_pg_1601,
		.size		= 0x408,
		.id		= 0xc0,
	}, {
		.name		= "cio-das1402/12",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 6250,
		.ai_pg		= das16_pg_1602,
		.size		= 0x408,
		.id		= 0xc0,
	}, {
		.name		= "cio-das1402/16",
		.ai_maxdata	= 0xffff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_1602,
		.size		= 0x408,
		.id		= 0xc0,
	}, {
		.name		= "cio-das1601/12",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 6250,
		.ai_pg		= das16_pg_1601,
		.has_ao		= 1,
		.has_8255	= 1,
		.i8255_offset	= 0x400,
		.size		= 0x408,
		.id		= 0xc0,
	}, {
		.name		= "cio-das1602/12",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_1602,
		.has_ao		= 1,
		.has_8255	= 1,
		.i8255_offset	= 0x400,
		.size		= 0x408,
		.id		= 0xc0,
	}, {
		.name		= "cio-das1602/16",
		.ai_maxdata	= 0xffff,
		.ai_speed	= 10000,
		.ai_pg		= das16_pg_1602,
		.has_ao		= 1,
		.has_8255	= 1,
		.i8255_offset	= 0x400,
		.size		= 0x408,
		.id		= 0xc0,
	}, {
		.name		= "cio-das16/330",
		.ai_maxdata	= 0x0fff,
		.ai_speed	= 3030,
		.ai_pg		= das16_pg_16jr,
		.size		= 0x14,
		.id		= 0xf0,
	},
};

/* Period for timer interrupt in jiffies.  It's a function
 * to deal with possibility of dynamic HZ patches  */
static inline int timer_period(void)
{
	return HZ / 20;
}

struct das16_private_struct {
	struct comedi_isadma	*dma;
	unsigned int		clockbase;
	unsigned int		ctrl_reg;
	unsigned int		divisor1;
	unsigned int		divisor2;
	struct timer_list	timer;
	unsigned long		extra_iobase;
	unsigned int		can_burst:1;
	unsigned int		timer_running:1;
};

static void das16_ai_setup_dma(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       unsigned int unread_samples)
{
	struct das16_private_struct *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[dma->cur_dma];
	unsigned int max_samples = comedi_bytes_to_samples(s, desc->maxsize);
	unsigned int nsamples;

	/*
	 * Determine dma size based on the buffer size plus the number of
	 * unread samples and the number of samples remaining in the command.
	 */
	nsamples = comedi_nsamples_left(s, max_samples + unread_samples);
	if (nsamples > unread_samples) {
		nsamples -= unread_samples;
		desc->size = comedi_samples_to_bytes(s, nsamples);
		comedi_isadma_program(desc);
	}
}

static void das16_interrupt(struct comedi_device *dev)
{
	struct das16_private_struct *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[dma->cur_dma];
	unsigned long spin_flags;
	unsigned int residue;
	unsigned int nbytes;
	unsigned int nsamples;

	spin_lock_irqsave(&dev->spinlock, spin_flags);
	if (!(devpriv->ctrl_reg & DAS16_CTRL_DMAE)) {
		spin_unlock_irqrestore(&dev->spinlock, spin_flags);
		return;
	}

	/*
	 * The pc104-das16jr (at least) has problems if the dma
	 * transfer is interrupted in the middle of transferring
	 * a 16 bit sample.
	 */
	residue = comedi_isadma_disable_on_sample(desc->chan,
						  comedi_bytes_per_sample(s));

	/* figure out how many samples to read */
	if (residue > desc->size) {
		dev_err(dev->class_dev, "residue > transfer size!\n");
		async->events |= COMEDI_CB_ERROR;
		nbytes = 0;
	} else {
		nbytes = desc->size - residue;
	}
	nsamples = comedi_bytes_to_samples(s, nbytes);

	/* restart DMA if more samples are needed */
	if (nsamples) {
		dma->cur_dma = 1 - dma->cur_dma;
		das16_ai_setup_dma(dev, s, nsamples);
	}

	spin_unlock_irqrestore(&dev->spinlock, spin_flags);

	comedi_buf_write_samples(s, desc->virt_addr, nsamples);

	if (cmd->stop_src == TRIG_COUNT && async->scans_done >= cmd->stop_arg)
		async->events |= COMEDI_CB_EOA;

	comedi_handle_events(dev, s);
}

static void das16_timer_interrupt(unsigned long arg)
{
	struct comedi_device *dev = (struct comedi_device *)arg;
	struct das16_private_struct *devpriv = dev->private;
	unsigned long flags;

	das16_interrupt(dev);

	spin_lock_irqsave(&dev->spinlock, flags);
	if (devpriv->timer_running)
		mod_timer(&devpriv->timer, jiffies + timer_period());
	spin_unlock_irqrestore(&dev->spinlock, flags);
}

static void das16_ai_set_mux_range(struct comedi_device *dev,
				   unsigned int first_chan,
				   unsigned int last_chan,
				   unsigned int range)
{
	const struct das16_board *board = dev->board_ptr;

	/* set multiplexer */
	outb(first_chan | (last_chan << 4), dev->iobase + DAS16_MUX_REG);

	/* some boards do not have programmable gain */
	if (board->ai_pg == das16_pg_none)
		return;

	/*
	 * Set gain (this is also burst rate register but according to
	 * computer boards manual, burst rate does nothing, even on
	 * keithley cards).
	 */
	outb((das16_gainlists[board->ai_pg])[range],
	     dev->iobase + DAS16_GAIN_REG);
}

static int das16_ai_check_chanlist(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_cmd *cmd)
{
	unsigned int chan0 = CR_CHAN(cmd->chanlist[0]);
	unsigned int range0 = CR_RANGE(cmd->chanlist[0]);
	int i;

	for (i = 1; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);
		unsigned int range = CR_RANGE(cmd->chanlist[i]);

		if (chan != ((chan0 + i) % s->n_chan)) {
			dev_dbg(dev->class_dev,
				"entries in chanlist must be consecutive channels, counting upwards\n");
			return -EINVAL;
		}

		if (range != range0) {
			dev_dbg(dev->class_dev,
				"entries in chanlist must all have the same gain\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int das16_cmd_test(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_cmd *cmd)
{
	const struct das16_board *board = dev->board_ptr;
	struct das16_private_struct *devpriv = dev->private;
	int err = 0;
	unsigned int trig_mask;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);

	trig_mask = TRIG_FOLLOW;
	if (devpriv->can_burst)
		trig_mask |= TRIG_TIMER | TRIG_EXT;
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, trig_mask);

	trig_mask = TRIG_TIMER | TRIG_EXT;
	if (devpriv->can_burst)
		trig_mask |= TRIG_NOW;
	err |= cfc_check_trigger_src(&cmd->convert_src, trig_mask);

	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	/*  make sure scan_begin_src and convert_src dont conflict */
	if (cmd->scan_begin_src == TRIG_FOLLOW && cmd->convert_src == TRIG_NOW)
		err |= -EINVAL;
	if (cmd->scan_begin_src != TRIG_FOLLOW && cmd->convert_src != TRIG_NOW)
		err |= -EINVAL;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_FOLLOW)	/* internal trigger */
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	/* check against maximum frequency */
	if (cmd->scan_begin_src == TRIG_TIMER)
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
					board->ai_speed * cmd->chanlist_len);

	if (cmd->convert_src == TRIG_TIMER)
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg,
						 board->ai_speed);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/*  step 4: fix up arguments */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		i8253_cascade_ns_to_timer(devpriv->clockbase,
					  &devpriv->divisor1,
					  &devpriv->divisor2,
					  &arg, cmd->flags);
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}
	if (cmd->convert_src == TRIG_TIMER) {
		arg = cmd->convert_arg;
		i8253_cascade_ns_to_timer(devpriv->clockbase,
					  &devpriv->divisor1,
					  &devpriv->divisor2,
					  &arg, cmd->flags);
		err |= cfc_check_trigger_arg_is(&cmd->convert_arg, arg);
	}
	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= das16_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static unsigned int das16_set_pacer(struct comedi_device *dev, unsigned int ns,
				    unsigned int flags)
{
	struct das16_private_struct *devpriv = dev->private;
	unsigned long timer_base = dev->iobase + DAS16_TIMER_BASE_REG;

	i8253_cascade_ns_to_timer(devpriv->clockbase,
				  &devpriv->divisor1, &devpriv->divisor2,
				  &ns, flags);

	i8254_set_mode(timer_base, 0, 1, I8254_MODE2 | I8254_BINARY);
	i8254_set_mode(timer_base, 0, 2, I8254_MODE2 | I8254_BINARY);
	i8254_write(timer_base, 0, 1, devpriv->divisor1);
	i8254_write(timer_base, 0, 2, devpriv->divisor2);

	return ns;
}

static int das16_cmd_exec(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct das16_private_struct *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int first_chan = CR_CHAN(cmd->chanlist[0]);
	unsigned int last_chan = CR_CHAN(cmd->chanlist[cmd->chanlist_len - 1]);
	unsigned int range = CR_RANGE(cmd->chanlist[0]);
	unsigned int byte;
	unsigned long flags;

	if (cmd->flags & CMDF_PRIORITY) {
		dev_err(dev->class_dev,
			 "isa dma transfers cannot be performed with CMDF_PRIORITY, aborting\n");
		return -1;
	}

	if (devpriv->can_burst)
		outb(DAS1600_CONV_DISABLE, dev->iobase + DAS1600_CONV_REG);

	/* set mux and range for chanlist scan */
	das16_ai_set_mux_range(dev, first_chan, last_chan, range);

	/* set counter mode and counts */
	cmd->convert_arg = das16_set_pacer(dev, cmd->convert_arg, cmd->flags);

	/* enable counters */
	byte = 0;
	if (devpriv->can_burst) {
		if (cmd->convert_src == TRIG_NOW) {
			outb(DAS1600_BURST_VAL,
			     dev->iobase + DAS1600_BURST_REG);
			/*  set burst length */
			byte |= DAS16_PACER_BURST_LEN(cmd->chanlist_len - 1);
		} else {
			outb(0, dev->iobase + DAS1600_BURST_REG);
		}
	}
	outb(byte, dev->iobase + DAS16_PACER_REG);

	/* set up dma transfer */
	dma->cur_dma = 0;
	das16_ai_setup_dma(dev, s, 0);

	/*  set up timer */
	spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->timer_running = 1;
	devpriv->timer.expires = jiffies + timer_period();
	add_timer(&devpriv->timer);

	/* enable DMA interrupt with external or internal pacing */
	devpriv->ctrl_reg &= ~(DAS16_CTRL_INTE | DAS16_CTRL_PACING_MASK);
	devpriv->ctrl_reg |= DAS16_CTRL_DMAE;
	if (cmd->convert_src == TRIG_EXT)
		devpriv->ctrl_reg |= DAS16_CTRL_EXT_PACER;
	else
		devpriv->ctrl_reg |= DAS16_CTRL_INT_PACER;
	outb(devpriv->ctrl_reg, dev->iobase + DAS16_CTRL_REG);

	if (devpriv->can_burst)
		outb(0, dev->iobase + DAS1600_CONV_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return 0;
}

static int das16_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct das16_private_struct *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	unsigned long flags;

	spin_lock_irqsave(&dev->spinlock, flags);

	/* disable interrupts, dma and pacer clocked conversions */
	devpriv->ctrl_reg &= ~(DAS16_CTRL_INTE | DAS16_CTRL_DMAE |
			       DAS16_CTRL_PACING_MASK);
	outb(devpriv->ctrl_reg, dev->iobase + DAS16_CTRL_REG);

	comedi_isadma_disable(dma->chan);

	/*  disable SW timer */
	if (devpriv->timer_running) {
		devpriv->timer_running = 0;
		del_timer(&devpriv->timer);
	}

	if (devpriv->can_burst)
		outb(0, dev->iobase + DAS1600_BURST_REG);

	spin_unlock_irqrestore(&dev->spinlock, flags);

	return 0;
}

static void das16_ai_munge(struct comedi_device *dev,
			   struct comedi_subdevice *s, void *array,
			   unsigned int num_bytes,
			   unsigned int start_chan_index)
{
	unsigned short *data = array;
	unsigned int num_samples = comedi_bytes_to_samples(s, num_bytes);
	unsigned int i;

	for (i = 0; i < num_samples; i++) {
		data[i] = le16_to_cpu(data[i]);
		if (s->maxdata == 0x0fff)
			data[i] >>= 4;
		data[i] &= s->maxdata;
	}
}

static int das16_ai_eoc(struct comedi_device *dev,
			struct comedi_subdevice *s,
			struct comedi_insn *insn,
			unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DAS16_STATUS_REG);
	if ((status & DAS16_STATUS_BUSY) == 0)
		return 0;
	return -EBUSY;
}

static int das16_ai_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val;
	int ret;
	int i;

	/* set mux and range for single channel */
	das16_ai_set_mux_range(dev, chan, chan, range);

	for (i = 0; i < insn->n; i++) {
		/* trigger conversion */
		outb_p(0, dev->iobase + DAS16_TRIG_REG);

		ret = comedi_timeout(dev, s, insn, das16_ai_eoc, 0);
		if (ret)
			return ret;

		val = inb(dev->iobase + DAS16_AI_MSB_REG) << 8;
		val |= inb(dev->iobase + DAS16_AI_LSB_REG);
		if (s->maxdata == 0x0fff)
			val >>= 4;
		val &= s->maxdata;

		data[i] = val;
	}

	return insn->n;
}

static int das16_ao_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];

		s->readback[chan] = val;

		val <<= 4;

		outb(val & 0xff, dev->iobase + DAS16_AO_LSB_REG(chan));
		outb((val >> 8) & 0xff, dev->iobase + DAS16_AO_MSB_REG(chan));
	}

	return insn->n;
}

static int das16_di_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	data[1] = inb(dev->iobase + DAS16_DIO_REG) & 0xf;

	return insn->n;
}

static int das16_do_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + DAS16_DIO_REG);

	data[1] = s->state;

	return insn->n;
}

static int das16_probe(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct das16_board *board = dev->board_ptr;
	int diobits;

	/* diobits indicates boards */
	diobits = inb(dev->iobase + DAS16_DIO_REG) & 0xf0;
	if (board->id != diobits) {
		dev_err(dev->class_dev,
			"requested board's id bits are incorrect (0x%x != 0x%x)\n",
			board->id, diobits);
		return -EINVAL;
	}

	return 0;
}

static void das16_reset(struct comedi_device *dev)
{
	outb(0, dev->iobase + DAS16_STATUS_REG);
	outb(0, dev->iobase + DAS16_CTRL_REG);
	outb(0, dev->iobase + DAS16_PACER_REG);
	outb(0, dev->iobase + DAS16_TIMER_BASE_REG + i8254_control_reg);
}

static void das16_alloc_dma(struct comedi_device *dev, unsigned int dma_chan)
{
	struct das16_private_struct *devpriv = dev->private;

	/* only DMA channels 3 and 1 are valid */
	if (!(dma_chan == 1 || dma_chan == 3))
		return;

	/* DMA uses two buffers */
	devpriv->dma = comedi_isadma_alloc(dev, 2, dma_chan, dma_chan,
					   DAS16_DMA_SIZE, COMEDI_ISADMA_READ);
	if (devpriv->dma) {
		init_timer(&devpriv->timer);
		devpriv->timer.function = das16_timer_interrupt;
		devpriv->timer.data = (unsigned long)dev;
	}
}

static void das16_free_dma(struct comedi_device *dev)
{
	struct das16_private_struct *devpriv = dev->private;

	if (devpriv) {
		if (devpriv->timer.data)
			del_timer_sync(&devpriv->timer);
		comedi_isadma_free(devpriv->dma);
	}
}

static const struct comedi_lrange *das16_ai_range(struct comedi_device *dev,
						  struct comedi_subdevice *s,
						  struct comedi_devconfig *it,
						  unsigned int pg_type,
						  unsigned int status)
{
	unsigned int min = it->options[4];
	unsigned int max = it->options[5];

	/* get any user-defined input range */
	if (pg_type == das16_pg_none && (min || max)) {
		struct comedi_lrange *lrange;
		struct comedi_krange *krange;

		/* allocate single-range range table */
		lrange = comedi_alloc_spriv(s,
					    sizeof(*lrange) + sizeof(*krange));
		if (!lrange)
			return &range_unknown;

		/* initialize ai range */
		lrange->length = 1;
		krange = lrange->range;
		krange->min = min;
		krange->max = max;
		krange->flags = UNIT_volt;

		return lrange;
	}

	/* use software programmable range */
	if (status & DAS16_STATUS_UNIPOLAR)
		return das16_ai_uni_lranges[pg_type];
	return das16_ai_bip_lranges[pg_type];
}

static const struct comedi_lrange *das16_ao_range(struct comedi_device *dev,
						  struct comedi_subdevice *s,
						  struct comedi_devconfig *it)
{
	unsigned int min = it->options[6];
	unsigned int max = it->options[7];

	/* get any user-defined output range */
	if (min || max) {
		struct comedi_lrange *lrange;
		struct comedi_krange *krange;

		/* allocate single-range range table */
		lrange = comedi_alloc_spriv(s,
					    sizeof(*lrange) + sizeof(*krange));
		if (!lrange)
			return &range_unknown;

		/* initialize ao range */
		lrange->length = 1;
		krange = lrange->range;
		krange->min = min;
		krange->max = max;
		krange->flags = UNIT_volt;

		return lrange;
	}

	return &range_unknown;
}

static int das16_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct das16_board *board = dev->board_ptr;
	struct das16_private_struct *devpriv;
	struct comedi_subdevice *s;
	unsigned int status;
	int ret;

	/*  check that clock setting is valid */
	if (it->options[3]) {
		if (it->options[3] != 0 &&
		    it->options[3] != 1 && it->options[3] != 10) {
			dev_err(dev->class_dev,
				"Invalid option. Master clock must be set to 1 or 10 (MHz)\n");
			return -EINVAL;
		}
	}

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	if (board->size < 0x400) {
		ret = comedi_request_region(dev, it->options[0], board->size);
		if (ret)
			return ret;
	} else {
		ret = comedi_request_region(dev, it->options[0], 0x10);
		if (ret)
			return ret;
		/* Request an additional region for the 8255 */
		ret = __comedi_request_region(dev, dev->iobase + 0x400,
					      board->size & 0x3ff);
		if (ret)
			return ret;
		devpriv->extra_iobase = dev->iobase + 0x400;
		devpriv->can_burst = 1;
	}

	/*  probe id bits to make sure they are consistent */
	if (das16_probe(dev, it))
		return -EINVAL;

	/*  get master clock speed */
	if (devpriv->can_burst) {
		status = inb(dev->iobase + DAS1600_STATUS_REG);

		if (status & DAS1600_STATUS_CLK_10MHZ)
			devpriv->clockbase = I8254_OSC_BASE_10MHZ;
		else
			devpriv->clockbase = I8254_OSC_BASE_1MHZ;
	} else {
		if (it->options[3])
			devpriv->clockbase = I8254_OSC_BASE_1MHZ /
					     it->options[3];
		else
			devpriv->clockbase = I8254_OSC_BASE_1MHZ;
	}

	das16_alloc_dma(dev, it->options[2]);

	ret = comedi_alloc_subdevices(dev, 4 + board->has_8255);
	if (ret)
		return ret;

	status = inb(dev->iobase + DAS16_STATUS_REG);

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE;
	if (status & DAS16_STATUS_MUXBIT) {
		s->subdev_flags	|= SDF_GROUND;
		s->n_chan	= 16;
	} else {
		s->subdev_flags	|= SDF_DIFF;
		s->n_chan	= 8;
	}
	s->len_chanlist	= s->n_chan;
	s->maxdata	= board->ai_maxdata;
	s->range_table	= das16_ai_range(dev, s, it, board->ai_pg, status);
	s->insn_read	= das16_ai_insn_read;
	if (devpriv->dma) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->do_cmdtest	= das16_cmd_test;
		s->do_cmd	= das16_cmd_exec;
		s->cancel	= das16_cancel;
		s->munge	= das16_ai_munge;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	if (board->has_ao) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= 2;
		s->maxdata	= 0x0fff;
		s->range_table	= das16_ao_range(dev, s, it);
		s->insn_write	= das16_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das16_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das16_do_insn_bits;

	/* initialize digital output lines */
	outb(s->state, dev->iobase + DAS16_DIO_REG);

	/* 8255 Digital I/O subdevice */
	if (board->has_8255) {
		s = &dev->subdevices[4];
		ret = subdev_8255_init(dev, s, NULL, board->i8255_offset);
		if (ret)
			return ret;
	}

	das16_reset(dev);
	/* set the interrupt level */
	devpriv->ctrl_reg = DAS16_CTRL_IRQ(dev->irq);
	outb(devpriv->ctrl_reg, dev->iobase + DAS16_CTRL_REG);

	if (devpriv->can_burst) {
		outb(DAS1600_ENABLE_VAL, dev->iobase + DAS1600_ENABLE_REG);
		outb(0, dev->iobase + DAS1600_CONV_REG);
		outb(0, dev->iobase + DAS1600_BURST_REG);
	}

	return 0;
}

static void das16_detach(struct comedi_device *dev)
{
	const struct das16_board *board = dev->board_ptr;
	struct das16_private_struct *devpriv = dev->private;

	if (devpriv) {
		if (dev->iobase)
			das16_reset(dev);
		das16_free_dma(dev);

		if (devpriv->extra_iobase)
			release_region(devpriv->extra_iobase,
				       board->size & 0x3ff);
	}

	comedi_legacy_detach(dev);
}

static struct comedi_driver das16_driver = {
	.driver_name	= "das16",
	.module		= THIS_MODULE,
	.attach		= das16_attach,
	.detach		= das16_detach,
	.board_name	= &das16_boards[0].name,
	.num_names	= ARRAY_SIZE(das16_boards),
	.offset		= sizeof(das16_boards[0]),
};
module_comedi_driver(das16_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for DAS16 compatible boards");
MODULE_LICENSE("GPL");
