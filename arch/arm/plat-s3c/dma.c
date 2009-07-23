/* linux/arch/arm/plat-s3c/dma.c
 *
 * Copyright (c) 2003-2005,2006,2009 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C DMA core
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

struct s3c2410_dma_buf;

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>

#include <mach/dma.h>
#include <mach/irqs.h>

#include <plat/dma-plat.h>

/* dma channel state information */
struct s3c2410_dma_chan s3c2410_chans[S3C_DMA_CHANNELS];
struct s3c2410_dma_chan *s3c_dma_chan_map[DMACH_MAX];

/* s3c_dma_lookup_channel
 *
 * change the dma channel number given into a real dma channel id
*/

struct s3c2410_dma_chan *s3c_dma_lookup_channel(unsigned int channel)
{
	if (channel & DMACH_LOW_LEVEL)
		return &s3c2410_chans[channel & ~DMACH_LOW_LEVEL];
	else
		return s3c_dma_chan_map[channel];
}

/* do we need to protect the settings of the fields from
 * irq?
*/

int s3c2410_dma_set_opfn(unsigned int channel, s3c2410_dma_opfn_t rtn)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);

	if (chan == NULL)
		return -EINVAL;

	pr_debug("%s: chan=%p, op rtn=%p\n", __func__, chan, rtn);

	chan->op_fn = rtn;

	return 0;
}
EXPORT_SYMBOL(s3c2410_dma_set_opfn);

int s3c2410_dma_set_buffdone_fn(unsigned int channel, s3c2410_dma_cbfn_t rtn)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);

	if (chan == NULL)
		return -EINVAL;

	pr_debug("%s: chan=%p, callback rtn=%p\n", __func__, chan, rtn);

	chan->callback_fn = rtn;

	return 0;
}
EXPORT_SYMBOL(s3c2410_dma_set_buffdone_fn);

int s3c2410_dma_setflags(unsigned int channel, unsigned int flags)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);

	if (chan == NULL)
		return -EINVAL;

	chan->flags = flags;
	return 0;
}
EXPORT_SYMBOL(s3c2410_dma_setflags);
