/*
 * comedi/drivers/ni_labpc_isadma.c
 * ISA DMA support for National Instruments Lab-PC series boards and
 * compatibles.
 *
 * Extracted from ni_labpc.c:
 * Copyright (C) 2001-2003 Frank Mori Hess <fmhess@users.sourceforge.net>
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

#include <linux/module.h>
#include <linux/slab.h>
#include "../comedidev.h"

#include <asm/dma.h>

#include "comedi_fc.h"
#include "ni_labpc.h"
#include "ni_labpc_regs.h"
#include "ni_labpc_isadma.h"

/* size in bytes of dma buffer */
#define LABPC_ISADMA_BUFFER_SIZE	0xff00

static unsigned int labpc_isadma_disable(struct labpc_dma_desc *dma)
{
	unsigned long flags;
	unsigned int residue;

	flags = claim_dma_lock();
	disable_dma(dma->chan);
	residue = get_dma_residue(dma->chan);
	release_dma_lock(flags);

	return residue;
}

/* utility function that suggests a dma transfer size in bytes */
static unsigned int labpc_suggest_transfer_size(struct comedi_device *dev,
						struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int sample_size = comedi_bytes_per_sample(s);
	unsigned int size;
	unsigned int freq;

	if (cmd->convert_src == TRIG_TIMER)
		freq = 1000000000 / cmd->convert_arg;
	else
		/* return some default value */
		freq = 0xffffffff;

	/* make buffer fill in no more than 1/3 second */
	size = (freq / 3) * sample_size;

	/* set a minimum and maximum size allowed */
	if (size > LABPC_ISADMA_BUFFER_SIZE)
		size = LABPC_ISADMA_BUFFER_SIZE;
	else if (size < sample_size)
		size = sample_size;

	return size;
}

void labpc_setup_dma(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct labpc_private *devpriv = dev->private;
	struct labpc_dma_desc *dma = &devpriv->dma_desc;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int sample_size = comedi_bytes_per_sample(s);
	unsigned long irq_flags;

	irq_flags = claim_dma_lock();
	/* clear flip-flop to make sure 2-byte registers for
	 * count and address get set correctly */
	clear_dma_ff(dma->chan);
	set_dma_mode(dma->chan, DMA_MODE_READ);
	set_dma_addr(dma->chan, dma->hw_addr);
	/* set appropriate size of transfer */
	dma->size = labpc_suggest_transfer_size(dev, s);
	if (cmd->stop_src == TRIG_COUNT &&
	    devpriv->count * sample_size < dma->size)
		dma->size = devpriv->count * sample_size;
	set_dma_count(dma->chan, dma->size);
	enable_dma(dma->chan);
	release_dma_lock(irq_flags);
	/* set CMD3 bits for caller to enable DMA and interrupt */
	devpriv->cmd3 |= (CMD3_DMAEN | CMD3_DMATCINTEN);
}
EXPORT_SYMBOL_GPL(labpc_setup_dma);

void labpc_drain_dma(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	struct labpc_dma_desc *dma = &devpriv->dma_desc;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int sample_size = comedi_bytes_per_sample(s);
	int status;
	unsigned long flags;
	unsigned int max_points, num_points, residue, leftover;

	status = devpriv->stat1;

	/*
	 * residue is the number of bytes left to be done on the dma
	 * transfer.  It should always be zero at this point unless
	 * the stop_src is set to external triggering.
	 */
	residue = labpc_isadma_disable(dma);

	/* figure out how many points to read */
	max_points = dma->size / sample_size;
	num_points = max_points - comedi_bytes_to_samples(s, residue);
	if (cmd->stop_src == TRIG_COUNT && devpriv->count < num_points)
		num_points = devpriv->count;

	/* figure out how many points will be stored next time */
	leftover = 0;
	if (cmd->stop_src != TRIG_COUNT) {
		leftover = dma->size / sample_size;
	} else if (devpriv->count > num_points) {
		leftover = devpriv->count - num_points;
		if (leftover > max_points)
			leftover = max_points;
	}

	comedi_buf_write_samples(s, dma->virt_addr, num_points);

	if (cmd->stop_src == TRIG_COUNT)
		devpriv->count -= num_points;

	/* set address and count for next transfer */
	flags = claim_dma_lock();
	set_dma_mode(dma->chan, DMA_MODE_READ);
	set_dma_addr(dma->chan, dma->hw_addr);
	set_dma_count(dma->chan, leftover * sample_size);
	release_dma_lock(flags);
}
EXPORT_SYMBOL_GPL(labpc_drain_dma);

static void handle_isa_dma(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	struct labpc_dma_desc *dma = &devpriv->dma_desc;

	labpc_drain_dma(dev);

	enable_dma(dma->chan);

	/* clear dma tc interrupt */
	devpriv->write_byte(dev, 0x1, DMATC_CLEAR_REG);
}

void labpc_handle_dma_status(struct comedi_device *dev)
{
	const struct labpc_boardinfo *board = dev->board_ptr;
	struct labpc_private *devpriv = dev->private;

	/*
	 * if a dma terminal count of external stop trigger
	 * has occurred
	 */
	if (devpriv->stat1 & STAT1_GATA0 ||
	    (board->is_labpc1200 && devpriv->stat2 & STAT2_OUTA1))
		handle_isa_dma(dev);
}
EXPORT_SYMBOL_GPL(labpc_handle_dma_status);

void labpc_init_dma_chan(struct comedi_device *dev, unsigned int dma_chan)
{
	struct labpc_private *devpriv = dev->private;
	struct labpc_dma_desc *dma = &devpriv->dma_desc;

	if (dma_chan != 1 && dma_chan != 3)
		return;

	if (request_dma(dma_chan, dev->board_name))
		return;

	dma->virt_addr = dma_alloc_coherent(NULL, LABPC_ISADMA_BUFFER_SIZE,
					    &dma->hw_addr, GFP_KERNEL);
	if (!dma->virt_addr) {
		free_dma(dma_chan);
		return;
	}

	dma->chan = dma_chan;

	labpc_isadma_disable(dma);
}
EXPORT_SYMBOL_GPL(labpc_init_dma_chan);

void labpc_free_dma_chan(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	struct labpc_dma_desc *dma = &devpriv->dma_desc;

	if (dma->virt_addr)
		dma_free_coherent(NULL, LABPC_ISADMA_BUFFER_SIZE,
				  dma->virt_addr, dma->hw_addr);
	if (dma->chan)
		free_dma(dma->chan);
}
EXPORT_SYMBOL_GPL(labpc_free_dma_chan);

static int __init ni_labpc_isadma_init_module(void)
{
	return 0;
}
module_init(ni_labpc_isadma_init_module);

static void __exit ni_labpc_isadma_cleanup_module(void)
{
}
module_exit(ni_labpc_isadma_cleanup_module);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi NI Lab-PC ISA DMA support");
MODULE_LICENSE("GPL");
