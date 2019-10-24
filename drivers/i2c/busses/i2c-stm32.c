/*
 * i2c-stm32.c
 *
 * Copyright (C) M'boumba Cedric Madianga 2017
 * Author: M'boumba Cedric Madianga <cedric.madianga@gmail.com>
 *
 * License terms:  GNU General Public License (GPL), version 2
 */

#include "i2c-stm32.h"

/* Functions for DMA support */
struct stm32_i2c_dma *stm32_i2c_dma_request(struct device *dev,
					    dma_addr_t phy_addr,
					    u32 txdr_offset,
					    u32 rxdr_offset)
{
	struct stm32_i2c_dma *dma;
	struct dma_slave_config dma_sconfig;
	int ret;

	dma = devm_kzalloc(dev, sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return ERR_PTR(-ENOMEM);

	/* Request and configure I2C TX dma channel */
	dma->chan_tx = dma_request_chan(dev, "tx");
	if (IS_ERR(dma->chan_tx)) {
		dev_dbg(dev, "can't request DMA tx channel\n");
		ret = PTR_ERR(dma->chan_tx);
		goto fail_al;
	}

	memset(&dma_sconfig, 0, sizeof(dma_sconfig));
	dma_sconfig.dst_addr = phy_addr + txdr_offset;
	dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma_sconfig.dst_maxburst = 1;
	dma_sconfig.direction = DMA_MEM_TO_DEV;
	ret = dmaengine_slave_config(dma->chan_tx, &dma_sconfig);
	if (ret < 0) {
		dev_err(dev, "can't configure tx channel\n");
		goto fail_tx;
	}

	/* Request and configure I2C RX dma channel */
	dma->chan_rx = dma_request_chan(dev, "rx");
	if (IS_ERR(dma->chan_rx)) {
		dev_err(dev, "can't request DMA rx channel\n");
		ret = PTR_ERR(dma->chan_rx);
		goto fail_tx;
	}

	memset(&dma_sconfig, 0, sizeof(dma_sconfig));
	dma_sconfig.src_addr = phy_addr + rxdr_offset;
	dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma_sconfig.src_maxburst = 1;
	dma_sconfig.direction = DMA_DEV_TO_MEM;
	ret = dmaengine_slave_config(dma->chan_rx, &dma_sconfig);
	if (ret < 0) {
		dev_err(dev, "can't configure rx channel\n");
		goto fail_rx;
	}

	init_completion(&dma->dma_complete);

	dev_info(dev, "using %s (tx) and %s (rx) for DMA transfers\n",
		 dma_chan_name(dma->chan_tx), dma_chan_name(dma->chan_rx));

	return dma;

fail_rx:
	dma_release_channel(dma->chan_rx);
fail_tx:
	dma_release_channel(dma->chan_tx);
fail_al:
	devm_kfree(dev, dma);
	dev_info(dev, "can't use DMA\n");

	return ERR_PTR(ret);
}

void stm32_i2c_dma_free(struct stm32_i2c_dma *dma)
{
	dma->dma_buf = 0;
	dma->dma_len = 0;

	dma_release_channel(dma->chan_tx);
	dma->chan_tx = NULL;

	dma_release_channel(dma->chan_rx);
	dma->chan_rx = NULL;

	dma->chan_using = NULL;
}

int stm32_i2c_prep_dma_xfer(struct device *dev, struct stm32_i2c_dma *dma,
			    bool rd_wr, u32 len, u8 *buf,
			    dma_async_tx_callback callback,
			    void *dma_async_param)
{
	struct dma_async_tx_descriptor *txdesc;
	struct device *chan_dev;
	int ret;

	if (rd_wr) {
		dma->chan_using = dma->chan_rx;
		dma->dma_transfer_dir = DMA_DEV_TO_MEM;
		dma->dma_data_dir = DMA_FROM_DEVICE;
	} else {
		dma->chan_using = dma->chan_tx;
		dma->dma_transfer_dir = DMA_MEM_TO_DEV;
		dma->dma_data_dir = DMA_TO_DEVICE;
	}

	dma->dma_len = len;
	chan_dev = dma->chan_using->device->dev;

	dma->dma_buf = dma_map_single(chan_dev, buf, dma->dma_len,
				      dma->dma_data_dir);
	if (dma_mapping_error(chan_dev, dma->dma_buf)) {
		dev_err(dev, "DMA mapping failed\n");
		return -EINVAL;
	}

	txdesc = dmaengine_prep_slave_single(dma->chan_using, dma->dma_buf,
					     dma->dma_len,
					     dma->dma_transfer_dir,
					     DMA_PREP_INTERRUPT);
	if (!txdesc) {
		dev_err(dev, "Not able to get desc for DMA xfer\n");
		ret = -EINVAL;
		goto err;
	}

	reinit_completion(&dma->dma_complete);

	txdesc->callback = callback;
	txdesc->callback_param = dma_async_param;
	ret = dma_submit_error(dmaengine_submit(txdesc));
	if (ret < 0) {
		dev_err(dev, "DMA submit failed\n");
		goto err;
	}

	dma_async_issue_pending(dma->chan_using);

	return 0;

err:
	dma_unmap_single(chan_dev, dma->dma_buf, dma->dma_len,
			 dma->dma_data_dir);
	return ret;
}
