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
#include <linux/spi/altera.h>
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

#define ALTERA_SPI_MAX_CS		32

enum altera_spi_type {
	ALTERA_SPI_TYPE_UNKNOWN,
	ALTERA_SPI_TYPE_SUBDEV,
};

struct altera_spi {
	int irq;
	int len;
	int count;
	int bytes_per_word;
	u32 imr;

	/* data buffers */
	const unsigned char *tx;
	unsigned char *rx;

	struct regmap *regmap;
	u32 regoff;
	struct device *dev;
};

static const struct regmap_config spi_altera_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
};

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
	return spi_master_get_devdata(sdev->master);
}

static void altera_spi_set_cs(struct spi_device *spi, bool is_high)
{
	struct altera_spi *hw = altera_spi_to_hw(spi);

	if (is_high) {
		hw->imr &= ~ALTERA_SPI_CONTROL_SSO_MSK;
		altr_spi_writel(hw, ALTERA_SPI_CONTROL, hw->imr);
		altr_spi_writel(hw, ALTERA_SPI_SLAVE_SEL, 0);
	} else {
		altr_spi_writel(hw, ALTERA_SPI_SLAVE_SEL,
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

static int altera_spi_txrx(struct spi_master *master,
	struct spi_device *spi, struct spi_transfer *t)
{
	struct altera_spi *hw = spi_master_get_devdata(master);
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
	spi_finalize_current_transfer(master);

	return 0;
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
		altr_spi_writel(hw, ALTERA_SPI_CONTROL, hw->imr);

		spi_finalize_current_transfer(master);
	}

	return IRQ_HANDLED;
}

static int altera_spi_probe(struct platform_device *pdev)
{
	const struct platform_device_id *platid = platform_get_device_id(pdev);
	struct altera_spi_platform_data *pdata = dev_get_platdata(&pdev->dev);
	enum altera_spi_type type = ALTERA_SPI_TYPE_UNKNOWN;
	struct altera_spi *hw;
	struct spi_master *master;
	int err = -ENODEV;
	u32 val;
	u16 i;

	master = spi_alloc_master(&pdev->dev, sizeof(struct altera_spi));
	if (!master)
		return err;

	/* setup the master state. */
	master->bus_num = pdev->id;

	if (pdata) {
		if (pdata->num_chipselect > ALTERA_SPI_MAX_CS) {
			dev_err(&pdev->dev,
				"Invalid number of chipselect: %hu\n",
				pdata->num_chipselect);
			return -EINVAL;
		}

		master->num_chipselect = pdata->num_chipselect;
		master->mode_bits = pdata->mode_bits;
		master->bits_per_word_mask = pdata->bits_per_word_mask;
	} else {
		master->num_chipselect = 16;
		master->mode_bits = SPI_CS_HIGH;
		master->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 16);
	}

	master->dev.of_node = pdev->dev.of_node;
	master->transfer_one = altera_spi_txrx;
	master->set_cs = altera_spi_set_cs;

	hw = spi_master_get_devdata(master);
	hw->dev = &pdev->dev;

	if (platid)
		type = platid->driver_data;

	/* find and map our resources */
	if (type == ALTERA_SPI_TYPE_SUBDEV) {
		struct resource *regoff;

		hw->regmap = dev_get_regmap(pdev->dev.parent, NULL);
		if (!hw->regmap) {
			dev_err(&pdev->dev, "get regmap failed\n");
			goto exit;
		}

		regoff = platform_get_resource(pdev, IORESOURCE_REG, 0);
		if (regoff)
			hw->regoff = regoff->start;
	} else {
		void __iomem *res;

		res = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(res)) {
			err = PTR_ERR(res);
			goto exit;
		}

		hw->regmap = devm_regmap_init_mmio(&pdev->dev, res,
						   &spi_altera_config);
		if (IS_ERR(hw->regmap)) {
			dev_err(&pdev->dev, "regmap mmio init failed\n");
			err = PTR_ERR(hw->regmap);
			goto exit;
		}
	}

	/* program defaults into the registers */
	hw->imr = 0;		/* disable spi interrupts */
	altr_spi_writel(hw, ALTERA_SPI_CONTROL, hw->imr);
	altr_spi_writel(hw, ALTERA_SPI_STATUS, 0);	/* clear status reg */
	altr_spi_readl(hw, ALTERA_SPI_STATUS, &val);
	if (val & ALTERA_SPI_STATUS_RRDY_MSK)
		altr_spi_readl(hw, ALTERA_SPI_RXDATA, &val); /* flush rxdata */
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

	if (pdata) {
		for (i = 0; i < pdata->num_devices; i++) {
			if (!spi_new_device(master, pdata->devices + i))
				dev_warn(&pdev->dev,
					 "unable to create SPI device: %s\n",
					 pdata->devices[i].modalias);
		}
	}

	dev_info(&pdev->dev, "regoff %u, irq %d\n", hw->regoff, hw->irq);

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

static const struct platform_device_id altera_spi_ids[] = {
	{ DRV_NAME,		ALTERA_SPI_TYPE_UNKNOWN },
	{ "subdev_spi_altera",	ALTERA_SPI_TYPE_SUBDEV },
	{ }
};
MODULE_DEVICE_TABLE(platform, altera_spi_ids);

static struct platform_driver altera_spi_driver = {
	.probe = altera_spi_probe,
	.driver = {
		.name = DRV_NAME,
		.pm = NULL,
		.of_match_table = of_match_ptr(altera_spi_match),
	},
	.id_table	= altera_spi_ids,
};
module_platform_driver(altera_spi_driver);

MODULE_DESCRIPTION("Altera SPI driver");
MODULE_AUTHOR("Thomas Chou <thomas@wytron.com.tw>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
