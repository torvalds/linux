/*
 * AS3711 PMIC MFC driver
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 * Author: Guennadi Liakhovetski, <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License as
 * published by the Free Software Foundation
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/as3711.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>

enum {
	AS3711_REGULATOR,
	AS3711_BACKLIGHT,
};

/*
 * Ok to have it static: it is only used during probing and multiple I2C devices
 * cannot be probed simultaneously. Just make sure to avoid stale data.
 */
static struct mfd_cell as3711_subdevs[] = {
	[AS3711_REGULATOR] = {.name = "as3711-regulator",},
	[AS3711_BACKLIGHT] = {.name = "as3711-backlight",},
};

static bool as3711_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AS3711_GPIO_SIGNAL_IN:
	case AS3711_INTERRUPT_STATUS_1:
	case AS3711_INTERRUPT_STATUS_2:
	case AS3711_INTERRUPT_STATUS_3:
	case AS3711_CHARGER_STATUS_1:
	case AS3711_CHARGER_STATUS_2:
	case AS3711_REG_STATUS:
		return true;
	}
	return false;
}

static bool as3711_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AS3711_INTERRUPT_STATUS_1:
	case AS3711_INTERRUPT_STATUS_2:
	case AS3711_INTERRUPT_STATUS_3:
		return true;
	}
	return false;
}

static bool as3711_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AS3711_SD_1_VOLTAGE:
	case AS3711_SD_2_VOLTAGE:
	case AS3711_SD_3_VOLTAGE:
	case AS3711_SD_4_VOLTAGE:
	case AS3711_LDO_1_VOLTAGE:
	case AS3711_LDO_2_VOLTAGE:
	case AS3711_LDO_3_VOLTAGE:
	case AS3711_LDO_4_VOLTAGE:
	case AS3711_LDO_5_VOLTAGE:
	case AS3711_LDO_6_VOLTAGE:
	case AS3711_LDO_7_VOLTAGE:
	case AS3711_LDO_8_VOLTAGE:
	case AS3711_SD_CONTROL:
	case AS3711_GPIO_SIGNAL_OUT:
	case AS3711_GPIO_SIGNAL_IN:
	case AS3711_SD_CONTROL_1:
	case AS3711_SD_CONTROL_2:
	case AS3711_CURR_CONTROL:
	case AS3711_CURR1_VALUE:
	case AS3711_CURR2_VALUE:
	case AS3711_CURR3_VALUE:
	case AS3711_STEPUP_CONTROL_1:
	case AS3711_STEPUP_CONTROL_2:
	case AS3711_STEPUP_CONTROL_4:
	case AS3711_STEPUP_CONTROL_5:
	case AS3711_REG_STATUS:
	case AS3711_INTERRUPT_STATUS_1:
	case AS3711_INTERRUPT_STATUS_2:
	case AS3711_INTERRUPT_STATUS_3:
	case AS3711_CHARGER_STATUS_1:
	case AS3711_CHARGER_STATUS_2:
	case AS3711_ASIC_ID_1:
	case AS3711_ASIC_ID_2:
		return true;
	}
	return false;
}

static const struct regmap_config as3711_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = as3711_volatile_reg,
	.readable_reg = as3711_readable_reg,
	.precious_reg = as3711_precious_reg,
	.max_register = AS3711_MAX_REGS,
	.num_reg_defaults_raw = AS3711_MAX_REGS,
	.cache_type = REGCACHE_RBTREE,
};

#ifdef CONFIG_OF
static const struct of_device_id as3711_of_match[] = {
	{.compatible = "ams,as3711",},
	{}
};
MODULE_DEVICE_TABLE(of, as3711_of_match);
#endif

static int as3711_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct as3711 *as3711;
	struct as3711_platform_data *pdata;
	unsigned int id1, id2;
	int ret;

	if (!client->dev.of_node) {
		pdata = dev_get_platdata(&client->dev);
		if (!pdata)
			dev_dbg(&client->dev, "Platform data not found\n");
	} else {
		pdata = devm_kzalloc(&client->dev,
				     sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
	}

	as3711 = devm_kzalloc(&client->dev, sizeof(struct as3711), GFP_KERNEL);
	if (!as3711)
		return -ENOMEM;

	as3711->dev = &client->dev;
	i2c_set_clientdata(client, as3711);

	if (client->irq)
		dev_notice(&client->dev, "IRQ not supported yet\n");

	as3711->regmap = devm_regmap_init_i2c(client, &as3711_regmap_config);
	if (IS_ERR(as3711->regmap)) {
		ret = PTR_ERR(as3711->regmap);
		dev_err(&client->dev,
			"regmap initialization failed: %d\n", ret);
		return ret;
	}

	ret = regmap_read(as3711->regmap, AS3711_ASIC_ID_1, &id1);
	if (!ret)
		ret = regmap_read(as3711->regmap, AS3711_ASIC_ID_2, &id2);
	if (ret < 0) {
		dev_err(&client->dev, "regmap_read() failed: %d\n", ret);
		return ret;
	}
	if (id1 != 0x8b)
		return -ENODEV;
	dev_info(as3711->dev, "AS3711 detected: %x:%x\n", id1, id2);

	/*
	 * We can reuse as3711_subdevs[],
	 * it will be copied in mfd_add_devices()
	 */
	if (pdata) {
		as3711_subdevs[AS3711_REGULATOR].platform_data =
			&pdata->regulator;
		as3711_subdevs[AS3711_REGULATOR].pdata_size =
			sizeof(pdata->regulator);
		as3711_subdevs[AS3711_BACKLIGHT].platform_data =
			&pdata->backlight;
		as3711_subdevs[AS3711_BACKLIGHT].pdata_size =
			sizeof(pdata->backlight);
	} else {
		as3711_subdevs[AS3711_REGULATOR].platform_data = NULL;
		as3711_subdevs[AS3711_REGULATOR].pdata_size = 0;
		as3711_subdevs[AS3711_BACKLIGHT].platform_data = NULL;
		as3711_subdevs[AS3711_BACKLIGHT].pdata_size = 0;
	}

	ret = mfd_add_devices(as3711->dev, -1, as3711_subdevs,
			      ARRAY_SIZE(as3711_subdevs), NULL, 0, NULL);
	if (ret < 0)
		dev_err(&client->dev, "add mfd devices failed: %d\n", ret);

	return ret;
}

static int as3711_i2c_remove(struct i2c_client *client)
{
	struct as3711 *as3711 = i2c_get_clientdata(client);

	mfd_remove_devices(as3711->dev);
	return 0;
}

static const struct i2c_device_id as3711_i2c_id[] = {
	{.name = "as3711", .driver_data = 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, as3711_i2c_id);

static struct i2c_driver as3711_i2c_driver = {
	.driver = {
		   .name = "as3711",
		   .of_match_table = of_match_ptr(as3711_of_match),
	},
	.probe = as3711_i2c_probe,
	.remove = as3711_i2c_remove,
	.id_table = as3711_i2c_id,
};

static int __init as3711_i2c_init(void)
{
	return i2c_add_driver(&as3711_i2c_driver);
}
/* Initialise early */
subsys_initcall(as3711_i2c_init);

static void __exit as3711_i2c_exit(void)
{
	i2c_del_driver(&as3711_i2c_driver);
}
module_exit(as3711_i2c_exit);

MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_DESCRIPTION("AS3711 PMIC driver");
MODULE_LICENSE("GPL v2");
