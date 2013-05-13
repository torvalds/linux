/*
 * PXA2xx SPI DMA engine support.
 *
 * Copyright (C) 2013, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/pxa2xx_ssp.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>

#include "spi-pxa2xx.h"

static int pxa2xx_spi_map_dma_buffer(struct driver_data *drv_data,
				     enum dma_data_direction dir)
{
	int i, nents, len = drv_data->len;
	struct scatterlist *sg;
	struct device *dmadev;
	struct sg_table *sgt;
	void *buf, *pbuf;

	/*
	 * Some DMA controllers have problems transferring buffers that are
	 * not multiple of 4 bytes. So we truncate the transfer so that it
	 * is suitable for such controllers, and handle the trailing bytes
	 * manually after the DMA completes.
	 *
	 * REVISIT: It would be better if this information could be
	 * retrieved directly from the DMA device in a similar way than
	 * ->copy_align etc. is done.
	 */
	len = ALIGN(drv_data->len, 4);

	if (dir == DMA_TO_DEVICE) {
		dmadev = drv_data->tx_chan->device->dev;
		sgt = &drv_data->tx_sgt;
		buf = drv_data->tx;
		drv_data->tx_map_len = len;
	} else {
		dmadev = drv_data->rx_chan->device->dev;
		sgt = &drv_data->rx_sgt;
		buf = drv_data->rx;
		drv_data->rx_map_len = len;
	}

	nents = DIV_ROUND_UP(len, SZ_2K);
	if (nents != sgt->nents) {
		int ret;

		sg_free_table(sgt);
		ret = sg_alloc_table(sgt, nents, GFP_KERNEL);
		if (ret)
			return ret;
	}

	pbuf = buf;
	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		size_t bytes = min_t(size_t, len, SZ_2K);

		if (buf)
			sg_set_buf(sg, pbuf, bytes);
		else
			sg_set_buf(sg, drv_data->dummy, bytes);

		pbuf += bytes;
		len -= bytes;
	}

	nents = dma_map_sg(dmadev, sgt->sgl, sgt->nents, dir);
	if (!nents)
		return -ENOMEM;

	return nents;
}

static void pxa2xx_spi_unmap_dma_buffer(struct driver_data *drv_data,
					enum dma_data_direction dir)
{
	struct device *dmadev;
	struct sg_table *sgt;

	if (dir == DMA_TO_DEVICE) {
		dmadev = drv_data->tx_chan->device->dev;
		sgt = &drv_data->tx_sgt;
	} else {
		dmadev = drv_data->rx_chan->device->dev;
		sgt = &drv_data->rx_sgt;
	}

	dma_unmap_sg(dmadev, sgt->sgl, sgt->nents, dir);
}

static void pxa2xx_spi_unmap_dma_buffers(struct driver_data *drv_data)
{
	if (!drv_data->dma_mapped)
		return;

	pxa2xx_spi_unmap_dma_buffer(drv_data, DMA_FROM_DEVICE);
	pxa2xx_spi_unmap_dma_buffer(drv_data, DMA_TO_DEVICE);

	drv_data->dma_mapped = 0;
}

static void pxa2xx_spi_dma_transfer_complete(struct driver_data *drv_data,
					     bool error)
{
	struct spi_message *msg = drv_data->cur_msg;

	/*
	 * It is possible that one CPU is handling ROR interrupt and other
	 * just gets DMA completion. Calling pump_transfers() twice for the
	 * same transfer leads to problems thus we prevent concurrent calls
	 * by using ->dma_running.
	 */
	if (atomic_dec_and_test(&drv_data->dma_running)) {
		void __iomem *reg = drv_data->ioaddr;

		/*
		 * If the other CPU is still handling the ROR interrupt we
		 * might not know about the error yet. So we re-check the
		 * ROR bit here before we clear the status register.
		 */
		if (!error) {
			u32 status = read_SSSR(reg) & drv_data->mask_sr;
			error = status & SSSR_ROR;
		}

		/* Clear status & disable interrupts */
		write_SSCR1(read_SSCR1(reg) & ~drv_data->dma_cr1, reg);
		write_SSSR_CS(drv_data, drv_data->clear_sr);
		if (!pxa25x_ssp_comp(drv_data))
			write_SSTO(0, reg);

		if (!error) {
			pxa2xx_spi_unmap_dma_buffers(drv_data);

			/* Handle the last bytes of unaligned transfer */
			drv_data->tx += drv_data->tx_map_len;
			drv_data->write(drv_data);

			drv_data->rx += drv_data->rx_map_len;
			drv_data->read(drv_data);

			msg->actual_length += drv_data->len;
			msg->state = pxa2xx_spi_next_transfer(drv_data);
		} else {
			/* In case we got an error we disable the SSP now */
			write_SSCR0(read_SSCR0(reg) & ~SSCR0_SSE, reg);

			msg->state = ERROR_STATE;
		}

		tasklet_schedule(&drv_data->pump_transfers);
	}
}

static void pxa2xx_spi_dma_callback(void *data)
{
	pxa2xx_spi_dma_transfer_complete(data, false);
}

static struct dma_async_tx_descriptor *
pxa2xx_spi_dma_prepare_one(struct driver_data *drv_data,
			   enum dma_transfer_direction dir)
{
	struct pxa2xx_spi_master *pdata = drv_data->master_info;
	struct chip_data *chip = drv_data->cur_chip;
	enum dma_slave_buswidth width;
	struct dma_slave_config cfg;
	struct dma_chan *chan;
	struct sg_table *sgt;
	int nents, ret;

	switch (drv_data->n_bytes) {
	case 1:
		width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		break;
	case 2:
		width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	default:
		width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.direction = dir;

	if (dir == DMA_MEM_TO_DEV) {
		cfg.dst_addr = drv_data->ssdr_physical;
		cfg.dst_addr_width = width;
		cfg.dst_maxburst = chip->dma_burst_size;
		cfg.slave_id = pdata->tx_slave_id;

		sgt = &drv_data->tx_sgt;
		nents = drv_data->tx_nents;
		chan = drv_data->tx_chan;
	} else {
		cfg.src_addr = drv_data->ssdr_physical;
		cfg.src_addr_width = width;
		cfg.src_maxburst = chip->dma_burst_size;
		cfg.slave_id = pdata->rx_slave_id;

		sgt = &drv_data->rx_sgt;
		nents = drv_data->rx_nents;
		chan = drv_data->rx_chan;
	}

	ret = dmaengine_slave_config(chan, &cfg);
	if (ret) {
		dev_warn(&drv_data->pdev->dev, "DMA slave config failed\n");
		return NULL;
	}

	return dmaengine_prep_slave_sg(chan, sgt->sgl, nents, dir,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
}

static bool pxa2xx_spi_dma_filter(struct dma_chan *chan, void *param)
{
	const struct pxa2xx_spi_master *pdata = param;

	return chan->chan_id == pdata->tx_chan_id ||
	       chan->chan_id == pdata->rx_chan_id;
}

bool pxa2xx_spi_dma_is_possible(size_t len)
{
	return len <= MAX_DMA_LEN;
}

int pxa2xx_spi_map_dma_buffers(struct driver_data *drv_data)
{
	const struct chip_data *chip = drv_data->cur_chip;
	int ret;

	if (!chip->enable_dma)
		return 0;

	/* Don't bother with DMA if we can't do even a single burst */
	if (drv_data->len < chip->dma_burst_size)
		return 0;

	ret = pxa2xx_spi_map_dma_buffer(drv_data, DMA_TO_DEVICE);
	if (ret <= 0) {
		dev_warn(&drv_data->pdev->dev, "failed to DMA map TX\n");
		return 0;
	}

	drv_data->tx_nents = ret;

	ret = pxa2xx_spi_map_dma_buffer(drv_data, DMA_FROM_DEVICE);
	if (ret <= 0) {
		pxa2xx_spi_unmap_dma_buffer(drv_data, DMA_TO_DEVICE);
		dev_warn(&drv_data->pdev->dev, "failed to DMA map RX\n");
		return 0;
	}

	drv_data->rx_nents = ret;
	return 1;
}

irqreturn_t pxa2xx_spi_dma_transfer(struct driver_data *drv_data)
{
	u32 status;

	status = read_SSSR(drv_data->ioaddr) & drv_data->mask_sr;
	if (status & SSSR_ROR) {
		dev_err(&drv_data->pdev->dev, "FIFO overrun\n");

		dmaengine_terminate_all(drv_data->rx_chan);
		dmaengine_terminate_all(drv_data->tx_chan);

		pxa2xx_spi_dma_transfer_complete(drv_data, true);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

int pxa2xx_spi_dma_prepare(struct driver_data *drv_data, u32 dma_burst)
{
	struct dma_async_tx_descriptor *tx_desc, *rx_desc;

	tx_desc = pxa2xx_spi_dma_prepare_one(drv_data, DMA_MEM_TO_DEV);
	if (!tx_desc) {
		dev_err(&drv_data->pdev->dev,
			"failed to get DMA TX descriptor\n");
		return -EBUSY;
	}

	rx_desc = pxa2xx_spi_dma_prepare_one(drv_data, DMA_DEV_TO_MEM);
	if (!rx_desc) {
		dev_err(&drv_data->pdev->dev,
			"failed to get DMA RX descriptor\n");
		return -EBUSY;
	}

	/* We are ready when RX completes */
	rx_desc->callback = pxa2xx_spi_dma_callback;
	rx_desc->callback_param = drv_data;

	dmaengine_submit(rx_desc);
	dmaengine_submit(tx_desc);
	return 0;
}

void pxa2xx_spi_dma_start(struct driver_data *drv_data)
{
	dma_async_issue_pending(drv_data->rx_chan);
	dma_async_issue_pending(drv_data->tx_chan);

	atomic_set(&drv_data->dma_running, 1);
}

int pxa2xx_spi_dma_setup(struct driver_data *drv_data)
{
	struct pxa2xx_spi_master *pdata = drv_data->master_info;
	struct device *dev = &drv_data->pdev->dev;
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	drv_data->dummy = devm_kzalloc(dev, SZ_2K, GFP_KERNEL);
	if (!drv_data->dummy)
		return -ENOMEM;

	drv_data->tx_chan = dma_request_slave_channel_compat(mask,
				pxa2xx_spi_dma_filter, pdata, dev, "tx");
	if (!drv_data->tx_chan)
		return -ENODEV;

	drv_data->rx_chan = dma_request_slave_channel_compat(mask,
				pxa2xx_spi_dma_filter, pdata, dev, "rx");
	if (!drv_data->rx_chan) {
		dma_release_channel(drv_data->tx_chan);
		drv_data->tx_chan = NULL;
		return -ENODEV;
	}

	return 0;
}

void pxa2xx_spi_dma_release(struct driver_data *drv_data)
{
	if (drv_data->rx_chan) {
		dmaengine_terminate_all(drv_data->rx_chan);
		dma_release_channel(drv_data->rx_chan);
		sg_free_table(&drv_data->rx_sgt);
		drv_data->rx_chan = NULL;
	}
	if (drv_data->tx_chan) {
		dmaengine_terminate_all(drv_data->tx_chan);
		dma_release_channel(drv_data->tx_chan);
		sg_free_table(&drv_data->tx_sgt);
		drv_data->tx_chan = NULL;
	}
}

void pxa2xx_spi_dma_resume(struct driver_data *drv_data)
{
}

int pxa2xx_spi_set_dma_burst_and_threshold(struct chip_data *chip,
					   struct spi_device *spi,
					   u8 bits_per_word, u32 *burst_code,
					   u32 *threshold)
{
	struct pxa2xx_spi_chip *chip_info = spi->controller_data;

	/*
	 * If the DMA burst size is given in chip_info we use that,
	 * otherwise we use the default. Also we use the default FIFO
	 * thresholds for now.
	 */
	*burst_code = chip_info ? chip_info->dma_burst_size : 16;
	*threshold = SSCR1_RxTresh(RX_THRESH_DFLT)
		   | SSCR1_TxTresh(TX_THRESH_DFLT);

	return 0;
}
