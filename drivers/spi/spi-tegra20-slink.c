// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI driver for Nvidia's Tegra20/Tegra30 SLINK Controller.
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>

#define SLINK_COMMAND			0x000
#define SLINK_BIT_LENGTH(x)		(((x) & 0x1f) << 0)
#define SLINK_WORD_SIZE(x)		(((x) & 0x1f) << 5)
#define SLINK_BOTH_EN			(1 << 10)
#define SLINK_CS_SW			(1 << 11)
#define SLINK_CS_VALUE			(1 << 12)
#define SLINK_CS_POLARITY		(1 << 13)
#define SLINK_IDLE_SDA_DRIVE_LOW	(0 << 16)
#define SLINK_IDLE_SDA_DRIVE_HIGH	(1 << 16)
#define SLINK_IDLE_SDA_PULL_LOW		(2 << 16)
#define SLINK_IDLE_SDA_PULL_HIGH	(3 << 16)
#define SLINK_IDLE_SDA_MASK		(3 << 16)
#define SLINK_CS_POLARITY1		(1 << 20)
#define SLINK_CK_SDA			(1 << 21)
#define SLINK_CS_POLARITY2		(1 << 22)
#define SLINK_CS_POLARITY3		(1 << 23)
#define SLINK_IDLE_SCLK_DRIVE_LOW	(0 << 24)
#define SLINK_IDLE_SCLK_DRIVE_HIGH	(1 << 24)
#define SLINK_IDLE_SCLK_PULL_LOW	(2 << 24)
#define SLINK_IDLE_SCLK_PULL_HIGH	(3 << 24)
#define SLINK_IDLE_SCLK_MASK		(3 << 24)
#define SLINK_M_S			(1 << 28)
#define SLINK_WAIT			(1 << 29)
#define SLINK_GO			(1 << 30)
#define SLINK_ENB			(1 << 31)

#define SLINK_MODES			(SLINK_IDLE_SCLK_MASK | SLINK_CK_SDA)

#define SLINK_COMMAND2			0x004
#define SLINK_LSBFE			(1 << 0)
#define SLINK_SSOE			(1 << 1)
#define SLINK_SPIE			(1 << 4)
#define SLINK_BIDIROE			(1 << 6)
#define SLINK_MODFEN			(1 << 7)
#define SLINK_INT_SIZE(x)		(((x) & 0x1f) << 8)
#define SLINK_CS_ACTIVE_BETWEEN		(1 << 17)
#define SLINK_SS_EN_CS(x)		(((x) & 0x3) << 18)
#define SLINK_SS_SETUP(x)		(((x) & 0x3) << 20)
#define SLINK_FIFO_REFILLS_0		(0 << 22)
#define SLINK_FIFO_REFILLS_1		(1 << 22)
#define SLINK_FIFO_REFILLS_2		(2 << 22)
#define SLINK_FIFO_REFILLS_3		(3 << 22)
#define SLINK_FIFO_REFILLS_MASK		(3 << 22)
#define SLINK_WAIT_PACK_INT(x)		(((x) & 0x7) << 26)
#define SLINK_SPC0			(1 << 29)
#define SLINK_TXEN			(1 << 30)
#define SLINK_RXEN			(1 << 31)

#define SLINK_STATUS			0x008
#define SLINK_COUNT(val)		(((val) >> 0) & 0x1f)
#define SLINK_WORD(val)			(((val) >> 5) & 0x1f)
#define SLINK_BLK_CNT(val)		(((val) >> 0) & 0xffff)
#define SLINK_MODF			(1 << 16)
#define SLINK_RX_UNF			(1 << 18)
#define SLINK_TX_OVF			(1 << 19)
#define SLINK_TX_FULL			(1 << 20)
#define SLINK_TX_EMPTY			(1 << 21)
#define SLINK_RX_FULL			(1 << 22)
#define SLINK_RX_EMPTY			(1 << 23)
#define SLINK_TX_UNF			(1 << 24)
#define SLINK_RX_OVF			(1 << 25)
#define SLINK_TX_FLUSH			(1 << 26)
#define SLINK_RX_FLUSH			(1 << 27)
#define SLINK_SCLK			(1 << 28)
#define SLINK_ERR			(1 << 29)
#define SLINK_RDY			(1 << 30)
#define SLINK_BSY			(1 << 31)
#define SLINK_FIFO_ERROR		(SLINK_TX_OVF | SLINK_RX_UNF |	\
					SLINK_TX_UNF | SLINK_RX_OVF)

#define SLINK_FIFO_EMPTY		(SLINK_TX_EMPTY | SLINK_RX_EMPTY)

#define SLINK_MAS_DATA			0x010
#define SLINK_SLAVE_DATA		0x014

#define SLINK_DMA_CTL			0x018
#define SLINK_DMA_BLOCK_SIZE(x)		(((x) & 0xffff) << 0)
#define SLINK_TX_TRIG_1			(0 << 16)
#define SLINK_TX_TRIG_4			(1 << 16)
#define SLINK_TX_TRIG_8			(2 << 16)
#define SLINK_TX_TRIG_16		(3 << 16)
#define SLINK_TX_TRIG_MASK		(3 << 16)
#define SLINK_RX_TRIG_1			(0 << 18)
#define SLINK_RX_TRIG_4			(1 << 18)
#define SLINK_RX_TRIG_8			(2 << 18)
#define SLINK_RX_TRIG_16		(3 << 18)
#define SLINK_RX_TRIG_MASK		(3 << 18)
#define SLINK_PACKED			(1 << 20)
#define SLINK_PACK_SIZE_4		(0 << 21)
#define SLINK_PACK_SIZE_8		(1 << 21)
#define SLINK_PACK_SIZE_16		(2 << 21)
#define SLINK_PACK_SIZE_32		(3 << 21)
#define SLINK_PACK_SIZE_MASK		(3 << 21)
#define SLINK_IE_TXC			(1 << 26)
#define SLINK_IE_RXC			(1 << 27)
#define SLINK_DMA_EN			(1 << 31)

#define SLINK_STATUS2			0x01c
#define SLINK_TX_FIFO_EMPTY_COUNT(val)	(((val) & 0x3f) >> 0)
#define SLINK_RX_FIFO_FULL_COUNT(val)	(((val) & 0x3f0000) >> 16)
#define SLINK_SS_HOLD_TIME(val)		(((val) & 0xF) << 6)

#define SLINK_TX_FIFO			0x100
#define SLINK_RX_FIFO			0x180

#define DATA_DIR_TX			(1 << 0)
#define DATA_DIR_RX			(1 << 1)

#define SLINK_DMA_TIMEOUT		(msecs_to_jiffies(1000))

#define DEFAULT_SPI_DMA_BUF_LEN		(16*1024)
#define TX_FIFO_EMPTY_COUNT_MAX		SLINK_TX_FIFO_EMPTY_COUNT(0x20)
#define RX_FIFO_FULL_COUNT_ZERO		SLINK_RX_FIFO_FULL_COUNT(0)

#define SLINK_STATUS2_RESET \
	(TX_FIFO_EMPTY_COUNT_MAX | RX_FIFO_FULL_COUNT_ZERO << 16)

#define MAX_CHIP_SELECT			4
#define SLINK_FIFO_DEPTH		32

struct tegra_slink_chip_data {
	bool cs_hold_time;
};

struct tegra_slink_data {
	struct device				*dev;
	struct spi_master			*master;
	const struct tegra_slink_chip_data	*chip_data;
	spinlock_t				lock;

	struct clk				*clk;
	struct reset_control			*rst;
	void __iomem				*base;
	phys_addr_t				phys;
	unsigned				irq;
	u32					cur_speed;

	struct spi_device			*cur_spi;
	unsigned				cur_pos;
	unsigned				cur_len;
	unsigned				words_per_32bit;
	unsigned				bytes_per_word;
	unsigned				curr_dma_words;
	unsigned				cur_direction;

	unsigned				cur_rx_pos;
	unsigned				cur_tx_pos;

	unsigned				dma_buf_size;
	unsigned				max_buf_size;
	bool					is_curr_dma_xfer;

	struct completion			rx_dma_complete;
	struct completion			tx_dma_complete;

	u32					tx_status;
	u32					rx_status;
	u32					status_reg;
	bool					is_packed;
	u32					packed_size;

	u32					command_reg;
	u32					command2_reg;
	u32					dma_control_reg;
	u32					def_command_reg;
	u32					def_command2_reg;

	struct completion			xfer_completion;
	struct spi_transfer			*curr_xfer;
	struct dma_chan				*rx_dma_chan;
	u32					*rx_dma_buf;
	dma_addr_t				rx_dma_phys;
	struct dma_async_tx_descriptor		*rx_dma_desc;

	struct dma_chan				*tx_dma_chan;
	u32					*tx_dma_buf;
	dma_addr_t				tx_dma_phys;
	struct dma_async_tx_descriptor		*tx_dma_desc;
};

static inline u32 tegra_slink_readl(struct tegra_slink_data *tspi,
		unsigned long reg)
{
	return readl(tspi->base + reg);
}

static inline void tegra_slink_writel(struct tegra_slink_data *tspi,
		u32 val, unsigned long reg)
{
	writel(val, tspi->base + reg);

	/* Read back register to make sure that register writes completed */
	if (reg != SLINK_TX_FIFO)
		readl(tspi->base + SLINK_MAS_DATA);
}

static void tegra_slink_clear_status(struct tegra_slink_data *tspi)
{
	u32 val_write;

	tegra_slink_readl(tspi, SLINK_STATUS);

	/* Write 1 to clear status register */
	val_write = SLINK_RDY | SLINK_FIFO_ERROR;
	tegra_slink_writel(tspi, val_write, SLINK_STATUS);
}

static u32 tegra_slink_get_packed_size(struct tegra_slink_data *tspi,
				  struct spi_transfer *t)
{
	switch (tspi->bytes_per_word) {
	case 0:
		return SLINK_PACK_SIZE_4;
	case 1:
		return SLINK_PACK_SIZE_8;
	case 2:
		return SLINK_PACK_SIZE_16;
	case 4:
		return SLINK_PACK_SIZE_32;
	default:
		return 0;
	}
}

static unsigned tegra_slink_calculate_curr_xfer_param(
	struct spi_device *spi, struct tegra_slink_data *tspi,
	struct spi_transfer *t)
{
	unsigned remain_len = t->len - tspi->cur_pos;
	unsigned max_word;
	unsigned bits_per_word;
	unsigned max_len;
	unsigned total_fifo_words;

	bits_per_word = t->bits_per_word;
	tspi->bytes_per_word = DIV_ROUND_UP(bits_per_word, 8);

	if (bits_per_word == 8 || bits_per_word == 16) {
		tspi->is_packed = true;
		tspi->words_per_32bit = 32/bits_per_word;
	} else {
		tspi->is_packed = false;
		tspi->words_per_32bit = 1;
	}
	tspi->packed_size = tegra_slink_get_packed_size(tspi, t);

	if (tspi->is_packed) {
		max_len = min(remain_len, tspi->max_buf_size);
		tspi->curr_dma_words = max_len/tspi->bytes_per_word;
		total_fifo_words = max_len/4;
	} else {
		max_word = (remain_len - 1) / tspi->bytes_per_word + 1;
		max_word = min(max_word, tspi->max_buf_size/4);
		tspi->curr_dma_words = max_word;
		total_fifo_words = max_word;
	}
	return total_fifo_words;
}

static unsigned tegra_slink_fill_tx_fifo_from_client_txbuf(
	struct tegra_slink_data *tspi, struct spi_transfer *t)
{
	unsigned nbytes;
	unsigned tx_empty_count;
	u32 fifo_status;
	unsigned max_n_32bit;
	unsigned i, count;
	unsigned int written_words;
	unsigned fifo_words_left;
	u8 *tx_buf = (u8 *)t->tx_buf + tspi->cur_tx_pos;

	fifo_status = tegra_slink_readl(tspi, SLINK_STATUS2);
	tx_empty_count = SLINK_TX_FIFO_EMPTY_COUNT(fifo_status);

	if (tspi->is_packed) {
		fifo_words_left = tx_empty_count * tspi->words_per_32bit;
		written_words = min(fifo_words_left, tspi->curr_dma_words);
		nbytes = written_words * tspi->bytes_per_word;
		max_n_32bit = DIV_ROUND_UP(nbytes, 4);
		for (count = 0; count < max_n_32bit; count++) {
			u32 x = 0;
			for (i = 0; (i < 4) && nbytes; i++, nbytes--)
				x |= (u32)(*tx_buf++) << (i * 8);
			tegra_slink_writel(tspi, x, SLINK_TX_FIFO);
		}
	} else {
		max_n_32bit = min(tspi->curr_dma_words,  tx_empty_count);
		written_words = max_n_32bit;
		nbytes = written_words * tspi->bytes_per_word;
		for (count = 0; count < max_n_32bit; count++) {
			u32 x = 0;
			for (i = 0; nbytes && (i < tspi->bytes_per_word);
							i++, nbytes--)
				x |= (u32)(*tx_buf++) << (i * 8);
			tegra_slink_writel(tspi, x, SLINK_TX_FIFO);
		}
	}
	tspi->cur_tx_pos += written_words * tspi->bytes_per_word;
	return written_words;
}

static unsigned int tegra_slink_read_rx_fifo_to_client_rxbuf(
		struct tegra_slink_data *tspi, struct spi_transfer *t)
{
	unsigned rx_full_count;
	u32 fifo_status;
	unsigned i, count;
	unsigned int read_words = 0;
	unsigned len;
	u8 *rx_buf = (u8 *)t->rx_buf + tspi->cur_rx_pos;

	fifo_status = tegra_slink_readl(tspi, SLINK_STATUS2);
	rx_full_count = SLINK_RX_FIFO_FULL_COUNT(fifo_status);
	if (tspi->is_packed) {
		len = tspi->curr_dma_words * tspi->bytes_per_word;
		for (count = 0; count < rx_full_count; count++) {
			u32 x = tegra_slink_readl(tspi, SLINK_RX_FIFO);
			for (i = 0; len && (i < 4); i++, len--)
				*rx_buf++ = (x >> i*8) & 0xFF;
		}
		tspi->cur_rx_pos += tspi->curr_dma_words * tspi->bytes_per_word;
		read_words += tspi->curr_dma_words;
	} else {
		for (count = 0; count < rx_full_count; count++) {
			u32 x = tegra_slink_readl(tspi, SLINK_RX_FIFO);
			for (i = 0; (i < tspi->bytes_per_word); i++)
				*rx_buf++ = (x >> (i*8)) & 0xFF;
		}
		tspi->cur_rx_pos += rx_full_count * tspi->bytes_per_word;
		read_words += rx_full_count;
	}
	return read_words;
}

static void tegra_slink_copy_client_txbuf_to_spi_txbuf(
		struct tegra_slink_data *tspi, struct spi_transfer *t)
{
	/* Make the dma buffer to read by cpu */
	dma_sync_single_for_cpu(tspi->dev, tspi->tx_dma_phys,
				tspi->dma_buf_size, DMA_TO_DEVICE);

	if (tspi->is_packed) {
		unsigned len = tspi->curr_dma_words * tspi->bytes_per_word;
		memcpy(tspi->tx_dma_buf, t->tx_buf + tspi->cur_pos, len);
	} else {
		unsigned int i;
		unsigned int count;
		u8 *tx_buf = (u8 *)t->tx_buf + tspi->cur_tx_pos;
		unsigned consume = tspi->curr_dma_words * tspi->bytes_per_word;

		for (count = 0; count < tspi->curr_dma_words; count++) {
			u32 x = 0;
			for (i = 0; consume && (i < tspi->bytes_per_word);
							i++, consume--)
				x |= (u32)(*tx_buf++) << (i * 8);
			tspi->tx_dma_buf[count] = x;
		}
	}
	tspi->cur_tx_pos += tspi->curr_dma_words * tspi->bytes_per_word;

	/* Make the dma buffer to read by dma */
	dma_sync_single_for_device(tspi->dev, tspi->tx_dma_phys,
				tspi->dma_buf_size, DMA_TO_DEVICE);
}

static void tegra_slink_copy_spi_rxbuf_to_client_rxbuf(
		struct tegra_slink_data *tspi, struct spi_transfer *t)
{
	unsigned len;

	/* Make the dma buffer to read by cpu */
	dma_sync_single_for_cpu(tspi->dev, tspi->rx_dma_phys,
		tspi->dma_buf_size, DMA_FROM_DEVICE);

	if (tspi->is_packed) {
		len = tspi->curr_dma_words * tspi->bytes_per_word;
		memcpy(t->rx_buf + tspi->cur_rx_pos, tspi->rx_dma_buf, len);
	} else {
		unsigned int i;
		unsigned int count;
		unsigned char *rx_buf = t->rx_buf + tspi->cur_rx_pos;
		u32 rx_mask = ((u32)1 << t->bits_per_word) - 1;

		for (count = 0; count < tspi->curr_dma_words; count++) {
			u32 x = tspi->rx_dma_buf[count] & rx_mask;
			for (i = 0; (i < tspi->bytes_per_word); i++)
				*rx_buf++ = (x >> (i*8)) & 0xFF;
		}
	}
	tspi->cur_rx_pos += tspi->curr_dma_words * tspi->bytes_per_word;

	/* Make the dma buffer to read by dma */
	dma_sync_single_for_device(tspi->dev, tspi->rx_dma_phys,
		tspi->dma_buf_size, DMA_FROM_DEVICE);
}

static void tegra_slink_dma_complete(void *args)
{
	struct completion *dma_complete = args;

	complete(dma_complete);
}

static int tegra_slink_start_tx_dma(struct tegra_slink_data *tspi, int len)
{
	reinit_completion(&tspi->tx_dma_complete);
	tspi->tx_dma_desc = dmaengine_prep_slave_single(tspi->tx_dma_chan,
				tspi->tx_dma_phys, len, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT |  DMA_CTRL_ACK);
	if (!tspi->tx_dma_desc) {
		dev_err(tspi->dev, "Not able to get desc for Tx\n");
		return -EIO;
	}

	tspi->tx_dma_desc->callback = tegra_slink_dma_complete;
	tspi->tx_dma_desc->callback_param = &tspi->tx_dma_complete;

	dmaengine_submit(tspi->tx_dma_desc);
	dma_async_issue_pending(tspi->tx_dma_chan);
	return 0;
}

static int tegra_slink_start_rx_dma(struct tegra_slink_data *tspi, int len)
{
	reinit_completion(&tspi->rx_dma_complete);
	tspi->rx_dma_desc = dmaengine_prep_slave_single(tspi->rx_dma_chan,
				tspi->rx_dma_phys, len, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT |  DMA_CTRL_ACK);
	if (!tspi->rx_dma_desc) {
		dev_err(tspi->dev, "Not able to get desc for Rx\n");
		return -EIO;
	}

	tspi->rx_dma_desc->callback = tegra_slink_dma_complete;
	tspi->rx_dma_desc->callback_param = &tspi->rx_dma_complete;

	dmaengine_submit(tspi->rx_dma_desc);
	dma_async_issue_pending(tspi->rx_dma_chan);
	return 0;
}

static int tegra_slink_start_dma_based_transfer(
		struct tegra_slink_data *tspi, struct spi_transfer *t)
{
	u32 val;
	unsigned int len;
	int ret = 0;
	u32 status;

	/* Make sure that Rx and Tx fifo are empty */
	status = tegra_slink_readl(tspi, SLINK_STATUS);
	if ((status & SLINK_FIFO_EMPTY) != SLINK_FIFO_EMPTY) {
		dev_err(tspi->dev, "Rx/Tx fifo are not empty status 0x%08x\n",
			(unsigned)status);
		return -EIO;
	}

	val = SLINK_DMA_BLOCK_SIZE(tspi->curr_dma_words - 1);
	val |= tspi->packed_size;
	if (tspi->is_packed)
		len = DIV_ROUND_UP(tspi->curr_dma_words * tspi->bytes_per_word,
					4) * 4;
	else
		len = tspi->curr_dma_words * 4;

	/* Set attention level based on length of transfer */
	if (len & 0xF)
		val |= SLINK_TX_TRIG_1 | SLINK_RX_TRIG_1;
	else if (((len) >> 4) & 0x1)
		val |= SLINK_TX_TRIG_4 | SLINK_RX_TRIG_4;
	else
		val |= SLINK_TX_TRIG_8 | SLINK_RX_TRIG_8;

	if (tspi->cur_direction & DATA_DIR_TX)
		val |= SLINK_IE_TXC;

	if (tspi->cur_direction & DATA_DIR_RX)
		val |= SLINK_IE_RXC;

	tegra_slink_writel(tspi, val, SLINK_DMA_CTL);
	tspi->dma_control_reg = val;

	if (tspi->cur_direction & DATA_DIR_TX) {
		tegra_slink_copy_client_txbuf_to_spi_txbuf(tspi, t);
		wmb();
		ret = tegra_slink_start_tx_dma(tspi, len);
		if (ret < 0) {
			dev_err(tspi->dev,
				"Starting tx dma failed, err %d\n", ret);
			return ret;
		}

		/* Wait for tx fifo to be fill before starting slink */
		status = tegra_slink_readl(tspi, SLINK_STATUS);
		while (!(status & SLINK_TX_FULL))
			status = tegra_slink_readl(tspi, SLINK_STATUS);
	}

	if (tspi->cur_direction & DATA_DIR_RX) {
		/* Make the dma buffer to read by dma */
		dma_sync_single_for_device(tspi->dev, tspi->rx_dma_phys,
				tspi->dma_buf_size, DMA_FROM_DEVICE);

		ret = tegra_slink_start_rx_dma(tspi, len);
		if (ret < 0) {
			dev_err(tspi->dev,
				"Starting rx dma failed, err %d\n", ret);
			if (tspi->cur_direction & DATA_DIR_TX)
				dmaengine_terminate_all(tspi->tx_dma_chan);
			return ret;
		}
	}
	tspi->is_curr_dma_xfer = true;
	if (tspi->is_packed) {
		val |= SLINK_PACKED;
		tegra_slink_writel(tspi, val, SLINK_DMA_CTL);
		/* HW need small delay after settign Packed mode */
		udelay(1);
	}
	tspi->dma_control_reg = val;

	val |= SLINK_DMA_EN;
	tegra_slink_writel(tspi, val, SLINK_DMA_CTL);
	return ret;
}

static int tegra_slink_start_cpu_based_transfer(
		struct tegra_slink_data *tspi, struct spi_transfer *t)
{
	u32 val;
	unsigned cur_words;

	val = tspi->packed_size;
	if (tspi->cur_direction & DATA_DIR_TX)
		val |= SLINK_IE_TXC;

	if (tspi->cur_direction & DATA_DIR_RX)
		val |= SLINK_IE_RXC;

	tegra_slink_writel(tspi, val, SLINK_DMA_CTL);
	tspi->dma_control_reg = val;

	if (tspi->cur_direction & DATA_DIR_TX)
		cur_words = tegra_slink_fill_tx_fifo_from_client_txbuf(tspi, t);
	else
		cur_words = tspi->curr_dma_words;
	val |= SLINK_DMA_BLOCK_SIZE(cur_words - 1);
	tegra_slink_writel(tspi, val, SLINK_DMA_CTL);
	tspi->dma_control_reg = val;

	tspi->is_curr_dma_xfer = false;
	if (tspi->is_packed) {
		val |= SLINK_PACKED;
		tegra_slink_writel(tspi, val, SLINK_DMA_CTL);
		udelay(1);
		wmb();
	}
	tspi->dma_control_reg = val;
	val |= SLINK_DMA_EN;
	tegra_slink_writel(tspi, val, SLINK_DMA_CTL);
	return 0;
}

static int tegra_slink_init_dma_param(struct tegra_slink_data *tspi,
			bool dma_to_memory)
{
	struct dma_chan *dma_chan;
	u32 *dma_buf;
	dma_addr_t dma_phys;
	int ret;
	struct dma_slave_config dma_sconfig;

	dma_chan = dma_request_chan(tspi->dev, dma_to_memory ? "rx" : "tx");
	if (IS_ERR(dma_chan))
		return dev_err_probe(tspi->dev, PTR_ERR(dma_chan),
				     "Dma channel is not available\n");

	dma_buf = dma_alloc_coherent(tspi->dev, tspi->dma_buf_size,
				&dma_phys, GFP_KERNEL);
	if (!dma_buf) {
		dev_err(tspi->dev, " Not able to allocate the dma buffer\n");
		dma_release_channel(dma_chan);
		return -ENOMEM;
	}

	if (dma_to_memory) {
		dma_sconfig.src_addr = tspi->phys + SLINK_RX_FIFO;
		dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		dma_sconfig.src_maxburst = 0;
	} else {
		dma_sconfig.dst_addr = tspi->phys + SLINK_TX_FIFO;
		dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		dma_sconfig.dst_maxburst = 0;
	}

	ret = dmaengine_slave_config(dma_chan, &dma_sconfig);
	if (ret)
		goto scrub;
	if (dma_to_memory) {
		tspi->rx_dma_chan = dma_chan;
		tspi->rx_dma_buf = dma_buf;
		tspi->rx_dma_phys = dma_phys;
	} else {
		tspi->tx_dma_chan = dma_chan;
		tspi->tx_dma_buf = dma_buf;
		tspi->tx_dma_phys = dma_phys;
	}
	return 0;

scrub:
	dma_free_coherent(tspi->dev, tspi->dma_buf_size, dma_buf, dma_phys);
	dma_release_channel(dma_chan);
	return ret;
}

static void tegra_slink_deinit_dma_param(struct tegra_slink_data *tspi,
	bool dma_to_memory)
{
	u32 *dma_buf;
	dma_addr_t dma_phys;
	struct dma_chan *dma_chan;

	if (dma_to_memory) {
		dma_buf = tspi->rx_dma_buf;
		dma_chan = tspi->rx_dma_chan;
		dma_phys = tspi->rx_dma_phys;
		tspi->rx_dma_chan = NULL;
		tspi->rx_dma_buf = NULL;
	} else {
		dma_buf = tspi->tx_dma_buf;
		dma_chan = tspi->tx_dma_chan;
		dma_phys = tspi->tx_dma_phys;
		tspi->tx_dma_buf = NULL;
		tspi->tx_dma_chan = NULL;
	}
	if (!dma_chan)
		return;

	dma_free_coherent(tspi->dev, tspi->dma_buf_size, dma_buf, dma_phys);
	dma_release_channel(dma_chan);
}

static int tegra_slink_start_transfer_one(struct spi_device *spi,
		struct spi_transfer *t)
{
	struct tegra_slink_data *tspi = spi_master_get_devdata(spi->master);
	u32 speed;
	u8 bits_per_word;
	unsigned total_fifo_words;
	int ret;
	u32 command;
	u32 command2;

	bits_per_word = t->bits_per_word;
	speed = t->speed_hz;
	if (speed != tspi->cur_speed) {
		clk_set_rate(tspi->clk, speed * 4);
		tspi->cur_speed = speed;
	}

	tspi->cur_spi = spi;
	tspi->cur_pos = 0;
	tspi->cur_rx_pos = 0;
	tspi->cur_tx_pos = 0;
	tspi->curr_xfer = t;
	total_fifo_words = tegra_slink_calculate_curr_xfer_param(spi, tspi, t);

	command = tspi->command_reg;
	command &= ~SLINK_BIT_LENGTH(~0);
	command |= SLINK_BIT_LENGTH(bits_per_word - 1);

	command2 = tspi->command2_reg;
	command2 &= ~(SLINK_RXEN | SLINK_TXEN);

	tspi->cur_direction = 0;
	if (t->rx_buf) {
		command2 |= SLINK_RXEN;
		tspi->cur_direction |= DATA_DIR_RX;
	}
	if (t->tx_buf) {
		command2 |= SLINK_TXEN;
		tspi->cur_direction |= DATA_DIR_TX;
	}

	/*
	 * Writing to the command2 register bevore the command register prevents
	 * a spike in chip_select line 0. This selects the chip_select line
	 * before changing the chip_select value.
	 */
	tegra_slink_writel(tspi, command2, SLINK_COMMAND2);
	tspi->command2_reg = command2;

	tegra_slink_writel(tspi, command, SLINK_COMMAND);
	tspi->command_reg = command;

	if (total_fifo_words > SLINK_FIFO_DEPTH)
		ret = tegra_slink_start_dma_based_transfer(tspi, t);
	else
		ret = tegra_slink_start_cpu_based_transfer(tspi, t);
	return ret;
}

static int tegra_slink_setup(struct spi_device *spi)
{
	static const u32 cs_pol_bit[MAX_CHIP_SELECT] = {
			SLINK_CS_POLARITY,
			SLINK_CS_POLARITY1,
			SLINK_CS_POLARITY2,
			SLINK_CS_POLARITY3,
	};

	struct tegra_slink_data *tspi = spi_master_get_devdata(spi->master);
	u32 val;
	unsigned long flags;
	int ret;

	dev_dbg(&spi->dev, "setup %d bpw, %scpol, %scpha, %dHz\n",
		spi->bits_per_word,
		spi->mode & SPI_CPOL ? "" : "~",
		spi->mode & SPI_CPHA ? "" : "~",
		spi->max_speed_hz);

	ret = pm_runtime_get_sync(tspi->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(tspi->dev);
		dev_err(tspi->dev, "pm runtime failed, e = %d\n", ret);
		return ret;
	}

	spin_lock_irqsave(&tspi->lock, flags);
	val = tspi->def_command_reg;
	if (spi->mode & SPI_CS_HIGH)
		val |= cs_pol_bit[spi->chip_select];
	else
		val &= ~cs_pol_bit[spi->chip_select];
	tspi->def_command_reg = val;
	tegra_slink_writel(tspi, tspi->def_command_reg, SLINK_COMMAND);
	spin_unlock_irqrestore(&tspi->lock, flags);

	pm_runtime_put(tspi->dev);
	return 0;
}

static int tegra_slink_prepare_message(struct spi_master *master,
				       struct spi_message *msg)
{
	struct tegra_slink_data *tspi = spi_master_get_devdata(master);
	struct spi_device *spi = msg->spi;

	tegra_slink_clear_status(tspi);

	tspi->command_reg = tspi->def_command_reg;
	tspi->command_reg |= SLINK_CS_SW | SLINK_CS_VALUE;

	tspi->command2_reg = tspi->def_command2_reg;
	tspi->command2_reg |= SLINK_SS_EN_CS(spi->chip_select);

	tspi->command_reg &= ~SLINK_MODES;
	if (spi->mode & SPI_CPHA)
		tspi->command_reg |= SLINK_CK_SDA;

	if (spi->mode & SPI_CPOL)
		tspi->command_reg |= SLINK_IDLE_SCLK_DRIVE_HIGH;
	else
		tspi->command_reg |= SLINK_IDLE_SCLK_DRIVE_LOW;

	return 0;
}

static int tegra_slink_transfer_one(struct spi_master *master,
				    struct spi_device *spi,
				    struct spi_transfer *xfer)
{
	struct tegra_slink_data *tspi = spi_master_get_devdata(master);
	int ret;

	reinit_completion(&tspi->xfer_completion);
	ret = tegra_slink_start_transfer_one(spi, xfer);
	if (ret < 0) {
		dev_err(tspi->dev,
			"spi can not start transfer, err %d\n", ret);
		return ret;
	}

	ret = wait_for_completion_timeout(&tspi->xfer_completion,
					  SLINK_DMA_TIMEOUT);
	if (WARN_ON(ret == 0)) {
		dev_err(tspi->dev,
			"spi transfer timeout, err %d\n", ret);
		return -EIO;
	}

	if (tspi->tx_status)
		return tspi->tx_status;
	if (tspi->rx_status)
		return tspi->rx_status;

	return 0;
}

static int tegra_slink_unprepare_message(struct spi_master *master,
					 struct spi_message *msg)
{
	struct tegra_slink_data *tspi = spi_master_get_devdata(master);

	tegra_slink_writel(tspi, tspi->def_command_reg, SLINK_COMMAND);
	tegra_slink_writel(tspi, tspi->def_command2_reg, SLINK_COMMAND2);

	return 0;
}

static irqreturn_t handle_cpu_based_xfer(struct tegra_slink_data *tspi)
{
	struct spi_transfer *t = tspi->curr_xfer;
	unsigned long flags;

	spin_lock_irqsave(&tspi->lock, flags);
	if (tspi->tx_status ||  tspi->rx_status ||
				(tspi->status_reg & SLINK_BSY)) {
		dev_err(tspi->dev,
			"CpuXfer ERROR bit set 0x%x\n", tspi->status_reg);
		dev_err(tspi->dev,
			"CpuXfer 0x%08x:0x%08x:0x%08x\n", tspi->command_reg,
				tspi->command2_reg, tspi->dma_control_reg);
		reset_control_assert(tspi->rst);
		udelay(2);
		reset_control_deassert(tspi->rst);
		complete(&tspi->xfer_completion);
		goto exit;
	}

	if (tspi->cur_direction & DATA_DIR_RX)
		tegra_slink_read_rx_fifo_to_client_rxbuf(tspi, t);

	if (tspi->cur_direction & DATA_DIR_TX)
		tspi->cur_pos = tspi->cur_tx_pos;
	else
		tspi->cur_pos = tspi->cur_rx_pos;

	if (tspi->cur_pos == t->len) {
		complete(&tspi->xfer_completion);
		goto exit;
	}

	tegra_slink_calculate_curr_xfer_param(tspi->cur_spi, tspi, t);
	tegra_slink_start_cpu_based_transfer(tspi, t);
exit:
	spin_unlock_irqrestore(&tspi->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t handle_dma_based_xfer(struct tegra_slink_data *tspi)
{
	struct spi_transfer *t = tspi->curr_xfer;
	long wait_status;
	int err = 0;
	unsigned total_fifo_words;
	unsigned long flags;

	/* Abort dmas if any error */
	if (tspi->cur_direction & DATA_DIR_TX) {
		if (tspi->tx_status) {
			dmaengine_terminate_all(tspi->tx_dma_chan);
			err += 1;
		} else {
			wait_status = wait_for_completion_interruptible_timeout(
				&tspi->tx_dma_complete, SLINK_DMA_TIMEOUT);
			if (wait_status <= 0) {
				dmaengine_terminate_all(tspi->tx_dma_chan);
				dev_err(tspi->dev, "TxDma Xfer failed\n");
				err += 1;
			}
		}
	}

	if (tspi->cur_direction & DATA_DIR_RX) {
		if (tspi->rx_status) {
			dmaengine_terminate_all(tspi->rx_dma_chan);
			err += 2;
		} else {
			wait_status = wait_for_completion_interruptible_timeout(
				&tspi->rx_dma_complete, SLINK_DMA_TIMEOUT);
			if (wait_status <= 0) {
				dmaengine_terminate_all(tspi->rx_dma_chan);
				dev_err(tspi->dev, "RxDma Xfer failed\n");
				err += 2;
			}
		}
	}

	spin_lock_irqsave(&tspi->lock, flags);
	if (err) {
		dev_err(tspi->dev,
			"DmaXfer: ERROR bit set 0x%x\n", tspi->status_reg);
		dev_err(tspi->dev,
			"DmaXfer 0x%08x:0x%08x:0x%08x\n", tspi->command_reg,
				tspi->command2_reg, tspi->dma_control_reg);
		reset_control_assert(tspi->rst);
		udelay(2);
		reset_control_assert(tspi->rst);
		complete(&tspi->xfer_completion);
		spin_unlock_irqrestore(&tspi->lock, flags);
		return IRQ_HANDLED;
	}

	if (tspi->cur_direction & DATA_DIR_RX)
		tegra_slink_copy_spi_rxbuf_to_client_rxbuf(tspi, t);

	if (tspi->cur_direction & DATA_DIR_TX)
		tspi->cur_pos = tspi->cur_tx_pos;
	else
		tspi->cur_pos = tspi->cur_rx_pos;

	if (tspi->cur_pos == t->len) {
		complete(&tspi->xfer_completion);
		goto exit;
	}

	/* Continue transfer in current message */
	total_fifo_words = tegra_slink_calculate_curr_xfer_param(tspi->cur_spi,
							tspi, t);
	if (total_fifo_words > SLINK_FIFO_DEPTH)
		err = tegra_slink_start_dma_based_transfer(tspi, t);
	else
		err = tegra_slink_start_cpu_based_transfer(tspi, t);

exit:
	spin_unlock_irqrestore(&tspi->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t tegra_slink_isr_thread(int irq, void *context_data)
{
	struct tegra_slink_data *tspi = context_data;

	if (!tspi->is_curr_dma_xfer)
		return handle_cpu_based_xfer(tspi);
	return handle_dma_based_xfer(tspi);
}

static irqreturn_t tegra_slink_isr(int irq, void *context_data)
{
	struct tegra_slink_data *tspi = context_data;

	tspi->status_reg = tegra_slink_readl(tspi, SLINK_STATUS);
	if (tspi->cur_direction & DATA_DIR_TX)
		tspi->tx_status = tspi->status_reg &
					(SLINK_TX_OVF | SLINK_TX_UNF);

	if (tspi->cur_direction & DATA_DIR_RX)
		tspi->rx_status = tspi->status_reg &
					(SLINK_RX_OVF | SLINK_RX_UNF);
	tegra_slink_clear_status(tspi);

	return IRQ_WAKE_THREAD;
}

static const struct tegra_slink_chip_data tegra30_spi_cdata = {
	.cs_hold_time = true,
};

static const struct tegra_slink_chip_data tegra20_spi_cdata = {
	.cs_hold_time = false,
};

static const struct of_device_id tegra_slink_of_match[] = {
	{ .compatible = "nvidia,tegra30-slink", .data = &tegra30_spi_cdata, },
	{ .compatible = "nvidia,tegra20-slink", .data = &tegra20_spi_cdata, },
	{}
};
MODULE_DEVICE_TABLE(of, tegra_slink_of_match);

static int tegra_slink_probe(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct tegra_slink_data	*tspi;
	struct resource		*r;
	int ret, spi_irq;
	const struct tegra_slink_chip_data *cdata = NULL;

	cdata = of_device_get_match_data(&pdev->dev);

	master = spi_alloc_master(&pdev->dev, sizeof(*tspi));
	if (!master) {
		dev_err(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->setup = tegra_slink_setup;
	master->prepare_message = tegra_slink_prepare_message;
	master->transfer_one = tegra_slink_transfer_one;
	master->unprepare_message = tegra_slink_unprepare_message;
	master->auto_runtime_pm = true;
	master->num_chipselect = MAX_CHIP_SELECT;

	platform_set_drvdata(pdev, master);
	tspi = spi_master_get_devdata(master);
	tspi->master = master;
	tspi->dev = &pdev->dev;
	tspi->chip_data = cdata;
	spin_lock_init(&tspi->lock);

	if (of_property_read_u32(tspi->dev->of_node, "spi-max-frequency",
				 &master->max_speed_hz))
		master->max_speed_hz = 25000000; /* 25MHz */

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "No IO memory resource\n");
		ret = -ENODEV;
		goto exit_free_master;
	}
	tspi->phys = r->start;
	tspi->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(tspi->base)) {
		ret = PTR_ERR(tspi->base);
		goto exit_free_master;
	}

	/* disabled clock may cause interrupt storm upon request */
	tspi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tspi->clk)) {
		ret = PTR_ERR(tspi->clk);
		dev_err(&pdev->dev, "Can not get clock %d\n", ret);
		goto exit_free_master;
	}

	tspi->rst = devm_reset_control_get_exclusive(&pdev->dev, "spi");
	if (IS_ERR(tspi->rst)) {
		dev_err(&pdev->dev, "can not get reset\n");
		ret = PTR_ERR(tspi->rst);
		goto exit_free_master;
	}

	tspi->max_buf_size = SLINK_FIFO_DEPTH << 2;
	tspi->dma_buf_size = DEFAULT_SPI_DMA_BUF_LEN;

	ret = tegra_slink_init_dma_param(tspi, true);
	if (ret < 0)
		goto exit_free_master;
	ret = tegra_slink_init_dma_param(tspi, false);
	if (ret < 0)
		goto exit_rx_dma_free;
	tspi->max_buf_size = tspi->dma_buf_size;
	init_completion(&tspi->tx_dma_complete);
	init_completion(&tspi->rx_dma_complete);

	init_completion(&tspi->xfer_completion);

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "pm runtime get failed, e = %d\n", ret);
		goto exit_pm_disable;
	}

	reset_control_assert(tspi->rst);
	udelay(2);
	reset_control_deassert(tspi->rst);

	spi_irq = platform_get_irq(pdev, 0);
	tspi->irq = spi_irq;
	ret = request_threaded_irq(tspi->irq, tegra_slink_isr,
				   tegra_slink_isr_thread, IRQF_ONESHOT,
				   dev_name(&pdev->dev), tspi);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register ISR for IRQ %d\n",
			tspi->irq);
		goto exit_pm_put;
	}

	tspi->def_command_reg  = SLINK_M_S;
	tspi->def_command2_reg = SLINK_CS_ACTIVE_BETWEEN;
	tegra_slink_writel(tspi, tspi->def_command_reg, SLINK_COMMAND);
	tegra_slink_writel(tspi, tspi->def_command2_reg, SLINK_COMMAND2);

	master->dev.of_node = pdev->dev.of_node;
	ret = spi_register_master(master);
	if (ret < 0) {
		dev_err(&pdev->dev, "can not register to master err %d\n", ret);
		goto exit_free_irq;
	}

	pm_runtime_put(&pdev->dev);

	return ret;

exit_free_irq:
	free_irq(spi_irq, tspi);
exit_pm_put:
	pm_runtime_put(&pdev->dev);
exit_pm_disable:
	pm_runtime_disable(&pdev->dev);

	tegra_slink_deinit_dma_param(tspi, false);
exit_rx_dma_free:
	tegra_slink_deinit_dma_param(tspi, true);
exit_free_master:
	spi_master_put(master);
	return ret;
}

static int tegra_slink_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct tegra_slink_data	*tspi = spi_master_get_devdata(master);

	spi_unregister_master(master);

	free_irq(tspi->irq, tspi);

	pm_runtime_disable(&pdev->dev);

	if (tspi->tx_dma_chan)
		tegra_slink_deinit_dma_param(tspi, false);

	if (tspi->rx_dma_chan)
		tegra_slink_deinit_dma_param(tspi, true);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_slink_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);

	return spi_master_suspend(master);
}

static int tegra_slink_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_slink_data *tspi = spi_master_get_devdata(master);
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		dev_err(dev, "pm runtime failed, e = %d\n", ret);
		return ret;
	}
	tegra_slink_writel(tspi, tspi->command_reg, SLINK_COMMAND);
	tegra_slink_writel(tspi, tspi->command2_reg, SLINK_COMMAND2);
	pm_runtime_put(dev);

	return spi_master_resume(master);
}
#endif

static int __maybe_unused tegra_slink_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_slink_data *tspi = spi_master_get_devdata(master);

	/* Flush all write which are in PPSB queue by reading back */
	tegra_slink_readl(tspi, SLINK_MAS_DATA);

	clk_disable_unprepare(tspi->clk);
	return 0;
}

static int __maybe_unused tegra_slink_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_slink_data *tspi = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(tspi->clk);
	if (ret < 0) {
		dev_err(tspi->dev, "clk_prepare failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static const struct dev_pm_ops slink_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_slink_runtime_suspend,
		tegra_slink_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_slink_suspend, tegra_slink_resume)
};
static struct platform_driver tegra_slink_driver = {
	.driver = {
		.name		= "spi-tegra-slink",
		.pm		= &slink_pm_ops,
		.of_match_table	= tegra_slink_of_match,
	},
	.probe =	tegra_slink_probe,
	.remove =	tegra_slink_remove,
};
module_platform_driver(tegra_slink_driver);

MODULE_ALIAS("platform:spi-tegra-slink");
MODULE_DESCRIPTION("NVIDIA Tegra20/Tegra30 SLINK Controller Driver");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
