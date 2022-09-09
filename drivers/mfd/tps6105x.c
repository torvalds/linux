// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core driver for TPS61050/61052 boost converters, used for while LED
 * driving, audio power amplification, white LED flash, and generic
 * boost conversion. Additionally it provides a 1-bit GPIO pin (out or in)
 * and a flash synchronization pin to synchronize flash events when used as
 * flashgun.
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps6105x.h>

static struct regmap_config tps6105x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TPS6105X_REG_3,
};

static int tps6105x_startup(struct tps6105x *tps6105x)
{
	int ret;
	unsigned int regval;

	ret = regmap_read(tps6105x->regmap, TPS6105X_REG_0, &regval);
	if (ret)
		return ret;
	switch (regval >> TPS6105X_REG0_MODE_SHIFT) {
	case TPS6105X_REG0_MODE_SHUTDOWN:
		dev_info(&tps6105x->client->dev,
			 "TPS6105x found in SHUTDOWN mode\n");
		break;
	case TPS6105X_REG0_MODE_TORCH:
		dev_info(&tps6105x->client->dev,
			 "TPS6105x found in TORCH mode\n");
		break;
	case TPS6105X_REG0_MODE_TORCH_FLASH:
		dev_info(&tps6105x->client->dev,
			 "TPS6105x found in FLASH mode\n");
		break;
	case TPS6105X_REG0_MODE_VOLTAGE:
		dev_info(&tps6105x->client->dev,
			 "TPS6105x found in VOLTAGE mode\n");
		break;
	default:
		break;
	}

	return ret;
}

/*
 * MFD cells - we always have a GPIO cell and we have one cell
 * which is selected operation mode.
 */
static struct mfd_cell tps6105x_gpio_cell = {
	.name = "tps6105x-gpio",
};

static struct mfd_cell tps6105x_leds_cell = {
	.name = "tps6105x-leds",
};

static struct mfd_cell tps6105x_flash_cell = {
	.name = "tps6105x-flash",
};

static struct mfd_cell tps6105x_regulator_cell = {
	.name = "tps6105x-regulator",
};

static int tps6105x_add_device(struct tps6105x *tps6105x,
			       struct mfd_cell *cell)
{
	cell->platform_data = tps6105x;
	cell->pdata_size = sizeof(*tps6105x);

	return mfd_add_devices(&tps6105x->client->dev,
			       PLATFORM_DEVID_AUTO, cell, 1, NULL, 0, NULL);
}

static struct tps6105x_platform_data *tps6105x_parse_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct tps6105x_platform_data *pdata;
	struct device_node *child;

	if (!np)
		return ERR_PTR(-EINVAL);
	if (of_get_available_child_count(np) > 1) {
		dev_err(dev, "cannot support multiple operational modes");
		return ERR_PTR(-EINVAL);
	}
	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);
	pdata->mode = TPS6105X_MODE_SHUTDOWN;
	for_each_available_child_of_node(np, child) {
		if (child->name && !of_node_cmp(child->name, "regulator"))
			pdata->mode = TPS6105X_MODE_VOLTAGE;
		else if (child->name && !of_node_cmp(child->name, "led"))
			pdata->mode = TPS6105X_MODE_TORCH;
	}

	return pdata;
}

static int tps6105x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct tps6105x			*tps6105x;
	struct tps6105x_platform_data	*pdata;
	int ret;

	pdata = dev_get_platdata(&client->dev);
	if (!pdata)
		pdata = tps6105x_parse_dt(&client->dev);
	if (IS_ERR(pdata)) {
		dev_err(&client->dev, "No platform data or DT found");
		return PTR_ERR(pdata);
	}

	tps6105x = devm_kmalloc(&client->dev, sizeof(*tps6105x), GFP_KERNEL);
	if (!tps6105x)
		return -ENOMEM;

	tps6105x->regmap = devm_regmap_init_i2c(client, &tps6105x_regmap_config);
	if (IS_ERR(tps6105x->regmap))
		return PTR_ERR(tps6105x->regmap);

	i2c_set_clientdata(client, tps6105x);
	tps6105x->client = client;
	tps6105x->pdata = pdata;

	ret = tps6105x_startup(tps6105x);
	if (ret) {
		dev_err(&client->dev, "chip initialization failed\n");
		return ret;
	}

	ret = tps6105x_add_device(tps6105x, &tps6105x_gpio_cell);
	if (ret)
		return ret;

	switch (pdata->mode) {
	case TPS6105X_MODE_SHUTDOWN:
		dev_info(&client->dev,
			 "present, not used for anything, only GPIO\n");
		break;
	case TPS6105X_MODE_TORCH:
		ret = tps6105x_add_device(tps6105x, &tps6105x_leds_cell);
		break;
	case TPS6105X_MODE_TORCH_FLASH:
		ret = tps6105x_add_device(tps6105x, &tps6105x_flash_cell);
		break;
	case TPS6105X_MODE_VOLTAGE:
		ret = tps6105x_add_device(tps6105x, &tps6105x_regulator_cell);
		break;
	default:
		dev_warn(&client->dev, "invalid mode: %d\n", pdata->mode);
		break;
	}

	if (ret)
		mfd_remove_devices(&client->dev);

	return ret;
}

static int tps6105x_remove(struct i2c_client *client)
{
	struct tps6105x *tps6105x = i2c_get_clientdata(client);

	mfd_remove_devices(&client->dev);

	/* Put chip in shutdown mode */
	regmap_update_bits(tps6105x->regmap, TPS6105X_REG_0,
		TPS6105X_REG0_MODE_MASK,
		TPS6105X_MODE_SHUTDOWN << TPS6105X_REG0_MODE_SHIFT);

	return 0;
}

static const struct i2c_device_id tps6105x_id[] = {
	{ "tps61050", 0 },
	{ "tps61052", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tps6105x_id);

static const struct of_device_id tps6105x_of_match[] = {
	{ .compatible = "ti,tps61050" },
	{ .compatible = "ti,tps61052" },
	{ },
};
MODULE_DEVICE_TABLE(of, tps6105x_of_match);

static struct i2c_driver tps6105x_driver = {
	.driver = {
		.name	= "tps6105x",
		.of_match_table = tps6105x_of_match,
	},
	.probe		= tps6105x_probe,
	.remove		= tps6105x_remove,
	.id_table	= tps6105x_id,
};

static int __init tps6105x_init(void)
{
	return i2c_add_driver(&tps6105x_driver);
}
subsys_initcall(tps6105x_init);

static void __exit tps6105x_exit(void)
{
	i2c_del_driver(&tps6105x_driver);
}
module_exit(tps6105x_exit);

MODULE_AUTHOR("Linus Walleij");
MODULE_DESCRIPTION("TPS6105x White LED Boost Converter Driver");
MODULE_LICENSE("GPL v2");
