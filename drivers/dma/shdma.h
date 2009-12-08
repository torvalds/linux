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

#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/dmaengine.h>

#define SH_DMA_TCR_MAX 0x00FFFFFF	/* 16MB */

struct sh_dmae_regs {
	u32 sar; /* SAR / source address */
	u32 dar; /* DAR / destination address */
	u32 tcr; /* TCR / transfer count */
};

struct sh_desc {
	struct list_head tx_list;
	struct sh_dmae_regs hw;
	struct list_head node;
	struct dma_async_tx_descriptor async_tx;
	int mark;
};

struct sh_dmae_chan {
	dma_cookie_t completed_cookie;	/* The maximum cookie completed */
	spinlock_t desc_lock;			/* Descriptor operation lock */
	struct list_head ld_queue;		/* Link descriptors queue */
	struct list_head ld_free;		/* Link descriptors free */
	struct dma_chan common;			/* DMA common channel */
	struct device *dev;				/* Channel device */
	struct tasklet_struct tasklet;	/* Tasklet */
	int descs_allocated;			/* desc count */
	int id;				/* Raw id of this channel */
	char dev_id[16];	/* unique name per DMAC of channel */

	/* Set chcr */
	int (*set_chcr)(struct sh_dmae_chan *sh_chan, u32 regs);
	/* Set DMA resource */
	int (*set_dmars)(struct sh_dmae_chan *sh_chan, u16 res);
};

struct sh_dmae_device {
	struct dma_device common;
	struct sh_dmae_chan *chan[MAX_DMA_CHANNELS];
	struct sh_dmae_pdata pdata;
};

#define to_sh_chan(chan) container_of(chan, struct sh_dmae_chan, common)
#define to_sh_desc(lh) container_of(lh, struct sh_desc, node)
#define tx_to_sh_desc(tx) container_of(tx, struct sh_desc, async_tx)

#endif	/* __DMA_SHDMA_H */
