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
#define DMA_BUFFER_SIZE (PAGE_SIZE<<4)


struct spi_dma_slave {
	struct dma_chan *ch;
	enum dma_transfer_direction direction;
	unsigned int dmach;
};


struct spi_dma {
	struct spi_dma_slave	dmas_tx;
	struct spi_dma_slave	dmas_rx;
};

static void printk_transfer_data(struct dw_spi *dws, char *buf, int len)
{
	int i = 0;
	for(i=0; i<len; i++)
		DBG_SPI("0x%02x,",*buf++);

	DBG_SPI("\n");

}

static int mid_spi_dma_init(struct dw_spi *dws)
{
	struct spi_dma *dw_dma = dws->dma_priv;
	struct spi_dma_slave *rxs, *txs;
	
	DBG_SPI("%s:start\n",__func__);

	/* 1. Init rx channel */
	dws->rxchan = dma_request_slave_channel(dws->parent_dev, "rx");
	if (!dws->rxchan)
	{
		dev_err(dws->parent_dev, "Failed to get RX DMA channel\n");
		goto err_exit;
	}
	
	DBG_SPI("%s:rx_chan_id=%d\n",__func__,dws->rxchan->chan_id);
	
	rxs = &dw_dma->dmas_rx;
	dws->rxchan->private = rxs;

	/* 2. Init tx channel */
	dws->txchan = dma_request_slave_channel(dws->parent_dev, "tx");
	if (!dws->txchan)
	{
		dev_err(dws->parent_dev, "Failed to get TX DMA channel\n");
		goto free_rxchan;
	}
	txs = &dw_dma->dmas_tx;
	dws->txchan->private = txs;
	
	DBG_SPI("%s:tx_chan_id=%d\n",__func__,dws->txchan->chan_id);

	dws->dma_inited = 1;

	DBG_SPI("%s:line=%d\n",__func__,__LINE__);
	return 0;

free_rxchan:
	dma_release_channel(dws->rxchan);
err_exit:
	return -1;

}

static void mid_spi_dma_exit(struct dw_spi *dws)
{
	DBG_SPI("%s:start\n",__func__);
	dma_release_channel(dws->txchan);
	dma_release_channel(dws->rxchan);
}


static void dw_spi_dma_rxcb(void *arg)
{
	struct dw_spi *dws = arg;
	unsigned long flags;
	struct dma_tx_state		state;
	int				dma_status;

	dma_sync_single_for_device(dws->rxchan->device->dev, dws->rx_dma,
				   dws->len, DMA_FROM_DEVICE);
	
	dma_status = dmaengine_tx_status(dws->rxchan, dws->rx_cookie, &state);
	
	DBG_SPI("%s:dma_status=0x%x\n", __FUNCTION__, dma_status);
	
	spin_lock_irqsave(&dws->lock, flags);		
	if (dma_status == DMA_SUCCESS)
		dws->state &= ~RXBUSY;
	else
		dev_err(&dws->master->dev, "error:rx dma_status=%x\n", dma_status);

	//copy data from dma to transfer buf
	if(dws->cur_transfer && (dws->cur_transfer->rx_buf != NULL))
	{
		memcpy(dws->cur_transfer->rx_buf, dws->rx_buffer, dws->cur_transfer->len);
		
		DBG_SPI("dma rx:");
		printk_transfer_data(dws, dws->cur_transfer->rx_buf, dws->cur_transfer->len);
	}
	
	spin_unlock_irqrestore(&dws->lock, flags);
	
	/* If the other done */
	if (!(dws->state & TXBUSY))
	{
		//DMA could not lose intterupt
		dw_spi_xfer_done(dws);
		complete(&dws->xfer_completion);
		DBG_SPI("%s:complete\n", __FUNCTION__);
	}

}

static void dw_spi_dma_txcb(void *arg)
{
	struct dw_spi *dws = arg;
	unsigned long flags;
	struct dma_tx_state		state;
	int				dma_status;

	dma_sync_single_for_device(dws->txchan->device->dev, dws->tx_dma,
				   dws->len, DMA_TO_DEVICE);
	
	dma_status = dmaengine_tx_status(dws->txchan, dws->tx_cookie, &state);
	
	DBG_SPI("%s:dma_status=0x%x\n", __FUNCTION__, dma_status);
	DBG_SPI("dma tx:");
	printk_transfer_data(dws, (char *)dws->cur_transfer->tx_buf, dws->cur_transfer->len);
	
	spin_lock_irqsave(&dws->lock, flags);
	
	if (dma_status == DMA_SUCCESS)
		dws->state &= ~TXBUSY;
	else	
		dev_err(&dws->master->dev, "error:tx dma_status=%x\n", dma_status);	

	spin_unlock_irqrestore(&dws->lock, flags);
	
	/* If the other done */
	if (!(dws->state & RXBUSY))
	{
		//DMA could not lose intterupt
		dw_spi_xfer_done(dws);
		complete(&dws->xfer_completion);
		DBG_SPI("%s:complete\n", __FUNCTION__);
	}

}


static int mid_spi_dma_transfer(struct dw_spi *dws, int cs_change)
{
	struct dma_async_tx_descriptor *txdesc = NULL, *rxdesc = NULL;
	struct dma_chan *txchan, *rxchan;
	struct dma_slave_config txconf, rxconf;
	int ret = 0;
	
	enum dma_slave_buswidth width;

	DBG_SPI("%s:cs_change=%d\n",__func__,cs_change);
	
	//alloc dma buffer default while cur_transfer->tx_dma or cur_transfer->rx_dma is null
	if((dws->cur_transfer->tx_buf) && dws->dma_mapped && (!dws->cur_transfer->tx_dma))
	{
		//printk("%s:warning tx_dma is %p\n",__func__, (int *)dws->tx_dma);
		memcpy(dws->tx_buffer, dws->cur_transfer->tx_buf, dws->cur_transfer->len);		
		dws->tx_dma = dws->tx_dma_init;
	}

	if((dws->cur_transfer->rx_buf) && dws->dma_mapped && (!dws->cur_transfer->rx_dma))
	{		
		//printk("%s:warning rx_dma is %p\n",__func__, (int *)dws->rx_dma);
		dws->rx_dma = dws->rx_dma_init;
	}

	
	if (dws->tx)
		dws->state |= TXBUSY;	
	if (dws->rx)
		dws->state |= RXBUSY;

	
	switch (dws->n_bytes) {
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
		
	dws->dma_chan_done = 0;
	
	if (dws->tx)
	txchan = dws->txchan;
	
	if (dws->rx)
	rxchan = dws->rxchan;
	
	if (dws->tx)
	{
		/* 2. Prepare the TX dma transfer */
		txconf.direction = DMA_MEM_TO_DEV;
		txconf.dst_addr = dws->tx_dma_addr;
		txconf.dst_maxburst = dws->dma_width;
		//txconf.src_addr_width = width;
		txconf.dst_addr_width = width;
		//txconf.device_fc = false;

		ret = dmaengine_slave_config(txchan, &txconf);
		if (ret) {
			dev_warn(dws->parent_dev, "TX DMA slave config failed\n");
			return -1;
		}
		
		memset(&dws->tx_sgl, 0, sizeof(dws->tx_sgl));
		dws->tx_sgl.dma_address = dws->tx_dma;
		dws->tx_sgl.length = dws->len;

		txdesc = dmaengine_prep_slave_sg(txchan,
					&dws->tx_sgl,
					1,
					DMA_MEM_TO_DEV,
					DMA_PREP_INTERRUPT);
		
		txdesc->callback = dw_spi_dma_txcb;
		txdesc->callback_param = dws;

		DBG_SPI("%s:dst_addr=0x%p,tx_dma=0x%p,len=%d,burst=%d,width=%d\n",__func__,(int *)dws->tx_dma_addr, (int *)dws->tx_dma, dws->len,dws->dma_width, width);
	}

	if (dws->rx)
	{
		/* 3. Prepare the RX dma transfer */
		rxconf.direction = DMA_DEV_TO_MEM;
		rxconf.src_addr = dws->rx_dma_addr;
		rxconf.src_maxburst = dws->dma_width; 
		//rxconf.dst_addr_width = width;
		rxconf.src_addr_width = width;
		//rxconf.device_fc = false;

		ret = dmaengine_slave_config(rxchan, &rxconf);
		if (ret) {
			dev_warn(dws->parent_dev, "RX DMA slave config failed\n");
			return -1;
		}

		memset(&dws->rx_sgl, 0, sizeof(dws->rx_sgl));
		dws->rx_sgl.dma_address = dws->rx_dma;
		dws->rx_sgl.length = dws->len;				

		rxdesc = dmaengine_prep_slave_sg(rxchan,
					&dws->rx_sgl,
					1,
					DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT);
		rxdesc->callback = dw_spi_dma_rxcb;
		rxdesc->callback_param = dws;
		
		DBG_SPI("%s:src_addr=0x%p,rx_dma=0x%p,len=%d,burst=%d,width=%d\n",__func__, (int *)dws->rx_dma_addr, (int *)dws->rx_dma, dws->len, dws->dma_width, width);
	}
	
	/* rx must be started before tx due to spi instinct */	
	if (dws->rx)
	{		
		dws->rx_cookie = dmaengine_submit(rxdesc);
		dma_sync_single_for_device(rxchan->device->dev, dws->rx_dma,
				   dws->len, DMA_FROM_DEVICE);
		dma_async_issue_pending(rxchan);
		
		DBG_SPI("%s:rx end\n",__func__);
	}
	
	if (dws->tx)
	{		
		dws->tx_cookie = dmaengine_submit(txdesc);
		dma_sync_single_for_device(txchan->device->dev, dws->tx_dma,
				   dws->len, DMA_TO_DEVICE);
		dma_async_issue_pending(txchan);
		
		DBG_SPI("%s:tx end\n",__func__);
	}
	
	return 0;
}

static struct dw_spi_dma_ops spi_dma_ops = {
	.dma_init	= mid_spi_dma_init,
	.dma_exit	= mid_spi_dma_exit,
	.dma_transfer	= mid_spi_dma_transfer,
};

int dw_spi_dma_init(struct dw_spi *dws)
{
	DBG_SPI("%s:start\n",__func__);
	dws->dma_priv = kzalloc(sizeof(struct spi_dma), GFP_KERNEL);
	if (!dws->dma_priv)
		return -ENOMEM;
	dws->dma_ops = &spi_dma_ops;

	dws->tx_buffer = dma_alloc_coherent(dws->parent_dev, DMA_BUFFER_SIZE, &dws->tx_dma_init, GFP_KERNEL | GFP_DMA);
	if (!dws->tx_buffer)
	{
		dev_err(dws->parent_dev, "fail to dma tx buffer alloc\n");
		return -1;
	}

	dws->rx_buffer = dma_alloc_coherent(dws->parent_dev, DMA_BUFFER_SIZE, &dws->rx_dma_init, GFP_KERNEL | GFP_DMA);
	if (!dws->rx_buffer)
	{
		dev_err(dws->parent_dev, "fail to dma rx buffer alloc\n");
		return -1;
	}

	memset(dws->tx_buffer, 0, DMA_BUFFER_SIZE);
	memset(dws->rx_buffer, 0, DMA_BUFFER_SIZE);

	dws->state = 0;
	
	init_completion(&dws->xfer_completion);
	
	return 0;
}
#endif

