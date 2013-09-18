/*
    comedi/drivers/ni_at_a2150.c
    Driver for National Instruments AT-A2150 boards
    Copyright (C) 2001, 2002 Frank Mori Hess <fmhess@users.sourceforge.net>

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
Driver: ni_at_a2150
Description: National Instruments AT-A2150
Author: Frank Mori Hess
Status: works
Devices: [National Instruments] AT-A2150C (at_a2150c), AT-2150S (at_a2150s)

If you want to ac couple the board's inputs, use AREF_OTHER.

Configuration options:
  [0] - I/O port base address
  [1] - IRQ (optional, required for timed conversions)
  [2] - DMA (optional, required for timed conversions)

*/
/*
Yet another driver for obsolete hardware brought to you by Frank Hess.
Testing and debugging help provided by Dave Andruczyk.

This driver supports the boards:

AT-A2150C
AT-A2150S

The only difference is their master clock frequencies.

Options:
	[0] - base io address
	[1] - irq
	[2] - dma channel

References (from ftp://ftp.natinst.com/support/manuals):

	   320360.pdf  AT-A2150 User Manual

TODO:

analog level triggering
TRIG_WAKE_EOS

*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include "../comedidev.h"

#include <linux/ioport.h>
#include <linux/io.h>
#include <asm/dma.h>

#include "8253.h"
#include "comedi_fc.h"

#define A2150_SIZE           28
#define A2150_DMA_BUFFER_SIZE	0xff00	/*  size in bytes of dma buffer */

/* #define A2150_DEBUG     enable debugging code */
#undef A2150_DEBUG		/*  disable debugging code */

/* Registers and bits */
#define CONFIG_REG		0x0
#define   CHANNEL_BITS(x)		((x) & 0x7)
#define   CHANNEL_MASK		0x7
#define   CLOCK_SELECT_BITS(x)		(((x) & 0x3) << 3)
#define   CLOCK_DIVISOR_BITS(x)		(((x) & 0x3) << 5)
#define   CLOCK_MASK		(0xf << 3)
#define   ENABLE0_BIT		0x80	/*  enable (don't internally ground) channels 0 and 1 */
#define   ENABLE1_BIT		0x100	/*  enable (don't internally ground) channels 2 and 3 */
#define   AC0_BIT		0x200	/*  ac couple channels 0,1 */
#define   AC1_BIT		0x400	/*  ac couple channels 2,3 */
#define   APD_BIT		0x800	/*  analog power down */
#define   DPD_BIT		0x1000	/*  digital power down */
#define TRIGGER_REG		0x2	/*  trigger config register */
#define   POST_TRIGGER_BITS		0x2
#define   DELAY_TRIGGER_BITS		0x3
#define   HW_TRIG_EN		0x10	/*  enable hardware trigger */
#define FIFO_START_REG		0x6	/*  software start aquistion trigger */
#define FIFO_RESET_REG		0x8	/*  clears fifo + fifo flags */
#define FIFO_DATA_REG		0xa	/*  read data */
#define DMA_TC_CLEAR_REG		0xe	/*  clear dma terminal count interrupt */
#define STATUS_REG		0x12	/*  read only */
#define   FNE_BIT		0x1	/*  fifo not empty */
#define   OVFL_BIT		0x8	/*  fifo overflow */
#define   EDAQ_BIT		0x10	/*  end of acquisition interrupt */
#define   DCAL_BIT		0x20	/*  offset calibration in progress */
#define   INTR_BIT		0x40	/*  interrupt has occurred */
#define   DMA_TC_BIT		0x80	/*  dma terminal count interrupt has occurred */
#define   ID_BITS(x)	(((x) >> 8) & 0x3)
#define IRQ_DMA_CNTRL_REG		0x12	/*  write only */
#define   DMA_CHAN_BITS(x)		((x) & 0x7)	/*  sets dma channel */
#define   DMA_EN_BIT		0x8	/*  enables dma */
#define   IRQ_LVL_BITS(x)		(((x) & 0xf) << 4)	/*  sets irq level */
#define   FIFO_INTR_EN_BIT		0x100	/*  enable fifo interrupts */
#define   FIFO_INTR_FHF_BIT		0x200	/*  interrupt fifo half full */
#define   DMA_INTR_EN_BIT 		0x800	/*  enable interrupt on dma terminal count */
#define   DMA_DEM_EN_BIT	0x1000	/*  enables demand mode dma */
#define I8253_BASE_REG		0x14
#define I8253_MODE_REG		0x17
#define   HW_COUNT_DISABLE		0x30	/*  disable hardware counting of conversions */

struct a2150_board {
	const char *name;
	int clock[4];		/*  master clock periods, in nanoseconds */
	int num_clocks;		/*  number of available master clock speeds */
	int ai_speed;		/*  maximum conversion rate in nanoseconds */
};

/* analog input range */
static const struct comedi_lrange range_a2150 = {
	1,
	{
	 RANGE(-2.828, 2.828),
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

	volatile unsigned int count;	/* number of data points left to be taken */
	unsigned int dma;	/*  dma channel */
	s16 *dma_buffer;	/*  dma buffer */
	unsigned int dma_transfer_size;	/*  size in bytes of dma transfers */
	int irq_dma_bits;	/*  irq/dma register bits */
	int config_bits;	/*  config register bits */
};

static int a2150_cancel(struct comedi_device *dev, struct comedi_subdevice *s);

static int a2150_get_timing(struct comedi_device *dev, unsigned int *period,
			    int flags);
static int a2150_set_chanlist(struct comedi_device *dev,
			      unsigned int start_channel,
			      unsigned int num_channels);
#ifdef A2150_DEBUG

static void ni_dump_regs(struct comedi_device *dev)
{
	struct a2150_private *devpriv = dev->private;

	printk("config bits 0x%x\n", devpriv->config_bits);
	printk("irq dma bits 0x%x\n", devpriv->irq_dma_bits);
	printk("status bits 0x%x\n", inw(dev->iobase + STATUS_REG));
}

#endif

/* interrupt service routine */
static irqreturn_t a2150_interrupt(int irq, void *d)
{
	int i;
	int status;
	unsigned long flags;
	struct comedi_device *dev = d;
	struct a2150_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async;
	struct comedi_cmd *cmd;
	unsigned int max_points, num_points, residue, leftover;
	short dpnt;
	static const int sample_size = sizeof(devpriv->dma_buffer[0]);

	if (!dev->attached) {
		comedi_error(dev, "premature interrupt");
		return IRQ_HANDLED;
	}
	/*  initialize async here to make sure s is not NULL */
	async = s->async;
	async->events = 0;
	cmd = &async->cmd;

	status = inw(dev->iobase + STATUS_REG);

	if ((status & INTR_BIT) == 0) {
		comedi_error(dev, "spurious interrupt");
		return IRQ_NONE;
	}

	if (status & OVFL_BIT) {
		comedi_error(dev, "fifo overflow");
		a2150_cancel(dev, s);
		async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
	}

	if ((status & DMA_TC_BIT) == 0) {
		comedi_error(dev, "caught non-dma interrupt?  Aborting.");
		a2150_cancel(dev, s);
		async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		comedi_event(dev, s);
		return IRQ_HANDLED;
	}

	flags = claim_dma_lock();
	disable_dma(devpriv->dma);
	/* clear flip-flop to make sure 2-byte registers for
	 * count and address get set correctly */
	clear_dma_ff(devpriv->dma);

	/*  figure out how many points to read */
	max_points = devpriv->dma_transfer_size / sample_size;
	/* residue is the number of points left to be done on the dma
	 * transfer.  It should always be zero at this point unless
	 * the stop_src is set to external triggering.
	 */
	residue = get_dma_residue(devpriv->dma) / sample_size;
	num_points = max_points - residue;
	if (devpriv->count < num_points && cmd->stop_src == TRIG_COUNT)
		num_points = devpriv->count;

	/*  figure out how many points will be stored next time */
	leftover = 0;
	if (cmd->stop_src == TRIG_NONE) {
		leftover = devpriv->dma_transfer_size / sample_size;
	} else if (devpriv->count > max_points) {
		leftover = devpriv->count - max_points;
		if (leftover > max_points)
			leftover = max_points;
	}
	/* there should only be a residue if collection was stopped by having
	 * the stop_src set to an external trigger, in which case there
	 * will be no more data
	 */
	if (residue)
		leftover = 0;

	for (i = 0; i < num_points; i++) {
		/* write data point to comedi buffer */
		dpnt = devpriv->dma_buffer[i];
		/*  convert from 2's complement to unsigned coding */
		dpnt ^= 0x8000;
		cfc_write_to_buffer(s, dpnt);
		if (cmd->stop_src == TRIG_COUNT) {
			if (--devpriv->count == 0) {	/* end of acquisition */
				a2150_cancel(dev, s);
				async->events |= COMEDI_CB_EOA;
				break;
			}
		}
	}
	/*  re-enable  dma */
	if (leftover) {
		set_dma_addr(devpriv->dma, virt_to_bus(devpriv->dma_buffer));
		set_dma_count(devpriv->dma, leftover * sample_size);
		enable_dma(devpriv->dma);
	}
	release_dma_lock(flags);

	async->events |= COMEDI_CB_BLOCK;

	comedi_event(dev, s);

	/* clear interrupt */
	outw(0x00, dev->iobase + DMA_TC_CLEAR_REG);

	return IRQ_HANDLED;
}

static int a2150_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct a2150_private *devpriv = dev->private;

	/*  disable dma on card */
	devpriv->irq_dma_bits &= ~DMA_INTR_EN_BIT & ~DMA_EN_BIT;
	outw(devpriv->irq_dma_bits, dev->iobase + IRQ_DMA_CNTRL_REG);

	/*  disable computer's dma */
	disable_dma(devpriv->dma);

	/*  clear fifo and reset triggering circuitry */
	outw(0, dev->iobase + FIFO_RESET_REG);

	return 0;
}

static int a2150_ai_cmdtest(struct comedi_device *dev,
			    struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	const struct a2150_board *thisboard = comedi_board(dev);
	int err = 0;
	int tmp;
	int startChan;
	int i;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_TIMER);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
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

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		a2150_get_timing(dev, &cmd->scan_begin_arg, cmd->flags);
		if (tmp != cmd->scan_begin_arg)
			err++;
	}

	if (err)
		return 4;

	/*  check channel/gain list against card's limitations */
	if (cmd->chanlist) {
		startChan = CR_CHAN(cmd->chanlist[0]);
		for (i = 1; i < cmd->chanlist_len; i++) {
			if (CR_CHAN(cmd->chanlist[i]) != (startChan + i)) {
				comedi_error(dev,
					     "entries in chanlist must be consecutive channels, counting upwards\n");
				err++;
			}
		}
		if (cmd->chanlist_len == 2 && CR_CHAN(cmd->chanlist[0]) == 1) {
			comedi_error(dev,
				     "length 2 chanlist must be channels 0,1 or channels 2,3");
			err++;
		}
		if (cmd->chanlist_len == 3) {
			comedi_error(dev,
				     "chanlist must have 1,2 or 4 channels");
			err++;
		}
		if (CR_AREF(cmd->chanlist[0]) != CR_AREF(cmd->chanlist[1]) ||
		    CR_AREF(cmd->chanlist[2]) != CR_AREF(cmd->chanlist[3])) {
			comedi_error(dev,
				     "channels 0/1 and 2/3 must have the same analog reference");
			err++;
		}
	}

	if (err)
		return 5;

	return 0;
}

static int a2150_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct a2150_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned long lock_flags;
	unsigned int old_config_bits = devpriv->config_bits;
	unsigned int trigger_bits;

	if (!dev->irq || !devpriv->dma) {
		comedi_error(dev,
			     " irq and dma required, cannot do hardware conversions");
		return -1;
	}
	if (cmd->flags & TRIG_RT) {
		comedi_error(dev,
			     " dma incompatible with hard real-time interrupt (TRIG_RT), aborting");
		return -1;
	}
	/*  clear fifo and reset triggering circuitry */
	outw(0, dev->iobase + FIFO_RESET_REG);

	/* setup chanlist */
	if (a2150_set_chanlist(dev, CR_CHAN(cmd->chanlist[0]),
			       cmd->chanlist_len) < 0)
		return -1;

	/*  setup ac/dc coupling */
	if (CR_AREF(cmd->chanlist[0]) == AREF_OTHER)
		devpriv->config_bits |= AC0_BIT;
	else
		devpriv->config_bits &= ~AC0_BIT;
	if (CR_AREF(cmd->chanlist[2]) == AREF_OTHER)
		devpriv->config_bits |= AC1_BIT;
	else
		devpriv->config_bits &= ~AC1_BIT;

	/*  setup timing */
	a2150_get_timing(dev, &cmd->scan_begin_arg, cmd->flags);

	/*  send timing, channel, config bits */
	outw(devpriv->config_bits, dev->iobase + CONFIG_REG);

	/*  initialize number of samples remaining */
	devpriv->count = cmd->stop_arg * cmd->chanlist_len;

	/*  enable computer's dma */
	lock_flags = claim_dma_lock();
	disable_dma(devpriv->dma);
	/* clear flip-flop to make sure 2-byte registers for
	 * count and address get set correctly */
	clear_dma_ff(devpriv->dma);
	set_dma_addr(devpriv->dma, virt_to_bus(devpriv->dma_buffer));
	/*  set size of transfer to fill in 1/3 second */
#define ONE_THIRD_SECOND 333333333
	devpriv->dma_transfer_size =
	    sizeof(devpriv->dma_buffer[0]) * cmd->chanlist_len *
	    ONE_THIRD_SECOND / cmd->scan_begin_arg;
	if (devpriv->dma_transfer_size > A2150_DMA_BUFFER_SIZE)
		devpriv->dma_transfer_size = A2150_DMA_BUFFER_SIZE;
	if (devpriv->dma_transfer_size < sizeof(devpriv->dma_buffer[0]))
		devpriv->dma_transfer_size = sizeof(devpriv->dma_buffer[0]);
	devpriv->dma_transfer_size -=
	    devpriv->dma_transfer_size % sizeof(devpriv->dma_buffer[0]);
	set_dma_count(devpriv->dma, devpriv->dma_transfer_size);
	enable_dma(devpriv->dma);
	release_dma_lock(lock_flags);

	/* clear dma interrupt before enabling it, to try and get rid of that
	 * one spurious interrupt that has been happening */
	outw(0x00, dev->iobase + DMA_TC_CLEAR_REG);

	/*  enable dma on card */
	devpriv->irq_dma_bits |= DMA_INTR_EN_BIT | DMA_EN_BIT;
	outw(devpriv->irq_dma_bits, dev->iobase + IRQ_DMA_CNTRL_REG);

	/*  may need to wait 72 sampling periods if timing was changed */
	i8254_load(dev->iobase + I8253_BASE_REG, 0, 2, 72, 0);

	/*  setup start triggering */
	trigger_bits = 0;
	/*  decide if we need to wait 72 periods for valid data */
	if (cmd->start_src == TRIG_NOW &&
	    (old_config_bits & CLOCK_MASK) !=
	    (devpriv->config_bits & CLOCK_MASK)) {
		/*  set trigger source to delay trigger */
		trigger_bits |= DELAY_TRIGGER_BITS;
	} else {
		/*  otherwise no delay */
		trigger_bits |= POST_TRIGGER_BITS;
	}
	/*  enable external hardware trigger */
	if (cmd->start_src == TRIG_EXT) {
		trigger_bits |= HW_TRIG_EN;
	} else if (cmd->start_src == TRIG_OTHER) {
		/*  XXX add support for level/slope start trigger using TRIG_OTHER */
		comedi_error(dev, "you shouldn't see this?");
	}
	/*  send trigger config bits */
	outw(trigger_bits, dev->iobase + TRIGGER_REG);

	/*  start acquisition for soft trigger */
	if (cmd->start_src == TRIG_NOW)
		outw(0, dev->iobase + FIFO_START_REG);
#ifdef A2150_DEBUG
	ni_dump_regs(dev);
#endif

	return 0;
}

static int a2150_ai_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	struct a2150_private *devpriv = dev->private;
	unsigned int i, n;
	static const int timeout = 100000;
	static const int filter_delay = 36;

	/*  clear fifo and reset triggering circuitry */
	outw(0, dev->iobase + FIFO_RESET_REG);

	/* setup chanlist */
	if (a2150_set_chanlist(dev, CR_CHAN(insn->chanspec), 1) < 0)
		return -1;

	/*  set dc coupling */
	devpriv->config_bits &= ~AC0_BIT;
	devpriv->config_bits &= ~AC1_BIT;

	/*  send timing, channel, config bits */
	outw(devpriv->config_bits, dev->iobase + CONFIG_REG);

	/*  disable dma on card */
	devpriv->irq_dma_bits &= ~DMA_INTR_EN_BIT & ~DMA_EN_BIT;
	outw(devpriv->irq_dma_bits, dev->iobase + IRQ_DMA_CNTRL_REG);

	/*  setup start triggering */
	outw(0, dev->iobase + TRIGGER_REG);

	/*  start acquisition for soft trigger */
	outw(0, dev->iobase + FIFO_START_REG);

	/*
	 * there is a 35.6 sample delay for data to get through the
	 * antialias filter
	 */
	for (n = 0; n < filter_delay; n++) {
		for (i = 0; i < timeout; i++) {
			if (inw(dev->iobase + STATUS_REG) & FNE_BIT)
				break;
			udelay(1);
		}
		if (i == timeout) {
			comedi_error(dev, "timeout");
			return -ETIME;
		}
		inw(dev->iobase + FIFO_DATA_REG);
	}

	/*  read data */
	for (n = 0; n < insn->n; n++) {
		for (i = 0; i < timeout; i++) {
			if (inw(dev->iobase + STATUS_REG) & FNE_BIT)
				break;
			udelay(1);
		}
		if (i == timeout) {
			comedi_error(dev, "timeout");
			return -ETIME;
		}
#ifdef A2150_DEBUG
		ni_dump_regs(dev);
#endif
		data[n] = inw(dev->iobase + FIFO_DATA_REG);
#ifdef A2150_DEBUG
		printk(" data is %i\n", data[n]);
#endif
		data[n] ^= 0x8000;
	}

	/*  clear fifo and reset triggering circuitry */
	outw(0, dev->iobase + FIFO_RESET_REG);

	return n;
}

/*
 * sets bits in devpriv->clock_bits to nearest approximation of requested
 * period, adjusts requested period to actual timing.
 */
static int a2150_get_timing(struct comedi_device *dev, unsigned int *period,
			    int flags)
{
	const struct a2150_board *thisboard = comedi_board(dev);
	struct a2150_private *devpriv = dev->private;
	int lub, glb, temp;
	int lub_divisor_shift, lub_index, glb_divisor_shift, glb_index;
	int i, j;

	/*  initialize greatest lower and least upper bounds */
	lub_divisor_shift = 3;
	lub_index = 0;
	lub = thisboard->clock[lub_index] * (1 << lub_divisor_shift);
	glb_divisor_shift = 0;
	glb_index = thisboard->num_clocks - 1;
	glb = thisboard->clock[glb_index] * (1 << glb_divisor_shift);

	/*  make sure period is in available range */
	if (*period < glb)
		*period = glb;
	if (*period > lub)
		*period = lub;

	/*  we can multiply period by 1, 2, 4, or 8, using (1 << i) */
	for (i = 0; i < 4; i++) {
		/*  there are a maximum of 4 master clocks */
		for (j = 0; j < thisboard->num_clocks; j++) {
			/*  temp is the period in nanosec we are evaluating */
			temp = thisboard->clock[j] * (1 << i);
			/*  if it is the best match yet */
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
	flags &= TRIG_ROUND_MASK;
	switch (flags) {
	case TRIG_ROUND_NEAREST:
	default:
		/*  if least upper bound is better approximation */
		if (lub - *period < *period - glb)
			*period = lub;
		else
			*period = glb;
		break;
	case TRIG_ROUND_UP:
		*period = lub;
		break;
	case TRIG_ROUND_DOWN:
		*period = glb;
		break;
	}

	/*  set clock bits for config register appropriately */
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
		if (start_channel == 0) {
			devpriv->config_bits |= CHANNEL_BITS(0x2);
		} else if (start_channel == 2) {
			devpriv->config_bits |= CHANNEL_BITS(0x3);
		} else {
			return -1;
		}
		break;
	case 4:
		devpriv->config_bits |= CHANNEL_BITS(0x1);
		break;
	default:
		return -1;
		break;
	}

	return 0;
}

/* probes board type, returns offset */
static int a2150_probe(struct comedi_device *dev)
{
	int status = inw(dev->iobase + STATUS_REG);
	return ID_BITS(status);
}

static int a2150_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct a2150_board *thisboard = comedi_board(dev);
	struct a2150_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int irq = it->options[1];
	unsigned int dma = it->options[2];
	static const int timeout = 2000;
	int i;
	int ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_request_region(dev, it->options[0], A2150_SIZE);
	if (ret)
		return ret;

	/* grab our IRQ */
	if (irq) {
		/*  check that irq is supported */
		if (irq < 3 || irq == 8 || irq == 13 || irq > 15) {
			printk(" invalid irq line %u\n", irq);
			return -EINVAL;
		}
		if (request_irq(irq, a2150_interrupt, 0,
				dev->driver->driver_name, dev)) {
			printk("unable to allocate irq %u\n", irq);
			return -EINVAL;
		}
		devpriv->irq_dma_bits |= IRQ_LVL_BITS(irq);
		dev->irq = irq;
	}
	/*  initialize dma */
	if (dma) {
		if (dma == 4 || dma > 7) {
			printk(" invalid dma channel %u\n", dma);
			return -EINVAL;
		}
		if (request_dma(dma, dev->driver->driver_name)) {
			printk(" failed to allocate dma channel %u\n", dma);
			return -EINVAL;
		}
		devpriv->dma = dma;
		devpriv->dma_buffer =
		    kmalloc(A2150_DMA_BUFFER_SIZE, GFP_KERNEL | GFP_DMA);
		if (devpriv->dma_buffer == NULL)
			return -ENOMEM;

		disable_dma(dma);
		set_dma_mode(dma, DMA_MODE_READ);

		devpriv->irq_dma_bits |= DMA_CHAN_BITS(dma);
	}

	dev->board_ptr = a2150_boards + a2150_probe(dev);
	thisboard = comedi_board(dev);
	dev->board_name = thisboard->name;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	/* analog input subdevice */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_OTHER | SDF_CMD_READ;
	s->n_chan = 4;
	s->len_chanlist = 4;
	s->maxdata = 0xffff;
	s->range_table = &range_a2150;
	s->do_cmd = a2150_ai_cmd;
	s->do_cmdtest = a2150_ai_cmdtest;
	s->insn_read = a2150_ai_rinsn;
	s->cancel = a2150_cancel;

	/* need to do this for software counting of completed conversions, to
	 * prevent hardware count from stopping acquisition */
	outw(HW_COUNT_DISABLE, dev->iobase + I8253_MODE_REG);

	/*  set card's irq and dma levels */
	outw(devpriv->irq_dma_bits, dev->iobase + IRQ_DMA_CNTRL_REG);

	/*  reset and sync adc clock circuitry */
	outw_p(DPD_BIT | APD_BIT, dev->iobase + CONFIG_REG);
	outw_p(DPD_BIT, dev->iobase + CONFIG_REG);
	/*  initialize configuration register */
	devpriv->config_bits = 0;
	outw(devpriv->config_bits, dev->iobase + CONFIG_REG);
	/*  wait until offset calibration is done, then enable analog inputs */
	for (i = 0; i < timeout; i++) {
		if ((DCAL_BIT & inw(dev->iobase + STATUS_REG)) == 0)
			break;
		udelay(1000);
	}
	if (i == timeout) {
		printk
		    (" timed out waiting for offset calibration to complete\n");
		return -ETIME;
	}
	devpriv->config_bits |= ENABLE0_BIT | ENABLE1_BIT;
	outw(devpriv->config_bits, dev->iobase + CONFIG_REG);

	return 0;
};

static void a2150_detach(struct comedi_device *dev)
{
	struct a2150_private *devpriv = dev->private;

	if (dev->iobase)
		outw(APD_BIT | DPD_BIT, dev->iobase + CONFIG_REG);
	if (devpriv) {
		if (devpriv->dma)
			free_dma(devpriv->dma);
		kfree(devpriv->dma_buffer);
	}
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
