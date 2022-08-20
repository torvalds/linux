// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX96745 MFD driver
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
#include <linux/mfd/max96745.h>

struct max96745 {
	struct device *dev;
	struct regmap *regmap;
	struct i2c_mux_core *muxc;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *lock_gpio;
};

static const struct mfd_cell max96745_devs[] = {
	{
		.name = "max96745-pinctrl",
		.of_compatible = "maxim,max96745-pinctrl",
	}, {
		.name = "max96745-bridge",
		.of_compatible = "maxim,max96745-bridge",
	},
};

static bool max96745_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x0028 ... 0x0029:
	case 0x0032 ... 0x0033:
	case 0x0076:
	case 0x0086:
	case 0x0100:
	case 0x0200 ... 0x02ce:
	case 0x7000:
	case 0x7070:
	case 0x7074:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config max96745_regmap_config = {
	.name = "max96745",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x8000,
	.volatile_reg = max96745_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static int max96745_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct max96745 *max96745 = dev_get_drvdata(muxc->dev);

	if (chan == 1)
		regmap_update_bits(max96745->regmap, 0x0086, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 0));
	else
		regmap_update_bits(max96745->regmap, 0x0076, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 0));

	return 0;
}

static int max96745_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	struct max96745 *max96745 = dev_get_drvdata(muxc->dev);

	if (chan == 1)
		regmap_update_bits(max96745->regmap, 0x0086, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 1));
	else
		regmap_update_bits(max96745->regmap, 0x0076, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 1));

	return 0;
}

static void max96745_power_off(void *data)
{
	struct max96745 *max96745 = data;

	if (max96745->enable_gpio)
		gpiod_direction_output(max96745->enable_gpio, 0);
}

static void max96745_power_on(struct max96745 *max96745)
{
	u32 val;
	int ret;

	ret = regmap_read(max96745->regmap, 0x0107, &val);
	if (!ret && FIELD_GET(VID_TX_ACTIVE_A | VID_TX_ACTIVE_B, val))
		return;

	if (max96745->enable_gpio) {
		gpiod_direction_output(max96745->enable_gpio, 1);
		msleep(200);
	}

	regmap_update_bits(max96745->regmap, 0x0076, DIS_REM_CC,
			   FIELD_PREP(DIS_REM_CC, 1));
	regmap_update_bits(max96745->regmap, 0x0086, DIS_REM_CC,
			   FIELD_PREP(DIS_REM_CC, 1));
}

static ssize_t line_fault_monitor_show(struct device *device,
				       struct device_attribute *attr,
				       char *buf)
{
	struct max96745 *max96745 = dev_get_drvdata(device);
	u32 pu_lf, lf, status;

	regmap_read(max96745->regmap, 0x0005, &pu_lf);

	/*
	 * Line-fault status of wire connected to LMN0/1/2/3 pin
	 *
	 * 0b000: Short to battery
	 * 0b001: Short to GND
	 * 0b010: Normal operation
	 * 0b011: Line open
	 * 0b1XX: Line-to-line short
	 */
	regmap_read(max96745->regmap, 0x0026, &lf);

	if (FIELD_GET(PU_LF0, pu_lf)) {
		status = (lf & LF_0);
		return sprintf(buf, "%d\n", status);
	}

	if (FIELD_GET(PU_LF1, pu_lf)) {
		status = (lf & LF_1) >> 4;
		return sprintf(buf, "%d\n", status);
	}

	regmap_read(max96745->regmap, 0x0027, &lf);

	if (FIELD_GET(PU_LF2, pu_lf)) {
		status = (lf & LF_2);
		return sprintf(buf, "%d\n", status);
	}

	if (FIELD_GET(PU_LF3, pu_lf)) {
		status = (lf & LF_3) >> 4;
		return sprintf(buf, "%d\n", status);
	}


	return sprintf(buf, "%d\n", -EINVAL);
}

static DEVICE_ATTR_RO(line_fault_monitor);

static struct attribute *max96745_attrs[] = {
	&dev_attr_line_fault_monitor.attr,
	NULL
};

static const struct attribute_group max96745_attr_group = {
	.attrs = max96745_attrs,
};

static int max96745_sysfs_add(struct max96745 *max96745)
{
	struct device *dev = max96745->dev;
	int ret;
	u32 ch;

	ret = of_property_read_u32(dev->of_node, "line-fault-monitor", &ch);
	if (!ret)
		regmap_update_bits(max96745->regmap, 0x0005,
				   PU_LF0 << ch, PU_LF0 << ch);

	ret = devm_device_add_group(dev, &max96745_attr_group);
	if (ret) {
		dev_err(dev, "failed to register sysfs. err: %d\n", ret);
		return ret;
	};

	return 0;
}

static int max96745_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *child;
	struct max96745 *max96745;
	unsigned int nr = 0;
	bool idle_disc;
	int ret;

	for_each_available_child_of_node(dev->of_node, child) {
		if (!of_find_property(child, "reg", NULL))
			continue;

		nr++;
	}

	max96745 = devm_kzalloc(dev, sizeof(*max96745), GFP_KERNEL);
	if (!max96745)
		return -ENOMEM;

	idle_disc = device_property_read_bool(dev, "i2c-mux-idle-disconnect");

	max96745->muxc = i2c_mux_alloc(client->adapter, dev, nr,
				       0, I2C_MUX_LOCKED, max96745_select,
				       idle_disc ? max96745_deselect : NULL);
	if (!max96745->muxc)
		return -ENOMEM;

	max96745->dev = dev;
	i2c_set_clientdata(client, max96745);

	max96745->regmap = devm_regmap_init_i2c(client, &max96745_regmap_config);
	if (IS_ERR(max96745->regmap))
		return dev_err_probe(dev, PTR_ERR(max96745->regmap),
				     "failed to initialize regmap");

	max96745->enable_gpio = devm_gpiod_get_optional(dev, "enable",
							GPIOD_ASIS);
	if (IS_ERR(max96745->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(max96745->enable_gpio),
				     "failed to get enable GPIO\n");

	max96745->lock_gpio = devm_gpiod_get_optional(dev, "lock", GPIOD_IN);
	if (IS_ERR(max96745->lock_gpio))
		return dev_err_probe(dev, PTR_ERR(max96745->lock_gpio),
				     "failed to get lock GPIO\n");

	max96745_power_on(max96745);

	ret = devm_add_action_or_reset(dev, max96745_power_off, max96745);
	if (ret)
		return ret;

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, max96745_devs,
				   ARRAY_SIZE(max96745_devs), NULL, 0, NULL);
	if (ret)
		return ret;

	for_each_available_child_of_node(dev->of_node, child) {
		if (of_property_read_u32(child, "reg", &nr))
			continue;

		ret = i2c_mux_add_adapter(max96745->muxc, 0, nr, 0);
		if (ret) {
			i2c_mux_del_adapters(max96745->muxc);
			return ret;
		}
	}

	ret = max96745_sysfs_add(max96745);
	if (ret)
		return dev_err_probe(dev, ret, "failed to registers sysfs\n");

	return 0;
}

static int max96745_i2c_remove(struct i2c_client *client)
{
	struct max96745 *max96745 = i2c_get_clientdata(client);

	i2c_mux_del_adapters(max96745->muxc);

	return 0;
}

static void max96745_i2c_shutdown(struct i2c_client *client)
{
	struct max96745 *max96745 = i2c_get_clientdata(client);

	max96745_power_off(max96745);
}

static int __maybe_unused max96745_suspend(struct device *dev)
{
	struct max96745 *max96745 = dev_get_drvdata(dev);

	regcache_mark_dirty(max96745->regmap);
	regcache_cache_only(max96745->regmap, true);

	return 0;
}

static int __maybe_unused max96745_resume(struct device *dev)
{
	struct max96745 *max96745 = dev_get_drvdata(dev);

	regcache_cache_only(max96745->regmap, false);
	regcache_sync(max96745->regmap);

	return 0;
}

static SIMPLE_DEV_PM_OPS(max96745_pm_ops, max96745_suspend, max96745_resume);

static const struct of_device_id max96745_of_match[] = {
	{ .compatible = "maxim,max96745", },
	{}
};
MODULE_DEVICE_TABLE(of, max96745_of_match);

static struct i2c_driver max96745_i2c_driver = {
	.driver = {
		.name = "max96745",
		.of_match_table = max96745_of_match,
		.pm = &max96745_pm_ops,
	},
	.probe_new = max96745_i2c_probe,
	.remove = max96745_i2c_remove,
	.shutdown = max96745_i2c_shutdown,
};

module_i2c_driver(max96745_i2c_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96745 MFD driver");
MODULE_LICENSE("GPL");
