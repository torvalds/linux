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
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "maxim4c_api.h"

#define MAX96715_I2C_ADDR_DEF		0x40

#define MAX96715_CHIP_ID		0x45
#define MAX96715_REG_CHIP_ID		0x1E

/* Config and Video mode switch */
#define MAX96715_MODE_SWITCH		1

enum {
	LINK_MODE_VIDEO = 0,
	LINK_MODE_CONFIG,
};

static int __maybe_unused max96715_link_mode_select(maxim4c_remote_t *max96715, u32 mode)
{
	struct device *dev = max96715->dev;
	struct i2c_client *client = max96715->client;
	u8 reg_mask = 0, reg_value = 0;
	u32 delay_ms = 0;
	int ret = 0;

	dev_dbg(dev, "%s: mode = %d\n", __func__, mode);

	reg_mask = BIT(7) | BIT(6);
	if (mode == LINK_MODE_CONFIG) {
		reg_value = BIT(6);
		delay_ms = 5;
	} else {
		reg_value = BIT(7);
		delay_ms = 50;
	}
	ret |= maxim4c_i2c_update_byte(client,
			0x04, MAXIM4C_I2C_REG_ADDR_08BITS,
			reg_mask, reg_value);

	msleep(delay_ms);

	return ret;
}

static int max96715_i2c_addr_remap(maxim4c_remote_t *max96715)
{
	struct device *dev = max96715->dev;
	struct i2c_client *client = max96715->client;
	u16 i2c_8bit_addr = 0;
	int ret = 0;

	if (max96715->ser_i2c_addr_map) {
		dev_info(dev, "Serializer i2c address remap\n");

		maxim4c_remote_i2c_addr_select(max96715, MAXIM4C_I2C_SER_DEF);

		i2c_8bit_addr = (max96715->ser_i2c_addr_map << 1);
		ret = maxim4c_i2c_write_byte(client,
				0x00, MAXIM4C_I2C_REG_ADDR_08BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "ser i2c address map setting error!\n");
			return ret;
		}

		maxim4c_remote_i2c_addr_select(max96715, MAXIM4C_I2C_SER_MAP);
	}

	if (max96715->cam_i2c_addr_map) {
		dev_info(dev, "Camera i2c address remap\n");

		i2c_8bit_addr = (max96715->cam_i2c_addr_map << 1);
		ret = maxim4c_i2c_write_byte(client,
				0x09, MAXIM4C_I2C_REG_ADDR_08BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "cam i2c address source setting error!\n");
			return ret;
		}

		i2c_8bit_addr = (max96715->cam_i2c_addr_def << 1);
		ret = maxim4c_i2c_write_byte(client,
				0x0A, MAXIM4C_I2C_REG_ADDR_08BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "cam i2c address destination setting error!\n");
			return ret;
		}
	}

	return 0;
}

static int max96715_i2c_addr_def(maxim4c_remote_t *max96715)
{
	struct device *dev = max96715->dev;
	struct i2c_client *client = max96715->client;
	u16 i2c_8bit_addr = 0;
	int ret = 0;

	if (max96715->ser_i2c_addr_map) {
		dev_info(dev, "Serializer i2c address def\n");

		maxim4c_remote_i2c_addr_select(max96715, MAXIM4C_I2C_SER_MAP);

		i2c_8bit_addr = (max96715->ser_i2c_addr_def << 1);
		ret = maxim4c_i2c_write_byte(client,
				0x00, MAXIM4C_I2C_REG_ADDR_08BITS,
				i2c_8bit_addr);
		if (ret) {
			dev_err(dev, "ser i2c address def setting error!\n");
			return ret;
		}

		maxim4c_remote_i2c_addr_select(max96715, MAXIM4C_I2C_SER_DEF);
	}

	return 0;
}

static int max96715_check_chipid(maxim4c_remote_t *max96715)
{
	struct device *dev = max96715->dev;
	struct i2c_client *client = max96715->client;
	u8 chip_id;
	int ret = 0;

	// max96715
	ret = maxim4c_i2c_read_byte(client,
			MAX96715_REG_CHIP_ID, MAXIM4C_I2C_REG_ADDR_08BITS,
			&chip_id);
	if (ret != 0) {
		dev_info(dev, "Retry check chipid using map address\n");
		maxim4c_remote_i2c_addr_select(max96715, MAXIM4C_I2C_SER_MAP);
		ret = maxim4c_i2c_read_byte(client,
				MAX96715_REG_CHIP_ID, MAXIM4C_I2C_REG_ADDR_08BITS,
				&chip_id);
		if (ret != 0) {
			dev_err(dev, "MAX96715 detect error, ret(%d)\n", ret);
			maxim4c_remote_i2c_addr_select(max96715, MAXIM4C_I2C_SER_DEF);

			return -ENODEV;
		}

		max96715_i2c_addr_def(max96715);
	}

	if (chip_id != MAX96715_CHIP_ID) {
		dev_err(dev, "Unexpected chip id = %02x\n", chip_id);
		return -ENODEV;
	}

	dev_info(dev, "Detected MAX96715 chip id: 0x%02x\n", chip_id);

	return 0;
}

static int max96715_soft_power_down(maxim4c_remote_t *max96715)
{
	struct device *dev = max96715->dev;
	struct i2c_client *client = max96715->client;
	int ret = 0;

	ret = maxim4c_i2c_write_byte(client,
			0x13, MAXIM4C_I2C_REG_ADDR_08BITS,
			BIT(7));
	if (ret) {
		dev_err(dev, "soft power down setting error!\n");
		return ret;
	}

	return 0;
}

static int max96715_module_init(maxim4c_remote_t *max96715)
{
	struct device *dev = max96715->dev;
	struct i2c_client *client = max96715->client;
	int ret = 0;

	ret = maxim4c_remote_i2c_addr_select(max96715, MAXIM4C_I2C_SER_DEF);
	if (ret)
		return ret;

	ret = max96715_check_chipid(max96715);
	if (ret)
		return ret;

	ret = max96715_i2c_addr_remap(max96715);
	if (ret)
		return ret;

#if MAX96715_MODE_SWITCH
	ret = max96715_link_mode_select(max96715, LINK_MODE_CONFIG);
	if (ret)
		return ret;
#endif

	ret = maxim4c_i2c_run_init_seq(client,
			&max96715->remote_init_seq);

	if (ret) {
		dev_err(dev, "remote id = %d init sequence error\n",
				max96715->remote_id);

		return ret;
	}

#if MAX96715_MODE_SWITCH
	ret = max96715_link_mode_select(max96715, LINK_MODE_VIDEO);
	if (ret)
		return ret;
#endif

	return 0;
}

static int max96715_module_deinit(maxim4c_remote_t *max96715)
{
	int ret = 0;

#if 0
	ret |= max96715_i2c_addr_def(max96715);
#endif
	ret |= max96715_soft_power_down(max96715);

	return ret;
}

static const struct maxim4c_remote_ops max96715_ops = {
	.remote_init = max96715_module_init,
	.remote_deinit = max96715_module_deinit,
};

static int max96715_parse_dt(maxim4c_remote_t *max96715)
{
	struct device *dev = max96715->dev;
	struct device_node *of_node = dev->of_node;
	u32 value = 0;
	int ret = 0;

	dev_info(dev, "=== maxim4c remote max96715 parse dt ===\n");

	ret = of_property_read_u32(of_node, "remote-id", &value);
	if (ret == 0) {
		dev_info(dev, "remote-id property: %d\n", value);
		max96715->remote_id = value;
	} else {
		max96715->remote_id = MAXIM4C_LINK_ID_MAX;
	}

	dev_info(dev, "max96715 remote id: %d\n", max96715->remote_id);

	ret = of_property_read_u32(of_node, "ser-i2c-addr-def", &value);
	if (ret == 0) {
		dev_info(dev, "ser-i2c-addr-def property: 0x%x", value);
		max96715->ser_i2c_addr_def = value;
	} else {
		max96715->ser_i2c_addr_def = MAX96715_I2C_ADDR_DEF;
	}

	ret = of_property_read_u32(of_node, "ser-i2c-addr-map", &value);
	if (ret == 0) {
		dev_info(dev, "ser-i2c-addr-map property: 0x%x", value);
		max96715->ser_i2c_addr_map = value;
	}

	ret = of_property_read_u32(of_node, "cam-i2c-addr-def", &value);
	if (ret == 0) {
		dev_info(dev, "cam-i2c-addr-def property: 0x%x", value);
		max96715->cam_i2c_addr_def = value;
	}

	ret = of_property_read_u32(of_node, "cam-i2c-addr-map", &value);
	if (ret == 0) {
		dev_info(dev, "cam-i2c-addr-map property: 0x%x", value);
		max96715->cam_i2c_addr_map = value;
	}

	return 0;
}

static int max96715_probe(struct platform_device *pdev)
{
	struct i2c_client *client = to_i2c_client(pdev->dev.parent);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);
	struct maxim4c_remote *max96715 = NULL;
	u32 link_id = MAXIM4C_LINK_ID_MAX;
	int ret = 0;

	dev_info(&pdev->dev, "max96715 serializer probe\n");

	link_id = (uintptr_t)of_device_get_match_data(&pdev->dev);
	link_id = link_id - MAXIM4C_LINK_ID_MAX;
	if (link_id >= MAXIM4C_LINK_ID_MAX) {
		dev_err(&pdev->dev, "max96715 probe match data error\n");
		return -EINVAL;
	}
	dev_info(&pdev->dev, "max96715 probe link id = %d\n", link_id);

	max96715 = devm_kzalloc(&pdev->dev, sizeof(*max96715), GFP_KERNEL);
	if (!max96715) {
		dev_err(&pdev->dev, "max96715 probe no memory error\n");
		return -ENOMEM;
	}

	max96715->dev = &pdev->dev;
	max96715->remote_ops = &max96715_ops;
	max96715->local = maxim4c;
	dev_set_drvdata(max96715->dev, max96715);

	max96715_parse_dt(max96715);

	if (max96715->remote_id != link_id) {
		dev_err(&pdev->dev, "max96715 probe remote_id error\n");
		return -EINVAL;
	}

	ret = maxim4c_remote_i2c_client_init(max96715, client);
	if (ret) {
		dev_err(&pdev->dev, "remote i2c client init error\n");
		return ret;
	}

	ret = maxim4c_remote_device_register(maxim4c, max96715);
	if (ret) {
		dev_err(&pdev->dev, "remote serializer register error\n");
		return ret;
	}

	maxim4c_remote_load_init_seq(max96715);

	return 0;
}

static int max96715_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id max96715_of_table[] = {
	{
		.compatible = "maxim4c,link0,max96715",
		.data = (const void *)(MAXIM4C_LINK_ID_MAX + MAXIM4C_LINK_ID_A)
	}, {
		.compatible = "maxim4c,link1,max96715",
		.data = (const void *)(MAXIM4C_LINK_ID_MAX + MAXIM4C_LINK_ID_B)
	}, {
		.compatible = "maxim4c,link2,max96715",
		.data = (const void *)(MAXIM4C_LINK_ID_MAX + MAXIM4C_LINK_ID_C)
	}, {
		.compatible = "maxim4c,link3,max96715",
		.data = (const void *)(MAXIM4C_LINK_ID_MAX + MAXIM4C_LINK_ID_D)
	},
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, max96715_of_table);

static struct platform_driver max96715_driver = {
	.probe		= max96715_probe,
	.remove		= max96715_remove,
	.driver		= {
		.name	= "maxim4c-max96715",
		.of_match_table = max96715_of_table,
	},
};

module_platform_driver(max96715_driver);

MODULE_AUTHOR("Cai Wenzhong <cwz@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96715 Serializer Driver");
MODULE_LICENSE("GPL");
