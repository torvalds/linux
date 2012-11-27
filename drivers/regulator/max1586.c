/*
 * max1586.c  --  Voltage and current regulation for the Maxim 1586
 *
 * Copyright (C) 2008 Robert Jarzmik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>
#include <linux/regulator/max1586.h>

#define MAX1586_V3_MAX_VSEL 31
#define MAX1586_V6_MAX_VSEL 3

#define MAX1586_V3_MIN_UV   700000
#define MAX1586_V3_MAX_UV  1475000

#define MAX1586_V6_MIN_UV        0
#define MAX1586_V6_MAX_UV  3000000

#define I2C_V3_SELECT (0 << 5)
#define I2C_V6_SELECT (1 << 5)

struct max1586_data {
	struct i2c_client *client;

	/* min/max V3 voltage */
	unsigned int min_uV;
	unsigned int max_uV;

	struct regulator_dev *rdev[0];
};

/*
 * V6 voltage
 * On I2C bus, sending a "x" byte to the max1586 means :
 *   set V6 to either 0V, 1.8V, 2.5V, 3V depending on (x & 0x3)
 * As regulator framework doesn't accept voltages to be 0V, we use 1uV.
 */
static int v6_voltages_uv[] = { 1, 1800000, 2500000, 3000000 };

/*
 * V3 voltage
 * On I2C bus, sending a "x" byte to the max1586 means :
 *   set V3 to 0.700V + (x & 0x1f) * 0.025V
 * This voltage can be increased by external resistors
 * R24 and R25=100kOhm as described in the data sheet.
 * The gain is approximately: 1 + R24/R25 + R24/185.5kOhm
 */
static int max1586_v3_set_voltage_sel(struct regulator_dev *rdev,
				      unsigned selector)
{
	struct max1586_data *max1586 = rdev_get_drvdata(rdev);
	struct i2c_client *client = max1586->client;
	u8 v3_prog;

	dev_dbg(&client->dev, "changing voltage v3 to %dmv\n",
		regulator_list_voltage_linear(rdev, selector) / 1000);

	v3_prog = I2C_V3_SELECT | (u8) selector;
	return i2c_smbus_write_byte(client, v3_prog);
}

static int max1586_v6_set_voltage_sel(struct regulator_dev *rdev,
				      unsigned int selector)
{
	struct i2c_client *client = rdev_get_drvdata(rdev);
	u8 v6_prog;

	dev_dbg(&client->dev, "changing voltage v6 to %dmv\n",
		rdev->desc->volt_table[selector] / 1000);

	v6_prog = I2C_V6_SELECT | (u8) selector;
	return i2c_smbus_write_byte(client, v6_prog);
}

/*
 * The Maxim 1586 controls V3 and V6 voltages, but offers no way of reading back
 * the set up value.
 */
static struct regulator_ops max1586_v3_ops = {
	.set_voltage_sel = max1586_v3_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
};

static struct regulator_ops max1586_v6_ops = {
	.set_voltage_sel = max1586_v6_set_voltage_sel,
	.list_voltage = regulator_list_voltage_table,
};

static struct regulator_desc max1586_reg[] = {
	{
		.name = "Output_V3",
		.id = MAX1586_V3,
		.ops = &max1586_v3_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX1586_V3_MAX_VSEL + 1,
		.owner = THIS_MODULE,
	},
	{
		.name = "Output_V6",
		.id = MAX1586_V6,
		.ops = &max1586_v6_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX1586_V6_MAX_VSEL + 1,
		.volt_table = v6_voltages_uv,
		.owner = THIS_MODULE,
	},
};

static int max1586_pmic_probe(struct i2c_client *client,
					const struct i2c_device_id *i2c_id)
{
	struct regulator_dev **rdev;
	struct max1586_platform_data *pdata = client->dev.platform_data;
	struct regulator_config config = { };
	struct max1586_data *max1586;
	int i, id, ret = -ENOMEM;

	max1586 = devm_kzalloc(&client->dev, sizeof(struct max1586_data) +
			sizeof(struct regulator_dev *) * (MAX1586_V6 + 1),
			GFP_KERNEL);
	if (!max1586)
		return -ENOMEM;

	max1586->client = client;

	if (!pdata->v3_gain)
		return -EINVAL;

	max1586->min_uV = MAX1586_V3_MIN_UV / 1000 * pdata->v3_gain / 1000;
	max1586->max_uV = MAX1586_V3_MAX_UV / 1000 * pdata->v3_gain / 1000;

	rdev = max1586->rdev;
	for (i = 0; i < pdata->num_subdevs && i <= MAX1586_V6; i++) {
		id = pdata->subdevs[i].id;
		if (!pdata->subdevs[i].platform_data)
			continue;
		if (id < MAX1586_V3 || id > MAX1586_V6) {
			dev_err(&client->dev, "invalid regulator id %d\n", id);
			goto err;
		}

		if (id == MAX1586_V3) {
			max1586_reg[id].min_uV = max1586->min_uV;
			max1586_reg[id].uV_step =
					(max1586->max_uV - max1586->min_uV) /
					MAX1586_V3_MAX_VSEL;
		}

		config.dev = &client->dev;
		config.init_data = pdata->subdevs[i].platform_data;
		config.driver_data = max1586;

		rdev[i] = regulator_register(&max1586_reg[id], &config);
		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(&client->dev, "failed to register %s\n",
				max1586_reg[id].name);
			goto err;
		}
	}

	i2c_set_clientdata(client, max1586);
	dev_info(&client->dev, "Maxim 1586 regulator driver loaded\n");
	return 0;

err:
	while (--i >= 0)
		regulator_unregister(rdev[i]);
	return ret;
}

static int max1586_pmic_remove(struct i2c_client *client)
{
	struct max1586_data *max1586 = i2c_get_clientdata(client);
	int i;

	for (i = 0; i <= MAX1586_V6; i++)
		if (max1586->rdev[i])
			regulator_unregister(max1586->rdev[i]);
	return 0;
}

static const struct i2c_device_id max1586_id[] = {
	{ "max1586", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max1586_id);

static struct i2c_driver max1586_pmic_driver = {
	.probe = max1586_pmic_probe,
	.remove = max1586_pmic_remove,
	.driver		= {
		.name	= "max1586",
		.owner	= THIS_MODULE,
	},
	.id_table	= max1586_id,
};

static int __init max1586_pmic_init(void)
{
	return i2c_add_driver(&max1586_pmic_driver);
}
subsys_initcall(max1586_pmic_init);

static void __exit max1586_pmic_exit(void)
{
	i2c_del_driver(&max1586_pmic_driver);
}
module_exit(max1586_pmic_exit);

/* Module information */
MODULE_DESCRIPTION("MAXIM 1586 voltage regulator driver");
MODULE_AUTHOR("Robert Jarzmik");
MODULE_LICENSE("GPL");
