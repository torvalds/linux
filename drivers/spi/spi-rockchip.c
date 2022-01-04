// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Addy Ke <addy.ke@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>

#define DRIVER_NAME "rockchip-spi"

#define ROCKCHIP_SPI_CLR_BITS(reg, bits) \
		writel_relaxed(readl_relaxed(reg) & ~(bits), reg)
#define ROCKCHIP_SPI_SET_BITS(reg, bits) \
		writel_relaxed(readl_relaxed(reg) | (bits), reg)

/* SPI register offsets */
#define ROCKCHIP_SPI_CTRLR0			0x0000
#define ROCKCHIP_SPI_CTRLR1			0x0004
#define ROCKCHIP_SPI_SSIENR			0x0008
#define ROCKCHIP_SPI_SER			0x000c
#define ROCKCHIP_SPI_BAUDR			0x0010
#define ROCKCHIP_SPI_TXFTLR			0x0014
#define ROCKCHIP_SPI_RXFTLR			0x0018
#define ROCKCHIP_SPI_TXFLR			0x001c
#define ROCKCHIP_SPI_RXFLR			0x0020
#define ROCKCHIP_SPI_SR				0x0024
#define ROCKCHIP_SPI_IPR			0x0028
#define ROCKCHIP_SPI_IMR			0x002c
#define ROCKCHIP_SPI_ISR			0x0030
#define ROCKCHIP_SPI_RISR			0x0034
#define ROCKCHIP_SPI_ICR			0x0038
#define ROCKCHIP_SPI_DMACR			0x003c
#define ROCKCHIP_SPI_DMATDLR			0x0040
#define ROCKCHIP_SPI_DMARDLR			0x0044
#define ROCKCHIP_SPI_VERSION			0x0048
#define ROCKCHIP_SPI_TXDR			0x0400
#define ROCKCHIP_SPI_RXDR			0x0800

/* Bit fields in CTRLR0 */
#define CR0_DFS_OFFSET				0
#define CR0_DFS_4BIT				0x0
#define CR0_DFS_8BIT				0x1
#define CR0_DFS_16BIT				0x2

#define CR0_CFS_OFFSET				2

#define CR0_SCPH_OFFSET				6

#define CR0_SCPOL_OFFSET			7

#define CR0_CSM_OFFSET				8
#define CR0_CSM_KEEP				0x0
/* ss_n be high for half sclk_out cycles */
#define CR0_CSM_HALF				0X1
/* ss_n be high for one sclk_out cycle */
#define CR0_CSM_ONE					0x2

/* ss_n to sclk_out delay */
#define CR0_SSD_OFFSET				10
/*
 * The period between ss_n active and
 * sclk_out active is half sclk_out cycles
 */
#define CR0_SSD_HALF				0x0
/*
 * The period between ss_n active and
 * sclk_out active is one sclk_out cycle
 */
#define CR0_SSD_ONE					0x1

#define CR0_EM_OFFSET				11
#define CR0_EM_LITTLE				0x0
#define CR0_EM_BIG					0x1

#define CR0_FBM_OFFSET				12
#define CR0_FBM_MSB					0x0
#define CR0_FBM_LSB					0x1

#define CR0_BHT_OFFSET				13
#define CR0_BHT_16BIT				0x0
#define CR0_BHT_8BIT				0x1

#define CR0_RSD_OFFSET				14
#define CR0_RSD_MAX				0x3

#define CR0_FRF_OFFSET				16
#define CR0_FRF_SPI					0x0
#define CR0_FRF_SSP					0x1
#define CR0_FRF_MICROWIRE			0x2

#define CR0_XFM_OFFSET				18
#define CR0_XFM_MASK				(0x03 << SPI_XFM_OFFSET)
#define CR0_XFM_TR					0x0
#define CR0_XFM_TO					0x1
#define CR0_XFM_RO					0x2

#define CR0_OPM_OFFSET				20
#define CR0_OPM_MASTER				0x0
#define CR0_OPM_SLAVE				0x1

#define CR0_SOI_OFFSET				23

#define CR0_MTM_OFFSET				0x21

/* Bit fields in SER, 2bit */
#define SER_MASK					0x3

/* Bit fields in BAUDR */
#define BAUDR_SCKDV_MIN				2
#define BAUDR_SCKDV_MAX				65534

/* Bit fields in SR, 6bit */
#define SR_MASK						0x3f
#define SR_BUSY						(1 << 0)
#define SR_TF_FULL					(1 << 1)
#define SR_TF_EMPTY					(1 << 2)
#define SR_RF_EMPTY					(1 << 3)
#define SR_RF_FULL					(1 << 4)
#define SR_SLAVE_TX_BUSY				(1 << 5)

/* Bit fields in ISR, IMR, ISR, RISR, 5bit */
#define INT_MASK					0x1f
#define INT_TF_EMPTY				(1 << 0)
#define INT_TF_OVERFLOW				(1 << 1)
#define INT_RF_UNDERFLOW			(1 << 2)
#define INT_RF_OVERFLOW				(1 << 3)
#define INT_RF_FULL					(1 << 4)

/* Bit fields in ICR, 4bit */
#define ICR_MASK					0x0f
#define ICR_ALL						(1 << 0)
#define ICR_RF_UNDERFLOW			(1 << 1)
#define ICR_RF_OVERFLOW				(1 << 2)
#define ICR_TF_OVERFLOW				(1 << 3)

/* Bit fields in DMACR */
#define RF_DMA_EN					(1 << 0)
#define TF_DMA_EN					(1 << 1)

/* Driver state flags */
#define RXDMA					(1 << 0)
#define TXDMA					(1 << 1)

/* sclk_out: spi master internal logic in rk3x can support 50Mhz */
#define MAX_SCLK_OUT				50000000U

/*
 * SPI_CTRLR1 is 16-bits, so we should support lengths of 0xffff + 1. However,
 * the controller seems to hang when given 0x10000, so stick with this for now.
 */
#define ROCKCHIP_SPI_MAX_TRANLEN		0xffff

/* 2 for native cs, 2 for cs-gpio */
#define ROCKCHIP_SPI_MAX_CS_NUM			4
#define ROCKCHIP_SPI_VER2_TYPE1			0x05EC0002
#define ROCKCHIP_SPI_VER2_TYPE2			0x00110002

#define ROCKCHIP_AUTOSUSPEND_TIMEOUT		2000

struct rockchip_spi {
	struct device *dev;

	struct clk *spiclk;
	struct clk *apb_pclk;

	void __iomem *regs;
	dma_addr_t dma_addr_rx;
	dma_addr_t dma_addr_tx;

	const void *tx;
	void *rx;
	unsigned int tx_left;
	unsigned int rx_left;

	atomic_t state;

	/*depth of the FIFO buffer */
	u32 fifo_len;
	/* frequency of spiclk */
	u32 freq;

	u8 n_bytes;
	u8 rsd;

	bool cs_asserted[ROCKCHIP_SPI_MAX_CS_NUM];

	bool slave_abort;
};

static inline void spi_enable_chip(struct rockchip_spi *rs, bool enable)
{
	writel_relaxed((enable ? 1U : 0U), rs->regs + ROCKCHIP_SPI_SSIENR);
}

static inline void wait_for_tx_idle(struct rockchip_spi *rs, bool slave_mode)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(5);

	do {
		if (slave_mode) {
			if (!(readl_relaxed(rs->regs + ROCKCHIP_SPI_SR) & SR_SLAVE_TX_BUSY) &&
			    !((readl_relaxed(rs->regs + ROCKCHIP_SPI_SR) & SR_BUSY)))
				return;
		} else {
			if (!(readl_relaxed(rs->regs + ROCKCHIP_SPI_SR) & SR_BUSY))
				return;
		}
	} while (!time_after(jiffies, timeout));

	dev_warn(rs->dev, "spi controller is in busy state!\n");
}

static u32 get_fifo_len(struct rockchip_spi *rs)
{
	u32 ver;

	ver = readl_relaxed(rs->regs + ROCKCHIP_SPI_VERSION);

	switch (ver) {
	case ROCKCHIP_SPI_VER2_TYPE1:
	case ROCKCHIP_SPI_VER2_TYPE2:
		return 64;
	default:
		return 32;
	}
}

static void rockchip_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct spi_controller *ctlr = spi->controller;
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	bool cs_asserted = spi->mode & SPI_CS_HIGH ? enable : !enable;

	/* Return immediately for no-op */
	if (cs_asserted == rs->cs_asserted[spi->chip_select])
		return;

	if (cs_asserted) {
		/* Keep things powered as long as CS is asserted */
		pm_runtime_get_sync(rs->dev);

		if (spi->cs_gpiod)
			ROCKCHIP_SPI_SET_BITS(rs->regs + ROCKCHIP_SPI_SER, 1);
		else
			ROCKCHIP_SPI_SET_BITS(rs->regs + ROCKCHIP_SPI_SER, BIT(spi->chip_select));
	} else {
		if (spi->cs_gpiod)
			ROCKCHIP_SPI_CLR_BITS(rs->regs + ROCKCHIP_SPI_SER, 1);
		else
			ROCKCHIP_SPI_CLR_BITS(rs->regs + ROCKCHIP_SPI_SER, BIT(spi->chip_select));

		/* Drop reference from when we first asserted CS */
		pm_runtime_put(rs->dev);
	}

	rs->cs_asserted[spi->chip_select] = cs_asserted;
}

static void rockchip_spi_handle_err(struct spi_controller *ctlr,
				    struct spi_message *msg)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	/* stop running spi transfer
	 * this also flushes both rx and tx fifos
	 */
	spi_enable_chip(rs, false);

	/* make sure all interrupts are masked */
	writel_relaxed(0, rs->regs + ROCKCHIP_SPI_IMR);

	if (atomic_read(&rs->state) & TXDMA)
		dmaengine_terminate_async(ctlr->dma_tx);

	if (atomic_read(&rs->state) & RXDMA)
		dmaengine_terminate_async(ctlr->dma_rx);
}

static void rockchip_spi_pio_writer(struct rockchip_spi *rs)
{
	u32 tx_free = rs->fifo_len - readl_relaxed(rs->regs + ROCKCHIP_SPI_TXFLR);
	u32 words = min(rs->tx_left, tx_free);

	rs->tx_left -= words;
	for (; words; words--) {
		u32 txw;

		if (rs->n_bytes == 1)
			txw = *(u8 *)rs->tx;
		else
			txw = *(u16 *)rs->tx;

		writel_relaxed(txw, rs->regs + ROCKCHIP_SPI_TXDR);
		rs->tx += rs->n_bytes;
	}
}

static void rockchip_spi_pio_reader(struct rockchip_spi *rs)
{
	u32 words = readl_relaxed(rs->regs + ROCKCHIP_SPI_RXFLR);
	u32 rx_left = (rs->rx_left > words) ? rs->rx_left - words : 0;

	/* the hardware doesn't allow us to change fifo threshold
	 * level while spi is enabled, so instead make sure to leave
	 * enough words in the rx fifo to get the last interrupt
	 * exactly when all words have been received
	 */
	if (rx_left) {
		u32 ftl = readl_relaxed(rs->regs + ROCKCHIP_SPI_RXFTLR) + 1;

		if (rx_left < ftl) {
			rx_left = ftl;
			words = rs->rx_left - rx_left;
		}
	}

	rs->rx_left = rx_left;
	for (; words; words--) {
		u32 rxw = readl_relaxed(rs->regs + ROCKCHIP_SPI_RXDR);

		if (!rs->rx)
			continue;

		if (rs->n_bytes == 1)
			*(u8 *)rs->rx = (u8)rxw;
		else
			*(u16 *)rs->rx = (u16)rxw;
		rs->rx += rs->n_bytes;
	}
}

static irqreturn_t rockchip_spi_isr(int irq, void *dev_id)
{
	struct spi_controller *ctlr = dev_id;
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	if (rs->tx_left)
		rockchip_spi_pio_writer(rs);

	rockchip_spi_pio_reader(rs);
	if (!rs->rx_left) {
		spi_enable_chip(rs, false);
		writel_relaxed(0, rs->regs + ROCKCHIP_SPI_IMR);
		spi_finalize_current_transfer(ctlr);
	}

	return IRQ_HANDLED;
}

static int rockchip_spi_prepare_irq(struct rockchip_spi *rs,
		struct spi_transfer *xfer)
{
	rs->tx = xfer->tx_buf;
	rs->rx = xfer->rx_buf;
	rs->tx_left = rs->tx ? xfer->len / rs->n_bytes : 0;
	rs->rx_left = xfer->len / rs->n_bytes;

	writel_relaxed(INT_RF_FULL, rs->regs + ROCKCHIP_SPI_IMR);
	spi_enable_chip(rs, true);

	if (rs->tx_left)
		rockchip_spi_pio_writer(rs);

	/* 1 means the transfer is in progress */
	return 1;
}

static void rockchip_spi_dma_rxcb(void *data)
{
	struct spi_controller *ctlr = data;
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	int state = atomic_fetch_andnot(RXDMA, &rs->state);

	if (state & TXDMA && !rs->slave_abort)
		return;

	spi_enable_chip(rs, false);
	spi_finalize_current_transfer(ctlr);
}

static void rockchip_spi_dma_txcb(void *data)
{
	struct spi_controller *ctlr = data;
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	int state = atomic_fetch_andnot(TXDMA, &rs->state);

	if (state & RXDMA && !rs->slave_abort)
		return;

	/* Wait until the FIFO data completely. */
	wait_for_tx_idle(rs, ctlr->slave);

	spi_enable_chip(rs, false);
	spi_finalize_current_transfer(ctlr);
}

static u32 rockchip_spi_calc_burst_size(u32 data_len)
{
	u32 i;

	/* burst size: 1, 2, 4, 8 */
	for (i = 1; i < 8; i <<= 1) {
		if (data_len & i)
			break;
	}

	return i;
}

static int rockchip_spi_prepare_dma(struct rockchip_spi *rs,
		struct spi_controller *ctlr, struct spi_transfer *xfer)
{
	struct dma_async_tx_descriptor *rxdesc, *txdesc;

	atomic_set(&rs->state, 0);

	rxdesc = NULL;
	if (xfer->rx_buf) {
		struct dma_slave_config rxconf = {
			.direction = DMA_DEV_TO_MEM,
			.src_addr = rs->dma_addr_rx,
			.src_addr_width = rs->n_bytes,
			.src_maxburst = rockchip_spi_calc_burst_size(xfer->len /
								     rs->n_bytes),
		};

		dmaengine_slave_config(ctlr->dma_rx, &rxconf);

		rxdesc = dmaengine_prep_slave_sg(
				ctlr->dma_rx,
				xfer->rx_sg.sgl, xfer->rx_sg.nents,
				DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);
		if (!rxdesc)
			return -EINVAL;

		rxdesc->callback = rockchip_spi_dma_rxcb;
		rxdesc->callback_param = ctlr;
	}

	txdesc = NULL;
	if (xfer->tx_buf) {
		struct dma_slave_config txconf = {
			.direction = DMA_MEM_TO_DEV,
			.dst_addr = rs->dma_addr_tx,
			.dst_addr_width = rs->n_bytes,
			.dst_maxburst = rs->fifo_len / 4,
		};

		dmaengine_slave_config(ctlr->dma_tx, &txconf);

		txdesc = dmaengine_prep_slave_sg(
				ctlr->dma_tx,
				xfer->tx_sg.sgl, xfer->tx_sg.nents,
				DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);
		if (!txdesc) {
			if (rxdesc)
				dmaengine_terminate_sync(ctlr->dma_rx);
			return -EINVAL;
		}

		txdesc->callback = rockchip_spi_dma_txcb;
		txdesc->callback_param = ctlr;
	}

	/* rx must be started before tx due to spi instinct */
	if (rxdesc) {
		atomic_or(RXDMA, &rs->state);
		dmaengine_submit(rxdesc);
		dma_async_issue_pending(ctlr->dma_rx);
	}

	spi_enable_chip(rs, true);

	if (txdesc) {
		atomic_or(TXDMA, &rs->state);
		dmaengine_submit(txdesc);
		dma_async_issue_pending(ctlr->dma_tx);
	}

	/* 1 means the transfer is in progress */
	return 1;
}

static int rockchip_spi_config(struct rockchip_spi *rs,
		struct spi_device *spi, struct spi_transfer *xfer,
		bool use_dma, bool slave_mode)
{
	u32 cr0 = CR0_FRF_SPI  << CR0_FRF_OFFSET
		| CR0_BHT_8BIT << CR0_BHT_OFFSET
		| CR0_SSD_ONE  << CR0_SSD_OFFSET
		| CR0_EM_BIG   << CR0_EM_OFFSET;
	u32 cr1;
	u32 dmacr = 0;

	if (slave_mode)
		cr0 |= CR0_OPM_SLAVE << CR0_OPM_OFFSET;
	rs->slave_abort = false;

	cr0 |= rs->rsd << CR0_RSD_OFFSET;
	cr0 |= (spi->mode & 0x3U) << CR0_SCPH_OFFSET;
	if (spi->mode & SPI_LSB_FIRST)
		cr0 |= CR0_FBM_LSB << CR0_FBM_OFFSET;
	if (spi->mode & SPI_CS_HIGH)
		cr0 |= BIT(spi->chip_select) << CR0_SOI_OFFSET;

	if (xfer->rx_buf && xfer->tx_buf)
		cr0 |= CR0_XFM_TR << CR0_XFM_OFFSET;
	else if (xfer->rx_buf)
		cr0 |= CR0_XFM_RO << CR0_XFM_OFFSET;
	else if (use_dma)
		cr0 |= CR0_XFM_TO << CR0_XFM_OFFSET;

	switch (xfer->bits_per_word) {
	case 4:
		cr0 |= CR0_DFS_4BIT << CR0_DFS_OFFSET;
		cr1 = xfer->len - 1;
		break;
	case 8:
		cr0 |= CR0_DFS_8BIT << CR0_DFS_OFFSET;
		cr1 = xfer->len - 1;
		break;
	case 16:
		cr0 |= CR0_DFS_16BIT << CR0_DFS_OFFSET;
		cr1 = xfer->len / 2 - 1;
		break;
	default:
		/* we only whitelist 4, 8 and 16 bit words in
		 * ctlr->bits_per_word_mask, so this shouldn't
		 * happen
		 */
		dev_err(rs->dev, "unknown bits per word: %d\n",
			xfer->bits_per_word);
		return -EINVAL;
	}

	if (use_dma) {
		if (xfer->tx_buf)
			dmacr |= TF_DMA_EN;
		if (xfer->rx_buf)
			dmacr |= RF_DMA_EN;
	}

	writel_relaxed(cr0, rs->regs + ROCKCHIP_SPI_CTRLR0);
	writel_relaxed(cr1, rs->regs + ROCKCHIP_SPI_CTRLR1);

	/* unfortunately setting the fifo threshold level to generate an
	 * interrupt exactly when the fifo is full doesn't seem to work,
	 * so we need the strict inequality here
	 */
	if ((xfer->len / rs->n_bytes) < rs->fifo_len)
		writel_relaxed(xfer->len / rs->n_bytes - 1, rs->regs + ROCKCHIP_SPI_RXFTLR);
	else
		writel_relaxed(rs->fifo_len / 2 - 1, rs->regs + ROCKCHIP_SPI_RXFTLR);

	writel_relaxed(rs->fifo_len / 2 - 1, rs->regs + ROCKCHIP_SPI_DMATDLR);
	writel_relaxed(rockchip_spi_calc_burst_size(xfer->len / rs->n_bytes) - 1,
		       rs->regs + ROCKCHIP_SPI_DMARDLR);
	writel_relaxed(dmacr, rs->regs + ROCKCHIP_SPI_DMACR);

	/* the hardware only supports an even clock divisor, so
	 * round divisor = spiclk / speed up to nearest even number
	 * so that the resulting speed is <= the requested speed
	 */
	writel_relaxed(2 * DIV_ROUND_UP(rs->freq, 2 * xfer->speed_hz),
			rs->regs + ROCKCHIP_SPI_BAUDR);

	return 0;
}

static size_t rockchip_spi_max_transfer_size(struct spi_device *spi)
{
	return ROCKCHIP_SPI_MAX_TRANLEN;
}

static int rockchip_spi_slave_abort(struct spi_controller *ctlr)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	rs->slave_abort = true;
	spi_finalize_current_transfer(ctlr);

	return 0;
}

static int rockchip_spi_transfer_one(
		struct spi_controller *ctlr,
		struct spi_device *spi,
		struct spi_transfer *xfer)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	int ret;
	bool use_dma;

	WARN_ON(readl_relaxed(rs->regs + ROCKCHIP_SPI_SSIENR) &&
		(readl_relaxed(rs->regs + ROCKCHIP_SPI_SR) & SR_BUSY));

	if (!xfer->tx_buf && !xfer->rx_buf) {
		dev_err(rs->dev, "No buffer for transfer\n");
		return -EINVAL;
	}

	if (xfer->len > ROCKCHIP_SPI_MAX_TRANLEN) {
		dev_err(rs->dev, "Transfer is too long (%d)\n", xfer->len);
		return -EINVAL;
	}

	rs->n_bytes = xfer->bits_per_word <= 8 ? 1 : 2;

	use_dma = ctlr->can_dma ? ctlr->can_dma(ctlr, spi, xfer) : false;

	ret = rockchip_spi_config(rs, spi, xfer, use_dma, ctlr->slave);
	if (ret)
		return ret;

	if (use_dma)
		return rockchip_spi_prepare_dma(rs, ctlr, xfer);

	return rockchip_spi_prepare_irq(rs, xfer);
}

static bool rockchip_spi_can_dma(struct spi_controller *ctlr,
				 struct spi_device *spi,
				 struct spi_transfer *xfer)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	unsigned int bytes_per_word = xfer->bits_per_word <= 8 ? 1 : 2;

	/* if the numbor of spi words to transfer is less than the fifo
	 * length we can just fill the fifo and wait for a single irq,
	 * so don't bother setting up dma
	 */
	return xfer->len / bytes_per_word >= rs->fifo_len;
}

static int rockchip_spi_probe(struct platform_device *pdev)
{
	int ret;
	struct rockchip_spi *rs;
	struct spi_controller *ctlr;
	struct resource *mem;
	struct device_node *np = pdev->dev.of_node;
	u32 rsd_nsecs;
	bool slave_mode;

	slave_mode = of_property_read_bool(np, "spi-slave");

	if (slave_mode)
		ctlr = spi_alloc_slave(&pdev->dev,
				sizeof(struct rockchip_spi));
	else
		ctlr = spi_alloc_master(&pdev->dev,
				sizeof(struct rockchip_spi));

	if (!ctlr)
		return -ENOMEM;

	platform_set_drvdata(pdev, ctlr);

	rs = spi_controller_get_devdata(ctlr);
	ctlr->slave = slave_mode;

	/* Get basic io resource and map it */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rs->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(rs->regs)) {
		ret =  PTR_ERR(rs->regs);
		goto err_put_ctlr;
	}

	rs->apb_pclk = devm_clk_get(&pdev->dev, "apb_pclk");
	if (IS_ERR(rs->apb_pclk)) {
		dev_err(&pdev->dev, "Failed to get apb_pclk\n");
		ret = PTR_ERR(rs->apb_pclk);
		goto err_put_ctlr;
	}

	rs->spiclk = devm_clk_get(&pdev->dev, "spiclk");
	if (IS_ERR(rs->spiclk)) {
		dev_err(&pdev->dev, "Failed to get spi_pclk\n");
		ret = PTR_ERR(rs->spiclk);
		goto err_put_ctlr;
	}

	ret = clk_prepare_enable(rs->apb_pclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable apb_pclk\n");
		goto err_put_ctlr;
	}

	ret = clk_prepare_enable(rs->spiclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable spi_clk\n");
		goto err_disable_apbclk;
	}

	spi_enable_chip(rs, false);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_disable_spiclk;

	ret = devm_request_threaded_irq(&pdev->dev, ret, rockchip_spi_isr, NULL,
			IRQF_ONESHOT, dev_name(&pdev->dev), ctlr);
	if (ret)
		goto err_disable_spiclk;

	rs->dev = &pdev->dev;
	rs->freq = clk_get_rate(rs->spiclk);

	if (!of_property_read_u32(pdev->dev.of_node, "rx-sample-delay-ns",
				  &rsd_nsecs)) {
		/* rx sample delay is expressed in parent clock cycles (max 3) */
		u32 rsd = DIV_ROUND_CLOSEST(rsd_nsecs * (rs->freq >> 8),
				1000000000 >> 8);
		if (!rsd) {
			dev_warn(rs->dev, "%u Hz are too slow to express %u ns delay\n",
					rs->freq, rsd_nsecs);
		} else if (rsd > CR0_RSD_MAX) {
			rsd = CR0_RSD_MAX;
			dev_warn(rs->dev, "%u Hz are too fast to express %u ns delay, clamping at %u ns\n",
					rs->freq, rsd_nsecs,
					CR0_RSD_MAX * 1000000000U / rs->freq);
		}
		rs->rsd = rsd;
	}

	rs->fifo_len = get_fifo_len(rs);
	if (!rs->fifo_len) {
		dev_err(&pdev->dev, "Failed to get fifo length\n");
		ret = -EINVAL;
		goto err_disable_spiclk;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, ROCKCHIP_AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ctlr->auto_runtime_pm = true;
	ctlr->bus_num = pdev->id;
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LOOP | SPI_LSB_FIRST;
	if (slave_mode) {
		ctlr->mode_bits |= SPI_NO_CS;
		ctlr->slave_abort = rockchip_spi_slave_abort;
	} else {
		ctlr->flags = SPI_MASTER_GPIO_SS;
		ctlr->max_native_cs = ROCKCHIP_SPI_MAX_CS_NUM;
		/*
		 * rk spi0 has two native cs, spi1..5 one cs only
		 * if num-cs is missing in the dts, default to 1
		 */
		if (of_property_read_u16(np, "num-cs", &ctlr->num_chipselect))
			ctlr->num_chipselect = 1;
		ctlr->use_gpio_descriptors = true;
	}
	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->bits_per_word_mask = SPI_BPW_MASK(16) | SPI_BPW_MASK(8) | SPI_BPW_MASK(4);
	ctlr->min_speed_hz = rs->freq / BAUDR_SCKDV_MAX;
	ctlr->max_speed_hz = min(rs->freq / BAUDR_SCKDV_MIN, MAX_SCLK_OUT);

	ctlr->set_cs = rockchip_spi_set_cs;
	ctlr->transfer_one = rockchip_spi_transfer_one;
	ctlr->max_transfer_size = rockchip_spi_max_transfer_size;
	ctlr->handle_err = rockchip_spi_handle_err;

	ctlr->dma_tx = dma_request_chan(rs->dev, "tx");
	if (IS_ERR(ctlr->dma_tx)) {
		/* Check tx to see if we need defer probing driver */
		if (PTR_ERR(ctlr->dma_tx) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_disable_pm_runtime;
		}
		dev_warn(rs->dev, "Failed to request TX DMA channel\n");
		ctlr->dma_tx = NULL;
	}

	ctlr->dma_rx = dma_request_chan(rs->dev, "rx");
	if (IS_ERR(ctlr->dma_rx)) {
		if (PTR_ERR(ctlr->dma_rx) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_free_dma_tx;
		}
		dev_warn(rs->dev, "Failed to request RX DMA channel\n");
		ctlr->dma_rx = NULL;
	}

	if (ctlr->dma_tx && ctlr->dma_rx) {
		rs->dma_addr_tx = mem->start + ROCKCHIP_SPI_TXDR;
		rs->dma_addr_rx = mem->start + ROCKCHIP_SPI_RXDR;
		ctlr->can_dma = rockchip_spi_can_dma;
	}

	switch (readl_relaxed(rs->regs + ROCKCHIP_SPI_VERSION)) {
	case ROCKCHIP_SPI_VER2_TYPE2:
		ctlr->mode_bits |= SPI_CS_HIGH;
		break;
	default:
		break;
	}

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register controller\n");
		goto err_free_dma_rx;
	}

	return 0;

err_free_dma_rx:
	if (ctlr->dma_rx)
		dma_release_channel(ctlr->dma_rx);
err_free_dma_tx:
	if (ctlr->dma_tx)
		dma_release_channel(ctlr->dma_tx);
err_disable_pm_runtime:
	pm_runtime_disable(&pdev->dev);
err_disable_spiclk:
	clk_disable_unprepare(rs->spiclk);
err_disable_apbclk:
	clk_disable_unprepare(rs->apb_pclk);
err_put_ctlr:
	spi_controller_put(ctlr);

	return ret;
}

static int rockchip_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = spi_controller_get(platform_get_drvdata(pdev));
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	pm_runtime_get_sync(&pdev->dev);

	clk_disable_unprepare(rs->spiclk);
	clk_disable_unprepare(rs->apb_pclk);

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	if (ctlr->dma_tx)
		dma_release_channel(ctlr->dma_tx);
	if (ctlr->dma_rx)
		dma_release_channel(ctlr->dma_rx);

	spi_controller_put(ctlr);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_spi_suspend(struct device *dev)
{
	int ret;
	struct spi_controller *ctlr = dev_get_drvdata(dev);

	ret = spi_controller_suspend(ctlr);
	if (ret < 0)
		return ret;

	ret = pm_runtime_force_suspend(dev);
	if (ret < 0)
		return ret;

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int rockchip_spi_resume(struct device *dev)
{
	int ret;
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	pinctrl_pm_select_default_state(dev);

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	ret = spi_controller_resume(ctlr);
	if (ret < 0) {
		clk_disable_unprepare(rs->spiclk);
		clk_disable_unprepare(rs->apb_pclk);
	}

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int rockchip_spi_runtime_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	clk_disable_unprepare(rs->spiclk);
	clk_disable_unprepare(rs->apb_pclk);

	return 0;
}

static int rockchip_spi_runtime_resume(struct device *dev)
{
	int ret;
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	ret = clk_prepare_enable(rs->apb_pclk);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(rs->spiclk);
	if (ret < 0)
		clk_disable_unprepare(rs->apb_pclk);

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops rockchip_spi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_spi_suspend, rockchip_spi_resume)
	SET_RUNTIME_PM_OPS(rockchip_spi_runtime_suspend,
			   rockchip_spi_runtime_resume, NULL)
};

static const struct of_device_id rockchip_spi_dt_match[] = {
	{ .compatible = "rockchip,px30-spi", },
	{ .compatible = "rockchip,rk3036-spi", },
	{ .compatible = "rockchip,rk3066-spi", },
	{ .compatible = "rockchip,rk3188-spi", },
	{ .compatible = "rockchip,rk3228-spi", },
	{ .compatible = "rockchip,rk3288-spi", },
	{ .compatible = "rockchip,rk3308-spi", },
	{ .compatible = "rockchip,rk3328-spi", },
	{ .compatible = "rockchip,rk3368-spi", },
	{ .compatible = "rockchip,rk3399-spi", },
	{ .compatible = "rockchip,rv1108-spi", },
	{ .compatible = "rockchip,rv1126-spi", },
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_spi_dt_match);

static struct platform_driver rockchip_spi_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.pm = &rockchip_spi_pm,
		.of_match_table = of_match_ptr(rockchip_spi_dt_match),
	},
	.probe = rockchip_spi_probe,
	.remove = rockchip_spi_remove,
};

module_platform_driver(rockchip_spi_driver);

MODULE_AUTHOR("Addy Ke <addy.ke@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP SPI Controller Driver");
MODULE_LICENSE("GPL v2");
