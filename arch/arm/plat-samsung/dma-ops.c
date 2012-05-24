/* linux/arch/arm/plat-samsung/dma-ops.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung DMA Operations
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/amba/pl330.h>
#include <linux/scatterlist.h>
#include <linux/export.h>

#include <mach/dma.h>

static unsigned samsung_dmadev_request(enum dma_ch dma_ch,
				struct samsung_dma_info *info)
{
	struct dma_chan *chan;
	dma_cap_mask_t mask;
	struct dma_slave_config slave_config;
	void *filter_param;

	dma_cap_zero(mask);
	dma_cap_set(info->cap, mask);

	/*
	 * If a dma channel property of a device node from device tree is
	 * specified, use that as the fliter parameter.
	 */
	filter_param = (dma_ch == DMACH_DT_PROP) ? (void *)info->dt_dmach_prop :
				(void *)dma_ch;
	chan = dma_request_channel(mask, pl330_filter, filter_param);

	if (info->direction == DMA_DEV_TO_MEM) {
		memset(&slave_config, 0, sizeof(struct dma_slave_config));
		slave_config.direction = info->direction;
		slave_config.src_addr = info->fifo;
		slave_config.src_addr_width = info->width;
		slave_config.src_maxburst = 1;
		dmaengine_slave_config(chan, &slave_config);
	} else if (info->direction == DMA_MEM_TO_DEV) {
		memset(&slave_config, 0, sizeof(struct dma_slave_config));
		slave_config.direction = info->direction;
		slave_config.dst_addr = info->fifo;
		slave_config.dst_addr_width = info->width;
		slave_config.dst_maxburst = 1;
		dmaengine_slave_config(chan, &slave_config);
	}

	return (unsigned)chan;
}

static int samsung_dmadev_release(unsigned ch,
			struct s3c2410_dma_client *client)
{
	dma_release_channel((struct dma_chan *)ch);

	return 0;
}

static int samsung_dmadev_prepare(unsigned ch,
			struct samsung_dma_prep_info *info)
{
	struct scatterlist sg;
	struct dma_chan *chan = (struct dma_chan *)ch;
	struct dma_async_tx_descriptor *desc;

	switch (info->cap) {
	case DMA_SLAVE:
		sg_init_table(&sg, 1);
		sg_dma_len(&sg) = info->len;
		sg_set_page(&sg, pfn_to_page(PFN_DOWN(info->buf)),
			    info->len, offset_in_page(info->buf));
		sg_dma_address(&sg) = info->buf;

		desc = dmaengine_prep_slave_sg(chan,
			&sg, 1, info->direction, DMA_PREP_INTERRUPT);
		break;
	case DMA_CYCLIC:
		desc = dmaengine_prep_dma_cyclic(chan,
			info->buf, info->len, info->period, info->direction);
		break;
	default:
		dev_err(&chan->dev->device, "unsupported format\n");
		return -EFAULT;
	}

	if (!desc) {
		dev_err(&chan->dev->device, "cannot prepare cyclic dma\n");
		return -EFAULT;
	}

	desc->callback = info->fp;
	desc->callback_param = info->fp_param;

	dmaengine_submit((struct dma_async_tx_descriptor *)desc);

	return 0;
}

static inline int samsung_dmadev_trigger(unsigned ch)
{
	dma_async_issue_pending((struct dma_chan *)ch);

	return 0;
}

static inline int samsung_dmadev_flush(unsigned ch)
{
	return dmaengine_terminate_all((struct dma_chan *)ch);
}

static struct samsung_dma_ops dmadev_ops = {
	.request	= samsung_dmadev_request,
	.release	= samsung_dmadev_release,
	.prepare	= samsung_dmadev_prepare,
	.trigger	= samsung_dmadev_trigger,
	.started	= NULL,
	.flush		= samsung_dmadev_flush,
	.stop		= samsung_dmadev_flush,
};

void *samsung_dmadev_get_ops(void)
{
	return &dmadev_ops;
}
EXPORT_SYMBOL(samsung_dmadev_get_ops);
