// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (C) 2020 NVIDIA CORPORATION.

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>

#define QSPI_COMMAND1				0x000
#define QSPI_BIT_LENGTH(x)			(((x) & 0x1f) << 0)
#define QSPI_PACKED				BIT(5)
#define QSPI_INTERFACE_WIDTH_MASK		(0x03 << 7)
#define QSPI_INTERFACE_WIDTH(x)			(((x) & 0x03) << 7)
#define QSPI_INTERFACE_WIDTH_SINGLE		QSPI_INTERFACE_WIDTH(0)
#define QSPI_INTERFACE_WIDTH_DUAL		QSPI_INTERFACE_WIDTH(1)
#define QSPI_INTERFACE_WIDTH_QUAD		QSPI_INTERFACE_WIDTH(2)
#define QSPI_SDR_DDR_SEL			BIT(9)
#define QSPI_TX_EN				BIT(11)
#define QSPI_RX_EN				BIT(12)
#define QSPI_CS_SW_VAL				BIT(20)
#define QSPI_CS_SW_HW				BIT(21)
#define QSPI_CONTROL_MODE_0			(0 << 28)
#define QSPI_CONTROL_MODE_3			(3 << 28)
#define QSPI_CONTROL_MODE_MASK			(3 << 28)
#define QSPI_M_S				BIT(30)
#define QSPI_PIO				BIT(31)

#define QSPI_COMMAND2				0x004
#define QSPI_TX_TAP_DELAY(x)			(((x) & 0x3f) << 10)
#define QSPI_RX_TAP_DELAY(x)			(((x) & 0xff) << 0)

#define QSPI_CS_TIMING1				0x008
#define QSPI_SETUP_HOLD(setup, hold)		(((setup) << 4) | (hold))

#define QSPI_CS_TIMING2				0x00c
#define CYCLES_BETWEEN_PACKETS_0(x)		(((x) & 0x1f) << 0)
#define CS_ACTIVE_BETWEEN_PACKETS_0		BIT(5)

#define QSPI_TRANS_STATUS			0x010
#define QSPI_BLK_CNT(val)			(((val) >> 0) & 0xffff)
#define QSPI_RDY				BIT(30)

#define QSPI_FIFO_STATUS			0x014
#define QSPI_RX_FIFO_EMPTY			BIT(0)
#define QSPI_RX_FIFO_FULL			BIT(1)
#define QSPI_TX_FIFO_EMPTY			BIT(2)
#define QSPI_TX_FIFO_FULL			BIT(3)
#define QSPI_RX_FIFO_UNF			BIT(4)
#define QSPI_RX_FIFO_OVF			BIT(5)
#define QSPI_TX_FIFO_UNF			BIT(6)
#define QSPI_TX_FIFO_OVF			BIT(7)
#define QSPI_ERR				BIT(8)
#define QSPI_TX_FIFO_FLUSH			BIT(14)
#define QSPI_RX_FIFO_FLUSH			BIT(15)
#define QSPI_TX_FIFO_EMPTY_COUNT(val)		(((val) >> 16) & 0x7f)
#define QSPI_RX_FIFO_FULL_COUNT(val)		(((val) >> 23) & 0x7f)

#define QSPI_FIFO_ERROR				(QSPI_RX_FIFO_UNF | \
						 QSPI_RX_FIFO_OVF | \
						 QSPI_TX_FIFO_UNF | \
						 QSPI_TX_FIFO_OVF)
#define QSPI_FIFO_EMPTY				(QSPI_RX_FIFO_EMPTY | \
						 QSPI_TX_FIFO_EMPTY)

#define QSPI_TX_DATA				0x018
#define QSPI_RX_DATA				0x01c

#define QSPI_DMA_CTL				0x020
#define QSPI_TX_TRIG(n)				(((n) & 0x3) << 15)
#define QSPI_TX_TRIG_1				QSPI_TX_TRIG(0)
#define QSPI_TX_TRIG_4				QSPI_TX_TRIG(1)
#define QSPI_TX_TRIG_8				QSPI_TX_TRIG(2)
#define QSPI_TX_TRIG_16				QSPI_TX_TRIG(3)

#define QSPI_RX_TRIG(n)				(((n) & 0x3) << 19)
#define QSPI_RX_TRIG_1				QSPI_RX_TRIG(0)
#define QSPI_RX_TRIG_4				QSPI_RX_TRIG(1)
#define QSPI_RX_TRIG_8				QSPI_RX_TRIG(2)
#define QSPI_RX_TRIG_16				QSPI_RX_TRIG(3)

#define QSPI_DMA_EN				BIT(31)

#define QSPI_DMA_BLK				0x024
#define QSPI_DMA_BLK_SET(x)			(((x) & 0xffff) << 0)

#define QSPI_TX_FIFO				0x108
#define QSPI_RX_FIFO				0x188

#define QSPI_FIFO_DEPTH				64

#define QSPI_INTR_MASK				0x18c
#define QSPI_INTR_RX_FIFO_UNF_MASK		BIT(25)
#define QSPI_INTR_RX_FIFO_OVF_MASK		BIT(26)
#define QSPI_INTR_TX_FIFO_UNF_MASK		BIT(27)
#define QSPI_INTR_TX_FIFO_OVF_MASK		BIT(28)
#define QSPI_INTR_RDY_MASK			BIT(29)
#define QSPI_INTR_RX_TX_FIFO_ERR		(QSPI_INTR_RX_FIFO_UNF_MASK | \
						 QSPI_INTR_RX_FIFO_OVF_MASK | \
						 QSPI_INTR_TX_FIFO_UNF_MASK | \
						 QSPI_INTR_TX_FIFO_OVF_MASK)

#define QSPI_MISC_REG                           0x194
#define QSPI_NUM_DUMMY_CYCLE(x)			(((x) & 0xff) << 0)
#define QSPI_DUMMY_CYCLES_MAX			0xff

#define DATA_DIR_TX				BIT(0)
#define DATA_DIR_RX				BIT(1)

#define QSPI_DMA_TIMEOUT			(msecs_to_jiffies(1000))
#define DEFAULT_QSPI_DMA_BUF_LEN		(64 * 1024)

struct tegra_qspi_client_data {
	int tx_clk_tap_delay;
	int rx_clk_tap_delay;
};

struct tegra_qspi {
	struct device				*dev;
	struct spi_master			*master;
	/* lock to protect data accessed by irq */
	spinlock_t				lock;

	struct clk				*clk;
	struct reset_control			*rst;
	void __iomem				*base;
	phys_addr_t				phys;
	unsigned int				irq;

	u32					cur_speed;
	unsigned int				cur_pos;
	unsigned int				words_per_32bit;
	unsigned int				bytes_per_word;
	unsigned int				curr_dma_words;
	unsigned int				cur_direction;

	unsigned int				cur_rx_pos;
	unsigned int				cur_tx_pos;

	unsigned int				dma_buf_size;
	unsigned int				max_buf_size;
	bool					is_curr_dma_xfer;

	struct completion			rx_dma_complete;
	struct completion			tx_dma_complete;

	u32					tx_status;
	u32					rx_status;
	u32					status_reg;
	bool					is_packed;
	bool					use_dma;

	u32					command1_reg;
	u32					dma_control_reg;
	u32					def_command1_reg;
	u32					def_command2_reg;
	u32					spi_cs_timing1;
	u32					spi_cs_timing2;
	u8					dummy_cycles;

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

static inline u32 tegra_qspi_readl(struct tegra_qspi *tqspi, unsigned long offset)
{
	return readl(tqspi->base + offset);
}

static inline void tegra_qspi_writel(struct tegra_qspi *tqspi, u32 value, unsigned long offset)
{
	writel(value, tqspi->base + offset);

	/* read back register to make sure that register writes completed */
	if (offset != QSPI_TX_FIFO)
		readl(tqspi->base + QSPI_COMMAND1);
}

static void tegra_qspi_mask_clear_irq(struct tegra_qspi *tqspi)
{
	u32 value;

	/* write 1 to clear status register */
	value = tegra_qspi_readl(tqspi, QSPI_TRANS_STATUS);
	tegra_qspi_writel(tqspi, value, QSPI_TRANS_STATUS);

	value = tegra_qspi_readl(tqspi, QSPI_INTR_MASK);
	if (!(value & QSPI_INTR_RDY_MASK)) {
		value |= (QSPI_INTR_RDY_MASK | QSPI_INTR_RX_TX_FIFO_ERR);
		tegra_qspi_writel(tqspi, value, QSPI_INTR_MASK);
	}

	/* clear fifo status error if any */
	value = tegra_qspi_readl(tqspi, QSPI_FIFO_STATUS);
	if (value & QSPI_ERR)
		tegra_qspi_writel(tqspi, QSPI_ERR | QSPI_FIFO_ERROR, QSPI_FIFO_STATUS);
}

static unsigned int
tegra_qspi_calculate_curr_xfer_param(struct tegra_qspi *tqspi, struct spi_transfer *t)
{
	unsigned int max_word, max_len, total_fifo_words;
	unsigned int remain_len = t->len - tqspi->cur_pos;
	unsigned int bits_per_word = t->bits_per_word;

	tqspi->bytes_per_word = DIV_ROUND_UP(bits_per_word, 8);

	/*
	 * Tegra QSPI controller supports packed or unpacked mode transfers.
	 * Packed mode is used for data transfers using 8, 16, or 32 bits per
	 * word with a minimum transfer of 1 word and for all other transfers
	 * unpacked mode will be used.
	 */

	if ((bits_per_word == 8 || bits_per_word == 16 ||
	     bits_per_word == 32) && t->len > 3) {
		tqspi->is_packed = true;
		tqspi->words_per_32bit = 32 / bits_per_word;
	} else {
		tqspi->is_packed = false;
		tqspi->words_per_32bit = 1;
	}

	if (tqspi->is_packed) {
		max_len = min(remain_len, tqspi->max_buf_size);
		tqspi->curr_dma_words = max_len / tqspi->bytes_per_word;
		total_fifo_words = (max_len + 3) / 4;
	} else {
		max_word = (remain_len - 1) / tqspi->bytes_per_word + 1;
		max_word = min(max_word, tqspi->max_buf_size / 4);
		tqspi->curr_dma_words = max_word;
		total_fifo_words = max_word;
	}

	return total_fifo_words;
}

static unsigned int
tegra_qspi_fill_tx_fifo_from_client_txbuf(struct tegra_qspi *tqspi, struct spi_transfer *t)
{
	unsigned int written_words, fifo_words_left, count;
	unsigned int len, tx_empty_count, max_n_32bit, i;
	u8 *tx_buf = (u8 *)t->tx_buf + tqspi->cur_tx_pos;
	u32 fifo_status;

	fifo_status = tegra_qspi_readl(tqspi, QSPI_FIFO_STATUS);
	tx_empty_count = QSPI_TX_FIFO_EMPTY_COUNT(fifo_status);

	if (tqspi->is_packed) {
		fifo_words_left = tx_empty_count * tqspi->words_per_32bit;
		written_words = min(fifo_words_left, tqspi->curr_dma_words);
		len = written_words * tqspi->bytes_per_word;
		max_n_32bit = DIV_ROUND_UP(len, 4);
		for (count = 0; count < max_n_32bit; count++) {
			u32 x = 0;

			for (i = 0; (i < 4) && len; i++, len--)
				x |= (u32)(*tx_buf++) << (i * 8);
			tegra_qspi_writel(tqspi, x, QSPI_TX_FIFO);
		}

		tqspi->cur_tx_pos += written_words * tqspi->bytes_per_word;
	} else {
		unsigned int write_bytes;
		u8 bytes_per_word = tqspi->bytes_per_word;

		max_n_32bit = min(tqspi->curr_dma_words, tx_empty_count);
		written_words = max_n_32bit;
		len = written_words * tqspi->bytes_per_word;
		if (len > t->len - tqspi->cur_pos)
			len = t->len - tqspi->cur_pos;
		write_bytes = len;
		for (count = 0; count < max_n_32bit; count++) {
			u32 x = 0;

			for (i = 0; len && (i < bytes_per_word); i++, len--)
				x |= (u32)(*tx_buf++) << (i * 8);
			tegra_qspi_writel(tqspi, x, QSPI_TX_FIFO);
		}

		tqspi->cur_tx_pos += write_bytes;
	}

	return written_words;
}

static unsigned int
tegra_qspi_read_rx_fifo_to_client_rxbuf(struct tegra_qspi *tqspi, struct spi_transfer *t)
{
	u8 *rx_buf = (u8 *)t->rx_buf + tqspi->cur_rx_pos;
	unsigned int len, rx_full_count, count, i;
	unsigned int read_words = 0;
	u32 fifo_status, x;

	fifo_status = tegra_qspi_readl(tqspi, QSPI_FIFO_STATUS);
	rx_full_count = QSPI_RX_FIFO_FULL_COUNT(fifo_status);
	if (tqspi->is_packed) {
		len = tqspi->curr_dma_words * tqspi->bytes_per_word;
		for (count = 0; count < rx_full_count; count++) {
			x = tegra_qspi_readl(tqspi, QSPI_RX_FIFO);

			for (i = 0; len && (i < 4); i++, len--)
				*rx_buf++ = (x >> i * 8) & 0xff;
		}

		read_words += tqspi->curr_dma_words;
		tqspi->cur_rx_pos += tqspi->curr_dma_words * tqspi->bytes_per_word;
	} else {
		u32 rx_mask = ((u32)1 << t->bits_per_word) - 1;
		u8 bytes_per_word = tqspi->bytes_per_word;
		unsigned int read_bytes;

		len = rx_full_count * bytes_per_word;
		if (len > t->len - tqspi->cur_pos)
			len = t->len - tqspi->cur_pos;
		read_bytes = len;
		for (count = 0; count < rx_full_count; count++) {
			x = tegra_qspi_readl(tqspi, QSPI_RX_FIFO) & rx_mask;

			for (i = 0; len && (i < bytes_per_word); i++, len--)
				*rx_buf++ = (x >> (i * 8)) & 0xff;
		}

		read_words += rx_full_count;
		tqspi->cur_rx_pos += read_bytes;
	}

	return read_words;
}

static void
tegra_qspi_copy_client_txbuf_to_qspi_txbuf(struct tegra_qspi *tqspi, struct spi_transfer *t)
{
	dma_sync_single_for_cpu(tqspi->dev, tqspi->tx_dma_phys,
				tqspi->dma_buf_size, DMA_TO_DEVICE);

	/*
	 * In packed mode, each word in FIFO may contain multiple packets
	 * based on bits per word. So all bytes in each FIFO word are valid.
	 *
	 * In unpacked mode, each word in FIFO contains single packet and
	 * based on bits per word any remaining bits in FIFO word will be
	 * ignored by the hardware and are invalid bits.
	 */
	if (tqspi->is_packed) {
		tqspi->cur_tx_pos += tqspi->curr_dma_words * tqspi->bytes_per_word;
	} else {
		u8 *tx_buf = (u8 *)t->tx_buf + tqspi->cur_tx_pos;
		unsigned int i, count, consume, write_bytes;

		/*
		 * Fill tx_dma_buf to contain single packet in each word based
		 * on bits per word from SPI core tx_buf.
		 */
		consume = tqspi->curr_dma_words * tqspi->bytes_per_word;
		if (consume > t->len - tqspi->cur_pos)
			consume = t->len - tqspi->cur_pos;
		write_bytes = consume;
		for (count = 0; count < tqspi->curr_dma_words; count++) {
			u32 x = 0;

			for (i = 0; consume && (i < tqspi->bytes_per_word); i++, consume--)
				x |= (u32)(*tx_buf++) << (i * 8);
			tqspi->tx_dma_buf[count] = x;
		}

		tqspi->cur_tx_pos += write_bytes;
	}

	dma_sync_single_for_device(tqspi->dev, tqspi->tx_dma_phys,
				   tqspi->dma_buf_size, DMA_TO_DEVICE);
}

static void
tegra_qspi_copy_qspi_rxbuf_to_client_rxbuf(struct tegra_qspi *tqspi, struct spi_transfer *t)
{
	dma_sync_single_for_cpu(tqspi->dev, tqspi->rx_dma_phys,
				tqspi->dma_buf_size, DMA_FROM_DEVICE);

	if (tqspi->is_packed) {
		tqspi->cur_rx_pos += tqspi->curr_dma_words * tqspi->bytes_per_word;
	} else {
		unsigned char *rx_buf = t->rx_buf + tqspi->cur_rx_pos;
		u32 rx_mask = ((u32)1 << t->bits_per_word) - 1;
		unsigned int i, count, consume, read_bytes;

		/*
		 * Each FIFO word contains single data packet.
		 * Skip invalid bits in each FIFO word based on bits per word
		 * and align bytes while filling in SPI core rx_buf.
		 */
		consume = tqspi->curr_dma_words * tqspi->bytes_per_word;
		if (consume > t->len - tqspi->cur_pos)
			consume = t->len - tqspi->cur_pos;
		read_bytes = consume;
		for (count = 0; count < tqspi->curr_dma_words; count++) {
			u32 x = tqspi->rx_dma_buf[count] & rx_mask;

			for (i = 0; consume && (i < tqspi->bytes_per_word); i++, consume--)
				*rx_buf++ = (x >> (i * 8)) & 0xff;
		}

		tqspi->cur_rx_pos += read_bytes;
	}

	dma_sync_single_for_device(tqspi->dev, tqspi->rx_dma_phys,
				   tqspi->dma_buf_size, DMA_FROM_DEVICE);
}

static void tegra_qspi_dma_complete(void *args)
{
	struct completion *dma_complete = args;

	complete(dma_complete);
}

static int tegra_qspi_start_tx_dma(struct tegra_qspi *tqspi, struct spi_transfer *t, int len)
{
	dma_addr_t tx_dma_phys;

	reinit_completion(&tqspi->tx_dma_complete);

	if (tqspi->is_packed)
		tx_dma_phys = t->tx_dma;
	else
		tx_dma_phys = tqspi->tx_dma_phys;

	tqspi->tx_dma_desc = dmaengine_prep_slave_single(tqspi->tx_dma_chan, tx_dma_phys,
							 len, DMA_MEM_TO_DEV,
							 DMA_PREP_INTERRUPT |  DMA_CTRL_ACK);

	if (!tqspi->tx_dma_desc) {
		dev_err(tqspi->dev, "Unable to get TX descriptor\n");
		return -EIO;
	}

	tqspi->tx_dma_desc->callback = tegra_qspi_dma_complete;
	tqspi->tx_dma_desc->callback_param = &tqspi->tx_dma_complete;
	dmaengine_submit(tqspi->tx_dma_desc);
	dma_async_issue_pending(tqspi->tx_dma_chan);

	return 0;
}

static int tegra_qspi_start_rx_dma(struct tegra_qspi *tqspi, struct spi_transfer *t, int len)
{
	dma_addr_t rx_dma_phys;

	reinit_completion(&tqspi->rx_dma_complete);

	if (tqspi->is_packed)
		rx_dma_phys = t->rx_dma;
	else
		rx_dma_phys = tqspi->rx_dma_phys;

	tqspi->rx_dma_desc = dmaengine_prep_slave_single(tqspi->rx_dma_chan, rx_dma_phys,
							 len, DMA_DEV_TO_MEM,
							 DMA_PREP_INTERRUPT |  DMA_CTRL_ACK);

	if (!tqspi->rx_dma_desc) {
		dev_err(tqspi->dev, "Unable to get RX descriptor\n");
		return -EIO;
	}

	tqspi->rx_dma_desc->callback = tegra_qspi_dma_complete;
	tqspi->rx_dma_desc->callback_param = &tqspi->rx_dma_complete;
	dmaengine_submit(tqspi->rx_dma_desc);
	dma_async_issue_pending(tqspi->rx_dma_chan);

	return 0;
}

static int tegra_qspi_flush_fifos(struct tegra_qspi *tqspi, bool atomic)
{
	void __iomem *addr = tqspi->base + QSPI_FIFO_STATUS;
	u32 val;

	val = tegra_qspi_readl(tqspi, QSPI_FIFO_STATUS);
	if ((val & QSPI_FIFO_EMPTY) == QSPI_FIFO_EMPTY)
		return 0;

	val |= QSPI_RX_FIFO_FLUSH | QSPI_TX_FIFO_FLUSH;
	tegra_qspi_writel(tqspi, val, QSPI_FIFO_STATUS);

	if (!atomic)
		return readl_relaxed_poll_timeout(addr, val,
						  (val & QSPI_FIFO_EMPTY) == QSPI_FIFO_EMPTY,
						  1000, 1000000);

	return readl_relaxed_poll_timeout_atomic(addr, val,
						 (val & QSPI_FIFO_EMPTY) == QSPI_FIFO_EMPTY,
						 1000, 1000000);
}

static void tegra_qspi_unmask_irq(struct tegra_qspi *tqspi)
{
	u32 intr_mask;

	intr_mask = tegra_qspi_readl(tqspi, QSPI_INTR_MASK);
	intr_mask &= ~(QSPI_INTR_RDY_MASK | QSPI_INTR_RX_TX_FIFO_ERR);
	tegra_qspi_writel(tqspi, intr_mask, QSPI_INTR_MASK);
}

static int tegra_qspi_dma_map_xfer(struct tegra_qspi *tqspi, struct spi_transfer *t)
{
	u8 *tx_buf = (u8 *)t->tx_buf + tqspi->cur_tx_pos;
	u8 *rx_buf = (u8 *)t->rx_buf + tqspi->cur_rx_pos;
	unsigned int len;

	len = DIV_ROUND_UP(tqspi->curr_dma_words * tqspi->bytes_per_word, 4) * 4;

	if (t->tx_buf) {
		t->tx_dma = dma_map_single(tqspi->dev, (void *)tx_buf, len, DMA_TO_DEVICE);
		if (dma_mapping_error(tqspi->dev, t->tx_dma))
			return -ENOMEM;
	}

	if (t->rx_buf) {
		t->rx_dma = dma_map_single(tqspi->dev, (void *)rx_buf, len, DMA_FROM_DEVICE);
		if (dma_mapping_error(tqspi->dev, t->rx_dma)) {
			dma_unmap_single(tqspi->dev, t->tx_dma, len, DMA_TO_DEVICE);
			return -ENOMEM;
		}
	}

	return 0;
}

static void tegra_qspi_dma_unmap_xfer(struct tegra_qspi *tqspi, struct spi_transfer *t)
{
	unsigned int len;

	len = DIV_ROUND_UP(tqspi->curr_dma_words * tqspi->bytes_per_word, 4) * 4;

	dma_unmap_single(tqspi->dev, t->tx_dma, len, DMA_TO_DEVICE);
	dma_unmap_single(tqspi->dev, t->rx_dma, len, DMA_FROM_DEVICE);
}

static int tegra_qspi_start_dma_based_transfer(struct tegra_qspi *tqspi, struct spi_transfer *t)
{
	struct dma_slave_config dma_sconfig = { 0 };
	unsigned int len;
	u8 dma_burst;
	int ret = 0;
	u32 val;

	if (tqspi->is_packed) {
		ret = tegra_qspi_dma_map_xfer(tqspi, t);
		if (ret < 0)
			return ret;
	}

	val = QSPI_DMA_BLK_SET(tqspi->curr_dma_words - 1);
	tegra_qspi_writel(tqspi, val, QSPI_DMA_BLK);

	tegra_qspi_unmask_irq(tqspi);

	if (tqspi->is_packed)
		len = DIV_ROUND_UP(tqspi->curr_dma_words * tqspi->bytes_per_word, 4) * 4;
	else
		len = tqspi->curr_dma_words * 4;

	/* set attention level based on length of transfer */
	val = 0;
	if (len & 0xf) {
		val |= QSPI_TX_TRIG_1 | QSPI_RX_TRIG_1;
		dma_burst = 1;
	} else if (((len) >> 4) & 0x1) {
		val |= QSPI_TX_TRIG_4 | QSPI_RX_TRIG_4;
		dma_burst = 4;
	} else {
		val |= QSPI_TX_TRIG_8 | QSPI_RX_TRIG_8;
		dma_burst = 8;
	}

	tegra_qspi_writel(tqspi, val, QSPI_DMA_CTL);
	tqspi->dma_control_reg = val;

	dma_sconfig.device_fc = true;
	if (tqspi->cur_direction & DATA_DIR_TX) {
		dma_sconfig.dst_addr = tqspi->phys + QSPI_TX_FIFO;
		dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		dma_sconfig.dst_maxburst = dma_burst;
		ret = dmaengine_slave_config(tqspi->tx_dma_chan, &dma_sconfig);
		if (ret < 0) {
			dev_err(tqspi->dev, "failed DMA slave config: %d\n", ret);
			return ret;
		}

		tegra_qspi_copy_client_txbuf_to_qspi_txbuf(tqspi, t);
		ret = tegra_qspi_start_tx_dma(tqspi, t, len);
		if (ret < 0) {
			dev_err(tqspi->dev, "failed to starting TX DMA: %d\n", ret);
			return ret;
		}
	}

	if (tqspi->cur_direction & DATA_DIR_RX) {
		dma_sconfig.src_addr = tqspi->phys + QSPI_RX_FIFO;
		dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		dma_sconfig.src_maxburst = dma_burst;
		ret = dmaengine_slave_config(tqspi->rx_dma_chan, &dma_sconfig);
		if (ret < 0) {
			dev_err(tqspi->dev, "failed DMA slave config: %d\n", ret);
			return ret;
		}

		dma_sync_single_for_device(tqspi->dev, tqspi->rx_dma_phys,
					   tqspi->dma_buf_size,
					   DMA_FROM_DEVICE);

		ret = tegra_qspi_start_rx_dma(tqspi, t, len);
		if (ret < 0) {
			dev_err(tqspi->dev, "failed to start RX DMA: %d\n", ret);
			if (tqspi->cur_direction & DATA_DIR_TX)
				dmaengine_terminate_all(tqspi->tx_dma_chan);
			return ret;
		}
	}

	tegra_qspi_writel(tqspi, tqspi->command1_reg, QSPI_COMMAND1);

	tqspi->is_curr_dma_xfer = true;
	tqspi->dma_control_reg = val;
	val |= QSPI_DMA_EN;
	tegra_qspi_writel(tqspi, val, QSPI_DMA_CTL);

	return ret;
}

static int tegra_qspi_start_cpu_based_transfer(struct tegra_qspi *qspi, struct spi_transfer *t)
{
	u32 val;
	unsigned int cur_words;

	if (qspi->cur_direction & DATA_DIR_TX)
		cur_words = tegra_qspi_fill_tx_fifo_from_client_txbuf(qspi, t);
	else
		cur_words = qspi->curr_dma_words;

	val = QSPI_DMA_BLK_SET(cur_words - 1);
	tegra_qspi_writel(qspi, val, QSPI_DMA_BLK);

	tegra_qspi_unmask_irq(qspi);

	qspi->is_curr_dma_xfer = false;
	val = qspi->command1_reg;
	val |= QSPI_PIO;
	tegra_qspi_writel(qspi, val, QSPI_COMMAND1);

	return 0;
}

static void tegra_qspi_deinit_dma(struct tegra_qspi *tqspi)
{
	if (tqspi->tx_dma_buf) {
		dma_free_coherent(tqspi->dev, tqspi->dma_buf_size,
				  tqspi->tx_dma_buf, tqspi->tx_dma_phys);
		tqspi->tx_dma_buf = NULL;
	}

	if (tqspi->tx_dma_chan) {
		dma_release_channel(tqspi->tx_dma_chan);
		tqspi->tx_dma_chan = NULL;
	}

	if (tqspi->rx_dma_buf) {
		dma_free_coherent(tqspi->dev, tqspi->dma_buf_size,
				  tqspi->rx_dma_buf, tqspi->rx_dma_phys);
		tqspi->rx_dma_buf = NULL;
	}

	if (tqspi->rx_dma_chan) {
		dma_release_channel(tqspi->rx_dma_chan);
		tqspi->rx_dma_chan = NULL;
	}
}

static int tegra_qspi_init_dma(struct tegra_qspi *tqspi)
{
	struct dma_chan *dma_chan;
	dma_addr_t dma_phys;
	u32 *dma_buf;
	int err;

	dma_chan = dma_request_chan(tqspi->dev, "rx");
	if (IS_ERR(dma_chan)) {
		err = PTR_ERR(dma_chan);
		goto err_out;
	}

	tqspi->rx_dma_chan = dma_chan;

	dma_buf = dma_alloc_coherent(tqspi->dev, tqspi->dma_buf_size, &dma_phys, GFP_KERNEL);
	if (!dma_buf) {
		err = -ENOMEM;
		goto err_out;
	}

	tqspi->rx_dma_buf = dma_buf;
	tqspi->rx_dma_phys = dma_phys;

	dma_chan = dma_request_chan(tqspi->dev, "tx");
	if (IS_ERR(dma_chan)) {
		err = PTR_ERR(dma_chan);
		goto err_out;
	}

	tqspi->tx_dma_chan = dma_chan;

	dma_buf = dma_alloc_coherent(tqspi->dev, tqspi->dma_buf_size, &dma_phys, GFP_KERNEL);
	if (!dma_buf) {
		err = -ENOMEM;
		goto err_out;
	}

	tqspi->tx_dma_buf = dma_buf;
	tqspi->tx_dma_phys = dma_phys;
	tqspi->use_dma = true;

	return 0;

err_out:
	tegra_qspi_deinit_dma(tqspi);

	if (err != -EPROBE_DEFER) {
		dev_err(tqspi->dev, "cannot use DMA: %d\n", err);
		dev_err(tqspi->dev, "falling back to PIO\n");
		return 0;
	}

	return err;
}

static u32 tegra_qspi_setup_transfer_one(struct spi_device *spi, struct spi_transfer *t,
					 bool is_first_of_msg)
{
	struct tegra_qspi *tqspi = spi_master_get_devdata(spi->master);
	struct tegra_qspi_client_data *cdata = spi->controller_data;
	u32 command1, command2, speed = t->speed_hz;
	u8 bits_per_word = t->bits_per_word;
	u32 tx_tap = 0, rx_tap = 0;
	int req_mode;

	if (speed != tqspi->cur_speed) {
		clk_set_rate(tqspi->clk, speed);
		tqspi->cur_speed = speed;
	}

	tqspi->cur_pos = 0;
	tqspi->cur_rx_pos = 0;
	tqspi->cur_tx_pos = 0;
	tqspi->curr_xfer = t;

	if (is_first_of_msg) {
		tegra_qspi_mask_clear_irq(tqspi);

		command1 = tqspi->def_command1_reg;
		command1 |= QSPI_BIT_LENGTH(bits_per_word - 1);

		command1 &= ~QSPI_CONTROL_MODE_MASK;
		req_mode = spi->mode & 0x3;
		if (req_mode == SPI_MODE_3)
			command1 |= QSPI_CONTROL_MODE_3;
		else
			command1 |= QSPI_CONTROL_MODE_0;

		if (spi->mode & SPI_CS_HIGH)
			command1 |= QSPI_CS_SW_VAL;
		else
			command1 &= ~QSPI_CS_SW_VAL;
		tegra_qspi_writel(tqspi, command1, QSPI_COMMAND1);

		if (cdata && cdata->tx_clk_tap_delay)
			tx_tap = cdata->tx_clk_tap_delay;

		if (cdata && cdata->rx_clk_tap_delay)
			rx_tap = cdata->rx_clk_tap_delay;

		command2 = QSPI_TX_TAP_DELAY(tx_tap) | QSPI_RX_TAP_DELAY(rx_tap);
		if (command2 != tqspi->def_command2_reg)
			tegra_qspi_writel(tqspi, command2, QSPI_COMMAND2);

	} else {
		command1 = tqspi->command1_reg;
		command1 &= ~QSPI_BIT_LENGTH(~0);
		command1 |= QSPI_BIT_LENGTH(bits_per_word - 1);
	}

	command1 &= ~QSPI_SDR_DDR_SEL;

	return command1;
}

static int tegra_qspi_start_transfer_one(struct spi_device *spi,
					 struct spi_transfer *t, u32 command1)
{
	struct tegra_qspi *tqspi = spi_master_get_devdata(spi->master);
	unsigned int total_fifo_words;
	u8 bus_width = 0;
	int ret;

	total_fifo_words = tegra_qspi_calculate_curr_xfer_param(tqspi, t);

	command1 &= ~QSPI_PACKED;
	if (tqspi->is_packed)
		command1 |= QSPI_PACKED;
	tegra_qspi_writel(tqspi, command1, QSPI_COMMAND1);

	tqspi->cur_direction = 0;

	command1 &= ~(QSPI_TX_EN | QSPI_RX_EN);
	if (t->rx_buf) {
		command1 |= QSPI_RX_EN;
		tqspi->cur_direction |= DATA_DIR_RX;
		bus_width = t->rx_nbits;
	}

	if (t->tx_buf) {
		command1 |= QSPI_TX_EN;
		tqspi->cur_direction |= DATA_DIR_TX;
		bus_width = t->tx_nbits;
	}

	command1 &= ~QSPI_INTERFACE_WIDTH_MASK;

	if (bus_width == SPI_NBITS_QUAD)
		command1 |= QSPI_INTERFACE_WIDTH_QUAD;
	else if (bus_width == SPI_NBITS_DUAL)
		command1 |= QSPI_INTERFACE_WIDTH_DUAL;
	else
		command1 |= QSPI_INTERFACE_WIDTH_SINGLE;

	tqspi->command1_reg = command1;

	tegra_qspi_writel(tqspi, QSPI_NUM_DUMMY_CYCLE(tqspi->dummy_cycles), QSPI_MISC_REG);

	ret = tegra_qspi_flush_fifos(tqspi, false);
	if (ret < 0)
		return ret;

	if (tqspi->use_dma && total_fifo_words > QSPI_FIFO_DEPTH)
		ret = tegra_qspi_start_dma_based_transfer(tqspi, t);
	else
		ret = tegra_qspi_start_cpu_based_transfer(tqspi, t);

	return ret;
}

static struct tegra_qspi_client_data *tegra_qspi_parse_cdata_dt(struct spi_device *spi)
{
	struct tegra_qspi_client_data *cdata;
	struct device_node *slave_np = spi->dev.of_node;

	cdata = devm_kzalloc(&spi->dev, sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return NULL;

	of_property_read_u32(slave_np, "nvidia,tx-clk-tap-delay",
			     &cdata->tx_clk_tap_delay);
	of_property_read_u32(slave_np, "nvidia,rx-clk-tap-delay",
			     &cdata->rx_clk_tap_delay);
	return cdata;
}

static int tegra_qspi_setup(struct spi_device *spi)
{
	struct tegra_qspi *tqspi = spi_master_get_devdata(spi->master);
	struct tegra_qspi_client_data *cdata = spi->controller_data;
	unsigned long flags;
	u32 val;
	int ret;

	ret = pm_runtime_resume_and_get(tqspi->dev);
	if (ret < 0) {
		dev_err(tqspi->dev, "failed to get runtime PM: %d\n", ret);
		return ret;
	}

	if (!cdata) {
		cdata = tegra_qspi_parse_cdata_dt(spi);
		spi->controller_data = cdata;
	}

	spin_lock_irqsave(&tqspi->lock, flags);

	/* keep default cs state to inactive */
	val = tqspi->def_command1_reg;
	if (spi->mode & SPI_CS_HIGH)
		val &= ~QSPI_CS_SW_VAL;
	else
		val |= QSPI_CS_SW_VAL;

	tqspi->def_command1_reg = val;
	tegra_qspi_writel(tqspi, tqspi->def_command1_reg, QSPI_COMMAND1);

	spin_unlock_irqrestore(&tqspi->lock, flags);

	pm_runtime_put(tqspi->dev);

	return 0;
}

static void tegra_qspi_dump_regs(struct tegra_qspi *tqspi)
{
	dev_dbg(tqspi->dev, "============ QSPI REGISTER DUMP ============\n");
	dev_dbg(tqspi->dev, "Command1:    0x%08x | Command2:    0x%08x\n",
		tegra_qspi_readl(tqspi, QSPI_COMMAND1),
		tegra_qspi_readl(tqspi, QSPI_COMMAND2));
	dev_dbg(tqspi->dev, "DMA_CTL:     0x%08x | DMA_BLK:     0x%08x\n",
		tegra_qspi_readl(tqspi, QSPI_DMA_CTL),
		tegra_qspi_readl(tqspi, QSPI_DMA_BLK));
	dev_dbg(tqspi->dev, "INTR_MASK:  0x%08x | MISC: 0x%08x\n",
		tegra_qspi_readl(tqspi, QSPI_INTR_MASK),
		tegra_qspi_readl(tqspi, QSPI_MISC_REG));
	dev_dbg(tqspi->dev, "TRANS_STAT:  0x%08x | FIFO_STATUS: 0x%08x\n",
		tegra_qspi_readl(tqspi, QSPI_TRANS_STATUS),
		tegra_qspi_readl(tqspi, QSPI_FIFO_STATUS));
}

static void tegra_qspi_handle_error(struct tegra_qspi *tqspi)
{
	dev_err(tqspi->dev, "error in transfer, fifo status 0x%08x\n", tqspi->status_reg);
	tegra_qspi_dump_regs(tqspi);
	tegra_qspi_flush_fifos(tqspi, true);
	reset_control_assert(tqspi->rst);
	udelay(2);
	reset_control_deassert(tqspi->rst);
}

static void tegra_qspi_transfer_end(struct spi_device *spi)
{
	struct tegra_qspi *tqspi = spi_master_get_devdata(spi->master);
	int cs_val = (spi->mode & SPI_CS_HIGH) ? 0 : 1;

	if (cs_val)
		tqspi->command1_reg |= QSPI_CS_SW_VAL;
	else
		tqspi->command1_reg &= ~QSPI_CS_SW_VAL;
	tegra_qspi_writel(tqspi, tqspi->command1_reg, QSPI_COMMAND1);
	tegra_qspi_writel(tqspi, tqspi->def_command1_reg, QSPI_COMMAND1);
}

static int tegra_qspi_transfer_one_message(struct spi_master *master, struct spi_message *msg)
{
	struct tegra_qspi *tqspi = spi_master_get_devdata(master);
	struct spi_device *spi = msg->spi;
	struct spi_transfer *transfer;
	bool is_first_msg = true;
	int ret;

	msg->status = 0;
	msg->actual_length = 0;
	tqspi->tx_status = 0;
	tqspi->rx_status = 0;

	list_for_each_entry(transfer, &msg->transfers, transfer_list) {
		struct spi_transfer *xfer = transfer;
		u8 dummy_bytes = 0;
		u32 cmd1;

		tqspi->dummy_cycles = 0;
		/*
		 * Tegra QSPI hardware supports dummy bytes transfer after actual transfer
		 * bytes based on programmed dummy clock cycles in the QSPI_MISC register.
		 * So, check if the next transfer is dummy data transfer and program dummy
		 * clock cycles along with the current transfer and skip next transfer.
		 */
		if (!list_is_last(&xfer->transfer_list, &msg->transfers)) {
			struct spi_transfer *next_xfer;

			next_xfer = list_next_entry(xfer, transfer_list);
			if (next_xfer->dummy_data) {
				u32 dummy_cycles = next_xfer->len * 8 / next_xfer->tx_nbits;

				if (dummy_cycles <= QSPI_DUMMY_CYCLES_MAX) {
					tqspi->dummy_cycles = dummy_cycles;
					dummy_bytes = next_xfer->len;
					transfer = next_xfer;
				}
			}
		}

		reinit_completion(&tqspi->xfer_completion);

		cmd1 = tegra_qspi_setup_transfer_one(spi, xfer, is_first_msg);

		ret = tegra_qspi_start_transfer_one(spi, xfer, cmd1);
		if (ret < 0) {
			dev_err(tqspi->dev, "failed to start transfer: %d\n", ret);
			goto complete_xfer;
		}

		is_first_msg = false;
		ret = wait_for_completion_timeout(&tqspi->xfer_completion,
						  QSPI_DMA_TIMEOUT);
		if (WARN_ON(ret == 0)) {
			dev_err(tqspi->dev, "transfer timeout\n");
			if (tqspi->is_curr_dma_xfer && (tqspi->cur_direction & DATA_DIR_TX))
				dmaengine_terminate_all(tqspi->tx_dma_chan);
			if (tqspi->is_curr_dma_xfer && (tqspi->cur_direction & DATA_DIR_RX))
				dmaengine_terminate_all(tqspi->rx_dma_chan);
			tegra_qspi_handle_error(tqspi);
			ret = -EIO;
			goto complete_xfer;
		}

		if (tqspi->tx_status ||  tqspi->rx_status) {
			tegra_qspi_handle_error(tqspi);
			ret = -EIO;
			goto complete_xfer;
		}

		msg->actual_length += xfer->len + dummy_bytes;

complete_xfer:
		if (ret < 0) {
			tegra_qspi_transfer_end(spi);
			spi_transfer_delay_exec(xfer);
			goto exit;
		}

		if (list_is_last(&xfer->transfer_list, &msg->transfers)) {
			/* de-activate CS after last transfer only when cs_change is not set */
			if (!xfer->cs_change) {
				tegra_qspi_transfer_end(spi);
				spi_transfer_delay_exec(xfer);
			}
		} else if (xfer->cs_change) {
			 /* de-activated CS between the transfers only when cs_change is set */
			tegra_qspi_transfer_end(spi);
			spi_transfer_delay_exec(xfer);
		}
	}

	ret = 0;
exit:
	msg->status = ret;
	spi_finalize_current_message(master);
	return ret;
}

static irqreturn_t handle_cpu_based_xfer(struct tegra_qspi *tqspi)
{
	struct spi_transfer *t = tqspi->curr_xfer;
	unsigned long flags;

	spin_lock_irqsave(&tqspi->lock, flags);

	if (tqspi->tx_status ||  tqspi->rx_status) {
		tegra_qspi_handle_error(tqspi);
		complete(&tqspi->xfer_completion);
		goto exit;
	}

	if (tqspi->cur_direction & DATA_DIR_RX)
		tegra_qspi_read_rx_fifo_to_client_rxbuf(tqspi, t);

	if (tqspi->cur_direction & DATA_DIR_TX)
		tqspi->cur_pos = tqspi->cur_tx_pos;
	else
		tqspi->cur_pos = tqspi->cur_rx_pos;

	if (tqspi->cur_pos == t->len) {
		complete(&tqspi->xfer_completion);
		goto exit;
	}

	tegra_qspi_calculate_curr_xfer_param(tqspi, t);
	tegra_qspi_start_cpu_based_transfer(tqspi, t);
exit:
	spin_unlock_irqrestore(&tqspi->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t handle_dma_based_xfer(struct tegra_qspi *tqspi)
{
	struct spi_transfer *t = tqspi->curr_xfer;
	unsigned int total_fifo_words;
	unsigned long flags;
	long wait_status;
	int err = 0;

	if (tqspi->cur_direction & DATA_DIR_TX) {
		if (tqspi->tx_status) {
			dmaengine_terminate_all(tqspi->tx_dma_chan);
			err += 1;
		} else {
			wait_status = wait_for_completion_interruptible_timeout(
				&tqspi->tx_dma_complete, QSPI_DMA_TIMEOUT);
			if (wait_status <= 0) {
				dmaengine_terminate_all(tqspi->tx_dma_chan);
				dev_err(tqspi->dev, "failed TX DMA transfer\n");
				err += 1;
			}
		}
	}

	if (tqspi->cur_direction & DATA_DIR_RX) {
		if (tqspi->rx_status) {
			dmaengine_terminate_all(tqspi->rx_dma_chan);
			err += 2;
		} else {
			wait_status = wait_for_completion_interruptible_timeout(
				&tqspi->rx_dma_complete, QSPI_DMA_TIMEOUT);
			if (wait_status <= 0) {
				dmaengine_terminate_all(tqspi->rx_dma_chan);
				dev_err(tqspi->dev, "failed RX DMA transfer\n");
				err += 2;
			}
		}
	}

	spin_lock_irqsave(&tqspi->lock, flags);

	if (err) {
		tegra_qspi_dma_unmap_xfer(tqspi, t);
		tegra_qspi_handle_error(tqspi);
		complete(&tqspi->xfer_completion);
		goto exit;
	}

	if (tqspi->cur_direction & DATA_DIR_RX)
		tegra_qspi_copy_qspi_rxbuf_to_client_rxbuf(tqspi, t);

	if (tqspi->cur_direction & DATA_DIR_TX)
		tqspi->cur_pos = tqspi->cur_tx_pos;
	else
		tqspi->cur_pos = tqspi->cur_rx_pos;

	if (tqspi->cur_pos == t->len) {
		tegra_qspi_dma_unmap_xfer(tqspi, t);
		complete(&tqspi->xfer_completion);
		goto exit;
	}

	tegra_qspi_dma_unmap_xfer(tqspi, t);

	/* continue transfer in current message */
	total_fifo_words = tegra_qspi_calculate_curr_xfer_param(tqspi, t);
	if (total_fifo_words > QSPI_FIFO_DEPTH)
		err = tegra_qspi_start_dma_based_transfer(tqspi, t);
	else
		err = tegra_qspi_start_cpu_based_transfer(tqspi, t);

exit:
	spin_unlock_irqrestore(&tqspi->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t tegra_qspi_isr_thread(int irq, void *context_data)
{
	struct tegra_qspi *tqspi = context_data;

	tqspi->status_reg = tegra_qspi_readl(tqspi, QSPI_FIFO_STATUS);

	if (tqspi->cur_direction & DATA_DIR_TX)
		tqspi->tx_status = tqspi->status_reg & (QSPI_TX_FIFO_UNF | QSPI_TX_FIFO_OVF);

	if (tqspi->cur_direction & DATA_DIR_RX)
		tqspi->rx_status = tqspi->status_reg & (QSPI_RX_FIFO_OVF | QSPI_RX_FIFO_UNF);

	tegra_qspi_mask_clear_irq(tqspi);

	if (!tqspi->is_curr_dma_xfer)
		return handle_cpu_based_xfer(tqspi);

	return handle_dma_based_xfer(tqspi);
}

static const struct of_device_id tegra_qspi_of_match[] = {
	{ .compatible = "nvidia,tegra210-qspi", },
	{ .compatible = "nvidia,tegra186-qspi", },
	{ .compatible = "nvidia,tegra194-qspi", },
	{}
};

MODULE_DEVICE_TABLE(of, tegra_qspi_of_match);

static int tegra_qspi_probe(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct tegra_qspi	*tqspi;
	struct resource		*r;
	int ret, qspi_irq;
	int bus_num;

	master = devm_spi_alloc_master(&pdev->dev, sizeof(*tqspi));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);
	tqspi = spi_master_get_devdata(master);

	master->mode_bits = SPI_MODE_0 | SPI_MODE_3 | SPI_CS_HIGH |
			    SPI_TX_DUAL | SPI_RX_DUAL | SPI_TX_QUAD | SPI_RX_QUAD;
	master->bits_per_word_mask = SPI_BPW_MASK(32) | SPI_BPW_MASK(16) | SPI_BPW_MASK(8);
	master->setup = tegra_qspi_setup;
	master->transfer_one_message = tegra_qspi_transfer_one_message;
	master->num_chipselect = 1;
	master->auto_runtime_pm = true;

	bus_num = of_alias_get_id(pdev->dev.of_node, "spi");
	if (bus_num >= 0)
		master->bus_num = bus_num;

	tqspi->master = master;
	tqspi->dev = &pdev->dev;
	spin_lock_init(&tqspi->lock);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tqspi->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(tqspi->base))
		return PTR_ERR(tqspi->base);

	tqspi->phys = r->start;
	qspi_irq = platform_get_irq(pdev, 0);
	if (qspi_irq < 0)
		return qspi_irq;
	tqspi->irq = qspi_irq;

	tqspi->clk = devm_clk_get(&pdev->dev, "qspi");
	if (IS_ERR(tqspi->clk)) {
		ret = PTR_ERR(tqspi->clk);
		dev_err(&pdev->dev, "failed to get clock: %d\n", ret);
		return ret;
	}

	tqspi->rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(tqspi->rst)) {
		ret = PTR_ERR(tqspi->rst);
		dev_err(&pdev->dev, "failed to get reset control: %d\n", ret);
		return ret;
	}

	tqspi->max_buf_size = QSPI_FIFO_DEPTH << 2;
	tqspi->dma_buf_size = DEFAULT_QSPI_DMA_BUF_LEN;

	ret = tegra_qspi_init_dma(tqspi);
	if (ret < 0)
		return ret;

	if (tqspi->use_dma)
		tqspi->max_buf_size = tqspi->dma_buf_size;

	init_completion(&tqspi->tx_dma_complete);
	init_completion(&tqspi->rx_dma_complete);
	init_completion(&tqspi->xfer_completion);

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get runtime PM: %d\n", ret);
		goto exit_pm_disable;
	}

	reset_control_assert(tqspi->rst);
	udelay(2);
	reset_control_deassert(tqspi->rst);

	tqspi->def_command1_reg = QSPI_M_S | QSPI_CS_SW_HW |  QSPI_CS_SW_VAL;
	tegra_qspi_writel(tqspi, tqspi->def_command1_reg, QSPI_COMMAND1);
	tqspi->spi_cs_timing1 = tegra_qspi_readl(tqspi, QSPI_CS_TIMING1);
	tqspi->spi_cs_timing2 = tegra_qspi_readl(tqspi, QSPI_CS_TIMING2);
	tqspi->def_command2_reg = tegra_qspi_readl(tqspi, QSPI_COMMAND2);

	pm_runtime_put(&pdev->dev);

	ret = request_threaded_irq(tqspi->irq, NULL,
				   tegra_qspi_isr_thread, IRQF_ONESHOT,
				   dev_name(&pdev->dev), tqspi);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ#%u: %d\n", tqspi->irq, ret);
		goto exit_pm_disable;
	}

	master->dev.of_node = pdev->dev.of_node;
	ret = spi_register_master(master);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register master: %d\n", ret);
		goto exit_free_irq;
	}

	return 0;

exit_free_irq:
	free_irq(qspi_irq, tqspi);
exit_pm_disable:
	pm_runtime_force_suspend(&pdev->dev);
	tegra_qspi_deinit_dma(tqspi);
	return ret;
}

static int tegra_qspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct tegra_qspi *tqspi = spi_master_get_devdata(master);

	spi_unregister_master(master);
	free_irq(tqspi->irq, tqspi);
	pm_runtime_force_suspend(&pdev->dev);
	tegra_qspi_deinit_dma(tqspi);

	return 0;
}

static int __maybe_unused tegra_qspi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);

	return spi_master_suspend(master);
}

static int __maybe_unused tegra_qspi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_qspi *tqspi = spi_master_get_devdata(master);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err(dev, "failed to get runtime PM: %d\n", ret);
		return ret;
	}

	tegra_qspi_writel(tqspi, tqspi->command1_reg, QSPI_COMMAND1);
	tegra_qspi_writel(tqspi, tqspi->def_command2_reg, QSPI_COMMAND2);
	pm_runtime_put(dev);

	return spi_master_resume(master);
}

static int __maybe_unused tegra_qspi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_qspi *tqspi = spi_master_get_devdata(master);

	/* flush all write which are in PPSB queue by reading back */
	tegra_qspi_readl(tqspi, QSPI_COMMAND1);

	clk_disable_unprepare(tqspi->clk);

	return 0;
}

static int __maybe_unused tegra_qspi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct tegra_qspi *tqspi = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(tqspi->clk);
	if (ret < 0)
		dev_err(tqspi->dev, "failed to enable clock: %d\n", ret);

	return ret;
}

static const struct dev_pm_ops tegra_qspi_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_qspi_runtime_suspend, tegra_qspi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_qspi_suspend, tegra_qspi_resume)
};

static struct platform_driver tegra_qspi_driver = {
	.driver = {
		.name		= "tegra-qspi",
		.pm		= &tegra_qspi_pm_ops,
		.of_match_table	= tegra_qspi_of_match,
	},
	.probe =	tegra_qspi_probe,
	.remove =	tegra_qspi_remove,
};
module_platform_driver(tegra_qspi_driver);

MODULE_ALIAS("platform:qspi-tegra");
MODULE_DESCRIPTION("NVIDIA Tegra QSPI Controller Driver");
MODULE_AUTHOR("Sowjanya Komatineni <skomatineni@nvidia.com>");
MODULE_LICENSE("GPL v2");
