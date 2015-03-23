/*
 * Special handling for DW core on Intel MID platform
 *
 * Copyright (c) 2009, 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "spi-dw.h"

#ifdef CONFIG_SPI_DW_MID_DMA
#include <linux/intel_mid_dma.h>
#include <linux/pci.h>

#define RX_BUSY		0
#define TX_BUSY		1

struct mid_dma {
	struct intel_mid_dma_slave	dmas_tx;
	struct intel_mid_dma_slave	dmas_rx;
};

static bool mid_spi_dma_chan_filter(struct dma_chan *chan, void *param)
{
	struct dw_spi *dws = param;

	return dws->dma_dev == chan->device->dev;
}

static int mid_spi_dma_init(struct dw_spi *dws)
{
	struct mid_dma *dw_dma = dws->dma_priv;
	struct pci_dev *dma_dev;
	struct intel_mid_dma_slave *rxs, *txs;
	dma_cap_mask_t mask;

	/*
	 * Get pci device for DMA controller, currently it could only
	 * be the DMA controller of Medfield
	 */
	dma_dev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x0827, NULL);
	if (!dma_dev)
		return -ENODEV;

	dws->dma_dev = &dma_dev->dev;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* 1. Init rx channel */
	dws->rxchan = dma_request_channel(mask, mid_spi_dma_chan_filter, dws);
	if (!dws->rxchan)
		goto err_exit;
	rxs = &dw_dma->dmas_rx;
	rxs->hs_mode = LNW_DMA_HW_HS;
	rxs->cfg_mode = LNW_DMA_PER_TO_MEM;
	dws->rxchan->private = rxs;

	/* 2. Init tx channel */
	dws->txchan = dma_request_channel(mask, mid_spi_dma_chan_filter, dws);
	if (!dws->txchan)
		goto free_rxchan;
	txs = &dw_dma->dmas_tx;
	txs->hs_mode = LNW_DMA_HW_HS;
	txs->cfg_mode = LNW_DMA_MEM_TO_PER;
	dws->txchan->private = txs;

	dws->dma_inited = 1;
	return 0;

free_rxchan:
	dma_release_channel(dws->rxchan);
err_exit:
	return -EBUSY;
}

static void mid_spi_dma_exit(struct dw_spi *dws)
{
	if (!dws->dma_inited)
		return;

	dmaengine_terminate_all(dws->txchan);
	dma_release_channel(dws->txchan);

	dmaengine_terminate_all(dws->rxchan);
	dma_release_channel(dws->rxchan);
}

/*
 * dws->dma_chan_busy is set before the dma transfer starts, callback for tx
 * channel will clear a corresponding bit.
 */
static void dw_spi_dma_tx_done(void *arg)
{
	struct dw_spi *dws = arg;

	if (test_and_clear_bit(TX_BUSY, &dws->dma_chan_busy) & BIT(RX_BUSY))
		return;
	dw_spi_xfer_done(dws);
}

static struct dma_async_tx_descriptor *dw_spi_dma_prepare_tx(struct dw_spi *dws)
{
	struct dma_slave_config txconf;
	struct dma_async_tx_descriptor *txdesc;

	if (!dws->tx_dma)
		return NULL;

	txconf.direction = DMA_MEM_TO_DEV;
	txconf.dst_addr = dws->dma_addr;
	txconf.dst_maxburst = LNW_DMA_MSIZE_16;
	txconf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	txconf.dst_addr_width = dws->dma_width;
	txconf.device_fc = false;

	dmaengine_slave_config(dws->txchan, &txconf);

	memset(&dws->tx_sgl, 0, sizeof(dws->tx_sgl));
	dws->tx_sgl.dma_address = dws->tx_dma;
	dws->tx_sgl.length = dws->len;

	txdesc = dmaengine_prep_slave_sg(dws->txchan,
				&dws->tx_sgl,
				1,
				DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc)
		return NULL;

	txdesc->callback = dw_spi_dma_tx_done;
	txdesc->callback_param = dws;

	return txdesc;
}

/*
 * dws->dma_chan_busy is set before the dma transfer starts, callback for rx
 * channel will clear a corresponding bit.
 */
static void dw_spi_dma_rx_done(void *arg)
{
	struct dw_spi *dws = arg;

	if (test_and_clear_bit(RX_BUSY, &dws->dma_chan_busy) & BIT(TX_BUSY))
		return;
	dw_spi_xfer_done(dws);
}

static struct dma_async_tx_descriptor *dw_spi_dma_prepare_rx(struct dw_spi *dws)
{
	struct dma_slave_config rxconf;
	struct dma_async_tx_descriptor *rxdesc;

	if (!dws->rx_dma)
		return NULL;

	rxconf.direction = DMA_DEV_TO_MEM;
	rxconf.src_addr = dws->dma_addr;
	rxconf.src_maxburst = LNW_DMA_MSIZE_16;
	rxconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	rxconf.src_addr_width = dws->dma_width;
	rxconf.device_fc = false;

	dmaengine_slave_config(dws->rxchan, &rxconf);

	memset(&dws->rx_sgl, 0, sizeof(dws->rx_sgl));
	dws->rx_sgl.dma_address = dws->rx_dma;
	dws->rx_sgl.length = dws->len;

	rxdesc = dmaengine_prep_slave_sg(dws->rxchan,
				&dws->rx_sgl,
				1,
				DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxdesc)
		return NULL;

	rxdesc->callback = dw_spi_dma_rx_done;
	rxdesc->callback_param = dws;

	return rxdesc;
}

static void dw_spi_dma_setup(struct dw_spi *dws)
{
	u16 dma_ctrl = 0;

	spi_enable_chip(dws, 0);

	dw_writew(dws, DW_SPI_DMARDLR, 0xf);
	dw_writew(dws, DW_SPI_DMATDLR, 0x10);

	if (dws->tx_dma)
		dma_ctrl |= SPI_DMA_TDMAE;
	if (dws->rx_dma)
		dma_ctrl |= SPI_DMA_RDMAE;
	dw_writew(dws, DW_SPI_DMACR, dma_ctrl);

	spi_enable_chip(dws, 1);
}

static int mid_spi_dma_transfer(struct dw_spi *dws, int cs_change)
{
	struct dma_async_tx_descriptor *txdesc, *rxdesc;

	/* 1. setup DMA related registers */
	if (cs_change)
		dw_spi_dma_setup(dws);

	/* 2. Prepare the TX dma transfer */
	txdesc = dw_spi_dma_prepare_tx(dws);

	/* 3. Prepare the RX dma transfer */
	rxdesc = dw_spi_dma_prepare_rx(dws);

	/* rx must be started before tx due to spi instinct */
	if (rxdesc) {
		set_bit(RX_BUSY, &dws->dma_chan_busy);
		dmaengine_submit(rxdesc);
		dma_async_issue_pending(dws->rxchan);
	}

	if (txdesc) {
		set_bit(TX_BUSY, &dws->dma_chan_busy);
		dmaengine_submit(txdesc);
		dma_async_issue_pending(dws->txchan);
	}

	return 0;
}

static struct dw_spi_dma_ops mid_dma_ops = {
	.dma_init	= mid_spi_dma_init,
	.dma_exit	= mid_spi_dma_exit,
	.dma_transfer	= mid_spi_dma_transfer,
};
#endif

/* Some specific info for SPI0 controller on Intel MID */

/* HW info for MRST Clk Control Unit, 32b reg per controller */
#define MRST_SPI_CLK_BASE	100000000	/* 100m */
#define MRST_CLK_SPI_REG	0xff11d86c
#define CLK_SPI_BDIV_OFFSET	0
#define CLK_SPI_BDIV_MASK	0x00000007
#define CLK_SPI_CDIV_OFFSET	9
#define CLK_SPI_CDIV_MASK	0x00000e00
#define CLK_SPI_DISABLE_OFFSET	8

int dw_spi_mid_init(struct dw_spi *dws)
{
	void __iomem *clk_reg;
	u32 clk_cdiv;

	clk_reg = ioremap_nocache(MRST_CLK_SPI_REG, 16);
	if (!clk_reg)
		return -ENOMEM;

	/* Get SPI controller operating freq info */
	clk_cdiv = readl(clk_reg + dws->bus_num * sizeof(u32));
	clk_cdiv &= CLK_SPI_CDIV_MASK;
	clk_cdiv >>= CLK_SPI_CDIV_OFFSET;
	dws->max_freq = MRST_SPI_CLK_BASE / (clk_cdiv + 1);

	iounmap(clk_reg);

#ifdef CONFIG_SPI_DW_MID_DMA
	dws->dma_priv = kzalloc(sizeof(struct mid_dma), GFP_KERNEL);
	if (!dws->dma_priv)
		return -ENOMEM;
	dws->dma_ops = &mid_dma_ops;
#endif
	return 0;
}
