/*
 * max8660.c  --  Voltage regulation for the Maxim 8660/8661
 *
 * based on max1586.c and wm8400-regulator.c
 *
 * Copyright (C) 2009 Wolfram Sang, Pengutronix e.K.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
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
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>
#include <linux/regulator/max8660.h>

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
	struct regulator_dev *rdev[];
};

static int max8660_write(struct max8660 *max8660, u8 reg, u8 mask, u8 val)
{
	static const u8 max8660_addresses[MAX8660_N_REGS] =
	  { 0x10, 0x12, 0x20, 0x23, 0x24, 0x29, 0x2a, 0x32, 0x33, 0x39, 0x80 };

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

static int max8660_dcdc_list(struct regulator_dev *rdev, unsigned selector)
{
	if (selector > MAX8660_DCDC_MAX_SEL)
		return -EINVAL;
	return MAX8660_DCDC_MIN_UV + selector * MAX8660_DCDC_STEP;
}

static int max8660_dcdc_get(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 reg = (rdev_get_id(rdev) == MAX8660_V3) ? MAX8660_ADTV2 : MAX8660_SDTV2;
	u8 selector = max8660->shadow_regs[reg];
	return MAX8660_DCDC_MIN_UV + selector * MAX8660_DCDC_STEP;
}

static int max8660_dcdc_set(struct regulator_dev *rdev, int min_uV, int max_uV,
			    unsigned int *s)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 reg, selector, bits;
	int ret;

	if (min_uV < MAX8660_DCDC_MIN_UV || min_uV > MAX8660_DCDC_MAX_UV)
		return -EINVAL;
	if (max_uV < MAX8660_DCDC_MIN_UV || max_uV > MAX8660_DCDC_MAX_UV)
		return -EINVAL;

	selector = DIV_ROUND_UP(min_uV - MAX8660_DCDC_MIN_UV,
				MAX8660_DCDC_STEP);

	ret = max8660_dcdc_list(rdev, selector);
	if (ret < 0 || ret > max_uV)
		return -EINVAL;

	*s = selector;

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
	.list_voltage = max8660_dcdc_list,
	.set_voltage = max8660_dcdc_set,
	.get_voltage = max8660_dcdc_get,
};


/*
 * LDO5 functions
 */

static int max8660_ldo5_list(struct regulator_dev *rdev, unsigned selector)
{
	if (selector > MAX8660_LDO5_MAX_SEL)
		return -EINVAL;
	return MAX8660_LDO5_MIN_UV + selector * MAX8660_LDO5_STEP;
}

static int max8660_ldo5_get(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 selector = max8660->shadow_regs[MAX8660_MDTV2];

	return MAX8660_LDO5_MIN_UV + selector * MAX8660_LDO5_STEP;
}

static int max8660_ldo5_set(struct regulator_dev *rdev, int min_uV, int max_uV,
			    unsigned int *s)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 selector;
	int ret;

	if (min_uV < MAX8660_LDO5_MIN_UV || min_uV > MAX8660_LDO5_MAX_UV)
		return -EINVAL;
	if (max_uV < MAX8660_LDO5_MIN_UV || max_uV > MAX8660_LDO5_MAX_UV)
		return -EINVAL;

	selector = DIV_ROUND_UP(min_uV - MAX8660_LDO5_MIN_UV,
				MAX8660_LDO5_STEP);

	ret = max8660_ldo5_list(rdev, selector);
	if (ret < 0 || ret > max_uV)
		return -EINVAL;

	*s = selector;

	ret = max8660_write(max8660, MAX8660_MDTV2, 0, selector);
	if (ret)
		return ret;

	/* Select target voltage register and activate regulation */
	return max8660_write(max8660, MAX8660_VCC1, 0xff, 0xc0);
}

static struct regulator_ops max8660_ldo5_ops = {
	.list_voltage = max8660_ldo5_list,
	.set_voltage = max8660_ldo5_set,
	.get_voltage = max8660_ldo5_get,
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

static int max8660_ldo67_list(struct regulator_dev *rdev, unsigned selector)
{
	if (selector > MAX8660_LDO67_MAX_SEL)
		return -EINVAL;
	return MAX8660_LDO67_MIN_UV + selector * MAX8660_LDO67_STEP;
}

static int max8660_ldo67_get(struct regulator_dev *rdev)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 shift = (rdev_get_id(rdev) == MAX8660_V6) ? 0 : 4;
	u8 selector = (max8660->shadow_regs[MAX8660_L12VCR] >> shift) & 0xf;

	return MAX8660_LDO67_MIN_UV + selector * MAX8660_LDO67_STEP;
}

static int max8660_ldo67_set(struct regulator_dev *rdev, int min_uV,
			     int max_uV, unsigned int *s)
{
	struct max8660 *max8660 = rdev_get_drvdata(rdev);
	u8 selector;
	int ret;

	if (min_uV < MAX8660_LDO67_MIN_UV || min_uV > MAX8660_LDO67_MAX_UV)
		return -EINVAL;
	if (max_uV < MAX8660_LDO67_MIN_UV || max_uV > MAX8660_LDO67_MAX_UV)
		return -EINVAL;

	selector = DIV_ROUND_UP(min_uV - MAX8660_LDO67_MIN_UV,
				MAX8660_LDO67_STEP);

	ret = max8660_ldo67_list(rdev, selector);
	if (ret < 0 || ret > max_uV)
		return -EINVAL;

	*s = selector;

	if (rdev_get_id(rdev) == MAX8660_V6)
		return max8660_write(max8660, MAX8660_L12VCR, 0xf0, selector);
	else
		return max8660_write(max8660, MAX8660_L12VCR, 0x0f, selector << 4);
}

static struct regulator_ops max8660_ldo67_ops = {
	.is_enabled = max8660_ldo67_is_enabled,
	.enable = max8660_ldo67_enable,
	.disable = max8660_ldo67_disable,
	.list_voltage = max8660_ldo67_list,
	.get_voltage = max8660_ldo67_get,
	.set_voltage = max8660_ldo67_set,
};

static const struct regulator_desc max8660_reg[] = {
	{
		.name = "V3(DCDC)",
		.id = MAX8660_V3,
		.ops = &max8660_dcdc_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX8660_DCDC_MAX_SEL + 1,
		.owner = THIS_MODULE,
	},
	{
		.name = "V4(DCDC)",
		.id = MAX8660_V4,
		.ops = &max8660_dcdc_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX8660_DCDC_MAX_SEL + 1,
		.owner = THIS_MODULE,
	},
	{
		.name = "V5(LDO)",
		.id = MAX8660_V5,
		.ops = &max8660_ldo5_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX8660_LDO5_MAX_SEL + 1,
		.owner = THIS_MODULE,
	},
	{
		.name = "V6(LDO)",
		.id = MAX8660_V6,
		.ops = &max8660_ldo67_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX8660_LDO67_MAX_SEL + 1,
		.owner = THIS_MODULE,
	},
	{
		.name = "V7(LDO)",
		.id = MAX8660_V7,
		.ops = &max8660_ldo67_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = MAX8660_LDO67_MAX_SEL + 1,
		.owner = THIS_MODULE,
	},
};

static int __devinit max8660_probe(struct i2c_client *client,
				   const struct i2c_device_id *i2c_id)
{
	struct regulator_dev **rdev;
	struct max8660_platform_data *pdata = client->dev.platform_data;
	struct regulator_config config = { };
	struct max8660 *max8660;
	int boot_on, i, id, ret = -EINVAL;

	if (pdata->num_subdevs > MAX8660_V_END) {
		dev_err(&client->dev, "Too many regulators found!\n");
		return -EINVAL;
	}

	max8660 = kzalloc(sizeof(struct max8660) +
			sizeof(struct regulator_dev *) * MAX8660_V_END,
			GFP_KERNEL);
	if (!max8660)
		return -ENOMEM;

	max8660->client = client;
	rdev = max8660->rdev;

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
			goto err_out;

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
			if (!strcmp(i2c_id->name, "max8661")) {
				dev_err(&client->dev, "Regulator not on this chip!\n");
				goto err_out;
			}

			if (boot_on)
				max8660->shadow_regs[MAX8660_OVER2] |= 4;
			break;

		default:
			dev_err(&client->dev, "invalid regulator %s\n",
				 pdata->subdevs[i].name);
			goto err_out;
		}
	}

	/* Finally register devices */
	for (i = 0; i < pdata->num_subdevs; i++) {

		id = pdata->subdevs[i].id;

		config.dev = &client->dev;
		config.init_data = pdata->subdevs[i].platform_data;
		config.driver_data = max8660;

		rdev[i] = regulator_register(&max8660_reg[id], &config);
		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(&client->dev, "failed to register %s\n",
				max8660_reg[id].name);
			goto err_unregister;
		}
	}

	i2c_set_clientdata(client, max8660);
	return 0;

err_unregister:
	while (--i >= 0)
		regulator_unregister(rdev[i]);
err_out:
	return ret;
}

static int __devexit max8660_remove(struct i2c_client *client)
{
	struct max8660 *max8660 = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < MAX8660_V_END; i++)
		if (max8660->rdev[i])
			regulator_unregister(max8660->rdev[i]);
	return 0;
}

static const struct i2c_device_id max8660_id[] = {
	{ "max8660", 0 },
	{ "max8661", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max8660_id);

static struct i2c_driver max8660_driver = {
	.probe = max8660_probe,
	.remove = __devexit_p(max8660_remove),
	.driver		= {
		.name	= "max8660",
		.owner	= THIS_MODULE,
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
