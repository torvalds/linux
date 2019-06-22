/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Addy Ke <addy.ke@rock-chips.com>
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
#define ROCKCHIP_SPI_DMATDLR		0x0040
#define ROCKCHIP_SPI_DMARDLR		0x0044
#define ROCKCHIP_SPI_TXDR			0x0400
#define ROCKCHIP_SPI_RXDR			0x0800

/* Bit fields in CTRLR0 */
#define CR0_DFS_OFFSET				0

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

#define CR0_MTM_OFFSET				0x21

/* Bit fields in SER, 2bit */
#define SER_MASK					0x3

/* Bit fields in SR, 5bit */
#define SR_MASK						0x1f
#define SR_BUSY						(1 << 0)
#define SR_TF_FULL					(1 << 1)
#define SR_TF_EMPTY					(1 << 2)
#define SR_RF_EMPTY					(1 << 3)
#define SR_RF_FULL					(1 << 4)

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

#define RXBUSY						(1 << 0)
#define TXBUSY						(1 << 1)

/* sclk_out: spi master internal logic in rk3x can support 50Mhz */
#define MAX_SCLK_OUT		50000000

/*
 * SPI_CTRLR1 is 16-bits, so we should support lengths of 0xffff + 1. However,
 * the controller seems to hang when given 0x10000, so stick with this for now.
 */
#define ROCKCHIP_SPI_MAX_TRANLEN		0xffff

#define ROCKCHIP_SPI_MAX_CS_NUM			2

enum rockchip_ssi_type {
	SSI_MOTO_SPI = 0,
	SSI_TI_SSP,
	SSI_NS_MICROWIRE,
};

struct rockchip_spi_dma_data {
	struct dma_chan *ch;
	enum dma_transfer_direction direction;
	dma_addr_t addr;
};

struct rockchip_spi {
	struct device *dev;
	struct spi_master *master;

	struct clk *spiclk;
	struct clk *apb_pclk;

	void __iomem *regs;
	/*depth of the FIFO buffer */
	u32 fifo_len;
	/* max bus freq supported */
	u32 max_freq;
	/* supported slave numbers */
	enum rockchip_ssi_type type;

	u16 mode;
	u8 tmode;
	u8 bpw;
	u8 n_bytes;
	u32 rsd_nsecs;
	unsigned len;
	u32 speed;

	const void *tx;
	const void *tx_end;
	void *rx;
	void *rx_end;

	u32 state;
	/* protect state */
	spinlock_t lock;

	bool cs_asserted[ROCKCHIP_SPI_MAX_CS_NUM];

	u32 use_dma;
	struct sg_table tx_sg;
	struct sg_table rx_sg;
	struct rockchip_spi_dma_data dma_rx;
	struct rockchip_spi_dma_data dma_tx;
	struct dma_slave_caps dma_caps;
};

static inline void spi_enable_chip(struct rockchip_spi *rs, int enable)
{
	writel_relaxed((enable ? 1 : 0), rs->regs + ROCKCHIP_SPI_SSIENR);
}

static inline void spi_set_clk(struct rockchip_spi *rs, u16 div)
{
	writel_relaxed(div, rs->regs + ROCKCHIP_SPI_BAUDR);
}

static inline void flush_fifo(struct rockchip_spi *rs)
{
	while (readl_relaxed(rs->regs + ROCKCHIP_SPI_RXFLR))
		readl_relaxed(rs->regs + ROCKCHIP_SPI_RXDR);
}

static inline void wait_for_idle(struct rockchip_spi *rs)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(5);

	do {
		if (!(readl_relaxed(rs->regs + ROCKCHIP_SPI_SR) & SR_BUSY))
			return;
	} while (!time_after(jiffies, timeout));

	dev_warn(rs->dev, "spi controller is in busy state!\n");
}

static u32 get_fifo_len(struct rockchip_spi *rs)
{
	u32 fifo;

	for (fifo = 2; fifo < 32; fifo++) {
		writel_relaxed(fifo, rs->regs + ROCKCHIP_SPI_TXFTLR);
		if (fifo != readl_relaxed(rs->regs + ROCKCHIP_SPI_TXFTLR))
			break;
	}

	writel_relaxed(0, rs->regs + ROCKCHIP_SPI_TXFTLR);

	return (fifo == 31) ? 0 : fifo;
}

static inline u32 tx_max(struct rockchip_spi *rs)
{
	u32 tx_left, tx_room;

	tx_left = (rs->tx_end - rs->tx) / rs->n_bytes;
	tx_room = rs->fifo_len - readl_relaxed(rs->regs + ROCKCHIP_SPI_TXFLR);

	return min(tx_left, tx_room);
}

static inline u32 rx_max(struct rockchip_spi *rs)
{
	u32 rx_left = (rs->rx_end - rs->rx) / rs->n_bytes;
	u32 rx_room = (u32)readl_relaxed(rs->regs + ROCKCHIP_SPI_RXFLR);

	return min(rx_left, rx_room);
}

static void rockchip_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct spi_master *master = spi->master;
	struct rockchip_spi *rs = spi_master_get_devdata(master);
	bool cs_asserted = !enable;

	/* Return immediately for no-op */
	if (cs_asserted == rs->cs_asserted[spi->chip_select])
		return;

	if (cs_asserted) {
		/* Keep things powered as long as CS is asserted */
		pm_runtime_get_sync(rs->dev);

		ROCKCHIP_SPI_SET_BITS(rs->regs + ROCKCHIP_SPI_SER,
				      BIT(spi->chip_select));
	} else {
		ROCKCHIP_SPI_CLR_BITS(rs->regs + ROCKCHIP_SPI_SER,
				      BIT(spi->chip_select));

		/* Drop reference from when we first asserted CS */
		pm_runtime_put(rs->dev);
	}

	rs->cs_asserted[spi->chip_select] = cs_asserted;
}

static int rockchip_spi_prepare_message(struct spi_master *master,
					struct spi_message *msg)
{
	struct rockchip_spi *rs = spi_master_get_devdata(master);
	struct spi_device *spi = msg->spi;

	rs->mode = spi->mode;

	return 0;
}

static void rockchip_spi_handle_err(struct spi_master *master,
				    struct spi_message *msg)
{
	unsigned long flags;
	struct rockchip_spi *rs = spi_master_get_devdata(master);

	spin_lock_irqsave(&rs->lock, flags);

	/*
	 * For DMA mode, we need terminate DMA channel and flush
	 * fifo for the next transfer if DMA thansfer timeout.
	 * handle_err() was called by core if transfer failed.
	 * Maybe it is reasonable for error handling here.
	 */
	if (rs->use_dma) {
		if (rs->state & RXBUSY) {
			dmaengine_terminate_async(rs->dma_rx.ch);
			flush_fifo(rs);
		}

		if (rs->state & TXBUSY)
			dmaengine_terminate_async(rs->dma_tx.ch);
	}

	spin_unlock_irqrestore(&rs->lock, flags);
}

static int rockchip_spi_unprepare_message(struct spi_master *master,
					  struct spi_message *msg)
{
	struct rockchip_spi *rs = spi_master_get_devdata(master);

	spi_enable_chip(rs, 0);

	return 0;
}

static void rockchip_spi_pio_writer(struct rockchip_spi *rs)
{
	u32 max = tx_max(rs);
	u32 txw = 0;

	while (max--) {
		if (rs->n_bytes == 1)
			txw = *(u8 *)(rs->tx);
		else
			txw = *(u16 *)(rs->tx);

		writel_relaxed(txw, rs->regs + ROCKCHIP_SPI_TXDR);
		rs->tx += rs->n_bytes;
	}
}

static void rockchip_spi_pio_reader(struct rockchip_spi *rs)
{
	u32 max = rx_max(rs);
	u32 rxw;

	while (max--) {
		rxw = readl_relaxed(rs->regs + ROCKCHIP_SPI_RXDR);
		if (rs->n_bytes == 1)
			*(u8 *)(rs->rx) = (u8)rxw;
		else
			*(u16 *)(rs->rx) = (u16)rxw;
		rs->rx += rs->n_bytes;
	}
}

static int rockchip_spi_pio_transfer(struct rockchip_spi *rs)
{
	int remain = 0;

	do {
		if (rs->tx) {
			remain = rs->tx_end - rs->tx;
			rockchip_spi_pio_writer(rs);
		}

		if (rs->rx) {
			remain = rs->rx_end - rs->rx;
			rockchip_spi_pio_reader(rs);
		}

		cpu_relax();
	} while (remain);

	/* If tx, wait until the FIFO data completely. */
	if (rs->tx)
		wait_for_idle(rs);

	spi_enable_chip(rs, 0);

	return 0;
}

static void rockchip_spi_dma_rxcb(void *data)
{
	unsigned long flags;
	struct rockchip_spi *rs = data;

	spin_lock_irqsave(&rs->lock, flags);

	rs->state &= ~RXBUSY;
	if (!(rs->state & TXBUSY)) {
		spi_enable_chip(rs, 0);
		spi_finalize_current_transfer(rs->master);
	}

	spin_unlock_irqrestore(&rs->lock, flags);
}

static void rockchip_spi_dma_txcb(void *data)
{
	unsigned long flags;
	struct rockchip_spi *rs = data;

	/* Wait until the FIFO data completely. */
	wait_for_idle(rs);

	spin_lock_irqsave(&rs->lock, flags);

	rs->state &= ~TXBUSY;
	if (!(rs->state & RXBUSY)) {
		spi_enable_chip(rs, 0);
		spi_finalize_current_transfer(rs->master);
	}

	spin_unlock_irqrestore(&rs->lock, flags);
}

static int rockchip_spi_prepare_dma(struct rockchip_spi *rs)
{
	unsigned long flags;
	struct dma_slave_config rxconf, txconf;
	struct dma_async_tx_descriptor *rxdesc, *txdesc;

	spin_lock_irqsave(&rs->lock, flags);
	rs->state &= ~RXBUSY;
	rs->state &= ~TXBUSY;
	spin_unlock_irqrestore(&rs->lock, flags);

	rxdesc = NULL;
	if (rs->rx) {
		rxconf.direction = rs->dma_rx.direction;
		rxconf.src_addr = rs->dma_rx.addr;
		rxconf.src_addr_width = rs->n_bytes;
		if (rs->dma_caps.max_burst > 4)
			rxconf.src_maxburst = 4;
		else
			rxconf.src_maxburst = 1;
		dmaengine_slave_config(rs->dma_rx.ch, &rxconf);

		rxdesc = dmaengine_prep_slave_sg(
				rs->dma_rx.ch,
				rs->rx_sg.sgl, rs->rx_sg.nents,
				rs->dma_rx.direction, DMA_PREP_INTERRUPT);
		if (!rxdesc)
			return -EINVAL;

		rxdesc->callback = rockchip_spi_dma_rxcb;
		rxdesc->callback_param = rs;
	}

	txdesc = NULL;
	if (rs->tx) {
		txconf.direction = rs->dma_tx.direction;
		txconf.dst_addr = rs->dma_tx.addr;
		txconf.dst_addr_width = rs->n_bytes;
		if (rs->dma_caps.max_burst > 4)
			txconf.dst_maxburst = 4;
		else
			txconf.dst_maxburst = 1;
		dmaengine_slave_config(rs->dma_tx.ch, &txconf);

		txdesc = dmaengine_prep_slave_sg(
				rs->dma_tx.ch,
				rs->tx_sg.sgl, rs->tx_sg.nents,
				rs->dma_tx.direction, DMA_PREP_INTERRUPT);
		if (!txdesc) {
			if (rxdesc)
				dmaengine_terminate_sync(rs->dma_rx.ch);
			return -EINVAL;
		}

		txdesc->callback = rockchip_spi_dma_txcb;
		txdesc->callback_param = rs;
	}

	/* rx must be started before tx due to spi instinct */
	if (rxdesc) {
		spin_lock_irqsave(&rs->lock, flags);
		rs->state |= RXBUSY;
		spin_unlock_irqrestore(&rs->lock, flags);
		dmaengine_submit(rxdesc);
		dma_async_issue_pending(rs->dma_rx.ch);
	}

	if (txdesc) {
		spin_lock_irqsave(&rs->lock, flags);
		rs->state |= TXBUSY;
		spin_unlock_irqrestore(&rs->lock, flags);
		dmaengine_submit(txdesc);
		dma_async_issue_pending(rs->dma_tx.ch);
	}

	return 0;
}

static void rockchip_spi_config(struct rockchip_spi *rs)
{
	u32 div = 0;
	u32 dmacr = 0;
	int rsd = 0;

	u32 cr0 = (CR0_BHT_8BIT << CR0_BHT_OFFSET)
		| (CR0_SSD_ONE << CR0_SSD_OFFSET)
		| (CR0_EM_BIG << CR0_EM_OFFSET);

	cr0 |= (rs->n_bytes << CR0_DFS_OFFSET);
	cr0 |= ((rs->mode & 0x3) << CR0_SCPH_OFFSET);
	cr0 |= (rs->tmode << CR0_XFM_OFFSET);
	cr0 |= (rs->type << CR0_FRF_OFFSET);

	if (rs->use_dma) {
		if (rs->tx)
			dmacr |= TF_DMA_EN;
		if (rs->rx)
			dmacr |= RF_DMA_EN;
	}

	if (WARN_ON(rs->speed > MAX_SCLK_OUT))
		rs->speed = MAX_SCLK_OUT;

	/* the minimum divisor is 2 */
	if (rs->max_freq < 2 * rs->speed) {
		clk_set_rate(rs->spiclk, 2 * rs->speed);
		rs->max_freq = clk_get_rate(rs->spiclk);
	}

	/* div doesn't support odd number */
	div = DIV_ROUND_UP(rs->max_freq, rs->speed);
	div = (div + 1) & 0xfffe;

	/* Rx sample delay is expressed in parent clock cycles (max 3) */
	rsd = DIV_ROUND_CLOSEST(rs->rsd_nsecs * (rs->max_freq >> 8),
				1000000000 >> 8);
	if (!rsd && rs->rsd_nsecs) {
		pr_warn_once("rockchip-spi: %u Hz are too slow to express %u ns delay\n",
			     rs->max_freq, rs->rsd_nsecs);
	} else if (rsd > 3) {
		rsd = 3;
		pr_warn_once("rockchip-spi: %u Hz are too fast to express %u ns delay, clamping at %u ns\n",
			     rs->max_freq, rs->rsd_nsecs,
			     rsd * 1000000000U / rs->max_freq);
	}
	cr0 |= rsd << CR0_RSD_OFFSET;

	writel_relaxed(cr0, rs->regs + ROCKCHIP_SPI_CTRLR0);

	if (rs->n_bytes == 1)
		writel_relaxed(rs->len - 1, rs->regs + ROCKCHIP_SPI_CTRLR1);
	else if (rs->n_bytes == 2)
		writel_relaxed((rs->len / 2) - 1, rs->regs + ROCKCHIP_SPI_CTRLR1);
	else
		writel_relaxed((rs->len * 2) - 1, rs->regs + ROCKCHIP_SPI_CTRLR1);

	writel_relaxed(rs->fifo_len / 2 - 1, rs->regs + ROCKCHIP_SPI_TXFTLR);
	writel_relaxed(rs->fifo_len / 2 - 1, rs->regs + ROCKCHIP_SPI_RXFTLR);

	writel_relaxed(0, rs->regs + ROCKCHIP_SPI_DMATDLR);
	writel_relaxed(0, rs->regs + ROCKCHIP_SPI_DMARDLR);
	writel_relaxed(dmacr, rs->regs + ROCKCHIP_SPI_DMACR);

	spi_set_clk(rs, div);

	dev_dbg(rs->dev, "cr0 0x%x, div %d\n", cr0, div);
}

static size_t rockchip_spi_max_transfer_size(struct spi_device *spi)
{
	return ROCKCHIP_SPI_MAX_TRANLEN;
}

static int rockchip_spi_transfer_one(
		struct spi_master *master,
		struct spi_device *spi,
		struct spi_transfer *xfer)
{
	int ret = 0;
	struct rockchip_spi *rs = spi_master_get_devdata(master);

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

	rs->speed = xfer->speed_hz;
	rs->bpw = xfer->bits_per_word;
	rs->n_bytes = rs->bpw >> 3;

	rs->tx = xfer->tx_buf;
	rs->tx_end = rs->tx + xfer->len;
	rs->rx = xfer->rx_buf;
	rs->rx_end = rs->rx + xfer->len;
	rs->len = xfer->len;

	rs->tx_sg = xfer->tx_sg;
	rs->rx_sg = xfer->rx_sg;

	if (rs->tx && rs->rx)
		rs->tmode = CR0_XFM_TR;
	else if (rs->tx)
		rs->tmode = CR0_XFM_TO;
	else if (rs->rx)
		rs->tmode = CR0_XFM_RO;

	/* we need prepare dma before spi was enabled */
	if (master->can_dma && master->can_dma(master, spi, xfer))
		rs->use_dma = 1;
	else
		rs->use_dma = 0;

	rockchip_spi_config(rs);

	if (rs->use_dma) {
		if (rs->tmode == CR0_XFM_RO) {
			/* rx: dma must be prepared first */
			ret = rockchip_spi_prepare_dma(rs);
			spi_enable_chip(rs, 1);
		} else {
			/* tx or tr: spi must be enabled first */
			spi_enable_chip(rs, 1);
			ret = rockchip_spi_prepare_dma(rs);
		}
		/* successful DMA prepare means the transfer is in progress */
		ret = ret ? ret : 1;
	} else {
		spi_enable_chip(rs, 1);
		ret = rockchip_spi_pio_transfer(rs);
	}

	return ret;
}

static bool rockchip_spi_can_dma(struct spi_master *master,
				 struct spi_device *spi,
				 struct spi_transfer *xfer)
{
	struct rockchip_spi *rs = spi_master_get_devdata(master);

	return (xfer->len > rs->fifo_len);
}

static int rockchip_spi_probe(struct platform_device *pdev)
{
	int ret;
	struct rockchip_spi *rs;
	struct spi_master *master;
	struct resource *mem;
	u32 rsd_nsecs;

	master = spi_alloc_master(&pdev->dev, sizeof(struct rockchip_spi));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);

	rs = spi_master_get_devdata(master);

	/* Get basic io resource and map it */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rs->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(rs->regs)) {
		ret =  PTR_ERR(rs->regs);
		goto err_put_master;
	}

	rs->apb_pclk = devm_clk_get(&pdev->dev, "apb_pclk");
	if (IS_ERR(rs->apb_pclk)) {
		dev_err(&pdev->dev, "Failed to get apb_pclk\n");
		ret = PTR_ERR(rs->apb_pclk);
		goto err_put_master;
	}

	rs->spiclk = devm_clk_get(&pdev->dev, "spiclk");
	if (IS_ERR(rs->spiclk)) {
		dev_err(&pdev->dev, "Failed to get spi_pclk\n");
		ret = PTR_ERR(rs->spiclk);
		goto err_put_master;
	}

	ret = clk_prepare_enable(rs->apb_pclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable apb_pclk\n");
		goto err_put_master;
	}

	ret = clk_prepare_enable(rs->spiclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable spi_clk\n");
		goto err_disable_apbclk;
	}

	spi_enable_chip(rs, 0);

	rs->type = SSI_MOTO_SPI;
	rs->master = master;
	rs->dev = &pdev->dev;
	rs->max_freq = clk_get_rate(rs->spiclk);

	if (!of_property_read_u32(pdev->dev.of_node, "rx-sample-delay-ns",
				  &rsd_nsecs))
		rs->rsd_nsecs = rsd_nsecs;

	rs->fifo_len = get_fifo_len(rs);
	if (!rs->fifo_len) {
		dev_err(&pdev->dev, "Failed to get fifo length\n");
		ret = -EINVAL;
		goto err_disable_spiclk;
	}

	spin_lock_init(&rs->lock);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	master->auto_runtime_pm = true;
	master->bus_num = pdev->id;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LOOP;
	master->num_chipselect = ROCKCHIP_SPI_MAX_CS_NUM;
	master->dev.of_node = pdev->dev.of_node;
	master->bits_per_word_mask = SPI_BPW_MASK(16) | SPI_BPW_MASK(8);

	master->set_cs = rockchip_spi_set_cs;
	master->prepare_message = rockchip_spi_prepare_message;
	master->unprepare_message = rockchip_spi_unprepare_message;
	master->transfer_one = rockchip_spi_transfer_one;
	master->max_transfer_size = rockchip_spi_max_transfer_size;
	master->handle_err = rockchip_spi_handle_err;
	master->flags = SPI_MASTER_GPIO_SS;

	rs->dma_tx.ch = dma_request_chan(rs->dev, "tx");
	if (IS_ERR(rs->dma_tx.ch)) {
		/* Check tx to see if we need defer probing driver */
		if (PTR_ERR(rs->dma_tx.ch) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_disable_pm_runtime;
		}
		dev_warn(rs->dev, "Failed to request TX DMA channel\n");
		rs->dma_tx.ch = NULL;
	}

	rs->dma_rx.ch = dma_request_chan(rs->dev, "rx");
	if (IS_ERR(rs->dma_rx.ch)) {
		if (PTR_ERR(rs->dma_rx.ch) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_free_dma_tx;
		}
		dev_warn(rs->dev, "Failed to request RX DMA channel\n");
		rs->dma_rx.ch = NULL;
	}

	if (rs->dma_tx.ch && rs->dma_rx.ch) {
		dma_get_slave_caps(rs->dma_rx.ch, &(rs->dma_caps));
		rs->dma_tx.addr = (dma_addr_t)(mem->start + ROCKCHIP_SPI_TXDR);
		rs->dma_rx.addr = (dma_addr_t)(mem->start + ROCKCHIP_SPI_RXDR);
		rs->dma_tx.direction = DMA_MEM_TO_DEV;
		rs->dma_rx.direction = DMA_DEV_TO_MEM;

		master->can_dma = rockchip_spi_can_dma;
		master->dma_tx = rs->dma_tx.ch;
		master->dma_rx = rs->dma_rx.ch;
	}

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register master\n");
		goto err_free_dma_rx;
	}

	return 0;

err_free_dma_rx:
	if (rs->dma_rx.ch)
		dma_release_channel(rs->dma_rx.ch);
err_free_dma_tx:
	if (rs->dma_tx.ch)
		dma_release_channel(rs->dma_tx.ch);
err_disable_pm_runtime:
	pm_runtime_disable(&pdev->dev);
err_disable_spiclk:
	clk_disable_unprepare(rs->spiclk);
err_disable_apbclk:
	clk_disable_unprepare(rs->apb_pclk);
err_put_master:
	spi_master_put(master);

	return ret;
}

static int rockchip_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = spi_master_get(platform_get_drvdata(pdev));
	struct rockchip_spi *rs = spi_master_get_devdata(master);

	pm_runtime_get_sync(&pdev->dev);

	clk_disable_unprepare(rs->spiclk);
	clk_disable_unprepare(rs->apb_pclk);

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	if (rs->dma_tx.ch)
		dma_release_channel(rs->dma_tx.ch);
	if (rs->dma_rx.ch)
		dma_release_channel(rs->dma_rx.ch);

	spi_master_put(master);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_spi_suspend(struct device *dev)
{
	int ret;
	struct spi_master *master = dev_get_drvdata(dev);
	struct rockchip_spi *rs = spi_master_get_devdata(master);

	ret = spi_master_suspend(rs->master);
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
	struct spi_master *master = dev_get_drvdata(dev);
	struct rockchip_spi *rs = spi_master_get_devdata(master);

	pinctrl_pm_select_default_state(dev);

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	ret = spi_master_resume(rs->master);
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
	struct spi_master *master = dev_get_drvdata(dev);
	struct rockchip_spi *rs = spi_master_get_devdata(master);

	clk_disable_unprepare(rs->spiclk);
	clk_disable_unprepare(rs->apb_pclk);

	return 0;
}

static int rockchip_spi_runtime_resume(struct device *dev)
{
	int ret;
	struct spi_master *master = dev_get_drvdata(dev);
	struct rockchip_spi *rs = spi_master_get_devdata(master);

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
	{ .compatible = "rockchip,rv1108-spi", },
	{ .compatible = "rockchip,rk3036-spi", },
	{ .compatible = "rockchip,rk3066-spi", },
	{ .compatible = "rockchip,rk3188-spi", },
	{ .compatible = "rockchip,rk3228-spi", },
	{ .compatible = "rockchip,rk3288-spi", },
	{ .compatible = "rockchip,rk3368-spi", },
	{ .compatible = "rockchip,rk3399-spi", },
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
