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

//#define WM831X_LDO_MAX_NAME 6

#define WM831X_LDO_CONTROL       0
#define WM831X_LDO_ON_CONTROL    1
#define WM831X_LDO_SLEEP_CONTROL 2

#define WM831X_ALIVE_LDO_ON_CONTROL    0
#define WM831X_ALIVE_LDO_SLEEP_CONTROL 1

#if 0
struct wm831x_ldo {
	char name[WM831X_LDO_MAX_NAME];
	struct regulator_desc desc;
	int base;
	struct wm831x *wm831x;
	struct regulator_dev *regulator;
};
#endif

/*
 * Shared
 */
extern int reboot_cmd_get(void);
static int wm831x_ldo_is_enabled(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int mask = 1 << rdev_get_id(rdev);
	int reg;

	reg = wm831x_reg_read(wm831x, WM831X_LDO_ENABLE);
	if (reg < 0)
		return reg;

	if (reg & mask)
		return 1;
	else
		return 0;
}

static int wm831x_ldo_enable(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int mask = 1 << rdev_get_id(rdev);
	//printk("%s,%x\n", __FUNCTION__,mask);
	return wm831x_set_bits(wm831x, WM831X_LDO_ENABLE, mask, mask);
}

static int wm831x_ldo_disable(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int mask = 1 << rdev_get_id(rdev);
	//printk("%s\n", __FUNCTION__);
	return wm831x_set_bits(wm831x, WM831X_LDO_ENABLE, mask, 0);
}

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

#define WM831X_GP_LDO_SELECTOR_LOW 0xe
#define WM831X_GP_LDO_MAX_SELECTOR 0x1f

static int wm831x_gp_ldo_list_voltage(struct regulator_dev *rdev,
				      unsigned int selector)
{
	/* 0.9-1.6V in 50mV steps */
	if (selector <= WM831X_GP_LDO_SELECTOR_LOW)
		return 900000 + (selector * 50000);
	/* 1.7-3.3V in 50mV steps */
	if (selector <= WM831X_GP_LDO_MAX_SELECTOR)
		return 1600000 + ((selector - WM831X_GP_LDO_SELECTOR_LOW)
				  * 100000);
	return -EINVAL;
}

static int wm831x_gp_ldo_set_voltage_int(struct regulator_dev *rdev, int reg,
					 int min_uV, int max_uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int vsel, ret;

	if (min_uV < 900000)
		vsel = 0;
	else if (min_uV < 1700000)
		vsel = ((min_uV - 900000) / 50000);
	else
		vsel = ((min_uV - 1700000) / 100000)
			+ WM831X_GP_LDO_SELECTOR_LOW + 1;

	ret = wm831x_gp_ldo_list_voltage(rdev, vsel);
	if (ret < 0)
		return ret;
	if (ret < min_uV || ret > max_uV)
		return -EINVAL;

	return wm831x_set_bits(wm831x, reg, WM831X_LDO1_ON_VSEL_MASK, vsel);
}

static int wm831x_gp_ldo_set_voltage(struct regulator_dev *rdev,
				     int min_uV, int max_uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	int reg = ldo->base + WM831X_LDO_ON_CONTROL;
	//printk("%s base=%x,%d,%d\n", __FUNCTION__,ldo->base,min_uV,max_uV);
	return wm831x_gp_ldo_set_voltage_int(rdev, reg, min_uV, max_uV);
}

static int wm831x_gp_ldo_set_suspend_voltage(struct regulator_dev *rdev,
					     int uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	int reg = ldo->base + WM831X_LDO_SLEEP_CONTROL;

	return wm831x_gp_ldo_set_voltage_int(rdev, reg, uV, uV);
}

static int wm831x_gp_ldo_get_voltage(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int reg = ldo->base + WM831X_LDO_ON_CONTROL;
	int ret;

	ret = wm831x_reg_read(wm831x, reg);
	if (ret < 0)
		return ret;
	//printk("%s base=%x,ret=%x\n", __FUNCTION__,ldo->base,ret);
	ret &= WM831X_LDO1_ON_VSEL_MASK;

	return wm831x_gp_ldo_list_voltage(rdev, ret);
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

	printk("%s base=%x,mode=%x\n", __FUNCTION__,ldo->base,mode);
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

int wm831x_ldo_set_suspend_enable(struct regulator_dev *rdev)
{

        return 0;
}
int wm831x_ldo_set_suspend_disable(struct regulator_dev *rdev)
{

        return 0;
}

static struct regulator_ops wm831x_gp_ldo_ops = {
	.list_voltage = wm831x_gp_ldo_list_voltage,
	.get_voltage = wm831x_gp_ldo_get_voltage,
	.set_voltage = wm831x_gp_ldo_set_voltage,
	.set_suspend_voltage = wm831x_gp_ldo_set_suspend_voltage,
	.get_mode = wm831x_gp_ldo_get_mode,
	.set_mode = wm831x_gp_ldo_set_mode,
	.get_status = wm831x_gp_ldo_get_status,
	.get_optimum_mode = wm831x_gp_ldo_get_optimum_mode,

	.is_enabled = wm831x_ldo_is_enabled,
	.enable = wm831x_ldo_enable,
	.disable = wm831x_ldo_disable,
	.set_suspend_enable = wm831x_ldo_set_suspend_enable,
	.set_suspend_disable = wm831x_ldo_set_suspend_disable,
};

static __devinit int wm831x_gp_ldo_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	int id = pdev->id % ARRAY_SIZE(pdata->ldo);
	struct wm831x_ldo *ldo;
	struct resource *res;
	int ret, irq;

	dev_dbg(&pdev->dev, "Probing LDO%d\n", id + 1);
	printk("Probing LDO%d\n", id + 1);
	if (pdata == NULL || pdata->ldo[id] == NULL)
		return -ENODEV;

	ldo = kzalloc(sizeof(struct wm831x_ldo), GFP_KERNEL);
	if (ldo == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	ldo->wm831x = wm831x;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No I/O resource\n");
		ret = -EINVAL;
		goto err;
	}
	ldo->base = res->start;

	snprintf(ldo->name, sizeof(ldo->name), "LDO%d", id + 1);
	ldo->desc.name = ldo->name;
	ldo->desc.id = id;
	ldo->desc.type = REGULATOR_VOLTAGE;
	ldo->desc.n_voltages = WM831X_GP_LDO_MAX_SELECTOR + 1;
	ldo->desc.ops = &wm831x_gp_ldo_ops;
	ldo->desc.owner = THIS_MODULE;

	ldo->regulator = regulator_register(&ldo->desc, &pdev->dev,
					     pdata->ldo[id], ldo);
	if (IS_ERR(ldo->regulator)) {
		ret = PTR_ERR(ldo->regulator);
		dev_err(wm831x->dev, "Failed to register LDO%d: %d\n",
			id + 1, ret);
		goto err;
	}

	irq = platform_get_irq_byname(pdev, "UV");
	ret = wm831x_request_irq(wm831x, irq, wm831x_ldo_uv_irq,
				 IRQF_TRIGGER_RISING, ldo->name,
				 ldo);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request UV IRQ %d: %d\n",
			irq, ret);
		goto err_regulator;
	}

	platform_set_drvdata(pdev, ldo);

	return 0;

err_regulator:
	regulator_unregister(ldo->regulator);
err:
	kfree(ldo);
	return ret;
}

static __devexit int wm831x_gp_ldo_remove(struct platform_device *pdev)
{
	struct wm831x_ldo *ldo = platform_get_drvdata(pdev);
	struct wm831x *wm831x = ldo->wm831x;

	platform_set_drvdata(pdev, NULL);

	wm831x_free_irq(wm831x, platform_get_irq_byname(pdev, "UV"), ldo);
	regulator_unregister(ldo->regulator);
	kfree(ldo);

	return 0;
}

static struct platform_driver wm831x_gp_ldo_driver = {
	.probe = wm831x_gp_ldo_probe,
	.remove = __devexit_p(wm831x_gp_ldo_remove),
	.driver		= {
		.name	= "wm831x-ldo",
		.owner	= THIS_MODULE,
	},
};

/*
 * Analogue LDOs
 */


#define WM831X_ALDO_SELECTOR_LOW 0xc
#define WM831X_ALDO_MAX_SELECTOR 0x1f

static int wm831x_aldo_list_voltage(struct regulator_dev *rdev,
				      unsigned int selector)
{
	/* 1-1.6V in 50mV steps */
	if (selector <= WM831X_ALDO_SELECTOR_LOW)
		return 1000000 + (selector * 50000);
	/* 1.7-3.5V in 50mV steps */
	if (selector <= WM831X_ALDO_MAX_SELECTOR)
		return 1600000 + ((selector - WM831X_ALDO_SELECTOR_LOW)
				  * 100000);
	return -EINVAL;
}

static int wm831x_aldo_set_voltage_int(struct regulator_dev *rdev, int reg,
					 int min_uV, int max_uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int vsel, ret;

	if (min_uV < 1000000)
		vsel = 0;
	else if (min_uV < 1700000)
		vsel = ((min_uV - 1000000) / 50000);
	else
		vsel = ((min_uV - 1700000) / 100000)
			+ WM831X_ALDO_SELECTOR_LOW + 1;

	ret = wm831x_aldo_list_voltage(rdev, vsel);
	if (ret < 0)
		return ret;
	if (ret < min_uV || ret > max_uV)
		return -EINVAL;

	return wm831x_set_bits(wm831x, reg, WM831X_LDO7_ON_VSEL_MASK, vsel);
}

static int wm831x_aldo_set_voltage(struct regulator_dev *rdev,
				     int min_uV, int max_uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	int reg = ldo->base + WM831X_LDO_ON_CONTROL;
	printk("%s base=%x,min_uV=%d,%d\n", __FUNCTION__,ldo->base,min_uV,max_uV);
	return wm831x_aldo_set_voltage_int(rdev, reg, min_uV, max_uV);
}

static int wm831x_aldo_set_suspend_voltage(struct regulator_dev *rdev,
					     int uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	int reg = ldo->base + WM831X_LDO_SLEEP_CONTROL;

	return wm831x_aldo_set_voltage_int(rdev, reg, uV, uV);
}

static int wm831x_aldo_get_voltage(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int reg = ldo->base + WM831X_LDO_ON_CONTROL;
	int ret;
	
	ret = wm831x_reg_read(wm831x, reg);
	if (ret < 0)
		return ret;
	printk("%s base=%x,ret=%x\n", __FUNCTION__,ldo->base,ret);
	ret &= WM831X_LDO7_ON_VSEL_MASK;
	
	return wm831x_aldo_list_voltage(rdev, ret);
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
	int ctrl_reg = ldo->base + WM831X_LDO_CONTROL;
	int on_reg = ldo->base + WM831X_LDO_ON_CONTROL;
	int ret;


	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		ret = wm831x_set_bits(wm831x, on_reg,
				      WM831X_LDO7_ON_MODE, 0);
		if (ret < 0)
			return ret;
		break;

	case REGULATOR_MODE_IDLE:
		ret = wm831x_set_bits(wm831x, ctrl_reg,
				      WM831X_LDO7_ON_MODE,
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
	if (ret & mask)
		return REGULATOR_STATUS_ERROR;

	ret = wm831x_aldo_get_mode(rdev);
	if (ret < 0)
		return ret;
	else
		return regulator_mode_to_status(ret);
}

static struct regulator_ops wm831x_aldo_ops = {
	.list_voltage = wm831x_aldo_list_voltage,
	.get_voltage = wm831x_aldo_get_voltage,
	.set_voltage = wm831x_aldo_set_voltage,
	.set_suspend_voltage = wm831x_aldo_set_suspend_voltage,
	.get_mode = wm831x_aldo_get_mode,
	.set_mode = wm831x_aldo_set_mode,
	.get_status = wm831x_aldo_get_status,

	.is_enabled = wm831x_ldo_is_enabled,
	.enable = wm831x_ldo_enable,
	.disable = wm831x_ldo_disable,
	.set_suspend_enable = wm831x_ldo_set_suspend_enable,
	.set_suspend_disable = wm831x_ldo_set_suspend_disable,
};

static __devinit int wm831x_aldo_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	int id = pdev->id % ARRAY_SIZE(pdata->ldo);
	struct wm831x_ldo *ldo;
	struct resource *res;
	int ret, irq;

	dev_dbg(&pdev->dev, "Probing LDO%d\n", id + 1);
	printk("Probing LDO%d--\n", id + 1);
	if (pdata == NULL || pdata->ldo[id] == NULL)
		return -ENODEV;

	ldo = kzalloc(sizeof(struct wm831x_ldo), GFP_KERNEL);
	if (ldo == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	ldo->wm831x = wm831x;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No I/O resource\n");
		ret = -EINVAL;
		goto err;
	}
	ldo->base = res->start;

	snprintf(ldo->name, sizeof(ldo->name), "LDO%d", id + 1);
	ldo->desc.name = ldo->name;
	ldo->desc.id = id;
	ldo->desc.type = REGULATOR_VOLTAGE;
	ldo->desc.n_voltages = WM831X_ALDO_MAX_SELECTOR + 1;
	ldo->desc.ops = &wm831x_aldo_ops;
	ldo->desc.owner = THIS_MODULE;

	ldo->regulator = regulator_register(&ldo->desc, &pdev->dev,
					     pdata->ldo[id], ldo);
	if (IS_ERR(ldo->regulator)) {
		ret = PTR_ERR(ldo->regulator);
		dev_err(wm831x->dev, "Failed to register LDO%d: %d\n",
			id + 1, ret);
		goto err;
	}

	irq = platform_get_irq_byname(pdev, "UV");
	ret = wm831x_request_irq(wm831x, irq, wm831x_ldo_uv_irq,
				 IRQF_TRIGGER_RISING, ldo->name,
				 ldo);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request UV IRQ %d: %d\n",
			irq, ret);
		goto err_regulator;
	}

	platform_set_drvdata(pdev, ldo);

	return 0;

err_regulator:
	regulator_unregister(ldo->regulator);
err:
	kfree(ldo);
	return ret;
}

static __devexit int wm831x_aldo_remove(struct platform_device *pdev)
{
	struct wm831x_ldo *ldo = platform_get_drvdata(pdev);
	struct wm831x *wm831x = ldo->wm831x;

	wm831x_free_irq(wm831x, platform_get_irq_byname(pdev, "UV"), ldo);
	regulator_unregister(ldo->regulator);
	kfree(ldo);

	return 0;
}

static struct platform_driver wm831x_aldo_driver = {
	.probe = wm831x_aldo_probe,
	.remove = __devexit_p(wm831x_aldo_remove),
	.driver		= {
		.name	= "wm831x-aldo",
		.owner	= THIS_MODULE,
	},
};

/*
 * Alive LDO
 */

#define WM831X_ALIVE_LDO_MAX_SELECTOR 0xf

static int wm831x_alive_ldo_list_voltage(struct regulator_dev *rdev,
				      unsigned int selector)
{
	/* 0.8-1.55V in 50mV steps */
	if (selector <= WM831X_ALIVE_LDO_MAX_SELECTOR)
		return 800000 + (selector * 50000);
	return -EINVAL;
}

static int wm831x_alive_ldo_set_voltage_int(struct regulator_dev *rdev,
					    int reg,
					    int min_uV, int max_uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int vsel, ret;

	vsel = (min_uV - 800000) / 50000;

	ret = wm831x_alive_ldo_list_voltage(rdev, vsel);
	if (ret < 0)
		return ret;
	if (ret < min_uV || ret > max_uV)
		return -EINVAL;

	return wm831x_set_bits(wm831x, reg, WM831X_LDO11_ON_VSEL_MASK, vsel);
}

static int wm831x_alive_ldo_set_voltage(struct regulator_dev *rdev,
				     int min_uV, int max_uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	int reg = ldo->base + WM831X_ALIVE_LDO_ON_CONTROL;

	return wm831x_alive_ldo_set_voltage_int(rdev, reg, min_uV, max_uV);
}

static int wm831x_alive_ldo_set_suspend_voltage(struct regulator_dev *rdev,
					     int uV)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	int reg = ldo->base + WM831X_ALIVE_LDO_SLEEP_CONTROL;

	return wm831x_alive_ldo_set_voltage_int(rdev, reg, uV, uV);
}

static int wm831x_alive_ldo_get_voltage(struct regulator_dev *rdev)
{
	struct wm831x_ldo *ldo = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = ldo->wm831x;
	int reg = ldo->base + WM831X_ALIVE_LDO_ON_CONTROL;
	int ret;

	ret = wm831x_reg_read(wm831x, reg);
	if (ret < 0)
		return ret;

	ret &= WM831X_LDO11_ON_VSEL_MASK;

	return wm831x_alive_ldo_list_voltage(rdev, ret);
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
	.list_voltage = wm831x_alive_ldo_list_voltage,
	.get_voltage = wm831x_alive_ldo_get_voltage,
	.set_voltage = wm831x_alive_ldo_set_voltage,
	.set_suspend_voltage = wm831x_alive_ldo_set_suspend_voltage,
	.get_status = wm831x_alive_ldo_get_status,

	.is_enabled = wm831x_ldo_is_enabled,
	.enable = wm831x_ldo_enable,
	.disable = wm831x_ldo_disable,
	.set_suspend_enable = wm831x_ldo_set_suspend_enable,
	.set_suspend_disable = wm831x_ldo_set_suspend_disable,
};

static __devinit int wm831x_alive_ldo_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	int id = pdev->id % ARRAY_SIZE(pdata->ldo);
	struct wm831x_ldo *ldo;
	struct resource *res;
	int ret;

	dev_dbg(&pdev->dev, "Probing LDO%d\n", id + 1);
	printk("wm831x_alive_ldo_probe Probing LDO%d\n", id + 1);
	if (pdata == NULL || pdata->ldo[id] == NULL)
		return -ENODEV;

	ldo = kzalloc(sizeof(struct wm831x_ldo), GFP_KERNEL);
	if (ldo == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	ldo->wm831x = wm831x;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No I/O resource\n");
		ret = -EINVAL;
		goto err;
	}
	ldo->base = res->start;

	snprintf(ldo->name, sizeof(ldo->name), "LDO%d", id + 1);
	ldo->desc.name = ldo->name;
	ldo->desc.id = id;
	ldo->desc.type = REGULATOR_VOLTAGE;
	ldo->desc.n_voltages = WM831X_ALIVE_LDO_MAX_SELECTOR + 1;
	ldo->desc.ops = &wm831x_alive_ldo_ops;
	ldo->desc.owner = THIS_MODULE;

	ldo->regulator = regulator_register(&ldo->desc, &pdev->dev,
					     pdata->ldo[id], ldo);
	if (IS_ERR(ldo->regulator)) {
		ret = PTR_ERR(ldo->regulator);
		dev_err(wm831x->dev, "Failed to register LDO%d: %d\n",
			id + 1, ret);
		goto err;
	}

	platform_set_drvdata(pdev, ldo);

	return 0;

err:
	kfree(ldo);
	return ret;
}

static __devexit int wm831x_alive_ldo_remove(struct platform_device *pdev)
{
	struct wm831x_ldo *ldo = platform_get_drvdata(pdev);

	regulator_unregister(ldo->regulator);
	kfree(ldo);

	return 0;
}

static __devexit int wm831x_alive_ldo_shutdown(struct platform_device *pdev)	/*ZMF*/
{
	//struct wm831x_ldo *ldo = platform_get_drvdata(pdev);
#if 0
	//close ldo in wm831x_last_deinit()
	struct regulator* ldo;

	//if (reboot_cmd_get())
	//	return 0;
	printk("%s\n", __FUNCTION__);

	ldo = regulator_get(NULL, "ldo1");
	regulator_disable(ldo);			
	regulator_put(ldo);
	
	ldo = regulator_get(NULL, "ldo2");
	regulator_disable(ldo);			
	regulator_put(ldo);
	
	ldo = regulator_get(NULL, "ldo3");
	regulator_disable(ldo);			
	regulator_put(ldo);
	
	ldo = regulator_get(NULL, "ldo4");
	//regulator_disable(ldo);	
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo5");
	regulator_disable(ldo);			
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo6");
	regulator_disable(ldo);			
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo7");
	regulator_disable(ldo);			
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo8");
	//regulator_disable(ldo);			
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo9");
	regulator_disable(ldo);			
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo10");
	regulator_disable(ldo);						
	regulator_put(ldo);
#endif
	return 0;
}

static struct platform_driver wm831x_alive_ldo_driver = {
	.probe = wm831x_alive_ldo_probe,
	.remove = __devexit_p(wm831x_alive_ldo_remove),
	.shutdown = __devexit_p(wm831x_alive_ldo_shutdown),
	.driver		= {
		.name	= "wm831x-alive-ldo",
		.owner	= THIS_MODULE,
	},
};

static int __init wm831x_ldo_init(void)
{
	int ret;
	printk("%s \n", __FUNCTION__);	
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
