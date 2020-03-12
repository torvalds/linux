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
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/reset.h>

/*
 * The Meson SPICC controller could support DMA based transfers, but is not
 * implemented by the vendor code, and while having the registers documentation
 * it has never worked on the GXL Hardware.
 * The PIO mode is the only mode implemented, and due to badly designed HW :
 * - all transfers are cutted in 16 words burst because the FIFO hangs on
 *   TX underflow, and there is no TX "Half-Empty" interrupt, so we go by
 *   FIFO max size chunk only
 * - CS management is dumb, and goes UP between every burst, so is really a
 *   "Data Valid" signal than a Chip Select, GPIO link should be used instead
 *   to have a CS go down over the full transfer
 */

#define SPICC_MAX_FREQ	30000000
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
#define SPICC_DLYCTL_W1_MASK	GENMASK(21, 16) /* Delay Control Write-Only */
#define SPICC_FIFORST_RO_MASK	GENMASK(22, 21) /* FIFO Softreset Read-Only */
#define SPICC_FIFORST_W1_MASK	GENMASK(23, 22) /* FIFO Softreset Write-Only */

#define SPICC_DRADDR	0x20	/* Read Address of DMA */

#define SPICC_DWADDR	0x24	/* Write Address of DMA */

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

#define SPICC_BURST_MAX	16
#define SPICC_FIFO_HALF 10

struct meson_spicc_data {
	bool				has_oen;
	bool				has_enhance_clk_div;
};

struct meson_spicc_device {
	struct spi_master		*master;
	struct platform_device		*pdev;
	void __iomem			*base;
	struct clk			*core;
	struct clk			*clk;
	struct spi_message		*message;
	struct spi_transfer		*xfer;
	const struct meson_spicc_data	*data;
	u8				*tx_buf;
	u8				*rx_buf;
	unsigned int			bytes_per_word;
	unsigned long			tx_remain;
	unsigned long			rx_remain;
	unsigned long			xfer_remain;
	bool				is_burst_end;
	bool				is_last_burst;
};

static void meson_spicc_oen_enable(struct meson_spicc_device *spicc)
{
	u32 conf;

	if (!spicc->data->has_oen)
		return;

	conf = readl_relaxed(spicc->base + SPICC_ENH_CTL0) |
		SPICC_ENH_MOSI_OEN | SPICC_ENH_CLK_OEN | SPICC_ENH_CS_OEN;

	writel_relaxed(conf, spicc->base + SPICC_ENH_CTL0);
}

static inline bool meson_spicc_txfull(struct meson_spicc_device *spicc)
{
	return !!FIELD_GET(SPICC_TF,
			   readl_relaxed(spicc->base + SPICC_STATREG));
}

static inline bool meson_spicc_rxready(struct meson_spicc_device *spicc)
{
	return FIELD_GET(SPICC_RH | SPICC_RR | SPICC_RF_EN,
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

static inline u32 meson_spicc_setup_rx_irq(struct meson_spicc_device *spicc,
					   u32 irq_ctrl)
{
	if (spicc->rx_remain > SPICC_FIFO_HALF)
		irq_ctrl |= SPICC_RH_EN;
	else
		irq_ctrl |= SPICC_RR_EN;

	return irq_ctrl;
}

static inline void meson_spicc_setup_burst(struct meson_spicc_device *spicc,
					   unsigned int burst_len)
{
	/* Setup Xfer variables */
	spicc->tx_remain = burst_len;
	spicc->rx_remain = burst_len;
	spicc->xfer_remain -= burst_len * spicc->bytes_per_word;
	spicc->is_burst_end = false;
	if (burst_len < SPICC_BURST_MAX || !spicc->xfer_remain)
		spicc->is_last_burst = true;
	else
		spicc->is_last_burst = false;

	/* Setup burst length */
	writel_bits_relaxed(SPICC_BURSTLENGTH_MASK,
			FIELD_PREP(SPICC_BURSTLENGTH_MASK,
				burst_len),
			spicc->base + SPICC_CONREG);

	/* Fill TX FIFO */
	meson_spicc_tx(spicc);
}

static irqreturn_t meson_spicc_irq(int irq, void *data)
{
	struct meson_spicc_device *spicc = (void *) data;
	u32 ctrl = readl_relaxed(spicc->base + SPICC_INTREG);
	u32 stat = readl_relaxed(spicc->base + SPICC_STATREG) & ctrl;

	ctrl &= ~(SPICC_RH_EN | SPICC_RR_EN);

	/* Empty RX FIFO */
	meson_spicc_rx(spicc);

	/* Enable TC interrupt since we transferred everything */
	if (!spicc->tx_remain && !spicc->rx_remain) {
		spicc->is_burst_end = true;

		/* Enable TC interrupt */
		ctrl |= SPICC_TC_EN;

		/* Reload IRQ status */
		stat = readl_relaxed(spicc->base + SPICC_STATREG) & ctrl;
	}

	/* Check transfer complete */
	if ((stat & SPICC_TC) && spicc->is_burst_end) {
		unsigned int burst_len;

		/* Clear TC bit */
		writel_relaxed(SPICC_TC, spicc->base + SPICC_STATREG);

		/* Disable TC interrupt */
		ctrl &= ~SPICC_TC_EN;

		if (spicc->is_last_burst) {
			/* Disable all IRQs */
			writel(0, spicc->base + SPICC_INTREG);

			spi_finalize_current_transfer(spicc->master);

			return IRQ_HANDLED;
		}

		burst_len = min_t(unsigned int,
				  spicc->xfer_remain / spicc->bytes_per_word,
				  SPICC_BURST_MAX);

		/* Setup burst */
		meson_spicc_setup_burst(spicc, burst_len);

		/* Restart burst */
		writel_bits_relaxed(SPICC_XCH, SPICC_XCH,
				    spicc->base + SPICC_CONREG);
	}

	/* Setup RX interrupt trigger */
	ctrl = meson_spicc_setup_rx_irq(spicc, ctrl);

	/* Reconfigure interrupts */
	writel(ctrl, spicc->base + SPICC_INTREG);

	return IRQ_HANDLED;
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
}

static int meson_spicc_transfer_one(struct spi_master *master,
				    struct spi_device *spi,
				    struct spi_transfer *xfer)
{
	struct meson_spicc_device *spicc = spi_master_get_devdata(master);
	unsigned int burst_len;
	u32 irq = 0;

	/* Store current transfer */
	spicc->xfer = xfer;

	/* Setup transfer parameters */
	spicc->tx_buf = (u8 *)xfer->tx_buf;
	spicc->rx_buf = (u8 *)xfer->rx_buf;
	spicc->xfer_remain = xfer->len;

	/* Pre-calculate word size */
	spicc->bytes_per_word =
	   DIV_ROUND_UP(spicc->xfer->bits_per_word, 8);

	/* Setup transfer parameters */
	meson_spicc_setup_xfer(spicc, xfer);

	burst_len = min_t(unsigned int,
			  spicc->xfer_remain / spicc->bytes_per_word,
			  SPICC_BURST_MAX);

	meson_spicc_setup_burst(spicc, burst_len);

	irq = meson_spicc_setup_rx_irq(spicc, irq);

	/* Start burst */
	writel_bits_relaxed(SPICC_XCH, SPICC_XCH, spicc->base + SPICC_CONREG);

	/* Enable interrupts */
	writel_relaxed(irq, spicc->base + SPICC_INTREG);

	return 1;
}

static int meson_spicc_prepare_message(struct spi_master *master,
				       struct spi_message *message)
{
	struct meson_spicc_device *spicc = spi_master_get_devdata(master);
	struct spi_device *spi = message->spi;
	u32 conf = 0;

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
	conf |= FIELD_PREP(SPICC_CS_MASK, spi->chip_select);

	/* Default Clock rate core/4 */

	/* Default 8bit word */
	conf |= FIELD_PREP(SPICC_BITLENGTH_MASK, 8 - 1);

	writel_relaxed(conf, spicc->base + SPICC_CONREG);

	/* Setup no wait cycles by default */
	writel_relaxed(0, spicc->base + SPICC_PERIODREG);

	writel_bits_relaxed(BIT(24), BIT(24), spicc->base + SPICC_TESTREG);

	return 0;
}

static int meson_spicc_unprepare_transfer(struct spi_master *master)
{
	struct meson_spicc_device *spicc = spi_master_get_devdata(master);

	/* Disable all IRQs */
	writel(0, spicc->base + SPICC_INTREG);

	device_reset_optional(&spicc->pdev->dev);

	return 0;
}

static int meson_spicc_setup(struct spi_device *spi)
{
	if (!spi->controller_state)
		spi->controller_state = spi_master_get_devdata(spi->master);

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
 */

static int meson_spicc_clk_init(struct meson_spicc_device *spicc)
{
	struct device *dev = &spicc->pdev->dev;
	struct clk_fixed_factor *pow2_fixed_div, *enh_fixed_div;
	struct clk_divider *pow2_div, *enh_div;
	struct clk_mux *mux;
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
	init.flags = 0;
	parent_data[0].hw = __clk_get_hw(spicc->core);
	init.num_parents = 1;

	pow2_fixed_div->mult = 1,
	pow2_fixed_div->div = 4,
	pow2_fixed_div->hw.init = &init;

	clk = devm_clk_register(dev, &pow2_fixed_div->hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	pow2_div = devm_kzalloc(dev, sizeof(*pow2_div), GFP_KERNEL);
	if (!pow2_div)
		return -ENOMEM;

	snprintf(name, sizeof(name), "%s#pow2_div", dev_name(dev));
	init.name = name;
	init.ops = &clk_divider_ops;
	init.flags = CLK_SET_RATE_PARENT;
	parent_data[0].hw = &pow2_fixed_div->hw;
	init.num_parents = 1;

	pow2_div->shift = 16,
	pow2_div->width = 3,
	pow2_div->flags = CLK_DIVIDER_POWER_OF_TWO,
	pow2_div->reg = spicc->base + SPICC_CONREG;
	pow2_div->hw.init = &init;

	clk = devm_clk_register(dev, &pow2_div->hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	if (!spicc->data->has_enhance_clk_div) {
		spicc->clk = clk;
		return 0;
	}

	/* algorithm for enh div: rate = freq / 2 / (N + 1) */

	enh_fixed_div = devm_kzalloc(dev, sizeof(*enh_fixed_div), GFP_KERNEL);
	if (!enh_fixed_div)
		return -ENOMEM;

	snprintf(name, sizeof(name), "%s#enh_fixed_div", dev_name(dev));
	init.name = name;
	init.ops = &clk_fixed_factor_ops;
	init.flags = 0;
	parent_data[0].hw = __clk_get_hw(spicc->core);
	init.num_parents = 1;

	enh_fixed_div->mult = 1,
	enh_fixed_div->div = 2,
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

	enh_div->shift	= 16,
	enh_div->width	= 8,
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
	parent_data[0].hw = &pow2_div->hw;
	parent_data[1].hw = &enh_div->hw;
	init.num_parents = 2;
	init.flags = CLK_SET_RATE_PARENT;

	mux->mask = 0x1,
	mux->shift = 24,
	mux->reg = spicc->base + SPICC_ENH_CTL0;
	mux->hw.init = &init;

	spicc->clk = devm_clk_register(dev, &mux->hw);
	if (WARN_ON(IS_ERR(spicc->clk)))
		return PTR_ERR(spicc->clk);

	return 0;
}

static int meson_spicc_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct meson_spicc_device *spicc;
	int ret, irq, rate;

	master = spi_alloc_master(&pdev->dev, sizeof(*spicc));
	if (!master) {
		dev_err(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}
	spicc = spi_master_get_devdata(master);
	spicc->master = master;

	spicc->data = of_device_get_match_data(&pdev->dev);
	if (!spicc->data) {
		dev_err(&pdev->dev, "failed to get match data\n");
		ret = -EINVAL;
		goto out_master;
	}

	spicc->pdev = pdev;
	platform_set_drvdata(pdev, spicc);

	spicc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spicc->base)) {
		dev_err(&pdev->dev, "io resource mapping failed\n");
		ret = PTR_ERR(spicc->base);
		goto out_master;
	}

	/* Set master mode and enable controller */
	writel_relaxed(SPICC_ENABLE | SPICC_MODE_MASTER,
		       spicc->base + SPICC_CONREG);

	/* Disable all IRQs */
	writel_relaxed(0, spicc->base + SPICC_INTREG);

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, meson_spicc_irq,
			       0, NULL, spicc);
	if (ret) {
		dev_err(&pdev->dev, "irq request failed\n");
		goto out_master;
	}

	spicc->core = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(spicc->core)) {
		dev_err(&pdev->dev, "core clock request failed\n");
		ret = PTR_ERR(spicc->core);
		goto out_master;
	}

	ret = clk_prepare_enable(spicc->core);
	if (ret) {
		dev_err(&pdev->dev, "core clock enable failed\n");
		goto out_master;
	}
	rate = clk_get_rate(spicc->core);

	device_reset_optional(&pdev->dev);

	master->num_chipselect = 4;
	master->dev.of_node = pdev->dev.of_node;
	master->mode_bits = SPI_CPHA | SPI_CPOL | SPI_CS_HIGH;
	master->bits_per_word_mask = SPI_BPW_MASK(32) |
				     SPI_BPW_MASK(24) |
				     SPI_BPW_MASK(16) |
				     SPI_BPW_MASK(8);
	master->flags = (SPI_MASTER_MUST_RX | SPI_MASTER_MUST_TX);
	master->min_speed_hz = rate >> 9;
	master->setup = meson_spicc_setup;
	master->cleanup = meson_spicc_cleanup;
	master->prepare_message = meson_spicc_prepare_message;
	master->unprepare_transfer_hardware = meson_spicc_unprepare_transfer;
	master->transfer_one = meson_spicc_transfer_one;
	master->use_gpio_descriptors = true;

	/* Setup max rate according to the Meson GX datasheet */
	if ((rate >> 2) > SPICC_MAX_FREQ)
		master->max_speed_hz = SPICC_MAX_FREQ;
	else
		master->max_speed_hz = rate >> 2;

	meson_spicc_oen_enable(spicc);

	ret = meson_spicc_clk_init(spicc);
	if (ret) {
		dev_err(&pdev->dev, "clock registration failed\n");
		goto out_master;
	}

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret) {
		dev_err(&pdev->dev, "spi master registration failed\n");
		goto out_clk;
	}

	return 0;

out_clk:
	clk_disable_unprepare(spicc->core);

out_master:
	spi_master_put(master);

	return ret;
}

static int meson_spicc_remove(struct platform_device *pdev)
{
	struct meson_spicc_device *spicc = platform_get_drvdata(pdev);

	/* Disable SPI */
	writel(0, spicc->base + SPICC_CONREG);

	clk_disable_unprepare(spicc->core);

	return 0;
}

static const struct meson_spicc_data meson_spicc_gx_data = {
};

static const struct meson_spicc_data meson_spicc_axg_data = {
	.has_oen		= true,
	.has_enhance_clk_div	= true,
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
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_spicc_of_match);

static struct platform_driver meson_spicc_driver = {
	.probe   = meson_spicc_probe,
	.remove  = meson_spicc_remove,
	.driver  = {
		.name = "meson-spicc",
		.of_match_table = of_match_ptr(meson_spicc_of_match),
	},
};

module_platform_driver(meson_spicc_driver);

MODULE_DESCRIPTION("Meson SPI Communication Controller driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL");
