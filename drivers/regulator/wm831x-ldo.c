/*
 * wm831x-ldo.c  --  LDO driver for the WM831x series
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/regulator.h>
#include <linux/mfd/wm831x/pdata.h>

#define WM831X_LDO_MAX_NAME 9

#define WM831X_LDO_CONTROL       0
#define WM831X_LDO_ON_CONTROL    1
#define WM831X_LDO_SLEEP_CONTROL 2

#define WM831X_ALIVE_LDO_ON_CONTROL    0
#define WM831X_ALIVE_LDO_SLEEP_CONTROL 1

struct wm831x_ldo {
	char name[WM831X_LDO_MAX_NAME];
	char supply_name[WM831X_LDO_MAX_NAME];
	struct regulator_desc desc;
	int base;
	struct wm831x *wm831x;
	struct regulator_dev *regulator;
};

/*
 * Shared
 */

static irqreturn_t wm831x_ldo_uv_irq(int irq, void *data)
{
	struct wm831x_ldo *ldo = data;

	regulator_notifier_call_chain(ldo->regulator,
				      REGULATOR_EVENT_UNDER_VOLTAGE,
				      NULL);

	return IRQ_HANDLED;
}

/*
 * General purpose LDOs
 */

static const struct regulator_linear_range wm831x_gp_ldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000, 0, 14, 50000),
	REGULATOR_LINEAR_RANGE(1700000, 15, 31, 100000),
};

static int wm831x_gp_ldo_set_suspend_voltage(struct regulator_dev *rdev,
					     int uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int sel, reg = ldo->base + WM831X_LDO_SLEEP_CONTROL;

	sel = regulator_map_voltage_linear_range(rdev, uV, uV);
	if (sel < 0)
		return sel;

	return wm831x_set_bits(wm831x, reg, WM831X_LDO1_ON_VSEL_MASK, sel);
}

static unsigned int wm831x_gp_ldo_get_mode(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int ctrl_reg = ldo->base + WM831X_LDO_CONTROL;
	int on_reg = ldo->base + WM831X_LDO_ON_CONTROL;
	int ret;

	ret = wm831x_reg_read(wm831x, on_reg);
	if (ret < 0)
		return ret;

	if (!(ret & WM831X_LDO1_ON_MODE))
		return REGULATOR_MODE_NORMAL;

	ret = wm831x_reg_read(wm831x, ctrl_reg);
	if (ret < 0)
		return ret;

	if (ret & WM831X_LDO1_LP_MODE)
		return REGULATOR_MODE_STANDBY;
	else
		return REGULATOR_MODE_IDLE;
}

static int wm831x_gp_ldo_set_mode(struct regulator_dev *rdev,
				  unsigned int mode)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int ctrl_reg = ldo->base + WM831X_LDO_CONTROL;
	int on_reg = ldo->base + WM831X_LDO_ON_CONTROL;
	int ret;


	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		ret = wm831x_set_bits(wm831x, on_reg,
				      WM831X_LDO1_ON_MODE, 0);
		if (ret < 0)
			return ret;
		break;

	case REGULATOR_MODE_IDLE:
		ret = wm831x_set_bits(wm831x, ctrl_reg,
				      WM831X_LDO1_LP_MODE, 0);
		if (ret < 0)
			return ret;

		ret = wm831x_set_bits(wm831x, on_reg,
				      WM831X_LDO1_ON_MODE,
				      WM831X_LDO1_ON_MODE);
		if (ret < 0)
			return ret;
		break;

	case REGULATOR_MODE_STANDBY:
		ret = wm831x_set_bits(wm831x, ctrl_reg,
				      WM831X_LDO1_LP_MODE,
				      WM831X_LDO1_LP_MODE);
		if (ret < 0)
			return ret;

		ret = wm831x_set_bits(wm831x, on_reg,
				      WM831X_LDO1_ON_MODE,
				      WM831X_LDO1_ON_MODE);
		if (ret < 0)
			return ret;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int wm831x_gp_ldo_get_status(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int mask = 1 << rdev_get_id(rdev);
	int ret;

	/* Is the regulator on? */
	ret = wm831x_reg_read(wm831x, WM831X_LDO_STATUS);
	if (ret < 0)
		return ret;
	if (!(ret & mask))
		return REGULATOR_STATUS_OFF;

	/* Is it reporting under voltage? */
	ret = wm831x_reg_read(wm831x, WM831X_LDO_UV_STATUS);
	if (ret < 0)
		return ret;
	if (ret & mask)
		return REGULATOR_STATUS_ERROR;

	ret = wm831x_gp_ldo_get_mode(rdev);
	if (ret < 0)
		return ret;
	else
		return regulator_mode_to_status(ret);
}

static unsigned int wm831x_gp_ldo_get_optimum_mode(struct regulator_dev *rdev,
						   int input_uV,
						   int output_uV, int load_uA)
{
	if (load_uA < 20000)
		return REGULATOR_MODE_STANDBY;
	if (load_uA < 50000)
		return REGULATOR_MODE_IDLE;
	return REGULATOR_MODE_NORMAL;
}


static struct regulator_ops wm831x_gp_ldo_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.set_suspend_voltage = wm831x_gp_ldo_set_suspend_voltage,
	.get_mode = wm831x_gp_ldo_get_mode,
	.set_mode = wm831x_gp_ldo_set_mode,
	.get_status = wm831x_gp_ldo_get_status,
	.get_optimum_mode = wm831x_gp_ldo_get_optimum_mode,
	.get_bypass = regulator_get_bypass_regmap,
	.set_bypass = regulator_set_bypass_regmap,

	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
};

static int wm831x_gp_ldo_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = dev_get_platdata(wm831x->dev);
	struct regulator_config config = { };
	int id;
	struct wm831x_ldo *ldo;
	struct resource *res;
	int ret, irq;

	if (pdata && pdata->wm831x_num)
		id = (pdata->wm831x_num * 10) + 1;
	else
		id = 0;
	id = pdev->id - id;

	dev_dbg(&pdev->dev, "Probing LDO%d\n", id + 1);

	ldo = devm_kzalloc(&pdev->dev, sizeof(struct wm831x_ldo), GFP_KERNEL);
	if (!ldo)
		return -ENOMEM;

	ldo->wm831x = wm831x;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No REG resource\n");
		ret = -EINVAL;
		goto err;
	}
	ldo->base = res->start;

	snprintf(ldo->name, sizeof(ldo->name), "LDO%d", id + 1);
	ldo->desc.name = ldo->name;

	snprintf(ldo->supply_name, sizeof(ldo->supply_name),
		 "LDO%dVDD", id + 1);
	ldo->desc.supply_name = ldo->supply_name;

	ldo->desc.id = id;
	ldo->desc.type = REGULATOR_VOLTAGE;
	ldo->desc.n_voltages = 32;
	ldo->desc.ops = &wm831x_gp_ldo_ops;
	ldo->desc.owner = THIS_MODULE;
	ldo->desc.vsel_reg = ldo->base + WM831X_LDO_ON_CONTROL;
	ldo->desc.vsel_mask = WM831X_LDO1_ON_VSEL_MASK;
	ldo->desc.enable_reg = WM831X_LDO_ENABLE;
	ldo->desc.enable_mask = 1 << id;
	ldo->desc.bypass_reg = ldo->base;
	ldo->desc.bypass_mask = WM831X_LDO1_SWI;
	ldo->desc.linear_ranges = wm831x_gp_ldo_ranges;
	ldo->desc.n_linear_ranges = ARRAY_SIZE(wm831x_gp_ldo_ranges);

	config.dev = pdev->dev.parent;
	if (pdata)
		config.init_data = pdata->ldo[id];
	config.driver_data = ldo;
	config.regmap = wm831x->regmap;

	ldo->regulator = devm_regulator_register(&pdev->dev, &ldo->desc,
						 &config);
	if (IS_ERR(ldo->regulator)) {
		ret = PTR_ERR(ldo->regulator);
		dev_err(wm831x->dev, "Failed to register LDO%d: %d\n",
			id + 1, ret);
		goto err;
	}

	irq = wm831x_irq(wm831x, platform_get_irq_byname(pdev, "UV"));
	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					wm831x_ldo_uv_irq,
					IRQF_TRIGGER_RISING, ldo->name,
					ldo);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request UV IRQ %d: %d\n",
			irq, ret);
		goto err;
	}

	platform_set_drvdata(pdev, ldo);

	return 0;

err:
	return ret;
}

static struct platform_driver wm831x_gp_ldo_driver = {
	.probe = wm831x_gp_ldo_probe,
	.driver		= {
		.name	= "wm831x-ldo",
	},
};

/*
 * Analogue LDOs
 */

static const struct regulator_linear_range wm831x_aldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(1000000, 0, 12, 50000),
	REGULATOR_LINEAR_RANGE(1700000, 13, 31, 100000),
};

static int wm831x_aldo_set_suspend_voltage(struct regulator_dev *rdev,
					     int uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int sel, reg = ldo->base + WM831X_LDO_SLEEP_CONTROL;

	sel = regulator_map_voltage_linear_range(rdev, uV, uV);
	if (sel < 0)
		return sel;

	return wm831x_set_bits(wm831x, reg, WM831X_LDO7_ON_VSEL_MASK, sel);
}

static unsigned int wm831x_aldo_get_mode(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int on_reg = ldo->base + WM831X_LDO_ON_CONTROL;
	int ret;

	ret = wm831x_reg_read(wm831x, on_reg);
	if (ret < 0)
		return 0;

	if (ret & WM831X_LDO7_ON_MODE)
		return REGULATOR_MODE_IDLE;
	else
		return REGULATOR_MODE_NORMAL;
}

static int wm831x_aldo_set_mode(struct regulator_dev *rdev,
				  unsigned int mode)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int on_reg = ldo->base + WM831X_LDO_ON_CONTROL;
	int ret;


	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		ret = wm831x_set_bits(wm831x, on_reg, WM831X_LDO7_ON_MODE, 0);
		if (ret < 0)
			return ret;
		break;

	case REGULATOR_MODE_IDLE:
		ret = wm831x_set_bits(wm831x, on_reg, WM831X_LDO7_ON_MODE,
				      WM831X_LDO7_ON_MODE);
		if (ret < 0)
			return ret;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int wm831x_aldo_get_status(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int mask = 1 << rdev_get_id(rdev);
	int ret;

	/* Is the regulator on? */
	ret = wm831x_reg_read(wm831x, WM831X_LDO_STATUS);
	if (ret < 0)
		return ret;
	if (!(ret & mask))
		return REGULATOR_STATUS_OFF;

	/* Is it reporting under voltage? */
	ret = wm831x_reg_read(wm831x, WM831X_LDO_UV_STATUS);
	if (ret < 0)
		return ret;
	if (ret & mask)
		return REGULATOR_STATUS_ERROR;

	ret = wm831x_aldo_get_mode(rdev);
	if (ret < 0)
		return ret;
	else
		return regulator_mode_to_status(ret);
}

static struct regulator_ops wm831x_aldo_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.set_suspend_voltage = wm831x_aldo_set_suspend_voltage,
	.get_mode = wm831x_aldo_get_mode,
	.set_mode = wm831x_aldo_set_mode,
	.get_status = wm831x_aldo_get_status,
	.set_bypass = regulator_set_bypass_regmap,
	.get_bypass = regulator_get_bypass_regmap,

	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
};

static int wm831x_aldo_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = dev_get_platdata(wm831x->dev);
	struct regulator_config config = { };
	int id;
	struct wm831x_ldo *ldo;
	struct resource *res;
	int ret, irq;

	if (pdata && pdata->wm831x_num)
		id = (pdata->wm831x_num * 10) + 1;
	else
		id = 0;
	id = pdev->id - id;

	dev_dbg(&pdev->dev, "Probing LDO%d\n", id + 1);

	ldo = devm_kzalloc(&pdev->dev, sizeof(struct wm831x_ldo), GFP_KERNEL);
	if (!ldo)
		return -ENOMEM;

	ldo->wm831x = wm831x;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No REG resource\n");
		ret = -EINVAL;
		goto err;
	}
	ldo->base = res->start;

	snprintf(ldo->name, sizeof(ldo->name), "LDO%d", id + 1);
	ldo->desc.name = ldo->name;

	snprintf(ldo->supply_name, sizeof(ldo->supply_name),
		 "LDO%dVDD", id + 1);
	ldo->desc.supply_name = ldo->supply_name;

	ldo->desc.id = id;
	ldo->desc.type = REGULATOR_VOLTAGE;
	ldo->desc.n_voltages = 32;
	ldo->desc.linear_ranges = wm831x_aldo_ranges;
	ldo->desc.n_linear_ranges = ARRAY_SIZE(wm831x_aldo_ranges);
	ldo->desc.ops = &wm831x_aldo_ops;
	ldo->desc.owner = THIS_MODULE;
	ldo->desc.vsel_reg = ldo->base + WM831X_LDO_ON_CONTROL;
	ldo->desc.vsel_mask = WM831X_LDO7_ON_VSEL_MASK;
	ldo->desc.enable_reg = WM831X_LDO_ENABLE;
	ldo->desc.enable_mask = 1 << id;
	ldo->desc.bypass_reg = ldo->base;
	ldo->desc.bypass_mask = WM831X_LDO7_SWI;

	config.dev = pdev->dev.parent;
	if (pdata)
		config.init_data = pdata->ldo[id];
	config.driver_data = ldo;
	config.regmap = wm831x->regmap;

	ldo->regulator = devm_regulator_register(&pdev->dev, &ldo->desc,
						 &config);
	if (IS_ERR(ldo->regulator)) {
		ret = PTR_ERR(ldo->regulator);
		dev_err(wm831x->dev, "Failed to register LDO%d: %d\n",
			id + 1, ret);
		goto err;
	}

	irq = wm831x_irq(wm831x, platform_get_irq_byname(pdev, "UV"));
	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					wm831x_ldo_uv_irq,
					IRQF_TRIGGER_RISING, ldo->name, ldo);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request UV IRQ %d: %d\n",
			irq, ret);
		goto err;
	}

	platform_set_drvdata(pdev, ldo);

	return 0;

err:
	return ret;
}

static struct platform_driver wm831x_aldo_driver = {
	.probe = wm831x_aldo_probe,
	.driver		= {
		.name	= "wm831x-aldo",
	},
};

/*
 * Alive LDO
 */

#define WM831X_ALIVE_LDO_MAX_SELECTOR 0xf

static int wm831x_alive_ldo_set_suspend_voltage(struct regulator_dev *rdev,
					     int uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int sel, reg = ldo->base + WM831X_ALIVE_LDO_SLEEP_CONTROL;

	sel = regulator_map_voltage_linear(rdev, uV, uV);
	if (sel < 0)
		return sel;

	return wm831x_set_bits(wm831x, reg, WM831X_LDO11_ON_VSEL_MASK, sel);
}

static int wm831x_alive_ldo_get_status(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int mask = 1 << rdev_get_id(rdev);
	int ret;

	/* Is the regulator on? */
	ret = wm831x_reg_read(wm831x, WM831X_LDO_STATUS);
	if (ret < 0)
		return ret;
	if (ret & mask)
		return REGULATOR_STATUS_ON;
	else
		return REGULATOR_STATUS_OFF;
}

static struct regulator_ops wm831x_alive_ldo_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.set_suspend_voltage = wm831x_alive_ldo_set_suspend_voltage,
	.get_status = wm831x_alive_ldo_get_status,

	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
};

static int wm831x_alive_ldo_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = dev_get_platdata(wm831x->dev);
	struct regulator_config config = { };
	int id;
	struct wm831x_ldo *ldo;
	struct resource *res;
	int ret;

	if (pdata && pdata->wm831x_num)
		id = (pdata->wm831x_num * 10) + 1;
	else
		id = 0;
	id = pdev->id - id;


	dev_dbg(&pdev->dev, "Probing LDO%d\n", id + 1);

	ldo = devm_kzalloc(&pdev->dev, sizeof(struct wm831x_ldo), GFP_KERNEL);
	if (!ldo)
		return -ENOMEM;

	ldo->wm831x = wm831x;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No REG resource\n");
		ret = -EINVAL;
		goto err;
	}
	ldo->base = res->start;

	snprintf(ldo->name, sizeof(ldo->name), "LDO%d", id + 1);
	ldo->desc.name = ldo->name;

	snprintf(ldo->supply_name, sizeof(ldo->supply_name),
		 "LDO%dVDD", id + 1);
	ldo->desc.supply_name = ldo->supply_name;

	ldo->desc.id = id;
	ldo->desc.type = REGULATOR_VOLTAGE;
	ldo->desc.n_voltages = WM831X_ALIVE_LDO_MAX_SELECTOR + 1;
	ldo->desc.ops = &wm831x_alive_ldo_ops;
	ldo->desc.owner = THIS_MODULE;
	ldo->desc.vsel_reg = ldo->base + WM831X_ALIVE_LDO_ON_CONTROL;
	ldo->desc.vsel_mask = WM831X_LDO11_ON_VSEL_MASK;
	ldo->desc.enable_reg = WM831X_LDO_ENABLE;
	ldo->desc.enable_mask = 1 << id;
	ldo->desc.min_uV = 800000;
	ldo->desc.uV_step = 50000;
	ldo->desc.enable_time = 1000;

	config.dev = pdev->dev.parent;
	if (pdata)
		config.init_data = pdata->ldo[id];
	config.driver_data = ldo;
	config.regmap = wm831x->regmap;

	ldo->regulator = devm_regulator_register(&pdev->dev, &ldo->desc,
						 &config);
	if (IS_ERR(ldo->regulator)) {
		ret = PTR_ERR(ldo->regulator);
		dev_err(wm831x->dev, "Failed to register LDO%d: %d\n",
			id + 1, ret);
		goto err;
	}

	platform_set_drvdata(pdev, ldo);

	return 0;

err:
	return ret;
}

static struct platform_driver wm831x_alive_ldo_driver = {
	.probe = wm831x_alive_ldo_probe,
	.driver		= {
		.name	= "wm831x-alive-ldo",
	},
};

static int __init wm831x_ldo_init(void)
{
	int ret;

	ret = platform_driver_register(&wm831x_gp_ldo_driver);
	if (ret != 0)
		pr_err("Failed to register WM831x GP LDO driver: %d\n", ret);

	ret = platform_driver_register(&wm831x_aldo_driver);
	if (ret != 0)
		pr_err("Failed to register WM831x ALDO driver: %d\n", ret);

	ret = platform_driver_register(&wm831x_alive_ldo_driver);
	if (ret != 0)
		pr_err("Failed to register WM831x alive LDO driver: %d\n",
		       ret);

	return 0;
}
subsys_initcall(wm831x_ldo_init);

static void __exit wm831x_ldo_exit(void)
{
	platform_driver_unregister(&wm831x_alive_ldo_driver);
	platform_driver_unregister(&wm831x_aldo_driver);
	platform_driver_unregister(&wm831x_gp_ldo_driver);
}
module_exit(wm831x_ldo_exit);

/* Module information */
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("WM831x LDO driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-ldo");
MODULE_ALIAS("platform:wm831x-aldo");
MODULE_ALIAS("platform:wm831x-aliveldo");
