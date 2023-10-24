// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Dual GMSL2/GMSL1 to CSI-2 Serializer driver
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

#include "maxim2c_api.h"

#define MAX96717_I2C_ADDR_DEF		0x40

#define MAX96717_CHIP_ID		0xBF
#define MAX96717_REG_CHIP_ID		0x0D

static int max96717_i2c_addr_remap(maxim2c_remote_t *max96717)
{
	struct device *dev = max96717->dev;
	struct i2c_client *client = max96717->client;
	u16 i2c_8bit_addr = 0;
	int ret = 0;

	if (max96717->ser_i2c_addr_map) {
		dev_info(dev, "Serializer i2c address remap\n");

		maxim2c_remote_i2c_addr_select(max96717, MAXIM2C_I2C_SER_DEF);

		i2c_8bit_addr = (max96717->ser_i2c_addr_map << 1);
		ret = maxim2c_i2c_write_byte(client,
				0x0000, MAXIM2C_I2C_REG_ADDR_16BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "ser i2c address map setting error!\n");
			return ret;
		}

		maxim2c_remote_i2c_addr_select(max96717, MAXIM2C_I2C_SER_MAP);
	}

	if (max96717->cam_i2c_addr_map) {
		dev_info(dev, "Camera i2c address remap\n");

		i2c_8bit_addr = (max96717->cam_i2c_addr_map << 1);
		ret = maxim2c_i2c_write_byte(client,
				0x0042, MAXIM2C_I2C_REG_ADDR_16BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "cam i2c address source setting error!\n");
			return ret;
		}

		i2c_8bit_addr = (max96717->cam_i2c_addr_def << 1);
		ret = maxim2c_i2c_write_byte(client,
				0x0043, MAXIM2C_I2C_REG_ADDR_16BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "cam i2c address destination setting error!\n");
			return ret;
		}
	}

	return 0;
}

static int max96717_i2c_addr_def(maxim2c_remote_t *max96717)
{
	struct device *dev = max96717->dev;
	struct i2c_client *client = max96717->client;
	u16 i2c_8bit_addr = 0;
	int ret = 0;

	if (max96717->ser_i2c_addr_map) {
		dev_info(dev, "Serializer i2c address def\n");

		maxim2c_remote_i2c_addr_select(max96717, MAXIM2C_I2C_SER_MAP);

		i2c_8bit_addr = (max96717->ser_i2c_addr_def << 1);
		ret = maxim2c_i2c_write_byte(client,
				0x0000, MAXIM2C_I2C_REG_ADDR_16BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "ser i2c address def setting error!\n");
			return ret;
		}

		maxim2c_remote_i2c_addr_select(max96717, MAXIM2C_I2C_SER_DEF);
	}

	return 0;
}

static int max96717_check_chipid(maxim2c_remote_t *max96717)
{
	struct device *dev = max96717->dev;
	struct i2c_client *client = max96717->client;
	u8 chip_id;
	int ret = 0;

	// max96717
	ret = maxim2c_i2c_read_byte(client,
			MAX96717_REG_CHIP_ID, MAXIM2C_I2C_REG_ADDR_16BITS,
			&chip_id);
	if (ret != 0) {
		dev_info(dev, "Retry check chipid using map address\n");
		maxim2c_remote_i2c_addr_select(max96717, MAXIM2C_I2C_SER_MAP);
		ret = maxim2c_i2c_read_byte(client,
				MAX96717_REG_CHIP_ID, MAXIM2C_I2C_REG_ADDR_16BITS,
				&chip_id);
		if (ret != 0) {
			dev_err(dev, "MAX96717 detect error, ret(%d)\n", ret);
			maxim2c_remote_i2c_addr_select(max96717, MAXIM2C_I2C_SER_DEF);

			return -ENODEV;
		}

		max96717_i2c_addr_def(max96717);
	}

	if (chip_id != MAX96717_CHIP_ID) {
		dev_err(dev, "Unexpected chip id = %02x\n", chip_id);
		return -ENODEV;
	}
	dev_info(dev, "Detected MAX96717 chip id: 0x%02x\n", chip_id);

	return 0;
}

static int max96717_module_init(maxim2c_remote_t *max96717)
{
	struct device *dev = max96717->dev;
	struct i2c_client *client = max96717->client;
	int ret = 0;

	ret = maxim2c_remote_i2c_addr_select(max96717, MAXIM2C_I2C_SER_DEF);
	if (ret)
		return ret;

	ret = max96717_check_chipid(max96717);
	if (ret)
		return ret;

	ret = max96717_i2c_addr_remap(max96717);
	if (ret)
		return ret;

	ret = maxim2c_i2c_run_init_seq(client,
			&max96717->remote_init_seq);
	if (ret) {
		dev_err(dev, "remote id = %d init sequence error\n",
				max96717->remote_id);
		return ret;
	}

	return 0;
}

static int max96717_module_deinit(maxim2c_remote_t *max96717)
{
	int ret = 0;

	ret |= max96717_i2c_addr_def(max96717);

	return ret;
}

static const struct maxim2c_remote_ops max96717_ops = {
	.remote_init = max96717_module_init,
	.remote_deinit = max96717_module_deinit,
};

static int max96717_parse_dt(maxim2c_remote_t *max96717)
{
	struct device *dev = max96717->dev;
	struct device_node *of_node = dev->of_node;
	u32 value = 0;
	int ret = 0;

	dev_info(dev, "=== maxim2c remote max96717 parse dt ===\n");

	ret = of_property_read_u32(of_node, "remote-id", &value);
	if (ret == 0) {
		dev_info(dev, "remote-id property: %d\n", value);
		max96717->remote_id = value;
	} else {
		max96717->remote_id = MAXIM2C_LINK_ID_MAX;
	}

	dev_info(dev, "max96717 remote id: %d\n", max96717->remote_id);

	ret = of_property_read_u32(of_node, "ser-i2c-addr-def", &value);
	if (ret == 0) {
		dev_info(dev, "ser-i2c-addr-def property: 0x%x", value);
		max96717->ser_i2c_addr_def = value;
	} else {
		max96717->ser_i2c_addr_def = MAX96717_I2C_ADDR_DEF;
	}

	ret = of_property_read_u32(of_node, "ser-i2c-addr-map", &value);
	if (ret == 0) {
		dev_info(dev, "ser-i2c-addr-map property: 0x%x", value);
		max96717->ser_i2c_addr_map = value;
	}

	ret = of_property_read_u32(of_node, "cam-i2c-addr-def", &value);
	if (ret == 0) {
		dev_info(dev, "cam-i2c-addr-def property: 0x%x", value);
		max96717->cam_i2c_addr_def = value;
	}

	ret = of_property_read_u32(of_node, "cam-i2c-addr-map", &value);
	if (ret == 0) {
		dev_info(dev, "cam-i2c-addr-map property: 0x%x", value);
		max96717->cam_i2c_addr_map = value;
	}

	return 0;
}

static int max96717_probe(struct platform_device *pdev)
{
	struct i2c_client *client = to_i2c_client(pdev->dev.parent);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct maxim2c *maxim2c = v4l2_get_subdevdata(sd);
	struct maxim2c_remote *max96717 = NULL;
	u32 link_id = MAXIM2C_LINK_ID_MAX;
	int ret = 0;

	dev_info(&pdev->dev, "max96717 serializer probe\n");

	link_id = (uintptr_t)of_device_get_match_data(&pdev->dev);
	link_id = link_id - MAXIM2C_LINK_ID_MAX;
	if (link_id >= MAXIM2C_LINK_ID_MAX) {
		dev_err(&pdev->dev, "max96717 probe match data error\n");
		return -EINVAL;
	}
	dev_info(&pdev->dev, "max96717 probe link id = %d\n", link_id);

	max96717 = devm_kzalloc(&pdev->dev, sizeof(*max96717), GFP_KERNEL);
	if (!max96717) {
		dev_err(&pdev->dev, "max96717 probe no memory error\n");
		return -ENOMEM;
	}

	max96717->dev = &pdev->dev;
	max96717->remote_ops = &max96717_ops;
	max96717->local = maxim2c;
	dev_set_drvdata(max96717->dev, max96717);

	max96717_parse_dt(max96717);

	if (max96717->remote_id != link_id) {
		dev_err(&pdev->dev, "max96717 probe remote_id error\n");
		return -EINVAL;
	}

	ret = maxim2c_remote_i2c_client_init(max96717, client);
	if (ret) {
		dev_err(&pdev->dev, "remote i2c client init error\n");
		return ret;
	}

	ret = maxim2c_remote_device_register(maxim2c, max96717);
	if (ret) {
		dev_err(&pdev->dev, "remote serializer register error\n");
		return ret;
	}

	maxim2c_remote_load_init_seq(max96717);

	return 0;
}

static int max96717_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id max96717_of_table[] = {
	{
		.compatible = "maxim2c,link0,max96717",
		.data = (const void *)(MAXIM2C_LINK_ID_MAX + MAXIM2C_LINK_ID_A)
	}, {
		.compatible = "maxim2c,link1,max96717",
		.data = (const void *)(MAXIM2C_LINK_ID_MAX + MAXIM2C_LINK_ID_B)
	},
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, max96717_of_table);

static struct platform_driver max96717_driver = {
	.probe		= max96717_probe,
	.remove		= max96717_remove,
	.driver		= {
		.name	= "maxim2c-max96717",
		.of_match_table = max96717_of_table,
	},
};

module_platform_driver(max96717_driver);

MODULE_AUTHOR("Cai Wenzhong <cwz@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96717 Serializer Driver");
MODULE_LICENSE("GPL");
