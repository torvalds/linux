// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for AT91 USART
 *
 * Copyright (C) 2018 Microchip Technology
 *
 * Author: Radu Pirea <radu.pirea@microchip.com>
 *
 */

#include <dt-bindings/mfd/at91-usart.h>

#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/of.h>
#include <linux/property.h>

static struct mfd_cell at91_usart_spi_subdev = {
		.name = "at91_usart_spi",
		.of_compatible = "microchip,at91sam9g45-usart-spi",
	};

static struct mfd_cell at91_usart_serial_subdev = {
		.name = "atmel_usart_serial",
		.of_compatible = "atmel,at91rm9200-usart-serial",
	};

static int at91_usart_mode_probe(struct platform_device *pdev)
{
	struct mfd_cell cell;
	u32 opmode = AT91_USART_MODE_SERIAL;

	device_property_read_u32(&pdev->dev, "atmel,usart-mode", &opmode);

	switch (opmode) {
	case AT91_USART_MODE_SPI:
		cell = at91_usart_spi_subdev;
		break;
	case AT91_USART_MODE_SERIAL:
		cell = at91_usart_serial_subdev;
		break;
	default:
		dev_err(&pdev->dev, "atmel,usart-mode has an invalid value %u\n",
			opmode);
		return -EINVAL;
	}

	return devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO, &cell, 1,
			      NULL, 0, NULL);
}

static const struct of_device_id at91_usart_mode_of_match[] = {
	{ .compatible = "atmel,at91rm9200-usart" },
	{ .compatible = "atmel,at91sam9260-usart" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, at91_usart_mode_of_match);

static struct platform_driver at91_usart_mfd = {
	.probe	= at91_usart_mode_probe,
	.driver	= {
		.name		= "at91_usart_mode",
		.of_match_table	= at91_usart_mode_of_match,
	},
};

module_platform_driver(at91_usart_mfd);

MODULE_AUTHOR("Radu Pirea <radu.pirea@microchip.com>");
MODULE_DESCRIPTION("AT91 USART MFD driver");
MODULE_LICENSE("GPL v2");
