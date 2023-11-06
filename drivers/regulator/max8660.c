// SPDX-License-Identifier: GPL-2.0-only
/*
 * max8660.c  --  Voltage regulation for the Maxim 8660/8661
 *
 * based on max1586.c and wm8400-regulator.c
 *
 * Copyright (C) 2009 Wolfram Sang, Pengutronix e.K.
 *
 * Some info:
 *
 * Datasheet: http://datasheets.maxim-ic.com/en/ds/MAX8660-MAX8661.pdf
 *
 * This chip is a bit nasty because it is a write-only device. Thus, the driver
 * uses shadow registers to keep track of its values. The main problem appears
 * to be the initialization: When Linux boots up, we cannot know if the chip is
 * in the default state or not, so we would have to pass such information in
 * platform_data. As this adds a bit of complexity to the driver, this is left
 * out for now until it is really needed.
 *
 * [A|S|M]DTV1 registers are currently not used, but [A|S|M]DTV2.
 *
 * If the driver is feature complete, it might be worth to check if one set of
 * functions for V3-V7 is sufficient. For maximum flexibility during
 * development, they are separated for now.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>
#include <linux/regulator/max8660.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>

#define MAX8660_DCDC_MIN_UV	 725000
#define MAX8660_DCDC_MAX_UV	1800000
#define MAX8660_DCDC_STEP	  25000
#define MAX8660_DCDC_MAX_SEL	0x2b

#define MAX8660_LDO5_MIN_UV	1700000
#define MAX8660_LDO5_MAX_UV	2000000
#define MAX8660_LDO5_STEP	  25000
#define MAX8660_LDO5_MAX_SEL	0x0c

#define MAX8660_LDO67_MIN_UV	1800000
#define MAX8660_LDO67_MAX_UV	3300000
#define MAX8660_LDO67_STEP	 100000
#define MAX8660_LDO67_MAX_SEL	0x0f

enum {
	MAX8660_OVER1,
	MAX8660_OVER2,
	MAX8660_VCC1,
	MAX8660_ADTV1,
	MAX8660_ADTV2,
	MAX8660_SDTV1,
	MAX8660_SDTV2,
	MAX8660_MDTV1,
	MAX8660_MDTV2,
	MAX8660_L12VCR,
	MAX8660_FPWM,
	MAX8660_N_REGS,	/* not a real register */
};

struct max8660 {
	struct i2c_client *client;
	u8 shadow_regs[MAX8660_N_REGS];		/* as chip is write only */
};

static int max8660_write(struct max8660 *max8660, u8 reg, u8 mask, u8 val)
{
	static const u8 max8660_addresses[MAX8660_N_REGS] = {
	 0x10, 0x12, 0x20, 0x23, 0x24, 0x29, 0x2a, 0x32, 0x33, 0x39, 0x80
	};

	int ret;
	u8 reg_val = (max8660->shadow_regs[reg] & mask) | val;

	dev_vdbg(&max8660->client->dev, "Writing reg %02x with %02x\n",
			max8660_addresses[reg], reg_val);

	ret = i2c_smbus_write_byte_data(max8660->client,
			max8660_addresses[reg], reg_val);
	if (ret == 0)
		max8660->shadow_regs[reg] = reg_val;

	return ret;
}


/*
 * DCDC functions
 */

static int max8660_dcdc_is_enabled(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 val = max8660->shadow_regs[MAX8660_OVER1];
	u8 mask = (rdev_get_id(rdev) == MAX8660_V3) ? 1 : 4;

	return !!(val & mask);
}

static int max8660_dcdc_enable(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 bit = (rdev_get_id(rdev) == MAX8660_V3) ? 1 : 4;

	return max8660_write(max8660, MAX8660_OVER1, 0xff, bit);
}

static int max8660_dcdc_disable(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 mask = (rdev_get_id(rdev) == MAX8660_V3) ? ~1 : ~4;

	return max8660_write(max8660, MAX8660_OVER1, mask, 0);
}

static int max8660_dcdc_get_voltage_sel(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 reg = (rdev_get_id(rdev) == MAX8660_V3) ? MAX8660_ADTV2 : MAX8660_SDTV2;
	u8 selector = max8660->shadow_regs[reg];

	return selector;
}

static int max8660_dcdc_set_voltage_sel(struct regulator_dev *rdev,
					unsigned int selector)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 reg, bits;
	int ret;

	reg = (rdev_get_id(rdev) == MAX8660_V3) ? MAX8660_ADTV2 : MAX8660_SDTV2;
	ret = max8660_write(max8660, reg, 0, selector);
	if (ret)
		return ret;

	/* Select target voltage register and activate regulation */
	bits = (rdev_get_id(rdev) == MAX8660_V3) ? 0x03 : 0x30;
	return max8660_write(max8660, MAX8660_VCC1, 0xff, bits);
}

static struct regulator_ops max8660_dcdc_ops = {
	.is_enabled = max8660_dcdc_is_enabled,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_voltage_sel = max8660_dcdc_set_voltage_sel,
	.get_voltage_sel = max8660_dcdc_get_voltage_sel,
};


/*
 * LDO5 functions
 */

static int max8660_ldo5_get_voltage_sel(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);

	u8 selector = max8660->shadow_regs[MAX8660_MDTV2];
	return selector;
}

static int max8660_ldo5_set_voltage_sel(struct regulator_dev *rdev,
					unsigned int selector)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	int ret;

	ret = max8660_write(max8660, MAX8660_MDTV2, 0, selector);
	if (ret)
		return ret;

	/* Select target voltage register and activate regulation */
	return max8660_write(max8660, MAX8660_VCC1, 0xff, 0xc0);
}

static const struct regulator_ops max8660_ldo5_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_voltage_sel = max8660_ldo5_set_voltage_sel,
	.get_voltage_sel = max8660_ldo5_get_voltage_sel,
};


/*
 * LDO67 functions
 */

static int max8660_ldo67_is_enabled(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 val = max8660->shadow_regs[MAX8660_OVER2];
	u8 mask = (rdev_get_id(rdev) == MAX8660_V6) ? 2 : 4;

	return !!(val & mask);
}

static int max8660_ldo67_enable(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 bit = (rdev_get_id(rdev) == MAX8660_V6) ? 2 : 4;

	return max8660_write(max8660, MAX8660_OVER2, 0xff, bit);
}

static int max8660_ldo67_disable(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 mask = (rdev_get_id(rdev) == MAX8660_V6) ? ~2 : ~4;

	return max8660_write(max8660, MAX8660_OVER2, mask, 0);
}

static int max8660_ldo67_get_voltage_sel(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 shift = (rdev_get_id(rdev) == MAX8660_V6) ? 0 : 4;
	u8 selector = (max8660->shadow_regs[MAX8660_L12VCR] >> shift) & 0xf;

	return selector;
}

static int max8660_ldo67_set_voltage_sel(struct regulator_dev *rdev,
					 unsigned int selector)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);

	if (rdev_get_id(rdev) == MAX8660_V6)
		return max8660_write(max8660, MAX8660_L12VCR, 0xf0, selector);
	else
		return max8660_write(max8660, MAX8660_L12VCR, 0x0f,
				     selector << 4);
}

static const struct regulator_ops max8660_ldo67_ops = {
	.is_enabled = max8660_ldo67_is_enabled,
	.enable = max8660_ldo67_enable,
	.disable = max8660_ldo67_disable,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = max8660_ldo67_get_voltage_sel,
	.set_voltage_sel = max8660_ldo67_set_voltage_sel,
};

static const struct regulator_desc max8660_reg[] = {
	{
		.name = "V3(DCDC)",
		.id = MAX8660_V3,
		.ops = &max8660_dcdc_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX8660_DCDC_MAX_SEL + 1,
		.owner = THIS_MODULE,
		.min_uV = MAX8660_DCDC_MIN_UV,
		.uV_step = MAX8660_DCDC_STEP,
	},
	{
		.name = "V4(DCDC)",
		.id = MAX8660_V4,
		.ops = &max8660_dcdc_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX8660_DCDC_MAX_SEL + 1,
		.owner = THIS_MODULE,
		.min_uV = MAX8660_DCDC_MIN_UV,
		.uV_step = MAX8660_DCDC_STEP,
	},
	{
		.name = "V5(LDO)",
		.id = MAX8660_V5,
		.ops = &max8660_ldo5_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX8660_LDO5_MAX_SEL + 1,
		.owner = THIS_MODULE,
		.min_uV = MAX8660_LDO5_MIN_UV,
		.uV_step = MAX8660_LDO5_STEP,
	},
	{
		.name = "V6(LDO)",
		.id = MAX8660_V6,
		.ops = &max8660_ldo67_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX8660_LDO67_MAX_SEL + 1,
		.owner = THIS_MODULE,
		.min_uV = MAX8660_LDO67_MIN_UV,
		.uV_step = MAX8660_LDO67_STEP,
	},
	{
		.name = "V7(LDO)",
		.id = MAX8660_V7,
		.ops = &max8660_ldo67_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX8660_LDO67_MAX_SEL + 1,
		.owner = THIS_MODULE,
		.min_uV = MAX8660_LDO67_MIN_UV,
		.uV_step = MAX8660_LDO67_STEP,
	},
};

enum {
	MAX8660 = 0,
	MAX8661 = 1,
};

#ifdef CONFIG_OF
static const struct of_device_id max8660_dt_ids[] = {
	{ .compatible = "maxim,max8660", .data = (void *) MAX8660 },
	{ .compatible = "maxim,max8661", .data = (void *) MAX8661 },
	{ }
};
MODULE_DEVICE_TABLE(of, max8660_dt_ids);

static int max8660_pdata_from_dt(struct device *dev,
				 struct device_node **of_node,
				 struct max8660_platform_data *pdata)
{
	int matched, i;
	struct device_node *np;
	struct max8660_subdev_data *sub;
	struct of_regulator_match rmatch[ARRAY_SIZE(max8660_reg)] = { };

	np = of_get_child_by_name(dev->of_node, "regulators");
	if (!np) {
		dev_err(dev, "missing 'regulators' subnode in DT\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(rmatch); i++)
		rmatch[i].name = max8660_reg[i].name;

	matched = of_regulator_match(dev, np, rmatch, ARRAY_SIZE(rmatch));
	of_node_put(np);
	if (matched <= 0)
		return matched;

	pdata->subdevs = devm_kcalloc(dev,
				      matched,
				      sizeof(struct max8660_subdev_data),
				      GFP_KERNEL);
	if (!pdata->subdevs)
		return -ENOMEM;

	pdata->num_subdevs = matched;
	sub = pdata->subdevs;

	for (i = 0; i < matched; i++) {
		sub->id = i;
		sub->name = rmatch[i].name;
		sub->platform_data = rmatch[i].init_data;
		of_node[i] = rmatch[i].of_node;
		sub++;
	}

	return 0;
}
#else
static inline int max8660_pdata_from_dt(struct device *dev,
					struct device_node **of_node,
					struct max8660_platform_data *pdata)
{
	return 0;
}
#endif

static int max8660_probe(struct i2c_client *client)
{
	const struct i2c_device_id *i2c_id = i2c_client_get_device_id(client);
	struct device *dev = &client->dev;
	struct max8660_platform_data pdata_of, *pdata = dev_get_platdata(dev);
	struct regulator_config config = { };
	struct max8660 *max8660;
	int boot_on, i, id, ret = -EINVAL;
	struct device_node *of_node[MAX8660_V_END];
	unsigned long type;

	if (dev->of_node && !pdata) {
		const struct of_device_id *id;

		id = of_match_device(of_match_ptr(max8660_dt_ids), dev);
		if (!id)
			return -ENODEV;

		ret = max8660_pdata_from_dt(dev, of_node, &pdata_of);
		if (ret < 0)
			return ret;

		pdata = &pdata_of;
		type = (unsigned long) id->data;
	} else {
		type = i2c_id->driver_data;
		memset(of_node, 0, sizeof(of_node));
	}

	if (pdata->num_subdevs > MAX8660_V_END) {
		dev_err(dev, "Too many regulators found!\n");
		return -EINVAL;
	}

	max8660 = devm_kzalloc(dev, sizeof(struct max8660), GFP_KERNEL);
	if (!max8660)
		return -ENOMEM;

	max8660->client = client;

	if (pdata->en34_is_high) {
		/* Simulate always on */
		max8660->shadow_regs[MAX8660_OVER1] = 5;
	} else {
		/* Otherwise devices can be toggled via software */
		max8660_dcdc_ops.enable = max8660_dcdc_enable;
		max8660_dcdc_ops.disable = max8660_dcdc_disable;
	}

	/*
	 * First, set up shadow registers to prevent glitches. As some
	 * registers are shared between regulators, everything must be properly
	 * set up for all regulators in advance.
	 */
	max8660->shadow_regs[MAX8660_ADTV1] =
		max8660->shadow_regs[MAX8660_ADTV2] =
		max8660->shadow_regs[MAX8660_SDTV1] =
		max8660->shadow_regs[MAX8660_SDTV2] = 0x1b;
	max8660->shadow_regs[MAX8660_MDTV1] =
		max8660->shadow_regs[MAX8660_MDTV2] = 0x04;

	for (i = 0; i < pdata->num_subdevs; i++) {

		if (!pdata->subdevs[i].platform_data)
			boot_on = false;
		else
			boot_on = pdata->subdevs[i].platform_data->constraints.boot_on;

		switch (pdata->subdevs[i].id) {
		case MAX8660_V3:
			if (boot_on)
				max8660->shadow_regs[MAX8660_OVER1] |= 1;
			break;

		case MAX8660_V4:
			if (boot_on)
				max8660->shadow_regs[MAX8660_OVER1] |= 4;
			break;

		case MAX8660_V5:
			break;

		case MAX8660_V6:
			if (boot_on)
				max8660->shadow_regs[MAX8660_OVER2] |= 2;
			break;

		case MAX8660_V7:
			if (type == MAX8661) {
				dev_err(dev, "Regulator not on this chip!\n");
				return -EINVAL;
			}

			if (boot_on)
				max8660->shadow_regs[MAX8660_OVER2] |= 4;
			break;

		default:
			dev_err(dev, "invalid regulator %s\n",
				 pdata->subdevs[i].name);
			return ret;
		}
	}

	/* Finally register devices */
	for (i = 0; i < pdata->num_subdevs; i++) {
		struct regulator_dev *rdev;

		id = pdata->subdevs[i].id;

		config.dev = dev;
		config.init_data = pdata->subdevs[i].platform_data;
		config.of_node = of_node[i];
		config.driver_data = max8660;

		rdev = devm_regulator_register(&client->dev,
						  &max8660_reg[id], &config);
		if (IS_ERR(rdev)) {
			dev_err(&client->dev, "failed to register %s\n",
				max8660_reg[id].name);
			return PTR_ERR(rdev);
		}
	}

	i2c_set_clientdata(client, max8660);
	return 0;
}

static const struct i2c_device_id max8660_id[] = {
	{ .name = "max8660", .driver_data = MAX8660 },
	{ .name = "max8661", .driver_data = MAX8661 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max8660_id);

static struct i2c_driver max8660_driver = {
	.probe = max8660_probe,
	.driver		= {
		.name	= "max8660",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table	= max8660_id,
};

static int __init max8660_init(void)
{
	return i2c_add_driver(&max8660_driver);
}
subsys_initcall(max8660_init);

static void __exit max8660_exit(void)
{
	i2c_del_driver(&max8660_driver);
}
module_exit(max8660_exit);

/* Module information */
MODULE_DESCRIPTION("MAXIM 8660/8661 voltage regulator driver");
MODULE_AUTHOR("Wolfram Sang");
MODULE_LICENSE("GPL v2");
