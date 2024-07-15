// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2022 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 *
 * This module handles the DMA transfers. A standard dmaengine API as provided
 * by the XDMA module is used.
 */

#include <linux/pci.h>
#include <linux/dma-direction.h>
#include "mgb4_core.h"
#include "mgb4_dma.h"

static void chan_irq(void *param)
{
	struct mgb4_dma_channel *chan = param;

	complete(&chan->req_compl);
}

int mgb4_dma_transfer(struct mgb4_dev *mgbdev, u32 channel, bool write,
		      u64 paddr, struct sg_table *sgt)
{
	struct dma_slave_config cfg;
	struct mgb4_dma_channel *chan;
	struct dma_async_tx_descriptor *tx;
	struct pci_dev *pdev = mgbdev->pdev;
	int ret;

	memset(&cfg, 0, sizeof(cfg));

	if (write) {
		cfg.direction = DMA_MEM_TO_DEV;
		cfg.dst_addr = paddr;
		cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		chan = &mgbdev->h2c_chan[channel];
	} else {
		cfg.direction = DMA_DEV_TO_MEM;
		cfg.src_addr = paddr;
		cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		chan = &mgbdev->c2h_chan[channel];
	}

	ret = dmaengine_slave_config(chan->chan, &cfg);
	if (ret) {
		dev_err(&pdev->dev, "failed to config dma: %d\n", ret);
		return ret;
	}

	tx = dmaengine_prep_slave_sg(chan->chan, sgt->sgl, sgt->nents,
				     cfg.direction, 0);
	if (!tx) {
		dev_err(&pdev->dev, "failed to prep slave sg\n");
		return -EIO;
	}

	tx->callback = chan_irq;
	tx->callback_param = chan;

	ret = dma_submit_error(dmaengine_submit(tx));
	if (ret) {
		dev_err(&pdev->dev, "failed to submit sg\n");
		return -EIO;
	}

	dma_async_issue_pending(chan->chan);

	if (!wait_for_completion_timeout(&chan->req_compl,
					 msecs_to_jiffies(10000))) {
		dev_err(&pdev->dev, "dma timeout\n");
		dmaengine_terminate_sync(chan->chan);
		return -EIO;
	}

	return 0;
}

int mgb4_dma_channel_init(struct mgb4_dev *mgbdev)
{
	int i, ret;
	char name[16];
	struct pci_dev *pdev = mgbdev->pdev;

	for (i = 0; i < MGB4_VIN_DEVICES; i++) {
		sprintf(name, "c2h%d", i);
		mgbdev->c2h_chan[i].chan = dma_request_chan(&pdev->dev, name);
		if (IS_ERR(mgbdev->c2h_chan[i].chan)) {
			dev_err(&pdev->dev, "failed to initialize %s", name);
			ret = PTR_ERR(mgbdev->c2h_chan[i].chan);
			mgbdev->c2h_chan[i].chan = NULL;
			return ret;
		}
		init_completion(&mgbdev->c2h_chan[i].req_compl);
	}
	for (i = 0; i < MGB4_VOUT_DEVICES; i++) {
		sprintf(name, "h2c%d", i);
		mgbdev->h2c_chan[i].chan = dma_request_chan(&pdev->dev, name);
		if (IS_ERR(mgbdev->h2c_chan[i].chan)) {
			dev_err(&pdev->dev, "failed to initialize %s", name);
			ret = PTR_ERR(mgbdev->h2c_chan[i].chan);
			mgbdev->h2c_chan[i].chan = NULL;
			return ret;
		}
		init_completion(&mgbdev->h2c_chan[i].req_compl);
	}

	return 0;
}

void mgb4_dma_channel_free(struct mgb4_dev *mgbdev)
{
	int i;

	for (i = 0; i < MGB4_VIN_DEVICES; i++) {
		if (mgbdev->c2h_chan[i].chan)
			dma_release_channel(mgbdev->c2h_chan[i].chan);
	}
	for (i = 0; i < MGB4_VOUT_DEVICES; i++) {
		if (mgbdev->h2c_chan[i].chan)
			dma_release_channel(mgbdev->h2c_chan[i].chan);
	}
}
