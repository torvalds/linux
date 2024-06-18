// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Core driver for WM8400.
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/init.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/wm8400-private.h>
#include <linux/mfd/wm8400-audio.h>
#include <linux/regmap.h>
#include <linux/slab.h>

static bool wm8400_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8400_INTERRUPT_STATUS_1:
	case WM8400_INTERRUPT_LEVELS:
	case WM8400_SHUTDOWN_REASON:
		return true;
	default:
		return false;
	}
}

static int wm8400_register_codec(struct wm8400 *wm8400)
{
	const struct mfd_cell cell = {
		.name = "wm8400-codec",
		.platform_data = wm8400,
		.pdata_size = sizeof(*wm8400),
	};

	return devm_mfd_add_devices(wm8400->dev, -1, &cell, 1, NULL, 0, NULL);
}

/*
 * wm8400_init - Generic initialisation
 *
 * The WM8400 can be configured as either an I2C or SPI device.  Probe
 * functions for each bus set up the accessors then call into this to
 * set up the device itself.
 */
static int wm8400_init(struct wm8400 *wm8400,
		       struct wm8400_platform_data *pdata)
{
	unsigned int reg;
	int ret;

	/* Check that this is actually a WM8400 */
	ret = regmap_read(wm8400->regmap, WM8400_RESET_ID, &reg);
	if (ret != 0) {
		dev_err(wm8400->dev, "Chip ID register read failed\n");
		return -EIO;
	}
	if (reg != 0x6172) {
		dev_err(wm8400->dev, "Device is not a WM8400, ID is %x\n",
			reg);
		return -ENODEV;
	}

	ret = regmap_read(wm8400->regmap, WM8400_ID, &reg);
	if (ret != 0) {
		dev_err(wm8400->dev, "ID register read failed: %d\n", ret);
		return ret;
	}
	reg = (reg & WM8400_CHIP_REV_MASK) >> WM8400_CHIP_REV_SHIFT;
	dev_info(wm8400->dev, "WM8400 revision %x\n", reg);

	ret = wm8400_register_codec(wm8400);
	if (ret != 0) {
		dev_err(wm8400->dev, "Failed to register codec\n");
		return ret;
	}

	if (pdata && pdata->platform_init) {
		ret = pdata->platform_init(wm8400->dev);
		if (ret != 0) {
			dev_err(wm8400->dev, "Platform init failed: %d\n",
				ret);
			return ret;
		}
	} else
		dev_warn(wm8400->dev, "No platform initialisation supplied\n");

	return 0;
}

static const struct regmap_config wm8400_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = WM8400_REGISTER_COUNT - 1,

	.volatile_reg = wm8400_volatile,

	.cache_type = REGCACHE_MAPLE,
};

/**
 * wm8400_reset_codec_reg_cache - Reset cached codec registers to
 * their default values.
 *
 * @wm8400: pointer to local driver data structure
 */
void wm8400_reset_codec_reg_cache(struct wm8400 *wm8400)
{
	regmap_reinit_cache(wm8400->regmap, &wm8400_regmap_config);
}
EXPORT_SYMBOL_GPL(wm8400_reset_codec_reg_cache);

#if IS_ENABLED(CONFIG_I2C)
static int wm8400_i2c_probe(struct i2c_client *i2c)
{
	struct wm8400 *wm8400;

	wm8400 = devm_kzalloc(&i2c->dev, sizeof(struct wm8400), GFP_KERNEL);
	if (!wm8400)
		return -ENOMEM;

	wm8400->regmap = devm_regmap_init_i2c(i2c, &wm8400_regmap_config);
	if (IS_ERR(wm8400->regmap))
		return PTR_ERR(wm8400->regmap);

	wm8400->dev = &i2c->dev;
	i2c_set_clientdata(i2c, wm8400);

	return wm8400_init(wm8400, dev_get_platdata(&i2c->dev));
}

static const struct i2c_device_id wm8400_i2c_id[] = {
       { "wm8400", 0 },
       { }
};

static struct i2c_driver wm8400_i2c_driver = {
	.driver = {
		.name = "WM8400",
	},
	.probe = wm8400_i2c_probe,
	.id_table = wm8400_i2c_id,
};
#endif

static int __init wm8400_driver_init(void)
{
	int ret = -ENODEV;

#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&wm8400_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);
#endif

	return ret;
}
subsys_initcall(wm8400_driver_init);
