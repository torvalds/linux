/*
 * SPI driver for NVIDIA's Tegra114 SPI Controller.
 *
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
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

#define SPI_COMMAND1				0x000
#define SPI_BIT_LENGTH(x)			(((x) & 0x1f) << 0)
#define SPI_PACKED				(1 << 5)
#define SPI_TX_EN				(1 << 11)
#define SPI_RX_EN				(1 << 12)
#define SPI_BOTH_EN_BYTE			(1 << 13)
#define SPI_BOTH_EN_BIT				(1 << 14)
#define SPI_LSBYTE_FE				(1 << 15)
#define SPI_LSBIT_FE				(1 << 16)
#define SPI_BIDIROE				(1 << 17)
#define SPI_IDLE_SDA_DRIVE_LOW			(0 << 18)
#define SPI_IDLE_SDA_DRIVE_HIGH			(1 << 18)
#define SPI_IDLE_SDA_PULL_LOW			(2 << 18)
#define SPI_IDLE_SDA_PULL_HIGH			(3 << 18)
#define SPI_IDLE_SDA_MASK			(3 << 18)
#define SPI_CS_SW_VAL				(1 << 20)
#define SPI_CS_SW_HW				(1 << 21)
/* SPI_CS_POL_INACTIVE bits are default high */
						/* n from 0 to 3 */
#define SPI_CS_POL_INACTIVE(n)			(1 << (22 + (n)))
#define SPI_CS_POL_INACTIVE_MASK		(0xF << 22)

#define SPI_CS_SEL_0				(0 << 26)
#define SPI_CS_SEL_1				(1 << 26)
#define SPI_CS_SEL_2				(2 << 26)
#define SPI_CS_SEL_3				(3 << 26)
#define SPI_CS_SEL_MASK				(3 << 26)
#define SPI_CS_SEL(x)				(((x) & 0x3) << 26)
#define SPI_CONTROL_MODE_0			(0 << 28)
#define SPI_CONTROL_MODE_1			(1 << 28)
#define SPI_CONTROL_MODE_2			(2 << 28)
#define SPI_CONTROL_MODE_3			(3 << 28)
#define SPI_CONTROL_MODE_MASK			(3 << 28)
#define SPI_MODE_SEL(x)				(((x) & 0x3) << 28)
#define SPI_M_S					(1 << 30)
#define SPI_PIO					(1 << 31)

#define SPI_COMMAND2				0x004
#define SPI_TX_TAP_DELAY(x)			(((x) & 0x3F) << 6)
#define SPI_RX_TAP_DELAY(x)			(((x) & 0x3F) << 0)

#define SPI_CS_TIMING1				0x008
#define SPI_SETUP_HOLD(setup, hold)		(((setup) << 4) | (hold))
#define SPI_CS_SETUP_HOLD(reg, cs, val)			\
		((((val) & 0xFFu) << ((cs) * 8)) |	\
		((reg) & ~(0xFFu << ((cs) * 8))))

#define SPI_CS_TIMING2				0x00C
#define CYCLES_BETWEEN_PACKETS_0(x)		(((x) & 0x1F) << 0)
#define CS_ACTIVE_BETWEEN_PACKETS_0		(1 << 5)
#define CYCLES_BETWEEN_PACKETS_1(x)		(((x) & 0x1F) << 8)
#define CS_ACTIVE_BETWEEN_PACKETS_1		(1 << 13)
#define CYCLES_BETWEEN_PACKETS_2(x)		(((x) & 0x1F) << 16)
#define CS_ACTIVE_BETWEEN_PACKETS_2		(1 << 21)
#define CYCLES_BETWEEN_PACKETS_3(x)		(((x) & 0x1F) << 24)
#define CS_ACTIVE_BETWEEN_PACKETS_3		(1 << 29)
#define SPI_SET_CS_ACTIVE_BETWEEN_PACKETS(reg, cs, val)		\
		(reg = (((val) & 0x1) << ((cs) * 8 + 5)) |	\
			((reg) & ~(1 << ((cs) * 8 + 5))))
#define SPI_SET_CYCLES_BETWEEN_PACKETS(reg, cs, val)		\
		(reg = (((val) & 0xF) << ((cs) * 8)) |		\
			((reg) & ~(0xF << ((cs) * 8))))

#define SPI_TRANS_STATUS			0x010
#define SPI_BLK_CNT(val)			(((val) >> 0) & 0xFFFF)
#define SPI_SLV_IDLE_COUNT(val)			(((val) >> 16) & 0xFF)
#define SPI_RDY					(1 << 30)

#define SPI_FIFO_STATUS				0x014
#define SPI_RX_FIFO_EMPTY			(1 << 0)
#define SPI_RX_FIFO_FULL			(1 << 1)
#define SPI_TX_FIFO_EMPTY			(1 << 2)
#define SPI_TX_FIFO_FULL			(1 << 3)
#define SPI_RX_FIFO_UNF				(1 << 4)
#define SPI_RX_FIFO_OVF				(1 << 5)
#define SPI_TX_FIFO_UNF				(1 << 6)
#define SPI_TX_FIFO_OVF				(1 << 7)
#define SPI_ERR					(1 << 8)
#define SPI_TX_FIFO_FLUSH			(1 << 14)
#define SPI_RX_FIFO_FLUSH			(1 << 15)
#define SPI_TX_FIFO_EMPTY_COUNT(val)		(((val) >> 16) & 0x7F)
#define SPI_RX_FIFO_FULL_COUNT(val)		(((val) >> 23) & 0x7F)
#define SPI_FRAME_END				(1 << 30)
#define SPI_CS_INACTIVE				(1 << 31)

#define SPI_FIFO_ERROR				(SPI_RX_FIFO_UNF | \
			SPI_RX_FIFO_OVF | SPI_TX_FIFO_UNF | SPI_TX_FIFO_OVF)
#define SPI_FIFO_EMPTY			(SPI_RX_FIFO_EMPTY | SPI_TX_FIFO_EMPTY)

#define SPI_TX_DATA				0x018
#define SPI_RX_DATA				0x01C

#define SPI_DMA_CTL				0x020
#define SPI_TX_TRIG_1				(0 << 15)
#define SPI_TX_TRIG_4				(1 << 15)
#define SPI_TX_TRIG_8				(2 << 15)
#define SPI_TX_TRIG_16				(3 << 15)
#define SPI_TX_TRIG_MASK			(3 << 15)
#define SPI_RX_TRIG_1				(0 << 19)
#define SPI_RX_TRIG_4				(1 << 19)
#define SPI_RX_TRIG_8				(2 << 19)
#define SPI_RX_TRIG_16				(3 << 19)
#define SPI_RX_TRIG_MASK			(3 << 19)
#define SPI_IE_TX				(1 << 28)
#define SPI_IE_RX				(1 << 29)
#define SPI_CONT				(1 << 30)
#define SPI_DMA					(1 << 31)
#define SPI_DMA_EN				SPI_DMA

#define SPI_DMA_BLK				0x024
#define SPI_DMA_BLK_SET(x)			(((x) & 0xFFFF) << 0)

#define SPI_TX_FIFO				0x108
#define SPI_RX_FIFO				0x188
#define MAX_CHIP_SELECT				4
#define SPI_FIFO_DEPTH				64
#define DATA_DIR_TX				(1 << 0)
#define DATA_DIR_RX				(1 << 1)

#define SPI_DMA_TIMEOUT				(msecs_to_jiffies(1000))
#define DEFAULT_SPI_DMA_BUF_LEN			(16*1024)
#define TX_FIFO_EMPTY_COUNT_MAX			SPI_TX_FIFO_EMPTY_COUNT(0x40)
#define RX_FIFO_FULL_COUNT_ZERO			SPI_RX_FIFO_FULL_COUNT(0)
#define MAX_HOLD_CYCLES				16
#define SPI_DEFAULT_SPEED			25000000

struct tegra_spi_data {
	struct device				*dev;
	struct spi_master			*master;
	spinlock_t				lock;

	struct clk				*clk;
	struct reset_control			*rst;
	void __iomem				*base;
	phys_addr_t				phys;
	unsigned				irq;
	u32					cur_speed;

	struct spi_device			*cur_spi;
	struct spi_device			*cs_control;
	unsigned				cur_pos;
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

	u32					command1_reg;
	u32					dma_control_reg;
	u32					def_command1_reg;

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

static int tegra_spi_runtime_suspend(struct device *dev);
static int tegra_spi_runtime_resume(struct device *dev);

static inline u32 tegra_spi_readl(struct tegra_spi_data *tspi,
		unsigned long reg)
{
	return readl(tspi->base + reg);
}

static inline void tegra_spi_writel(struct tegra_spi_data *tspi,
		u32 val, unsigned long reg)
{
	writel(val, tspi->base + reg);

	/* Read back register to make sure that register writes completed */
	if (reg != SPI_TX_FIFO)
		readl(tspi->base + SPI_COMMAND1);
}

static void tegra_spi_clear_status(struct tegra_spi_data *tspi)
{
	u32 val;

	/* Write 1 to clear status register */
	val = tegra_spi_readl(tspi, SPI_TRANS_STATUS);
	tegra_spi_writel(tspi, val, SPI_TRANS_STATUS);

	/* Clear fifo status error if any */
	val = tegra_spi_readl(tspi, SPI_FIFO_STATUS);
	if (val & SPI_ERR)
		tegra_spi_writel(tspi, SPI_ERR | SPI_FIFO_ERROR,
				SPI_FIFO_STATUS);
}

static unsigned tegra_spi_calculate_curr_xfer_param(
	struct spi_device *spi, struct tegra_spi_data *tspi,
	struct spi_transfer *t)
{
	unsigned remain_len = t->len - tspi->cur_pos;
	unsigned max_word;
	unsigned bits_per_word = t->bits_per_word;
	unsigned max_len;
	unsigned total_fifo_words;

	tspi->bytes_per_word = DIV_ROUND_UP(bits_per_word, 8);

	if (bits_per_word == 8 || bits_per_word == 16) {
		tspi->is_packed = 1;
		tspi->words_per_32bit = 32/bits_per_word;
	} else {
		tspi->is_packed = 0;
		tspi->words_per_32bit = 1;
	}

	if (tspi->is_packed) {
		max_len = min(remain_len, tspi->max_buf_size);
		tspi->curr_dma_words = max_len/tspi->bytes_per_word;
		total_fifo_words = (max_len + 3) / 4;
	} else {
		max_word = (remain_len - 1) / tspi->bytes_per_word + 1;
		max_word = min(max_word, tspi->max_buf_size/4);
		tspi->curr_dma_words = max_word;
		total_fifo_words = max_word;
	}
	return total_fifo_words;
}

static unsigned tegra_spi_fill_tx_fifo_from_client_txbuf(
	struct tegra_spi_data *tspi, struct spi_transfer *t)
{
	unsigned nbytes;
	unsigned tx_empty_count;
	u32 fifo_status;
	unsigned max_n_32bit;
	unsigned i, count;
	unsigned int written_words;
	unsigned fifo_words_left;
	u8 *tx_buf = (u8 *)t->tx_buf + tspi->cur_tx_pos;

	fifo_status = tegra_spi_readl(tspi, SPI_FIFO_STATUS);
	tx_empty_count = SPI_TX_FIFO_EMPTY_COUNT(fifo_status);

	if (tspi->is_packed) {
		fifo_words_left = tx_empty_count * tspi->words_per_32bit;
		written_words = min(fifo_words_left, tspi->curr_dma_words);
		nbytes = written_words * tspi->bytes_per_word;
		max_n_32bit = DIV_ROUND_UP(nbytes, 4);
		for (count = 0; count < max_n_32bit; count++) {
			u32 x = 0;

			for (i = 0; (i < 4) && nbytes; i++, nbytes--)
				x |= (u32)(*tx_buf++) << (i * 8);
			tegra_spi_writel(tspi, x, SPI_TX_FIFO);
		}

		tspi->cur_tx_pos += written_words * tspi->bytes_per_word;
	} else {
		unsigned int write_bytes;
		max_n_32bit = min(tspi->curr_dma_words,  tx_empty_count);
		written_words = max_n_32bit;
		nbytes = written_words * tspi->bytes_per_word;
		if (nbytes > t->len - tspi->cur_pos)
			nbytes = t->len - tspi->cur_pos;
		write_bytes = nbytes;
		for (count = 0; count < max_n_32bit; count++) {
			u32 x = 0;

			for (i = 0; nbytes && (i < tspi->bytes_per_word);
							i++, nbytes--)
				x |= (u32)(*tx_buf++) << (i * 8);
			tegra_spi_writel(tspi, x, SPI_TX_FIFO);
		}

		tspi->cur_tx_pos += write_bytes;
	}

	return written_words;
}

static unsigned int tegra_spi_read_rx_fifo_to_client_rxbuf(
		struct tegra_spi_data *tspi, struct spi_transfer *t)
{
	unsigned rx_full_count;
	u32 fifo_status;
	unsigned i, count;
	unsigned int read_words = 0;
	unsigned len;
	u8 *rx_buf = (u8 *)t->rx_buf + tspi->cur_rx_pos;

	fifo_status = tegra_spi_readl(tspi, SPI_FIFO_STATUS);
	rx_full_count = SPI_RX_FIFO_FULL_COUNT(fifo_status);
	if (tspi->is_packed) {
		len = tspi->curr_dma_words * tspi->bytes_per_word;
		for (count = 0; count < rx_full_count; count++) {
			u32 x = tegra_spi_readl(tspi, SPI_RX_FIFO);

			for (i = 0; len && (i < 4); i++, len--)
				*rx_buf++ = (x >> i*8) & 0xFF;
		}
		read_words += tspi->curr_dma_words;
		tspi->cur_rx_pos += tspi->curr_dma_words * tspi->bytes_per_word;
	} else {
		u32 rx_mask = ((u32)1 << t->bits_per_word) - 1;
		u8 bytes_per_word = tspi->bytes_per_word;
		unsigned int read_bytes;

		len = rx_full_count * bytes_per_word;
		if (len > t->len - tspi->cur_pos)
			len = t->len - tspi->cur_pos;
		read_bytes = len;
		for (count = 0; count < rx_full_count; count++) {
			u32 x = tegra_spi_readl(tspi, SPI_RX_FIFO) & rx_mask;

			for (i = 0; len && (i < bytes_per_word); i++, len--)
				*rx_buf++ = (x >> (i*8)) & 0xFF;
		}
		read_words += rx_full_count;
		tspi->cur_rx_pos += read_bytes;
	}

	return read_words;
}

static void tegra_spi_copy_client_txbuf_to_spi_txbuf(
		struct tegra_spi_data *tspi, struct spi_transfer *t)
{
	/* Make the dma buffer to read by cpu */
	dma_sync_single_for_cpu(tspi->dev, tspi->tx_dma_phys,
				tspi->dma_buf_size, DMA_TO_DEVICE);

	if (tspi->is_packed) {
		unsigned len = tspi->curr_dma_words * tspi->bytes_per_word;

		memcpy(tspi->tx_dma_buf, t->tx_buf + tspi->cur_pos, len);
		tspi->cur_tx_pos += tspi->curr_dma_words * tspi->bytes_per_word;
	} else {
		unsigned int i;
		unsigned int count;
		u8 *tx_buf = (u8 *)t->tx_buf + tspi->cur_tx_pos;
		unsigned consume = tspi->curr_dma_words * tspi->bytes_per_word;
		unsigned int write_bytes;

		if (consume > t->len - tspi->cur_pos)
			consume = t->len - tspi->cur_pos;
		write_bytes = consume;
		for (count = 0; count < tspi->curr_dma_words; count++) {
			u32 x = 0;

			for (i = 0; consume && (i < tspi->bytes_per_word);
							i++, consume--)
				x |= (u32)(*tx_buf++) << (i * 8);
			tspi->tx_dma_buf[count] = x;
		}

		tspi->cur_tx_pos += write_bytes;
	}

	/* Make the dma buffer to read by dma */
	dma_sync_single_for_device(tspi->dev, tspi->tx_dma_phys,
				tspi->dma_buf_size, DMA_TO_DEVICE);
}

static void tegra_spi_copy_spi_rxbuf_to_client_rxbuf(
		struct tegra_spi_data *tspi, struct spi_transfer *t)
{
	/* Make the dma buffer to read by cpu */
	dma_sync_single_for_cpu(tspi->dev, tspi->rx_dma_phys,
		tspi->dma_buf_size, DMA_FROM_DEVICE);

	if (tspi->is_packed) {
		unsigned len = tspi->curr_dma_words * tspi->bytes_per_word;

		memcpy(t->rx_buf + tspi->cur_rx_pos, tspi->rx_dma_buf, len);
		tspi->cur_rx_pos += tspi->curr_dma_words * tspi->bytes_per_word;
	} else {
		unsigned int i;
		unsigned int count;
		unsigned char *rx_buf = t->rx_buf + tspi->cur_rx_pos;
		u32 rx_mask = ((u32)1 << t->bits_per_word) - 1;
		unsigned consume = tspi->curr_dma_words * tspi->bytes_per_word;
		unsigned int read_bytes;

		if (consume > t->len - tspi->cur_pos)
			consume = t->len - tspi->cur_pos;
		read_bytes = consume;
		for (count = 0; count < tspi->curr_dma_words; count++) {
			u32 x = tspi->rx_dma_buf[count] & rx_mask;

			for (i = 0; consume && (i < tspi->bytes_per_word);
							i++, consume--)
				*rx_buf++ = (x >> (i*8)) & 0xFF;
		}

		tspi->cur_rx_pos += read_bytes;
	}

	/* Make the dma buffer to read by dma */
	dma_sync_single_for_device(tspi->dev, tspi->rx_dma_phys,
		tspi->dma_buf_size, DMA_FROM_DEVICE);
}

static void tegra_spi_dma_complete(void *args)
{
	struct completion *dma_complete = args;

	complete(dma_complete);
}

static int tegra_spi_start_tx_dma(struct tegra_spi_data *tspi, int len)
{
	reinit_completion(&tspi->tx_dma_complete);
	tspi->tx_dma_desc = dmaengine_prep_slave_single(tspi->tx_dma_chan,
				tspi->tx_dma_phys, len, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT |  DMA_CTRL_ACK);
	if (!tspi->tx_dma_desc) {
		dev_err(tspi->dev, "Not able to get desc for Tx\n");
		return -EIO;
	}

	tspi->tx_dma_desc->callback = tegra_spi_dma_complete;
	tspi->tx_dma_desc->callback_param = &tspi->tx_dma_complete;

	dmaengine_submit(tspi->tx_dma_desc);
	dma_async_issue_pending(tspi->tx_dma_chan);
	return 0;
}

static int tegra_spi_start_rx_dma(struct tegra_spi_data *tspi, int len)
{
	reinit_completion(&tspi->rx_dma_complete);
	tspi->rx_dma_desc = dmaengine_prep_slave_single(tspi->rx_dma_chan,
				tspi->rx_dma_phys, len, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT |  DMA_CTRL_ACK);
	if (!tspi->rx_dma_desc) {
		dev_err(tspi->dev, "Not able to get desc for Rx\n");
		return -EIO;
	}

	tspi->rx_dma_desc->callback = tegra_spi_dma_complete;
	tspi->rx_dma_desc->callback_param = &tspi->rx_dma_complete;

	dmaengine_submit(tspi->rx_dma_desc);
	dma_async_issue_pending(tspi->rx_dma_chan);
	return 0;
}

static int tegra_spi_start_dma_based_transfer(
		struct tegra_spi_data *tspi, struct spi_transfer *t)
{
	u32 val;
	unsigned int len;
	int ret = 0;
	u32 status;

	/* Make sure that Rx and Tx fifo are empty */
	status = tegra_spi_readl(tspi, SPI_FIFO_STATUS);
	if ((status & SPI_FIFO_EMPTY) != SPI_FIFO_EMPTY) {
		dev_err(tspi->dev, "Rx/Tx fifo are not empty status 0x%08x\n",
			(unsigned)status);
		return -EIO;
	}

	val = SPI_DMA_BLK_SET(tspi->curr_dma_words - 1);
	tegra_spi_writel(tspi, val, SPI_DMA_BLK);

	if (tspi->is_packed)
		len = DIV_ROUND_UP(tspi->curr_dma_words * tspi->bytes_per_word,
					4) * 4;
	else
		len = tspi->curr_dma_words * 4;

	/* Set attention level based on length of transfer */
	if (len & 0xF)
		val |= SPI_TX_TRIG_1 | SPI_RX_TRIG_1;
	else if (((len) >> 4) & 0x1)
		val |= SPI_TX_TRIG_4 | SPI_RX_TRIG_4;
	else
		val |= SPI_TX_TRIG_8 | SPI_RX_TRIG_8;

	if (tspi->cur_direction & DATA_DIR_TX)
		val |= SPI_IE_TX;

	if (tspi->cur_direction & DATA_DIR_RX)
		val |= SPI_IE_RX;

	tegra_spi_writel(tspi, val, SPI_DMA_CTL);
	tspi->dma_control_reg = val;

	if (tspi->cur_direction & DATA_DIR_TX) {
		tegra_spi_copy_client_txbuf_to_spi_txbuf(tspi, t);
		ret = tegra_spi_start_tx_dma(tspi, len);
		if (ret < 0) {
			dev_err(tspi->dev,
				"Starting tx dma failed, err %d\n", ret);
			return ret;
		}
	}

	if (tspi->cur_direction & DATA_DIR_RX) {
		/* Make the dma buffer to read by dma */
		dma_sync_single_for_device(tspi->dev, tspi->rx_dma_phys,
				tspi->dma_buf_size, DMA_FROM_DEVICE);

		ret = tegra_spi_start_rx_dma(tspi, len);
		if (ret < 0) {
			dev_err(tspi->dev,
				"Starting rx dma failed, err %d\n", ret);
			if (tspi->cur_direction & DATA_DIR_TX)
				dmaengine_terminate_all(tspi->tx_dma_chan);
			return ret;
		}
	}
	tspi->is_curr_dma_xfer = true;
	tspi->dma_control_reg = val;

	val |= SPI_DMA_EN;
	tegra_spi_writel(tspi, val, SPI_DMA_CTL);
	return ret;
}

static int tegra_spi_start_cpu_based_transfer(
		struct tegra_spi_data *tspi, struct spi_transfer *t)
{
	u32 val;
	unsigned cur_words;

	if (tspi->cur_direction & DATA_DIR_TX)
		cur_words = tegra_spi_fill_tx_fifo_from_client_txbuf(tspi, t);
	else
		cur_words = tspi->curr_dma_words;

	val = SPI_DMA_BLK_SET(cur_words - 1);
	tegra_spi_writel(tspi, val, SPI_DMA_BLK);

	val = 0;
	if (tspi->cur_direction & DATA_DIR_TX)
		val |= SPI_IE_TX;

	if (tspi->cur_direction & DATA_DIR_RX)
		val |= SPI_IE_RX;

	tegra_spi_writel(tspi, val, SPI_DMA_CTL);
	tspi->dma_control_reg = val;

	tspi->is_curr_dma_xfer = false;

	val |= SPI_DMA_EN;
	tegra_spi_writel(tspi, val, SPI_DMA_CTL);
	return 0;
}

static int tegra_spi_init_dma_param(struct tegra_spi_data *tspi,
			bool dma_to_memory)
{
	struct dma_chan *dma_chan;
	u32 *dma_buf;
	dma_addr_t dma_phys;
	int ret;
	struct dma_slave_config dma_sconfig;

	dma_chan = dma_request_slave_channel_reason(tspi->dev,
					dma_to_memory ? "rx" : "tx");
	if (IS_ERR(dma_chan)) {
		ret = PTR_ERR(dma_chan);
		if (ret != -EPROBE_DEFER)
			dev_err(tspi->dev,
				"Dma channel is not available: %d\n", ret);
		return ret;
	}

	dma_buf = dma_alloc_coherent(tspi->dev, tspi->dma_buf_size,
				&dma_phys, GFP_KERNEL);
	if (!dma_buf) {
		dev_err(tspi->dev, " Not able to allocate the dma buffer\n");
		dma_release_channel(dma_chan);
		return -ENOMEM;
	}

	if (dma_to_memory) {
		dma_sconfig.src_addr = tspi->phys + SPI_RX_FIFO;
		dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		dma_sconfig.src_maxburst = 0;
	} else {
		dma_sconfig.dst_addr = tspi->phys + SPI_TX_FIFO;
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

static void tegra_spi_deinit_dma_param(struct tegra_spi_data *tspi,
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

static u32 tegra_spi_setup_transfer_one(struct spi_device *spi,
		struct spi_transfer *t, bool is_first_of_msg)
{
	struct tegra_spi_data *tspi = spi_master_get_devdata(spi->master);
	u32 speed = t->speed_hz;
	u8 bits_per_word = t->bits_per_word;
	u32 command1;
	int req_mode;

	if (speed != tspi->cur_speed) {
		clk_set_rate(tspi->clk, speed);
		tspi->cur_speed = speed;
	}

	tspi->cur_spi = spi;
	tspi->cur_pos = 0;
	tspi->cur_rx_pos = 0;
	tspi->cur_tx_pos = 0;
	tspi->curr_xfer = t;

	if (is_first_of_msg) {
		tegra_spi_clear_status(tspi);

		command1 = tspi->def_command1_reg;
		command1 |= SPI_BIT_LENGTH(bits_per_word - 1);

		command1 &= ~SPI_CONTROL_MODE_MASK;
		req_mode = spi->mode & 0x3;
		if (req_mode == SPI_MODE_0)
			command1 |= SPI_CONTROL_MODE_0;
		else if (req_mode == SPI_MODE_1)
			command1 |= SPI_CONTROL_MODE_1;
		else if (req_mode == SPI_MODE_2)
			command1 |= SPI_CONTROL_MODE_2;
		else if (req_mode == SPI_MODE_3)
			command1 |= SPI_CONTROL_MODE_3;

		if (tspi->cs_control) {
			if (tspi->cs_control != spi)
				tegra_spi_writel(tspi, command1, SPI_COMMAND1);
			tspi->cs_control = NULL;
		} else
			tegra_spi_writel(tspi, command1, SPI_COMMAND1);

		command1 |= SPI_CS_SW_HW;
		if (spi->mode & SPI_CS_HIGH)
			command1 |= SPI_CS_SW_VAL;
		else
			command1 &= ~SPI_CS_SW_VAL;

		tegra_spi_writel(tspi, 0, SPI_COMMAND2);
	} else {
		command1 = tspi->command1_reg;
		command1 &= ~SPI_BIT_LENGTH(~0);
		command1 |= SPI_BIT_LENGTH(bits_per_word - 1);
	}

	return command1;
}

static int tegra_spi_start_transfer_one(struct spi_device *spi,
		struct spi_transfer *t, u32 command1)
{
	struct tegra_spi_data *tspi = spi_master_get_devdata(spi->master);
	unsigned total_fifo_words;
	int ret;

	total_fifo_words = tegra_spi_calculate_curr_xfer_param(spi, tspi, t);

	if (tspi->is_packed)
		command1 |= SPI_PACKED;
	else
		command1 &= ~SPI_PACKED;

	command1 &= ~(SPI_CS_SEL_MASK | SPI_TX_EN | SPI_RX_EN);
	tspi->cur_direction = 0;
	if (t->rx_buf) {
		command1 |= SPI_RX_EN;
		tspi->cur_direction |= DATA_DIR_RX;
	}
	if (t->tx_buf) {
		command1 |= SPI_TX_EN;
		tspi->cur_direction |= DATA_DIR_TX;
	}
	command1 |= SPI_CS_SEL(spi->chip_select);
	tegra_spi_writel(tspi, command1, SPI_COMMAND1);
	tspi->command1_reg = command1;

	dev_dbg(tspi->dev, "The def 0x%x and written 0x%x\n",
		tspi->def_command1_reg, (unsigned)command1);

	if (total_fifo_words > SPI_FIFO_DEPTH)
		ret = tegra_spi_start_dma_based_transfer(tspi, t);
	else
		ret = tegra_spi_start_cpu_based_transfer(tspi, t);
	return ret;
}

static int tegra_spi_setup(struct spi_device *spi)
{
	struct tegra_spi_data *tspi = spi_master_get_devdata(spi->master);
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
		dev_err(tspi->dev, "pm runtime failed, e = %d\n", ret);
		return ret;
	}

	spin_lock_irqsave(&tspi->lock, flags);
	val = tspi->def_command1_reg;
	if (spi->mode & SPI_CS_HIGH)
		val &= ~SPI_CS_POL_INACTIVE(spi->chip_select);
	else
		val |= SPI_CS_POL_INACTIVE(spi->chip_select);
	tspi->def_command1_reg = val;
	tegra_spi_writel(tspi, tspi->def_command1_reg, SPI_COMMAND1);
	spin_unlock_irqrestore(&tspi->lock, flags);

	pm_runtime_put(tspi->dev);
	return 0;
}

static void tegra_spi_transfer_delay(int delay)
{
	if (!delay)
		return;

	if (delay >= 1000)
		mdelay(delay / 1000);

	udelay(delay % 1000);
}

static int tegra_spi_transfer_one_message(struct spi_master *master,
			struct spi_message *msg)
{
	bool is_first_msg = true;
	struct tegra_spi_data *tspi = spi_master_get_devdata(master);
	struct spi_transfer *xfer;
	struct spi_device *spi = msg->spi;
	int ret;
	bool skip = false;

	msg->status = 0;
	msg->actual_length = 0;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		u32 cmd1;

		reinit_completion(&tspi->xfer_completion);

		cmd1 = tegra_spi_setup_transfer_one(spi, xfer, is_first_msg);

		if (!xfer->len) {
			ret = 0;
			skip = true;
			goto complete_xfer;
		}

		ret = tegra_spi_start_transfer_one(spi, xfer, cmd1);
		if (ret < 0) {
			dev_err(tspi->dev,
				"spi can not start transfer, err %d\n", ret);
			goto complete_xfer;
		}

		is_first_msg = false;
		ret = wait_for_completion_timeout(&tspi->xfer_completion,
						SPI_DMA_TIMEOUT);
		if (WARN_ON(ret == 0)) {
			dev_err(tspi->dev,
				"spi transfer timeout, err %d\n", ret);
			ret = -EIO;
			goto complete_xfer;
		}

		if (tspi->tx_status ||  tspi->rx_status) {
			dev_err(tspi->dev, "Error in Transfer\n");
			ret = -EIO;
			goto complete_xfer;
		}
		msg->actual_length += xfer->len;

complete_xfer:
		if (ret < 0 || skip) {
			tegra_spi_writel(tspi, tspi->def_command1_reg,
					SPI_COMMAND1);
			tegra_spi_transfer_delay(xfer->delay_usecs);
			goto exit;
		} else if (list_is_last(&xfer->transfer_list,
					&msg->transfers)) {
			if (xfer->cs_change)
				tspi->cs_control = spi;
			else {
				tegra_spi_writel(tspi, tspi->def_command1_reg,
						SPI_COMMAND1);
				tegra_spi_transfer_delay(xfer->delay_usecs);
			}
		} else if (xfer->cs_change) {
			tegra_spi_writel(tspi, tspi->def_command1_reg,
					SPI_COMMAND1);
			tegra_spi_transfer_delay(xfer->delay_usecs);
		}

	}
	ret = 0;
exit:
	msg->status = ret;
	spi_finalize_current_message(master);
	return ret;
}

static irqreturn_t handle_cpu_based_xfer(struct tegra_spi_data *tspi)
{
	struct spi_transfer *t = tspi->curr_xfer;
	unsigned long flags;

	spin_lock_irqsave(&tspi->lock, flags);
	if (tspi->tx_status ||  tspi->rx_status) {
		dev_err(tspi->dev, "CpuXfer ERROR bit set 0x%x\n",
			tspi->status_reg);
		dev_err(tspi->dev, "CpuXfer 0x%08x:0x%08x\n",
			tspi->command1_reg, tspi->dma_control_reg);
		reset_control_assert(tspi->rst);
		udelay(2);
		reset_control_deassert(tspi->rst);
		complete(&tspi->xfer_completion);
		goto exit;
	}

	if (tspi->cur_direction & DATA_DIR_RX)
		tegra_spi_read_rx_fifo_to_client_rxbuf(tspi, t);

	if (tspi->cur_direction & DATA_DIR_TX)
		tspi->cur_pos = tspi->cur_tx_pos;
	else
		tspi->cur_pos = tspi->cur_rx_pos;

	if (tspi->cur_pos == t->len) {
		complete(&tspi->xfer_completion);
		goto exit;
	}

	tegra_spi_calculate_curr_xfer_param(tspi->cur_spi, tspi, t);
	tegra_spi_start_cpu_based_transfer(tspi, t);
exit:
	spin_unlock_irqrestore(&tspi->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t handle_dma_based_xfer(struct tegra_spi_data *tspi)
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
				&tspi->tx_dma_complete, SPI_DMA_TIMEOUT);
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
				&tspi->rx_dma_complete, SPI_DMA_TIMEOUT);
			if (wait_status <= 0) {
				dmaengine_terminate_all(tspi->rx_dma_chan);
				dev_err(tspi->dev, "RxDma Xfer failed\n");
				err += 2;
			}
		}
	}

	spin_lock_irqsave(&tspi->lock, flags);
	if (err) {
		dev_err(tspi->dev, "DmaXfer: ERROR bit set 0x%x\n",
			tspi->status_reg);
		dev_err(tspi->dev, "DmaXfer 0x%08x:0x%08x\n",
			tspi->command1_reg, tspi->dma_control_reg);
		reset_control_assert(tspi->rst);
		udelay(2);
		reset_control_deassert(tspi->rst);
		complete(&tspi->xfer_completion);
		spin_unlock_irqrestore(&tspi->lock, flags);
		return IRQ_HANDLED;
	}

	if (tspi->cur_direction & DATA_DIR_RX)
		tegra_spi_copy_spi_rxbuf_to_client_rxbuf(tspi, t);

	if (tspi->cur_direction & DATA_DIR_TX)
		tspi->cur_pos = tspi->cur_tx_pos;
	else
		tspi->cur_pos = tspi->cur_rx_pos;

	if (tspi->cur_pos == t->len) {
		complete(&tspi->xfer_completion);
		goto exit;
	}

	/* Continue transfer in current message */
	total_fifo_words = tegra_spi_calculate_curr_xfer_param(tspi->cur_spi,
							tspi, t);
	if (total_fifo_words > SPI_FIFO_DEPTH)
		err = tegra_spi_start_dma_based_transfer(tspi, t);
	else
		err = tegra_spi_start_cpu_based_transfer(tspi, t);

exit:
	spin_unlock_irqrestore(&tspi->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t tegra_spi_isr_thread(int irq, void *context_data)
{
	struct tegra_spi_data *tspi = context_data;

	if (!tspi->is_curr_dma_xfer)
		return handle_cpu_based_xfer(tspi);
	return handle_dma_based_xfer(tspi);
}

static irqreturn_t tegra_spi_isr(int irq, void *context_data)
{
	struct tegra_spi_data *tspi = context_data;

	tspi->status_reg = tegra_spi_readl(tspi, SPI_FIFO_STATUS);
	if (tspi->cur_direction & DATA_DIR_TX)
		tspi->tx_status = tspi->status_reg &
					(SPI_TX_FIFO_UNF | SPI_TX_FIFO_OVF);

	if (tspi->cur_direction & DATA_DIR_RX)
		tspi->rx_status = tspi->status_reg &
					(SPI_RX_FIFO_OVF | SPI_RX_FIFO_UNF);
	tegra_spi_clear_status(tspi);

	return IRQ_WAKE_THREAD;
}

static const struct of_device_id tegra_spi_of_match[] = {
	{ .compatible = "nvidia,tegra114-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, tegra_spi_of_match);

static int tegra_spi_probe(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct tegra_spi_data	*tspi;
	struct resource		*r;
	int ret, spi_irq;

	master = spi_alloc_master(&pdev->dev, sizeof(*tspi));
	if (!master) {
		dev_err(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, master);
	tspi = spi_master_get_devdata(master);

	if (of_property_read_u32(pdev->dev.of_node, "spi-max-frequency",
				 &master->max_speed_hz))
		master->max_speed_hz = 25000000; /* 25MHz */

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->setup = tegra_spi_setup;
	master->transfer_one_message = tegra_spi_transfer_one_message;
	master->num_chipselect = MAX_CHIP_SELECT;
	master->auto_runtime_pm = true;

	tspi->master = master;
	tspi->dev = &pdev->dev;
	spin_lock_init(&tspi->lock);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tspi->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(tspi->base)) {
		ret = PTR_ERR(tspi->base);
		goto exit_free_master;
	}
	tspi->phys = r->start;

	spi_irq = platform_get_irq(pdev, 0);
	tspi->irq = spi_irq;
	ret = request_threaded_irq(tspi->irq, tegra_spi_isr,
			tegra_spi_isr_thread, IRQF_ONESHOT,
			dev_name(&pdev->dev), tspi);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register ISR for IRQ %d\n",
					tspi->irq);
		goto exit_free_master;
	}

	tspi->clk = devm_clk_get(&pdev->dev, "spi");
	if (IS_ERR(tspi->clk)) {
		dev_err(&pdev->dev, "can not get clock\n");
		ret = PTR_ERR(tspi->clk);
		goto exit_free_irq;
	}

	tspi->rst = devm_reset_control_get_exclusive(&pdev->dev, "spi");
	if (IS_ERR(tspi->rst)) {
		dev_err(&pdev->dev, "can not get reset\n");
		ret = PTR_ERR(tspi->rst);
		goto exit_free_irq;
	}

	tspi->max_buf_size = SPI_FIFO_DEPTH << 2;
	tspi->dma_buf_size = DEFAULT_SPI_DMA_BUF_LEN;

	ret = tegra_spi_init_dma_param(tspi, true);
	if (ret < 0)
		goto exit_free_irq;
	ret = tegra_spi_init_dma_param(tspi, false);
	if (ret < 0)
		goto exit_rx_dma_free;
	tspi->max_buf_size = tspi->dma_buf_size;
	init_completion(&tspi->tx_dma_complete);
	init_completion(&tspi->rx_dma_complete);

	init_completion(&tspi->xfer_completion);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = tegra_spi_runtime_resume(&pdev->dev);
		if (ret)
			goto exit_pm_disable;
	}

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm runtime get failed, e = %d\n", ret);
		goto exit_pm_disable;
	}
	tspi->def_command1_reg  = SPI_M_S;
	tegra_spi_writel(tspi, tspi->def_command1_reg, SPI_COMMAND1);
	pm_runtime_put(&pdev->dev);

	master->dev.of_node = pdev->dev.of_node;
	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret < 0) {
		dev_err(&pdev->dev, "can not register to master err %d\n", ret);
		goto exit_pm_disable;
	}
	return ret;

exit_pm_disable:
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_spi_runtime_suspend(&pdev->dev);
	tegra_spi_deinit_dma_param(tspi, false);
exit_rx_dma_free:
	tegra_spi_deinit_dma_param(tspi, true);
exit_free_irq:
	free_irq(spi_irq, tspi);
exit_free_master:
	spi_master_put(master);
	return ret;
}

static int tegra_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct tegra_spi_data	*tspi = spi_master_get_devdata(master);

	free_irq(tspi->irq, tspi);

	if (tspi->tx_dma_chan)
		tegra_spi_deinit_dma_param(tspi, false);

	if (tspi->rx_dma_chan)
		tegra_spi_deinit_dma_param(tspi, true);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_spi_runtime_suspend(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_spi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);

	return spi_master_suspend(master);
}

static int tegra_spi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_spi_data *tspi = spi_master_get_devdata(master);
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm runtime failed, e = %d\n", ret);
		return ret;
	}
	tegra_spi_writel(tspi, tspi->command1_reg, SPI_COMMAND1);
	pm_runtime_put(dev);

	return spi_master_resume(master);
}
#endif

static int tegra_spi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_spi_data *tspi = spi_master_get_devdata(master);

	/* Flush all write which are in PPSB queue by reading back */
	tegra_spi_readl(tspi, SPI_COMMAND1);

	clk_disable_unprepare(tspi->clk);
	return 0;
}

static int tegra_spi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_spi_data *tspi = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(tspi->clk);
	if (ret < 0) {
		dev_err(tspi->dev, "clk_prepare failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static const struct dev_pm_ops tegra_spi_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_spi_runtime_suspend,
		tegra_spi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_spi_suspend, tegra_spi_resume)
};
static struct platform_driver tegra_spi_driver = {
	.driver = {
		.name		= "spi-tegra114",
		.pm		= &tegra_spi_pm_ops,
		.of_match_table	= tegra_spi_of_match,
	},
	.probe =	tegra_spi_probe,
	.remove =	tegra_spi_remove,
};
module_platform_driver(tegra_spi_driver);

MODULE_ALIAS("platform:spi-tegra114");
MODULE_DESCRIPTION("NVIDIA Tegra114 SPI Controller Driver");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
