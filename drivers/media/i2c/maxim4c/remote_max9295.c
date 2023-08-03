// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Quad GMSL2/GMSL1 to CSI-2 Serializer driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "maxim4c_api.h"

#define MAX9295_I2C_ADDR_DEF		0x40

#define MAX9295_CHIP_ID			0x91
#define MAX9295_REG_CHIP_ID		0x0D

static int max9295_i2c_addr_select(maxim4c_remote_t *max9295, u32 i2c_id)
{
	struct device *dev = max9295->dev;
	struct i2c_client *client = max9295->client;

	if (i2c_id == MAXIM4C_I2C_SER_DEF) {
		client->addr = max9295->ser_i2c_addr_def;
		dev_info(dev, "select default i2c addr = 0x%x\n", client->addr);
	} else if (i2c_id == MAXIM4C_I2C_SER_MAP) {
		client->addr = max9295->ser_i2c_addr_map;
		dev_info(dev, "select mapping i2c addr = 0x%x\n", client->addr);
	} else {
		dev_err(dev, "i2c select id = %d error\n", i2c_id);
		return -EINVAL;
	}

	return 0;
}

static int max9295_i2c_addr_remap(maxim4c_remote_t *max9295)
{
	struct device *dev = max9295->dev;
	struct i2c_client *client = max9295->client;
	u16 i2c_8bit_addr = 0;
	int ret = 0;

	if (max9295->ser_i2c_addr_map) {
		dev_info(dev, "Serializer i2c address remap\n");

		max9295_i2c_addr_select(max9295, MAXIM4C_I2C_SER_DEF);

		i2c_8bit_addr = (max9295->ser_i2c_addr_map << 1);
		ret = maxim4c_i2c_write_byte(client,
				0x0000, MAXIM4C_I2C_REG_ADDR_16BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "ser i2c address map setting error!\n");
			return ret;
		}

		max9295_i2c_addr_select(max9295, MAXIM4C_I2C_SER_MAP);
	}

	if (max9295->cam_i2c_addr_map) {
		dev_info(dev, "Camera i2c address remap\n");

		i2c_8bit_addr = (max9295->cam_i2c_addr_map << 1);
		ret = maxim4c_i2c_write_byte(client,
				0x0042, MAXIM4C_I2C_REG_ADDR_16BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "cam i2c address source setting error!\n");
			return ret;
		}

		i2c_8bit_addr = (max9295->cam_i2c_addr_def << 1);
		ret = maxim4c_i2c_write_byte(client,
				0x0043, MAXIM4C_I2C_REG_ADDR_16BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "cam i2c address destination setting error!\n");
			return ret;
		}
	}

	return 0;
}

static int max9295_i2c_addr_def(maxim4c_remote_t *max9295)
{
	struct device *dev = max9295->dev;
	struct i2c_client *client = max9295->client;
	u16 i2c_8bit_addr = 0;
	int ret = 0;

	if (max9295->ser_i2c_addr_map) {
		dev_info(dev, "Serializer i2c address def\n");

		max9295_i2c_addr_select(max9295, MAXIM4C_I2C_SER_MAP);

		i2c_8bit_addr = (max9295->ser_i2c_addr_def << 1);
		ret = maxim4c_i2c_write_byte(client,
				0x0000, MAXIM4C_I2C_REG_ADDR_16BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "ser i2c address def setting error!\n");
			return ret;
		}

		max9295_i2c_addr_select(max9295, MAXIM4C_I2C_SER_DEF);
	}

	return 0;
}

static int max9295_check_chipid(maxim4c_remote_t *max9295)
{
	struct device *dev = max9295->dev;
	struct i2c_client *client = max9295->client;
	u8 chip_id;
	int ret = 0;

	// max9295
	ret = maxim4c_i2c_read_byte(client,
			MAX9295_REG_CHIP_ID, MAXIM4C_I2C_REG_ADDR_16BITS,
			&chip_id);
	if (ret != 0) {
		dev_info(dev, "Retry check chipid using map address\n");
		max9295_i2c_addr_select(max9295, MAXIM4C_I2C_SER_MAP);
		ret = maxim4c_i2c_read_byte(client,
				MAX9295_REG_CHIP_ID, MAXIM4C_I2C_REG_ADDR_16BITS,
				&chip_id);
		if (ret != 0) {
			dev_err(dev, "MAX9295 detect error, ret(%d)\n", ret);
			max9295_i2c_addr_select(max9295, MAXIM4C_I2C_SER_DEF);

			return -ENODEV;
		}

		max9295_i2c_addr_def(max9295);
	}

	if (chip_id != MAX9295_CHIP_ID) {
		dev_err(dev, "Unexpected MAX9295 chip id(%02x)\n", chip_id);
		return -ENODEV;
	}

	dev_info(dev, "Detected MAX9295 chipid: 0x%02x\n", chip_id);

	return ret;
}

static int max9295_soft_power_down(maxim4c_remote_t *max9295)
{
	struct device *dev = max9295->dev;
	struct i2c_client *client = max9295->client;
	int ret = 0;

	ret = maxim4c_i2c_write_byte(client,
			0x10, MAXIM4C_I2C_REG_ADDR_16BITS,
			BIT(7));
	if (ret) {
		dev_err(dev, "soft power down setting error!\n");
		return ret;
	}

	return 0;
}

static int max9295_module_init(maxim4c_remote_t *max9295)
{
	struct device *dev = max9295->dev;
	struct i2c_client *client = max9295->client;
	int ret = 0;

	ret = max9295_i2c_addr_select(max9295, MAXIM4C_I2C_SER_DEF);
	if (ret)
		return ret;

	ret = max9295_check_chipid(max9295);
	if (ret)
		return ret;

	ret = max9295_i2c_addr_remap(max9295);
	if (ret)
		return ret;

	ret = maxim4c_i2c_run_init_seq(client,
			&max9295->remote_init_seq);
	if (ret) {
		dev_err(dev, "remote id = %d init sequence error\n",
				max9295->remote_id);
		return ret;
	}

	return 0;
}

static int max9295_module_deinit(maxim4c_remote_t *max9295)
{
	int ret = 0;

#if 0
	ret |= max9295_i2c_addr_def(max9295);
#endif
	ret |= max9295_soft_power_down(max9295);

	return ret;
}

static const struct maxim4c_remote_ops max9295_ops = {
	.remote_init = max9295_module_init,
	.remote_deinit = max9295_module_deinit,
};

static int max9295_parse_dt(maxim4c_remote_t *max9295)
{
	struct device *dev = max9295->dev;
	struct device_node *of_node = dev->of_node;
	u32 value = 0;
	int ret = 0;

	dev_info(dev, "=== maxim4c remote max9295 parse dt ===\n");

	ret = of_property_read_u32(of_node, "remote-id", &value);
	if (ret == 0) {
		dev_info(dev, "remote-id property: %d\n", value);
		max9295->remote_id = value;
	} else {
		max9295->remote_id = MAXIM4C_LINK_ID_MAX;
	}

	dev_info(dev, "max9295 remote id: %d\n", max9295->remote_id);

	ret = of_property_read_u32(of_node, "ser-i2c-addr-def", &value);
	if (ret == 0) {
		dev_info(dev, "ser-i2c-addr-def property: 0x%x", value);
		max9295->ser_i2c_addr_def = value;
	} else {
		max9295->ser_i2c_addr_def = MAX9295_I2C_ADDR_DEF;
	}

	ret = of_property_read_u32(of_node, "ser-i2c-addr-map", &value);
	if (ret == 0) {
		dev_info(dev, "ser-i2c-addr-map property: 0x%x", value);
		max9295->ser_i2c_addr_map = value;
	}

	ret = of_property_read_u32(of_node, "cam-i2c-addr-def", &value);
	if (ret == 0) {
		dev_info(dev, "cam-i2c-addr-def property: 0x%x", value);
		max9295->cam_i2c_addr_def = value;
	}

	ret = of_property_read_u32(of_node, "cam-i2c-addr-map", &value);
	if (ret == 0) {
		dev_info(dev, "cam-i2c-addr-map property: 0x%x", value);
		max9295->cam_i2c_addr_map = value;
	}

	return 0;
}

static int max9295_i2c_client_init(maxim4c_remote_t *max9295,
				struct i2c_client *local_client)
{
	struct device *dev = max9295->dev;
	struct i2c_client *remote_client = NULL;
	u16 remote_client_addr = 0;

	if (max9295->ser_i2c_addr_map)
		remote_client_addr = max9295->ser_i2c_addr_map;
	else
		remote_client_addr = max9295->ser_i2c_addr_def;
	remote_client = devm_i2c_new_dummy_device(&local_client->dev,
				local_client->adapter, remote_client_addr);
	if (IS_ERR(remote_client)) {
		dev_err(dev, "failed to alloc i2c client.\n");
		return -PTR_ERR(remote_client);
	}
	remote_client->addr = max9295->ser_i2c_addr_def;

	max9295->client = remote_client;
	i2c_set_clientdata(remote_client, max9295);

	dev_info(dev, "remote i2c client init, i2c_addr = 0x%x\n",
		remote_client_addr);

	return 0;
}

static int max9295_probe(struct platform_device *pdev)
{
	struct i2c_client *client = to_i2c_client(pdev->dev.parent);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	struct maxim4c_remote *max9295 = NULL;
	int ret = 0;

	dev_info(&pdev->dev, "max9295 serializer probe\n");

	max9295 = devm_kzalloc(&pdev->dev, sizeof(*max9295), GFP_KERNEL);
	if (!max9295)
		return -ENOMEM;

	max9295->dev = &pdev->dev;
	max9295->remote_ops = &max9295_ops;
	max9295->local = maxim4c;
	dev_set_drvdata(max9295->dev, max9295);

	max9295_parse_dt(max9295);

	max9295_i2c_client_init(max9295, client);

	ret = maxim4c_remote_device_register(maxim4c, max9295);
	if (ret)
		return ret;

	maxim4c_remote_load_init_seq(max9295);

	return 0;
}

static int max9295_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id max9295_of_table[] = {
	{ .compatible = "maxim4c,max9295", },
	{ /* Sentinel */ },
};

MODULE_DEVICE_TABLE(of, max9295_of_table);

static struct platform_driver max9295_driver = {
	.probe		= max9295_probe,
	.remove		= max9295_remove,
	.driver		= {
		.name	= "max9295",
		.of_match_table = max9295_of_table,
	},
};

module_platform_driver(max9295_driver);

MODULE_AUTHOR("Cai Wenzhong <cwz@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX9295 Serializer Driver");
MODULE_LICENSE("GPL");
