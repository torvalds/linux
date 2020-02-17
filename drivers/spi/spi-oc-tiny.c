// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenCores tiny SPI master driver
 *
 * http://opencores.org/project,tiny_spi
 *
 * Copyright (C) 2011 Thomas Chou <thomas@wytron.com.tw>
 *
 * Based on spi_s3c24xx.c, which is:
 * Copyright (c) 2006 Ben Dooks
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 */

#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/spi/spi_oc_tiny.h>
#include <linux/io.h>
#include <linux/of.h>

#define DRV_NAME "spi_oc_tiny"

#define TINY_SPI_RXDATA 0
#define TINY_SPI_TXDATA 4
#define TINY_SPI_STATUS 8
#define TINY_SPI_CONTROL 12
#define TINY_SPI_BAUD 16

#define TINY_SPI_STATUS_TXE 0x1
#define TINY_SPI_STATUS_TXR 0x2

struct tiny_spi {
	/* bitbang has to be first */
	struct spi_bitbang bitbang;
	struct completion done;

	void __iomem *base;
	int irq;
	unsigned int freq;
	unsigned int baudwidth;
	unsigned int baud;
	unsigned int speed_hz;
	unsigned int mode;
	unsigned int len;
	unsigned int txc, rxc;
	const u8 *txp;
	u8 *rxp;
};

static inline struct tiny_spi *tiny_spi_to_hw(struct spi_device *sdev)
{
	return spi_master_get_devdata(sdev->master);
}

static unsigned int tiny_spi_baud(struct spi_device *spi, unsigned int hz)
{
	struct tiny_spi *hw = tiny_spi_to_hw(spi);

	return min(DIV_ROUND_UP(hw->freq, hz * 2), (1U << hw->baudwidth)) - 1;
}

static int tiny_spi_setup_transfer(struct spi_device *spi,
				   struct spi_transfer *t)
{
	struct tiny_spi *hw = tiny_spi_to_hw(spi);
	unsigned int baud = hw->baud;

	if (t) {
		if (t->speed_hz && t->speed_hz != hw->speed_hz)
			baud = tiny_spi_baud(spi, t->speed_hz);
	}
	writel(baud, hw->base + TINY_SPI_BAUD);
	writel(hw->mode, hw->base + TINY_SPI_CONTROL);
	return 0;
}

static int tiny_spi_setup(struct spi_device *spi)
{
	struct tiny_spi *hw = tiny_spi_to_hw(spi);

	if (spi->max_speed_hz != hw->speed_hz) {
		hw->speed_hz = spi->max_speed_hz;
		hw->baud = tiny_spi_baud(spi, hw->speed_hz);
	}
	hw->mode = spi->mode & (SPI_CPOL | SPI_CPHA);
	return 0;
}

static inline void tiny_spi_wait_txr(struct tiny_spi *hw)
{
	while (!(readb(hw->base + TINY_SPI_STATUS) &
		 TINY_SPI_STATUS_TXR))
		cpu_relax();
}

static inline void tiny_spi_wait_txe(struct tiny_spi *hw)
{
	while (!(readb(hw->base + TINY_SPI_STATUS) &
		 TINY_SPI_STATUS_TXE))
		cpu_relax();
}

static int tiny_spi_txrx_bufs(struct spi_device *spi, struct spi_transfer *t)
{
	struct tiny_spi *hw = tiny_spi_to_hw(spi);
	const u8 *txp = t->tx_buf;
	u8 *rxp = t->rx_buf;
	unsigned int i;

	if (hw->irq >= 0) {
		/* use interrupt driven data transfer */
		hw->len = t->len;
		hw->txp = t->tx_buf;
		hw->rxp = t->rx_buf;
		hw->txc = 0;
		hw->rxc = 0;

		/* send the first byte */
		if (t->len > 1) {
			writeb(hw->txp ? *hw->txp++ : 0,
			       hw->base + TINY_SPI_TXDATA);
			hw->txc++;
			writeb(hw->txp ? *hw->txp++ : 0,
			       hw->base + TINY_SPI_TXDATA);
			hw->txc++;
			writeb(TINY_SPI_STATUS_TXR, hw->base + TINY_SPI_STATUS);
		} else {
			writeb(hw->txp ? *hw->txp++ : 0,
			       hw->base + TINY_SPI_TXDATA);
			hw->txc++;
			writeb(TINY_SPI_STATUS_TXE, hw->base + TINY_SPI_STATUS);
		}

		wait_for_completion(&hw->done);
	} else {
		/* we need to tighten the transfer loop */
		writeb(txp ? *txp++ : 0, hw->base + TINY_SPI_TXDATA);
		for (i = 1; i < t->len; i++) {
			writeb(txp ? *txp++ : 0, hw->base + TINY_SPI_TXDATA);

			if (rxp || (i != t->len - 1))
				tiny_spi_wait_txr(hw);
			if (rxp)
				*rxp++ = readb(hw->base + TINY_SPI_TXDATA);
		}
		tiny_spi_wait_txe(hw);
		if (rxp)
			*rxp++ = readb(hw->base + TINY_SPI_RXDATA);
	}

	return t->len;
}

static irqreturn_t tiny_spi_irq(int irq, void *dev)
{
	struct tiny_spi *hw = dev;

	writeb(0, hw->base + TINY_SPI_STATUS);
	if (hw->rxc + 1 == hw->len) {
		if (hw->rxp)
			*hw->rxp++ = readb(hw->base + TINY_SPI_RXDATA);
		hw->rxc++;
		complete(&hw->done);
	} else {
		if (hw->rxp)
			*hw->rxp++ = readb(hw->base + TINY_SPI_TXDATA);
		hw->rxc++;
		if (hw->txc < hw->len) {
			writeb(hw->txp ? *hw->txp++ : 0,
			       hw->base + TINY_SPI_TXDATA);
			hw->txc++;
			writeb(TINY_SPI_STATUS_TXR,
			       hw->base + TINY_SPI_STATUS);
		} else {
			writeb(TINY_SPI_STATUS_TXE,
			       hw->base + TINY_SPI_STATUS);
		}
	}
	return IRQ_HANDLED;
}

#ifdef CONFIG_OF
#include <linux/of_gpio.h>

static int tiny_spi_of_probe(struct platform_device *pdev)
{
	struct tiny_spi *hw = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	u32 val;

	if (!np)
		return 0;
	hw->bitbang.master->dev.of_node = pdev->dev.of_node;
	if (!of_property_read_u32(np, "clock-frequency", &val))
		hw->freq = val;
	if (!of_property_read_u32(np, "baud-width", &val))
		hw->baudwidth = val;
	return 0;
}
#else /* !CONFIG_OF */
static int tiny_spi_of_probe(struct platform_device *pdev)
{
	return 0;
}
#endif /* CONFIG_OF */

static int tiny_spi_probe(struct platform_device *pdev)
{
	struct tiny_spi_platform_data *platp = dev_get_platdata(&pdev->dev);
	struct tiny_spi *hw;
	struct spi_master *master;
	int err = -ENODEV;

	master = spi_alloc_master(&pdev->dev, sizeof(struct tiny_spi));
	if (!master)
		return err;

	/* setup the master state. */
	master->bus_num = pdev->id;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->setup = tiny_spi_setup;
	master->use_gpio_descriptors = true;

	hw = spi_master_get_devdata(master);
	platform_set_drvdata(pdev, hw);

	/* setup the state for the bitbang driver */
	hw->bitbang.master = master;
	hw->bitbang.setup_transfer = tiny_spi_setup_transfer;
	hw->bitbang.txrx_bufs = tiny_spi_txrx_bufs;

	/* find and map our resources */
	hw->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hw->base)) {
		err = PTR_ERR(hw->base);
		goto exit;
	}
	/* irq is optional */
	hw->irq = platform_get_irq(pdev, 0);
	if (hw->irq >= 0) {
		init_completion(&hw->done);
		err = devm_request_irq(&pdev->dev, hw->irq, tiny_spi_irq, 0,
				       pdev->name, hw);
		if (err)
			goto exit;
	}
	/* find platform data */
	if (platp) {
		hw->freq = platp->freq;
		hw->baudwidth = platp->baudwidth;
	} else {
		err = tiny_spi_of_probe(pdev);
		if (err)
			goto exit;
	}

	/* register our spi controller */
	err = spi_bitbang_start(&hw->bitbang);
	if (err)
		goto exit;
	dev_info(&pdev->dev, "base %p, irq %d\n", hw->base, hw->irq);

	return 0;

exit:
	spi_master_put(master);
	return err;
}

static int tiny_spi_remove(struct platform_device *pdev)
{
	struct tiny_spi *hw = platform_get_drvdata(pdev);
	struct spi_master *master = hw->bitbang.master;

	spi_bitbang_stop(&hw->bitbang);
	spi_master_put(master);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id tiny_spi_match[] = {
	{ .compatible = "opencores,tiny-spi-rtlsvn2", },
	{},
};
MODULE_DEVICE_TABLE(of, tiny_spi_match);
#endif /* CONFIG_OF */

static struct platform_driver tiny_spi_driver = {
	.probe = tiny_spi_probe,
	.remove = tiny_spi_remove,
	.driver = {
		.name = DRV_NAME,
		.pm = NULL,
		.of_match_table = of_match_ptr(tiny_spi_match),
	},
};
module_platform_driver(tiny_spi_driver);

MODULE_DESCRIPTION("OpenCores tiny SPI driver");
MODULE_AUTHOR("Thomas Chou <thomas@wytron.com.tw>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
