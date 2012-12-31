/*
 * max8698.c - Voltage regulator driver for the Maxim 8698
 *
 *  Copyright (C) 2009-2010 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *  Marek Szyprowski <m.szyprowski@samsung.com>
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
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/max8698.h>
#include <linux/mfd/max8698-private.h>

struct max8698_data {
	struct device		*dev;
	struct max8698_dev	*iodev;
	int			num_regulators;
	struct regulator_dev	**rdev;
};

struct voltage_map_desc {
	int min;
	int max;
	int step;
};

#define RAMP_DELAY_VAL	10

/* Voltage maps */
static const struct voltage_map_desc ldo23_voltage_map_desc = {
	.min	= 800,
	.step	= 50,
	.max	= 1300,
};
static const struct voltage_map_desc ldo45679_voltage_map_desc = {
	.min	= 1600,
	.step	= 100,
	.max	= 3600,
};
static const struct voltage_map_desc ldo8_voltage_map_desc = {
	.min	= 3000,
	.step	= 100,
	.max	= 3600,
};
static const struct voltage_map_desc buck12_voltage_map_desc = {
	.min	= 750,
	.step	= 50,
	.max	= 1500,
};

static const struct voltage_map_desc *ldo_voltage_map[] = {
	NULL,
	&ldo23_voltage_map_desc,	/* LDO1 */
	&ldo23_voltage_map_desc,	/* LDO2 */
	&ldo23_voltage_map_desc,	/* LDO3 */
	&ldo45679_voltage_map_desc,	/* LDO4 */
	&ldo45679_voltage_map_desc,	/* LDO5 */
	&ldo45679_voltage_map_desc,	/* LDO6 */
	&ldo45679_voltage_map_desc,	/* LDO7 */
	&ldo8_voltage_map_desc,		/* LDO8 */
	&ldo45679_voltage_map_desc,	/* LDO9 */
	&buck12_voltage_map_desc,	/* BUCK1 */
	&buck12_voltage_map_desc,	/* BUCK2 */
	&ldo45679_voltage_map_desc,	/* BUCK3 */
};

static inline int max8698_get_ldo(struct regulator_dev *rdev)
{
	return rdev_get_id(rdev);
}

static int max8698_list_voltage(struct regulator_dev *rdev,
		unsigned int selector)
{
	const struct voltage_map_desc *desc;
	int ldo = max8698_get_ldo(rdev);
	int val;

	if (ldo >= ARRAY_SIZE(ldo_voltage_map))
		return -EINVAL;

	desc = ldo_voltage_map[ldo];
	if (desc == NULL)
		return -EINVAL;

	val = desc->min + desc->step * selector;
	if (val > desc->max)
		return -EINVAL;

	return val * 1000;
}

static int max8698_get_enable_register(struct regulator_dev *rdev,
		int *reg, int *shift)
{
	int ldo = max8698_get_ldo(rdev);

	switch (ldo) {
	case MAX8698_LDO2 ... MAX8698_LDO5:
		*reg = MAX8698_REG_ONOFF1;
		*shift = 4 - (ldo - MAX8698_LDO2);
		break;
	case MAX8698_LDO6 ... MAX8698_LDO9:
		*reg = MAX8698_REG_ONOFF2;
		*shift = 7 - (ldo - MAX8698_LDO6);
		break;
	case MAX8698_BUCK1 ... MAX8698_BUCK3:
		*reg = MAX8698_REG_ONOFF1;
		*shift = 7 - (ldo - MAX8698_BUCK1);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int max8698_ldo_is_enabled(struct regulator_dev *rdev)
{
	struct max8698_data *max8698 = rdev_get_drvdata(rdev);
	int ret, reg, shift = 8;
	u8 val;

	ret = max8698_get_enable_register(rdev, &reg, &shift);
	if (ret)
		return ret;

	ret = max8698_read_reg(max8698->iodev, reg, &val);
	if (ret)
		return ret;

	return val & (1 << shift);
}

static int max8698_ldo_enable(struct regulator_dev *rdev)
{
	struct max8698_data *max8698 = rdev_get_drvdata(rdev);
	int reg, shift = 8, ret;

	ret = max8698_get_enable_register(rdev, &reg, &shift);
	if (ret)
		return ret;

	return max8698_update_reg(max8698->iodev, reg, 1<<shift, 1<<shift);
}

static int max8698_ldo_disable(struct regulator_dev *rdev)
{
	struct max8698_data *max8698 = rdev_get_drvdata(rdev);
	int reg, shift = 8, ret;

	ret = max8698_get_enable_register(rdev, &reg, &shift);
	if (ret)
		return ret;

	return max8698_update_reg(max8698->iodev, reg, 0, 1<<shift);
}

static int max8698_get_voltage_register(struct regulator_dev *rdev,
		int *_reg, int *_shift, int *_mask)
{
	struct max8698_data *max8698 = rdev_get_drvdata(rdev);
	struct max8698_platform_data *pdata = dev_get_platdata(max8698->iodev->dev);
	int ldo = max8698_get_ldo(rdev);
	int reg, shift = 0, mask = 0xff;

	switch (ldo) {
	case MAX8698_LDO2 ... MAX8698_LDO3:
		reg = MAX8698_REG_LDO2_LDO3;
		mask = 0xf;
		if (ldo == MAX8698_LDO2)
			shift = 4;
		else
			shift = 0;
		break;
	case MAX8698_LDO4 ... MAX8698_LDO7:
		reg = MAX8698_REG_LDO4 + (ldo - MAX8698_LDO4);
		break;
	case MAX8698_LDO8:
		reg = MAX8698_REG_LDO8_BKCHAR;
		mask = 0xf;
		shift = 4;
		break;
	case MAX8698_LDO9:
		reg = MAX8698_REG_LDO9;
		break;
	case MAX8698_BUCK1:
		mask = 0xf;
		if (gpio_get_value(pdata->set2) == 0)
			reg = MAX8698_REG_DVSARM12;
		else
			reg = MAX8698_REG_DVSARM34;

		if (gpio_get_value(pdata->set1) == 0)
			shift = 0;
		else
			shift = 4;
		break;
	case MAX8698_BUCK2:
		if (gpio_get_value(pdata->set3) == 0)
			shift = 0;
		else
			shift = 4;
		reg = MAX8698_REG_DVSINT12;
		mask = 0xf;
		break;
	case MAX8698_BUCK3:
		reg = MAX8698_REG_BUCK3;
		break;
	default:
		return -EINVAL;
	}

	*_reg = reg;
	*_shift = shift;
	*_mask = mask;

	return 0;
}

static int max8698_get_voltage(struct regulator_dev *rdev)
{
	struct max8698_data *max8698 = rdev_get_drvdata(rdev);
	int reg, shift = 0, mask, ret;
	u8 val;

	ret = max8698_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	ret = max8698_read_reg(max8698->iodev, reg, &val);
	if (ret)
		return ret;

	val >>= shift;
	val &= mask;

	return max8698_list_voltage(rdev, val);
}

static int max8698_set_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct max8698_data *max8698 = rdev_get_drvdata(rdev);
	struct max8698_platform_data *pdata = dev_get_platdata(max8698->iodev->dev);
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const struct voltage_map_desc *desc;
	int ldo = max8698_get_ldo(rdev);
	int reg, shift = 0, mask, ret = 0;
	int i = 0;
	int previous_vol = 0;

	if (ldo >= ARRAY_SIZE(ldo_voltage_map))
		return -EINVAL;

	desc = ldo_voltage_map[ldo];
	if (desc == NULL)
		return -EINVAL;

	/* For Buck1/2 */
	if (ldo == MAX8698_BUCK1)   {
		if (min_vol == (desc->min + desc->step * pdata->dvsarm2)) {
			gpio_set_value(pdata->set2, 0);
			gpio_set_value(pdata->set1, 1);
		} else if (min_vol == (desc->min + desc->step * pdata->dvsarm3)) {
			gpio_set_value(pdata->set2, 1);
			gpio_set_value(pdata->set1, 0);
		} else if (min_vol == (desc->min + desc->step * pdata->dvsarm4)) {
			gpio_set_value(pdata->set2, 1);
			gpio_set_value(pdata->set1, 1);
		} else {
			gpio_set_value(pdata->set2, 0);
			gpio_set_value(pdata->set1, 0);
		}

		goto ramp_delay;
	}

	if (ldo == MAX8698_BUCK2) {
		if (min_vol == (desc->min + desc->step * pdata->dvsint2))
			gpio_set_value(pdata->set3, 1);
		else
			gpio_set_value(pdata->set3, 0);

		goto ramp_delay;
	}

	if (max_vol < desc->min || min_vol > desc->max)
		return -EINVAL;

	while (desc->min + desc->step*i < min_vol &&
			desc->min + desc->step*i < desc->max)
		i++;

	if (desc->min + desc->step*i > max_vol)
		return -EINVAL;

	*selector = i;

	ret = max8698_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	ret = max8698_update_reg(max8698->iodev, reg, i<<shift, mask<<shift);

ramp_delay:
	/* wait for RAMP_UP_DELAY if rdev is BUCK1/2 */
	if (ldo == MAX8698_BUCK1 || MAX8698_BUCK2) {
		int difference = desc->min + desc->step*i - previous_vol/1000;
		if (difference > 0)
			udelay(difference / RAMP_DELAY_VAL);
	}

	return ret;
}

static struct regulator_ops max8698_ldo_ops = {
	.list_voltage		= max8698_list_voltage,
	.is_enabled		= max8698_ldo_is_enabled,
	.enable			= max8698_ldo_enable,
	.disable		= max8698_ldo_disable,
	.get_voltage		= max8698_get_voltage,
	.set_voltage		= max8698_set_voltage,
	.set_suspend_enable	= max8698_ldo_enable,
	.set_suspend_disable	= max8698_ldo_disable,
};

static struct regulator_ops max8698_buck_ops = {
	.list_voltage		= max8698_list_voltage,
	.is_enabled		= max8698_ldo_is_enabled,
	.enable			= max8698_ldo_enable,
	.disable		= max8698_ldo_disable,
	.get_voltage		= max8698_get_voltage,
	.set_voltage		= max8698_set_voltage,
	.set_suspend_enable	= max8698_ldo_enable,
	.set_suspend_disable	= max8698_ldo_disable,
};

static struct regulator_desc regulators[] = {
	{
		.name	= "LDO1",
		.id	= MAX8698_LDO1,
		.ops	= &max8698_ldo_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "LDO2",
		.id	= MAX8698_LDO2,
		.ops	= &max8698_ldo_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "LDO3",
		.id	= MAX8698_LDO3,
		.ops	= &max8698_ldo_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "LDO4",
		.id	= MAX8698_LDO4,
		.ops	= &max8698_ldo_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "LDO5",
		.id	= MAX8698_LDO5,
		.ops	= &max8698_ldo_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "LDO6",
		.id	= MAX8698_LDO6,
		.ops	= &max8698_ldo_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "LDO7",
		.id	= MAX8698_LDO7,
		.ops	= &max8698_ldo_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "LDO8",
		.id	= MAX8698_LDO8,
		.ops	= &max8698_ldo_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "LDO9",
		.id	= MAX8698_LDO9,
		.ops	= &max8698_ldo_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "BUCK1",
		.id	= MAX8698_BUCK1,
		.ops	= &max8698_buck_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "BUCK2",
		.id	= MAX8698_BUCK2,
		.ops	= &max8698_buck_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "BUCK3",
		.id	= MAX8698_BUCK3,
		.ops	= &max8698_buck_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}
};

static __devinit int max8698_pmic_probe(struct platform_device *pdev)
{
	struct max8698_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max8698_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct regulator_dev **rdev;
	struct max8698_data *max8698;
	int i, ret, size;

	if (!pdata) {
		dev_err(pdev->dev.parent, "No platform init data supplied\n");
		return -ENODEV;
	}

	max8698 = kzalloc(sizeof(struct max8698_data), GFP_KERNEL);
	if (!max8698)
		return -ENOMEM;

	size = sizeof(struct regulator_dev *) * (pdata->num_regulators + 1);
	max8698->rdev = kzalloc(size, GFP_KERNEL);
	if (!max8698->rdev) {
		kfree(max8698);
		return -ENOMEM;
	}

	rdev = max8698->rdev;
	max8698->iodev = iodev;
	platform_set_drvdata(pdev, max8698);

	for (i = 0; i < pdata->num_regulators; i++) {
		const struct voltage_map_desc *desc;
		int count;
		int id = pdata->regulators[i].id;
		int index = id - MAX8698_LDO1;

		desc = ldo_voltage_map[id];
		count = (desc->max - desc->min) / desc->step + 1;
		regulators[index].n_voltages = count;

		rdev[i] = regulator_register(&regulators[index], max8698->dev,
				pdata->regulators[i].initdata, max8698);
		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(max8698->dev, "regulator init failed\n");
			rdev[i] = NULL;
			goto err;
		}
	}

	if (pdata->dvsarm1 && pdata->dvsarm2 && pdata->dvsarm3 &&
			pdata->dvsarm4 && gpio_is_valid(pdata->set1) &&
			gpio_is_valid(pdata->set2)) {
		max8698_write_reg(iodev, MAX8698_REG_DVSARM12,
				pdata->dvsarm1 | (pdata->dvsarm2 << 4));
		max8698_write_reg(iodev, MAX8698_REG_DVSARM34,
				pdata->dvsarm3 | (pdata->dvsarm4 << 4));

		ret = gpio_request(pdata->set1, "MAX8698 SET1");
		if (ret)
			goto err;
		gpio_direction_output(pdata->set1, 0);
		ret = gpio_request(pdata->set2, "MAX8698 SET2");
		if (ret)
			goto out_set2;
		gpio_direction_output(pdata->set2, 0);
	}

	if (pdata->dvsint1 && pdata->dvsint2 && gpio_is_valid(pdata->set3)) {
		max8698_write_reg(iodev, MAX8698_REG_DVSINT12,
				pdata->dvsint1 | (pdata->dvsint2 << 4));

		ret = gpio_request(pdata->set3, "MAX8698 SET3");
		if (ret)
			goto out_set3;
		gpio_direction_output(pdata->set3, 0);
	}

	return 0;

out_set3:
	gpio_free(S5PV210_GPH1(7));

out_set2:
	gpio_free(S5PV210_GPH1(6));
err:
	for (i = 0; i <= max8698->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max8698->rdev);
	kfree(max8698);

	return ret;
}

static int __devexit max8698_pmic_remove(struct platform_device *pdev)
{
	struct max8698_data *max8698 = platform_get_drvdata(pdev);
	struct regulator_dev **rdev = max8698->rdev;
	int i;

	for (i = 0; i <= max8698->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max8698->rdev);
	kfree(max8698);

	return 0;
}

static struct platform_driver max8698_pmic_driver = {
	.driver = {
		.name = "max8698-pmic",
		.owner = THIS_MODULE,
	},
	.probe = max8698_pmic_probe,
	.remove = __devexit_p(max8698_pmic_remove),
};

static int __init max8698_pmic_init(void)
{
	return platform_driver_register(&max8698_pmic_driver);
}
subsys_initcall(max8698_pmic_init);

static void __exit max8698_pmic_cleanup(void)
{
	platform_driver_unregister(&max8698_pmic_driver);
}
module_exit(max8698_pmic_cleanup);

MODULE_DESCRIPTION("MAXIM 8698 voltage regulator driver");
