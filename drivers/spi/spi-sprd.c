// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Spreadtrum Communications Inc.

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/sprd-dma.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>

#define SPRD_SPI_TXD			0x0
#define SPRD_SPI_CLKD			0x4
#define SPRD_SPI_CTL0			0x8
#define SPRD_SPI_CTL1			0xc
#define SPRD_SPI_CTL2			0x10
#define SPRD_SPI_CTL3			0x14
#define SPRD_SPI_CTL4			0x18
#define SPRD_SPI_CTL5			0x1c
#define SPRD_SPI_INT_EN			0x20
#define SPRD_SPI_INT_CLR		0x24
#define SPRD_SPI_INT_RAW_STS		0x28
#define SPRD_SPI_INT_MASK_STS		0x2c
#define SPRD_SPI_STS1			0x30
#define SPRD_SPI_STS2			0x34
#define SPRD_SPI_DSP_WAIT		0x38
#define SPRD_SPI_STS3			0x3c
#define SPRD_SPI_CTL6			0x40
#define SPRD_SPI_STS4			0x44
#define SPRD_SPI_FIFO_RST		0x48
#define SPRD_SPI_CTL7			0x4c
#define SPRD_SPI_STS5			0x50
#define SPRD_SPI_CTL8			0x54
#define SPRD_SPI_CTL9			0x58
#define SPRD_SPI_CTL10			0x5c
#define SPRD_SPI_CTL11			0x60
#define SPRD_SPI_CTL12			0x64
#define SPRD_SPI_STS6			0x68
#define SPRD_SPI_STS7			0x6c
#define SPRD_SPI_STS8			0x70
#define SPRD_SPI_STS9			0x74

/* Bits & mask definition for register CTL0 */
#define SPRD_SPI_SCK_REV		BIT(13)
#define SPRD_SPI_NG_TX			BIT(1)
#define SPRD_SPI_NG_RX			BIT(0)
#define SPRD_SPI_CHNL_LEN_MASK		GENMASK(4, 0)
#define SPRD_SPI_CSN_MASK		GENMASK(11, 8)
#define SPRD_SPI_CS0_VALID		BIT(8)

/* Bits & mask definition for register SPI_INT_EN */
#define SPRD_SPI_TX_END_INT_EN		BIT(8)
#define SPRD_SPI_RX_END_INT_EN		BIT(9)

/* Bits & mask definition for register SPI_INT_RAW_STS */
#define SPRD_SPI_TX_END_RAW		BIT(8)
#define SPRD_SPI_RX_END_RAW		BIT(9)

/* Bits & mask definition for register SPI_INT_CLR */
#define SPRD_SPI_TX_END_CLR		BIT(8)
#define SPRD_SPI_RX_END_CLR		BIT(9)

/* Bits & mask definition for register INT_MASK_STS */
#define SPRD_SPI_MASK_RX_END		BIT(9)
#define SPRD_SPI_MASK_TX_END		BIT(8)

/* Bits & mask definition for register STS2 */
#define SPRD_SPI_TX_BUSY		BIT(8)

/* Bits & mask definition for register CTL1 */
#define SPRD_SPI_RX_MODE		BIT(12)
#define SPRD_SPI_TX_MODE		BIT(13)
#define SPRD_SPI_RTX_MD_MASK		GENMASK(13, 12)

/* Bits & mask definition for register CTL2 */
#define SPRD_SPI_DMA_EN			BIT(6)

/* Bits & mask definition for register CTL4 */
#define SPRD_SPI_START_RX		BIT(9)
#define SPRD_SPI_ONLY_RECV_MASK		GENMASK(8, 0)

/* Bits & mask definition for register SPI_INT_CLR */
#define SPRD_SPI_RX_END_INT_CLR		BIT(9)
#define SPRD_SPI_TX_END_INT_CLR		BIT(8)

/* Bits & mask definition for register SPI_INT_RAW */
#define SPRD_SPI_RX_END_IRQ		BIT(9)
#define SPRD_SPI_TX_END_IRQ		BIT(8)

/* Bits & mask definition for register CTL12 */
#define SPRD_SPI_SW_RX_REQ		BIT(0)
#define SPRD_SPI_SW_TX_REQ		BIT(1)

/* Bits & mask definition for register CTL7 */
#define SPRD_SPI_DATA_LINE2_EN		BIT(15)
#define SPRD_SPI_MODE_MASK		GENMASK(5, 3)
#define SPRD_SPI_MODE_OFFSET		3
#define SPRD_SPI_3WIRE_MODE		4
#define SPRD_SPI_4WIRE_MODE		0

/* Bits & mask definition for register CTL8 */
#define SPRD_SPI_TX_MAX_LEN_MASK	GENMASK(19, 0)
#define SPRD_SPI_TX_LEN_H_MASK		GENMASK(3, 0)
#define SPRD_SPI_TX_LEN_H_OFFSET	16

/* Bits & mask definition for register CTL9 */
#define SPRD_SPI_TX_LEN_L_MASK		GENMASK(15, 0)

/* Bits & mask definition for register CTL10 */
#define SPRD_SPI_RX_MAX_LEN_MASK	GENMASK(19, 0)
#define SPRD_SPI_RX_LEN_H_MASK		GENMASK(3, 0)
#define SPRD_SPI_RX_LEN_H_OFFSET	16

/* Bits & mask definition for register CTL11 */
#define SPRD_SPI_RX_LEN_L_MASK		GENMASK(15, 0)

/* Default & maximum word delay cycles */
#define SPRD_SPI_MIN_DELAY_CYCLE	14
#define SPRD_SPI_MAX_DELAY_CYCLE	130

#define SPRD_SPI_FIFO_SIZE		32
#define SPRD_SPI_CHIP_CS_NUM		0x4
#define SPRD_SPI_CHNL_LEN		2
#define SPRD_SPI_DEFAULT_SOURCE		26000000
#define SPRD_SPI_MAX_SPEED_HZ		48000000
#define SPRD_SPI_AUTOSUSPEND_DELAY	100
#define SPRD_SPI_DMA_STEP		8

enum sprd_spi_dma_channel {
	SPRD_SPI_RX,
	SPRD_SPI_TX,
	SPRD_SPI_MAX,
};

struct sprd_spi_dma {
	bool enable;
	struct dma_chan *dma_chan[SPRD_SPI_MAX];
	enum dma_slave_buswidth width;
	u32 fragmens_len;
	u32 rx_len;
};

struct sprd_spi {
	void __iomem *base;
	phys_addr_t phy_base;
	struct device *dev;
	struct clk *clk;
	int irq;
	u32 src_clk;
	u32 hw_mode;
	u32 trans_len;
	u32 trans_mode;
	u32 word_delay;
	u32 hw_speed_hz;
	u32 len;
	int status;
	struct sprd_spi_dma dma;
	struct completion xfer_completion;
	const void *tx_buf;
	void *rx_buf;
	int (*read_bufs)(struct sprd_spi *ss, u32 len);
	int (*write_bufs)(struct sprd_spi *ss, u32 len);
};

static u32 sprd_spi_transfer_max_timeout(struct sprd_spi *ss,
					 struct spi_transfer *t)
{
	/*
	 * The time spent on transmission of the full FIFO data is the maximum
	 * SPI transmission time.
	 */
	u32 size = t->bits_per_word * SPRD_SPI_FIFO_SIZE;
	u32 bit_time_us = DIV_ROUND_UP(USEC_PER_SEC, ss->hw_speed_hz);
	u32 total_time_us = size * bit_time_us;
	/*
	 * There is an interval between data and the data in our SPI hardware,
	 * so the total transmission time need add the interval time.
	 */
	u32 interval_cycle = SPRD_SPI_FIFO_SIZE * ss->word_delay;
	u32 interval_time_us = DIV_ROUND_UP(interval_cycle * USEC_PER_SEC,
					    ss->src_clk);

	return total_time_us + interval_time_us;
}

static int sprd_spi_wait_for_tx_end(struct sprd_spi *ss, struct spi_transfer *t)
{
	u32 val, us;
	int ret;

	us = sprd_spi_transfer_max_timeout(ss, t);
	ret = readl_relaxed_poll_timeout(ss->base + SPRD_SPI_INT_RAW_STS, val,
					 val & SPRD_SPI_TX_END_IRQ, 0, us);
	if (ret) {
		dev_err(ss->dev, "SPI error, spi send timeout!\n");
		return ret;
	}

	ret = readl_relaxed_poll_timeout(ss->base + SPRD_SPI_STS2, val,
					 !(val & SPRD_SPI_TX_BUSY), 0, us);
	if (ret) {
		dev_err(ss->dev, "SPI error, spi busy timeout!\n");
		return ret;
	}

	writel_relaxed(SPRD_SPI_TX_END_INT_CLR, ss->base + SPRD_SPI_INT_CLR);

	return 0;
}

static int sprd_spi_wait_for_rx_end(struct sprd_spi *ss, struct spi_transfer *t)
{
	u32 val, us;
	int ret;

	us = sprd_spi_transfer_max_timeout(ss, t);
	ret = readl_relaxed_poll_timeout(ss->base + SPRD_SPI_INT_RAW_STS, val,
					 val & SPRD_SPI_RX_END_IRQ, 0, us);
	if (ret) {
		dev_err(ss->dev, "SPI error, spi rx timeout!\n");
		return ret;
	}

	writel_relaxed(SPRD_SPI_RX_END_INT_CLR, ss->base + SPRD_SPI_INT_CLR);

	return 0;
}

static void sprd_spi_tx_req(struct sprd_spi *ss)
{
	writel_relaxed(SPRD_SPI_SW_TX_REQ, ss->base + SPRD_SPI_CTL12);
}

static void sprd_spi_rx_req(struct sprd_spi *ss)
{
	writel_relaxed(SPRD_SPI_SW_RX_REQ, ss->base + SPRD_SPI_CTL12);
}

static void sprd_spi_enter_idle(struct sprd_spi *ss)
{
	u32 val = readl_relaxed(ss->base + SPRD_SPI_CTL1);

	val &= ~SPRD_SPI_RTX_MD_MASK;
	writel_relaxed(val, ss->base + SPRD_SPI_CTL1);
}

static void sprd_spi_set_transfer_bits(struct sprd_spi *ss, u32 bits)
{
	u32 val = readl_relaxed(ss->base + SPRD_SPI_CTL0);

	/* Set the valid bits for every transaction */
	val &= ~(SPRD_SPI_CHNL_LEN_MASK << SPRD_SPI_CHNL_LEN);
	val |= bits << SPRD_SPI_CHNL_LEN;
	writel_relaxed(val, ss->base + SPRD_SPI_CTL0);
}

static void sprd_spi_set_tx_length(struct sprd_spi *ss, u32 length)
{
	u32 val = readl_relaxed(ss->base + SPRD_SPI_CTL8);

	length &= SPRD_SPI_TX_MAX_LEN_MASK;
	val &= ~SPRD_SPI_TX_LEN_H_MASK;
	val |= length >> SPRD_SPI_TX_LEN_H_OFFSET;
	writel_relaxed(val, ss->base + SPRD_SPI_CTL8);

	val = length & SPRD_SPI_TX_LEN_L_MASK;
	writel_relaxed(val, ss->base + SPRD_SPI_CTL9);
}

static void sprd_spi_set_rx_length(struct sprd_spi *ss, u32 length)
{
	u32 val = readl_relaxed(ss->base + SPRD_SPI_CTL10);

	length &= SPRD_SPI_RX_MAX_LEN_MASK;
	val &= ~SPRD_SPI_RX_LEN_H_MASK;
	val |= length >> SPRD_SPI_RX_LEN_H_OFFSET;
	writel_relaxed(val, ss->base + SPRD_SPI_CTL10);

	val = length & SPRD_SPI_RX_LEN_L_MASK;
	writel_relaxed(val, ss->base + SPRD_SPI_CTL11);
}

static void sprd_spi_chipselect(struct spi_device *sdev, bool cs)
{
	struct spi_controller *sctlr = sdev->controller;
	struct sprd_spi *ss = spi_controller_get_devdata(sctlr);
	u32 val;

	val = readl_relaxed(ss->base + SPRD_SPI_CTL0);
	/*  The SPI controller will pull down CS pin if cs is 0 */
	if (!cs) {
		val &= ~SPRD_SPI_CS0_VALID;
		writel_relaxed(val, ss->base + SPRD_SPI_CTL0);
	} else {
		val |= SPRD_SPI_CSN_MASK;
		writel_relaxed(val, ss->base + SPRD_SPI_CTL0);
	}
}

static int sprd_spi_write_only_receive(struct sprd_spi *ss, u32 len)
{
	u32 val;

	/* Clear the start receive bit and reset receive data number */
	val = readl_relaxed(ss->base + SPRD_SPI_CTL4);
	val &= ~(SPRD_SPI_START_RX | SPRD_SPI_ONLY_RECV_MASK);
	writel_relaxed(val, ss->base + SPRD_SPI_CTL4);

	/* Set the receive data length */
	val = readl_relaxed(ss->base + SPRD_SPI_CTL4);
	val |= len & SPRD_SPI_ONLY_RECV_MASK;
	writel_relaxed(val, ss->base + SPRD_SPI_CTL4);

	/* Trigger to receive data */
	val = readl_relaxed(ss->base + SPRD_SPI_CTL4);
	val |= SPRD_SPI_START_RX;
	writel_relaxed(val, ss->base + SPRD_SPI_CTL4);

	return len;
}

static int sprd_spi_write_bufs_u8(struct sprd_spi *ss, u32 len)
{
	u8 *tx_p = (u8 *)ss->tx_buf;
	int i;

	for (i = 0; i < len; i++)
		writeb_relaxed(tx_p[i], ss->base + SPRD_SPI_TXD);

	ss->tx_buf += i;
	return i;
}

static int sprd_spi_write_bufs_u16(struct sprd_spi *ss, u32 len)
{
	u16 *tx_p = (u16 *)ss->tx_buf;
	int i;

	for (i = 0; i < len; i++)
		writew_relaxed(tx_p[i], ss->base + SPRD_SPI_TXD);

	ss->tx_buf += i << 1;
	return i << 1;
}

static int sprd_spi_write_bufs_u32(struct sprd_spi *ss, u32 len)
{
	u32 *tx_p = (u32 *)ss->tx_buf;
	int i;

	for (i = 0; i < len; i++)
		writel_relaxed(tx_p[i], ss->base + SPRD_SPI_TXD);

	ss->tx_buf += i << 2;
	return i << 2;
}

static int sprd_spi_read_bufs_u8(struct sprd_spi *ss, u32 len)
{
	u8 *rx_p = (u8 *)ss->rx_buf;
	int i;

	for (i = 0; i < len; i++)
		rx_p[i] = readb_relaxed(ss->base + SPRD_SPI_TXD);

	ss->rx_buf += i;
	return i;
}

static int sprd_spi_read_bufs_u16(struct sprd_spi *ss, u32 len)
{
	u16 *rx_p = (u16 *)ss->rx_buf;
	int i;

	for (i = 0; i < len; i++)
		rx_p[i] = readw_relaxed(ss->base + SPRD_SPI_TXD);

	ss->rx_buf += i << 1;
	return i << 1;
}

static int sprd_spi_read_bufs_u32(struct sprd_spi *ss, u32 len)
{
	u32 *rx_p = (u32 *)ss->rx_buf;
	int i;

	for (i = 0; i < len; i++)
		rx_p[i] = readl_relaxed(ss->base + SPRD_SPI_TXD);

	ss->rx_buf += i << 2;
	return i << 2;
}

static int sprd_spi_txrx_bufs(struct spi_device *sdev, struct spi_transfer *t)
{
	struct sprd_spi *ss = spi_controller_get_devdata(sdev->controller);
	u32 trans_len = ss->trans_len, len;
	int ret, write_size = 0, read_size = 0;

	while (trans_len) {
		len = trans_len > SPRD_SPI_FIFO_SIZE ? SPRD_SPI_FIFO_SIZE :
			trans_len;
		if (ss->trans_mode & SPRD_SPI_TX_MODE) {
			sprd_spi_set_tx_length(ss, len);
			write_size += ss->write_bufs(ss, len);

			/*
			 * For our 3 wires mode or dual TX line mode, we need
			 * to request the controller to transfer.
			 */
			if (ss->hw_mode & SPI_3WIRE || ss->hw_mode & SPI_TX_DUAL)
				sprd_spi_tx_req(ss);

			ret = sprd_spi_wait_for_tx_end(ss, t);
		} else {
			sprd_spi_set_rx_length(ss, len);

			/*
			 * For our 3 wires mode or dual TX line mode, we need
			 * to request the controller to read.
			 */
			if (ss->hw_mode & SPI_3WIRE || ss->hw_mode & SPI_TX_DUAL)
				sprd_spi_rx_req(ss);
			else
				write_size += ss->write_bufs(ss, len);

			ret = sprd_spi_wait_for_rx_end(ss, t);
		}

		if (ret)
			goto complete;

		if (ss->trans_mode & SPRD_SPI_RX_MODE)
			read_size += ss->read_bufs(ss, len);

		trans_len -= len;
	}

	if (ss->trans_mode & SPRD_SPI_TX_MODE)
		ret = write_size;
	else
		ret = read_size;
complete:
	sprd_spi_enter_idle(ss);

	return ret;
}

static void sprd_spi_irq_enable(struct sprd_spi *ss)
{
	u32 val;

	/* Clear interrupt status before enabling interrupt. */
	writel_relaxed(SPRD_SPI_TX_END_CLR | SPRD_SPI_RX_END_CLR,
		ss->base + SPRD_SPI_INT_CLR);
	/* Enable SPI interrupt only in DMA mode. */
	val = readl_relaxed(ss->base + SPRD_SPI_INT_EN);
	writel_relaxed(val | SPRD_SPI_TX_END_INT_EN |
		       SPRD_SPI_RX_END_INT_EN,
		       ss->base + SPRD_SPI_INT_EN);
}

static void sprd_spi_irq_disable(struct sprd_spi *ss)
{
	writel_relaxed(0, ss->base + SPRD_SPI_INT_EN);
}

static void sprd_spi_dma_enable(struct sprd_spi *ss, bool enable)
{
	u32 val = readl_relaxed(ss->base + SPRD_SPI_CTL2);

	if (enable)
		val |= SPRD_SPI_DMA_EN;
	else
		val &= ~SPRD_SPI_DMA_EN;

	writel_relaxed(val, ss->base + SPRD_SPI_CTL2);
}

static int sprd_spi_dma_submit(struct dma_chan *dma_chan,
			       struct dma_slave_config *c,
			       struct sg_table *sg,
			       enum dma_transfer_direction dir)
{
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
	unsigned long flags;
	int ret;

	ret = dmaengine_slave_config(dma_chan, c);
	if (ret < 0)
		return ret;

	flags = SPRD_DMA_FLAGS(SPRD_DMA_CHN_MODE_NONE, SPRD_DMA_NO_TRG,
			       SPRD_DMA_FRAG_REQ, SPRD_DMA_TRANS_INT);
	desc = dmaengine_prep_slave_sg(dma_chan, sg->sgl, sg->nents, dir, flags);
	if (!desc)
		return  -ENODEV;

	cookie = dmaengine_submit(desc);
	if (dma_submit_error(cookie))
		return dma_submit_error(cookie);

	dma_async_issue_pending(dma_chan);

	return 0;
}

static int sprd_spi_dma_rx_config(struct sprd_spi *ss, struct spi_transfer *t)
{
	struct dma_chan *dma_chan = ss->dma.dma_chan[SPRD_SPI_RX];
	struct dma_slave_config config = {
		.src_addr = ss->phy_base,
		.src_addr_width = ss->dma.width,
		.dst_addr_width = ss->dma.width,
		.dst_maxburst = ss->dma.fragmens_len,
	};
	int ret;

	ret = sprd_spi_dma_submit(dma_chan, &config, &t->rx_sg, DMA_DEV_TO_MEM);
	if (ret)
		return ret;

	return ss->dma.rx_len;
}

static int sprd_spi_dma_tx_config(struct sprd_spi *ss, struct spi_transfer *t)
{
	struct dma_chan *dma_chan = ss->dma.dma_chan[SPRD_SPI_TX];
	struct dma_slave_config config = {
		.dst_addr = ss->phy_base,
		.src_addr_width = ss->dma.width,
		.dst_addr_width = ss->dma.width,
		.src_maxburst = ss->dma.fragmens_len,
	};
	int ret;

	ret = sprd_spi_dma_submit(dma_chan, &config, &t->tx_sg, DMA_MEM_TO_DEV);
	if (ret)
		return ret;

	return t->len;
}

static int sprd_spi_dma_request(struct sprd_spi *ss)
{
	ss->dma.dma_chan[SPRD_SPI_RX] = dma_request_chan(ss->dev, "rx_chn");
	if (IS_ERR_OR_NULL(ss->dma.dma_chan[SPRD_SPI_RX]))
		return dev_err_probe(ss->dev, PTR_ERR(ss->dma.dma_chan[SPRD_SPI_RX]),
				     "request RX DMA channel failed!\n");

	ss->dma.dma_chan[SPRD_SPI_TX]  = dma_request_chan(ss->dev, "tx_chn");
	if (IS_ERR_OR_NULL(ss->dma.dma_chan[SPRD_SPI_TX])) {
		dma_release_channel(ss->dma.dma_chan[SPRD_SPI_RX]);
		return dev_err_probe(ss->dev, PTR_ERR(ss->dma.dma_chan[SPRD_SPI_TX]),
				     "request TX DMA channel failed!\n");
	}

	return 0;
}

static void sprd_spi_dma_release(struct sprd_spi *ss)
{
	if (ss->dma.dma_chan[SPRD_SPI_RX])
		dma_release_channel(ss->dma.dma_chan[SPRD_SPI_RX]);

	if (ss->dma.dma_chan[SPRD_SPI_TX])
		dma_release_channel(ss->dma.dma_chan[SPRD_SPI_TX]);
}

static int sprd_spi_dma_txrx_bufs(struct spi_device *sdev,
				  struct spi_transfer *t)
{
	struct sprd_spi *ss = spi_master_get_devdata(sdev->master);
	u32 trans_len = ss->trans_len;
	int ret, write_size = 0;

	reinit_completion(&ss->xfer_completion);
	sprd_spi_irq_enable(ss);
	if (ss->trans_mode & SPRD_SPI_TX_MODE) {
		write_size = sprd_spi_dma_tx_config(ss, t);
		sprd_spi_set_tx_length(ss, trans_len);

		/*
		 * For our 3 wires mode or dual TX line mode, we need
		 * to request the controller to transfer.
		 */
		if (ss->hw_mode & SPI_3WIRE || ss->hw_mode & SPI_TX_DUAL)
			sprd_spi_tx_req(ss);
	} else {
		sprd_spi_set_rx_length(ss, trans_len);

		/*
		 * For our 3 wires mode or dual TX line mode, we need
		 * to request the controller to read.
		 */
		if (ss->hw_mode & SPI_3WIRE || ss->hw_mode & SPI_TX_DUAL)
			sprd_spi_rx_req(ss);
		else
			write_size = ss->write_bufs(ss, trans_len);
	}

	if (write_size < 0) {
		ret = write_size;
		dev_err(ss->dev, "failed to write, ret = %d\n", ret);
		goto trans_complete;
	}

	if (ss->trans_mode & SPRD_SPI_RX_MODE) {
		/*
		 * Set up the DMA receive data length, which must be an
		 * integral multiple of fragment length. But when the length
		 * of received data is less than fragment length, DMA can be
		 * configured to receive data according to the actual length
		 * of received data.
		 */
		ss->dma.rx_len = t->len > ss->dma.fragmens_len ?
			(t->len - t->len % ss->dma.fragmens_len) :
			 t->len;
		ret = sprd_spi_dma_rx_config(ss, t);
		if (ret < 0) {
			dev_err(&sdev->dev,
				"failed to configure rx DMA, ret = %d\n", ret);
			goto trans_complete;
		}
	}

	sprd_spi_dma_enable(ss, true);
	wait_for_completion(&(ss->xfer_completion));

	if (ss->trans_mode & SPRD_SPI_TX_MODE)
		ret = write_size;
	else
		ret = ss->dma.rx_len;

trans_complete:
	sprd_spi_dma_enable(ss, false);
	sprd_spi_enter_idle(ss);
	sprd_spi_irq_disable(ss);

	return ret;
}

static void sprd_spi_set_speed(struct sprd_spi *ss, u32 speed_hz)
{
	/*
	 * From SPI datasheet, the prescale calculation formula:
	 * prescale = SPI source clock / (2 * SPI_freq) - 1;
	 */
	u32 clk_div = DIV_ROUND_UP(ss->src_clk, speed_hz << 1) - 1;

	/* Save the real hardware speed */
	ss->hw_speed_hz = (ss->src_clk >> 1) / (clk_div + 1);
	writel_relaxed(clk_div, ss->base + SPRD_SPI_CLKD);
}

static int sprd_spi_init_hw(struct sprd_spi *ss, struct spi_transfer *t)
{
	struct spi_delay *d = &t->word_delay;
	u16 word_delay, interval;
	u32 val;

	if (d->unit != SPI_DELAY_UNIT_SCK)
		return -EINVAL;

	val = readl_relaxed(ss->base + SPRD_SPI_CTL0);
	val &= ~(SPRD_SPI_SCK_REV | SPRD_SPI_NG_TX | SPRD_SPI_NG_RX);
	/* Set default chip selection, clock phase and clock polarity */
	val |= ss->hw_mode & SPI_CPHA ? SPRD_SPI_NG_RX : SPRD_SPI_NG_TX;
	val |= ss->hw_mode & SPI_CPOL ? SPRD_SPI_SCK_REV : 0;
	writel_relaxed(val, ss->base + SPRD_SPI_CTL0);

	/*
	 * Set the intervals of two SPI frames, and the inteval calculation
	 * formula as below per datasheet:
	 * interval time (source clock cycles) = interval * 4 + 10.
	 */
	word_delay = clamp_t(u16, d->value, SPRD_SPI_MIN_DELAY_CYCLE,
			     SPRD_SPI_MAX_DELAY_CYCLE);
	interval = DIV_ROUND_UP(word_delay - 10, 4);
	ss->word_delay = interval * 4 + 10;
	writel_relaxed(interval, ss->base + SPRD_SPI_CTL5);

	/* Reset SPI fifo */
	writel_relaxed(1, ss->base + SPRD_SPI_FIFO_RST);
	writel_relaxed(0, ss->base + SPRD_SPI_FIFO_RST);

	/* Set SPI work mode */
	val = readl_relaxed(ss->base + SPRD_SPI_CTL7);
	val &= ~SPRD_SPI_MODE_MASK;

	if (ss->hw_mode & SPI_3WIRE)
		val |= SPRD_SPI_3WIRE_MODE << SPRD_SPI_MODE_OFFSET;
	else
		val |= SPRD_SPI_4WIRE_MODE << SPRD_SPI_MODE_OFFSET;

	if (ss->hw_mode & SPI_TX_DUAL)
		val |= SPRD_SPI_DATA_LINE2_EN;
	else
		val &= ~SPRD_SPI_DATA_LINE2_EN;

	writel_relaxed(val, ss->base + SPRD_SPI_CTL7);

	return 0;
}

static int sprd_spi_setup_transfer(struct spi_device *sdev,
				   struct spi_transfer *t)
{
	struct sprd_spi *ss = spi_controller_get_devdata(sdev->controller);
	u8 bits_per_word = t->bits_per_word;
	u32 val, mode = 0;
	int ret;

	ss->len = t->len;
	ss->tx_buf = t->tx_buf;
	ss->rx_buf = t->rx_buf;

	ss->hw_mode = sdev->mode;
	ret = sprd_spi_init_hw(ss, t);
	if (ret)
		return ret;

	/* Set tansfer speed and valid bits */
	sprd_spi_set_speed(ss, t->speed_hz);
	sprd_spi_set_transfer_bits(ss, bits_per_word);

	if (bits_per_word > 16)
		bits_per_word = round_up(bits_per_word, 16);
	else
		bits_per_word = round_up(bits_per_word, 8);

	switch (bits_per_word) {
	case 8:
		ss->trans_len = t->len;
		ss->read_bufs = sprd_spi_read_bufs_u8;
		ss->write_bufs = sprd_spi_write_bufs_u8;
		ss->dma.width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		ss->dma.fragmens_len = SPRD_SPI_DMA_STEP;
		break;
	case 16:
		ss->trans_len = t->len >> 1;
		ss->read_bufs = sprd_spi_read_bufs_u16;
		ss->write_bufs = sprd_spi_write_bufs_u16;
		ss->dma.width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		ss->dma.fragmens_len = SPRD_SPI_DMA_STEP << 1;
		break;
	case 32:
		ss->trans_len = t->len >> 2;
		ss->read_bufs = sprd_spi_read_bufs_u32;
		ss->write_bufs = sprd_spi_write_bufs_u32;
		ss->dma.width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		ss->dma.fragmens_len = SPRD_SPI_DMA_STEP << 2;
		break;
	default:
		return -EINVAL;
	}

	/* Set transfer read or write mode */
	val = readl_relaxed(ss->base + SPRD_SPI_CTL1);
	val &= ~SPRD_SPI_RTX_MD_MASK;
	if (t->tx_buf)
		mode |= SPRD_SPI_TX_MODE;
	if (t->rx_buf)
		mode |= SPRD_SPI_RX_MODE;

	writel_relaxed(val | mode, ss->base + SPRD_SPI_CTL1);

	ss->trans_mode = mode;

	/*
	 * If in only receive mode, we need to trigger the SPI controller to
	 * receive data automatically.
	 */
	if (ss->trans_mode == SPRD_SPI_RX_MODE)
		ss->write_bufs = sprd_spi_write_only_receive;

	return 0;
}

static int sprd_spi_transfer_one(struct spi_controller *sctlr,
				 struct spi_device *sdev,
				 struct spi_transfer *t)
{
	int ret;

	ret = sprd_spi_setup_transfer(sdev, t);
	if (ret)
		goto setup_err;

	if (sctlr->can_dma(sctlr, sdev, t))
		ret = sprd_spi_dma_txrx_bufs(sdev, t);
	else
		ret = sprd_spi_txrx_bufs(sdev, t);

	if (ret == t->len)
		ret = 0;
	else if (ret >= 0)
		ret = -EREMOTEIO;

setup_err:
	spi_finalize_current_transfer(sctlr);

	return ret;
}

static irqreturn_t sprd_spi_handle_irq(int irq, void *data)
{
	struct sprd_spi *ss = (struct sprd_spi *)data;
	u32 val = readl_relaxed(ss->base + SPRD_SPI_INT_MASK_STS);

	if (val & SPRD_SPI_MASK_TX_END) {
		writel_relaxed(SPRD_SPI_TX_END_CLR, ss->base + SPRD_SPI_INT_CLR);
		if (!(ss->trans_mode & SPRD_SPI_RX_MODE))
			complete(&ss->xfer_completion);

		return IRQ_HANDLED;
	}

	if (val & SPRD_SPI_MASK_RX_END) {
		writel_relaxed(SPRD_SPI_RX_END_CLR, ss->base + SPRD_SPI_INT_CLR);
		if (ss->dma.rx_len < ss->len) {
			ss->rx_buf += ss->dma.rx_len;
			ss->dma.rx_len +=
				ss->read_bufs(ss, ss->len - ss->dma.rx_len);
		}
		complete(&ss->xfer_completion);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int sprd_spi_irq_init(struct platform_device *pdev, struct sprd_spi *ss)
{
	int ret;

	ss->irq = platform_get_irq(pdev, 0);
	if (ss->irq < 0)
		return ss->irq;

	ret = devm_request_irq(&pdev->dev, ss->irq, sprd_spi_handle_irq,
				0, pdev->name, ss);
	if (ret)
		dev_err(&pdev->dev, "failed to request spi irq %d, ret = %d\n",
			ss->irq, ret);

	return ret;
}

static int sprd_spi_clk_init(struct platform_device *pdev, struct sprd_spi *ss)
{
	struct clk *clk_spi, *clk_parent;

	clk_spi = devm_clk_get(&pdev->dev, "spi");
	if (IS_ERR(clk_spi)) {
		dev_warn(&pdev->dev, "can't get the spi clock\n");
		clk_spi = NULL;
	}

	clk_parent = devm_clk_get(&pdev->dev, "source");
	if (IS_ERR(clk_parent)) {
		dev_warn(&pdev->dev, "can't get the source clock\n");
		clk_parent = NULL;
	}

	ss->clk = devm_clk_get(&pdev->dev, "enable");
	if (IS_ERR(ss->clk)) {
		dev_err(&pdev->dev, "can't get the enable clock\n");
		return PTR_ERR(ss->clk);
	}

	if (!clk_set_parent(clk_spi, clk_parent))
		ss->src_clk = clk_get_rate(clk_spi);
	else
		ss->src_clk = SPRD_SPI_DEFAULT_SOURCE;

	return 0;
}

static bool sprd_spi_can_dma(struct spi_controller *sctlr,
			     struct spi_device *spi, struct spi_transfer *t)
{
	struct sprd_spi *ss = spi_controller_get_devdata(sctlr);

	return ss->dma.enable && (t->len > SPRD_SPI_FIFO_SIZE);
}

static int sprd_spi_dma_init(struct platform_device *pdev, struct sprd_spi *ss)
{
	int ret;

	ret = sprd_spi_dma_request(ss);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			return ret;

		dev_warn(&pdev->dev,
			 "failed to request dma, enter no dma mode, ret = %d\n",
			 ret);

		return 0;
	}

	ss->dma.enable = true;

	return 0;
}

static int sprd_spi_probe(struct platform_device *pdev)
{
	struct spi_controller *sctlr;
	struct resource *res;
	struct sprd_spi *ss;
	int ret;

	pdev->id = of_alias_get_id(pdev->dev.of_node, "spi");
	sctlr = spi_alloc_master(&pdev->dev, sizeof(*ss));
	if (!sctlr)
		return -ENOMEM;

	ss = spi_controller_get_devdata(sctlr);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ss->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ss->base)) {
		ret = PTR_ERR(ss->base);
		goto free_controller;
	}

	ss->phy_base = res->start;
	ss->dev = &pdev->dev;
	sctlr->dev.of_node = pdev->dev.of_node;
	sctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_3WIRE | SPI_TX_DUAL;
	sctlr->bus_num = pdev->id;
	sctlr->set_cs = sprd_spi_chipselect;
	sctlr->transfer_one = sprd_spi_transfer_one;
	sctlr->can_dma = sprd_spi_can_dma;
	sctlr->auto_runtime_pm = true;
	sctlr->max_speed_hz = min_t(u32, ss->src_clk >> 1,
				    SPRD_SPI_MAX_SPEED_HZ);

	init_completion(&ss->xfer_completion);
	platform_set_drvdata(pdev, sctlr);
	ret = sprd_spi_clk_init(pdev, ss);
	if (ret)
		goto free_controller;

	ret = sprd_spi_irq_init(pdev, ss);
	if (ret)
		goto free_controller;

	ret = sprd_spi_dma_init(pdev, ss);
	if (ret)
		goto free_controller;

	ret = clk_prepare_enable(ss->clk);
	if (ret)
		goto release_dma;

	ret = pm_runtime_set_active(&pdev->dev);
	if (ret < 0)
		goto disable_clk;

	pm_runtime_set_autosuspend_delay(&pdev->dev,
					 SPRD_SPI_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to resume SPI controller\n");
		goto err_rpm_put;
	}

	ret = devm_spi_register_controller(&pdev->dev, sctlr);
	if (ret)
		goto err_rpm_put;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

err_rpm_put:
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
disable_clk:
	clk_disable_unprepare(ss->clk);
release_dma:
	sprd_spi_dma_release(ss);
free_controller:
	spi_controller_put(sctlr);

	return ret;
}

static int sprd_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *sctlr = platform_get_drvdata(pdev);
	struct sprd_spi *ss = spi_controller_get_devdata(sctlr);
	int ret;

	ret = pm_runtime_get_sync(ss->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(ss->dev);
		dev_err(ss->dev, "failed to resume SPI controller\n");
		return ret;
	}

	spi_controller_suspend(sctlr);

	if (ss->dma.enable)
		sprd_spi_dma_release(ss);
	clk_disable_unprepare(ss->clk);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int __maybe_unused sprd_spi_runtime_suspend(struct device *dev)
{
	struct spi_controller *sctlr = dev_get_drvdata(dev);
	struct sprd_spi *ss = spi_controller_get_devdata(sctlr);

	if (ss->dma.enable)
		sprd_spi_dma_release(ss);

	clk_disable_unprepare(ss->clk);

	return 0;
}

static int __maybe_unused sprd_spi_runtime_resume(struct device *dev)
{
	struct spi_controller *sctlr = dev_get_drvdata(dev);
	struct sprd_spi *ss = spi_controller_get_devdata(sctlr);
	int ret;

	ret = clk_prepare_enable(ss->clk);
	if (ret)
		return ret;

	if (!ss->dma.enable)
		return 0;

	ret = sprd_spi_dma_request(ss);
	if (ret)
		clk_disable_unprepare(ss->clk);

	return ret;
}

static const struct dev_pm_ops sprd_spi_pm_ops = {
	SET_RUNTIME_PM_OPS(sprd_spi_runtime_suspend,
			   sprd_spi_runtime_resume, NULL)
};

static const struct of_device_id sprd_spi_of_match[] = {
	{ .compatible = "sprd,sc9860-spi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sprd_spi_of_match);

static struct platform_driver sprd_spi_driver = {
	.driver = {
		.name = "sprd-spi",
		.of_match_table = sprd_spi_of_match,
		.pm = &sprd_spi_pm_ops,
	},
	.probe = sprd_spi_probe,
	.remove  = sprd_spi_remove,
};

module_platform_driver(sprd_spi_driver);

MODULE_DESCRIPTION("Spreadtrum SPI Controller driver");
MODULE_AUTHOR("Lanqing Liu <lanqing.liu@spreadtrum.com>");
MODULE_LICENSE("GPL v2");
