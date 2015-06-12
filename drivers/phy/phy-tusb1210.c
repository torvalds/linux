/**
 * tusb1210.c - TUSB1210 USB ULPI PHY driver
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/ulpi/driver.h>
#include <linux/gpio/consumer.h>

#include "ulpi_phy.h"

#define TUSB1210_VENDOR_SPECIFIC2		0x80
#define TUSB1210_VENDOR_SPECIFIC2_IHSTX_SHIFT	0
#define TUSB1210_VENDOR_SPECIFIC2_ZHSDRV_SHIFT	4
#define TUSB1210_VENDOR_SPECIFIC2_DP_SHIFT	6

struct tusb1210 {
	struct ulpi *ulpi;
	struct phy *phy;
	struct gpio_desc *gpio_reset;
	struct gpio_desc *gpio_cs;
	u8 vendor_specific2;
};

static int tusb1210_power_on(struct phy *phy)
{
	struct tusb1210 *tusb = phy_get_drvdata(phy);

	gpiod_set_value_cansleep(tusb->gpio_reset, 1);
	gpiod_set_value_cansleep(tusb->gpio_cs, 1);

	/* Restore the optional eye diagram optimization value */
	if (tusb->vendor_specific2)
		ulpi_write(tusb->ulpi, TUSB1210_VENDOR_SPECIFIC2,
			   tusb->vendor_specific2);

	return 0;
}

static int tusb1210_power_off(struct phy *phy)
{
	struct tusb1210 *tusb = phy_get_drvdata(phy);

	gpiod_set_value_cansleep(tusb->gpio_reset, 0);
	gpiod_set_value_cansleep(tusb->gpio_cs, 0);

	return 0;
}

static struct phy_ops phy_ops = {
	.power_on = tusb1210_power_on,
	.power_off = tusb1210_power_off,
	.owner = THIS_MODULE,
};

static int tusb1210_probe(struct ulpi *ulpi)
{
	struct tusb1210 *tusb;
	u8 val, reg;

	tusb = devm_kzalloc(&ulpi->dev, sizeof(*tusb), GFP_KERNEL);
	if (!tusb)
		return -ENOMEM;

	tusb->gpio_reset = devm_gpiod_get_optional(&ulpi->dev, "reset",
						   GPIOD_OUT_LOW);
	if (IS_ERR(tusb->gpio_reset))
		return PTR_ERR(tusb->gpio_reset);

	gpiod_set_value_cansleep(tusb->gpio_reset, 1);

	tusb->gpio_cs = devm_gpiod_get_optional(&ulpi->dev, "cs",
						GPIOD_OUT_LOW);
	if (IS_ERR(tusb->gpio_cs))
		return PTR_ERR(tusb->gpio_cs);

	gpiod_set_value_cansleep(tusb->gpio_cs, 1);

	/*
	 * VENDOR_SPECIFIC2 register in TUSB1210 can be used for configuring eye
	 * diagram optimization and DP/DM swap.
	 */

	/* High speed output drive strength configuration */
	device_property_read_u8(&ulpi->dev, "ihstx", &val);
	reg = val << TUSB1210_VENDOR_SPECIFIC2_IHSTX_SHIFT;

	/* High speed output impedance configuration */
	device_property_read_u8(&ulpi->dev, "zhsdrv", &val);
	reg |= val << TUSB1210_VENDOR_SPECIFIC2_ZHSDRV_SHIFT;

	/* DP/DM swap control */
	device_property_read_u8(&ulpi->dev, "datapolarity", &val);
	reg |= val << TUSB1210_VENDOR_SPECIFIC2_DP_SHIFT;

	if (reg) {
		ulpi_write(ulpi, TUSB1210_VENDOR_SPECIFIC2, reg);
		tusb->vendor_specific2 = reg;
	}

	tusb->phy = ulpi_phy_create(ulpi, &phy_ops);
	if (IS_ERR(tusb->phy))
		return PTR_ERR(tusb->phy);

	tusb->ulpi = ulpi;

	phy_set_drvdata(tusb->phy, tusb);
	ulpi_set_drvdata(ulpi, tusb);
	return 0;
}

static void tusb1210_remove(struct ulpi *ulpi)
{
	struct tusb1210 *tusb = ulpi_get_drvdata(ulpi);

	ulpi_phy_destroy(ulpi, tusb->phy);
}

#define TI_VENDOR_ID 0x0451

static const struct ulpi_device_id tusb1210_ulpi_id[] = {
	{ TI_VENDOR_ID, 0x1507, },
	{ },
};
MODULE_DEVICE_TABLE(ulpi, tusb1210_ulpi_id);

static struct ulpi_driver tusb1210_driver = {
	.id_table = tusb1210_ulpi_id,
	.probe = tusb1210_probe,
	.remove = tusb1210_remove,
	.driver = {
		.name = "tusb1210",
		.owner = THIS_MODULE,
	},
};

module_ulpi_driver(tusb1210_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TUSB1210 ULPI PHY driver");
