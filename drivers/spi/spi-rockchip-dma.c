/*
 * Special handling for DW core on Intel MID platform
 *
 * Copyright (c) 2009, Intel Corporation.
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

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_data/spi-rockchip.h>


#include "spi-rockchip-core.h"

#ifdef CONFIG_SPI_ROCKCHIP_DMA

struct spi_dma_slave {
	struct dma_chan *ch;
	enum dma_transfer_direction direction;
	unsigned int dmach;
};


struct spi_dma {
	struct spi_dma_slave	dmas_tx;
	struct spi_dma_slave	dmas_rx;
};

static bool mid_spi_dma_chan_filter(struct dma_chan *chan, void *param)
{
	struct dw_spi *dws = param;
	int ret = 0;
	ret = dws->parent_dev && (&dws->parent_dev == chan->device->dev);

	printk("%s:ret=%d\n",__func__, ret);
	return ret;
}


static int mid_spi_dma_init(struct dw_spi *dws)
{
	struct spi_dma *dw_dma = dws->dma_priv;
	struct spi_dma_slave *rxs, *txs;
	dma_cap_mask_t mask;

	/*
	 * Get pci device for DMA controller, currently it could only
	 * be the DMA controller of either Moorestown or Medfield
	 */
	//dws->dmac = pci_get_device(PCI_VENDOR_ID_INTEL, 0x0813, NULL);
	//if (!dws->dmac)
	//	dws->dmac = pci_get_device(PCI_VENDOR_ID_INTEL, 0x0827, NULL);

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* 1. Init rx channel */
	dws->rxchan = dma_request_channel(mask, mid_spi_dma_chan_filter, dws);
	if (!dws->rxchan)
		goto err_exit;
	rxs = &dw_dma->dmas_rx;
	//rxs->hs_mode = LNW_DMA_HW_HS;
	//rxs->cfg_mode = LNW_DMA_PER_TO_MEM;
	dws->rxchan->private = rxs;

	/* 2. Init tx channel */
	dws->txchan = dma_request_channel(mask, mid_spi_dma_chan_filter, dws);
	if (!dws->txchan)
		goto free_rxchan;
	txs = &dw_dma->dmas_tx;
	//txs->hs_mode = LNW_DMA_HW_HS;
	//txs->cfg_mode = LNW_DMA_MEM_TO_PER;
	dws->txchan->private = txs;

	dws->dma_inited = 1;
	return 0;

	free_rxchan:
	dma_release_channel(dws->rxchan);
	err_exit:
	return -1;

}

static void mid_spi_dma_exit(struct dw_spi *dws)
{
	dma_release_channel(dws->txchan);
	dma_release_channel(dws->rxchan);
}

/*
 * dws->dma_chan_done is cleared before the dma transfer starts,
 * callback for rx/tx channel will each increment it by 1.
 * Reaching 2 means the whole spi transaction is done.
 */
static void dw_spi_dma_done(void *arg)
{
	struct dw_spi *dws = arg;

	if (++dws->dma_chan_done != 2)
		return;
	dw_spi_xfer_done(dws);
}

static int mid_spi_dma_transfer(struct dw_spi *dws, int cs_change)
{
	struct dma_async_tx_descriptor *txdesc = NULL, *rxdesc = NULL;
	struct dma_chan *txchan, *rxchan;
	struct dma_slave_config txconf, rxconf;
	u16 dma_ctrl = 0;

	/* 1. setup DMA related registers */
	if (cs_change) {
		spi_enable_chip(dws, 0);
		dw_writew(dws, SPIM_DMARDLR, 0xf);
		dw_writew(dws, SPIM_DMATDLR, 0x10);
		if (dws->tx_dma)
			dma_ctrl |= 0x2;
		if (dws->rx_dma)
			dma_ctrl |= 0x1;
		dw_writew(dws, SPIM_DMACR, dma_ctrl);
		spi_enable_chip(dws, 1);
	}

	dws->dma_chan_done = 0;
	txchan = dws->txchan;
	rxchan = dws->rxchan;

	/* 2. Prepare the TX dma transfer */
	txconf.direction = DMA_MEM_TO_DEV;
	txconf.dst_addr = dws->dma_addr;
	txconf.dst_maxburst = 0x03;//LNW_DMA_MSIZE_16;
	txconf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	txconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	txconf.device_fc = false;

	txchan->device->device_control(txchan, DMA_SLAVE_CONFIG,
				       (unsigned long) &txconf);

	memset(&dws->tx_sgl, 0, sizeof(dws->tx_sgl));
	dws->tx_sgl.dma_address = dws->tx_dma;
	dws->tx_sgl.length = dws->len;

	txdesc = dmaengine_prep_slave_sg(txchan,
				&dws->tx_sgl,
				1,
				DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT | DMA_COMPL_SKIP_DEST_UNMAP);
	txdesc->callback = dw_spi_dma_done;
	txdesc->callback_param = dws;

	/* 3. Prepare the RX dma transfer */
	rxconf.direction = DMA_DEV_TO_MEM;
	rxconf.src_addr = dws->dma_addr;
	rxconf.src_maxburst = 0x03; //LNW_DMA_MSIZE_16;
	rxconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	rxconf.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	rxconf.device_fc = false;

	rxchan->device->device_control(rxchan, DMA_SLAVE_CONFIG,
				       (unsigned long) &rxconf);

	memset(&dws->rx_sgl, 0, sizeof(dws->rx_sgl));
	dws->rx_sgl.dma_address = dws->rx_dma;
	dws->rx_sgl.length = dws->len;

	rxdesc = dmaengine_prep_slave_sg(rxchan,
				&dws->rx_sgl,
				1,
				DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_COMPL_SKIP_DEST_UNMAP);
	rxdesc->callback = dw_spi_dma_done;
	rxdesc->callback_param = dws;

	/* rx must be started before tx due to spi instinct */
	rxdesc->tx_submit(rxdesc);
	txdesc->tx_submit(txdesc);
	return 0;
}

static struct dw_spi_dma_ops spi_dma_ops = {
	.dma_init	= mid_spi_dma_init,
	.dma_exit	= mid_spi_dma_exit,
	.dma_transfer	= mid_spi_dma_transfer,
};

int dw_spi_dma_init(struct dw_spi *dws)
{

	dws->dma_priv = kzalloc(sizeof(struct spi_dma), GFP_KERNEL);
	if (!dws->dma_priv)
		return -ENOMEM;
	dws->dma_ops = &spi_dma_ops;
	return 0;
}
#endif

