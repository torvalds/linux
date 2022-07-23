// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX96752F MFD driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max96752f.h>

static const struct mfd_cell max96752f_devs[] = {
	{
		.name = "max96752f-pinctrl",
		.of_compatible = "maxim,max96752f-pinctrl",
	}, {
		.name = "max96752f-gpio",
		.of_compatible = "maxim,max96752f-gpio",
	}, {
		.name = "max96752f-bridge",
		.of_compatible = "maxim,max96752f-bridge",
	},
};

static int max96752f_select(struct i2c_mux_core *muxc, u32 chan)
{
	return 0;
}

static const struct regmap_config max96752f_regmap_config = {
	.name = "max96752f",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x25d,
};

static const unsigned short addr_list[] = {
	0x48, 0x4a, 0x4c, 0x68, 0x6a, 0x6c, 0x28, 0x2a, I2C_CLIENT_END
};

void max96752f_init(struct max96752f *max96752f)
{
	struct i2c_client *client = max96752f->client;
	u16 addr = client->addr;
	u32 id;
	int i, ret;

	for (i = 0; addr_list[i] != I2C_CLIENT_END; i++) {
		client->addr = addr_list[i];
		ret = regmap_read(max96752f->regmap, 0x000d, &id);
		if (ret < 0)
			continue;

		if (id == 0x82) {
			regmap_write(max96752f->regmap, 0x0000, addr << 1);
			break;
		}
	}

	client->addr = addr;

	regmap_update_bits(max96752f->regmap, 0x0050, STR_SEL,
			   FIELD_PREP(STR_SEL, max96752f->stream_id));
	regmap_update_bits(max96752f->regmap, 0x0073, TX_SRC_ID,
			   FIELD_PREP(TX_SRC_ID, max96752f->stream_id));
}
EXPORT_SYMBOL(max96752f_init);

static void max96752f_power_on(struct max96752f *max96752f)
{
	if (max96752f->enable_gpio) {
		gpiod_direction_output(max96752f->enable_gpio, 1);
		msleep(500);
	}
}

static void max96752f_power_off(struct max96752f *max96752f)
{
	if (max96752f->enable_gpio)
		gpiod_direction_output(max96752f->enable_gpio, 0);
}

static int max96752f_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *child;
	struct max96752f *max96752f;
	unsigned int nr = 0;
	int ret;

	for_each_available_child_of_node(dev->of_node, child) {
		if (!of_find_property(child, "reg", NULL))
			continue;

		nr++;
	}

	max96752f = devm_kzalloc(dev, sizeof(*max96752f), GFP_KERNEL);
	if (!max96752f)
		return -ENOMEM;

	max96752f->muxc = i2c_mux_alloc(client->adapter, dev, nr, 0,
				       I2C_MUX_LOCKED, max96752f_select, NULL);
	if (!max96752f->muxc)
		return -ENOMEM;

	max96752f->dev = dev;
	max96752f->client = client;

	max96752f->enable_gpio = devm_gpiod_get_optional(dev, "enable",
							 GPIOD_ASIS);
	if (IS_ERR(max96752f->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(max96752f->enable_gpio),
				     "failed to get enable GPIO\n");

	ret = device_property_read_u32(dev->parent, "reg", &max96752f->stream_id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get gmsl id\n");

	i2c_set_clientdata(client, max96752f);

	max96752f->regmap = devm_regmap_init_i2c(client,
						 &max96752f_regmap_config);
	if (IS_ERR(max96752f->regmap))
		return dev_err_probe(dev, PTR_ERR(max96752f->regmap),
				     "failed to initialize regmap\n");

	max96752f_power_on(max96752f);
	max96752f_init(max96752f);

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, max96752f_devs,
				   ARRAY_SIZE(max96752f_devs), NULL, 0, NULL);
	if (ret)
		return ret;

	for_each_available_child_of_node(dev->of_node, child) {
		if (of_property_read_u32(child, "reg", &nr))
			continue;

		ret = i2c_mux_add_adapter(max96752f->muxc, 0, nr, 0);
		if (ret) {
			i2c_mux_del_adapters(max96752f->muxc);
			return ret;
		}
	}

	return 0;
}

static void max96752f_i2c_shutdown(struct i2c_client *client)
{
	struct max96752f *max96752f = i2c_get_clientdata(client);

	regmap_update_bits(max96752f->regmap, 0x0010, RESET_ALL,
			   FIELD_PREP(RESET_ALL, 1));

	max96752f_power_off(max96752f);
}

static int __maybe_unused max96752f_suspend(struct device *dev)
{
	struct max96752f *max96752f = dev_get_drvdata(dev);

	max96752f_power_off(max96752f);

	return 0;
}

static int __maybe_unused max96752f_resume(struct device *dev)
{
	struct max96752f *max96752f = dev_get_drvdata(dev);

	max96752f_power_on(max96752f);

	return 0;
}

static SIMPLE_DEV_PM_OPS(max96752f_pm_ops, max96752f_suspend, max96752f_resume);

static const struct of_device_id max96752f_of_match[] = {
	{ .compatible = "maxim,max96752f", },
	{}
};
MODULE_DEVICE_TABLE(of, max96752f_of_match);

static struct i2c_driver max96752f_i2c_driver = {
	.driver = {
		.name = "max96752f",
		.of_match_table = of_match_ptr(max96752f_of_match),
		.pm = &max96752f_pm_ops,
	},
	.probe_new = max96752f_i2c_probe,
	.shutdown = max96752f_i2c_shutdown,
};

module_i2c_driver(max96752f_i2c_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96752F MFD driver");
MODULE_LICENSE("GPL");
