// SPDX-License-Identifier: (GPL-2.0)
//
// Microchip CoreSPI controller driver
//
// Copyright (c) 2025 Microchip Technology Inc. and its subsidiaries
//
// Author: Prajna Rajendra Kumar <prajna.rajendrakumar@microchip.com>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#define MCHP_CORESPI_MAX_CS				(8)
#define MCHP_CORESPI_DEFAULT_FIFO_DEPTH			(4)
#define MCHP_CORESPI_DEFAULT_MOTOROLA_MODE		(3)

#define MCHP_CORESPI_CONTROL_ENABLE			BIT(0)
#define MCHP_CORESPI_CONTROL_MASTER			BIT(1)
#define MCHP_CORESPI_CONTROL_TX_DATA_INT		BIT(3)
#define MCHP_CORESPI_CONTROL_RX_OVER_INT		BIT(4)
#define MCHP_CORESPI_CONTROL_TX_UNDER_INT		BIT(5)
#define MCHP_CORESPI_CONTROL_FRAMEURUN			BIT(6)
#define MCHP_CORESPI_CONTROL_OENOFF			BIT(7)

#define MCHP_CORESPI_STATUS_ACTIVE			BIT(7)
#define MCHP_CORESPI_STATUS_SSEL			BIT(6)
#define MCHP_CORESPI_STATUS_TXFIFO_UNDERFLOW		BIT(5)
#define MCHP_CORESPI_STATUS_RXFIFO_FULL			BIT(4)
#define MCHP_CORESPI_STATUS_TXFIFO_FULL			BIT(3)
#define MCHP_CORESPI_STATUS_RXFIFO_EMPTY		BIT(2)
#define MCHP_CORESPI_STATUS_DONE			BIT(1)
#define MCHP_CORESPI_STATUS_FIRSTFRAME			BIT(0)

#define MCHP_CORESPI_INT_TXDONE				BIT(0)
#define MCHP_CORESPI_INT_RX_CHANNEL_OVERFLOW		BIT(2)
#define MCHP_CORESPI_INT_TX_CHANNEL_UNDERRUN		BIT(3)
#define MCHP_CORESPI_INT_CMDINT				BIT(4)
#define MCHP_CORESPI_INT_SSEND				BIT(5)
#define MCHP_CORESPI_INT_DATA_RX			BIT(6)
#define MCHP_CORESPI_INT_TXRFM				BIT(7)

#define MCHP_CORESPI_CONTROL2_INTEN_TXRFMT		BIT(7)
#define MCHP_CORESPI_CONTROL2_INTEN_DATA_RX		BIT(6)
#define MCHP_CORESPI_CONTROL2_INTEN_SSEND		BIT(5)
#define MCHP_CORESPI_CONTROL2_INTEN_CMD			BIT(4)

#define INT_ENABLE_MASK (MCHP_CORESPI_CONTROL_TX_DATA_INT | MCHP_CORESPI_CONTROL_RX_OVER_INT | \
			 MCHP_CORESPI_CONTROL_TX_UNDER_INT)

#define MCHP_CORESPI_REG_CONTROL			(0x00)
#define MCHP_CORESPI_REG_INTCLEAR			(0x04)
#define MCHP_CORESPI_REG_RXDATA				(0x08)
#define MCHP_CORESPI_REG_TXDATA				(0x0c)
#define MCHP_CORESPI_REG_INTMASK			(0X10)
#define MCHP_CORESPI_REG_INTRAW				(0X14)
#define MCHP_CORESPI_REG_CONTROL2			(0x18)
#define MCHP_CORESPI_REG_COMMAND			(0x1c)
#define MCHP_CORESPI_REG_STAT				(0x20)
#define MCHP_CORESPI_REG_SSEL				(0x24)
#define MCHP_CORESPI_REG_TXDATA_LAST			(0X28)
#define MCHP_CORESPI_REG_CLK_DIV			(0x2c)

struct mchp_corespi {
	void __iomem *regs;
	struct clk *clk;
	const u8 *tx_buf;
	u8 *rx_buf;
	u32 clk_gen;
	int irq;
	unsigned int tx_len;
	unsigned int rx_len;
	u32 fifo_depth;
};

static inline void mchp_corespi_disable(struct mchp_corespi *spi)
{
	u8 control = readb(spi->regs + MCHP_CORESPI_REG_CONTROL);

	control &= ~MCHP_CORESPI_CONTROL_ENABLE;

	writeb(control, spi->regs + MCHP_CORESPI_REG_CONTROL);
}

static inline void mchp_corespi_read_fifo(struct mchp_corespi *spi, u32 fifo_max)
{
	for (int i = 0; i < fifo_max; i++) {
		u32 data;

		while (readb(spi->regs + MCHP_CORESPI_REG_STAT) &
		       MCHP_CORESPI_STATUS_RXFIFO_EMPTY)
			;

		/* On TX-only transfers always perform a dummy read */
		data = readb(spi->regs + MCHP_CORESPI_REG_RXDATA);
		if (spi->rx_buf)
			*spi->rx_buf++ = data;

		spi->rx_len--;
	}
}

static void mchp_corespi_enable_ints(struct mchp_corespi *spi)
{
	u8 control = readb(spi->regs + MCHP_CORESPI_REG_CONTROL);

	control |= INT_ENABLE_MASK;
	writeb(control, spi->regs + MCHP_CORESPI_REG_CONTROL);
}

static void mchp_corespi_disable_ints(struct mchp_corespi *spi)
{
	u8 control = readb(spi->regs + MCHP_CORESPI_REG_CONTROL);

	control &= ~INT_ENABLE_MASK;
	writeb(control, spi->regs + MCHP_CORESPI_REG_CONTROL);
}

static inline void mchp_corespi_write_fifo(struct mchp_corespi *spi, u32 fifo_max)
{
	for (int i = 0; i < fifo_max; i++) {
		if (readb(spi->regs + MCHP_CORESPI_REG_STAT) &
		    MCHP_CORESPI_STATUS_TXFIFO_FULL)
			break;

		/* On RX-only transfers always perform a dummy write */
		if (spi->tx_buf)
			writeb(*spi->tx_buf++, spi->regs + MCHP_CORESPI_REG_TXDATA);
		else
			writeb(0xaa, spi->regs + MCHP_CORESPI_REG_TXDATA);

		spi->tx_len--;
	}
}

static void mchp_corespi_set_cs(struct spi_device *spi, bool disable)
{
	struct mchp_corespi *corespi = spi_controller_get_devdata(spi->controller);
	u32 reg;

	reg = readb(corespi->regs + MCHP_CORESPI_REG_SSEL);
	reg &= ~BIT(spi_get_chipselect(spi, 0));
	reg |= !disable << spi_get_chipselect(spi, 0);

	writeb(reg, corespi->regs + MCHP_CORESPI_REG_SSEL);
}

static int mchp_corespi_setup(struct spi_device *spi)
{
	if (spi_get_csgpiod(spi, 0))
		return 0;

	if (spi->mode & (SPI_CS_HIGH)) {
		dev_err(&spi->dev, "unable to support active-high CS in Motorola mode\n");
		return -EOPNOTSUPP;
	}

	if ((spi->mode ^ spi->controller->mode_bits) & SPI_MODE_X_MASK) {
		dev_err(&spi->dev, "incompatible CPOL/CPHA, must match controller's Motorola mode\n");
		return -EINVAL;
	}

	return 0;
}

static void mchp_corespi_init(struct spi_controller *host, struct mchp_corespi *spi)
{
	u8 control = readb(spi->regs + MCHP_CORESPI_REG_CONTROL);

	/* Master mode changes require core to be disabled.*/
	control = (control & ~MCHP_CORESPI_CONTROL_ENABLE) | MCHP_CORESPI_CONTROL_MASTER;

	writeb(control, spi->regs + MCHP_CORESPI_REG_CONTROL);

	mchp_corespi_enable_ints(spi);

	control = readb(spi->regs + MCHP_CORESPI_REG_CONTROL);
	control |= MCHP_CORESPI_CONTROL_ENABLE;

	writeb(control, spi->regs + MCHP_CORESPI_REG_CONTROL);
}

static irqreturn_t mchp_corespi_interrupt(int irq, void *dev_id)
{
	struct spi_controller *host = dev_id;
	struct mchp_corespi *spi = spi_controller_get_devdata(host);
	u8 intfield = readb(spi->regs + MCHP_CORESPI_REG_INTMASK) & 0xff;
	bool finalise = false;

	/* Interrupt line may be shared and not for us at all */
	if (intfield == 0)
		return IRQ_NONE;

	if (intfield & MCHP_CORESPI_INT_TXDONE)
		writeb(MCHP_CORESPI_INT_TXDONE, spi->regs + MCHP_CORESPI_REG_INTCLEAR);

	if (intfield & MCHP_CORESPI_INT_RX_CHANNEL_OVERFLOW) {
		writeb(MCHP_CORESPI_INT_RX_CHANNEL_OVERFLOW,
		       spi->regs + MCHP_CORESPI_REG_INTCLEAR);
		finalise = true;
		dev_err(&host->dev,
			"RX OVERFLOW: rxlen: %u, txlen: %u\n",
			spi->rx_len, spi->tx_len);
	}

	if (intfield & MCHP_CORESPI_INT_TX_CHANNEL_UNDERRUN) {
		writeb(MCHP_CORESPI_INT_TX_CHANNEL_UNDERRUN,
		       spi->regs + MCHP_CORESPI_REG_INTCLEAR);
		finalise = true;
		dev_err(&host->dev,
			"TX UNDERFLOW: rxlen: %u, txlen: %u\n",
			spi->rx_len, spi->tx_len);
	}

	if (finalise)
		spi_finalize_current_transfer(host);

	return IRQ_HANDLED;
}

static int mchp_corespi_set_clk_div(struct mchp_corespi *spi,
				    unsigned long target_hz)
{
	unsigned long pclk_hz, spi_hz;
	u32 clk_div;

	/* Get peripheral clock rate */
	pclk_hz = clk_get_rate(spi->clk);
	if (!pclk_hz)
		return -EINVAL;

	/*
	 * Calculate clock rate generated by SPI master
	 * Formula: SPICLK = PCLK / (2 * (CLK_DIV + 1))
	 */
	clk_div = DIV_ROUND_UP(pclk_hz, 2 * target_hz) - 1;

	if (clk_div > 0xFF)
		return -EINVAL;

	spi_hz = pclk_hz / (2 * (clk_div + 1));

	if (spi_hz > target_hz)
		return -EINVAL;

	writeb(clk_div, spi->regs + MCHP_CORESPI_REG_CLK_DIV);

	return 0;
}

static int mchp_corespi_transfer_one(struct spi_controller *host,
				     struct spi_device *spi_dev,
				     struct spi_transfer *xfer)
{
	struct mchp_corespi *spi = spi_controller_get_devdata(host);
	int ret;

	ret = mchp_corespi_set_clk_div(spi, (unsigned long)xfer->speed_hz);
	if (ret) {
		dev_err(&host->dev, "failed to set clock divider for target %u Hz\n",
			xfer->speed_hz);
		return ret;
	}

	spi->tx_buf = xfer->tx_buf;
	spi->rx_buf = xfer->rx_buf;
	spi->tx_len = xfer->len;
	spi->rx_len = xfer->len;

	while (spi->tx_len) {
		unsigned int fifo_max = min(spi->tx_len, spi->fifo_depth);

		mchp_corespi_write_fifo(spi, fifo_max);
		mchp_corespi_read_fifo(spi, fifo_max);
	}

	spi_finalize_current_transfer(host);
	return 1;
}

static int mchp_corespi_probe(struct platform_device *pdev)
{
	const char *protocol = "motorola";
	struct device *dev = &pdev->dev;
	struct spi_controller *host;
	struct mchp_corespi *spi;
	struct resource *res;
	u32 num_cs, mode, frame_size;
	bool assert_ssel;
	int ret = 0;

	host = devm_spi_alloc_host(dev, sizeof(*spi));
	if (!host)
		return -ENOMEM;

	platform_set_drvdata(pdev, host);

	if (of_property_read_u32(dev->of_node, "num-cs", &num_cs))
		num_cs = MCHP_CORESPI_MAX_CS;

	/*
	 * Protocol: CFG_MODE
	 * CoreSPI can be configured for Motorola, TI or NSC.
	 * The current driver supports only Motorola mode.
	 */
	ret = of_property_read_string(dev->of_node, "microchip,protocol-configuration",
				      &protocol);
	if (ret && ret != -EINVAL)
		return dev_err_probe(dev, ret, "Error reading protocol-configuration\n");
	if (strcmp(protocol, "motorola") != 0)
		return dev_err_probe(dev, -EINVAL,
				     "CoreSPI: protocol '%s' not supported by this driver\n",
				      protocol);

	/*
	 * Motorola mode (0-3): CFG_MOT_MODE
	 * Mode is fixed in the IP configurator.
	 */
	ret = of_property_read_u32(dev->of_node, "microchip,motorola-mode", &mode);
	if (ret)
		mode = MCHP_CORESPI_DEFAULT_MOTOROLA_MODE;
	else if (mode > 3)
		return dev_err_probe(dev, -EINVAL,
				     "invalid 'microchip,motorola-mode' value %u\n", mode);

	/*
	 * Frame size: CFG_FRAME_SIZE
	 * The hardware allows frame sizes <= APB data width.
	 * However, this driver currently only supports 8-bit frames.
	 */
	ret = of_property_read_u32(dev->of_node, "microchip,frame-size", &frame_size);
	if (!ret && frame_size != 8)
		return dev_err_probe(dev, -EINVAL,
				     "CoreSPI: frame size %u not supported by this driver\n",
				     frame_size);

	/*
	 * SSEL: CFG_MOT_SSEL
	 * CoreSPI deasserts SSEL when the TX FIFO empties.
	 * To prevent CS deassertion when TX FIFO drains, the ssel-active property
	 * keeps CS asserted for the full SPI transfer.
	 */
	assert_ssel = of_property_read_bool(dev->of_node, "microchip,ssel-active");
	if (!assert_ssel)
		return dev_err_probe(dev, -EINVAL,
				     "hardware must enable 'microchip,ssel-active' to keep CS asserted for the SPI transfer\n");

	spi = spi_controller_get_devdata(host);

	host->num_chipselect = num_cs;
	host->mode_bits = mode;
	host->setup = mchp_corespi_setup;
	host->use_gpio_descriptors = true;
	host->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
	host->transfer_one = mchp_corespi_transfer_one;
	host->set_cs = mchp_corespi_set_cs;

	ret = of_property_read_u32(dev->of_node, "fifo-depth", &spi->fifo_depth);
	if (ret)
		spi->fifo_depth = MCHP_CORESPI_DEFAULT_FIFO_DEPTH;

	spi->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(spi->regs))
		return PTR_ERR(spi->regs);

	spi->irq = platform_get_irq(pdev, 0);
	if (spi->irq < 0)
		return spi->irq;

	ret = devm_request_irq(dev, spi->irq, mchp_corespi_interrupt, IRQF_SHARED,
			       dev_name(dev), host);
	if (ret)
		return dev_err_probe(dev, ret, "could not request irq\n");

	spi->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(spi->clk))
		return dev_err_probe(dev, PTR_ERR(spi->clk), "could not get clk\n");

	mchp_corespi_init(host, spi);

	ret = devm_spi_register_controller(dev, host);
	if (ret) {
		mchp_corespi_disable_ints(spi);
		mchp_corespi_disable(spi);
		return dev_err_probe(dev, ret, "unable to register host for CoreSPI controller\n");
	}

	return 0;
}

static void mchp_corespi_remove(struct platform_device *pdev)
{
	struct spi_controller *host = platform_get_drvdata(pdev);
	struct mchp_corespi *spi = spi_controller_get_devdata(host);

	mchp_corespi_disable_ints(spi);
	mchp_corespi_disable(spi);
}

/*
 * Platform driver data structure
 */

#if defined(CONFIG_OF)
static const struct of_device_id mchp_corespi_dt_ids[] = {
	{ .compatible = "microchip,corespi-rtl-v5" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mchp_corespi_dt_ids);
#endif

static struct platform_driver mchp_corespi_driver = {
	.probe = mchp_corespi_probe,
	.driver = {
		.name = "microchip-corespi",
		.of_match_table = of_match_ptr(mchp_corespi_dt_ids),
	},
	.remove = mchp_corespi_remove,
};
module_platform_driver(mchp_corespi_driver);
MODULE_DESCRIPTION("Microchip CoreSPI controller driver");
MODULE_AUTHOR("Prajna Rajendra Kumar <prajna.rajendrakumar@microchip.com>");
MODULE_LICENSE("GPL");
