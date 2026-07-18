// SPDX-License-Identifier: GPL-2.0-only
//
// SGMicro SGM3804 regulator Driver
//
// Copyright (C) 2025 Kancy Joe <kancy2333@outlook.com>
// Copyright (C) 2026 Linaro Limited
// Author: Neil Armstrong <neil.armstrong@linaro.org>

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/gpio/consumer.h>

#define SGM3804_POS_RAIL_VOLTAGE_REG	0x0
#define SGM3804_NEG_RAIL_VOLTAGE_REG	0x1
#define SGM3804_RAIL_DISCHARGE_REG	0x3

#define RAIL_VOLTAGE_MASK	GENMASK(5, 0)

#define POS_RAIL_DISCHARGE_EN	BIT(1)
#define NEG_RAIL_DISCHARGE_EN	BIT(0)

#define RAIL_VOLTAGE_INVALID		RAIL_VOLTAGE_MASK
#define RAIL_DISCHARGE_REG_DEFAULT	(POS_RAIL_DISCHARGE_EN | NEG_RAIL_DISCHARGE_EN)

#define SGM3804_VOLTAGES_MAX_SELECTOR	0x2f

enum {
	SGM3804_POS_RAIL = 0,
	SGM3804_NEG_RAIL,
	SGM3804_RAIL_COUNT,
};

/*
 * The registers are only writable when the gpio is enabled, so
 * we need to use the cache for read operations and set the regmap
 * as cache_only when both GPIOs are down.
 */
struct sgm3804_data {
	struct regmap *regmap;
	/* Protects the regcache state update */
	struct mutex lock;
	struct gpio_desc *gpios[SGM3804_RAIL_COUNT];
};

static const struct linear_range sgm3804_voltages[] = {
	REGULATOR_LINEAR_RANGE(2400000, 0x20, 0x2f, 100000),
	REGULATOR_LINEAR_RANGE(4000000, 0x00, 0x17, 100000),
};

/*
 * The cache is populated with those hardware default values
 * so the regmap_update_bits operation will use the cached
 * value to build a new register value and write it when GPIOs
 * are enabled.
 */
static const struct reg_default sgm3804_reg_defaults[] = {
	{ SGM3804_POS_RAIL_VOLTAGE_REG, RAIL_VOLTAGE_INVALID },
	{ SGM3804_NEG_RAIL_VOLTAGE_REG, RAIL_VOLTAGE_INVALID },
	{ SGM3804_RAIL_DISCHARGE_REG, RAIL_DISCHARGE_REG_DEFAULT },
};

/* Registers are only writable */
static bool sgm3804_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SGM3804_POS_RAIL_VOLTAGE_REG:
	case SGM3804_NEG_RAIL_VOLTAGE_REG:
	case SGM3804_RAIL_DISCHARGE_REG:
		return true;
	default:
		return false;
	}
}

/*
 * Since all registers are only writeable, regmap will only read from the cache data.
 */
static bool sgm3804_readable_reg(struct device *dev, unsigned int reg)
{
	return false;
}

static const struct regmap_config sgm3804_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x03,
	.writeable_reg = sgm3804_writeable_reg,
	.readable_reg = sgm3804_readable_reg,
	.cache_type = REGCACHE_MAPLE,
	.reg_defaults = sgm3804_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(sgm3804_reg_defaults),
};

static int sgm3804_sync_regcache_state(struct sgm3804_data *ctx)
{
	guard(mutex)(&ctx->lock);

	/* If both GPIOs are down, IC is powered down and I2C writes will fail */
	if (!gpiod_get_value_cansleep(ctx->gpios[SGM3804_POS_RAIL]) &&
	    !gpiod_get_value_cansleep(ctx->gpios[SGM3804_NEG_RAIL])) {
		regcache_cache_only(ctx->regmap, true);
		regcache_mark_dirty(ctx->regmap);
	} else {
		int ret;

		/* At least a GPIO is up, we can write registers */
		regcache_cache_only(ctx->regmap, false);
		ret = regcache_sync(ctx->regmap);
		if (ret) {
			regcache_cache_only(ctx->regmap, true);
			return ret;
		}
	}

	return 0;
}

static int sgm3804_get_voltage_sel(struct regulator_dev *rdev)
{
	int ret;

	ret = regulator_get_voltage_sel_regmap(rdev);
	if (ret < 0)
		return ret;

	/* Force setting a voltage on probe */
	if (ret == RAIL_VOLTAGE_INVALID)
		return -ENOTRECOVERABLE;

	return ret;
}

static int sgm3804_enable(struct regulator_dev *rdev)
{
	struct sgm3804_data *ctx = rdev->reg_data;
	int ret;

	ret = gpiod_set_value_cansleep(ctx->gpios[rdev_get_id(rdev)], 1);
	if (ret)
		return ret;

	ret = sgm3804_sync_regcache_state(ctx);
	if (ret)
		goto err;

	return 0;

err:
	gpiod_set_value_cansleep(ctx->gpios[rdev_get_id(rdev)], 0);
	return ret;
}

static int sgm3804_disable(struct regulator_dev *rdev)
{
	struct sgm3804_data *ctx = rdev->reg_data;
	int ret;

	ret = gpiod_set_value_cansleep(ctx->gpios[rdev_get_id(rdev)], 0);
	if (ret)
		return ret;

	return sgm3804_sync_regcache_state(ctx);
}

static int sgm3804_is_enabled(struct regulator_dev *rdev)
{
	struct sgm3804_data *ctx = rdev->reg_data;

	return gpiod_get_value_cansleep(ctx->gpios[rdev_get_id(rdev)]);
}

static const struct regulator_ops sgm3804_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = sgm3804_get_voltage_sel,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.enable = sgm3804_enable,
	.disable = sgm3804_disable,
	.is_enabled = sgm3804_is_enabled,
};

static const struct regulator_desc sgm3804_regulator_desc[] = {
	/* Positive Output */
	{
		.name = "pos",
		.of_match = "pos",
		.supply_name = "vin",
		.id = SGM3804_POS_RAIL,
		.ops = &sgm3804_ops,
		.type = REGULATOR_VOLTAGE,
		.linear_ranges = sgm3804_voltages,
		.n_linear_ranges = ARRAY_SIZE(sgm3804_voltages),
		.n_voltages = SGM3804_VOLTAGES_MAX_SELECTOR + 1,
		.vsel_reg = SGM3804_POS_RAIL_VOLTAGE_REG,
		.vsel_mask = RAIL_VOLTAGE_MASK,
		.active_discharge_on = POS_RAIL_DISCHARGE_EN,
		.active_discharge_mask = POS_RAIL_DISCHARGE_EN,
		.active_discharge_reg = SGM3804_RAIL_DISCHARGE_REG,
		.enable_time = 40000,
		.owner = THIS_MODULE,
	},
	/* Negative Output */
	{
		.name = "neg",
		.of_match = "neg",
		.supply_name = "vin",
		.id = SGM3804_NEG_RAIL,
		.ops = &sgm3804_ops,
		.type = REGULATOR_VOLTAGE,
		.linear_ranges = sgm3804_voltages,
		.n_linear_ranges = ARRAY_SIZE(sgm3804_voltages),
		.n_voltages = SGM3804_VOLTAGES_MAX_SELECTOR + 1,
		.vsel_reg = SGM3804_NEG_RAIL_VOLTAGE_REG,
		.vsel_mask = RAIL_VOLTAGE_MASK,
		.active_discharge_on = NEG_RAIL_DISCHARGE_EN,
		.active_discharge_mask = NEG_RAIL_DISCHARGE_EN,
		.active_discharge_reg = SGM3804_RAIL_DISCHARGE_REG,
		.enable_time = 40000,
		.owner = THIS_MODULE,
	},
};

static int sgm3804_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct sgm3804_data *ctx;
	int ret, i;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->lock);

	ctx->regmap = devm_regmap_init_i2c(i2c, &sgm3804_regmap_config);
	if (IS_ERR(ctx->regmap))
		return dev_err_probe(dev, PTR_ERR(ctx->regmap),
				     "failed to init regmap\n");

	/* Get enable GPIOs */
	for (i = 0; i < ARRAY_SIZE(sgm3804_regulator_desc); i++) {
		const struct regulator_desc *reg = &sgm3804_regulator_desc[i];
		struct fwnode_handle *child;

		child = device_get_named_child_node(dev, reg->of_match);
		if (!child) {
			dev_err(dev, "missing child '%s'\n", reg->of_match);
			return -EINVAL;
		}

		ctx->gpios[i] = devm_fwnode_gpiod_get(dev, child, "enable",
						      GPIOD_ASIS, reg->name);
		fwnode_handle_put(child);
		if (IS_ERR(ctx->gpios[i]))
			return dev_err_probe(dev, PTR_ERR(ctx->gpios[i]),
					     "failed to get '%s' enable GPIO\n",
					     reg->name);
	}

	ret = sgm3804_sync_regcache_state(ctx);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(sgm3804_regulator_desc); i++) {
		struct regulator_config config = { };
		struct regulator_dev *rdev;

		config.dev = dev;
		config.regmap = ctx->regmap;
		config.of_node = dev_of_node(dev);
		config.driver_data = ctx;
		rdev = devm_regulator_register(dev, &sgm3804_regulator_desc[i],
					       &config);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev),
					     "failed to register regulator %d\n", i);
	}

	return 0;
}

static const struct i2c_device_id sgm3804_id[] = {
	{ "sgm3804" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sgm3804_id);

static const struct of_device_id sgm3804_of_match[] = {
	{ .compatible = "sgmicro,sgm3804" },
	{ }
};
MODULE_DEVICE_TABLE(of, sgm3804_of_match);

static struct i2c_driver sgm3804_regulator_driver = {
	.driver = {
		.name = "sgm3804",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = sgm3804_of_match,
	},
	.probe = sgm3804_probe,
	.id_table = sgm3804_id,
};

module_i2c_driver(sgm3804_regulator_driver);

MODULE_DESCRIPTION("SGMicro SGM3804 regulator Driver");
MODULE_AUTHOR("Kancy Joe <kancy2333@outlook.com>");
MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
MODULE_LICENSE("GPL");
