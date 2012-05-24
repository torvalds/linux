/*
 * Renesas SuperH DMA Engine support
 *
 * Copyright (C) 2009 Nobuhiro Iwamatsu <iwamatsu.nobuhiro@renesas.com>
 * Copyright (C) 2009 Renesas Solutions, Inc. All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#ifndef __DMA_SHDMA_H
#define __DMA_SHDMA_H

#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/list.h>

#define SH_DMAC_MAX_CHANNELS 20
#define SH_DMA_SLAVE_NUMBER 256
#define SH_DMA_TCR_MAX 0x00FFFFFF	/* 16MB */

struct device;

enum dmae_pm_state {
	DMAE_PM_ESTABLISHED,
	DMAE_PM_BUSY,
	DMAE_PM_PENDING,
};

struct sh_dmae_chan {
	spinlock_t desc_lock;		/* Descriptor operation lock */
	struct list_head ld_queue;	/* Link descriptors queue */
	struct list_head ld_free;	/* Link descriptors free */
	struct dma_chan common;		/* DMA common channel */
	struct device *dev;		/* Channel device */
	struct tasklet_struct tasklet;	/* Tasklet */
	int descs_allocated;		/* desc count */
	int xmit_shift;			/* log_2(bytes_per_xfer) */
	int irq;
	int id;				/* Raw id of this channel */
	u32 __iomem *base;
	char dev_id[16];		/* unique name per DMAC of channel */
	int pm_error;
	enum dmae_pm_state pm_state;
};

struct sh_dmae_device {
	struct dma_device common;
	struct sh_dmae_chan *chan[SH_DMAC_MAX_CHANNELS];
	struct sh_dmae_pdata *pdata;
	struct list_head node;
	u32 __iomem *chan_reg;
	u16 __iomem *dmars;
	unsigned int chcr_offset;
	u32 chcr_ie_bit;
};

#define to_sh_chan(chan) container_of(chan, struct sh_dmae_chan, common)
#define to_sh_desc(lh) container_of(lh, struct sh_desc, node)
#define tx_to_sh_desc(tx) container_of(tx, struct sh_desc, async_tx)
#define to_sh_dev(chan) container_of(chan->common.device,\
				     struct sh_dmae_device, common)

#endif	/* __DMA_SHDMA_H */
