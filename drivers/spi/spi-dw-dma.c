// SPDX-License-Identifier: GPL-2.0-only
/*
 * Special handling for DW DMA core
 *
 * Copyright (c) 2009, 2014 Intel Corporation.
 */

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/irqreturn.h>
#include <linux/jiffies.h>
#include <linux/pci.h>
#include <linux/platform_data/dma-dw.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "spi-dw.h"

#define WAIT_RETRIES	5
#define RX_BUSY		0
#define RX_BURST_LEVEL	16
#define TX_BUSY		1
#define TX_BURST_LEVEL	16

static bool dw_spi_dma_chan_filter(struct dma_chan *chan, void *param)
{
	struct dw_dma_slave *s = param;

	if (s->dma_dev != chan->device->dev)
		return false;

	chan->private = s;
	return true;
}

static void dw_spi_dma_maxburst_init(struct dw_spi *dws)
{
	struct dma_slave_caps caps;
	u32 max_burst, def_burst;
	int ret;

	def_burst = dws->fifo_len / 2;

	ret = dma_get_slave_caps(dws->rxchan, &caps);
	if (!ret && caps.max_burst)
		max_burst = caps.max_burst;
	else
		max_burst = RX_BURST_LEVEL;

	dws->rxburst = min(max_burst, def_burst);

	ret = dma_get_slave_caps(dws->txchan, &caps);
	if (!ret && caps.max_burst)
		max_burst = caps.max_burst;
	else
		max_burst = TX_BURST_LEVEL;

	dws->txburst = min(max_burst, def_burst);
}

static int dw_spi_dma_init_mfld(struct device *dev, struct dw_spi *dws)
{
	struct dw_dma_slave dma_tx = { .dst_id = 1 }, *tx = &dma_tx;
	struct dw_dma_slave dma_rx = { .src_id = 0 }, *rx = &dma_rx;
	struct pci_dev *dma_dev;
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
	dws->rxchan = dma_request_channel(mask, dw_spi_dma_chan_filter, rx);
	if (!dws->rxchan)
		goto err_exit;

	/* 2. Init tx channel */
	tx->dma_dev = &dma_dev->dev;
	dws->txchan = dma_request_channel(mask, dw_spi_dma_chan_filter, tx);
	if (!dws->txchan)
		goto free_rxchan;

	dws->master->dma_rx = dws->rxchan;
	dws->master->dma_tx = dws->txchan;

	init_completion(&dws->dma_completion);

	dw_spi_dma_maxburst_init(dws);

	return 0;

free_rxchan:
	dma_release_channel(dws->rxchan);
	dws->rxchan = NULL;
err_exit:
	return -EBUSY;
}

static int dw_spi_dma_init_generic(struct device *dev, struct dw_spi *dws)
{
	dws->rxchan = dma_request_slave_channel(dev, "rx");
	if (!dws->rxchan)
		return -ENODEV;

	dws->txchan = dma_request_slave_channel(dev, "tx");
	if (!dws->txchan) {
		dma_release_channel(dws->rxchan);
		dws->rxchan = NULL;
		return -ENODEV;
	}

	dws->master->dma_rx = dws->rxchan;
	dws->master->dma_tx = dws->txchan;

	init_completion(&dws->dma_completion);

	dw_spi_dma_maxburst_init(dws);

	return 0;
}

static void dw_spi_dma_exit(struct dw_spi *dws)
{
	if (dws->txchan) {
		dmaengine_terminate_sync(dws->txchan);
		dma_release_channel(dws->txchan);
	}

	if (dws->rxchan) {
		dmaengine_terminate_sync(dws->rxchan);
		dma_release_channel(dws->rxchan);
	}

	dw_writel(dws, DW_SPI_DMACR, 0);
}

static irqreturn_t dw_spi_dma_transfer_handler(struct dw_spi *dws)
{
	u16 irq_status = dw_readl(dws, DW_SPI_ISR);

	if (!irq_status)
		return IRQ_NONE;

	dw_readl(dws, DW_SPI_ICR);
	spi_reset_chip(dws);

	dev_err(&dws->master->dev, "%s: FIFO overrun/underrun\n", __func__);
	dws->master->cur_msg->status = -EIO;
	complete(&dws->dma_completion);
	return IRQ_HANDLED;
}

static bool dw_spi_can_dma(struct spi_controller *master,
			   struct spi_device *spi, struct spi_transfer *xfer)
{
	struct dw_spi *dws = spi_controller_get_devdata(master);

	return xfer->len > dws->fifo_len;
}

static enum dma_slave_buswidth dw_spi_dma_convert_width(u8 n_bytes)
{
	if (n_bytes == 1)
		return DMA_SLAVE_BUSWIDTH_1_BYTE;
	else if (n_bytes == 2)
		return DMA_SLAVE_BUSWIDTH_2_BYTES;

	return DMA_SLAVE_BUSWIDTH_UNDEFINED;
}

static int dw_spi_dma_wait(struct dw_spi *dws, struct spi_transfer *xfer)
{
	unsigned long long ms;

	ms = xfer->len * MSEC_PER_SEC * BITS_PER_BYTE;
	do_div(ms, xfer->effective_speed_hz);
	ms += ms + 200;

	if (ms > UINT_MAX)
		ms = UINT_MAX;

	ms = wait_for_completion_timeout(&dws->dma_completion,
					 msecs_to_jiffies(ms));

	if (ms == 0) {
		dev_err(&dws->master->cur_msg->spi->dev,
			"DMA transaction timed out\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static inline bool dw_spi_dma_tx_busy(struct dw_spi *dws)
{
	return !(dw_readl(dws, DW_SPI_SR) & SR_TF_EMPT);
}

static int dw_spi_dma_wait_tx_done(struct dw_spi *dws,
				   struct spi_transfer *xfer)
{
	int retry = WAIT_RETRIES;
	struct spi_delay delay;
	u32 nents;

	nents = dw_readl(dws, DW_SPI_TXFLR);
	delay.unit = SPI_DELAY_UNIT_SCK;
	delay.value = nents * dws->n_bytes * BITS_PER_BYTE;

	while (dw_spi_dma_tx_busy(dws) && retry--)
		spi_delay_exec(&delay, xfer);

	if (retry < 0) {
		dev_err(&dws->master->dev, "Tx hanged up\n");
		return -EIO;
	}

	return 0;
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

	dw_writel(dws, DW_SPI_DMACR, 0);
	complete(&dws->dma_completion);
}

static struct dma_async_tx_descriptor *
dw_spi_dma_prepare_tx(struct dw_spi *dws, struct spi_transfer *xfer)
{
	struct dma_slave_config txconf;
	struct dma_async_tx_descriptor *txdesc;

	if (!xfer->tx_buf)
		return NULL;

	memset(&txconf, 0, sizeof(txconf));
	txconf.direction = DMA_MEM_TO_DEV;
	txconf.dst_addr = dws->dma_addr;
	txconf.dst_maxburst = dws->txburst;
	txconf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	txconf.dst_addr_width = dw_spi_dma_convert_width(dws->n_bytes);
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

static inline bool dw_spi_dma_rx_busy(struct dw_spi *dws)
{
	return !!(dw_readl(dws, DW_SPI_SR) & SR_RF_NOT_EMPT);
}

static int dw_spi_dma_wait_rx_done(struct dw_spi *dws)
{
	int retry = WAIT_RETRIES;
	struct spi_delay delay;
	unsigned long ns, us;
	u32 nents;

	/*
	 * It's unlikely that DMA engine is still doing the data fetching, but
	 * if it's let's give it some reasonable time. The timeout calculation
	 * is based on the synchronous APB/SSI reference clock rate, on a
	 * number of data entries left in the Rx FIFO, times a number of clock
	 * periods normally needed for a single APB read/write transaction
	 * without PREADY signal utilized (which is true for the DW APB SSI
	 * controller).
	 */
	nents = dw_readl(dws, DW_SPI_RXFLR);
	ns = 4U * NSEC_PER_SEC / dws->max_freq * nents;
	if (ns <= NSEC_PER_USEC) {
		delay.unit = SPI_DELAY_UNIT_NSECS;
		delay.value = ns;
	} else {
		us = DIV_ROUND_UP(ns, NSEC_PER_USEC);
		delay.unit = SPI_DELAY_UNIT_USECS;
		delay.value = clamp_val(us, 0, USHRT_MAX);
	}

	while (dw_spi_dma_rx_busy(dws) && retry--)
		spi_delay_exec(&delay, NULL);

	if (retry < 0) {
		dev_err(&dws->master->dev, "Rx hanged up\n");
		return -EIO;
	}

	return 0;
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

	dw_writel(dws, DW_SPI_DMACR, 0);
	complete(&dws->dma_completion);
}

static struct dma_async_tx_descriptor *dw_spi_dma_prepare_rx(struct dw_spi *dws,
		struct spi_transfer *xfer)
{
	struct dma_slave_config rxconf;
	struct dma_async_tx_descriptor *rxdesc;

	if (!xfer->rx_buf)
		return NULL;

	memset(&rxconf, 0, sizeof(rxconf));
	rxconf.direction = DMA_DEV_TO_MEM;
	rxconf.src_addr = dws->dma_addr;
	rxconf.src_maxburst = dws->rxburst;
	rxconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	rxconf.src_addr_width = dw_spi_dma_convert_width(dws->n_bytes);
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

static int dw_spi_dma_setup(struct dw_spi *dws, struct spi_transfer *xfer)
{
	u16 imr = 0, dma_ctrl = 0;

	/*
	 * Having a Rx DMA channel serviced with higher priority than a Tx DMA
	 * channel might not be enough to provide a well balanced DMA-based
	 * SPI transfer interface. There might still be moments when the Tx DMA
	 * channel is occasionally handled faster than the Rx DMA channel.
	 * That in its turn will eventually cause the SPI Rx FIFO overflow if
	 * SPI bus speed is high enough to fill the SPI Rx FIFO in before it's
	 * cleared by the Rx DMA channel. In order to fix the problem the Tx
	 * DMA activity is intentionally slowed down by limiting the SPI Tx
	 * FIFO depth with a value twice bigger than the Tx burst length
	 * calculated earlier by the dw_spi_dma_maxburst_init() method.
	 */
	dw_writel(dws, DW_SPI_DMARDLR, dws->rxburst - 1);
	dw_writel(dws, DW_SPI_DMATDLR, dws->txburst);

	if (xfer->tx_buf)
		dma_ctrl |= SPI_DMA_TDMAE;
	if (xfer->rx_buf)
		dma_ctrl |= SPI_DMA_RDMAE;
	dw_writel(dws, DW_SPI_DMACR, dma_ctrl);

	/* Set the interrupt mask */
	if (xfer->tx_buf)
		imr |= SPI_INT_TXOI;
	if (xfer->rx_buf)
		imr |= SPI_INT_RXUI | SPI_INT_RXOI;
	spi_umask_intr(dws, imr);

	reinit_completion(&dws->dma_completion);

	dws->transfer_handler = dw_spi_dma_transfer_handler;

	return 0;
}

static int dw_spi_dma_transfer(struct dw_spi *dws, struct spi_transfer *xfer)
{
	struct dma_async_tx_descriptor *txdesc, *rxdesc;
	int ret;

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

	ret = dw_spi_dma_wait(dws, xfer);
	if (ret)
		return ret;

	if (txdesc && dws->master->cur_msg->status == -EINPROGRESS) {
		ret = dw_spi_dma_wait_tx_done(dws, xfer);
		if (ret)
			return ret;
	}

	if (rxdesc && dws->master->cur_msg->status == -EINPROGRESS)
		ret = dw_spi_dma_wait_rx_done(dws);

	return ret;
}

static void dw_spi_dma_stop(struct dw_spi *dws)
{
	if (test_bit(TX_BUSY, &dws->dma_chan_busy)) {
		dmaengine_terminate_sync(dws->txchan);
		clear_bit(TX_BUSY, &dws->dma_chan_busy);
	}
	if (test_bit(RX_BUSY, &dws->dma_chan_busy)) {
		dmaengine_terminate_sync(dws->rxchan);
		clear_bit(RX_BUSY, &dws->dma_chan_busy);
	}

	dw_writel(dws, DW_SPI_DMACR, 0);
}

static const struct dw_spi_dma_ops dw_spi_dma_mfld_ops = {
	.dma_init	= dw_spi_dma_init_mfld,
	.dma_exit	= dw_spi_dma_exit,
	.dma_setup	= dw_spi_dma_setup,
	.can_dma	= dw_spi_can_dma,
	.dma_transfer	= dw_spi_dma_transfer,
	.dma_stop	= dw_spi_dma_stop,
};

void dw_spi_dma_setup_mfld(struct dw_spi *dws)
{
	dws->dma_ops = &dw_spi_dma_mfld_ops;
}
EXPORT_SYMBOL_GPL(dw_spi_dma_setup_mfld);

static const struct dw_spi_dma_ops dw_spi_dma_generic_ops = {
	.dma_init	= dw_spi_dma_init_generic,
	.dma_exit	= dw_spi_dma_exit,
	.dma_setup	= dw_spi_dma_setup,
	.can_dma	= dw_spi_can_dma,
	.dma_transfer	= dw_spi_dma_transfer,
	.dma_stop	= dw_spi_dma_stop,
};

void dw_spi_dma_setup_generic(struct dw_spi *dws)
{
	dws->dma_ops = &dw_spi_dma_generic_ops;
}
EXPORT_SYMBOL_GPL(dw_spi_dma_setup_generic);
