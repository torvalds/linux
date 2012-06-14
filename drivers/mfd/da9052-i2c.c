/*
 * I2C access for DA9052 PMICs.
 *
 * Copyright(c) 2011 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/mfd/core.h>
#include <linux/i2c.h>
#include <linux/err.h>

#include <linux/mfd/da9052/da9052.h>
#include <linux/mfd/da9052/reg.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_device.h>
#endif

static int da9052_i2c_enable_multiwrite(struct da9052 *da9052)
{
	int reg_val, ret;

	ret = regmap_read(da9052->regmap, DA9052_CONTROL_B_REG, &reg_val);
	if (ret < 0)
		return ret;

	if (reg_val & DA9052_CONTROL_B_WRITEMODE) {
		reg_val &= ~DA9052_CONTROL_B_WRITEMODE;
		ret = regmap_write(da9052->regmap, DA9052_CONTROL_B_REG,
				   reg_val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static struct i2c_device_id da9052_i2c_id[] = {
	{"da9052", DA9052},
	{"da9053-aa", DA9053_AA},
	{"da9053-ba", DA9053_BA},
	{"da9053-bb", DA9053_BB},
	{}
};

#ifdef CONFIG_OF
static const struct of_device_id dialog_dt_ids[] = {
	{ .compatible = "dlg,da9052", .data = &da9052_i2c_id[0] },
	{ .compatible = "dlg,da9053-aa", .data = &da9052_i2c_id[1] },
	{ .compatible = "dlg,da9053-ab", .data = &da9052_i2c_id[2] },
	{ .compatible = "dlg,da9053-bb", .data = &da9052_i2c_id[3] },
	{ /* sentinel */ }
};
#endif

static int __devinit da9052_i2c_probe(struct i2c_client *client,
				       const struct i2c_device_id *id)
{
	struct da9052 *da9052;
	int ret;

	da9052 = devm_kzalloc(&client->dev, sizeof(struct da9052), GFP_KERNEL);
	if (!da9052)
		return -ENOMEM;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_info(&client->dev, "Error in %s:i2c_check_functionality\n",
			 __func__);
		return  -ENODEV;
	}

	da9052->dev = &client->dev;
	da9052->chip_irq = client->irq;

	i2c_set_clientdata(client, da9052);

	da9052->regmap = devm_regmap_init_i2c(client, &da9052_regmap_config);
	if (IS_ERR(da9052->regmap)) {
		ret = PTR_ERR(da9052->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = da9052_i2c_enable_multiwrite(da9052);
	if (ret < 0)
		return ret;

#ifdef CONFIG_OF
	if (!id) {
		struct device_node *np = client->dev.of_node;
		const struct of_device_id *deviceid;

		deviceid = of_match_node(dialog_dt_ids, np);
		id = (const struct i2c_device_id *)deviceid->data;
	}
#endif

	if (!id) {
		ret = -ENODEV;
		dev_err(&client->dev, "id is null.\n");
		return ret;
	}

	ret = da9052_device_init(da9052, id->driver_data);
	if (ret != 0)
		return ret;

	return 0;
}

static int __devexit da9052_i2c_remove(struct i2c_client *client)
{
	struct da9052 *da9052 = i2c_get_clientdata(client);

	da9052_device_exit(da9052);
	return 0;
}

static struct i2c_driver da9052_i2c_driver = {
	.probe = da9052_i2c_probe,
	.remove = __devexit_p(da9052_i2c_remove),
	.id_table = da9052_i2c_id,
	.driver = {
		.name = "da9052",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = dialog_dt_ids,
#endif
	},
};

static int __init da9052_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&da9052_i2c_driver);
	if (ret != 0) {
		pr_err("DA9052 I2C registration failed %d\n", ret);
		return ret;
	}

	return 0;
}
subsys_initcall(da9052_i2c_init);

static void __exit da9052_i2c_exit(void)
{
	i2c_del_driver(&da9052_i2c_driver);
}
module_exit(da9052_i2c_exit);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("I2C driver for Dialog DA9052 PMIC");
MODULE_LICENSE("GPL");
