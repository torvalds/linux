// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Renesas RZ/V2H Renesas Serial Peripheral Interface (RSPI)
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/limits.h>
#include <linux/log2.h>
#include <linux/math.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>
#include <linux/wait.h>

#include "internals.h"

/* Registers */
#define RSPI_SPDR		0x00
#define RSPI_SPCR		0x08
#define RSPI_SPPCR		0x0e
#define RSPI_SSLP		0x10
#define RSPI_SPBR		0x11
#define RSPI_SPSCR		0x13
#define RSPI_SPCMD		0x14
#define RSPI_SPDCR2		0x44
#define RSPI_SPSR		0x52
#define RSPI_SPSRC		0x6a
#define RSPI_SPFCR		0x6c

/* Register SPCR */
#define RSPI_SPCR_BPEN		BIT(31)
#define RSPI_SPCR_MSTR		BIT(30)
#define RSPI_SPCR_SPTIE		BIT(20)
#define RSPI_SPCR_SPRIE		BIT(17)
#define RSPI_SPCR_SCKASE	BIT(12)
#define RSPI_SPCR_SPE		BIT(0)

/* Register SPPCR */
#define RSPI_SPPCR_SPLP2	BIT(1)

/* Register SPBR */
#define RSPI_SPBR_SPR_MIN	0
#define RSPI_SPBR_SPR_PCLK_MIN	1
#define RSPI_SPBR_SPR_MAX	255

/* Register SPCMD */
#define RSPI_SPCMD_SSLA		GENMASK(25, 24)
#define RSPI_SPCMD_SPB		GENMASK(20, 16)
#define RSPI_SPCMD_LSBF		BIT(12)
#define RSPI_SPCMD_SSLKP	BIT(7)
#define RSPI_SPCMD_BRDV		GENMASK(3, 2)
#define RSPI_SPCMD_CPOL		BIT(1)
#define RSPI_SPCMD_CPHA		BIT(0)

#define RSPI_SPCMD_BRDV_MIN	0
#define RSPI_SPCMD_BRDV_MAX	3

/* Register SPDCR2 */
#define RSPI_SPDCR2_TTRG	GENMASK(11, 8)
#define RSPI_SPDCR2_RTRG	GENMASK(3, 0)

/* Register SPSR */
#define RSPI_SPSR_SPRF		BIT(15)

/* Register RSPI_SPSRC */
#define RSPI_SPSRC_CLEAR	0xfd80

#define RSPI_RESET_NUM		2

struct rzv2h_rspi_best_clock {
	struct clk *clk;
	unsigned long clk_rate;
	unsigned long error;
	u32 actual_hz;
	u8 brdv;
	u8 spr;
};

struct rzv2h_rspi_info {
	void (*find_tclk_rate)(struct clk *clk, u32 hz, u8 spr_min, u8 spr_max,
			       struct rzv2h_rspi_best_clock *best_clk);
	void (*find_pclk_rate)(struct clk *clk, u32 hz, u8 spr_low, u8 spr_high,
			       struct rzv2h_rspi_best_clock *best_clk);
	const char *tclk_name;
	unsigned int fifo_size;
	unsigned int num_clks;
};

struct rzv2h_rspi_priv {
	struct spi_controller *controller;
	const struct rzv2h_rspi_info *info;
	struct platform_device *pdev;
	void __iomem *base;
	struct clk *tclk;
	struct clk *pclk;
	wait_queue_head_t wait;
	unsigned int bytes_per_word;
	int irq_rx;
	u32 last_speed_hz;
	u32 freq;
	u16 status;
	u8 spr;
	u8 brdv;
	bool use_pclk;
	bool dma_callbacked;
};

#define RZV2H_RSPI_TX(func, type)					\
static inline void rzv2h_rspi_tx_##type(struct rzv2h_rspi_priv *rspi,	\
					const void *txbuf,		\
					unsigned int index) {		\
	type buf = ((type *)txbuf)[index];				\
	func(buf, rspi->base + RSPI_SPDR);				\
}

#define RZV2H_RSPI_RX(func, type)					\
static inline void rzv2h_rspi_rx_##type(struct rzv2h_rspi_priv *rspi,	\
					void *rxbuf,			\
					unsigned int index) {		\
	type buf = func(rspi->base + RSPI_SPDR);			\
	((type *)rxbuf)[index] = buf;					\
}

RZV2H_RSPI_TX(writel, u32)
RZV2H_RSPI_TX(writew, u16)
RZV2H_RSPI_TX(writeb, u8)
RZV2H_RSPI_RX(readl, u32)
RZV2H_RSPI_RX(readw, u16)
RZV2H_RSPI_RX(readl, u8)

static void rzv2h_rspi_reg_rmw(const struct rzv2h_rspi_priv *rspi,
				int reg_offs, u32 bit_mask, u32 value)
{
	u32 tmp;

	value <<= __ffs(bit_mask);
	tmp = (readl(rspi->base + reg_offs) & ~bit_mask) | value;
	writel(tmp, rspi->base + reg_offs);
}

static inline void rzv2h_rspi_spe_disable(const struct rzv2h_rspi_priv *rspi)
{
	rzv2h_rspi_reg_rmw(rspi, RSPI_SPCR, RSPI_SPCR_SPE, 0);
}

static inline void rzv2h_rspi_spe_enable(const struct rzv2h_rspi_priv *rspi)
{
	rzv2h_rspi_reg_rmw(rspi, RSPI_SPCR, RSPI_SPCR_SPE, 1);
}

static inline void rzv2h_rspi_clear_fifos(const struct rzv2h_rspi_priv *rspi)
{
	writeb(1, rspi->base + RSPI_SPFCR);
}

static inline void rzv2h_rspi_clear_all_irqs(struct rzv2h_rspi_priv *rspi)
{
	writew(RSPI_SPSRC_CLEAR, rspi->base + RSPI_SPSRC);
	rspi->status = 0;
}

static irqreturn_t rzv2h_rx_irq_handler(int irq, void *data)
{
	struct rzv2h_rspi_priv *rspi = data;

	rspi->status = readw(rspi->base + RSPI_SPSR);
	wake_up(&rspi->wait);

	return IRQ_HANDLED;
}

static inline int rzv2h_rspi_wait_for_interrupt(struct rzv2h_rspi_priv *rspi,
						u32 wait_mask)
{
	return wait_event_timeout(rspi->wait, (rspi->status & wait_mask),
				  HZ) == 0 ? -ETIMEDOUT : 0;
}

static void rzv2h_rspi_send(struct rzv2h_rspi_priv *rspi, const void *txbuf,
			    unsigned int index)
{
	switch (rspi->bytes_per_word) {
	case 4:
		rzv2h_rspi_tx_u32(rspi, txbuf, index);
		break;
	case 2:
		rzv2h_rspi_tx_u16(rspi, txbuf, index);
		break;
	default:
		rzv2h_rspi_tx_u8(rspi, txbuf, index);
	}
}

static int rzv2h_rspi_receive(struct rzv2h_rspi_priv *rspi, void *rxbuf,
			      unsigned int index)
{
	int ret;

	ret = rzv2h_rspi_wait_for_interrupt(rspi, RSPI_SPSR_SPRF);
	if (ret)
		return ret;

	switch (rspi->bytes_per_word) {
	case 4:
		rzv2h_rspi_rx_u32(rspi, rxbuf, index);
		break;
	case 2:
		rzv2h_rspi_rx_u16(rspi, rxbuf, index);
		break;
	default:
		rzv2h_rspi_rx_u8(rspi, rxbuf, index);
	}

	return 0;
}

static bool rzv2h_rspi_can_dma(struct spi_controller *ctlr, struct spi_device *spi,
			       struct spi_transfer *xfer)
{
	struct rzv2h_rspi_priv *rspi = spi_controller_get_devdata(ctlr);

	if (ctlr->fallback)
		return false;

	if (!ctlr->dma_tx || !ctlr->dma_rx)
		return false;

	return xfer->len > rspi->info->fifo_size;
}

static int rzv2h_rspi_transfer_pio(struct rzv2h_rspi_priv *rspi,
				   struct spi_device *spi,
				   struct spi_transfer *transfer,
				   unsigned int words_to_transfer)
{
	unsigned int i;
	int ret = 0;

	for (i = 0; i < words_to_transfer; i++) {
		rzv2h_rspi_clear_all_irqs(rspi);

		rzv2h_rspi_send(rspi, transfer->tx_buf, i);

		ret = rzv2h_rspi_receive(rspi, transfer->rx_buf, i);
		if (ret)
			break;
	}

	return ret;
}

static void rzv2h_rspi_dma_complete(void *arg)
{
	struct rzv2h_rspi_priv *rspi = arg;

	rspi->dma_callbacked = 1;
	wake_up_interruptible(&rspi->wait);
}

static struct dma_async_tx_descriptor *
rzv2h_rspi_setup_dma_channel(struct rzv2h_rspi_priv *rspi,
			     struct dma_chan *chan, struct sg_table *sg,
			     enum dma_slave_buswidth width,
			     enum dma_transfer_direction direction)
{
	struct dma_slave_config config = {
		.dst_addr = rspi->pdev->resource->start + RSPI_SPDR,
		.src_addr = rspi->pdev->resource->start + RSPI_SPDR,
		.dst_addr_width = width,
		.src_addr_width = width,
		.direction = direction,
	};
	struct dma_async_tx_descriptor *desc;
	int ret;

	ret = dmaengine_slave_config(chan, &config);
	if (ret)
		return ERR_PTR(ret);

	desc = dmaengine_prep_slave_sg(chan, sg->sgl, sg->nents, direction,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		return ERR_PTR(-EAGAIN);

	if (direction == DMA_DEV_TO_MEM) {
		desc->callback = rzv2h_rspi_dma_complete;
		desc->callback_param = rspi;
	}

	return desc;
}

static enum dma_slave_buswidth
rzv2h_rspi_dma_width(struct rzv2h_rspi_priv *rspi)
{
	switch (rspi->bytes_per_word) {
	case 4:
		return DMA_SLAVE_BUSWIDTH_4_BYTES;
	case 2:
		return DMA_SLAVE_BUSWIDTH_2_BYTES;
	case 1:
		return DMA_SLAVE_BUSWIDTH_1_BYTE;
	default:
		return DMA_SLAVE_BUSWIDTH_UNDEFINED;
	}
}

static int rzv2h_rspi_transfer_dma(struct rzv2h_rspi_priv *rspi,
				   struct spi_device *spi,
				   struct spi_transfer *transfer,
				   unsigned int words_to_transfer)
{
	struct dma_async_tx_descriptor *tx_desc = NULL, *rx_desc = NULL;
	enum dma_slave_buswidth width;
	dma_cookie_t cookie;
	int ret;

	width = rzv2h_rspi_dma_width(rspi);
	if (width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
		return -EINVAL;

	rx_desc = rzv2h_rspi_setup_dma_channel(rspi, rspi->controller->dma_rx,
					       &transfer->rx_sg, width,
					       DMA_DEV_TO_MEM);
	if (IS_ERR(rx_desc))
		return PTR_ERR(rx_desc);

	tx_desc = rzv2h_rspi_setup_dma_channel(rspi, rspi->controller->dma_tx,
					       &transfer->tx_sg, width,
					       DMA_MEM_TO_DEV);
	if (IS_ERR(tx_desc))
		return PTR_ERR(tx_desc);

	cookie = dmaengine_submit(rx_desc);
	if (dma_submit_error(cookie))
		return cookie;

	cookie = dmaengine_submit(tx_desc);
	if (dma_submit_error(cookie)) {
		dmaengine_terminate_sync(rspi->controller->dma_rx);
		return cookie;
	}

	/*
	 * DMA transfer does not need IRQs to be enabled.
	 * For PIO, we only use RX IRQ, so disable that.
	 */
	disable_irq(rspi->irq_rx);

	rspi->dma_callbacked = 0;

	dma_async_issue_pending(rspi->controller->dma_rx);
	dma_async_issue_pending(rspi->controller->dma_tx);
	rzv2h_rspi_clear_all_irqs(rspi);

	ret = wait_event_interruptible_timeout(rspi->wait, rspi->dma_callbacked, HZ);
	if (ret) {
		dmaengine_synchronize(rspi->controller->dma_tx);
		dmaengine_synchronize(rspi->controller->dma_rx);
		ret = 0;
	} else {
		dmaengine_terminate_sync(rspi->controller->dma_tx);
		dmaengine_terminate_sync(rspi->controller->dma_rx);
		ret = -ETIMEDOUT;
	}

	enable_irq(rspi->irq_rx);

	return ret;
}

static int rzv2h_rspi_transfer_one(struct spi_controller *controller,
				   struct spi_device *spi,
				   struct spi_transfer *transfer)
{
	struct rzv2h_rspi_priv *rspi = spi_controller_get_devdata(controller);
	bool is_dma = spi_xfer_is_dma_mapped(controller, spi, transfer);
	unsigned int words_to_transfer;
	int ret;

	transfer->effective_speed_hz = rspi->freq;
	words_to_transfer = transfer->len / rspi->bytes_per_word;

	if (is_dma)
		ret = rzv2h_rspi_transfer_dma(rspi, spi, transfer, words_to_transfer);
	else
		ret = rzv2h_rspi_transfer_pio(rspi, spi, transfer, words_to_transfer);

	rzv2h_rspi_clear_all_irqs(rspi);

	if (is_dma && ret == -EAGAIN)
		/* Retry with PIO */
		transfer->error = SPI_TRANS_FAIL_NO_START;

	return ret;
}

static inline u32 rzv2h_rspi_calc_bitrate(unsigned long tclk_rate, u8 spr,
					  u8 brdv)
{
	return DIV_ROUND_UP(tclk_rate, (2 * (spr + 1) * (1 << brdv)));
}

static void rzv2h_rspi_find_rate_variable(struct clk *clk, u32 hz,
					  u8 spr_min, u8 spr_max,
					  struct rzv2h_rspi_best_clock *best)
{
	long clk_rate, clk_min_rate, clk_max_rate;
	int min_rate_spr, max_rate_spr;
	unsigned long error;
	u32 actual_hz;
	u8 brdv;
	int spr;

	/*
	 * On T2H / N2H, the source for the SPI clock is PCLKSPIn, which is a
	 * 1/32, 1/30, 1/25 or 1/24 divider of PLL4, which is 2400MHz,
	 * resulting in either 75MHz, 80MHz, 96MHz or 100MHz.
	 */
	clk_min_rate = clk_round_rate(clk, 0);
	if (clk_min_rate < 0)
		return;

	clk_max_rate = clk_round_rate(clk, ULONG_MAX);
	if (clk_max_rate < 0)
		return;

	/*
	 * From the manual:
	 * Bit rate = f(PCLKSPIn) / (2 * (n + 1) * 2^N)
	 *
	 * If we adapt it to the current context, we get the following:
	 * hz = rate / ((spr + 1) * (1 << (brdv + 1)))
	 *
	 * This can be written in multiple forms depending on what we want to
	 * determine.
	 *
	 * To find the rate, having hz, spr and brdv:
	 * rate = hz * (spr + 1) * (1 << (brdv + 1)
	 *
	 * To find the spr, having rate, hz, and spr:
	 * spr = rate / (hz * (1 << (brdv + 1)) - 1
	 */

	for (brdv = RSPI_SPCMD_BRDV_MIN; brdv <= RSPI_SPCMD_BRDV_MAX; brdv++) {
		/* Calculate the divisor needed to find the SPR from a rate. */
		u32 rate_div = hz * (1 << (brdv + 1));

		/*
		 * If the SPR for the minimum rate is greater than the maximum
		 * allowed value skip this BRDV. The divisor increases with each
		 * BRDV iteration, so the following BRDV might result in a
		 * minimum SPR that is in the valid range.
		 */
		min_rate_spr = DIV_ROUND_CLOSEST(clk_min_rate, rate_div) - 1;
		if (min_rate_spr > spr_max)
			continue;

		/*
		 * If the SPR for the maximum rate is less than the minimum
		 * allowed value, exit. The divisor only increases with each
		 * BRDV iteration, so the following BRDV cannot result in a
		 * maximum SPR that is in the valid range.
		 */
		max_rate_spr = DIV_ROUND_CLOSEST(clk_max_rate, rate_div) - 1;
		if (max_rate_spr < spr_min)
			break;

		if (min_rate_spr < spr_min)
			min_rate_spr = spr_min;

		if (max_rate_spr > spr_max)
			max_rate_spr = spr_max;

		for (spr = min_rate_spr; spr <= max_rate_spr; spr++) {
			clk_rate = (spr + 1) * rate_div;

			clk_rate = clk_round_rate(clk, clk_rate);
			if (clk_rate <= 0)
				continue;

			actual_hz = rzv2h_rspi_calc_bitrate(clk_rate, spr, brdv);
			error = abs((long)hz - (long)actual_hz);

			if (error >= best->error)
				continue;

			*best = (struct rzv2h_rspi_best_clock) {
				.clk = clk,
				.clk_rate = clk_rate,
				.error = error,
				.actual_hz = actual_hz,
				.brdv = brdv,
				.spr = spr,
			};

			if (!error)
				return;
		}
	}
}

static void rzv2h_rspi_find_rate_fixed(struct clk *clk, u32 hz,
				       u8 spr_min, u8 spr_max,
				       struct rzv2h_rspi_best_clock *best)
{
	unsigned long clk_rate;
	unsigned long error;
	u32 actual_hz;
	int spr;
	u8 brdv;

	/*
	 * From the manual:
	 * Bit rate = f(RSPI_n_TCLK)/(2*(n+1)*2^(N))
	 *
	 * Where:
	 * * RSPI_n_TCLK is fixed to 200MHz on V2H
	 * * n = SPR - is RSPI_SPBR.SPR (from 0 to 255)
	 * * N = BRDV - is RSPI_SPCMD.BRDV (from 0 to 3)
	 */
	clk_rate = clk_get_rate(clk);
	for (brdv = RSPI_SPCMD_BRDV_MIN; brdv <= RSPI_SPCMD_BRDV_MAX; brdv++) {
		spr = DIV_ROUND_UP(clk_rate, hz * (1 << (brdv + 1)));
		spr--;
		if (spr >= spr_min && spr <= spr_max)
			goto clock_found;
	}

	return;

clock_found:
	actual_hz = rzv2h_rspi_calc_bitrate(clk_rate, spr, brdv);
	error = abs((long)hz - (long)actual_hz);

	if (error >= best->error)
		return;

	*best = (struct rzv2h_rspi_best_clock) {
		.clk = clk,
		.clk_rate = clk_rate,
		.error = error,
		.actual_hz = actual_hz,
		.brdv = brdv,
		.spr = spr,
	};
}

static u32 rzv2h_rspi_setup_clock(struct rzv2h_rspi_priv *rspi, u32 hz)
{
	struct rzv2h_rspi_best_clock best_clock = {
		.error = ULONG_MAX,
	};
	int ret;

	rspi->info->find_tclk_rate(rspi->tclk, hz, RSPI_SPBR_SPR_MIN,
				   RSPI_SPBR_SPR_MAX, &best_clock);

	/*
	 * T2H and N2H can also use PCLK as a source, which is 125MHz, but not
	 * when both SPR and BRDV are 0.
	 */
	if (best_clock.error && rspi->info->find_pclk_rate)
		rspi->info->find_pclk_rate(rspi->pclk, hz, RSPI_SPBR_SPR_PCLK_MIN,
					   RSPI_SPBR_SPR_MAX, &best_clock);

	if (!best_clock.clk_rate)
		return -EINVAL;

	ret = clk_set_rate(best_clock.clk, best_clock.clk_rate);
	if (ret)
		return 0;

	rspi->use_pclk = best_clock.clk == rspi->pclk;
	rspi->spr = best_clock.spr;
	rspi->brdv = best_clock.brdv;

	return best_clock.actual_hz;
}

static int rzv2h_rspi_prepare_message(struct spi_controller *ctlr,
				      struct spi_message *message)
{
	struct rzv2h_rspi_priv *rspi = spi_controller_get_devdata(ctlr);
	const struct spi_device *spi = message->spi;
	struct spi_transfer *xfer;
	u32 speed_hz = U32_MAX;
	u8 bits_per_word;
	u32 conf32;
	u16 conf16;
	u8 conf8;

	/* Make sure SPCR.SPE is 0 before amending the configuration */
	rzv2h_rspi_spe_disable(rspi);

	list_for_each_entry(xfer, &message->transfers, transfer_list) {
		if (!xfer->speed_hz)
			continue;

		speed_hz = min(xfer->speed_hz, speed_hz);
		bits_per_word = xfer->bits_per_word;
	}

	if (speed_hz == U32_MAX)
		return -EINVAL;

	rspi->bytes_per_word = roundup_pow_of_two(BITS_TO_BYTES(bits_per_word));

	if (speed_hz != rspi->last_speed_hz) {
		rspi->freq = rzv2h_rspi_setup_clock(rspi, speed_hz);
		if (!rspi->freq)
			return -EINVAL;

		rspi->last_speed_hz = speed_hz;
	}

	writeb(rspi->spr, rspi->base + RSPI_SPBR);

	/* Configure the device to work in "host" mode */
	conf32 = RSPI_SPCR_MSTR;

	/* Auto-stop function */
	conf32 |= RSPI_SPCR_SCKASE;

	/* SPI receive buffer full interrupt enable */
	conf32 |= RSPI_SPCR_SPRIE;

	/* SPI transmit buffer empty interrupt enable */
	conf32 |= RSPI_SPCR_SPTIE;

	/* Bypass synchronization circuit */
	conf32 |= FIELD_PREP(RSPI_SPCR_BPEN, rspi->use_pclk);

	writel(conf32, rspi->base + RSPI_SPCR);

	/* Use SPCMD0 only */
	writeb(0x0, rspi->base + RSPI_SPSCR);

	/* Setup loopback */
	conf8 = FIELD_PREP(RSPI_SPPCR_SPLP2, !!(spi->mode & SPI_LOOP));
	writeb(conf8, rspi->base + RSPI_SPPCR);

	/* Setup mode */
	conf32 = FIELD_PREP(RSPI_SPCMD_CPOL, !!(spi->mode & SPI_CPOL));
	conf32 |= FIELD_PREP(RSPI_SPCMD_CPHA, !!(spi->mode & SPI_CPHA));
	conf32 |= FIELD_PREP(RSPI_SPCMD_LSBF, !!(spi->mode & SPI_LSB_FIRST));
	conf32 |= FIELD_PREP(RSPI_SPCMD_SPB, bits_per_word - 1);
	conf32 |= FIELD_PREP(RSPI_SPCMD_BRDV, rspi->brdv);
	conf32 |= FIELD_PREP(RSPI_SPCMD_SSLKP, 1);
	conf32 |= FIELD_PREP(RSPI_SPCMD_SSLA, spi_get_chipselect(spi, 0));
	writel(conf32, rspi->base + RSPI_SPCMD);
	if (spi->mode & SPI_CS_HIGH)
		writeb(BIT(spi_get_chipselect(spi, 0)), rspi->base + RSPI_SSLP);
	else
		writeb(0, rspi->base + RSPI_SSLP);

	/* Setup FIFO thresholds */
	conf16 = FIELD_PREP(RSPI_SPDCR2_TTRG, 0);
	conf16 |= FIELD_PREP(RSPI_SPDCR2_RTRG, 0);
	writew(conf16, rspi->base + RSPI_SPDCR2);

	rzv2h_rspi_clear_fifos(rspi);

	rzv2h_rspi_spe_enable(rspi);

	return 0;
}

static int rzv2h_rspi_unprepare_message(struct spi_controller *ctlr,
					struct spi_message *message)
{
	struct rzv2h_rspi_priv *rspi = spi_controller_get_devdata(ctlr);

	rzv2h_rspi_spe_disable(rspi);

	return 0;
}

static int rzv2h_rspi_probe(struct platform_device *pdev)
{
	struct spi_controller *controller;
	struct device *dev = &pdev->dev;
	struct rzv2h_rspi_priv *rspi;
	struct reset_control *reset;
	struct clk_bulk_data *clks;
	long tclk_rate;
	int ret, i;

	controller = devm_spi_alloc_host(dev, sizeof(*rspi));
	if (!controller)
		return -ENOMEM;

	rspi = spi_controller_get_devdata(controller);
	platform_set_drvdata(pdev, rspi);

	rspi->controller = controller;
	rspi->pdev = pdev;

	rspi->info = device_get_match_data(dev);

	rspi->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rspi->base))
		return PTR_ERR(rspi->base);

	ret = devm_clk_bulk_get_all_enabled(dev, &clks);
	if (ret != rspi->info->num_clks)
		return dev_err_probe(dev, ret >= 0 ? -EINVAL : ret,
				     "cannot get clocks\n");
	for (i = 0; i < rspi->info->num_clks; i++) {
		if (!strcmp(clks[i].id, rspi->info->tclk_name)) {
			rspi->tclk = clks[i].clk;
		} else if (rspi->info->find_pclk_rate &&
			   !strcmp(clks[i].id, "pclk")) {
			rspi->pclk = clks[i].clk;
		}
	}

	if (!rspi->tclk)
		return dev_err_probe(dev, -EINVAL, "Failed to get tclk\n");

	reset = devm_reset_control_get_optional_exclusive_deasserted(&pdev->dev,
								     "presetn");
	if (IS_ERR(reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(reset),
				     "cannot get presetn reset\n");

	reset = devm_reset_control_get_optional_exclusive_deasserted(&pdev->dev,
								     "tresetn");
	if (IS_ERR(reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(reset),
				     "cannot get tresetn reset\n");

	rspi->irq_rx = platform_get_irq_byname(pdev, "rx");
	if (rspi->irq_rx < 0)
		return dev_err_probe(dev, rspi->irq_rx, "cannot get IRQ 'rx'\n");

	init_waitqueue_head(&rspi->wait);

	ret = devm_request_irq(dev, rspi->irq_rx, rzv2h_rx_irq_handler, 0,
			       dev_name(dev), rspi);
	if (ret) {
		dev_err(dev, "cannot request `rx` IRQ\n");
		return ret;
	}

	controller->mode_bits = SPI_CPHA | SPI_CPOL | SPI_CS_HIGH |
				SPI_LSB_FIRST | SPI_LOOP;
	controller->flags = SPI_CONTROLLER_MUST_RX | SPI_CONTROLLER_MUST_TX;
	controller->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
	controller->prepare_message = rzv2h_rspi_prepare_message;
	controller->unprepare_message = rzv2h_rspi_unprepare_message;
	controller->num_chipselect = 4;
	controller->transfer_one = rzv2h_rspi_transfer_one;
	controller->can_dma = rzv2h_rspi_can_dma;

	tclk_rate = clk_round_rate(rspi->tclk, 0);
	if (tclk_rate < 0)
		return tclk_rate;

	controller->min_speed_hz = rzv2h_rspi_calc_bitrate(tclk_rate,
							   RSPI_SPBR_SPR_MAX,
							   RSPI_SPCMD_BRDV_MAX);

	tclk_rate = clk_round_rate(rspi->tclk, ULONG_MAX);
	if (tclk_rate < 0)
		return tclk_rate;

	controller->max_speed_hz = rzv2h_rspi_calc_bitrate(tclk_rate,
							   RSPI_SPBR_SPR_MIN,
							   RSPI_SPCMD_BRDV_MIN);

	controller->dma_tx = devm_dma_request_chan(dev, "tx");
	if (IS_ERR(controller->dma_tx)) {
		ret = dev_warn_probe(dev, PTR_ERR(controller->dma_tx),
				     "failed to request TX DMA channel\n");
		if (ret == -EPROBE_DEFER)
			return ret;
		controller->dma_tx = NULL;
	}

	controller->dma_rx = devm_dma_request_chan(dev, "rx");
	if (IS_ERR(controller->dma_rx)) {
		ret = dev_warn_probe(dev, PTR_ERR(controller->dma_rx),
				     "failed to request RX DMA channel\n");
		if (ret == -EPROBE_DEFER)
			return ret;
		controller->dma_rx = NULL;
	}

	ret = devm_spi_register_controller(dev, controller);
	if (ret)
		dev_err(dev, "register controller failed\n");

	return ret;
}

static const struct rzv2h_rspi_info rzv2h_info = {
	.find_tclk_rate = rzv2h_rspi_find_rate_fixed,
	.tclk_name = "tclk",
	.fifo_size = 16,
	.num_clks = 3,
};

static const struct rzv2h_rspi_info rzt2h_info = {
	.find_tclk_rate = rzv2h_rspi_find_rate_variable,
	.find_pclk_rate = rzv2h_rspi_find_rate_fixed,
	.tclk_name = "pclkspi",
	.fifo_size = 4,
	.num_clks = 2,
};

static const struct of_device_id rzv2h_rspi_match[] = {
	{ .compatible = "renesas,r9a09g057-rspi", &rzv2h_info },
	{ .compatible = "renesas,r9a09g077-rspi", &rzt2h_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzv2h_rspi_match);

static struct platform_driver rzv2h_rspi_drv = {
	.probe = rzv2h_rspi_probe,
	.driver = {
		.name = "rzv2h_rspi",
		.of_match_table = rzv2h_rspi_match,
	},
};
module_platform_driver(rzv2h_rspi_drv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fabrizio Castro <fabrizio.castro.jz@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/V2H(P) Serial Peripheral Interface Driver");
