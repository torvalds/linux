/*
 * DMA handling for DW core with DMA PL330 controller
 *
 * Modified from linux/driver/spi/spi-dw-mid.c
 *
 * Copyright (c) 2012, Altera Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/amba/pl330.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include "spi-dw.h"
#include <linux/spi/spi-dw.h>
#include <linux/of.h>


/*Burst size configuration*/
enum spi_pl330_brst_sz {
	PL330_DMA_BRSTSZ_1B = 0x1,
	PL330_DMA_BRSTSZ_2B = 0x2,
};

/* TX & RX FIFO depth supported by HW */
#define SSI_FIFO_DEPTH		0xFF

/* Maximum burst length
   Note: Can be up to 16, but now is default to 1 in the PL330 driver.
         Burst transfer is not supported in PL330 driver     */
#define SSI_DMA_MAXBURST	16


static int spi_pl330_dma_chan_alloc(struct dw_spi *dws)
{
	struct device_node *np = dws->master->dev.of_node;
	void *filter_param_rx, *filter_param_tx;
	dma_cap_mask_t mask;
	int lenp;


	/* If DMA channel already allocated */
	if (dws->rxchan && dws->txchan)
		return 0;

	filter_param_tx = of_find_property(np, "tx-dma-channel", &lenp);
	if (!filter_param_tx)
		return -1;
	filter_param_rx = of_find_property(np, "rx-dma-channel", &lenp);
	if (!filter_param_rx)
		return -1;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* 1. Init rx channel */
	dws->rxchan = dma_request_channel(mask, pl330_filter, filter_param_rx);
	while (!dws->rxchan) {
		/* all DMA channels are busy, try again */
		msleep(10);
		dws->rxchan = dma_request_channel(mask, pl330_filter, filter_param_rx);
	}

	/* 2. Init tx channel */
	dws->txchan = dma_request_channel(mask, pl330_filter, filter_param_tx);
	while (!dws->txchan) {
		/* all DMA channels are busy, try again */
		msleep(10);
		dws->txchan = dma_request_channel(mask, pl330_filter, filter_param_tx);
	}

	dws->dma_inited = 1;

	return 0;
}

static void spi_pl330_dma_chan_release(struct dw_spi *dws)
{
	dma_release_channel(dws->txchan);
	dma_release_channel(dws->rxchan);
	dws->txchan = 0;
	dws->rxchan = 0;
	dws->dma_inited = 0;
}

/*
 * dws->dma_chan_done is cleared before the dma transfer starts,
 * callback for rx/tx channel will each increment it by 1.
 * Reaching 2 means the whole spi transaction is done.
 */
static void spi_pl330_dma_done(void *arg)
{
	struct dw_spi *dws = arg;

	if (++dws->dma_chan_done != 2)
		return;
	dw_spi_xfer_done(dws);
}

static int spi_pl330_dma_transfer(struct dw_spi *dws, int cs_change)
{
	struct dma_async_tx_descriptor *txdesc = NULL, *rxdesc = NULL;
	struct dma_chan *txchan, *rxchan;
	struct dma_slave_config txconf, rxconf;
	u16 dma_ctrl = 0;

	/* 1. setup DMA related registers */
	if (cs_change) {
		spi_enable_chip(dws, 0);
		/* Setup peripheral's burst watermark for TX and RX FIFO */
		dw_writew(dws, DW_SPI_DMARDLR, SSI_DMA_MAXBURST - 1);
		dw_writew(dws, DW_SPI_DMATDLR, SSI_FIFO_DEPTH - SSI_DMA_MAXBURST);

		if (dws->tx_dma)
			dma_ctrl |= 0x2;
		if (dws->rx_dma)
			dma_ctrl |= 0x1;
		dw_writew(dws, DW_SPI_DMACR, dma_ctrl);
		spi_enable_chip(dws, 1);
	}

	dws->dma_chan_done = 0;
	txchan = dws->txchan;
	rxchan = dws->rxchan;

	/* 2. Prepare the TX dma transfer */
	txconf.direction = DMA_MEM_TO_DEV;
	txconf.dst_addr = dws->dma_addr;
	/* Note: By default the burst_len (dst_maxburst) for DMA_MEM_TO_DEV is set
			 to 1 and the burst_size (src_addr_width) for memory is set to
			 peripheral's configuration in PL330 driver (driver/dma/pl330.c).
			 Therefore the config listed below can be skipped
				i. txconf.dst_maxburst
				ii. txconf.src_addr_width
			 Max DMA width is 16-bit
	*/
	if (dws->dma_width == 1)
		txconf.dst_addr_width = PL330_DMA_BRSTSZ_1B;
	else
		txconf.dst_addr_width = PL330_DMA_BRSTSZ_2B;

	txchan->device->device_control(txchan, DMA_SLAVE_CONFIG,
				       (unsigned long) &txconf);

	memset(&dws->tx_sgl, 0, sizeof(dws->tx_sgl));
	dws->tx_sgl.dma_address = dws->tx_dma;
	dws->tx_sgl.length = dws->len;

	txdesc = txchan->device->device_prep_slave_sg(txchan,
				&dws->tx_sgl,
				1,
				DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT);
	txdesc->callback = spi_pl330_dma_done;
	txdesc->callback_param = dws;

	/* 3. Prepare the RX dma transfer */
	rxconf.direction = DMA_DEV_TO_MEM;
	rxconf.src_addr = dws->dma_addr;
	/* Note: By default the burst_len (src_maxburst) for DMA_DEV_TO_MEM is set
			 to 1 and the burst_size (dst_addr_width) for memory is set to
			 peripheral's configuration in PL330 driver (driver/dma/pl330.c).
			 Therefore the config listed below can be skipped
		txconf.src_maxburst
		txconf.dst_addr_width
	*/
	if (dws->dma_width == 1)
		rxconf.src_addr_width = PL330_DMA_BRSTSZ_1B;
	else
		rxconf.src_addr_width = PL330_DMA_BRSTSZ_2B;

	rxchan->device->device_control(rxchan, DMA_SLAVE_CONFIG,
				       (unsigned long) &rxconf);

	memset(&dws->rx_sgl, 0, sizeof(dws->rx_sgl));
	dws->rx_sgl.dma_address = dws->rx_dma;
	dws->rx_sgl.length = dws->len;

	rxdesc = rxchan->device->device_prep_slave_sg(rxchan,
				&dws->rx_sgl,
				1,
				DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT);
	rxdesc->callback = spi_pl330_dma_done;
	rxdesc->callback_param = dws;

	/* rx must be started before tx due to spi instinct */
	rxdesc->tx_submit(rxdesc);
	dma_async_issue_pending(rxchan);
	txdesc->tx_submit(txdesc);
	dma_async_issue_pending(txchan);

	return 0;
}

static struct dw_spi_dma_ops pl330_dma_ops = {
	.dma_transfer	= spi_pl330_dma_transfer,
	.dma_chan_alloc = spi_pl330_dma_chan_alloc,
	.dma_chan_release = spi_pl330_dma_chan_release,
};

int dw_spi_pl330_init(struct dw_spi *dws)
{
	dws->fifo_len = SSI_FIFO_DEPTH;
	dws->dma_ops = &pl330_dma_ops;

	return 0;
}
