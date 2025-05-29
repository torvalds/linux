/*
 * Driver for Amlogic Meson SPI communication controller (SPICC)
 *
 * Copyright (C) BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/reset.h>
#include <linux/pinctrl/consumer.h>
#include <linux/dma-mapping.h>

/*
 * There are two modes for data transmission: PIO and DMA.
 * When bits_per_word is 8, 16, 24, or 32, data is transferred using PIO mode.
 * When bits_per_word is 64, DMA mode is used by default.
 *
 * DMA achieves a transfer with one or more SPI bursts, each SPI burst is made
 * up of one or more DMA bursts. The DMA burst implementation mechanism is,
 * For TX, when the number of words in TXFIFO is less than the preset
 * reading threshold, SPICC starts a reading DMA burst, which reads the preset
 * number of words from TX buffer, then writes them into TXFIFO.
 * For RX, when the number of words in RXFIFO is greater than the preset
 * writing threshold, SPICC starts a writing request burst, which reads the
 * preset number of words from RXFIFO, then write them into RX buffer.
 * DMA works if the transfer meets the following conditions,
 * - 64 bits per word
 * - The transfer length in word must be multiples of the dma_burst_len, and
 *   the dma_burst_len should be one of 8,7...2, otherwise, it will be split
 *   into several SPI bursts by this driver
 */

#define SPICC_MAX_BURST	128

/* Register Map */
#define SPICC_RXDATA	0x00

#define SPICC_TXDATA	0x04

#define SPICC_CONREG	0x08
#define SPICC_ENABLE		BIT(0)
#define SPICC_MODE_MASTER	BIT(1)
#define SPICC_XCH		BIT(2)
#define SPICC_SMC		BIT(3)
#define SPICC_POL		BIT(4)
#define SPICC_PHA		BIT(5)
#define SPICC_SSCTL		BIT(6)
#define SPICC_SSPOL		BIT(7)
#define SPICC_DRCTL_MASK	GENMASK(9, 8)
#define SPICC_DRCTL_IGNORE	0
#define SPICC_DRCTL_FALLING	1
#define SPICC_DRCTL_LOWLEVEL	2
#define SPICC_CS_MASK		GENMASK(13, 12)
#define SPICC_DATARATE_MASK	GENMASK(18, 16)
#define SPICC_DATARATE_DIV4	0
#define SPICC_DATARATE_DIV8	1
#define SPICC_DATARATE_DIV16	2
#define SPICC_DATARATE_DIV32	3
#define SPICC_BITLENGTH_MASK	GENMASK(24, 19)
#define SPICC_BURSTLENGTH_MASK	GENMASK(31, 25)

#define SPICC_INTREG	0x0c
#define SPICC_TE_EN	BIT(0) /* TX FIFO Empty Interrupt */
#define SPICC_TH_EN	BIT(1) /* TX FIFO Half-Full Interrupt */
#define SPICC_TF_EN	BIT(2) /* TX FIFO Full Interrupt */
#define SPICC_RR_EN	BIT(3) /* RX FIFO Ready Interrupt */
#define SPICC_RH_EN	BIT(4) /* RX FIFO Half-Full Interrupt */
#define SPICC_RF_EN	BIT(5) /* RX FIFO Full Interrupt */
#define SPICC_RO_EN	BIT(6) /* RX FIFO Overflow Interrupt */
#define SPICC_TC_EN	BIT(7) /* Transfert Complete Interrupt */

#define SPICC_DMAREG	0x10
#define SPICC_DMA_ENABLE		BIT(0)
#define SPICC_TXFIFO_THRESHOLD_MASK	GENMASK(5, 1)
#define SPICC_RXFIFO_THRESHOLD_MASK	GENMASK(10, 6)
#define SPICC_READ_BURST_MASK		GENMASK(14, 11)
#define SPICC_WRITE_BURST_MASK		GENMASK(18, 15)
#define SPICC_DMA_URGENT		BIT(19)
#define SPICC_DMA_THREADID_MASK		GENMASK(25, 20)
#define SPICC_DMA_BURSTNUM_MASK		GENMASK(31, 26)

#define SPICC_STATREG	0x14
#define SPICC_TE	BIT(0) /* TX FIFO Empty Interrupt */
#define SPICC_TH	BIT(1) /* TX FIFO Half-Full Interrupt */
#define SPICC_TF	BIT(2) /* TX FIFO Full Interrupt */
#define SPICC_RR	BIT(3) /* RX FIFO Ready Interrupt */
#define SPICC_RH	BIT(4) /* RX FIFO Half-Full Interrupt */
#define SPICC_RF	BIT(5) /* RX FIFO Full Interrupt */
#define SPICC_RO	BIT(6) /* RX FIFO Overflow Interrupt */
#define SPICC_TC	BIT(7) /* Transfert Complete Interrupt */

#define SPICC_PERIODREG	0x18
#define SPICC_PERIOD	GENMASK(14, 0)	/* Wait cycles */

#define SPICC_TESTREG	0x1c
#define SPICC_TXCNT_MASK	GENMASK(4, 0)	/* TX FIFO Counter */
#define SPICC_RXCNT_MASK	GENMASK(9, 5)	/* RX FIFO Counter */
#define SPICC_SMSTATUS_MASK	GENMASK(12, 10)	/* State Machine Status */
#define SPICC_LBC_RO		BIT(13)	/* Loop Back Control Read-Only */
#define SPICC_LBC_W1		BIT(14) /* Loop Back Control Write-Only */
#define SPICC_SWAP_RO		BIT(14) /* RX FIFO Data Swap Read-Only */
#define SPICC_SWAP_W1		BIT(15) /* RX FIFO Data Swap Write-Only */
#define SPICC_DLYCTL_RO_MASK	GENMASK(20, 15) /* Delay Control Read-Only */
#define SPICC_MO_DELAY_MASK	GENMASK(17, 16) /* Master Output Delay */
#define SPICC_MO_NO_DELAY	0
#define SPICC_MO_DELAY_1_CYCLE	1
#define SPICC_MO_DELAY_2_CYCLE	2
#define SPICC_MO_DELAY_3_CYCLE	3
#define SPICC_MI_DELAY_MASK	GENMASK(19, 18) /* Master Input Delay */
#define SPICC_MI_NO_DELAY	0
#define SPICC_MI_DELAY_1_CYCLE	1
#define SPICC_MI_DELAY_2_CYCLE	2
#define SPICC_MI_DELAY_3_CYCLE	3
#define SPICC_MI_CAP_DELAY_MASK	GENMASK(21, 20) /* Master Capture Delay */
#define SPICC_CAP_AHEAD_2_CYCLE	0
#define SPICC_CAP_AHEAD_1_CYCLE	1
#define SPICC_CAP_NO_DELAY	2
#define SPICC_CAP_DELAY_1_CYCLE	3
#define SPICC_FIFORST_RO_MASK	GENMASK(22, 21) /* FIFO Softreset Read-Only */
#define SPICC_FIFORST_W1_MASK	GENMASK(23, 22) /* FIFO Softreset Write-Only */

#define SPICC_DRADDR	0x20	/* Read Address of DMA */

#define SPICC_DWADDR	0x24	/* Write Address of DMA */

#define SPICC_LD_CNTL0	0x28
#define VSYNC_IRQ_SRC_SELECT		BIT(0)
#define DMA_EN_SET_BY_VSYNC		BIT(2)
#define XCH_EN_SET_BY_VSYNC		BIT(3)
#define DMA_READ_COUNTER_EN		BIT(4)
#define DMA_WRITE_COUNTER_EN		BIT(5)
#define DMA_RADDR_LOAD_BY_VSYNC		BIT(6)
#define DMA_WADDR_LOAD_BY_VSYNC		BIT(7)
#define DMA_ADDR_LOAD_FROM_LD_ADDR	BIT(8)

#define SPICC_LD_CNTL1	0x2c
#define DMA_READ_COUNTER		GENMASK(15, 0)
#define DMA_WRITE_COUNTER		GENMASK(31, 16)
#define DMA_BURST_LEN_DEFAULT		8
#define DMA_BURST_COUNT_MAX		0xffff
#define SPI_BURST_LEN_MAX	(DMA_BURST_LEN_DEFAULT * DMA_BURST_COUNT_MAX)

#define SPICC_ENH_CTL0	0x38	/* Enhanced Feature */
#define SPICC_ENH_CLK_CS_DELAY_MASK	GENMASK(15, 0)
#define SPICC_ENH_DATARATE_MASK		GENMASK(23, 16)
#define SPICC_ENH_DATARATE_EN		BIT(24)
#define SPICC_ENH_MOSI_OEN		BIT(25)
#define SPICC_ENH_CLK_OEN		BIT(26)
#define SPICC_ENH_CS_OEN		BIT(27)
#define SPICC_ENH_CLK_CS_DELAY_EN	BIT(28)
#define SPICC_ENH_MAIN_CLK_AO		BIT(29)

#define writel_bits_relaxed(mask, val, addr) \
	writel_relaxed((readl_relaxed(addr) & ~(mask)) | (val), addr)

struct meson_spicc_data {
	unsigned int			max_speed_hz;
	unsigned int			min_speed_hz;
	unsigned int			fifo_size;
	bool				has_oen;
	bool				has_enhance_clk_div;
	bool				has_pclk;
};

struct meson_spicc_device {
	struct spi_controller		*host;
	struct platform_device		*pdev;
	void __iomem			*base;
	struct clk			*core;
	struct clk			*pclk;
	struct clk_divider		pow2_div;
	struct clk			*clk;
	struct spi_message		*message;
	struct spi_transfer		*xfer;
	struct completion		done;
	const struct meson_spicc_data	*data;
	u8				*tx_buf;
	u8				*rx_buf;
	unsigned int			bytes_per_word;
	unsigned long			tx_remain;
	unsigned long			rx_remain;
	unsigned long			xfer_remain;
	struct pinctrl			*pinctrl;
	struct pinctrl_state		*pins_idle_high;
	struct pinctrl_state		*pins_idle_low;
	dma_addr_t			tx_dma;
	dma_addr_t			rx_dma;
	bool				using_dma;
};

#define pow2_clk_to_spicc(_div) container_of(_div, struct meson_spicc_device, pow2_div)

static void meson_spicc_oen_enable(struct meson_spicc_device *spicc)
{
	u32 conf;

	if (!spicc->data->has_oen) {
		/* Try to get pinctrl states for idle high/low */
		spicc->pins_idle_high = pinctrl_lookup_state(spicc->pinctrl,
							     "idle-high");
		if (IS_ERR(spicc->pins_idle_high)) {
			dev_warn(&spicc->pdev->dev, "can't get idle-high pinctrl\n");
			spicc->pins_idle_high = NULL;
		}
		spicc->pins_idle_low = pinctrl_lookup_state(spicc->pinctrl,
							     "idle-low");
		if (IS_ERR(spicc->pins_idle_low)) {
			dev_warn(&spicc->pdev->dev, "can't get idle-low pinctrl\n");
			spicc->pins_idle_low = NULL;
		}
		return;
	}

	conf = readl_relaxed(spicc->base + SPICC_ENH_CTL0) |
		SPICC_ENH_MOSI_OEN | SPICC_ENH_CLK_OEN | SPICC_ENH_CS_OEN;

	writel_relaxed(conf, spicc->base + SPICC_ENH_CTL0);
}

static int meson_spicc_dma_map(struct meson_spicc_device *spicc,
			       struct spi_transfer *t)
{
	struct device *dev = spicc->host->dev.parent;

	if (!(t->tx_buf && t->rx_buf))
		return -EINVAL;

	t->tx_dma = dma_map_single(dev, (void *)t->tx_buf, t->len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, t->tx_dma))
		return -ENOMEM;

	t->rx_dma = dma_map_single(dev, t->rx_buf, t->len, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, t->rx_dma))
		return -ENOMEM;

	spicc->tx_dma = t->tx_dma;
	spicc->rx_dma = t->rx_dma;

	return 0;
}

static void meson_spicc_dma_unmap(struct meson_spicc_device *spicc,
				  struct spi_transfer *t)
{
	struct device *dev = spicc->host->dev.parent;

	if (t->tx_dma)
		dma_unmap_single(dev, t->tx_dma, t->len, DMA_TO_DEVICE);
	if (t->rx_dma)
		dma_unmap_single(dev, t->rx_dma, t->len, DMA_FROM_DEVICE);
}

/*
 * According to the remain words length, calculate a suitable spi burst length
 * and a dma burst length for current spi burst
 */
static u32 meson_spicc_calc_dma_len(struct meson_spicc_device *spicc,
				    u32 len, u32 *dma_burst_len)
{
	u32 i;

	if (len <= spicc->data->fifo_size) {
		*dma_burst_len = len;
		return len;
	}

	*dma_burst_len = DMA_BURST_LEN_DEFAULT;

	if (len == (SPI_BURST_LEN_MAX + 1))
		return SPI_BURST_LEN_MAX - DMA_BURST_LEN_DEFAULT;

	if (len >= SPI_BURST_LEN_MAX)
		return SPI_BURST_LEN_MAX;

	for (i = DMA_BURST_LEN_DEFAULT; i > 1; i--)
		if ((len % i) == 0) {
			*dma_burst_len = i;
			return len;
		}

	i = len % DMA_BURST_LEN_DEFAULT;
	len -= i;

	if (i == 1)
		len -= DMA_BURST_LEN_DEFAULT;

	return len;
}

static void meson_spicc_setup_dma(struct meson_spicc_device *spicc)
{
	unsigned int len;
	unsigned int dma_burst_len, dma_burst_count;
	unsigned int count_en = 0;
	unsigned int txfifo_thres = 0;
	unsigned int read_req = 0;
	unsigned int rxfifo_thres = 31;
	unsigned int write_req = 0;
	unsigned int ld_ctr1 = 0;

	writel_relaxed(spicc->tx_dma, spicc->base + SPICC_DRADDR);
	writel_relaxed(spicc->rx_dma, spicc->base + SPICC_DWADDR);

	/* Set the max burst length to support a transmission with length of
	 * no more than 1024 bytes(128 words), which must use the CS management
	 * because of some strict timing requirements
	 */
	writel_bits_relaxed(SPICC_BURSTLENGTH_MASK, SPICC_BURSTLENGTH_MASK,
			    spicc->base + SPICC_CONREG);

	len = meson_spicc_calc_dma_len(spicc, spicc->xfer_remain,
				       &dma_burst_len);
	spicc->xfer_remain -= len;
	dma_burst_count = DIV_ROUND_UP(len, dma_burst_len);
	dma_burst_len--;

	if (spicc->tx_dma) {
		spicc->tx_dma += len;
		count_en |= DMA_READ_COUNTER_EN;
		txfifo_thres = spicc->data->fifo_size - dma_burst_len;
		read_req = dma_burst_len;
		ld_ctr1 |= FIELD_PREP(DMA_READ_COUNTER, dma_burst_count);
	}

	if (spicc->rx_dma) {
		spicc->rx_dma += len;
		count_en |= DMA_WRITE_COUNTER_EN;
		rxfifo_thres = dma_burst_len;
		write_req = dma_burst_len;
		ld_ctr1 |= FIELD_PREP(DMA_WRITE_COUNTER, dma_burst_count);
	}

	writel_relaxed(count_en, spicc->base + SPICC_LD_CNTL0);
	writel_relaxed(ld_ctr1, spicc->base + SPICC_LD_CNTL1);
	writel_relaxed(SPICC_DMA_ENABLE
		    | SPICC_DMA_URGENT
		    | FIELD_PREP(SPICC_TXFIFO_THRESHOLD_MASK, txfifo_thres)
		    | FIELD_PREP(SPICC_READ_BURST_MASK, read_req)
		    | FIELD_PREP(SPICC_RXFIFO_THRESHOLD_MASK, rxfifo_thres)
		    | FIELD_PREP(SPICC_WRITE_BURST_MASK, write_req),
		    spicc->base + SPICC_DMAREG);
}

static irqreturn_t meson_spicc_dma_irq(struct meson_spicc_device *spicc)
{
	if (readl_relaxed(spicc->base + SPICC_DMAREG) & SPICC_DMA_ENABLE)
		return IRQ_HANDLED;

	if (spicc->xfer_remain) {
		meson_spicc_setup_dma(spicc);
	} else {
		writel_bits_relaxed(SPICC_SMC, 0, spicc->base + SPICC_CONREG);
		writel_relaxed(0, spicc->base + SPICC_INTREG);
		writel_relaxed(0, spicc->base + SPICC_DMAREG);
		meson_spicc_dma_unmap(spicc, spicc->xfer);
		complete(&spicc->done);
	}

	return IRQ_HANDLED;
}

static inline bool meson_spicc_txfull(struct meson_spicc_device *spicc)
{
	return !!FIELD_GET(SPICC_TF,
			   readl_relaxed(spicc->base + SPICC_STATREG));
}

static inline bool meson_spicc_rxready(struct meson_spicc_device *spicc)
{
	return FIELD_GET(SPICC_RH | SPICC_RR | SPICC_RF,
			 readl_relaxed(spicc->base + SPICC_STATREG));
}

static inline u32 meson_spicc_pull_data(struct meson_spicc_device *spicc)
{
	unsigned int bytes = spicc->bytes_per_word;
	unsigned int byte_shift = 0;
	u32 data = 0;
	u8 byte;

	while (bytes--) {
		byte = *spicc->tx_buf++;
		data |= (byte & 0xff) << byte_shift;
		byte_shift += 8;
	}

	spicc->tx_remain--;
	return data;
}

static inline void meson_spicc_push_data(struct meson_spicc_device *spicc,
					 u32 data)
{
	unsigned int bytes = spicc->bytes_per_word;
	unsigned int byte_shift = 0;
	u8 byte;

	while (bytes--) {
		byte = (data >> byte_shift) & 0xff;
		*spicc->rx_buf++ = byte;
		byte_shift += 8;
	}

	spicc->rx_remain--;
}

static inline void meson_spicc_rx(struct meson_spicc_device *spicc)
{
	/* Empty RX FIFO */
	while (spicc->rx_remain &&
	       meson_spicc_rxready(spicc))
		meson_spicc_push_data(spicc,
				readl_relaxed(spicc->base + SPICC_RXDATA));
}

static inline void meson_spicc_tx(struct meson_spicc_device *spicc)
{
	/* Fill Up TX FIFO */
	while (spicc->tx_remain &&
	       !meson_spicc_txfull(spicc))
		writel_relaxed(meson_spicc_pull_data(spicc),
			       spicc->base + SPICC_TXDATA);
}

static inline void meson_spicc_setup_burst(struct meson_spicc_device *spicc)
{

	unsigned int burst_len = min_t(unsigned int,
				       spicc->xfer_remain /
				       spicc->bytes_per_word,
				       spicc->data->fifo_size);
	/* Setup Xfer variables */
	spicc->tx_remain = burst_len;
	spicc->rx_remain = burst_len;
	spicc->xfer_remain -= burst_len * spicc->bytes_per_word;

	/* Setup burst length */
	writel_bits_relaxed(SPICC_BURSTLENGTH_MASK,
			FIELD_PREP(SPICC_BURSTLENGTH_MASK,
				burst_len - 1),
			spicc->base + SPICC_CONREG);

	/* Fill TX FIFO */
	meson_spicc_tx(spicc);
}

static irqreturn_t meson_spicc_irq(int irq, void *data)
{
	struct meson_spicc_device *spicc = (void *) data;

	writel_bits_relaxed(SPICC_TC, SPICC_TC, spicc->base + SPICC_STATREG);

	if (spicc->using_dma)
		return meson_spicc_dma_irq(spicc);

	/* Empty RX FIFO */
	meson_spicc_rx(spicc);

	if (!spicc->xfer_remain) {
		/* Disable all IRQs */
		writel(0, spicc->base + SPICC_INTREG);

		complete(&spicc->done);

		return IRQ_HANDLED;
	}

	/* Setup burst */
	meson_spicc_setup_burst(spicc);

	/* Start burst */
	writel_bits_relaxed(SPICC_XCH, SPICC_XCH, spicc->base + SPICC_CONREG);

	return IRQ_HANDLED;
}

static void meson_spicc_auto_io_delay(struct meson_spicc_device *spicc)
{
	u32 div, hz;
	u32 mi_delay, cap_delay;
	u32 conf;

	if (spicc->data->has_enhance_clk_div) {
		div = FIELD_GET(SPICC_ENH_DATARATE_MASK,
				readl_relaxed(spicc->base + SPICC_ENH_CTL0));
		div++;
		div <<= 1;
	} else {
		div = FIELD_GET(SPICC_DATARATE_MASK,
				readl_relaxed(spicc->base + SPICC_CONREG));
		div += 2;
		div = 1 << div;
	}

	mi_delay = SPICC_MI_NO_DELAY;
	cap_delay = SPICC_CAP_AHEAD_2_CYCLE;
	hz = clk_get_rate(spicc->clk);

	if (hz >= 100000000)
		cap_delay = SPICC_CAP_DELAY_1_CYCLE;
	else if (hz >= 80000000)
		cap_delay = SPICC_CAP_NO_DELAY;
	else if (hz >= 40000000)
		cap_delay = SPICC_CAP_AHEAD_1_CYCLE;
	else if (div >= 16)
		mi_delay = SPICC_MI_DELAY_3_CYCLE;
	else if (div >= 8)
		mi_delay = SPICC_MI_DELAY_2_CYCLE;
	else if (div >= 6)
		mi_delay = SPICC_MI_DELAY_1_CYCLE;

	conf = readl_relaxed(spicc->base + SPICC_TESTREG);
	conf &= ~(SPICC_MO_DELAY_MASK | SPICC_MI_DELAY_MASK
		  | SPICC_MI_CAP_DELAY_MASK);
	conf |= FIELD_PREP(SPICC_MI_DELAY_MASK, mi_delay);
	conf |= FIELD_PREP(SPICC_MI_CAP_DELAY_MASK, cap_delay);
	writel_relaxed(conf, spicc->base + SPICC_TESTREG);
}

static void meson_spicc_setup_xfer(struct meson_spicc_device *spicc,
				   struct spi_transfer *xfer)
{
	u32 conf, conf_orig;

	/* Read original configuration */
	conf = conf_orig = readl_relaxed(spicc->base + SPICC_CONREG);

	/* Setup word width */
	conf &= ~SPICC_BITLENGTH_MASK;
	conf |= FIELD_PREP(SPICC_BITLENGTH_MASK,
			   (spicc->bytes_per_word << 3) - 1);

	/* Ignore if unchanged */
	if (conf != conf_orig)
		writel_relaxed(conf, spicc->base + SPICC_CONREG);

	clk_set_rate(spicc->clk, xfer->speed_hz);

	meson_spicc_auto_io_delay(spicc);

	writel_relaxed(0, spicc->base + SPICC_DMAREG);
}

static void meson_spicc_reset_fifo(struct meson_spicc_device *spicc)
{
	if (spicc->data->has_oen)
		writel_bits_relaxed(SPICC_ENH_MAIN_CLK_AO,
				    SPICC_ENH_MAIN_CLK_AO,
				    spicc->base + SPICC_ENH_CTL0);

	writel_bits_relaxed(SPICC_FIFORST_W1_MASK, SPICC_FIFORST_W1_MASK,
			    spicc->base + SPICC_TESTREG);

	while (meson_spicc_rxready(spicc))
		readl_relaxed(spicc->base + SPICC_RXDATA);

	if (spicc->data->has_oen)
		writel_bits_relaxed(SPICC_ENH_MAIN_CLK_AO, 0,
				    spicc->base + SPICC_ENH_CTL0);
}

static int meson_spicc_transfer_one(struct spi_controller *host,
				    struct spi_device *spi,
				    struct spi_transfer *xfer)
{
	struct meson_spicc_device *spicc = spi_controller_get_devdata(host);
	uint64_t timeout;

	/* Store current transfer */
	spicc->xfer = xfer;

	/* Setup transfer parameters */
	spicc->tx_buf = (u8 *)xfer->tx_buf;
	spicc->rx_buf = (u8 *)xfer->rx_buf;
	spicc->xfer_remain = xfer->len;

	/* Pre-calculate word size */
	spicc->bytes_per_word =
	   DIV_ROUND_UP(spicc->xfer->bits_per_word, 8);

	if (xfer->len % spicc->bytes_per_word)
		return -EINVAL;

	/* Setup transfer parameters */
	meson_spicc_setup_xfer(spicc, xfer);

	meson_spicc_reset_fifo(spicc);

	/* Setup wait for completion */
	reinit_completion(&spicc->done);

	/* For each byte we wait for 8 cycles of the SPI clock */
	timeout = 8LL * MSEC_PER_SEC * xfer->len;
	do_div(timeout, xfer->speed_hz);

	/* Add 10us delay between each fifo bursts */
	timeout += ((xfer->len >> 4) * 10) / MSEC_PER_SEC;

	/* Increase it twice and add 200 ms tolerance */
	timeout += timeout + 200;

	if (xfer->bits_per_word == 64) {
		int ret;

		/* dma_burst_len 1 can't trigger a dma burst */
		if (xfer->len < 16)
			return -EINVAL;

		ret = meson_spicc_dma_map(spicc, xfer);
		if (ret) {
			meson_spicc_dma_unmap(spicc, xfer);
			dev_err(host->dev.parent, "dma map failed\n");
			return ret;
		}

		spicc->using_dma = true;
		spicc->xfer_remain = DIV_ROUND_UP(xfer->len, spicc->bytes_per_word);
		meson_spicc_setup_dma(spicc);
		writel_relaxed(SPICC_TE_EN, spicc->base + SPICC_INTREG);
		writel_bits_relaxed(SPICC_SMC, SPICC_SMC, spicc->base + SPICC_CONREG);
	} else {
		spicc->using_dma = false;
		/* Setup burst */
		meson_spicc_setup_burst(spicc);

		/* Start burst */
		writel_bits_relaxed(SPICC_XCH, SPICC_XCH, spicc->base + SPICC_CONREG);

		/* Enable interrupts */
		writel_relaxed(SPICC_TC_EN, spicc->base + SPICC_INTREG);
	}

	if (!wait_for_completion_timeout(&spicc->done, msecs_to_jiffies(timeout)))
		return -ETIMEDOUT;

	return 0;
}

static int meson_spicc_prepare_message(struct spi_controller *host,
				       struct spi_message *message)
{
	struct meson_spicc_device *spicc = spi_controller_get_devdata(host);
	struct spi_device *spi = message->spi;
	u32 conf = readl_relaxed(spicc->base + SPICC_CONREG) & SPICC_DATARATE_MASK;

	/* Store current message */
	spicc->message = message;

	/* Enable Master */
	conf |= SPICC_ENABLE;
	conf |= SPICC_MODE_MASTER;

	/* SMC = 0 */

	/* Setup transfer mode */
	if (spi->mode & SPI_CPOL)
		conf |= SPICC_POL;
	else
		conf &= ~SPICC_POL;

	if (!spicc->data->has_oen) {
		if (spi->mode & SPI_CPOL) {
			if (spicc->pins_idle_high)
				pinctrl_select_state(spicc->pinctrl, spicc->pins_idle_high);
		} else {
			if (spicc->pins_idle_low)
				pinctrl_select_state(spicc->pinctrl, spicc->pins_idle_low);
		}
	}

	if (spi->mode & SPI_CPHA)
		conf |= SPICC_PHA;
	else
		conf &= ~SPICC_PHA;

	/* SSCTL = 0 */

	if (spi->mode & SPI_CS_HIGH)
		conf |= SPICC_SSPOL;
	else
		conf &= ~SPICC_SSPOL;

	if (spi->mode & SPI_READY)
		conf |= FIELD_PREP(SPICC_DRCTL_MASK, SPICC_DRCTL_LOWLEVEL);
	else
		conf |= FIELD_PREP(SPICC_DRCTL_MASK, SPICC_DRCTL_IGNORE);

	/* Select CS */
	conf |= FIELD_PREP(SPICC_CS_MASK, spi_get_chipselect(spi, 0));

	/* Default 8bit word */
	conf |= FIELD_PREP(SPICC_BITLENGTH_MASK, 8 - 1);

	writel_relaxed(conf, spicc->base + SPICC_CONREG);

	/* Setup no wait cycles by default */
	writel_relaxed(0, spicc->base + SPICC_PERIODREG);

	writel_bits_relaxed(SPICC_LBC_W1,
			    spi->mode & SPI_LOOP ? SPICC_LBC_W1 : 0,
			    spicc->base + SPICC_TESTREG);

	return 0;
}

static int meson_spicc_unprepare_transfer(struct spi_controller *host)
{
	struct meson_spicc_device *spicc = spi_controller_get_devdata(host);
	u32 conf = readl_relaxed(spicc->base + SPICC_CONREG) & SPICC_DATARATE_MASK;

	/* Disable all IRQs */
	writel(0, spicc->base + SPICC_INTREG);

	device_reset_optional(&spicc->pdev->dev);

	/* Set default configuration, keeping datarate field */
	writel_relaxed(conf, spicc->base + SPICC_CONREG);

	if (!spicc->data->has_oen)
		pinctrl_select_default_state(&spicc->pdev->dev);

	return 0;
}

static int meson_spicc_setup(struct spi_device *spi)
{
	if (!spi->controller_state)
		spi->controller_state = spi_controller_get_devdata(spi->controller);

	/* DMA works at 64 bits, the rest works on PIO */
	if (spi->bits_per_word != 8 &&
	    spi->bits_per_word != 16 &&
	    spi->bits_per_word != 24 &&
	    spi->bits_per_word != 32 &&
	    spi->bits_per_word != 64)
		return -EINVAL;

	return 0;
}

static void meson_spicc_cleanup(struct spi_device *spi)
{
	spi->controller_state = NULL;
}

/*
 * The Clock Mux
 *            x-----------------x   x------------x    x------\
 *        |---| pow2 fixed div  |---| pow2 div   |----|      |
 *        |   x-----------------x   x------------x    |      |
 * src ---|                                           | mux  |-- out
 *        |   x-----------------x   x------------x    |      |
 *        |---| enh fixed div   |---| enh div    |0---|      |
 *            x-----------------x   x------------x    x------/
 *
 * Clk path for GX series:
 *    src -> pow2 fixed div -> pow2 div -> out
 *
 * Clk path for AXG series:
 *    src -> pow2 fixed div -> pow2 div -> mux -> out
 *    src -> enh fixed div -> enh div -> mux -> out
 *
 * Clk path for G12A series:
 *    pclk -> pow2 fixed div -> pow2 div -> mux -> out
 *    pclk -> enh fixed div -> enh div -> mux -> out
 *
 * The pow2 divider is tied to the controller HW state, and the
 * divider is only valid when the controller is initialized.
 *
 * A set of clock ops is added to make sure we don't read/set this
 * clock rate while the controller is in an unknown state.
 */

static unsigned long meson_spicc_pow2_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	struct meson_spicc_device *spicc = pow2_clk_to_spicc(divider);

	if (!spicc->host->cur_msg)
		return 0;

	return clk_divider_ops.recalc_rate(hw, parent_rate);
}

static int meson_spicc_pow2_determine_rate(struct clk_hw *hw,
					   struct clk_rate_request *req)
{
	struct clk_divider *divider = to_clk_divider(hw);
	struct meson_spicc_device *spicc = pow2_clk_to_spicc(divider);

	if (!spicc->host->cur_msg)
		return -EINVAL;

	return clk_divider_ops.determine_rate(hw, req);
}

static int meson_spicc_pow2_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	struct meson_spicc_device *spicc = pow2_clk_to_spicc(divider);

	if (!spicc->host->cur_msg)
		return -EINVAL;

	return clk_divider_ops.set_rate(hw, rate, parent_rate);
}

static const struct clk_ops meson_spicc_pow2_clk_ops = {
	.recalc_rate = meson_spicc_pow2_recalc_rate,
	.determine_rate = meson_spicc_pow2_determine_rate,
	.set_rate = meson_spicc_pow2_set_rate,
};

static int meson_spicc_pow2_clk_init(struct meson_spicc_device *spicc)
{
	struct device *dev = &spicc->pdev->dev;
	struct clk_fixed_factor *pow2_fixed_div;
	struct clk_init_data init;
	struct clk *clk;
	struct clk_parent_data parent_data[2];
	char name[64];

	memset(&init, 0, sizeof(init));
	memset(&parent_data, 0, sizeof(parent_data));

	init.parent_data = parent_data;

	/* algorithm for pow2 div: rate = freq / 4 / (2 ^ N) */

	pow2_fixed_div = devm_kzalloc(dev, sizeof(*pow2_fixed_div), GFP_KERNEL);
	if (!pow2_fixed_div)
		return -ENOMEM;

	snprintf(name, sizeof(name), "%s#pow2_fixed_div", dev_name(dev));
	init.name = name;
	init.ops = &clk_fixed_factor_ops;
	if (spicc->data->has_pclk) {
		init.flags = CLK_SET_RATE_PARENT;
		parent_data[0].hw = __clk_get_hw(spicc->pclk);
	} else {
		init.flags = 0;
		parent_data[0].hw = __clk_get_hw(spicc->core);
	}
	init.num_parents = 1;

	pow2_fixed_div->mult = 1;
	pow2_fixed_div->div = 4;
	pow2_fixed_div->hw.init = &init;

	clk = devm_clk_register(dev, &pow2_fixed_div->hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	snprintf(name, sizeof(name), "%s#pow2_div", dev_name(dev));
	init.name = name;
	init.ops = &meson_spicc_pow2_clk_ops;
	/*
	 * Set NOCACHE here to make sure we read the actual HW value
	 * since we reset the HW after each transfer.
	 */
	init.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE;
	parent_data[0].hw = &pow2_fixed_div->hw;
	init.num_parents = 1;

	spicc->pow2_div.shift = 16;
	spicc->pow2_div.width = 3;
	spicc->pow2_div.flags = CLK_DIVIDER_POWER_OF_TWO;
	spicc->pow2_div.reg = spicc->base + SPICC_CONREG;
	spicc->pow2_div.hw.init = &init;

	spicc->clk = devm_clk_register(dev, &spicc->pow2_div.hw);
	if (WARN_ON(IS_ERR(spicc->clk)))
		return PTR_ERR(spicc->clk);

	return 0;
}

static int meson_spicc_enh_clk_init(struct meson_spicc_device *spicc)
{
	struct device *dev = &spicc->pdev->dev;
	struct clk_fixed_factor *enh_fixed_div;
	struct clk_divider *enh_div;
	struct clk_mux *mux;
	struct clk_init_data init;
	struct clk *clk;
	struct clk_parent_data parent_data[2];
	char name[64];

	memset(&init, 0, sizeof(init));
	memset(&parent_data, 0, sizeof(parent_data));

	init.parent_data = parent_data;

	/* algorithm for enh div: rate = freq / 2 / (N + 1) */

	enh_fixed_div = devm_kzalloc(dev, sizeof(*enh_fixed_div), GFP_KERNEL);
	if (!enh_fixed_div)
		return -ENOMEM;

	snprintf(name, sizeof(name), "%s#enh_fixed_div", dev_name(dev));
	init.name = name;
	init.ops = &clk_fixed_factor_ops;
	if (spicc->data->has_pclk) {
		init.flags = CLK_SET_RATE_PARENT;
		parent_data[0].hw = __clk_get_hw(spicc->pclk);
	} else {
		init.flags = 0;
		parent_data[0].hw = __clk_get_hw(spicc->core);
	}
	init.num_parents = 1;

	enh_fixed_div->mult = 1;
	enh_fixed_div->div = 2;
	enh_fixed_div->hw.init = &init;

	clk = devm_clk_register(dev, &enh_fixed_div->hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	enh_div = devm_kzalloc(dev, sizeof(*enh_div), GFP_KERNEL);
	if (!enh_div)
		return -ENOMEM;

	snprintf(name, sizeof(name), "%s#enh_div", dev_name(dev));
	init.name = name;
	init.ops = &clk_divider_ops;
	init.flags = CLK_SET_RATE_PARENT;
	parent_data[0].hw = &enh_fixed_div->hw;
	init.num_parents = 1;

	enh_div->shift	= 16;
	enh_div->width	= 8;
	enh_div->reg = spicc->base + SPICC_ENH_CTL0;
	enh_div->hw.init = &init;

	clk = devm_clk_register(dev, &enh_div->hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	snprintf(name, sizeof(name), "%s#sel", dev_name(dev));
	init.name = name;
	init.ops = &clk_mux_ops;
	parent_data[0].hw = &spicc->pow2_div.hw;
	parent_data[1].hw = &enh_div->hw;
	init.num_parents = 2;
	init.flags = CLK_SET_RATE_PARENT;

	mux->mask = 0x1;
	mux->shift = 24;
	mux->reg = spicc->base + SPICC_ENH_CTL0;
	mux->hw.init = &init;

	spicc->clk = devm_clk_register(dev, &mux->hw);
	if (WARN_ON(IS_ERR(spicc->clk)))
		return PTR_ERR(spicc->clk);

	return 0;
}

static int meson_spicc_probe(struct platform_device *pdev)
{
	struct spi_controller *host;
	struct meson_spicc_device *spicc;
	int ret, irq;

	host = spi_alloc_host(&pdev->dev, sizeof(*spicc));
	if (!host) {
		dev_err(&pdev->dev, "host allocation failed\n");
		return -ENOMEM;
	}
	spicc = spi_controller_get_devdata(host);
	spicc->host = host;

	spicc->data = of_device_get_match_data(&pdev->dev);
	if (!spicc->data) {
		dev_err(&pdev->dev, "failed to get match data\n");
		ret = -EINVAL;
		goto out_host;
	}

	spicc->pdev = pdev;
	platform_set_drvdata(pdev, spicc);

	init_completion(&spicc->done);

	spicc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spicc->base)) {
		dev_err(&pdev->dev, "io resource mapping failed\n");
		ret = PTR_ERR(spicc->base);
		goto out_host;
	}

	/* Set master mode and enable controller */
	writel_relaxed(SPICC_ENABLE | SPICC_MODE_MASTER,
		       spicc->base + SPICC_CONREG);

	/* Disable all IRQs */
	writel_relaxed(0, spicc->base + SPICC_INTREG);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto out_host;
	}

	ret = devm_request_irq(&pdev->dev, irq, meson_spicc_irq,
			       0, NULL, spicc);
	if (ret) {
		dev_err(&pdev->dev, "irq request failed\n");
		goto out_host;
	}

	spicc->core = devm_clk_get_enabled(&pdev->dev, "core");
	if (IS_ERR(spicc->core)) {
		dev_err(&pdev->dev, "core clock request failed\n");
		ret = PTR_ERR(spicc->core);
		goto out_host;
	}

	if (spicc->data->has_pclk) {
		spicc->pclk = devm_clk_get_enabled(&pdev->dev, "pclk");
		if (IS_ERR(spicc->pclk)) {
			dev_err(&pdev->dev, "pclk clock request failed\n");
			ret = PTR_ERR(spicc->pclk);
			goto out_host;
		}
	}

	spicc->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(spicc->pinctrl)) {
		ret = PTR_ERR(spicc->pinctrl);
		goto out_host;
	}

	device_reset_optional(&pdev->dev);

	host->num_chipselect = 4;
	host->dev.of_node = pdev->dev.of_node;
	host->mode_bits = SPI_CPHA | SPI_CPOL | SPI_CS_HIGH | SPI_LOOP;
	host->flags = (SPI_CONTROLLER_MUST_RX | SPI_CONTROLLER_MUST_TX);
	host->min_speed_hz = spicc->data->min_speed_hz;
	host->max_speed_hz = spicc->data->max_speed_hz;
	host->setup = meson_spicc_setup;
	host->cleanup = meson_spicc_cleanup;
	host->prepare_message = meson_spicc_prepare_message;
	host->unprepare_transfer_hardware = meson_spicc_unprepare_transfer;
	host->transfer_one = meson_spicc_transfer_one;
	host->use_gpio_descriptors = true;

	meson_spicc_oen_enable(spicc);

	ret = meson_spicc_pow2_clk_init(spicc);
	if (ret) {
		dev_err(&pdev->dev, "pow2 clock registration failed\n");
		goto out_host;
	}

	if (spicc->data->has_enhance_clk_div) {
		ret = meson_spicc_enh_clk_init(spicc);
		if (ret) {
			dev_err(&pdev->dev, "clock registration failed\n");
			goto out_host;
		}
	}

	ret = devm_spi_register_controller(&pdev->dev, host);
	if (ret) {
		dev_err(&pdev->dev, "spi registration failed\n");
		goto out_host;
	}

	return 0;

out_host:
	spi_controller_put(host);

	return ret;
}

static void meson_spicc_remove(struct platform_device *pdev)
{
	struct meson_spicc_device *spicc = platform_get_drvdata(pdev);

	/* Disable SPI */
	writel(0, spicc->base + SPICC_CONREG);

	spi_controller_put(spicc->host);
}

static const struct meson_spicc_data meson_spicc_gx_data = {
	.max_speed_hz		= 30000000,
	.min_speed_hz		= 325000,
	.fifo_size		= 16,
};

static const struct meson_spicc_data meson_spicc_axg_data = {
	.max_speed_hz		= 80000000,
	.min_speed_hz		= 325000,
	.fifo_size		= 16,
	.has_oen		= true,
	.has_enhance_clk_div	= true,
};

static const struct meson_spicc_data meson_spicc_g12a_data = {
	.max_speed_hz		= 166666666,
	.min_speed_hz		= 50000,
	.fifo_size		= 15,
	.has_oen		= true,
	.has_enhance_clk_div	= true,
	.has_pclk		= true,
};

static const struct of_device_id meson_spicc_of_match[] = {
	{
		.compatible	= "amlogic,meson-gx-spicc",
		.data		= &meson_spicc_gx_data,
	},
	{
		.compatible = "amlogic,meson-axg-spicc",
		.data		= &meson_spicc_axg_data,
	},
	{
		.compatible = "amlogic,meson-g12a-spicc",
		.data		= &meson_spicc_g12a_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_spicc_of_match);

static struct platform_driver meson_spicc_driver = {
	.probe   = meson_spicc_probe,
	.remove = meson_spicc_remove,
	.driver  = {
		.name = "meson-spicc",
		.of_match_table = of_match_ptr(meson_spicc_of_match),
	},
};

module_platform_driver(meson_spicc_driver);

MODULE_DESCRIPTION("Meson SPI Communication Controller driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL");
