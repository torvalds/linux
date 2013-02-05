/*
 * LP5521/LP5523/LP55231 Common Driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from leds-lp5521.c, leds-lp5523.c
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_data/leds-lp55xx.h>

#include "leds-lp55xx-common.h"

int lp55xx_write(struct lp55xx_chip *chip, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(chip->cl, reg, val);
}
EXPORT_SYMBOL_GPL(lp55xx_write);

int lp55xx_read(struct lp55xx_chip *chip, u8 reg, u8 *val)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(chip->cl, reg);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}
EXPORT_SYMBOL_GPL(lp55xx_read);

int lp55xx_update_bits(struct lp55xx_chip *chip, u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	ret = lp55xx_read(chip, reg, &tmp);
	if (ret)
		return ret;

	tmp &= ~mask;
	tmp |= val & mask;

	return lp55xx_write(chip, reg, tmp);
}
EXPORT_SYMBOL_GPL(lp55xx_update_bits);

int lp55xx_init_device(struct lp55xx_chip *chip)
{
	struct lp55xx_platform_data *pdata;
	struct device *dev = &chip->cl->dev;
	int ret = 0;

	WARN_ON(!chip);

	pdata = chip->pdata;

	if (!pdata)
		return -EINVAL;

	if (pdata->setup_resources) {
		ret = pdata->setup_resources();
		if (ret < 0) {
			dev_err(dev, "setup resoure err: %d\n", ret);
			goto err;
		}
	}

	if (pdata->enable) {
		pdata->enable(0);
		usleep_range(1000, 2000); /* Keep enable down at least 1ms */
		pdata->enable(1);
		usleep_range(1000, 2000); /* 500us abs min. */
	}

err:
	return ret;
}
EXPORT_SYMBOL_GPL(lp55xx_init_device);

MODULE_AUTHOR("Milo Kim <milo.kim@ti.com>");
MODULE_DESCRIPTION("LP55xx Common Driver");
MODULE_LICENSE("GPL");
