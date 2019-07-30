// SPDX-License-Identifier: GPL-2.0-only
/*
 * Special handling for DW core on Intel MID platform
 *
 * Copyright (c) 2009, 2014 Intel Corporation.
 */

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "spi-dw.h"

#ifdef CONFIG_SPI_DW_MID_DMA
#include <linux/pci.h>
#include <linux/platform_data/dma-dw.h>

#define RX_BUSY		0
#define TX_BUSY		1

static struct dw_dma_slave mid_dma_tx = { .dst_id = 1 };
static struct dw_dma_slave mid_dma_rx = { .src_id = 0 };

static bool mid_spi_dma_chan_filter(struct dma_chan *chan, void *param)
{
	struct dw_dma_slave *s = param;

	if (s->dma_dev != chan->device->dev)
		return false;

	chan->private = s;
	return true;
}

static int mid_spi_dma_init(struct dw_spi *dws)
{
	struct pci_dev *dma_dev;
	struct dw_dma_slave *tx = dws->dma_tx;
	struct dw_dma_slave *rx = dws->dma_rx;
	dma_cap_mask_t mask;

	/*
	 * Get pci device for DMA controller, currently it could only
	 * be the DMA controller of Medfield
	 */
	dma_dev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x0827, NULL);
	if (!dma_dev)
		return -ENODEV;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* 1. Init rx channel */
	rx->dma_dev = &dma_dev->dev;
	dws->rxchan = dma_request_channel(mask, mid_spi_dma_chan_filter, rx);
	if (!dws->rxchan)
		goto err_exit;
	dws->master->dma_rx = dws->rxchan;

	/* 2. Init tx channel */
	tx->dma_dev = &dma_dev->dev;
	dws->txchan = dma_request_channel(mask, mid_spi_dma_chan_filter, tx);
	if (!dws->txchan)
		goto free_rxchan;
	dws->master->dma_tx = dws->txchan;

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

	dmaengine_terminate_sync(dws->txchan);
	dma_release_channel(dws->txchan);

	dmaengine_terminate_sync(dws->rxchan);
	dma_release_channel(dws->rxchan);
}

static irqreturn_t dma_transfer(struct dw_spi *dws)
{
	u16 irq_status = dw_readl(dws, DW_SPI_ISR);

	if (!irq_status)
		return IRQ_NONE;

	dw_readl(dws, DW_SPI_ICR);
	spi_reset_chip(dws);

	dev_err(&dws->master->dev, "%s: FIFO overrun/underrun\n", __func__);
	dws->master->cur_msg->status = -EIO;
	spi_finalize_current_transfer(dws->master);
	return IRQ_HANDLED;
}

static bool mid_spi_can_dma(struct spi_controller *master,
		struct spi_device *spi, struct spi_transfer *xfer)
{
	struct dw_spi *dws = spi_controller_get_devdata(master);

	if (!dws->dma_inited)
		return false;

	return xfer->len > dws->fifo_len;
}

static enum dma_slave_buswidth convert_dma_width(u32 dma_width) {
	if (dma_width == 1)
		return DMA_SLAVE_BUSWIDTH_1_BYTE;
	else if (dma_width == 2)
		return DMA_SLAVE_BUSWIDTH_2_BYTES;

	return DMA_SLAVE_BUSWIDTH_UNDEFINED;
}

/*
 * dws->dma_chan_busy is set before the dma transfer starts, callback for tx
 * channel will clear a corresponding bit.
 */
static void dw_spi_dma_tx_done(void *arg)
{
	struct dw_spi *dws = arg;

	clear_bit(TX_BUSY, &dws->dma_chan_busy);
	if (test_bit(RX_BUSY, &dws->dma_chan_busy))
		return;
	spi_finalize_current_transfer(dws->master);
}

static struct dma_async_tx_descriptor *dw_spi_dma_prepare_tx(struct dw_spi *dws,
		struct spi_transfer *xfer)
{
	struct dma_slave_config txconf;
	struct dma_async_tx_descriptor *txdesc;

	if (!xfer->tx_buf)
		return NULL;

	txconf.direction = DMA_MEM_TO_DEV;
	txconf.dst_addr = dws->dma_addr;
	txconf.dst_maxburst = 16;
	txconf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	txconf.dst_addr_width = convert_dma_width(dws->dma_width);
	txconf.device_fc = false;

	dmaengine_slave_config(dws->txchan, &txconf);

	txdesc = dmaengine_prep_slave_sg(dws->txchan,
				xfer->tx_sg.sgl,
				xfer->tx_sg.nents,
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

	clear_bit(RX_BUSY, &dws->dma_chan_busy);
	if (test_bit(TX_BUSY, &dws->dma_chan_busy))
		return;
	spi_finalize_current_transfer(dws->master);
}

static struct dma_async_tx_descriptor *dw_spi_dma_prepare_rx(struct dw_spi *dws,
		struct spi_transfer *xfer)
{
	struct dma_slave_config rxconf;
	struct dma_async_tx_descriptor *rxdesc;

	if (!xfer->rx_buf)
		return NULL;

	rxconf.direction = DMA_DEV_TO_MEM;
	rxconf.src_addr = dws->dma_addr;
	rxconf.src_maxburst = 16;
	rxconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	rxconf.src_addr_width = convert_dma_width(dws->dma_width);
	rxconf.device_fc = false;

	dmaengine_slave_config(dws->rxchan, &rxconf);

	rxdesc = dmaengine_prep_slave_sg(dws->rxchan,
				xfer->rx_sg.sgl,
				xfer->rx_sg.nents,
				DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxdesc)
		return NULL;

	rxdesc->callback = dw_spi_dma_rx_done;
	rxdesc->callback_param = dws;

	return rxdesc;
}

static int mid_spi_dma_setup(struct dw_spi *dws, struct spi_transfer *xfer)
{
	u16 dma_ctrl = 0;

	dw_writel(dws, DW_SPI_DMARDLR, 0xf);
	dw_writel(dws, DW_SPI_DMATDLR, 0x10);

	if (xfer->tx_buf)
		dma_ctrl |= SPI_DMA_TDMAE;
	if (xfer->rx_buf)
		dma_ctrl |= SPI_DMA_RDMAE;
	dw_writel(dws, DW_SPI_DMACR, dma_ctrl);

	/* Set the interrupt mask */
	spi_umask_intr(dws, SPI_INT_TXOI | SPI_INT_RXUI | SPI_INT_RXOI);

	dws->transfer_handler = dma_transfer;

	return 0;
}

static int mid_spi_dma_transfer(struct dw_spi *dws, struct spi_transfer *xfer)
{
	struct dma_async_tx_descriptor *txdesc, *rxdesc;

	/* Prepare the TX dma transfer */
	txdesc = dw_spi_dma_prepare_tx(dws, xfer);

	/* Prepare the RX dma transfer */
	rxdesc = dw_spi_dma_prepare_rx(dws, xfer);

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

static void mid_spi_dma_stop(struct dw_spi *dws)
{
	if (test_bit(TX_BUSY, &dws->dma_chan_busy)) {
		dmaengine_terminate_sync(dws->txchan);
		clear_bit(TX_BUSY, &dws->dma_chan_busy);
	}
	if (test_bit(RX_BUSY, &dws->dma_chan_busy)) {
		dmaengine_terminate_sync(dws->rxchan);
		clear_bit(RX_BUSY, &dws->dma_chan_busy);
	}
}

static const struct dw_spi_dma_ops mid_dma_ops = {
	.dma_init	= mid_spi_dma_init,
	.dma_exit	= mid_spi_dma_exit,
	.dma_setup	= mid_spi_dma_setup,
	.can_dma	= mid_spi_can_dma,
	.dma_transfer	= mid_spi_dma_transfer,
	.dma_stop	= mid_spi_dma_stop,
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
	dws->dma_tx = &mid_dma_tx;
	dws->dma_rx = &mid_dma_rx;
	dws->dma_ops = &mid_dma_ops;
#endif
	return 0;
}
