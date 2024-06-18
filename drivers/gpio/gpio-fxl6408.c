// SPDX-License-Identifier: GPL-2.0-only
/*
 * FXL6408 GPIO driver
 *
 * Copyright 2023 Toradex
 *
 * Author: Emanuele Ghidoli <emanuele.ghidoli@toradex.com>
 */

#include <linux/err.h>
#include <linux/gpio/regmap.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define FXL6408_REG_DEVICE_ID		0x01
#define FXL6408_MF_FAIRCHILD		0b101
#define FXL6408_MF_SHIFT		5

/* Bits set here indicate that the GPIO is an output. */
#define FXL6408_REG_IO_DIR		0x03

/*
 * Bits set here, when the corresponding bit of IO_DIR is set, drive
 * the output high instead of low.
 */
#define FXL6408_REG_OUTPUT		0x05

/* Bits here make the output High-Z, instead of the OUTPUT value. */
#define FXL6408_REG_OUTPUT_HIGH_Z	0x07

/* Returns the current status (1 = HIGH) of the input pins. */
#define FXL6408_REG_INPUT_STATUS	0x0f

/*
 * Return the current interrupt status
 * This bit is HIGH if input GPIO != default state (register 09h).
 * The flag is cleared after being read (bit returns to 0).
 * The input must go back to default state and change again before this flag is raised again.
 */
#define FXL6408_REG_INT_STS		0x13

#define FXL6408_NGPIO			8

static const struct regmap_range rd_range[] = {
	{ FXL6408_REG_DEVICE_ID, FXL6408_REG_DEVICE_ID },
	{ FXL6408_REG_IO_DIR, FXL6408_REG_OUTPUT },
	{ FXL6408_REG_INPUT_STATUS, FXL6408_REG_INPUT_STATUS },
};

static const struct regmap_range wr_range[] = {
	{ FXL6408_REG_DEVICE_ID, FXL6408_REG_DEVICE_ID },
	{ FXL6408_REG_IO_DIR, FXL6408_REG_OUTPUT },
	{ FXL6408_REG_OUTPUT_HIGH_Z, FXL6408_REG_OUTPUT_HIGH_Z },
};

static const struct regmap_range volatile_range[] = {
	{ FXL6408_REG_DEVICE_ID, FXL6408_REG_DEVICE_ID },
	{ FXL6408_REG_INPUT_STATUS, FXL6408_REG_INPUT_STATUS },
};

static const struct regmap_access_table rd_table = {
	.yes_ranges = rd_range,
	.n_yes_ranges = ARRAY_SIZE(rd_range),
};

static const struct regmap_access_table wr_table = {
	.yes_ranges = wr_range,
	.n_yes_ranges = ARRAY_SIZE(wr_range),
};

static const struct regmap_access_table volatile_table = {
	.yes_ranges = volatile_range,
	.n_yes_ranges = ARRAY_SIZE(volatile_range),
};

static const struct regmap_config regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = FXL6408_REG_INT_STS,
	.wr_table = &wr_table,
	.rd_table = &rd_table,
	.volatile_table = &volatile_table,

	.cache_type = REGCACHE_MAPLE,
	.num_reg_defaults_raw = FXL6408_REG_INT_STS + 1,
};

static int fxl6408_identify(struct device *dev, struct regmap *regmap)
{
	int val, ret;

	ret = regmap_read(regmap, FXL6408_REG_DEVICE_ID, &val);
	if (ret)
		return dev_err_probe(dev, ret, "error reading DEVICE_ID\n");
	if (val >> FXL6408_MF_SHIFT != FXL6408_MF_FAIRCHILD)
		return dev_err_probe(dev, -ENODEV, "invalid device id 0x%02x\n", val);

	return 0;
}

static int fxl6408_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int ret;
	struct gpio_regmap_config gpio_config = {
		.parent = dev,
		.ngpio = FXL6408_NGPIO,
		.reg_dat_base = GPIO_REGMAP_ADDR(FXL6408_REG_INPUT_STATUS),
		.reg_set_base = GPIO_REGMAP_ADDR(FXL6408_REG_OUTPUT),
		.reg_dir_out_base = GPIO_REGMAP_ADDR(FXL6408_REG_IO_DIR),
		.ngpio_per_reg = FXL6408_NGPIO,
	};

	gpio_config.regmap = devm_regmap_init_i2c(client, &regmap);
	if (IS_ERR(gpio_config.regmap))
		return dev_err_probe(dev, PTR_ERR(gpio_config.regmap),
				     "failed to allocate register map\n");

	ret = fxl6408_identify(dev, gpio_config.regmap);
	if (ret)
		return ret;

	/* Disable High-Z of outputs, so that our OUTPUT updates actually take effect. */
	ret = regmap_write(gpio_config.regmap, FXL6408_REG_OUTPUT_HIGH_Z, 0);
	if (ret)
		return dev_err_probe(dev, ret, "failed to write 'output high Z' register\n");

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(dev, &gpio_config));
}

static const __maybe_unused struct of_device_id fxl6408_dt_ids[] = {
	{ .compatible = "fcs,fxl6408" },
	{ }
};
MODULE_DEVICE_TABLE(of, fxl6408_dt_ids);

static const struct i2c_device_id fxl6408_id[] = {
	{ "fxl6408", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, fxl6408_id);

static struct i2c_driver fxl6408_driver = {
	.driver = {
		.name	= "fxl6408",
		.of_match_table = fxl6408_dt_ids,
	},
	.probe		= fxl6408_probe,
	.id_table	= fxl6408_id,
};
module_i2c_driver(fxl6408_driver);

MODULE_AUTHOR("Emanuele Ghidoli <emanuele.ghidoli@toradex.com>");
MODULE_DESCRIPTION("FXL6408 GPIO driver");
MODULE_LICENSE("GPL");
