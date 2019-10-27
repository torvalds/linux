// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  CLPS711X SPI bus driver
 *
 *  Copyright (C) 2012-2016 Alexander Shiyan <shc_work@mail.ru>
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/clps711x.h>
#include <linux/spi/spi.h>

#define DRIVER_NAME		"clps711x-spi"

#define SYNCIO_FRMLEN(x)	((x) << 8)
#define SYNCIO_TXFRMEN		(1 << 14)

struct spi_clps711x_data {
	void __iomem		*syncio;
	struct regmap		*syscon;
	struct clk		*spi_clk;

	u8			*tx_buf;
	u8			*rx_buf;
	unsigned int		bpw;
	int			len;
};

static int spi_clps711x_prepare_message(struct spi_master *master,
					struct spi_message *msg)
{
	struct spi_clps711x_data *hw = spi_master_get_devdata(master);
	struct spi_device *spi = msg->spi;

	/* Setup mode for transfer */
	return regmap_update_bits(hw->syscon, SYSCON_OFFSET, SYSCON3_ADCCKNSEN,
				  (spi->mode & SPI_CPHA) ?
				  SYSCON3_ADCCKNSEN : 0);
}

static int spi_clps711x_transfer_one(struct spi_master *master,
				     struct spi_device *spi,
				     struct spi_transfer *xfer)
{
	struct spi_clps711x_data *hw = spi_master_get_devdata(master);
	u8 data;

	clk_set_rate(hw->spi_clk, xfer->speed_hz ? : spi->max_speed_hz);

	hw->len = xfer->len;
	hw->bpw = xfer->bits_per_word;
	hw->tx_buf = (u8 *)xfer->tx_buf;
	hw->rx_buf = (u8 *)xfer->rx_buf;

	/* Initiate transfer */
	data = hw->tx_buf ? *hw->tx_buf++ : 0;
	writel(data | SYNCIO_FRMLEN(hw->bpw) | SYNCIO_TXFRMEN, hw->syncio);

	return 1;
}

static irqreturn_t spi_clps711x_isr(int irq, void *dev_id)
{
	struct spi_master *master = dev_id;
	struct spi_clps711x_data *hw = spi_master_get_devdata(master);
	u8 data;

	/* Handle RX */
	data = readb(hw->syncio);
	if (hw->rx_buf)
		*hw->rx_buf++ = data;

	/* Handle TX */
	if (--hw->len > 0) {
		data = hw->tx_buf ? *hw->tx_buf++ : 0;
		writel(data | SYNCIO_FRMLEN(hw->bpw) | SYNCIO_TXFRMEN,
		       hw->syncio);
	} else
		spi_finalize_current_transfer(master);

	return IRQ_HANDLED;
}

static int spi_clps711x_probe(struct platform_device *pdev)
{
	struct spi_clps711x_data *hw;
	struct spi_master *master;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	master = spi_alloc_master(&pdev->dev, sizeof(*hw));
	if (!master)
		return -ENOMEM;

	master->use_gpio_descriptors = true;
	master->bus_num = -1;
	master->mode_bits = SPI_CPHA | SPI_CS_HIGH;
	master->bits_per_word_mask =  SPI_BPW_RANGE_MASK(1, 8);
	master->dev.of_node = pdev->dev.of_node;
	master->prepare_message = spi_clps711x_prepare_message;
	master->transfer_one = spi_clps711x_transfer_one;

	hw = spi_master_get_devdata(master);

	hw->spi_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(hw->spi_clk)) {
		ret = PTR_ERR(hw->spi_clk);
		goto err_out;
	}

	hw->syscon =
		syscon_regmap_lookup_by_compatible("cirrus,ep7209-syscon3");
	if (IS_ERR(hw->syscon)) {
		ret = PTR_ERR(hw->syscon);
		goto err_out;
	}

	hw->syncio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hw->syncio)) {
		ret = PTR_ERR(hw->syncio);
		goto err_out;
	}

	/* Disable extended mode due hardware problems */
	regmap_update_bits(hw->syscon, SYSCON_OFFSET, SYSCON3_ADCCON, 0);

	/* Clear possible pending interrupt */
	readl(hw->syncio);

	ret = devm_request_irq(&pdev->dev, irq, spi_clps711x_isr, 0,
			       dev_name(&pdev->dev), master);
	if (ret)
		goto err_out;

	ret = devm_spi_register_master(&pdev->dev, master);
	if (!ret)
		return 0;

err_out:
	spi_master_put(master);

	return ret;
}

static const struct of_device_id clps711x_spi_dt_ids[] = {
	{ .compatible = "cirrus,ep7209-spi", },
	{ }
};
MODULE_DEVICE_TABLE(of, clps711x_spi_dt_ids);

static struct platform_driver clps711x_spi_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.of_match_table = clps711x_spi_dt_ids,
	},
	.probe	= spi_clps711x_probe,
};
module_platform_driver(clps711x_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("CLPS711X SPI bus driver");
MODULE_ALIAS("platform:" DRIVER_NAME);
