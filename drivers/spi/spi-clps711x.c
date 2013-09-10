/*
 *  CLPS711X SPI bus driver
 *
 *  Copyright (C) 2012 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/platform_data/spi-clps711x.h>

#include <mach/hardware.h>

#define DRIVER_NAME	"spi-clps711x"

struct spi_clps711x_data {
	struct completion	done;

	struct clk		*spi_clk;
	u32			max_speed_hz;

	u8			*tx_buf;
	u8			*rx_buf;
	int			count;
	int			len;

	int			chipselect[0];
};

static int spi_clps711x_setup(struct spi_device *spi)
{
	struct spi_clps711x_data *hw = spi_master_get_devdata(spi->master);

	/* We are expect that SPI-device is not selected */
	gpio_direction_output(hw->chipselect[spi->chip_select],
			      !(spi->mode & SPI_CS_HIGH));

	return 0;
}

static void spi_clps711x_setup_mode(struct spi_device *spi)
{
	/* Setup edge for transfer */
	if (spi->mode & SPI_CPHA)
		clps_writew(clps_readw(SYSCON3) | SYSCON3_ADCCKNSEN, SYSCON3);
	else
		clps_writew(clps_readw(SYSCON3) & ~SYSCON3_ADCCKNSEN, SYSCON3);
}

static int spi_clps711x_setup_xfer(struct spi_device *spi,
				   struct spi_transfer *xfer)
{
	u32 speed = xfer->speed_hz ? : spi->max_speed_hz;
	u8 bpw = xfer->bits_per_word;
	struct spi_clps711x_data *hw = spi_master_get_devdata(spi->master);

	if (bpw != 8) {
		dev_err(&spi->dev, "Unsupported master bus width %i\n", bpw);
		return -EINVAL;
	}

	/* Setup SPI frequency divider */
	if (!speed || (speed >= hw->max_speed_hz))
		clps_writel((clps_readl(SYSCON1) & ~SYSCON1_ADCKSEL_MASK) |
			    SYSCON1_ADCKSEL(3), SYSCON1);
	else if (speed >= (hw->max_speed_hz / 2))
		clps_writel((clps_readl(SYSCON1) & ~SYSCON1_ADCKSEL_MASK) |
			    SYSCON1_ADCKSEL(2), SYSCON1);
	else if (speed >= (hw->max_speed_hz / 8))
		clps_writel((clps_readl(SYSCON1) & ~SYSCON1_ADCKSEL_MASK) |
			    SYSCON1_ADCKSEL(1), SYSCON1);
	else
		clps_writel((clps_readl(SYSCON1) & ~SYSCON1_ADCKSEL_MASK) |
			    SYSCON1_ADCKSEL(0), SYSCON1);

	return 0;
}

static int spi_clps711x_transfer_one_message(struct spi_master *master,
					     struct spi_message *msg)
{
	struct spi_clps711x_data *hw = spi_master_get_devdata(master);
	struct spi_transfer *xfer;
	int status = 0, cs = hw->chipselect[msg->spi->chip_select];
	u32 data;

	spi_clps711x_setup_mode(msg->spi);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (spi_clps711x_setup_xfer(msg->spi, xfer)) {
			status = -EINVAL;
			goto out_xfr;
		}

		gpio_set_value(cs, !!(msg->spi->mode & SPI_CS_HIGH));

		INIT_COMPLETION(hw->done);

		hw->count = 0;
		hw->len = xfer->len;
		hw->tx_buf = (u8 *)xfer->tx_buf;
		hw->rx_buf = (u8 *)xfer->rx_buf;

		/* Initiate transfer */
		data = hw->tx_buf ? hw->tx_buf[hw->count] : 0;
		clps_writel(data | SYNCIO_FRMLEN(8) | SYNCIO_TXFRMEN, SYNCIO);

		wait_for_completion(&hw->done);

		if (xfer->delay_usecs)
			udelay(xfer->delay_usecs);

		if (xfer->cs_change ||
		    list_is_last(&xfer->transfer_list, &msg->transfers))
			gpio_set_value(cs, !(msg->spi->mode & SPI_CS_HIGH));

		msg->actual_length += xfer->len;
	}

out_xfr:
	msg->status = status;
	spi_finalize_current_message(master);

	return 0;
}

static irqreturn_t spi_clps711x_isr(int irq, void *dev_id)
{
	struct spi_clps711x_data *hw = (struct spi_clps711x_data *)dev_id;
	u32 data;

	/* Handle RX */
	data = clps_readb(SYNCIO);
	if (hw->rx_buf)
		hw->rx_buf[hw->count] = (u8)data;

	hw->count++;

	/* Handle TX */
	if (hw->count < hw->len) {
		data = hw->tx_buf ? hw->tx_buf[hw->count] : 0;
		clps_writel(data | SYNCIO_FRMLEN(8) | SYNCIO_TXFRMEN, SYNCIO);
	} else
		complete(&hw->done);

	return IRQ_HANDLED;
}

static int spi_clps711x_probe(struct platform_device *pdev)
{
	int i, ret;
	struct spi_master *master;
	struct spi_clps711x_data *hw;
	struct spi_clps711x_pdata *pdata = dev_get_platdata(&pdev->dev);

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data supplied\n");
		return -EINVAL;
	}

	if (pdata->num_chipselect < 1) {
		dev_err(&pdev->dev, "At least one CS must be defined\n");
		return -EINVAL;
	}

	master = spi_alloc_master(&pdev->dev,
				  sizeof(struct spi_clps711x_data) +
				  sizeof(int) * pdata->num_chipselect);
	if (!master) {
		dev_err(&pdev->dev, "SPI allocating memory error\n");
		return -ENOMEM;
	}

	master->bus_num = pdev->id;
	master->mode_bits = SPI_CPHA | SPI_CS_HIGH;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->num_chipselect = pdata->num_chipselect;
	master->setup = spi_clps711x_setup;
	master->transfer_one_message = spi_clps711x_transfer_one_message;

	hw = spi_master_get_devdata(master);

	for (i = 0; i < master->num_chipselect; i++) {
		hw->chipselect[i] = pdata->chipselect[i];
		if (!gpio_is_valid(hw->chipselect[i])) {
			dev_err(&pdev->dev, "Invalid CS GPIO %i\n", i);
			ret = -EINVAL;
			goto err_out;
		}
		if (gpio_request(hw->chipselect[i], DRIVER_NAME)) {
			dev_err(&pdev->dev, "Can't get CS GPIO %i\n", i);
			ret = -EINVAL;
			goto err_out;
		}
	}

	hw->spi_clk = devm_clk_get(&pdev->dev, "spi");
	if (IS_ERR(hw->spi_clk)) {
		dev_err(&pdev->dev, "Can't get clocks\n");
		ret = PTR_ERR(hw->spi_clk);
		goto err_out;
	}
	hw->max_speed_hz = clk_get_rate(hw->spi_clk);

	init_completion(&hw->done);
	platform_set_drvdata(pdev, master);

	/* Disable extended mode due hardware problems */
	clps_writew(clps_readw(SYSCON3) & ~SYSCON3_ADCCON, SYSCON3);

	/* Clear possible pending interrupt */
	clps_readl(SYNCIO);

	ret = devm_request_irq(&pdev->dev, IRQ_SSEOTI, spi_clps711x_isr, 0,
			       dev_name(&pdev->dev), hw);
	if (ret) {
		dev_err(&pdev->dev, "Can't request IRQ\n");
		clk_put(hw->spi_clk);
		goto clk_out;
	}

	ret = spi_register_master(master);
	if (!ret) {
		dev_info(&pdev->dev,
			 "SPI bus driver initialized. Master clock %u Hz\n",
			 hw->max_speed_hz);
		return 0;
	}

	dev_err(&pdev->dev, "Failed to register master\n");

clk_out:
err_out:
	while (--i >= 0)
		if (gpio_is_valid(hw->chipselect[i]))
			gpio_free(hw->chipselect[i]);

	spi_master_put(master);

	return ret;
}

static int spi_clps711x_remove(struct platform_device *pdev)
{
	int i;
	struct spi_master *master = platform_get_drvdata(pdev);
	struct spi_clps711x_data *hw = spi_master_get_devdata(master);

	for (i = 0; i < master->num_chipselect; i++)
		if (gpio_is_valid(hw->chipselect[i]))
			gpio_free(hw->chipselect[i]);

	spi_unregister_master(master);

	return 0;
}

static struct platform_driver clps711x_spi_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
	.probe	= spi_clps711x_probe,
	.remove	= spi_clps711x_remove,
};
module_platform_driver(clps711x_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shiyan <shc_work@mail.ru>");
MODULE_DESCRIPTION("CLPS711X SPI bus driver");
