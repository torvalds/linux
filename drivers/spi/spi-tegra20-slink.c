/*
 * SPI driver for Nvidia's Tegra20/Tegra30 SLINK Controller.
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-tegra.h>
#include <linux/clk/tegra.h>

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
	void __iomem				*base;
	phys_addr_t				phys;
	unsigned				irq;
	int					dma_req_sel;
	u32					spi_max_frequency;
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
	bool					is_hw_based_cs;

	struct completion			rx_dma_complete;
	struct completion			tx_dma_complete;

	u32					tx_status;
	u32					rx_status;
	u32					status_reg;
	bool					is_packed;
	unsigned long				packed_size;

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

static int tegra_slink_runtime_suspend(struct device *dev);
static int tegra_slink_runtime_resume(struct device *dev);

static inline unsigned long tegra_slink_readl(struct tegra_slink_data *tspi,
		unsigned long reg)
{
	return readl(tspi->base + reg);
}

static inline void tegra_slink_writel(struct tegra_slink_data *tspi,
		unsigned long val, unsigned long reg)
{
	writel(val, tspi->base + reg);

	/* Read back register to make sure that register writes completed */
	if (reg != SLINK_TX_FIFO)
		readl(tspi->base + SLINK_MAS_DATA);
}

static void tegra_slink_clear_status(struct tegra_slink_data *tspi)
{
	unsigned long val;
	unsigned long val_write = 0;

	val = tegra_slink_readl(tspi, SLINK_STATUS);

	/* Write 1 to clear status register */
	val_write = SLINK_RDY | SLINK_FIFO_ERROR;
	tegra_slink_writel(tspi, val_write, SLINK_STATUS);
}

static unsigned long tegra_slink_get_packed_size(struct tegra_slink_data *tspi,
				  struct spi_transfer *t)
{
	unsigned long val;

	switch (tspi->bytes_per_word) {
	case 0:
		val = SLINK_PACK_SIZE_4;
		break;
	case 1:
		val = SLINK_PACK_SIZE_8;
		break;
	case 2:
		val = SLINK_PACK_SIZE_16;
		break;
	case 4:
		val = SLINK_PACK_SIZE_32;
		break;
	default:
		val = 0;
	}
	return val;
}

static unsigned tegra_slink_calculate_curr_xfer_param(
	struct spi_device *spi, struct tegra_slink_data *tspi,
	struct spi_transfer *t)
{
	unsigned remain_len = t->len - tspi->cur_pos;
	unsigned max_word;
	unsigned bits_per_word ;
	unsigned max_len;
	unsigned total_fifo_words;

	bits_per_word = t->bits_per_word ? t->bits_per_word :
						spi->bits_per_word;
	tspi->bytes_per_word = (bits_per_word - 1) / 8 + 1;

	if (bits_per_word == 8 || bits_per_word == 16) {
		tspi->is_packed = 1;
		tspi->words_per_32bit = 32/bits_per_word;
	} else {
		tspi->is_packed = 0;
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
	unsigned long fifo_status;
	unsigned max_n_32bit;
	unsigned i, count;
	unsigned long x;
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
			x = 0;
			for (i = 0; (i < 4) && nbytes; i++, nbytes--)
				x |= (*tx_buf++) << (i*8);
			tegra_slink_writel(tspi, x, SLINK_TX_FIFO);
		}
	} else {
		max_n_32bit = min(tspi->curr_dma_words,  tx_empty_count);
		written_words = max_n_32bit;
		nbytes = written_words * tspi->bytes_per_word;
		for (count = 0; count < max_n_32bit; count++) {
			x = 0;
			for (i = 0; nbytes && (i < tspi->bytes_per_word);
							i++, nbytes--)
				x |= ((*tx_buf++) << i*8);
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
	unsigned long fifo_status;
	unsigned i, count;
	unsigned long x;
	unsigned int read_words = 0;
	unsigned len;
	u8 *rx_buf = (u8 *)t->rx_buf + tspi->cur_rx_pos;

	fifo_status = tegra_slink_readl(tspi, SLINK_STATUS2);
	rx_full_count = SLINK_RX_FIFO_FULL_COUNT(fifo_status);
	if (tspi->is_packed) {
		len = tspi->curr_dma_words * tspi->bytes_per_word;
		for (count = 0; count < rx_full_count; count++) {
			x = tegra_slink_readl(tspi, SLINK_RX_FIFO);
			for (i = 0; len && (i < 4); i++, len--)
				*rx_buf++ = (x >> i*8) & 0xFF;
		}
		tspi->cur_rx_pos += tspi->curr_dma_words * tspi->bytes_per_word;
		read_words += tspi->curr_dma_words;
	} else {
		unsigned int bits_per_word;

		bits_per_word = t->bits_per_word ? t->bits_per_word :
						tspi->cur_spi->bits_per_word;
		for (count = 0; count < rx_full_count; count++) {
			x = tegra_slink_readl(tspi, SLINK_RX_FIFO);
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
	unsigned len;

	/* Make the dma buffer to read by cpu */
	dma_sync_single_for_cpu(tspi->dev, tspi->tx_dma_phys,
				tspi->dma_buf_size, DMA_TO_DEVICE);

	if (tspi->is_packed) {
		len = tspi->curr_dma_words * tspi->bytes_per_word;
		memcpy(tspi->tx_dma_buf, t->tx_buf + tspi->cur_pos, len);
	} else {
		unsigned int i;
		unsigned int count;
		u8 *tx_buf = (u8 *)t->tx_buf + tspi->cur_tx_pos;
		unsigned consume = tspi->curr_dma_words * tspi->bytes_per_word;
		unsigned int x;

		for (count = 0; count < tspi->curr_dma_words; count++) {
			x = 0;
			for (i = 0; consume && (i < tspi->bytes_per_word);
							i++, consume--)
				x |= ((*tx_buf++) << i * 8);
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
		unsigned int x;
		unsigned int rx_mask, bits_per_word;

		bits_per_word = t->bits_per_word ? t->bits_per_word :
						tspi->cur_spi->bits_per_word;
		rx_mask = (1 << bits_per_word) - 1;
		for (count = 0; count < tspi->curr_dma_words; count++) {
			x = tspi->rx_dma_buf[count];
			x &= rx_mask;
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
	INIT_COMPLETION(tspi->tx_dma_complete);
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
	INIT_COMPLETION(tspi->rx_dma_complete);
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
	unsigned long val;
	unsigned long test_val;
	unsigned int len;
	int ret = 0;
	unsigned long status;

	/* Make sure that Rx and Tx fifo are empty */
	status = tegra_slink_readl(tspi, SLINK_STATUS);
	if ((status & SLINK_FIFO_EMPTY) != SLINK_FIFO_EMPTY) {
		dev_err(tspi->dev,
			"Rx/Tx fifo are not empty status 0x%08lx\n", status);
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
		test_val = tegra_slink_readl(tspi, SLINK_STATUS);
		while (!(test_val & SLINK_TX_FULL))
			test_val = tegra_slink_readl(tspi, SLINK_STATUS);
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
	unsigned long val;
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
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_chan = dma_request_channel(mask, NULL, NULL);
	if (!dma_chan) {
		dev_err(tspi->dev,
			"Dma channel is not available, will try later\n");
		return -EPROBE_DEFER;
	}

	dma_buf = dma_alloc_coherent(tspi->dev, tspi->dma_buf_size,
				&dma_phys, GFP_KERNEL);
	if (!dma_buf) {
		dev_err(tspi->dev, " Not able to allocate the dma buffer\n");
		dma_release_channel(dma_chan);
		return -ENOMEM;
	}

	dma_sconfig.slave_id = tspi->dma_req_sel;
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
		struct spi_transfer *t, bool is_first_of_msg,
		bool is_single_xfer)
{
	struct tegra_slink_data *tspi = spi_master_get_devdata(spi->master);
	u32 speed;
	u8 bits_per_word;
	unsigned total_fifo_words;
	int ret;
	struct tegra_spi_device_controller_data *cdata = spi->controller_data;
	unsigned long command;
	unsigned long command2;

	bits_per_word = t->bits_per_word;
	speed = t->speed_hz ? t->speed_hz : spi->max_speed_hz;
	if (!speed)
		speed = tspi->spi_max_frequency;
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

	if (is_first_of_msg) {
		tegra_slink_clear_status(tspi);

		command = tspi->def_command_reg;
		command |= SLINK_BIT_LENGTH(bits_per_word - 1);

		command2 = tspi->def_command2_reg;
		command2 |= SLINK_SS_EN_CS(spi->chip_select);

		/* possibly use the hw based chip select */
		tspi->is_hw_based_cs = false;
		if (cdata && cdata->is_hw_based_cs && is_single_xfer &&
			((tspi->curr_dma_words * tspi->bytes_per_word) ==
						(t->len - tspi->cur_pos))) {
			int setup_count;
			int sts2;

			setup_count = cdata->cs_setup_clk_count >> 1;
			setup_count = max(setup_count, 3);
			command2 |= SLINK_SS_SETUP(setup_count);
			if (tspi->chip_data->cs_hold_time) {
				int hold_count;

				hold_count = cdata->cs_hold_clk_count;
				hold_count = max(hold_count, 0xF);
				sts2 = tegra_slink_readl(tspi, SLINK_STATUS2);
				sts2 &= ~SLINK_SS_HOLD_TIME(0xF);
				sts2 |= SLINK_SS_HOLD_TIME(hold_count);
				tegra_slink_writel(tspi, sts2, SLINK_STATUS2);
			}
			tspi->is_hw_based_cs = true;
		}

		if (tspi->is_hw_based_cs)
			command &= ~SLINK_CS_SW;
		else
			command |= SLINK_CS_SW | SLINK_CS_VALUE;

		command &= ~SLINK_MODES;
		if (spi->mode & SPI_CPHA)
			command |= SLINK_CK_SDA;

		if (spi->mode & SPI_CPOL)
			command |= SLINK_IDLE_SCLK_DRIVE_HIGH;
		else
			command |= SLINK_IDLE_SCLK_DRIVE_LOW;
	} else {
		command = tspi->command_reg;
		command &= ~SLINK_BIT_LENGTH(~0);
		command |= SLINK_BIT_LENGTH(bits_per_word - 1);

		command2 = tspi->command2_reg;
		command2 &= ~(SLINK_RXEN | SLINK_TXEN);
	}

	tegra_slink_writel(tspi, command, SLINK_COMMAND);
	tspi->command_reg = command;

	tspi->cur_direction = 0;
	if (t->rx_buf) {
		command2 |= SLINK_RXEN;
		tspi->cur_direction |= DATA_DIR_RX;
	}
	if (t->tx_buf) {
		command2 |= SLINK_TXEN;
		tspi->cur_direction |= DATA_DIR_TX;
	}
	tegra_slink_writel(tspi, command2, SLINK_COMMAND2);
	tspi->command2_reg = command2;

	if (total_fifo_words > SLINK_FIFO_DEPTH)
		ret = tegra_slink_start_dma_based_transfer(tspi, t);
	else
		ret = tegra_slink_start_cpu_based_transfer(tspi, t);
	return ret;
}

static int tegra_slink_setup(struct spi_device *spi)
{
	struct tegra_slink_data *tspi = spi_master_get_devdata(spi->master);
	unsigned long val;
	unsigned long flags;
	int ret;
	unsigned int cs_pol_bit[MAX_CHIP_SELECT] = {
			SLINK_CS_POLARITY,
			SLINK_CS_POLARITY1,
			SLINK_CS_POLARITY2,
			SLINK_CS_POLARITY3,
	};

	dev_dbg(&spi->dev, "setup %d bpw, %scpol, %scpha, %dHz\n",
		spi->bits_per_word,
		spi->mode & SPI_CPOL ? "" : "~",
		spi->mode & SPI_CPHA ? "" : "~",
		spi->max_speed_hz);

	BUG_ON(spi->chip_select >= MAX_CHIP_SELECT);

	ret = pm_runtime_get_sync(tspi->dev);
	if (ret < 0) {
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

static int tegra_slink_prepare_transfer(struct spi_master *master)
{
	struct tegra_slink_data *tspi = spi_master_get_devdata(master);

	return pm_runtime_get_sync(tspi->dev);
}

static int tegra_slink_unprepare_transfer(struct spi_master *master)
{
	struct tegra_slink_data *tspi = spi_master_get_devdata(master);

	pm_runtime_put(tspi->dev);
	return 0;
}

static int tegra_slink_transfer_one_message(struct spi_master *master,
			struct spi_message *msg)
{
	bool is_first_msg = true;
	int single_xfer;
	struct tegra_slink_data *tspi = spi_master_get_devdata(master);
	struct spi_transfer *xfer;
	struct spi_device *spi = msg->spi;
	int ret;

	msg->status = 0;
	msg->actual_length = 0;
	single_xfer = list_is_singular(&msg->transfers);
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		INIT_COMPLETION(tspi->xfer_completion);
		ret = tegra_slink_start_transfer_one(spi, xfer,
					is_first_msg, single_xfer);
		if (ret < 0) {
			dev_err(tspi->dev,
				"spi can not start transfer, err %d\n", ret);
			goto exit;
		}
		is_first_msg = false;
		ret = wait_for_completion_timeout(&tspi->xfer_completion,
						SLINK_DMA_TIMEOUT);
		if (WARN_ON(ret == 0)) {
			dev_err(tspi->dev,
				"spi trasfer timeout, err %d\n", ret);
			ret = -EIO;
			goto exit;
		}

		if (tspi->tx_status ||  tspi->rx_status) {
			dev_err(tspi->dev, "Error in Transfer\n");
			ret = -EIO;
			goto exit;
		}
		msg->actual_length += xfer->len;
		if (xfer->cs_change && xfer->delay_usecs) {
			tegra_slink_writel(tspi, tspi->def_command_reg,
					SLINK_COMMAND);
			udelay(xfer->delay_usecs);
		}
	}
	ret = 0;
exit:
	tegra_slink_writel(tspi, tspi->def_command_reg, SLINK_COMMAND);
	tegra_slink_writel(tspi, tspi->def_command2_reg, SLINK_COMMAND2);
	msg->status = ret;
	spi_finalize_current_message(master);
	return ret;
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
		tegra_periph_reset_assert(tspi->clk);
		udelay(2);
		tegra_periph_reset_deassert(tspi->clk);
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
		tegra_periph_reset_assert(tspi->clk);
		udelay(2);
		tegra_periph_reset_deassert(tspi->clk);
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

static struct tegra_spi_platform_data *tegra_slink_parse_dt(
		struct platform_device *pdev)
{
	struct tegra_spi_platform_data *pdata;
	const unsigned int *prop;
	struct device_node *np = pdev->dev.of_node;
	u32 of_dma[2];

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "Memory alloc for pdata failed\n");
		return NULL;
	}

	if (of_property_read_u32_array(np, "nvidia,dma-request-selector",
				of_dma, 2) >= 0)
		pdata->dma_req_sel = of_dma[1];

	prop = of_get_property(np, "spi-max-frequency", NULL);
	if (prop)
		pdata->spi_max_frequency = be32_to_cpup(prop);

	return pdata;
}

const struct tegra_slink_chip_data tegra30_spi_cdata = {
	.cs_hold_time = true,
};

const struct tegra_slink_chip_data tegra20_spi_cdata = {
	.cs_hold_time = false,
};

static struct of_device_id tegra_slink_of_match[] = {
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
	struct tegra_spi_platform_data *pdata = pdev->dev.platform_data;
	int ret, spi_irq;
	const struct tegra_slink_chip_data *cdata = NULL;
	const struct of_device_id *match;

	match = of_match_device(of_match_ptr(tegra_slink_of_match), &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Error: No device match found\n");
		return -ENODEV;
	}
	cdata = match->data;
	if (!pdata && pdev->dev.of_node)
		pdata = tegra_slink_parse_dt(pdev);

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data, exiting\n");
		return -ENODEV;
	}

	if (!pdata->spi_max_frequency)
		pdata->spi_max_frequency = 25000000; /* 25MHz */

	master = spi_alloc_master(&pdev->dev, sizeof(*tspi));
	if (!master) {
		dev_err(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->setup = tegra_slink_setup;
	master->prepare_transfer_hardware = tegra_slink_prepare_transfer;
	master->transfer_one_message = tegra_slink_transfer_one_message;
	master->unprepare_transfer_hardware = tegra_slink_unprepare_transfer;
	master->num_chipselect = MAX_CHIP_SELECT;
	master->bus_num = -1;

	dev_set_drvdata(&pdev->dev, master);
	tspi = spi_master_get_devdata(master);
	tspi->master = master;
	tspi->dma_req_sel = pdata->dma_req_sel;
	tspi->dev = &pdev->dev;
	tspi->chip_data = cdata;
	spin_lock_init(&tspi->lock);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "No IO memory resource\n");
		ret = -ENODEV;
		goto exit_free_master;
	}
	tspi->phys = r->start;
	tspi->base = devm_request_and_ioremap(&pdev->dev, r);
	if (!tspi->base) {
		dev_err(&pdev->dev,
			"Cannot request memregion/iomap dma address\n");
		ret = -EADDRNOTAVAIL;
		goto exit_free_master;
	}

	spi_irq = platform_get_irq(pdev, 0);
	tspi->irq = spi_irq;
	ret = request_threaded_irq(tspi->irq, tegra_slink_isr,
			tegra_slink_isr_thread, IRQF_ONESHOT,
			dev_name(&pdev->dev), tspi);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register ISR for IRQ %d\n",
					tspi->irq);
		goto exit_free_master;
	}

	tspi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tspi->clk)) {
		dev_err(&pdev->dev, "can not get clock\n");
		ret = PTR_ERR(tspi->clk);
		goto exit_free_irq;
	}

	tspi->max_buf_size = SLINK_FIFO_DEPTH << 2;
	tspi->dma_buf_size = DEFAULT_SPI_DMA_BUF_LEN;
	tspi->spi_max_frequency = pdata->spi_max_frequency;

	if (pdata->dma_req_sel) {
		ret = tegra_slink_init_dma_param(tspi, true);
		if (ret < 0) {
			dev_err(&pdev->dev, "RxDma Init failed, err %d\n", ret);
			goto exit_free_irq;
		}

		ret = tegra_slink_init_dma_param(tspi, false);
		if (ret < 0) {
			dev_err(&pdev->dev, "TxDma Init failed, err %d\n", ret);
			goto exit_rx_dma_free;
		}
		tspi->max_buf_size = tspi->dma_buf_size;
		init_completion(&tspi->tx_dma_complete);
		init_completion(&tspi->rx_dma_complete);
	}

	init_completion(&tspi->xfer_completion);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = tegra_slink_runtime_resume(&pdev->dev);
		if (ret)
			goto exit_pm_disable;
	}

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm runtime get failed, e = %d\n", ret);
		goto exit_pm_disable;
	}
	tspi->def_command_reg  = SLINK_M_S;
	tspi->def_command2_reg = SLINK_CS_ACTIVE_BETWEEN;
	tegra_slink_writel(tspi, tspi->def_command_reg, SLINK_COMMAND);
	tegra_slink_writel(tspi, tspi->def_command2_reg, SLINK_COMMAND2);
	pm_runtime_put(&pdev->dev);

	master->dev.of_node = pdev->dev.of_node;
	ret = spi_register_master(master);
	if (ret < 0) {
		dev_err(&pdev->dev, "can not register to master err %d\n", ret);
		goto exit_pm_disable;
	}
	return ret;

exit_pm_disable:
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_slink_runtime_suspend(&pdev->dev);
	tegra_slink_deinit_dma_param(tspi, false);
exit_rx_dma_free:
	tegra_slink_deinit_dma_param(tspi, true);
exit_free_irq:
	free_irq(spi_irq, tspi);
exit_free_master:
	spi_master_put(master);
	return ret;
}

static int tegra_slink_remove(struct platform_device *pdev)
{
	struct spi_master *master = dev_get_drvdata(&pdev->dev);
	struct tegra_slink_data	*tspi = spi_master_get_devdata(master);

	free_irq(tspi->irq, tspi);
	spi_unregister_master(master);

	if (tspi->tx_dma_chan)
		tegra_slink_deinit_dma_param(tspi, false);

	if (tspi->rx_dma_chan)
		tegra_slink_deinit_dma_param(tspi, true);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_slink_runtime_suspend(&pdev->dev);

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
		dev_err(dev, "pm runtime failed, e = %d\n", ret);
		return ret;
	}
	tegra_slink_writel(tspi, tspi->command_reg, SLINK_COMMAND);
	tegra_slink_writel(tspi, tspi->command2_reg, SLINK_COMMAND2);
	pm_runtime_put(dev);

	return spi_master_resume(master);
}
#endif

static int tegra_slink_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_slink_data *tspi = spi_master_get_devdata(master);

	/* Flush all write which are in PPSB queue by reading back */
	tegra_slink_readl(tspi, SLINK_MAS_DATA);

	clk_disable_unprepare(tspi->clk);
	return 0;
}

static int tegra_slink_runtime_resume(struct device *dev)
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
		.owner		= THIS_MODULE,
		.pm		= &slink_pm_ops,
		.of_match_table	= of_match_ptr(tegra_slink_of_match),
	},
	.probe =	tegra_slink_probe,
	.remove =	tegra_slink_remove,
};
module_platform_driver(tegra_slink_driver);

MODULE_ALIAS("platform:spi-tegra-slink");
MODULE_DESCRIPTION("NVIDIA Tegra20/Tegra30 SLINK Controller Driver");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
