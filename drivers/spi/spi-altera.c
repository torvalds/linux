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

#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/io.h>
#include <linux/of.h>

#define DRV_NAME "spi_altera"

#define ALTERA_SPI_RXDATA	0
#define ALTERA_SPI_TXDATA	4
#define ALTERA_SPI_STATUS	8
#define ALTERA_SPI_CONTROL	12
#define ALTERA_SPI_SLAVE_SEL	20

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

struct altera_spi {
	void __iomem *base;
	int irq;
	int len;
	int count;
	int bytes_per_word;
	unsigned long imr;

	/* data buffers */
	const unsigned char *tx;
	unsigned char *rx;
};

static inline struct altera_spi *altera_spi_to_hw(struct spi_device *sdev)
{
	return spi_master_get_devdata(sdev->master);
}

static void altera_spi_set_cs(struct spi_device *spi, bool is_high)
{
	struct altera_spi *hw = altera_spi_to_hw(spi);

	if (is_high) {
		hw->imr &= ~ALTERA_SPI_CONTROL_SSO_MSK;
		writel(hw->imr, hw->base + ALTERA_SPI_CONTROL);
		writel(0, hw->base + ALTERA_SPI_SLAVE_SEL);
	} else {
		writel(BIT(spi->chip_select), hw->base + ALTERA_SPI_SLAVE_SEL);
		hw->imr |= ALTERA_SPI_CONTROL_SSO_MSK;
		writel(hw->imr, hw->base + ALTERA_SPI_CONTROL);
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
		}
	}

	writel(txd, hw->base + ALTERA_SPI_TXDATA);
}

static void altera_spi_rx_word(struct altera_spi *hw)
{
	unsigned int rxd;

	rxd = readl(hw->base + ALTERA_SPI_RXDATA);
	if (hw->rx) {
		switch (hw->bytes_per_word) {
		case 1:
			hw->rx[hw->count] = rxd;
			break;
		case 2:
			hw->rx[hw->count * 2] = rxd;
			hw->rx[hw->count * 2 + 1] = rxd >> 8;
			break;
		}
	}

	hw->count++;
}

static int altera_spi_txrx(struct spi_master *master,
	struct spi_device *spi, struct spi_transfer *t)
{
	struct altera_spi *hw = spi_master_get_devdata(master);

	hw->tx = t->tx_buf;
	hw->rx = t->rx_buf;
	hw->count = 0;
	hw->bytes_per_word = DIV_ROUND_UP(t->bits_per_word, 8);
	hw->len = t->len / hw->bytes_per_word;

	if (hw->irq >= 0) {
		/* enable receive interrupt */
		hw->imr |= ALTERA_SPI_CONTROL_IRRDY_MSK;
		writel(hw->imr, hw->base + ALTERA_SPI_CONTROL);

		/* send the first byte */
		altera_spi_tx_word(hw);
	} else {
		while (hw->count < hw->len) {
			altera_spi_tx_word(hw);

			while (!(readl(hw->base + ALTERA_SPI_STATUS) &
				 ALTERA_SPI_STATUS_RRDY_MSK))
				cpu_relax();

			altera_spi_rx_word(hw);
		}
		spi_finalize_current_transfer(master);
	}

	return t->len;
}

static irqreturn_t altera_spi_irq(int irq, void *dev)
{
	struct spi_master *master = dev;
	struct altera_spi *hw = spi_master_get_devdata(master);

	altera_spi_rx_word(hw);

	if (hw->count < hw->len) {
		altera_spi_tx_word(hw);
	} else {
		/* disable receive interrupt */
		hw->imr &= ~ALTERA_SPI_CONTROL_IRRDY_MSK;
		writel(hw->imr, hw->base + ALTERA_SPI_CONTROL);

		spi_finalize_current_transfer(master);
	}

	return IRQ_HANDLED;
}

static int altera_spi_probe(struct platform_device *pdev)
{
	struct altera_spi *hw;
	struct spi_master *master;
	int err = -ENODEV;

	master = spi_alloc_master(&pdev->dev, sizeof(struct altera_spi));
	if (!master)
		return err;

	/* setup the master state. */
	master->bus_num = pdev->id;
	master->num_chipselect = 16;
	master->mode_bits = SPI_CS_HIGH;
	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 16);
	master->dev.of_node = pdev->dev.of_node;
	master->transfer_one = altera_spi_txrx;
	master->set_cs = altera_spi_set_cs;

	hw = spi_master_get_devdata(master);

	/* find and map our resources */
	hw->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hw->base)) {
		err = PTR_ERR(hw->base);
		goto exit;
	}
	/* program defaults into the registers */
	hw->imr = 0;		/* disable spi interrupts */
	writel(hw->imr, hw->base + ALTERA_SPI_CONTROL);
	writel(0, hw->base + ALTERA_SPI_STATUS);	/* clear status reg */
	if (readl(hw->base + ALTERA_SPI_STATUS) & ALTERA_SPI_STATUS_RRDY_MSK)
		readl(hw->base + ALTERA_SPI_RXDATA);	/* flush rxdata */
	/* irq is optional */
	hw->irq = platform_get_irq(pdev, 0);
	if (hw->irq >= 0) {
		err = devm_request_irq(&pdev->dev, hw->irq, altera_spi_irq, 0,
				       pdev->name, master);
		if (err)
			goto exit;
	}

	err = devm_spi_register_master(&pdev->dev, master);
	if (err)
		goto exit;
	dev_info(&pdev->dev, "base %p, irq %d\n", hw->base, hw->irq);

	return 0;
exit:
	spi_master_put(master);
	return err;
}

#ifdef CONFIG_OF
static const struct of_device_id altera_spi_match[] = {
	{ .compatible = "ALTR,spi-1.0", },
	{ .compatible = "altr,spi-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, altera_spi_match);
#endif /* CONFIG_OF */

static struct platform_driver altera_spi_driver = {
	.probe = altera_spi_probe,
	.driver = {
		.name = DRV_NAME,
		.pm = NULL,
		.of_match_table = of_match_ptr(altera_spi_match),
	},
};
module_platform_driver(altera_spi_driver);

MODULE_DESCRIPTION("Altera SPI driver");
MODULE_AUTHOR("Thomas Chou <thomas@wytron.com.tw>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
