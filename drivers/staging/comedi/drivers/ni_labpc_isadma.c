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

#include "ni_labpc.h"
#include "ni_labpc_regs.h"
#include "ni_labpc_isadma.h"

/* size in bytes of dma buffer */
static const int dma_buffer_size = 0xff00;

int labpc_init_dma_chan(struct comedi_device *dev, unsigned int dma_chan)
{
	struct labpc_private *devpriv = dev->private;
	void *dma_buffer;
	unsigned long dma_flags;
	int ret;

	if (dma_chan != 1 && dma_chan != 3)
		return -EINVAL;

	dma_buffer = kmalloc(dma_buffer_size, GFP_KERNEL | GFP_DMA);
	if (!dma_buffer)
		return -ENOMEM;

	ret = request_dma(dma_chan, dev->board_name);
	if (ret) {
		kfree(dma_buffer);
		return ret;
	}

	devpriv->dma_buffer = dma_buffer;
	devpriv->dma_chan = dma_chan;
	devpriv->dma_addr = virt_to_bus(devpriv->dma_buffer);

	dma_flags = claim_dma_lock();
	disable_dma(devpriv->dma_chan);
	set_dma_mode(devpriv->dma_chan, DMA_MODE_READ);
	release_dma_lock(dma_flags);

	return 0;
}
EXPORT_SYMBOL_GPL(labpc_init_dma_chan);

void labpc_free_dma_chan(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;

	kfree(devpriv->dma_buffer);
	devpriv->dma_buffer = NULL;
	if (devpriv->dma_chan) {
		free_dma(devpriv->dma_chan);
		devpriv->dma_chan = 0;
	}
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
