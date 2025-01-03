// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024, Linaro Limited
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define NUM_SUPPLIES 2

struct ptn3222 {
	struct i2c_client *client;
	struct phy *phy;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static int ptn3222_init(struct phy *phy)
{
	struct ptn3222 *ptn3222 = phy_get_drvdata(phy);
	int ret;

	ret = regulator_bulk_enable(NUM_SUPPLIES, ptn3222->supplies);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(ptn3222->reset_gpio, 0);

	return 0;
}

static int ptn3222_exit(struct phy *phy)
{
	struct ptn3222 *ptn3222 = phy_get_drvdata(phy);

	gpiod_set_value_cansleep(ptn3222->reset_gpio, 1);

	return regulator_bulk_disable(NUM_SUPPLIES, ptn3222->supplies);
}

static const struct phy_ops ptn3222_ops = {
	.init		= ptn3222_init,
	.exit		= ptn3222_exit,
	.owner		= THIS_MODULE,
};

static const struct regulator_bulk_data ptn3222_supplies[NUM_SUPPLIES] = {
	{
		.supply = "vdd3v3",
		.init_load_uA = 11000,
	}, {
		.supply = "vdd1v8",
		.init_load_uA = 55000,
	}
};

static int ptn3222_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct phy_provider *phy_provider;
	struct ptn3222 *ptn3222;
	int ret;

	ptn3222 = devm_kzalloc(dev, sizeof(*ptn3222), GFP_KERNEL);
	if (!ptn3222)
		return -ENOMEM;

	ptn3222->client = client;

	ptn3222->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(ptn3222->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ptn3222->reset_gpio),
				     "unable to acquire reset gpio\n");

	ret = devm_regulator_bulk_get_const(dev, NUM_SUPPLIES, ptn3222_supplies,
					    &ptn3222->supplies);
	if (ret)
		return ret;

	ptn3222->phy = devm_phy_create(dev, dev->of_node, &ptn3222_ops);
	if (IS_ERR(ptn3222->phy)) {
		dev_err(dev, "failed to create PHY: %d\n", ret);
		return PTR_ERR(ptn3222->phy);
	}

	phy_set_drvdata(ptn3222->phy, ptn3222);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct i2c_device_id ptn3222_table[] = {
	{ "ptn3222" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ptn3222_table);

static const struct of_device_id ptn3222_of_table[] = {
	{ .compatible = "nxp,ptn3222" },
	{ }
};
MODULE_DEVICE_TABLE(of, ptn3222_of_table);

static struct i2c_driver ptn3222_driver = {
	.driver = {
		.name = "ptn3222",
		.of_match_table = ptn3222_of_table,
	},
	.probe = ptn3222_probe,
	.id_table = ptn3222_table,
};

module_i2c_driver(ptn3222_driver);

MODULE_DESCRIPTION("NXP PTN3222 eUSB2 Redriver driver");
MODULE_LICENSE("GPL");
