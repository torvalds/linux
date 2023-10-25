// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>

#define REG_CDEV_DRIVER "reg-cooling-device"

struct reg_cooling_device {
	struct regulator		*reg;
	struct thermal_cooling_device	*cool_dev;
	unsigned int			cur_state;
	unsigned int			*lvl;
	unsigned int			lvl_ct;
	char				reg_name[THERMAL_NAME_LENGTH];
	bool				reg_enable;
};

static int reg_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct reg_cooling_device *reg_dev = cdev->devdata;

	*state = reg_dev->lvl_ct;
	return 0;
}

static int reg_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct reg_cooling_device *reg_dev = cdev->devdata;

	*state = reg_dev->cur_state;
	return 0;
}

static int reg_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct reg_cooling_device *reg_dev = cdev->devdata;
	int ret = 0;

	if (state > reg_dev->lvl_ct)
		return -EINVAL;

	if (reg_dev->cur_state == state)
		return ret;

	ret = regulator_set_voltage(reg_dev->reg,
			reg_dev->lvl[state], INT_MAX);
	if (ret) {
		dev_err(&cdev->device,
			"switching to floor %lu err:%d\n",
			state, ret);
		return ret;
	}
	if (reg_dev->reg_enable && !state) {
		ret = regulator_disable(reg_dev->reg);
		if (ret) {
			dev_err(&cdev->device,
				"regulator disable err:%d\n", ret);
			return ret;
		}
		reg_dev->reg_enable = false;
	} else if (!reg_dev->reg_enable && state) {
		ret = regulator_enable(reg_dev->reg);
		if (ret) {
			dev_err(&cdev->device,
				"regulator enable err:%d\n", ret);
			return ret;
		}
		reg_dev->reg_enable = true;
	}
	reg_dev->cur_state = state;

	return ret;
}

static struct thermal_cooling_device_ops reg_device_ops = {
	.get_max_state = reg_get_max_state,
	.get_cur_state = reg_get_cur_state,
	.set_cur_state = reg_set_cur_state,
};

static int reg_cdev_probe(struct platform_device *pdev)
{
	struct reg_cooling_device *reg_dev;
	int ret = 0;
	struct device_node *np;

	np = dev_of_node(&pdev->dev);
	if (!np) {
		dev_err(&pdev->dev,
			"of node not available for cooling device\n");
		return -EINVAL;
	}

	reg_dev = devm_kzalloc(&pdev->dev, sizeof(*reg_dev), GFP_KERNEL);
	if (!reg_dev)
		return -ENOMEM;

	reg_dev->reg = devm_regulator_get(&pdev->dev, "regulator-cdev");
	if (IS_ERR_OR_NULL(reg_dev->reg)) {
		ret = PTR_ERR(reg_dev->reg);
		dev_err(&pdev->dev, "regulator register err:%d\n", ret);
		return ret;
	}
	ret = of_property_count_u32_elems(np, "regulator-levels");
	if (ret <= 0) {
		dev_err(&pdev->dev, "Invalid levels err:%d\n", ret);
		return ret;
	}
	reg_dev->lvl_ct = ret;
	reg_dev->lvl = devm_kcalloc(&pdev->dev, reg_dev->lvl_ct,
			sizeof(*reg_dev->lvl), GFP_KERNEL);
	if (!reg_dev->lvl)
		return -ENOMEM;
	ret = of_property_read_u32_array(np, "regulator-levels",
				reg_dev->lvl, reg_dev->lvl_ct);
	if (ret) {
		dev_err(&pdev->dev, "cdev level fetch err:%d\n", ret);
		return ret;
	}
	/* level count is an index and it depicts the max possible index */
	reg_dev->lvl_ct--;
	reg_dev->cur_state = 0;
	reg_dev->reg_enable = false;
	strscpy(reg_dev->reg_name, np->name, THERMAL_NAME_LENGTH);

	reg_dev->cool_dev = devm_thermal_of_cooling_device_register(&pdev->dev,
					np, reg_dev->reg_name, reg_dev,
					&reg_device_ops);
	if (IS_ERR(reg_dev->cool_dev)) {
		ret = PTR_ERR(reg_dev->cool_dev);
		dev_err(&pdev->dev, "regulator cdev register err:%d\n",
				ret);
		return ret;
	}

	return ret;
}

static const struct of_device_id reg_cdev_of_match[] = {
	{.compatible = "qcom,regulator-cooling-device", },
	{},
};

static struct platform_driver reg_cdev_driver = {
	.driver = {
		.name = REG_CDEV_DRIVER,
		.of_match_table = reg_cdev_of_match,
	},
	.probe = reg_cdev_probe,
};
module_platform_driver(reg_cdev_driver);
MODULE_LICENSE("GPL");
