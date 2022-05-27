// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define WL2868C_DEVICE_D1		0x00
#define WL2868C_DEVICE_D2		0x01
#define WL2868C_DISCHARGE_RESISTORS	0x02
#define WL2868C_LDO1_VOUT		0x03
#define WL2868C_LDO2_VOUT		0x04
#define WL2868C_LDO3_VOUT		0x05
#define WL2868C_LDO4_VOUT		0x06
#define WL2868C_LDO5_VOUT		0x07
#define WL2868C_LDO6_VOUT		0x08
#define WL2868C_LDO7_VOUT		0x09
#define WL2868C_LDO1_LDO2_SEQ		0x0a
#define WL2868C_LDO3_LDO4_SEQ		0x0b
#define WL2868C_LDO5_LDO6_SEQ		0x0c
#define WL2868C_LDO7_SEQ		0x0d
#define WL2868C_LDO_EN			0x0e
#define WL2868C_SEQ_STATUS		0x0f
#define WL2868C_LDO1_STATUS		0x10
#define WL2868C_LDO1_OCP_CTL		0x11
#define WL2868C_LDO2_STATUS		0x12
#define WL2868C_LDO2_OCP_CTL		0x13
#define WL2868C_LDO3_STATUS		0x14
#define WL2868C_LDO3_OCP_CTL		0x15
#define WL2868C_LDO4_STATUS		0x16
#define WL2868C_LDO4_OCP_CTL		0x17
#define WL2868C_LDO5_STATUS		0x18
#define WL2868C_LDO5_OCP_CTL		0x19
#define WL2868C_LDO6_STATUS		0x1a
#define WL2868C_LDO6_OCP_CTL		0x1b
#define WL2868C_LDO7_STATUS		0x1c
#define WL2868C_LDO7_OCP_CTL		0x1d
#define WL2868C_REPROGRAMMABLE_I2C_ADDR	0x1e
#define RESERVED_1			0x1f
#define INT_LATCHED_CLR			0x20
#define INT_EN_SET			0x21
#define INT_LATCHED_STS			0x22
#define INT_PENDING_STS			0x23
#define UVLO_CTL			0x24
#define RESERVED_2			0x25

#define WL2868C_VSEL_MASK		0xff

enum wl2868c_regulators {
	WL2868C_REGULATOR_LDO1 = 0,
	WL2868C_REGULATOR_LDO2,
	WL2868C_REGULATOR_LDO3,
	WL2868C_REGULATOR_LDO4,
	WL2868C_REGULATOR_LDO5,
	WL2868C_REGULATOR_LDO6,
	WL2868C_REGULATOR_LDO7,
	WL2868C_MAX_REGULATORS,
};

struct wl2868c {
	struct device *dev;
	struct regmap *regmap;
	struct regulator_dev *rdev;
	struct gpio_desc *reset_gpio;
	int min_dropout_uv;
};

static const struct regulator_ops wl2868c_reg_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

#define WL2868C_DESC(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _enval, _disval)		\
	{								\
		.name		= (_match),				\
		.supply_name	= (_supply),				\
		.of_match	= of_match_ptr(_match),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.type		= REGULATOR_VOLTAGE,			\
		.id		= (_id),				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),	\
		.owner		= THIS_MODULE,				\
		.min_uV		= (_min) * 1000,			\
		.uV_step	= (_step) * 1000,			\
		.vsel_reg	= (_vreg),				\
		.vsel_mask	= (_vmask),				\
		.enable_reg	= (_ereg),				\
		.enable_mask	= (_emask),				\
		.enable_val     = (_enval),				\
		.disable_val     = (_disval),				\
		.ops		= &wl2868c_reg_ops,			\
	}

static const struct regulator_desc wl2868c_reg[] = {
	WL2868C_DESC(WL2868C_REGULATOR_LDO1, "WL_LDO1", "ldo1", 496, 2536, 8,
		     WL2868C_LDO1_VOUT, WL2868C_VSEL_MASK, WL2868C_LDO_EN, BIT(0), BIT(0), 0),
	WL2868C_DESC(WL2868C_REGULATOR_LDO2, "WL_LDO2", "ldo2", 496, 2536, 8,
		     WL2868C_LDO2_VOUT, WL2868C_VSEL_MASK, WL2868C_LDO_EN, BIT(1), BIT(1), 0),
	WL2868C_DESC(WL2868C_REGULATOR_LDO3, "WL_LDO3", "ldo3", 1504, 3544, 8,
		     WL2868C_LDO3_VOUT, WL2868C_VSEL_MASK, WL2868C_LDO_EN, BIT(2), BIT(2), 0),
	WL2868C_DESC(WL2868C_REGULATOR_LDO4, "WL_LDO4", "ldo4", 1504, 3544, 8,
		     WL2868C_LDO4_VOUT, WL2868C_VSEL_MASK, WL2868C_LDO_EN, BIT(3), BIT(3), 0),
	WL2868C_DESC(WL2868C_REGULATOR_LDO5, "WL_LDO5", "ldo5", 1504, 3544, 8,
		     WL2868C_LDO5_VOUT, WL2868C_VSEL_MASK, WL2868C_LDO_EN, BIT(4), BIT(4), 0),
	WL2868C_DESC(WL2868C_REGULATOR_LDO6, "WL_LDO6", "ldo6", 1504, 3544, 8,
		     WL2868C_LDO6_VOUT, WL2868C_VSEL_MASK, WL2868C_LDO_EN, BIT(5), BIT(5), 0),
	WL2868C_DESC(WL2868C_REGULATOR_LDO7, "WL_LDO7", "ldo7", 1504, 3544, 8,
		     WL2868C_LDO7_VOUT, WL2868C_VSEL_MASK, WL2868C_LDO_EN, BIT(6), BIT(6), 0),
};

static const struct regmap_range wl2868c_writeable_ranges[] = {
	regmap_reg_range(WL2868C_DISCHARGE_RESISTORS, WL2868C_SEQ_STATUS),
	regmap_reg_range(WL2868C_LDO1_OCP_CTL, WL2868C_LDO1_OCP_CTL),
	regmap_reg_range(WL2868C_LDO2_OCP_CTL, WL2868C_LDO2_OCP_CTL),
	regmap_reg_range(WL2868C_LDO3_OCP_CTL, WL2868C_LDO3_OCP_CTL),
	regmap_reg_range(WL2868C_LDO4_OCP_CTL, WL2868C_LDO4_OCP_CTL),
	regmap_reg_range(WL2868C_LDO5_OCP_CTL, WL2868C_LDO5_OCP_CTL),
	regmap_reg_range(WL2868C_LDO6_OCP_CTL, WL2868C_LDO6_OCP_CTL),
	regmap_reg_range(WL2868C_LDO7_OCP_CTL, WL2868C_REPROGRAMMABLE_I2C_ADDR),
	regmap_reg_range(INT_LATCHED_CLR, INT_LATCHED_CLR),
	regmap_reg_range(INT_EN_SET, INT_EN_SET),
	regmap_reg_range(UVLO_CTL, UVLO_CTL),
};

static const struct regmap_range wl2868c_readable_ranges[] = {
	regmap_reg_range(WL2868C_DEVICE_D1, RESERVED_2),
};

static const struct regmap_range wl2868c_volatile_ranges[] = {
	regmap_reg_range(WL2868C_DISCHARGE_RESISTORS, WL2868C_SEQ_STATUS),
	regmap_reg_range(WL2868C_LDO1_OCP_CTL, WL2868C_LDO1_OCP_CTL),
	regmap_reg_range(WL2868C_LDO2_OCP_CTL, WL2868C_LDO2_OCP_CTL),
	regmap_reg_range(WL2868C_LDO3_OCP_CTL, WL2868C_LDO3_OCP_CTL),
	regmap_reg_range(WL2868C_LDO4_OCP_CTL, WL2868C_LDO4_OCP_CTL),
	regmap_reg_range(WL2868C_LDO5_OCP_CTL, WL2868C_LDO5_OCP_CTL),
	regmap_reg_range(WL2868C_LDO6_OCP_CTL, WL2868C_LDO6_OCP_CTL),
	regmap_reg_range(WL2868C_LDO7_OCP_CTL, WL2868C_REPROGRAMMABLE_I2C_ADDR),
	regmap_reg_range(INT_LATCHED_CLR, INT_LATCHED_CLR),
	regmap_reg_range(INT_EN_SET, INT_EN_SET),
	regmap_reg_range(UVLO_CTL, UVLO_CTL),
};

static const struct regmap_access_table wl2868c_writeable_table = {
	.yes_ranges   = wl2868c_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(wl2868c_writeable_ranges),
};

static const struct regmap_access_table wl2868c_readable_table = {
	.yes_ranges   = wl2868c_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(wl2868c_readable_ranges),
};

static const struct regmap_access_table wl2868c_volatile_table = {
	.yes_ranges   = wl2868c_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(wl2868c_volatile_ranges),
};

static const struct regmap_config wl2868c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RESERVED_2,
	.wr_table = &wl2868c_writeable_table,
	.rd_table = &wl2868c_readable_table,
	.cache_type = REGCACHE_RBTREE,
	.volatile_table = &wl2868c_volatile_table,
};

static int wl2868c_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	const struct regulator_desc *regulators;
	struct wl2868c *wl2868c;
	int ret, i;

	wl2868c = devm_kzalloc(dev, sizeof(struct wl2868c), GFP_KERNEL);
	if (!wl2868c)
		return -ENOMEM;

	wl2868c->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(wl2868c->reset_gpio)) {
		ret = PTR_ERR(wl2868c->reset_gpio);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(wl2868c->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(wl2868c->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(wl2868c->reset_gpio, 0);
	usleep_range(10000, 11000);

	i2c_set_clientdata(client, wl2868c);
	wl2868c->dev = dev;
	wl2868c->regmap = devm_regmap_init_i2c(client, &wl2868c_regmap_config);
	if (IS_ERR(wl2868c->regmap)) {
		ret = PTR_ERR(wl2868c->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	config.dev = &client->dev;
	config.regmap = wl2868c->regmap;
	regulators = wl2868c_reg;
	/* Instantiate the regulators */
	for (i = 0; i < WL2868C_MAX_REGULATORS; i++) {
		rdev = devm_regulator_register(&client->dev,
					       &regulators[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(&client->dev,
				"failed to register %d regulator\n", i);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static void wl2868c_regulator_shutdown(struct i2c_client *client)
{
	struct wl2868c *wl2868c = i2c_get_clientdata(client);

	if (system_state == SYSTEM_POWER_OFF)
		regmap_write(wl2868c->regmap, WL2868C_LDO_EN, 0x80);
}

static const struct i2c_device_id wl2868c_i2c_id[] = {
	{ "wl2868c", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, i2c_device_id);

static const struct of_device_id wl2868c_of_match[] = {
	{ .compatible = "willsemi,wl2868c" },
	{}
};
MODULE_DEVICE_TABLE(of, wl2868c_of_match);

static struct i2c_driver wl2868c_i2c_driver = {
	.driver = {
		.name = "wl2868c",
		.of_match_table = of_match_ptr(wl2868c_of_match),
	},
	.id_table = wl2868c_i2c_id,
	.probe	= wl2868c_i2c_probe,
	.shutdown = wl2868c_regulator_shutdown,
};

module_i2c_driver(wl2868c_i2c_driver);

MODULE_DESCRIPTION("WL2868C regulator driver");
MODULE_AUTHOR("Shunqing Chen <csq@rock-chips.com>");
MODULE_LICENSE("GPL");
