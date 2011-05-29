/*
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_MXS_DMA_H__
#define __MACH_MXS_DMA_H__

struct mxs_dma_data {
	int chan_irq;
};

static inline int mxs_dma_is_apbh(struct dma_chan *chan)
{
	return !strcmp(dev_name(chan->device->dev), "mxs-dma-apbh");
}

static inline int mxs_dma_is_apbx(struct dma_chan *chan)
{
	return !strcmp(dev_name(chan->device->dev), "mxs-dma-apbx");
}

#endif /* __MACH_MXS_DMA_H__ */
