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

#include <linux/sh_dma.h>
#include <linux/shdma-base.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/list.h>

#define SH_DMAE_MAX_CHANNELS 20
#define SH_DMAE_TCR_MAX 0x00FFFFFF	/* 16MB */

struct device;

struct sh_dmae_chan {
	struct shdma_chan shdma_chan;
	const struct sh_dmae_slave_config *config; /* Slave DMA configuration */
	int xmit_shift;			/* log_2(bytes_per_xfer) */
	void __iomem *base;
	char dev_id[16];		/* unique name per DMAC of channel */
	int pm_error;
	dma_addr_t slave_addr;
};

struct sh_dmae_device {
	struct shdma_dev shdma_dev;
	struct sh_dmae_chan *chan[SH_DMAE_MAX_CHANNELS];
	const struct sh_dmae_pdata *pdata;
	struct list_head node;
	void __iomem *chan_reg;
	void __iomem *dmars;
	unsigned int chcr_offset;
	u32 chcr_ie_bit;
};

struct sh_dmae_regs {
	u32 sar; /* SAR / source address */
	u32 dar; /* DAR / destination address */
	u32 tcr; /* TCR / transfer count */
};

struct sh_dmae_desc {
	struct sh_dmae_regs hw;
	struct shdma_desc shdma_desc;
};

#define to_sh_chan(chan) container_of(chan, struct sh_dmae_chan, shdma_chan)
#define to_sh_desc(lh) container_of(lh, struct sh_desc, node)
#define tx_to_sh_desc(tx) container_of(tx, struct sh_desc, async_tx)
#define to_sh_dev(chan) container_of(chan->shdma_chan.dma_chan.device,\
				     struct sh_dmae_device, shdma_dev.dma_dev)

#ifdef CONFIG_SH_DMAE_R8A73A4
extern const struct sh_dmae_pdata r8a73a4_dma_pdata;
#define r8a73a4_shdma_devid (&r8a73a4_dma_pdata)
#else
#define r8a73a4_shdma_devid NULL
#endif

#endif	/* __DMA_SHDMA_H */
