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

/* Registers */
#define RSPI_SPDR		0x00
#define RSPI_SPCR		0x08
#define RSPI_SSLP		0x10
#define RSPI_SPBR		0x11
#define RSPI_SPSCR		0x13
#define RSPI_SPCMD		0x14
#define RSPI_SPDCR2		0x44
#define RSPI_SPSR		0x52
#define RSPI_SPSRC		0x6a
#define RSPI_SPFCR		0x6c

/* Register SPCR */
#define RSPI_SPCR_MSTR		BIT(30)
#define RSPI_SPCR_SPRIE		BIT(17)
#define RSPI_SPCR_SCKASE	BIT(12)
#define RSPI_SPCR_SPE		BIT(0)

/* Register SPBR */
#define RSPI_SPBR_SPR_MIN	0
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
#define RSPI_FIFO_SIZE		16

/* Register SPSR */
#define RSPI_SPSR_SPRF		BIT(15)

/* Register RSPI_SPSRC */
#define RSPI_SPSRC_CLEAR	0xfd80

#define RSPI_RESET_NUM		2
#define RSPI_CLK_NUM		3

struct rzv2h_rspi_priv {
	struct reset_control_bulk_data resets[RSPI_RESET_NUM];
	struct spi_controller *controller;
	void __iomem *base;
	struct clk *tclk;
	wait_queue_head_t wait;
	unsigned int bytes_per_word;
	u32 freq;
	u16 status;
};

#define RZV2H_RSPI_TX(func, type)					\
static inline void rzv2h_rspi_tx_##type(struct rzv2h_rspi_priv *rspi,	\
					const void *txbuf,		\
					unsigned int index) {		\
	type buf = 0;							\
									\
	if (txbuf)							\
		buf = ((type *)txbuf)[index];				\
									\
	func(buf, rspi->base + RSPI_SPDR);				\
}

#define RZV2H_RSPI_RX(func, type)					\
static inline void rzv2h_rspi_rx_##type(struct rzv2h_rspi_priv *rspi,	\
					void *rxbuf,			\
					unsigned int index) {		\
	type buf = func(rspi->base + RSPI_SPDR);			\
									\
	if (rxbuf)							\
		((type *)rxbuf)[index] = buf;				\
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

static int rzv2h_rspi_transfer_one(struct spi_controller *controller,
				  struct spi_device *spi,
				  struct spi_transfer *transfer)
{
	struct rzv2h_rspi_priv *rspi = spi_controller_get_devdata(controller);
	unsigned int words_to_transfer, i;
	int ret = 0;

	transfer->effective_speed_hz = rspi->freq;
	words_to_transfer = transfer->len / rspi->bytes_per_word;

	for (i = 0; i < words_to_transfer; i++) {
		rzv2h_rspi_clear_all_irqs(rspi);

		rzv2h_rspi_send(rspi, transfer->tx_buf, i);

		ret = rzv2h_rspi_receive(rspi, transfer->rx_buf, i);
		if (ret)
			break;
	}

	rzv2h_rspi_clear_all_irqs(rspi);

	if (ret)
		transfer->error = SPI_TRANS_FAIL_IO;

	spi_finalize_current_transfer(controller);

	return ret;
}

static inline u32 rzv2h_rspi_calc_bitrate(unsigned long tclk_rate, u8 spr,
					  u8 brdv)
{
	return DIV_ROUND_UP(tclk_rate, (2 * (spr + 1) * (1 << brdv)));
}

static u32 rzv2h_rspi_setup_clock(struct rzv2h_rspi_priv *rspi, u32 hz)
{
	unsigned long tclk_rate;
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
	tclk_rate = clk_get_rate(rspi->tclk);
	for (brdv = RSPI_SPCMD_BRDV_MIN; brdv <= RSPI_SPCMD_BRDV_MAX; brdv++) {
		spr = DIV_ROUND_UP(tclk_rate, hz * (1 << (brdv + 1)));
		spr--;
		if (spr >= RSPI_SPBR_SPR_MIN && spr <= RSPI_SPBR_SPR_MAX)
			goto clock_found;
	}

	return 0;

clock_found:
	rzv2h_rspi_reg_rmw(rspi, RSPI_SPCMD, RSPI_SPCMD_BRDV, brdv);
	writeb(spr, rspi->base + RSPI_SPBR);

	return rzv2h_rspi_calc_bitrate(tclk_rate, spr, brdv);
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

	/* Make sure SPCR.SPE is 0 before amending the configuration */
	rzv2h_rspi_spe_disable(rspi);

	/* Configure the device to work in "host" mode */
	conf32 = RSPI_SPCR_MSTR;

	/* Auto-stop function */
	conf32 |= RSPI_SPCR_SCKASE;

	/* SPI receive buffer full interrupt enable */
	conf32 |= RSPI_SPCR_SPRIE;

	writel(conf32, rspi->base + RSPI_SPCR);

	/* Use SPCMD0 only */
	writeb(0x0, rspi->base + RSPI_SPSCR);

	/* Setup mode */
	conf32 = FIELD_PREP(RSPI_SPCMD_CPOL, !!(spi->mode & SPI_CPOL));
	conf32 |= FIELD_PREP(RSPI_SPCMD_CPHA, !!(spi->mode & SPI_CPHA));
	conf32 |= FIELD_PREP(RSPI_SPCMD_LSBF, !!(spi->mode & SPI_LSB_FIRST));
	conf32 |= FIELD_PREP(RSPI_SPCMD_SSLKP, 1);
	conf32 |= FIELD_PREP(RSPI_SPCMD_SSLA, spi_get_chipselect(spi, 0));
	writel(conf32, rspi->base + RSPI_SPCMD);
	if (spi->mode & SPI_CS_HIGH)
		writeb(BIT(spi_get_chipselect(spi, 0)), rspi->base + RSPI_SSLP);
	else
		writeb(0, rspi->base + RSPI_SSLP);

	/* Setup FIFO thresholds */
	conf16 = FIELD_PREP(RSPI_SPDCR2_TTRG, RSPI_FIFO_SIZE - 1);
	conf16 |= FIELD_PREP(RSPI_SPDCR2_RTRG, 0);
	writew(conf16, rspi->base + RSPI_SPDCR2);

	rzv2h_rspi_clear_fifos(rspi);

	list_for_each_entry(xfer, &message->transfers, transfer_list) {
		if (!xfer->speed_hz)
			continue;

		speed_hz = min(xfer->speed_hz, speed_hz);
		bits_per_word = xfer->bits_per_word;
	}

	if (speed_hz == U32_MAX)
		return -EINVAL;

	rspi->bytes_per_word = roundup_pow_of_two(BITS_TO_BYTES(bits_per_word));
	rzv2h_rspi_reg_rmw(rspi, RSPI_SPCMD, RSPI_SPCMD_SPB, bits_per_word - 1);

	rspi->freq = rzv2h_rspi_setup_clock(rspi, speed_hz);
	if (!rspi->freq)
		return -EINVAL;

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
	struct clk_bulk_data *clks;
	unsigned long tclk_rate;
	int irq_rx, ret, i;

	controller = devm_spi_alloc_host(dev, sizeof(*rspi));
	if (!controller)
		return -ENOMEM;

	rspi = spi_controller_get_devdata(controller);
	platform_set_drvdata(pdev, rspi);

	rspi->controller = controller;

	rspi->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rspi->base))
		return PTR_ERR(rspi->base);

	ret = devm_clk_bulk_get_all_enabled(dev, &clks);
	if (ret != RSPI_CLK_NUM)
		return dev_err_probe(dev, ret >= 0 ? -EINVAL : ret,
				     "cannot get clocks\n");
	for (i = 0; i < RSPI_CLK_NUM; i++) {
		if (!strcmp(clks[i].id, "tclk")) {
			rspi->tclk = clks[i].clk;
			break;
		}
	}

	if (!rspi->tclk)
		return dev_err_probe(dev, -EINVAL, "Failed to get tclk\n");

	tclk_rate = clk_get_rate(rspi->tclk);

	rspi->resets[0].id = "presetn";
	rspi->resets[1].id = "tresetn";
	ret = devm_reset_control_bulk_get_exclusive(dev, RSPI_RESET_NUM,
						    rspi->resets);
	if (ret)
		return dev_err_probe(dev, ret, "cannot get resets\n");

	irq_rx = platform_get_irq_byname(pdev, "rx");
	if (irq_rx < 0)
		return dev_err_probe(dev, irq_rx, "cannot get IRQ 'rx'\n");

	ret = reset_control_bulk_deassert(RSPI_RESET_NUM, rspi->resets);
	if (ret)
		return dev_err_probe(dev, ret, "failed to deassert resets\n");

	init_waitqueue_head(&rspi->wait);

	ret = devm_request_irq(dev, irq_rx, rzv2h_rx_irq_handler, 0,
			       dev_name(dev), rspi);
	if (ret) {
		dev_err(dev, "cannot request `rx` IRQ\n");
		goto quit_resets;
	}

	controller->mode_bits = SPI_CPHA | SPI_CPOL | SPI_CS_HIGH |
				SPI_LSB_FIRST;
	controller->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
	controller->prepare_message = rzv2h_rspi_prepare_message;
	controller->unprepare_message = rzv2h_rspi_unprepare_message;
	controller->num_chipselect = 4;
	controller->transfer_one = rzv2h_rspi_transfer_one;
	controller->min_speed_hz = rzv2h_rspi_calc_bitrate(tclk_rate,
							   RSPI_SPBR_SPR_MAX,
							   RSPI_SPCMD_BRDV_MAX);
	controller->max_speed_hz = rzv2h_rspi_calc_bitrate(tclk_rate,
							   RSPI_SPBR_SPR_MIN,
							   RSPI_SPCMD_BRDV_MIN);

	device_set_node(&controller->dev, dev_fwnode(dev));

	ret = spi_register_controller(controller);
	if (ret) {
		dev_err(dev, "register controller failed\n");
		goto quit_resets;
	}

	return 0;

quit_resets:
	reset_control_bulk_assert(RSPI_RESET_NUM, rspi->resets);

	return ret;
}

static void rzv2h_rspi_remove(struct platform_device *pdev)
{
	struct rzv2h_rspi_priv *rspi = platform_get_drvdata(pdev);

	spi_unregister_controller(rspi->controller);

	reset_control_bulk_assert(RSPI_RESET_NUM, rspi->resets);
}

static const struct of_device_id rzv2h_rspi_match[] = {
	{ .compatible = "renesas,r9a09g057-rspi" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzv2h_rspi_match);

static struct platform_driver rzv2h_rspi_drv = {
	.probe = rzv2h_rspi_probe,
	.remove = rzv2h_rspi_remove,
	.driver = {
		.name = "rzv2h_rspi",
		.of_match_table = rzv2h_rspi_match,
	},
};
module_platform_driver(rzv2h_rspi_drv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fabrizio Castro <fabrizio.castro.jz@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/V2H(P) Serial Peripheral Interface Driver");
