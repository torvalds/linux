// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2018, The Linux foundation. All rights reserved.

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/qcom-geni-se.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>

/* SPI SE specific registers and respective register fields */
#define SE_SPI_CPHA		0x224
#define CPHA			BIT(0)

#define SE_SPI_LOOPBACK		0x22c
#define LOOPBACK_ENABLE		0x1
#define NORMAL_MODE		0x0
#define LOOPBACK_MSK		GENMASK(1, 0)

#define SE_SPI_CPOL		0x230
#define CPOL			BIT(2)

#define SE_SPI_DEMUX_OUTPUT_INV	0x24c
#define CS_DEMUX_OUTPUT_INV_MSK	GENMASK(3, 0)

#define SE_SPI_DEMUX_SEL	0x250
#define CS_DEMUX_OUTPUT_SEL	GENMASK(3, 0)

#define SE_SPI_TRANS_CFG	0x25c
#define CS_TOGGLE		BIT(0)

#define SE_SPI_WORD_LEN		0x268
#define WORD_LEN_MSK		GENMASK(9, 0)
#define MIN_WORD_LEN		4

#define SE_SPI_TX_TRANS_LEN	0x26c
#define SE_SPI_RX_TRANS_LEN	0x270
#define TRANS_LEN_MSK		GENMASK(23, 0)

#define SE_SPI_PRE_POST_CMD_DLY	0x274

#define SE_SPI_DELAY_COUNTERS	0x278
#define SPI_INTER_WORDS_DELAY_MSK	GENMASK(9, 0)
#define SPI_CS_CLK_DELAY_MSK		GENMASK(19, 10)
#define SPI_CS_CLK_DELAY_SHFT		10

/* M_CMD OP codes for SPI */
#define SPI_TX_ONLY		1
#define SPI_RX_ONLY		2
#define SPI_TX_RX		7
#define SPI_CS_ASSERT		8
#define SPI_CS_DEASSERT		9
#define SPI_SCK_ONLY		10
/* M_CMD params for SPI */
#define SPI_PRE_CMD_DELAY	BIT(0)
#define TIMESTAMP_BEFORE	BIT(1)
#define FRAGMENTATION		BIT(2)
#define TIMESTAMP_AFTER		BIT(3)
#define POST_CMD_DELAY		BIT(4)

struct spi_geni_master {
	struct geni_se se;
	struct device *dev;
	u32 tx_fifo_depth;
	u32 fifo_width_bits;
	u32 tx_wm;
	u32 last_mode;
	unsigned long cur_speed_hz;
	unsigned long cur_sclk_hz;
	unsigned int cur_bits_per_word;
	unsigned int tx_rem_bytes;
	unsigned int rx_rem_bytes;
	const struct spi_transfer *cur_xfer;
	struct completion cs_done;
	struct completion cancel_done;
	struct completion abort_done;
	unsigned int oversampling;
	spinlock_t lock;
	int irq;
	bool cs_flag;
	bool abort_failed;
};

static int get_spi_clk_cfg(unsigned int speed_hz,
			struct spi_geni_master *mas,
			unsigned int *clk_idx,
			unsigned int *clk_div)
{
	unsigned long sclk_freq;
	unsigned int actual_hz;
	int ret;

	ret = geni_se_clk_freq_match(&mas->se,
				speed_hz * mas->oversampling,
				clk_idx, &sclk_freq, false);
	if (ret) {
		dev_err(mas->dev, "Failed(%d) to find src clk for %dHz\n",
							ret, speed_hz);
		return ret;
	}

	*clk_div = DIV_ROUND_UP(sclk_freq, mas->oversampling * speed_hz);
	actual_hz = sclk_freq / (mas->oversampling * *clk_div);

	dev_dbg(mas->dev, "req %u=>%u sclk %lu, idx %d, div %d\n", speed_hz,
				actual_hz, sclk_freq, *clk_idx, *clk_div);
	ret = dev_pm_opp_set_rate(mas->dev, sclk_freq);
	if (ret)
		dev_err(mas->dev, "dev_pm_opp_set_rate failed %d\n", ret);
	else
		mas->cur_sclk_hz = sclk_freq;

	return ret;
}

static void handle_fifo_timeout(struct spi_master *spi,
				struct spi_message *msg)
{
	struct spi_geni_master *mas = spi_master_get_devdata(spi);
	unsigned long time_left;
	struct geni_se *se = &mas->se;

	spin_lock_irq(&mas->lock);
	reinit_completion(&mas->cancel_done);
	writel(0, se->base + SE_GENI_TX_WATERMARK_REG);
	mas->cur_xfer = NULL;
	geni_se_cancel_m_cmd(se);
	spin_unlock_irq(&mas->lock);

	time_left = wait_for_completion_timeout(&mas->cancel_done, HZ);
	if (time_left)
		return;

	spin_lock_irq(&mas->lock);
	reinit_completion(&mas->abort_done);
	geni_se_abort_m_cmd(se);
	spin_unlock_irq(&mas->lock);

	time_left = wait_for_completion_timeout(&mas->abort_done, HZ);
	if (!time_left) {
		dev_err(mas->dev, "Failed to cancel/abort m_cmd\n");

		/*
		 * No need for a lock since SPI core has a lock and we never
		 * access this from an interrupt.
		 */
		mas->abort_failed = true;
	}
}

static bool spi_geni_is_abort_still_pending(struct spi_geni_master *mas)
{
	struct geni_se *se = &mas->se;
	u32 m_irq, m_irq_en;

	if (!mas->abort_failed)
		return false;

	/*
	 * The only known case where a transfer times out and then a cancel
	 * times out then an abort times out is if something is blocking our
	 * interrupt handler from running.  Avoid starting any new transfers
	 * until that sorts itself out.
	 */
	spin_lock_irq(&mas->lock);
	m_irq = readl(se->base + SE_GENI_M_IRQ_STATUS);
	m_irq_en = readl(se->base + SE_GENI_M_IRQ_EN);
	spin_unlock_irq(&mas->lock);

	if (m_irq & m_irq_en) {
		dev_err(mas->dev, "Interrupts pending after abort: %#010x\n",
			m_irq & m_irq_en);
		return true;
	}

	/*
	 * If we're here the problem resolved itself so no need to check more
	 * on future transfers.
	 */
	mas->abort_failed = false;

	return false;
}

static void spi_geni_set_cs(struct spi_device *slv, bool set_flag)
{
	struct spi_geni_master *mas = spi_master_get_devdata(slv->master);
	struct spi_master *spi = dev_get_drvdata(mas->dev);
	struct geni_se *se = &mas->se;
	unsigned long time_left;

	if (!(slv->mode & SPI_CS_HIGH))
		set_flag = !set_flag;

	if (set_flag == mas->cs_flag)
		return;

	pm_runtime_get_sync(mas->dev);

	if (spi_geni_is_abort_still_pending(mas)) {
		dev_err(mas->dev, "Can't set chip select\n");
		goto exit;
	}

	spin_lock_irq(&mas->lock);
	if (mas->cur_xfer) {
		dev_err(mas->dev, "Can't set CS when prev xfer running\n");
		spin_unlock_irq(&mas->lock);
		goto exit;
	}

	mas->cs_flag = set_flag;
	reinit_completion(&mas->cs_done);
	if (set_flag)
		geni_se_setup_m_cmd(se, SPI_CS_ASSERT, 0);
	else
		geni_se_setup_m_cmd(se, SPI_CS_DEASSERT, 0);
	spin_unlock_irq(&mas->lock);

	time_left = wait_for_completion_timeout(&mas->cs_done, HZ);
	if (!time_left) {
		dev_warn(mas->dev, "Timeout setting chip select\n");
		handle_fifo_timeout(spi, NULL);
	}

exit:
	pm_runtime_put(mas->dev);
}

static void spi_setup_word_len(struct spi_geni_master *mas, u16 mode,
					unsigned int bits_per_word)
{
	unsigned int pack_words;
	bool msb_first = (mode & SPI_LSB_FIRST) ? false : true;
	struct geni_se *se = &mas->se;
	u32 word_len;

	/*
	 * If bits_per_word isn't a byte aligned value, set the packing to be
	 * 1 SPI word per FIFO word.
	 */
	if (!(mas->fifo_width_bits % bits_per_word))
		pack_words = mas->fifo_width_bits / bits_per_word;
	else
		pack_words = 1;
	geni_se_config_packing(&mas->se, bits_per_word, pack_words, msb_first,
								true, true);
	word_len = (bits_per_word - MIN_WORD_LEN) & WORD_LEN_MSK;
	writel(word_len, se->base + SE_SPI_WORD_LEN);
}

static int geni_spi_set_clock_and_bw(struct spi_geni_master *mas,
					unsigned long clk_hz)
{
	u32 clk_sel, m_clk_cfg, idx, div;
	struct geni_se *se = &mas->se;
	int ret;

	if (clk_hz == mas->cur_speed_hz)
		return 0;

	ret = get_spi_clk_cfg(clk_hz, mas, &idx, &div);
	if (ret) {
		dev_err(mas->dev, "Err setting clk to %lu: %d\n", clk_hz, ret);
		return ret;
	}

	/*
	 * SPI core clock gets configured with the requested frequency
	 * or the frequency closer to the requested frequency.
	 * For that reason requested frequency is stored in the
	 * cur_speed_hz and referred in the consecutive transfer instead
	 * of calling clk_get_rate() API.
	 */
	mas->cur_speed_hz = clk_hz;

	clk_sel = idx & CLK_SEL_MSK;
	m_clk_cfg = (div << CLK_DIV_SHFT) | SER_CLK_EN;
	writel(clk_sel, se->base + SE_GENI_CLK_SEL);
	writel(m_clk_cfg, se->base + GENI_SER_M_CLK_CFG);

	/* Set BW quota for CPU as driver supports FIFO mode only. */
	se->icc_paths[CPU_TO_GENI].avg_bw = Bps_to_icc(mas->cur_speed_hz);
	ret = geni_icc_set_bw(se);
	if (ret)
		return ret;

	return 0;
}

static int setup_fifo_params(struct spi_device *spi_slv,
					struct spi_master *spi)
{
	struct spi_geni_master *mas = spi_master_get_devdata(spi);
	struct geni_se *se = &mas->se;
	u32 loopback_cfg = 0, cpol = 0, cpha = 0, demux_output_inv = 0;
	u32 demux_sel;

	if (mas->last_mode != spi_slv->mode) {
		if (spi_slv->mode & SPI_LOOP)
			loopback_cfg = LOOPBACK_ENABLE;

		if (spi_slv->mode & SPI_CPOL)
			cpol = CPOL;

		if (spi_slv->mode & SPI_CPHA)
			cpha = CPHA;

		if (spi_slv->mode & SPI_CS_HIGH)
			demux_output_inv = BIT(spi_slv->chip_select);

		demux_sel = spi_slv->chip_select;
		mas->cur_bits_per_word = spi_slv->bits_per_word;

		spi_setup_word_len(mas, spi_slv->mode, spi_slv->bits_per_word);
		writel(loopback_cfg, se->base + SE_SPI_LOOPBACK);
		writel(demux_sel, se->base + SE_SPI_DEMUX_SEL);
		writel(cpha, se->base + SE_SPI_CPHA);
		writel(cpol, se->base + SE_SPI_CPOL);
		writel(demux_output_inv, se->base + SE_SPI_DEMUX_OUTPUT_INV);

		mas->last_mode = spi_slv->mode;
	}

	return geni_spi_set_clock_and_bw(mas, spi_slv->max_speed_hz);
}

static int spi_geni_prepare_message(struct spi_master *spi,
					struct spi_message *spi_msg)
{
	int ret;
	struct spi_geni_master *mas = spi_master_get_devdata(spi);

	if (spi_geni_is_abort_still_pending(mas))
		return -EBUSY;

	ret = setup_fifo_params(spi_msg->spi, spi);
	if (ret)
		dev_err(mas->dev, "Couldn't select mode %d\n", ret);
	return ret;
}

static int spi_geni_init(struct spi_geni_master *mas)
{
	struct geni_se *se = &mas->se;
	unsigned int proto, major, minor, ver;
	u32 spi_tx_cfg;

	pm_runtime_get_sync(mas->dev);

	proto = geni_se_read_proto(se);
	if (proto != GENI_SE_SPI) {
		dev_err(mas->dev, "Invalid proto %d\n", proto);
		pm_runtime_put(mas->dev);
		return -ENXIO;
	}
	mas->tx_fifo_depth = geni_se_get_tx_fifo_depth(se);

	/* Width of Tx and Rx FIFO is same */
	mas->fifo_width_bits = geni_se_get_tx_fifo_width(se);

	/*
	 * Hardware programming guide suggests to configure
	 * RX FIFO RFR level to fifo_depth-2.
	 */
	geni_se_init(se, mas->tx_fifo_depth - 3, mas->tx_fifo_depth - 2);
	/* Transmit an entire FIFO worth of data per IRQ */
	mas->tx_wm = 1;
	ver = geni_se_get_qup_hw_version(se);
	major = GENI_SE_VERSION_MAJOR(ver);
	minor = GENI_SE_VERSION_MINOR(ver);

	if (major == 1 && minor == 0)
		mas->oversampling = 2;
	else
		mas->oversampling = 1;

	geni_se_select_mode(se, GENI_SE_FIFO);

	/* We always control CS manually */
	spi_tx_cfg = readl(se->base + SE_SPI_TRANS_CFG);
	spi_tx_cfg &= ~CS_TOGGLE;
	writel(spi_tx_cfg, se->base + SE_SPI_TRANS_CFG);

	pm_runtime_put(mas->dev);
	return 0;
}

static unsigned int geni_byte_per_fifo_word(struct spi_geni_master *mas)
{
	/*
	 * Calculate how many bytes we'll put in each FIFO word.  If the
	 * transfer words don't pack cleanly into a FIFO word we'll just put
	 * one transfer word in each FIFO word.  If they do pack we'll pack 'em.
	 */
	if (mas->fifo_width_bits % mas->cur_bits_per_word)
		return roundup_pow_of_two(DIV_ROUND_UP(mas->cur_bits_per_word,
						       BITS_PER_BYTE));

	return mas->fifo_width_bits / BITS_PER_BYTE;
}

static bool geni_spi_handle_tx(struct spi_geni_master *mas)
{
	struct geni_se *se = &mas->se;
	unsigned int max_bytes;
	const u8 *tx_buf;
	unsigned int bytes_per_fifo_word = geni_byte_per_fifo_word(mas);
	unsigned int i = 0;

	/* Stop the watermark IRQ if nothing to send */
	if (!mas->cur_xfer) {
		writel(0, se->base + SE_GENI_TX_WATERMARK_REG);
		return false;
	}

	max_bytes = (mas->tx_fifo_depth - mas->tx_wm) * bytes_per_fifo_word;
	if (mas->tx_rem_bytes < max_bytes)
		max_bytes = mas->tx_rem_bytes;

	tx_buf = mas->cur_xfer->tx_buf + mas->cur_xfer->len - mas->tx_rem_bytes;
	while (i < max_bytes) {
		unsigned int j;
		unsigned int bytes_to_write;
		u32 fifo_word = 0;
		u8 *fifo_byte = (u8 *)&fifo_word;

		bytes_to_write = min(bytes_per_fifo_word, max_bytes - i);
		for (j = 0; j < bytes_to_write; j++)
			fifo_byte[j] = tx_buf[i++];
		iowrite32_rep(se->base + SE_GENI_TX_FIFOn, &fifo_word, 1);
	}
	mas->tx_rem_bytes -= max_bytes;
	if (!mas->tx_rem_bytes) {
		writel(0, se->base + SE_GENI_TX_WATERMARK_REG);
		return false;
	}
	return true;
}

static void geni_spi_handle_rx(struct spi_geni_master *mas)
{
	struct geni_se *se = &mas->se;
	u32 rx_fifo_status;
	unsigned int rx_bytes;
	unsigned int rx_last_byte_valid;
	u8 *rx_buf;
	unsigned int bytes_per_fifo_word = geni_byte_per_fifo_word(mas);
	unsigned int i = 0;

	rx_fifo_status = readl(se->base + SE_GENI_RX_FIFO_STATUS);
	rx_bytes = (rx_fifo_status & RX_FIFO_WC_MSK) * bytes_per_fifo_word;
	if (rx_fifo_status & RX_LAST) {
		rx_last_byte_valid = rx_fifo_status & RX_LAST_BYTE_VALID_MSK;
		rx_last_byte_valid >>= RX_LAST_BYTE_VALID_SHFT;
		if (rx_last_byte_valid && rx_last_byte_valid < 4)
			rx_bytes -= bytes_per_fifo_word - rx_last_byte_valid;
	}

	/* Clear out the FIFO and bail if nowhere to put it */
	if (!mas->cur_xfer) {
		for (i = 0; i < DIV_ROUND_UP(rx_bytes, bytes_per_fifo_word); i++)
			readl(se->base + SE_GENI_RX_FIFOn);
		return;
	}

	if (mas->rx_rem_bytes < rx_bytes)
		rx_bytes = mas->rx_rem_bytes;

	rx_buf = mas->cur_xfer->rx_buf + mas->cur_xfer->len - mas->rx_rem_bytes;
	while (i < rx_bytes) {
		u32 fifo_word = 0;
		u8 *fifo_byte = (u8 *)&fifo_word;
		unsigned int bytes_to_read;
		unsigned int j;

		bytes_to_read = min(bytes_per_fifo_word, rx_bytes - i);
		ioread32_rep(se->base + SE_GENI_RX_FIFOn, &fifo_word, 1);
		for (j = 0; j < bytes_to_read; j++)
			rx_buf[i++] = fifo_byte[j];
	}
	mas->rx_rem_bytes -= rx_bytes;
}

static void setup_fifo_xfer(struct spi_transfer *xfer,
				struct spi_geni_master *mas,
				u16 mode, struct spi_master *spi)
{
	u32 m_cmd = 0;
	u32 len;
	struct geni_se *se = &mas->se;
	int ret;

	/*
	 * Ensure that our interrupt handler isn't still running from some
	 * prior command before we start messing with the hardware behind
	 * its back.  We don't need to _keep_ the lock here since we're only
	 * worried about racing with out interrupt handler.  The SPI core
	 * already handles making sure that we're not trying to do two
	 * transfers at once or setting a chip select and doing a transfer
	 * concurrently.
	 *
	 * NOTE: we actually _can't_ hold the lock here because possibly we
	 * might call clk_set_rate() which needs to be able to sleep.
	 */
	spin_lock_irq(&mas->lock);
	spin_unlock_irq(&mas->lock);

	if (xfer->bits_per_word != mas->cur_bits_per_word) {
		spi_setup_word_len(mas, mode, xfer->bits_per_word);
		mas->cur_bits_per_word = xfer->bits_per_word;
	}

	/* Speed and bits per word can be overridden per transfer */
	ret = geni_spi_set_clock_and_bw(mas, xfer->speed_hz);
	if (ret)
		return;

	mas->tx_rem_bytes = 0;
	mas->rx_rem_bytes = 0;

	if (!(mas->cur_bits_per_word % MIN_WORD_LEN))
		len = xfer->len * BITS_PER_BYTE / mas->cur_bits_per_word;
	else
		len = xfer->len / (mas->cur_bits_per_word / BITS_PER_BYTE + 1);
	len &= TRANS_LEN_MSK;

	mas->cur_xfer = xfer;
	if (xfer->tx_buf) {
		m_cmd |= SPI_TX_ONLY;
		mas->tx_rem_bytes = xfer->len;
		writel(len, se->base + SE_SPI_TX_TRANS_LEN);
	}

	if (xfer->rx_buf) {
		m_cmd |= SPI_RX_ONLY;
		writel(len, se->base + SE_SPI_RX_TRANS_LEN);
		mas->rx_rem_bytes = xfer->len;
	}

	/*
	 * Lock around right before we start the transfer since our
	 * interrupt could come in at any time now.
	 */
	spin_lock_irq(&mas->lock);
	geni_se_setup_m_cmd(se, m_cmd, FRAGMENTATION);

	/*
	 * TX_WATERMARK_REG should be set after SPI configuration and
	 * setting up GENI SE engine, as driver starts data transfer
	 * for the watermark interrupt.
	 */
	if (m_cmd & SPI_TX_ONLY) {
		if (geni_spi_handle_tx(mas))
			writel(mas->tx_wm, se->base + SE_GENI_TX_WATERMARK_REG);
	}
	spin_unlock_irq(&mas->lock);
}

static int spi_geni_transfer_one(struct spi_master *spi,
				struct spi_device *slv,
				struct spi_transfer *xfer)
{
	struct spi_geni_master *mas = spi_master_get_devdata(spi);

	if (spi_geni_is_abort_still_pending(mas))
		return -EBUSY;

	/* Terminate and return success for 0 byte length transfer */
	if (!xfer->len)
		return 0;

	setup_fifo_xfer(xfer, mas, slv->mode, spi);
	return 1;
}

static irqreturn_t geni_spi_isr(int irq, void *data)
{
	struct spi_master *spi = data;
	struct spi_geni_master *mas = spi_master_get_devdata(spi);
	struct geni_se *se = &mas->se;
	u32 m_irq;

	m_irq = readl(se->base + SE_GENI_M_IRQ_STATUS);
	if (!m_irq)
		return IRQ_NONE;

	if (m_irq & (M_CMD_OVERRUN_EN | M_ILLEGAL_CMD_EN | M_CMD_FAILURE_EN |
		     M_RX_FIFO_RD_ERR_EN | M_RX_FIFO_WR_ERR_EN |
		     M_TX_FIFO_RD_ERR_EN | M_TX_FIFO_WR_ERR_EN))
		dev_warn(mas->dev, "Unexpected IRQ err status %#010x\n", m_irq);

	spin_lock(&mas->lock);

	if ((m_irq & M_RX_FIFO_WATERMARK_EN) || (m_irq & M_RX_FIFO_LAST_EN))
		geni_spi_handle_rx(mas);

	if (m_irq & M_TX_FIFO_WATERMARK_EN)
		geni_spi_handle_tx(mas);

	if (m_irq & M_CMD_DONE_EN) {
		if (mas->cur_xfer) {
			spi_finalize_current_transfer(spi);
			mas->cur_xfer = NULL;
			/*
			 * If this happens, then a CMD_DONE came before all the
			 * Tx buffer bytes were sent out. This is unusual, log
			 * this condition and disable the WM interrupt to
			 * prevent the system from stalling due an interrupt
			 * storm.
			 *
			 * If this happens when all Rx bytes haven't been
			 * received, log the condition. The only known time
			 * this can happen is if bits_per_word != 8 and some
			 * registers that expect xfer lengths in num spi_words
			 * weren't written correctly.
			 */
			if (mas->tx_rem_bytes) {
				writel(0, se->base + SE_GENI_TX_WATERMARK_REG);
				dev_err(mas->dev, "Premature done. tx_rem = %d bpw%d\n",
					mas->tx_rem_bytes, mas->cur_bits_per_word);
			}
			if (mas->rx_rem_bytes)
				dev_err(mas->dev, "Premature done. rx_rem = %d bpw%d\n",
					mas->rx_rem_bytes, mas->cur_bits_per_word);
		} else {
			complete(&mas->cs_done);
		}
	}

	if (m_irq & M_CMD_CANCEL_EN)
		complete(&mas->cancel_done);
	if (m_irq & M_CMD_ABORT_EN)
		complete(&mas->abort_done);

	/*
	 * It's safe or a good idea to Ack all of our our interrupts at the
	 * end of the function. Specifically:
	 * - M_CMD_DONE_EN / M_RX_FIFO_LAST_EN: Edge triggered interrupts and
	 *   clearing Acks. Clearing at the end relies on nobody else having
	 *   started a new transfer yet or else we could be clearing _their_
	 *   done bit, but everyone grabs the spinlock before starting a new
	 *   transfer.
	 * - M_RX_FIFO_WATERMARK_EN / M_TX_FIFO_WATERMARK_EN: These appear
	 *   to be "latched level" interrupts so it's important to clear them
	 *   _after_ you've handled the condition and always safe to do so
	 *   since they'll re-assert if they're still happening.
	 */
	writel(m_irq, se->base + SE_GENI_M_IRQ_CLEAR);

	spin_unlock(&mas->lock);

	return IRQ_HANDLED;
}

static int spi_geni_probe(struct platform_device *pdev)
{
	int ret, irq;
	struct spi_master *spi;
	struct spi_geni_master *mas;
	void __iomem *base;
	struct clk *clk;
	struct device *dev = &pdev->dev;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(dev, "se");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	spi = devm_spi_alloc_master(dev, sizeof(*mas));
	if (!spi)
		return -ENOMEM;

	platform_set_drvdata(pdev, spi);
	mas = spi_master_get_devdata(spi);
	mas->irq = irq;
	mas->dev = dev;
	mas->se.dev = dev;
	mas->se.wrapper = dev_get_drvdata(dev->parent);
	mas->se.base = base;
	mas->se.clk = clk;

	ret = devm_pm_opp_set_clkname(&pdev->dev, "se");
	if (ret)
		return ret;
	/* OPP table is optional */
	ret = devm_pm_opp_of_add_table(&pdev->dev);
	if (ret && ret != -ENODEV) {
		dev_err(&pdev->dev, "invalid OPP table in device tree\n");
		return ret;
	}

	spi->bus_num = -1;
	spi->dev.of_node = dev->of_node;
	spi->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LOOP | SPI_CS_HIGH;
	spi->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
	spi->num_chipselect = 4;
	spi->max_speed_hz = 50000000;
	spi->prepare_message = spi_geni_prepare_message;
	spi->transfer_one = spi_geni_transfer_one;
	spi->auto_runtime_pm = true;
	spi->handle_err = handle_fifo_timeout;
	spi->set_cs = spi_geni_set_cs;
	spi->use_gpio_descriptors = true;

	init_completion(&mas->cs_done);
	init_completion(&mas->cancel_done);
	init_completion(&mas->abort_done);
	spin_lock_init(&mas->lock);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 250);
	pm_runtime_enable(dev);

	ret = geni_icc_get(&mas->se, NULL);
	if (ret)
		goto spi_geni_probe_runtime_disable;
	/* Set the bus quota to a reasonable value for register access */
	mas->se.icc_paths[GENI_TO_CORE].avg_bw = Bps_to_icc(CORE_2X_50_MHZ);
	mas->se.icc_paths[CPU_TO_GENI].avg_bw = GENI_DEFAULT_BW;

	ret = geni_icc_set_bw(&mas->se);
	if (ret)
		goto spi_geni_probe_runtime_disable;

	ret = spi_geni_init(mas);
	if (ret)
		goto spi_geni_probe_runtime_disable;

	ret = request_irq(mas->irq, geni_spi_isr, 0, dev_name(dev), spi);
	if (ret)
		goto spi_geni_probe_runtime_disable;

	ret = spi_register_master(spi);
	if (ret)
		goto spi_geni_probe_free_irq;

	return 0;
spi_geni_probe_free_irq:
	free_irq(mas->irq, spi);
spi_geni_probe_runtime_disable:
	pm_runtime_disable(dev);
	return ret;
}

static int spi_geni_remove(struct platform_device *pdev)
{
	struct spi_master *spi = platform_get_drvdata(pdev);
	struct spi_geni_master *mas = spi_master_get_devdata(spi);

	/* Unregister _before_ disabling pm_runtime() so we stop transfers */
	spi_unregister_master(spi);

	free_irq(mas->irq, spi);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int __maybe_unused spi_geni_runtime_suspend(struct device *dev)
{
	struct spi_master *spi = dev_get_drvdata(dev);
	struct spi_geni_master *mas = spi_master_get_devdata(spi);
	int ret;

	/* Drop the performance state vote */
	dev_pm_opp_set_rate(dev, 0);

	ret = geni_se_resources_off(&mas->se);
	if (ret)
		return ret;

	return geni_icc_disable(&mas->se);
}

static int __maybe_unused spi_geni_runtime_resume(struct device *dev)
{
	struct spi_master *spi = dev_get_drvdata(dev);
	struct spi_geni_master *mas = spi_master_get_devdata(spi);
	int ret;

	ret = geni_icc_enable(&mas->se);
	if (ret)
		return ret;

	ret = geni_se_resources_on(&mas->se);
	if (ret)
		return ret;

	return dev_pm_opp_set_rate(mas->dev, mas->cur_sclk_hz);
}

static int __maybe_unused spi_geni_suspend(struct device *dev)
{
	struct spi_master *spi = dev_get_drvdata(dev);
	int ret;

	ret = spi_master_suspend(spi);
	if (ret)
		return ret;

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		spi_master_resume(spi);

	return ret;
}

static int __maybe_unused spi_geni_resume(struct device *dev)
{
	struct spi_master *spi = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	ret = spi_master_resume(spi);
	if (ret)
		pm_runtime_force_suspend(dev);

	return ret;
}

static const struct dev_pm_ops spi_geni_pm_ops = {
	SET_RUNTIME_PM_OPS(spi_geni_runtime_suspend,
					spi_geni_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(spi_geni_suspend, spi_geni_resume)
};

static const struct of_device_id spi_geni_dt_match[] = {
	{ .compatible = "qcom,geni-spi" },
	{}
};
MODULE_DEVICE_TABLE(of, spi_geni_dt_match);

static struct platform_driver spi_geni_driver = {
	.probe  = spi_geni_probe,
	.remove = spi_geni_remove,
	.driver = {
		.name = "geni_spi",
		.pm = &spi_geni_pm_ops,
		.of_match_table = spi_geni_dt_match,
	},
};
module_platform_driver(spi_geni_driver);

MODULE_DESCRIPTION("SPI driver for GENI based QUP cores");
MODULE_LICENSE("GPL v2");
