// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * max1586.c  --  Voltage and current regulation for the Maxim 1586
 *
 * Copyright (C) 2008 Robert Jarzmik
 */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>
#include <linux/regulator/max1586.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>

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

	unsigned int v3_curr_sel;
	unsigned int v6_curr_sel;
};

/*
 * V6 voltage
 * On I2C bus, sending a "x" byte to the max1586 means :
 *   set V6 to either 0V, 1.8V, 2.5V, 3V depending on (x & 0x3)
 * As regulator framework doesn't accept voltages to be 0V, we use 1uV.
 */
static const unsigned int v6_voltages_uv[] = { 1, 1800000, 2500000, 3000000 };

/*
 * V3 voltage
 * On I2C bus, sending a "x" byte to the max1586 means :
 *   set V3 to 0.700V + (x & 0x1f) * 0.025V
 * This voltage can be increased by external resistors
 * R24 and R25=100kOhm as described in the data sheet.
 * The gain is approximately: 1 + R24/R25 + R24/185.5kOhm
 */
static int max1586_v3_get_voltage_sel(struct regulator_dev *rdev)
{
	struct max1586_data *max1586 = rdev_get_drvdata(rdev);

	return max1586->v3_curr_sel;
}

static int max1586_v3_set_voltage_sel(struct regulator_dev *rdev,
				      unsigned selector)
{
	struct max1586_data *max1586 = rdev_get_drvdata(rdev);
	struct i2c_client *client = max1586->client;
	int ret;
	u8 v3_prog;

	dev_dbg(&client->dev, "changing voltage v3 to %dmv\n",
		regulator_list_voltage_linear(rdev, selector) / 1000);

	v3_prog = I2C_V3_SELECT | (u8) selector;
	ret = i2c_smbus_write_byte(client, v3_prog);
	if (ret)
		return ret;

	max1586->v3_curr_sel = selector;

	return 0;
}

static int max1586_v6_get_voltage_sel(struct regulator_dev *rdev)
{
	struct max1586_data *max1586 = rdev_get_drvdata(rdev);

	return max1586->v6_curr_sel;
}

static int max1586_v6_set_voltage_sel(struct regulator_dev *rdev,
				      unsigned int selector)
{
	struct max1586_data *max1586 = rdev_get_drvdata(rdev);
	struct i2c_client *client = max1586->client;
	u8 v6_prog;
	int ret;

	dev_dbg(&client->dev, "changing voltage v6 to %dmv\n",
		rdev->desc->volt_table[selector] / 1000);

	v6_prog = I2C_V6_SELECT | (u8) selector;
	ret = i2c_smbus_write_byte(client, v6_prog);
	if (ret)
		return ret;

	max1586->v6_curr_sel = selector;

	return 0;
}

/*
 * The Maxim 1586 controls V3 and V6 voltages, but offers no way of reading back
 * the set up value.
 */
static const struct regulator_ops max1586_v3_ops = {
	.get_voltage_sel = max1586_v3_get_voltage_sel,
	.set_voltage_sel = max1586_v3_set_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
};

static const struct regulator_ops max1586_v6_ops = {
	.get_voltage_sel = max1586_v6_get_voltage_sel,
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

static int of_get_max1586_platform_data(struct device *dev,
				 struct max1586_platform_data *pdata)
{
	struct max1586_subdev_data *sub;
	struct of_regulator_match rmatch[ARRAY_SIZE(max1586_reg)] = { };
	struct device_node *np = dev->of_node;
	int i, matched;

	if (of_property_read_u32(np, "v3-gain",
				 &pdata->v3_gain) < 0) {
		dev_err(dev, "%pOF has no 'v3-gain' property\n", np);
		return -EINVAL;
	}

	np = of_get_child_by_name(np, "regulators");
	if (!np) {
		dev_err(dev, "missing 'regulators' subnode in DT\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(rmatch); i++)
		rmatch[i].name = max1586_reg[i].name;

	matched = of_regulator_match(dev, np, rmatch, ARRAY_SIZE(rmatch));
	of_node_put(np);
	/*
	 * If matched is 0, ie. neither Output_V3 nor Output_V6 have been found,
	 * return 0, which signals the normal situation where no subregulator is
	 * available. This is normal because the max1586 doesn't provide any
	 * readback support, so the subregulators can't report any status
	 * anyway.  If matched < 0, return the error.
	 */
	if (matched <= 0)
		return matched;

	pdata->subdevs = devm_kcalloc(dev,
				      matched,
				      sizeof(struct max1586_subdev_data),
				      GFP_KERNEL);
	if (!pdata->subdevs)
		return -ENOMEM;

	pdata->num_subdevs = matched;
	sub = pdata->subdevs;

	for (i = 0; i < matched; i++) {
		sub->id = i;
		sub->name = rmatch[i].of_node->name;
		sub->platform_data = rmatch[i].init_data;
		sub++;
	}

	return 0;
}

static const struct of_device_id __maybe_unused max1586_of_match[] = {
	{ .compatible = "maxim,max1586", },
	{},
};
MODULE_DEVICE_TABLE(of, max1586_of_match);

static int max1586_pmic_probe(struct i2c_client *client)
{
	struct max1586_platform_data *pdata, pdata_of;
	struct regulator_config config = { };
	struct max1586_data *max1586;
	int i, id, ret;

	pdata = dev_get_platdata(&client->dev);
	if (client->dev.of_node && !pdata) {
		ret = of_get_max1586_platform_data(&client->dev, &pdata_of);
		if (ret < 0)
			return ret;
		pdata = &pdata_of;
	}

	max1586 = devm_kzalloc(&client->dev, sizeof(struct max1586_data),
			GFP_KERNEL);
	if (!max1586)
		return -ENOMEM;

	max1586->client = client;

	if (!pdata->v3_gain)
		return -EINVAL;

	max1586->min_uV = MAX1586_V3_MIN_UV / 1000 * pdata->v3_gain / 1000;
	max1586->max_uV = MAX1586_V3_MAX_UV / 1000 * pdata->v3_gain / 1000;

	/* Set curr_sel to default voltage on power-up */
	max1586->v3_curr_sel = 24; /* 1.3V */
	max1586->v6_curr_sel = 0;

	for (i = 0; i < pdata->num_subdevs && i <= MAX1586_V6; i++) {
		struct regulator_dev *rdev;

		id = pdata->subdevs[i].id;
		if (!pdata->subdevs[i].platform_data)
			continue;
		if (id < MAX1586_V3 || id > MAX1586_V6) {
			dev_err(&client->dev, "invalid regulator id %d\n", id);
			return -EINVAL;
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

		rdev = devm_regulator_register(&client->dev,
						  &max1586_reg[id], &config);
		if (IS_ERR(rdev)) {
			dev_err(&client->dev, "failed to register %s\n",
				max1586_reg[id].name);
			return PTR_ERR(rdev);
		}
	}

	i2c_set_clientdata(client, max1586);
	dev_info(&client->dev, "Maxim 1586 regulator driver loaded\n");
	return 0;
}

static const struct i2c_device_id max1586_id[] = {
	{ "max1586", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max1586_id);

static struct i2c_driver max1586_pmic_driver = {
	.probe = max1586_pmic_probe,
	.driver		= {
		.name	= "max1586",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(max1586_of_match),
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
