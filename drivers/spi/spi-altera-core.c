// SPDX-License-Identifier: GPL-2.0-only
/*
 * Altera SPI driver
 *
 * Copyright (C) 2008 Thomas Chou <thomas@wytron.com.tw>
 *
 * Based on spi_s3c24xx.c, which is:
 * Copyright (c) 2006 Ben Dooks
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/altera.h>
#include <linux/spi/spi.h>
#include <linux/io.h>
#include <linux/of.h>

#define DRV_NAME "spi_altera"

#define ALTERA_SPI_RXDATA	0
#define ALTERA_SPI_TXDATA	4
#define ALTERA_SPI_STATUS	8
#define ALTERA_SPI_CONTROL	12
#define ALTERA_SPI_TARGET_SEL	20

#define ALTERA_SPI_STATUS_ROE_MSK	0x8
#define ALTERA_SPI_STATUS_TOE_MSK	0x10
#define ALTERA_SPI_STATUS_TMT_MSK	0x20
#define ALTERA_SPI_STATUS_TRDY_MSK	0x40
#define ALTERA_SPI_STATUS_RRDY_MSK	0x80
#define ALTERA_SPI_STATUS_E_MSK		0x100

#define ALTERA_SPI_CONTROL_IROE_MSK	0x8
#define ALTERA_SPI_CONTROL_ITOE_MSK	0x10
#define ALTERA_SPI_CONTROL_ITRDY_MSK	0x40
#define ALTERA_SPI_CONTROL_IRRDY_MSK	0x80
#define ALTERA_SPI_CONTROL_IE_MSK	0x100
#define ALTERA_SPI_CONTROL_SSO_MSK	0x400

static int altr_spi_writel(struct altera_spi *hw, unsigned int reg,
			   unsigned int val)
{
	int ret;

	ret = regmap_write(hw->regmap, hw->regoff + reg, val);
	if (ret)
		dev_err(hw->dev, "fail to write reg 0x%x val 0x%x: %d\n",
			reg, val, ret);

	return ret;
}

static int altr_spi_readl(struct altera_spi *hw, unsigned int reg,
			  unsigned int *val)
{
	int ret;

	ret = regmap_read(hw->regmap, hw->regoff + reg, val);
	if (ret)
		dev_err(hw->dev, "fail to read reg 0x%x: %d\n", reg, ret);

	return ret;
}

static inline struct altera_spi *altera_spi_to_hw(struct spi_device *sdev)
{
	return spi_controller_get_devdata(sdev->controller);
}

static void altera_spi_set_cs(struct spi_device *spi, bool is_high)
{
	struct altera_spi *hw = altera_spi_to_hw(spi);

	if (is_high) {
		hw->imr &= ~ALTERA_SPI_CONTROL_SSO_MSK;
		altr_spi_writel(hw, ALTERA_SPI_CONTROL, hw->imr);
		altr_spi_writel(hw, ALTERA_SPI_TARGET_SEL, 0);
	} else {
		altr_spi_writel(hw, ALTERA_SPI_TARGET_SEL,
				BIT(spi->chip_select));
		hw->imr |= ALTERA_SPI_CONTROL_SSO_MSK;
		altr_spi_writel(hw, ALTERA_SPI_CONTROL, hw->imr);
	}
}

static void altera_spi_tx_word(struct altera_spi *hw)
{
	unsigned int txd = 0;

	if (hw->tx) {
		switch (hw->bytes_per_word) {
		case 1:
			txd = hw->tx[hw->count];
			break;
		case 2:
			txd = (hw->tx[hw->count * 2]
				| (hw->tx[hw->count * 2 + 1] << 8));
			break;
		case 4:
			txd = (hw->tx[hw->count * 4]
				| (hw->tx[hw->count * 4 + 1] << 8)
				| (hw->tx[hw->count * 4 + 2] << 16)
				| (hw->tx[hw->count * 4 + 3] << 24));
			break;

		}
	}

	altr_spi_writel(hw, ALTERA_SPI_TXDATA, txd);
}

static void altera_spi_rx_word(struct altera_spi *hw)
{
	unsigned int rxd;

	altr_spi_readl(hw, ALTERA_SPI_RXDATA, &rxd);
	if (hw->rx) {
		switch (hw->bytes_per_word) {
		case 1:
			hw->rx[hw->count] = rxd;
			break;
		case 2:
			hw->rx[hw->count * 2] = rxd;
			hw->rx[hw->count * 2 + 1] = rxd >> 8;
			break;
		case 4:
			hw->rx[hw->count * 4] = rxd;
			hw->rx[hw->count * 4 + 1] = rxd >> 8;
			hw->rx[hw->count * 4 + 2] = rxd >> 16;
			hw->rx[hw->count * 4 + 3] = rxd >> 24;
			break;

		}
	}

	hw->count++;
}

static int altera_spi_txrx(struct spi_controller *host,
	struct spi_device *spi, struct spi_transfer *t)
{
	struct altera_spi *hw = spi_controller_get_devdata(host);
	u32 val;

	hw->tx = t->tx_buf;
	hw->rx = t->rx_buf;
	hw->count = 0;
	hw->bytes_per_word = DIV_ROUND_UP(t->bits_per_word, 8);
	hw->len = t->len / hw->bytes_per_word;

	if (hw->irq >= 0) {
		/* enable receive interrupt */
		hw->imr |= ALTERA_SPI_CONTROL_IRRDY_MSK;
		altr_spi_writel(hw, ALTERA_SPI_CONTROL, hw->imr);

		/* send the first byte */
		altera_spi_tx_word(hw);

		return 1;
	}

	while (hw->count < hw->len) {
		altera_spi_tx_word(hw);

		for (;;) {
			altr_spi_readl(hw, ALTERA_SPI_STATUS, &val);
			if (val & ALTERA_SPI_STATUS_RRDY_MSK)
				break;

			cpu_relax();
		}

		altera_spi_rx_word(hw);
	}
	spi_finalize_current_transfer(host);

	return 0;
}

irqreturn_t altera_spi_irq(int irq, void *dev)
{
	struct spi_controller *host = dev;
	struct altera_spi *hw = spi_controller_get_devdata(host);

	altera_spi_rx_word(hw);

	if (hw->count < hw->len) {
		altera_spi_tx_word(hw);
	} else {
		/* disable receive interrupt */
		hw->imr &= ~ALTERA_SPI_CONTROL_IRRDY_MSK;
		altr_spi_writel(hw, ALTERA_SPI_CONTROL, hw->imr);

		spi_finalize_current_transfer(host);
	}

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(altera_spi_irq);

void altera_spi_init_host(struct spi_controller *host)
{
	struct altera_spi *hw = spi_controller_get_devdata(host);
	u32 val;

	host->transfer_one = altera_spi_txrx;
	host->set_cs = altera_spi_set_cs;

	/* program defaults into the registers */
	hw->imr = 0;		/* disable spi interrupts */
	altr_spi_writel(hw, ALTERA_SPI_CONTROL, hw->imr);
	altr_spi_writel(hw, ALTERA_SPI_STATUS, 0);	/* clear status reg */
	altr_spi_readl(hw, ALTERA_SPI_STATUS, &val);
	if (val & ALTERA_SPI_STATUS_RRDY_MSK)
		altr_spi_readl(hw, ALTERA_SPI_RXDATA, &val); /* flush rxdata */
}
EXPORT_SYMBOL_GPL(altera_spi_init_host);

MODULE_LICENSE("GPL");
