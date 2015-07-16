/*
 * linux/drivers/mmc/tmio_mmc_dma.c
 *
 * Copyright (C) 2010-2011 Guennadi Liakhovetski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * DMA function for TMIO MMC implementations
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/tmio.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>

#include "tmio_mmc.h"

#define TMIO_MMC_MIN_DMA_LEN 8

void tmio_mmc_enable_dma(struct tmio_mmc_host *host, bool enable)
{
	if (!host->chan_tx || !host->chan_rx)
		return;

	if (host->dma->enable)
		host->dma->enable(host, enable);
}

void tmio_mmc_abort_dma(struct tmio_mmc_host *host)
{
	tmio_mmc_enable_dma(host, false);

	if (host->chan_rx)
		dmaengine_terminate_all(host->chan_rx);
	if (host->chan_tx)
		dmaengine_terminate_all(host->chan_tx);

	tmio_mmc_enable_dma(host, true);
}

static void tmio_mmc_start_dma_rx(struct tmio_mmc_host *host)
{
	struct scatterlist *sg = host->sg_ptr, *sg_tmp;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan = host->chan_rx;
	dma_cookie_t cookie;
	int ret, i;
	bool aligned = true, multiple = true;
	unsigned int align = (1 << host->pdata->alignment_shift) - 1;

	for_each_sg(sg, sg_tmp, host->sg_len, i) {
		if (sg_tmp->offset & align)
			aligned = false;
		if (sg_tmp->length & align) {
			multiple = false;
			break;
		}
	}

	if ((!aligned && (host->sg_len > 1 || sg->length > PAGE_CACHE_SIZE ||
			  (align & PAGE_MASK))) || !multiple) {
		ret = -EINVAL;
		goto pio;
	}

	if (sg->length < TMIO_MMC_MIN_DMA_LEN) {
		host->force_pio = true;
		return;
	}

	tmio_mmc_disable_mmc_irqs(host, TMIO_STAT_RXRDY);

	/* The only sg element can be unaligned, use our bounce buffer then */
	if (!aligned) {
		sg_init_one(&host->bounce_sg, host->bounce_buf, sg->length);
		host->sg_ptr = &host->bounce_sg;
		sg = host->sg_ptr;
	}

	ret = dma_map_sg(chan->device->dev, sg, host->sg_len, DMA_FROM_DEVICE);
	if (ret > 0)
		desc = dmaengine_prep_slave_sg(chan, sg, ret,
			DMA_DEV_TO_MEM, DMA_CTRL_ACK);

	if (desc) {
		cookie = dmaengine_submit(desc);
		if (cookie < 0) {
			desc = NULL;
			ret = cookie;
		}
	}
	dev_dbg(&host->pdev->dev, "%s(): mapped %d -> %d, cookie %d, rq %p\n",
		__func__, host->sg_len, ret, cookie, host->mrq);

pio:
	if (!desc) {
		/* DMA failed, fall back to PIO */
		tmio_mmc_enable_dma(host, false);
		if (ret >= 0)
			ret = -EIO;
		host->chan_rx = NULL;
		dma_release_channel(chan);
		/* Free the Tx channel too */
		chan = host->chan_tx;
		if (chan) {
			host->chan_tx = NULL;
			dma_release_channel(chan);
		}
		dev_warn(&host->pdev->dev,
			 "DMA failed: %d, falling back to PIO\n", ret);
	}

	dev_dbg(&host->pdev->dev, "%s(): desc %p, cookie %d, sg[%d]\n", __func__,
		desc, cookie, host->sg_len);
}

static void tmio_mmc_start_dma_tx(struct tmio_mmc_host *host)
{
	struct scatterlist *sg = host->sg_ptr, *sg_tmp;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan = host->chan_tx;
	dma_cookie_t cookie;
	int ret, i;
	bool aligned = true, multiple = true;
	unsigned int align = (1 << host->pdata->alignment_shift) - 1;

	for_each_sg(sg, sg_tmp, host->sg_len, i) {
		if (sg_tmp->offset & align)
			aligned = false;
		if (sg_tmp->length & align) {
			multiple = false;
			break;
		}
	}

	if ((!aligned && (host->sg_len > 1 || sg->length > PAGE_CACHE_SIZE ||
			  (align & PAGE_MASK))) || !multiple) {
		ret = -EINVAL;
		goto pio;
	}

	if (sg->length < TMIO_MMC_MIN_DMA_LEN) {
		host->force_pio = true;
		return;
	}

	tmio_mmc_disable_mmc_irqs(host, TMIO_STAT_TXRQ);

	/* The only sg element can be unaligned, use our bounce buffer then */
	if (!aligned) {
		unsigned long flags;
		void *sg_vaddr = tmio_mmc_kmap_atomic(sg, &flags);
		sg_init_one(&host->bounce_sg, host->bounce_buf, sg->length);
		memcpy(host->bounce_buf, sg_vaddr, host->bounce_sg.length);
		tmio_mmc_kunmap_atomic(sg, &flags, sg_vaddr);
		host->sg_ptr = &host->bounce_sg;
		sg = host->sg_ptr;
	}

	ret = dma_map_sg(chan->device->dev, sg, host->sg_len, DMA_TO_DEVICE);
	if (ret > 0)
		desc = dmaengine_prep_slave_sg(chan, sg, ret,
			DMA_MEM_TO_DEV, DMA_CTRL_ACK);

	if (desc) {
		cookie = dmaengine_submit(desc);
		if (cookie < 0) {
			desc = NULL;
			ret = cookie;
		}
	}
	dev_dbg(&host->pdev->dev, "%s(): mapped %d -> %d, cookie %d, rq %p\n",
		__func__, host->sg_len, ret, cookie, host->mrq);

pio:
	if (!desc) {
		/* DMA failed, fall back to PIO */
		tmio_mmc_enable_dma(host, false);
		if (ret >= 0)
			ret = -EIO;
		host->chan_tx = NULL;
		dma_release_channel(chan);
		/* Free the Rx channel too */
		chan = host->chan_rx;
		if (chan) {
			host->chan_rx = NULL;
			dma_release_channel(chan);
		}
		dev_warn(&host->pdev->dev,
			 "DMA failed: %d, falling back to PIO\n", ret);
	}

	dev_dbg(&host->pdev->dev, "%s(): desc %p, cookie %d\n", __func__,
		desc, cookie);
}

void tmio_mmc_start_dma(struct tmio_mmc_host *host,
			       struct mmc_data *data)
{
	if (data->flags & MMC_DATA_READ) {
		if (host->chan_rx)
			tmio_mmc_start_dma_rx(host);
	} else {
		if (host->chan_tx)
			tmio_mmc_start_dma_tx(host);
	}
}

static void tmio_mmc_issue_tasklet_fn(unsigned long priv)
{
	struct tmio_mmc_host *host = (struct tmio_mmc_host *)priv;
	struct dma_chan *chan = NULL;

	spin_lock_irq(&host->lock);

	if (host && host->data) {
		if (host->data->flags & MMC_DATA_READ)
			chan = host->chan_rx;
		else
			chan = host->chan_tx;
	}

	spin_unlock_irq(&host->lock);

	tmio_mmc_enable_mmc_irqs(host, TMIO_STAT_DATAEND);

	if (chan)
		dma_async_issue_pending(chan);
}

static void tmio_mmc_tasklet_fn(unsigned long arg)
{
	struct tmio_mmc_host *host = (struct tmio_mmc_host *)arg;

	spin_lock_irq(&host->lock);

	if (!host->data)
		goto out;

	if (host->data->flags & MMC_DATA_READ)
		dma_unmap_sg(host->chan_rx->device->dev,
			     host->sg_ptr, host->sg_len,
			     DMA_FROM_DEVICE);
	else
		dma_unmap_sg(host->chan_tx->device->dev,
			     host->sg_ptr, host->sg_len,
			     DMA_TO_DEVICE);

	tmio_mmc_do_data_irq(host);
out:
	spin_unlock_irq(&host->lock);
}

void tmio_mmc_request_dma(struct tmio_mmc_host *host, struct tmio_mmc_data *pdata)
{
	/* We can only either use DMA for both Tx and Rx or not use it at all */
	if (!host->dma || (!host->pdev->dev.of_node &&
		(!pdata->chan_priv_tx || !pdata->chan_priv_rx)))
		return;

	if (!host->chan_tx && !host->chan_rx) {
		struct resource *res = platform_get_resource(host->pdev,
							     IORESOURCE_MEM, 0);
		struct dma_slave_config cfg = {};
		dma_cap_mask_t mask;
		int ret;

		if (!res)
			return;

		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		host->chan_tx = dma_request_slave_channel_compat(mask,
					host->dma->filter, pdata->chan_priv_tx,
					&host->pdev->dev, "tx");
		dev_dbg(&host->pdev->dev, "%s: TX: got channel %p\n", __func__,
			host->chan_tx);

		if (!host->chan_tx)
			return;

		cfg.direction = DMA_MEM_TO_DEV;
		cfg.dst_addr = res->start + (CTL_SD_DATA_PORT << host->bus_shift);
		cfg.dst_addr_width = host->dma->dma_buswidth;
		if (!cfg.dst_addr_width)
			cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		cfg.src_addr = 0;
		ret = dmaengine_slave_config(host->chan_tx, &cfg);
		if (ret < 0)
			goto ecfgtx;

		host->chan_rx = dma_request_slave_channel_compat(mask,
					host->dma->filter, pdata->chan_priv_rx,
					&host->pdev->dev, "rx");
		dev_dbg(&host->pdev->dev, "%s: RX: got channel %p\n", __func__,
			host->chan_rx);

		if (!host->chan_rx)
			goto ereqrx;

		cfg.direction = DMA_DEV_TO_MEM;
		cfg.src_addr = cfg.dst_addr + host->pdata->dma_rx_offset;
		cfg.src_addr_width = host->dma->dma_buswidth;
		if (!cfg.src_addr_width)
			cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		cfg.dst_addr = 0;
		ret = dmaengine_slave_config(host->chan_rx, &cfg);
		if (ret < 0)
			goto ecfgrx;

		host->bounce_buf = (u8 *)__get_free_page(GFP_KERNEL | GFP_DMA);
		if (!host->bounce_buf)
			goto ebouncebuf;

		tasklet_init(&host->dma_complete, tmio_mmc_tasklet_fn, (unsigned long)host);
		tasklet_init(&host->dma_issue, tmio_mmc_issue_tasklet_fn, (unsigned long)host);
	}

	tmio_mmc_enable_dma(host, true);

	return;

ebouncebuf:
ecfgrx:
	dma_release_channel(host->chan_rx);
	host->chan_rx = NULL;
ereqrx:
ecfgtx:
	dma_release_channel(host->chan_tx);
	host->chan_tx = NULL;
}

void tmio_mmc_release_dma(struct tmio_mmc_host *host)
{
	if (host->chan_tx) {
		struct dma_chan *chan = host->chan_tx;
		host->chan_tx = NULL;
		dma_release_channel(chan);
	}
	if (host->chan_rx) {
		struct dma_chan *chan = host->chan_rx;
		host->chan_rx = NULL;
		dma_release_channel(chan);
	}
	if (host->bounce_buf) {
		free_pages((unsigned long)host->bounce_buf, 0);
		host->bounce_buf = NULL;
	}
}
