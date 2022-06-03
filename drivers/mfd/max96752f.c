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

static bool max96752f_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x0002:
	case 0x0050:
	case 0x0073:
	case 0x0140:
	case 0x01ce:
	case 0x0203 ... 0x022f:
		return false;
	default:
		return true;
	}
}

static const struct reg_default max96752f_reg_defaults[] = {
	{ 0x0002, 0x47 }, { 0x0050, 0x00 }, { 0x0073, 0x30 },
	{ 0x0140, 0x21 },
	{ 0x01ce, 0x06 },
	{ 0x0203, 0x84 }, { 0x0204, 0xa1 }, { 0x0205, 0x41 },
	{ 0x0206, 0x81 }, { 0x0207, 0xa2 }, { 0x0208, 0x42 },
	{ 0x0209, 0x81 }, { 0x020a, 0xa3 }, { 0x020b, 0x43 },
	{ 0x020c, 0x81 }, { 0x020d, 0xa4 }, { 0x020e, 0x44 },
	{ 0x020f, 0x81 }, { 0x0210, 0xa5 }, { 0x0211, 0x45 },
	{ 0x0212, 0x81 }, { 0x0213, 0xa6 }, { 0x0214, 0x46 },
	{ 0x0215, 0x81 }, { 0x0216, 0xa7 }, { 0x0217, 0x47 },
	{ 0x0218, 0x81 }, { 0x0219, 0xa8 }, { 0x021a, 0x48 },
	{ 0x021b, 0x80 }, { 0x021c, 0xa9 }, { 0x021d, 0x49 },
	{ 0x021e, 0x80 }, { 0x021f, 0xaa }, { 0x0220, 0x4a },
	{ 0x0221, 0x80 }, { 0x0222, 0x2b }, { 0x0223, 0x4b },
	{ 0x0224, 0x80 }, { 0x0225, 0x2c }, { 0x0226, 0x4c },
	{ 0x0227, 0x80 }, { 0x0228, 0x2d }, { 0x0229, 0x4d },
	{ 0x022a, 0x18 }, { 0x022b, 0x4e }, { 0x022c, 0x4e },
	{ 0x022d, 0x18 }, { 0x022e, 0x4f }, { 0x022f, 0x4f },
};

static const struct regmap_config max96752f_regmap_config = {
	.name = "max96752f",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x25d,
	.volatile_reg = max96752f_volatile_reg,
	.reg_defaults = max96752f_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(max96752f_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

static const unsigned short addr_list[] = {
	0x48, 0x4a, 0x4c, 0x68, 0x6a, 0x6c, 0x28, 0x2a, I2C_CLIENT_END
};

static void max96752f_check_addr(struct max96752f *max96752f)
{
	struct i2c_client *client = max96752f->client;
	u16 addr = client->addr;
	u32 id;
	int i, ret;

	if (!regmap_read(max96752f->regmap, 0x000d, &id))
		return;

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
}

void max96752f_regcache_sync(struct max96752f *max96752f)
{
	regcache_cache_only(max96752f->regmap, false);

	max96752f_check_addr(max96752f);

	regcache_sync(max96752f->regmap);
}
EXPORT_SYMBOL(max96752f_regcache_sync);

static int max96752f_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *child;
	struct max96752f *max96752f;
	unsigned int nr = 0;
	u32 stream_id;
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

	ret = device_property_read_u32(dev->parent, "reg", &stream_id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get gmsl id\n");

	i2c_set_clientdata(client, max96752f);

	max96752f->regmap = devm_regmap_init_i2c(client,
						 &max96752f_regmap_config);
	if (IS_ERR(max96752f->regmap))
		return dev_err_probe(dev, PTR_ERR(max96752f->regmap),
				     "failed to initialize regmap\n");

	max96752f_check_addr(max96752f);

	regmap_update_bits(max96752f->regmap, 0x0050, STR_SEL,
			   FIELD_PREP(STR_SEL, stream_id));
	regmap_update_bits(max96752f->regmap, 0x0073, TX_SRC_ID,
			   FIELD_PREP(TX_SRC_ID, stream_id));

	regcache_mark_dirty(max96752f->regmap);

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
}

static const struct of_device_id max96752f_of_match[] = {
	{ .compatible = "maxim,max96752f", },
	{}
};
MODULE_DEVICE_TABLE(of, max96752f_of_match);

static struct i2c_driver max96752f_i2c_driver = {
	.driver = {
		.name = "max96752f",
		.of_match_table = of_match_ptr(max96752f_of_match),
	},
	.probe_new = max96752f_i2c_probe,
	.shutdown = max96752f_i2c_shutdown,
};

module_i2c_driver(max96752f_i2c_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96752F MFD driver");
MODULE_LICENSE("GPL");
