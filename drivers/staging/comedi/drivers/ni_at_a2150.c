// SPDX-License-Identifier: GPL-2.0+
/*
 * Comedi driver for National Instruments AT-A2150 boards
 * Copyright (C) 2001, 2002 Frank Mori Hess <fmhess@users.sourceforge.net>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: ni_at_a2150
 * Description: National Instruments AT-A2150
 * Author: Frank Mori Hess
 * Status: works
 * Devices: [National Instruments] AT-A2150C (at_a2150c), AT-2150S (at_a2150s)
 *
 * Configuration options:
 *   [0] - I/O port base address
 *   [1] - IRQ (optional, required for timed conversions)
 *   [2] - DMA (optional, required for timed conversions)
 *
 * Yet another driver for obsolete hardware brought to you by Frank Hess.
 * Testing and debugging help provided by Dave Andruczyk.
 *
 * If you want to ac couple the board's inputs, use AREF_OTHER.
 *
 * The only difference in the boards is their master clock frequencies.
 *
 * References (from ftp://ftp.natinst.com/support/manuals):
 *   320360.pdf  AT-A2150 User Manual
 *
 * TODO:
 * - analog level triggering
 * - TRIG_WAKE_EOS
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>

#include "../comedidev.h"

#include "comedi_isadma.h"
#include "comedi_8254.h"

#define A2150_DMA_BUFFER_SIZE	0xff00	/*  size in bytes of dma buffer */

/* Registers and bits */
#define CONFIG_REG		0x0
#define   CHANNEL_BITS(x)	((x) & 0x7)
#define   CHANNEL_MASK		0x7
#define   CLOCK_SELECT_BITS(x)	(((x) & 0x3) << 3)
#define   CLOCK_DIVISOR_BITS(x)	(((x) & 0x3) << 5)
#define   CLOCK_MASK		(0xf << 3)
/* enable (don't internally ground) channels 0 and 1 */
#define   ENABLE0_BIT		0x80
/* enable (don't internally ground) channels 2 and 3 */
#define   ENABLE1_BIT		0x100
#define   AC0_BIT		0x200	/* ac couple channels 0,1 */
#define   AC1_BIT		0x400	/* ac couple channels 2,3 */
#define   APD_BIT		0x800	/* analog power down */
#define   DPD_BIT		0x1000	/* digital power down */
#define TRIGGER_REG		0x2	/* trigger config register */
#define   POST_TRIGGER_BITS	0x2
#define   DELAY_TRIGGER_BITS	0x3
#define   HW_TRIG_EN		0x10	/* enable hardware trigger */
#define FIFO_START_REG		0x6	/* software start aquistion trigger */
#define FIFO_RESET_REG		0x8	/* clears fifo + fifo flags */
#define FIFO_DATA_REG		0xa	/* read data */
#define DMA_TC_CLEAR_REG	0xe	/* clear dma terminal count interrupt */
#define STATUS_REG		0x12	/* read only */
#define   FNE_BIT		0x1	/* fifo not empty */
#define   OVFL_BIT		0x8	/* fifo overflow */
#define   EDAQ_BIT		0x10	/* end of acquisition interrupt */
#define   DCAL_BIT		0x20	/* offset calibration in progress */
#define   INTR_BIT		0x40	/* interrupt has occurred */
/* dma terminal count interrupt has occurred */
#define   DMA_TC_BIT		0x80
#define   ID_BITS(x)		(((x) >> 8) & 0x3)
#define IRQ_DMA_CNTRL_REG	0x12			/* write only */
#define   DMA_CHAN_BITS(x)	((x) & 0x7)		/* sets dma channel */
#define   DMA_EN_BIT		0x8			/* enables dma */
#define   IRQ_LVL_BITS(x)	(((x) & 0xf) << 4)	/* sets irq level */
#define   FIFO_INTR_EN_BIT	0x100	/* enable fifo interrupts */
#define   FIFO_INTR_FHF_BIT	0x200	/* interrupt fifo half full */
/* enable interrupt on dma terminal count */
#define   DMA_INTR_EN_BIT	0x800
#define   DMA_DEM_EN_BIT	0x1000	/* enables demand mode dma */
#define I8253_BASE_REG		0x14

struct a2150_board {
	const char *name;
	int clock[4];		/* master clock periods, in nanoseconds */
	int num_clocks;		/* number of available master clock speeds */
	int ai_speed;		/* maximum conversion rate in nanoseconds */
};

/* analog input range */
static const struct comedi_lrange range_a2150 = {
	1, {
		BIP_RANGE(2.828)
	}
};

/* enum must match board indices */
enum { a2150_c, a2150_s };
static const struct a2150_board a2150_boards[] = {
	{
	 .name = "at-a2150c",
	 .clock = {31250, 22676, 20833, 19531},
	 .num_clocks = 4,
	 .ai_speed = 19531,
	 },
	{
	 .name = "at-a2150s",
	 .clock = {62500, 50000, 41667, 0},
	 .num_clocks = 3,
	 .ai_speed = 41667,
	 },
};

struct a2150_private {
	struct comedi_isadma *dma;
	unsigned int count;	/* number of data points left to be taken */
	int irq_dma_bits;	/* irq/dma register bits */
	int config_bits;	/* config register bits */
};

/* interrupt service routine */
static irqreturn_t a2150_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct a2150_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[0];
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned short *buf = desc->virt_addr;
	unsigned int max_points, num_points, residue, leftover;
	unsigned short dpnt;
	int status;
	int i;

	if (!dev->attached)
		return IRQ_HANDLED;

	status = inw(dev->iobase + STATUS_REG);
	if ((status & INTR_BIT) == 0)
		return IRQ_NONE;

	if (status & OVFL_BIT) {
		async->events |= COMEDI_CB_ERROR;
		comedi_handle_events(dev, s);
	}

	if ((status & DMA_TC_BIT) == 0) {
		async->events |= COMEDI_CB_ERROR;
		comedi_handle_events(dev, s);
		return IRQ_HANDLED;
	}

	/*
	 * residue is the number of bytes left to be done on the dma
	 * transfer.  It should always be zero at this point unless
	 * the stop_src is set to external triggering.
	 */
	residue = comedi_isadma_disable(desc->chan);

	/* figure out how many points to read */
	max_points = comedi_bytes_to_samples(s, desc->size);
	num_points = max_points - comedi_bytes_to_samples(s, residue);
	if (devpriv->count < num_points && cmd->stop_src == TRIG_COUNT)
		num_points = devpriv->count;

	/* figure out how many points will be stored next time */
	leftover = 0;
	if (cmd->stop_src == TRIG_NONE) {
		leftover = comedi_bytes_to_samples(s, desc->size);
	} else if (devpriv->count > max_points) {
		leftover = devpriv->count - max_points;
		if (leftover > max_points)
			leftover = max_points;
	}
	/*
	 * There should only be a residue if collection was stopped by having
	 * the stop_src set to an external trigger, in which case there
	 * will be no more data
	 */
	if (residue)
		leftover = 0;

	for (i = 0; i < num_points; i++) {
		/* write data point to comedi buffer */
		dpnt = buf[i];
		/* convert from 2's complement to unsigned coding */
		dpnt ^= 0x8000;
		comedi_buf_write_samples(s, &dpnt, 1);
		if (cmd->stop_src == TRIG_COUNT) {
			if (--devpriv->count == 0) {	/* end of acquisition */
				async->events |= COMEDI_CB_EOA;
				break;
			}
		}
	}
	/* re-enable dma */
	if (leftover) {
		desc->size = comedi_samples_to_bytes(s, leftover);
		comedi_isadma_program(desc);
	}

	comedi_handle_events(dev, s);

	/* clear interrupt */
	outw(0x00, dev->iobase + DMA_TC_CLEAR_REG);

	return IRQ_HANDLED;
}

static int a2150_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct a2150_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[0];

	/* disable dma on card */
	devpriv->irq_dma_bits &= ~DMA_INTR_EN_BIT & ~DMA_EN_BIT;
	outw(devpriv->irq_dma_bits, dev->iobase + IRQ_DMA_CNTRL_REG);

	/* disable computer's dma */
	comedi_isadma_disable(desc->chan);

	/* clear fifo and reset triggering circuitry */
	outw(0, dev->iobase + FIFO_RESET_REG);

	return 0;
}

/*
 * sets bits in devpriv->clock_bits to nearest approximation of requested
 * period, adjusts requested period to actual timing.
 */
static int a2150_get_timing(struct comedi_device *dev, unsigned int *period,
			    unsigned int flags)
{
	const struct a2150_board *board = dev->board_ptr;
	struct a2150_private *devpriv = dev->private;
	int lub, glb, temp;
	int lub_divisor_shift, lub_index, glb_divisor_shift, glb_index;
	int i, j;

	/* initialize greatest lower and least upper bounds */
	lub_divisor_shift = 3;
	lub_index = 0;
	lub = board->clock[lub_index] * (1 << lub_divisor_shift);
	glb_divisor_shift = 0;
	glb_index = board->num_clocks - 1;
	glb = board->clock[glb_index] * (1 << glb_divisor_shift);

	/* make sure period is in available range */
	if (*period < glb)
		*period = glb;
	if (*period > lub)
		*period = lub;

	/* we can multiply period by 1, 2, 4, or 8, using (1 << i) */
	for (i = 0; i < 4; i++) {
		/* there are a maximum of 4 master clocks */
		for (j = 0; j < board->num_clocks; j++) {
			/* temp is the period in nanosec we are evaluating */
			temp = board->clock[j] * (1 << i);
			/* if it is the best match yet */
			if (temp < lub && temp >= *period) {
				lub_divisor_shift = i;
				lub_index = j;
				lub = temp;
			}
			if (temp > glb && temp <= *period) {
				glb_divisor_shift = i;
				glb_index = j;
				glb = temp;
			}
		}
	}
	switch (flags & CMDF_ROUND_MASK) {
	case CMDF_ROUND_NEAREST:
	default:
		/* if least upper bound is better approximation */
		if (lub - *period < *period - glb)
			*period = lub;
		else
			*period = glb;
		break;
	case CMDF_ROUND_UP:
		*period = lub;
		break;
	case CMDF_ROUND_DOWN:
		*period = glb;
		break;
	}

	/* set clock bits for config register appropriately */
	devpriv->config_bits &= ~CLOCK_MASK;
	if (*period == lub) {
		devpriv->config_bits |=
		    CLOCK_SELECT_BITS(lub_index) |
		    CLOCK_DIVISOR_BITS(lub_divisor_shift);
	} else {
		devpriv->config_bits |=
		    CLOCK_SELECT_BITS(glb_index) |
		    CLOCK_DIVISOR_BITS(glb_divisor_shift);
	}

	return 0;
}

static int a2150_set_chanlist(struct comedi_device *dev,
			      unsigned int start_channel,
			      unsigned int num_channels)
{
	struct a2150_private *devpriv = dev->private;

	if (start_channel + num_channels > 4)
		return -1;

	devpriv->config_bits &= ~CHANNEL_MASK;

	switch (num_channels) {
	case 1:
		devpriv->config_bits |= CHANNEL_BITS(0x4 | start_channel);
		break;
	case 2:
		if (start_channel == 0)
			devpriv->config_bits |= CHANNEL_BITS(0x2);
		else if (start_channel == 2)
			devpriv->config_bits |= CHANNEL_BITS(0x3);
		else
			return -1;
		break;
	case 4:
		devpriv->config_bits |= CHANNEL_BITS(0x1);
		break;
	default:
		return -1;
	}

	return 0;
}

static int a2150_ai_check_chanlist(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_cmd *cmd)
{
	unsigned int chan0 = CR_CHAN(cmd->chanlist[0]);
	unsigned int aref0 = CR_AREF(cmd->chanlist[0]);
	int i;

	if (cmd->chanlist_len == 2 && (chan0 == 1 || chan0 == 3)) {
		dev_dbg(dev->class_dev,
			"length 2 chanlist must be channels 0,1 or channels 2,3\n");
		return -EINVAL;
	}

	if (cmd->chanlist_len == 3) {
		dev_dbg(dev->class_dev,
			"chanlist must have 1,2 or 4 channels\n");
		return -EINVAL;
	}

	for (i = 1; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);
		unsigned int aref = CR_AREF(cmd->chanlist[i]);

		if (chan != (chan0 + i)) {
			dev_dbg(dev->class_dev,
				"entries in chanlist must be consecutive channels, counting upwards\n");
			return -EINVAL;
		}

		if (chan == 2)
			aref0 = aref;
		if (aref != aref0) {
			dev_dbg(dev->class_dev,
				"channels 0/1 and 2/3 must have the same analog reference\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int a2150_ai_cmdtest(struct comedi_device *dev,
			    struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	const struct a2150_board *board = dev->board_ptr;
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
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

	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		a2150_get_timing(dev, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= a2150_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int a2150_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct a2150_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[0];
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int old_config_bits = devpriv->config_bits;
	unsigned int trigger_bits;

	if (cmd->flags & CMDF_PRIORITY) {
		dev_err(dev->class_dev,
			"dma incompatible with hard real-time interrupt (CMDF_PRIORITY), aborting\n");
		return -1;
	}
	/* clear fifo and reset triggering circuitry */
	outw(0, dev->iobase + FIFO_RESET_REG);

	/* setup chanlist */
	if (a2150_set_chanlist(dev, CR_CHAN(cmd->chanlist[0]),
			       cmd->chanlist_len) < 0)
		return -1;

	/* setup ac/dc coupling */
	if (CR_AREF(cmd->chanlist[0]) == AREF_OTHER)
		devpriv->config_bits |= AC0_BIT;
	else
		devpriv->config_bits &= ~AC0_BIT;
	if (CR_AREF(cmd->chanlist[2]) == AREF_OTHER)
		devpriv->config_bits |= AC1_BIT;
	else
		devpriv->config_bits &= ~AC1_BIT;

	/* setup timing */
	a2150_get_timing(dev, &cmd->scan_begin_arg, cmd->flags);

	/* send timing, channel, config bits */
	outw(devpriv->config_bits, dev->iobase + CONFIG_REG);

	/* initialize number of samples remaining */
	devpriv->count = cmd->stop_arg * cmd->chanlist_len;

	comedi_isadma_disable(desc->chan);

	/* set size of transfer to fill in 1/3 second */
#define ONE_THIRD_SECOND 333333333
	desc->size = comedi_bytes_per_sample(s) * cmd->chanlist_len *
		    ONE_THIRD_SECOND / cmd->scan_begin_arg;
	if (desc->size > desc->maxsize)
		desc->size = desc->maxsize;
	if (desc->size < comedi_bytes_per_sample(s))
		desc->size = comedi_bytes_per_sample(s);
	desc->size -= desc->size % comedi_bytes_per_sample(s);

	comedi_isadma_program(desc);

	/*
	 * Clear dma interrupt before enabling it, to try and get rid of
	 * that one spurious interrupt that has been happening.
	 */
	outw(0x00, dev->iobase + DMA_TC_CLEAR_REG);

	/* enable dma on card */
	devpriv->irq_dma_bits |= DMA_INTR_EN_BIT | DMA_EN_BIT;
	outw(devpriv->irq_dma_bits, dev->iobase + IRQ_DMA_CNTRL_REG);

	/* may need to wait 72 sampling periods if timing was changed */
	comedi_8254_load(dev->pacer, 2, 72, I8254_MODE0 | I8254_BINARY);

	/* setup start triggering */
	trigger_bits = 0;
	/* decide if we need to wait 72 periods for valid data */
	if (cmd->start_src == TRIG_NOW &&
	    (old_config_bits & CLOCK_MASK) !=
	    (devpriv->config_bits & CLOCK_MASK)) {
		/* set trigger source to delay trigger */
		trigger_bits |= DELAY_TRIGGER_BITS;
	} else {
		/* otherwise no delay */
		trigger_bits |= POST_TRIGGER_BITS;
	}
	/* enable external hardware trigger */
	if (cmd->start_src == TRIG_EXT) {
		trigger_bits |= HW_TRIG_EN;
	} else if (cmd->start_src == TRIG_OTHER) {
		/*
		 * XXX add support for level/slope start trigger
		 * using TRIG_OTHER
		 */
		dev_err(dev->class_dev, "you shouldn't see this?\n");
	}
	/* send trigger config bits */
	outw(trigger_bits, dev->iobase + TRIGGER_REG);

	/* start acquisition for soft trigger */
	if (cmd->start_src == TRIG_NOW)
		outw(0, dev->iobase + FIFO_START_REG);

	return 0;
}

static int a2150_ai_eoc(struct comedi_device *dev,
			struct comedi_subdevice *s,
			struct comedi_insn *insn,
			unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + STATUS_REG);
	if (status & FNE_BIT)
		return 0;
	return -EBUSY;
}

static int a2150_ai_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	struct a2150_private *devpriv = dev->private;
	unsigned int n;
	int ret;

	/* clear fifo and reset triggering circuitry */
	outw(0, dev->iobase + FIFO_RESET_REG);

	/* setup chanlist */
	if (a2150_set_chanlist(dev, CR_CHAN(insn->chanspec), 1) < 0)
		return -1;

	/* set dc coupling */
	devpriv->config_bits &= ~AC0_BIT;
	devpriv->config_bits &= ~AC1_BIT;

	/* send timing, channel, config bits */
	outw(devpriv->config_bits, dev->iobase + CONFIG_REG);

	/* disable dma on card */
	devpriv->irq_dma_bits &= ~DMA_INTR_EN_BIT & ~DMA_EN_BIT;
	outw(devpriv->irq_dma_bits, dev->iobase + IRQ_DMA_CNTRL_REG);

	/* setup start triggering */
	outw(0, dev->iobase + TRIGGER_REG);

	/* start acquisition for soft trigger */
	outw(0, dev->iobase + FIFO_START_REG);

	/*
	 * there is a 35.6 sample delay for data to get through the
	 * antialias filter
	 */
	for (n = 0; n < 36; n++) {
		ret = comedi_timeout(dev, s, insn, a2150_ai_eoc, 0);
		if (ret)
			return ret;

		inw(dev->iobase + FIFO_DATA_REG);
	}

	/* read data */
	for (n = 0; n < insn->n; n++) {
		ret = comedi_timeout(dev, s, insn, a2150_ai_eoc, 0);
		if (ret)
			return ret;

		data[n] = inw(dev->iobase + FIFO_DATA_REG);
		data[n] ^= 0x8000;
	}

	/* clear fifo and reset triggering circuitry */
	outw(0, dev->iobase + FIFO_RESET_REG);

	return n;
}

static void a2150_alloc_irq_and_dma(struct comedi_device *dev,
				    struct comedi_devconfig *it)
{
	struct a2150_private *devpriv = dev->private;
	unsigned int irq_num = it->options[1];
	unsigned int dma_chan = it->options[2];

	/*
	 * Only IRQs 15, 14, 12-9, and 7-3 are valid.
	 * Only DMA channels 7-5 and 3-0 are valid.
	 */
	if (irq_num > 15 || dma_chan > 7 ||
	    !((1 << irq_num) & 0xdef8) || !((1 << dma_chan) & 0xef))
		return;

	if (request_irq(irq_num, a2150_interrupt, 0, dev->board_name, dev))
		return;

	/* DMA uses 1 buffer */
	devpriv->dma = comedi_isadma_alloc(dev, 1, dma_chan, dma_chan,
					   A2150_DMA_BUFFER_SIZE,
					   COMEDI_ISADMA_READ);
	if (!devpriv->dma) {
		free_irq(irq_num, dev);
	} else {
		dev->irq = irq_num;
		devpriv->irq_dma_bits = IRQ_LVL_BITS(irq_num) |
					DMA_CHAN_BITS(dma_chan);
	}
}

static void a2150_free_dma(struct comedi_device *dev)
{
	struct a2150_private *devpriv = dev->private;

	if (devpriv)
		comedi_isadma_free(devpriv->dma);
}

static const struct a2150_board *a2150_probe(struct comedi_device *dev)
{
	int id = ID_BITS(inw(dev->iobase + STATUS_REG));

	if (id >= ARRAY_SIZE(a2150_boards))
		return NULL;

	return &a2150_boards[id];
}

static int a2150_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct a2150_board *board;
	struct a2150_private *devpriv;
	struct comedi_subdevice *s;
	static const int timeout = 2000;
	int i;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x1c);
	if (ret)
		return ret;

	board = a2150_probe(dev);
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	/* an IRQ and DMA are required to support async commands */
	a2150_alloc_irq_and_dma(dev, it);

	dev->pacer = comedi_8254_init(dev->iobase + I8253_BASE_REG,
				      0, I8254_IO8, 0);
	if (!dev->pacer)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	/* analog input subdevice */
	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_OTHER;
	s->n_chan = 4;
	s->maxdata = 0xffff;
	s->range_table = &range_a2150;
	s->insn_read = a2150_ai_rinsn;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags |= SDF_CMD_READ;
		s->len_chanlist = s->n_chan;
		s->do_cmd = a2150_ai_cmd;
		s->do_cmdtest = a2150_ai_cmdtest;
		s->cancel = a2150_cancel;
	}

	/* set card's irq and dma levels */
	outw(devpriv->irq_dma_bits, dev->iobase + IRQ_DMA_CNTRL_REG);

	/* reset and sync adc clock circuitry */
	outw_p(DPD_BIT | APD_BIT, dev->iobase + CONFIG_REG);
	outw_p(DPD_BIT, dev->iobase + CONFIG_REG);
	/* initialize configuration register */
	devpriv->config_bits = 0;
	outw(devpriv->config_bits, dev->iobase + CONFIG_REG);
	/* wait until offset calibration is done, then enable analog inputs */
	for (i = 0; i < timeout; i++) {
		if ((DCAL_BIT & inw(dev->iobase + STATUS_REG)) == 0)
			break;
		usleep_range(1000, 3000);
	}
	if (i == timeout) {
		dev_err(dev->class_dev,
			"timed out waiting for offset calibration to complete\n");
		return -ETIME;
	}
	devpriv->config_bits |= ENABLE0_BIT | ENABLE1_BIT;
	outw(devpriv->config_bits, dev->iobase + CONFIG_REG);

	return 0;
};

static void a2150_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		outw(APD_BIT | DPD_BIT, dev->iobase + CONFIG_REG);
	a2150_free_dma(dev);
	comedi_legacy_detach(dev);
};

static struct comedi_driver ni_at_a2150_driver = {
	.driver_name	= "ni_at_a2150",
	.module		= THIS_MODULE,
	.attach		= a2150_attach,
	.detach		= a2150_detach,
};
module_comedi_driver(ni_at_a2150_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
