/*
 * wm831x-dcdc.c  --  DC-DC buck convertor driver for the WM831x series
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
#include <linux/regulator/machine.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/regulator.h>
#include <linux/mfd/wm831x/pdata.h>

#define WM831X_BUCKV_MAX_SELECTOR 0x68
#define WM831X_BUCKP_MAX_SELECTOR 0x66

#define WM831X_DCDC_MODE_FAST    0
#define WM831X_DCDC_MODE_NORMAL  1
#define WM831X_DCDC_MODE_IDLE    2
#define WM831X_DCDC_MODE_STANDBY 3

#define WM831X_DCDC_MAX_NAME 9

/* Register offsets in control block */
#define WM831X_DCDC_CONTROL_1     0
#define WM831X_DCDC_CONTROL_2     1
#define WM831X_DCDC_ON_CONFIG     2
#define WM831X_DCDC_SLEEP_CONTROL 3
#define WM831X_DCDC_DVS_CONTROL   4

/*
 * Shared
 */

struct wm831x_dcdc {
	char name[WM831X_DCDC_MAX_NAME];
	char supply_name[WM831X_DCDC_MAX_NAME];
	struct regulator_desc desc;
	int base;
	struct wm831x *wm831x;
	struct regulator_dev *regulator;
	int dvs_gpio;
	int dvs_gpio_state;
	int on_vsel;
	int dvs_vsel;
};

static unsigned int wm831x_dcdc_get_mode(struct regulator_dev *rdev)

{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = dcdc->wm831x;
	u16 reg = dcdc->base + WM831X_DCDC_ON_CONFIG;
	int val;

	val = wm831x_reg_read(wm831x, reg);
	if (val < 0)
		return val;

	val = (val & WM831X_DC1_ON_MODE_MASK) >> WM831X_DC1_ON_MODE_SHIFT;

	switch (val) {
	case WM831X_DCDC_MODE_FAST:
		return REGULATOR_MODE_FAST;
	case WM831X_DCDC_MODE_NORMAL:
		return REGULATOR_MODE_NORMAL;
	case WM831X_DCDC_MODE_STANDBY:
		return REGULATOR_MODE_STANDBY;
	case WM831X_DCDC_MODE_IDLE:
		return REGULATOR_MODE_IDLE;
	default:
		BUG();
		return -EINVAL;
	}
}

static int wm831x_dcdc_set_mode_int(struct wm831x *wm831x, int reg,
				    unsigned int mode)
{
	int val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = WM831X_DCDC_MODE_FAST;
		break;
	case REGULATOR_MODE_NORMAL:
		val = WM831X_DCDC_MODE_NORMAL;
		break;
	case REGULATOR_MODE_STANDBY:
		val = WM831X_DCDC_MODE_STANDBY;
		break;
	case REGULATOR_MODE_IDLE:
		val = WM831X_DCDC_MODE_IDLE;
		break;
	default:
		return -EINVAL;
	}

	return wm831x_set_bits(wm831x, reg, WM831X_DC1_ON_MODE_MASK,
			       val << WM831X_DC1_ON_MODE_SHIFT);
}

static int wm831x_dcdc_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = dcdc->wm831x;
	u16 reg = dcdc->base + WM831X_DCDC_ON_CONFIG;

	return wm831x_dcdc_set_mode_int(wm831x, reg, mode);
}

static int wm831x_dcdc_set_suspend_mode(struct regulator_dev *rdev,
					unsigned int mode)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = dcdc->wm831x;
	u16 reg = dcdc->base + WM831X_DCDC_SLEEP_CONTROL;

	return wm831x_dcdc_set_mode_int(wm831x, reg, mode);
}

static int wm831x_dcdc_get_status(struct regulator_dev *rdev)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = dcdc->wm831x;
	int ret;

	/* First, check for errors */
	ret = wm831x_reg_read(wm831x, WM831X_DCDC_UV_STATUS);
	if (ret < 0)
		return ret;

	if (ret & (1 << rdev_get_id(rdev))) {
		dev_dbg(wm831x->dev, "DCDC%d under voltage\n",
			rdev_get_id(rdev) + 1);
		return REGULATOR_STATUS_ERROR;
	}

	/* DCDC1 and DCDC2 can additionally detect high voltage/current */
	if (rdev_get_id(rdev) < 2) {
		if (ret & (WM831X_DC1_OV_STS << rdev_get_id(rdev))) {
			dev_dbg(wm831x->dev, "DCDC%d over voltage\n",
				rdev_get_id(rdev) + 1);
			return REGULATOR_STATUS_ERROR;
		}

		if (ret & (WM831X_DC1_HC_STS << rdev_get_id(rdev))) {
			dev_dbg(wm831x->dev, "DCDC%d over current\n",
				rdev_get_id(rdev) + 1);
			return REGULATOR_STATUS_ERROR;
		}
	}

	/* Is the regulator on? */
	ret = wm831x_reg_read(wm831x, WM831X_DCDC_STATUS);
	if (ret < 0)
		return ret;
	if (!(ret & (1 << rdev_get_id(rdev))))
		return REGULATOR_STATUS_OFF;

	/* TODO: When we handle hardware control modes so we can report the
	 * current mode. */
	return REGULATOR_STATUS_ON;
}

static irqreturn_t wm831x_dcdc_uv_irq(int irq, void *data)
{
	struct wm831x_dcdc *dcdc = data;

	regulator_notifier_call_chain(dcdc->regulator,
				      REGULATOR_EVENT_UNDER_VOLTAGE,
				      NULL);

	return IRQ_HANDLED;
}

static irqreturn_t wm831x_dcdc_oc_irq(int irq, void *data)
{
	struct wm831x_dcdc *dcdc = data;

	regulator_notifier_call_chain(dcdc->regulator,
				      REGULATOR_EVENT_OVER_CURRENT,
				      NULL);

	return IRQ_HANDLED;
}

/*
 * BUCKV specifics
 */

static int wm831x_buckv_list_voltage(struct regulator_dev *rdev,
				      unsigned selector)
{
	if (selector <= 0x8)
		return 600000;
	if (selector <= WM831X_BUCKV_MAX_SELECTOR)
		return 600000 + ((selector - 0x8) * 12500);
	return -EINVAL;
}

static int wm831x_buckv_map_voltage(struct regulator_dev *rdev,
				   int min_uV, int max_uV)
{
	u16 vsel;

	if (min_uV < 600000)
		vsel = 0;
	else if (min_uV <= 1800000)
		vsel = ((min_uV - 600000) / 12500) + 8;
	else
		return -EINVAL;

	if (wm831x_buckv_list_voltage(rdev, vsel) > max_uV)
		return -EINVAL;

	return vsel;
}

static int wm831x_buckv_set_dvs(struct regulator_dev *rdev, int state)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);

	if (state == dcdc->dvs_gpio_state)
		return 0;

	dcdc->dvs_gpio_state = state;
	gpio_set_value(dcdc->dvs_gpio, state);

	/* Should wait for DVS state change to be asserted if we have
	 * a GPIO for it, for now assume the device is configured
	 * for the fastest possible transition.
	 */

	return 0;
}

static int wm831x_buckv_set_voltage_sel(struct regulator_dev *rdev,
					unsigned vsel)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = dcdc->wm831x;
	int on_reg = dcdc->base + WM831X_DCDC_ON_CONFIG;
	int dvs_reg = dcdc->base + WM831X_DCDC_DVS_CONTROL;
	int ret;

	/* If this value is already set then do a GPIO update if we can */
	if (dcdc->dvs_gpio && dcdc->on_vsel == vsel)
		return wm831x_buckv_set_dvs(rdev, 0);

	if (dcdc->dvs_gpio && dcdc->dvs_vsel == vsel)
		return wm831x_buckv_set_dvs(rdev, 1);

	/* Always set the ON status to the minimum voltage */
	ret = wm831x_set_bits(wm831x, on_reg, WM831X_DC1_ON_VSEL_MASK, vsel);
	if (ret < 0)
		return ret;
	dcdc->on_vsel = vsel;

	if (!dcdc->dvs_gpio)
		return ret;

	/* Kick the voltage transition now */
	ret = wm831x_buckv_set_dvs(rdev, 0);
	if (ret < 0)
		return ret;

	/*
	 * If this VSEL is higher than the last one we've seen then
	 * remember it as the DVS VSEL.  This is optimised for CPUfreq
	 * usage where we want to get to the highest voltage very
	 * quickly.
	 */
	if (vsel > dcdc->dvs_vsel) {
		ret = wm831x_set_bits(wm831x, dvs_reg,
				      WM831X_DC1_DVS_VSEL_MASK,
				      dcdc->dvs_vsel);
		if (ret == 0)
			dcdc->dvs_vsel = vsel;
		else
			dev_warn(wm831x->dev,
				 "Failed to set DCDC DVS VSEL: %d\n", ret);
	}

	return 0;
}

static int wm831x_buckv_set_suspend_voltage(struct regulator_dev *rdev,
					    int uV)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = dcdc->wm831x;
	u16 reg = dcdc->base + WM831X_DCDC_SLEEP_CONTROL;
	int vsel;

	vsel = wm831x_buckv_map_voltage(rdev, uV, uV);
	if (vsel < 0)
		return vsel;

	return wm831x_set_bits(wm831x, reg, WM831X_DC1_SLP_VSEL_MASK, vsel);
}

static int wm831x_buckv_get_voltage_sel(struct regulator_dev *rdev)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);

	if (dcdc->dvs_gpio && dcdc->dvs_gpio_state)
		return dcdc->dvs_vsel;
	else
		return dcdc->on_vsel;
}

/* Current limit options */
static u16 wm831x_dcdc_ilim[] = {
	125, 250, 375, 500, 625, 750, 875, 1000
};

static int wm831x_buckv_set_current_limit(struct regulator_dev *rdev,
					   int min_uA, int max_uA)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = dcdc->wm831x;
	u16 reg = dcdc->base + WM831X_DCDC_CONTROL_2;
	int i;

	for (i = ARRAY_SIZE(wm831x_dcdc_ilim) - 1; i >= 0; i--) {
		if ((min_uA <= wm831x_dcdc_ilim[i]) &&
		    (wm831x_dcdc_ilim[i] <= max_uA))
			return wm831x_set_bits(wm831x, reg,
					       WM831X_DC1_HC_THR_MASK,
						i << WM831X_DC1_HC_THR_SHIFT);
	}

	return -EINVAL;
}

static int wm831x_buckv_get_current_limit(struct regulator_dev *rdev)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = dcdc->wm831x;
	u16 reg = dcdc->base + WM831X_DCDC_CONTROL_2;
	int val;

	val = wm831x_reg_read(wm831x, reg);
	if (val < 0)
		return val;

	val = (val & WM831X_DC1_HC_THR_MASK) >> WM831X_DC1_HC_THR_SHIFT;
	return wm831x_dcdc_ilim[val];
}

static struct regulator_ops wm831x_buckv_ops = {
	.set_voltage_sel = wm831x_buckv_set_voltage_sel,
	.get_voltage_sel = wm831x_buckv_get_voltage_sel,
	.list_voltage = wm831x_buckv_list_voltage,
	.map_voltage = wm831x_buckv_map_voltage,
	.set_suspend_voltage = wm831x_buckv_set_suspend_voltage,
	.set_current_limit = wm831x_buckv_set_current_limit,
	.get_current_limit = wm831x_buckv_get_current_limit,

	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.get_status = wm831x_dcdc_get_status,
	.get_mode = wm831x_dcdc_get_mode,
	.set_mode = wm831x_dcdc_set_mode,
	.set_suspend_mode = wm831x_dcdc_set_suspend_mode,
};

/*
 * Set up DVS control.  We just log errors since we can still run
 * (with reduced performance) if we fail.
 */
static void wm831x_buckv_dvs_init(struct wm831x_dcdc *dcdc,
					    struct wm831x_buckv_pdata *pdata)
{
	struct wm831x *wm831x = dcdc->wm831x;
	int ret;
	u16 ctrl;

	if (!pdata || !pdata->dvs_gpio)
		return;

	/* gpiolib won't let us read the GPIO status so pick the higher
	 * of the two existing voltages so we take it as platform data.
	 */
	dcdc->dvs_gpio_state = pdata->dvs_init_state;

	ret = gpio_request_one(pdata->dvs_gpio,
			       dcdc->dvs_gpio_state ? GPIOF_INIT_HIGH : 0,
			       "DCDC DVS");
	if (ret < 0) {
		dev_err(wm831x->dev, "Failed to get %s DVS GPIO: %d\n",
			dcdc->name, ret);
		return;
	}

	dcdc->dvs_gpio = pdata->dvs_gpio;

	switch (pdata->dvs_control_src) {
	case 1:
		ctrl = 2 << WM831X_DC1_DVS_SRC_SHIFT;
		break;
	case 2:
		ctrl = 3 << WM831X_DC1_DVS_SRC_SHIFT;
		break;
	default:
		dev_err(wm831x->dev, "Invalid DVS control source %d for %s\n",
			pdata->dvs_control_src, dcdc->name);
		return;
	}

	/* If DVS_VSEL is set to the minimum value then raise it to ON_VSEL
	 * to make bootstrapping a bit smoother.
	 */
	if (!dcdc->dvs_vsel) {
		ret = wm831x_set_bits(wm831x,
				      dcdc->base + WM831X_DCDC_DVS_CONTROL,
				      WM831X_DC1_DVS_VSEL_MASK, dcdc->on_vsel);
		if (ret == 0)
			dcdc->dvs_vsel = dcdc->on_vsel;
		else
			dev_warn(wm831x->dev, "Failed to set DVS_VSEL: %d\n",
				 ret);
	}

	ret = wm831x_set_bits(wm831x, dcdc->base + WM831X_DCDC_DVS_CONTROL,
			      WM831X_DC1_DVS_SRC_MASK, ctrl);
	if (ret < 0) {
		dev_err(wm831x->dev, "Failed to set %s DVS source: %d\n",
			dcdc->name, ret);
	}
}

static int wm831x_buckv_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	struct regulator_config config = { };
	int id;
	struct wm831x_dcdc *dcdc;
	struct resource *res;
	int ret, irq;

	if (pdata && pdata->wm831x_num)
		id = (pdata->wm831x_num * 10) + 1;
	else
		id = 0;
	id = pdev->id - id;

	dev_dbg(&pdev->dev, "Probing DCDC%d\n", id + 1);

	dcdc = devm_kzalloc(&pdev->dev,  sizeof(struct wm831x_dcdc),
			    GFP_KERNEL);
	if (dcdc == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	dcdc->wm831x = wm831x;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No REG resource\n");
		ret = -EINVAL;
		goto err;
	}
	dcdc->base = res->start;

	snprintf(dcdc->name, sizeof(dcdc->name), "DCDC%d", id + 1);
	dcdc->desc.name = dcdc->name;

	snprintf(dcdc->supply_name, sizeof(dcdc->supply_name),
		 "DC%dVDD", id + 1);
	dcdc->desc.supply_name = dcdc->supply_name;

	dcdc->desc.id = id;
	dcdc->desc.type = REGULATOR_VOLTAGE;
	dcdc->desc.n_voltages = WM831X_BUCKV_MAX_SELECTOR + 1;
	dcdc->desc.ops = &wm831x_buckv_ops;
	dcdc->desc.owner = THIS_MODULE;
	dcdc->desc.enable_reg = WM831X_DCDC_ENABLE;
	dcdc->desc.enable_mask = 1 << id;

	ret = wm831x_reg_read(wm831x, dcdc->base + WM831X_DCDC_ON_CONFIG);
	if (ret < 0) {
		dev_err(wm831x->dev, "Failed to read ON VSEL: %d\n", ret);
		goto err;
	}
	dcdc->on_vsel = ret & WM831X_DC1_ON_VSEL_MASK;

	ret = wm831x_reg_read(wm831x, dcdc->base + WM831X_DCDC_DVS_CONTROL);
	if (ret < 0) {
		dev_err(wm831x->dev, "Failed to read DVS VSEL: %d\n", ret);
		goto err;
	}
	dcdc->dvs_vsel = ret & WM831X_DC1_DVS_VSEL_MASK;

	if (pdata && pdata->dcdc[id])
		wm831x_buckv_dvs_init(dcdc, pdata->dcdc[id]->driver_data);

	config.dev = pdev->dev.parent;
	if (pdata)
		config.init_data = pdata->dcdc[id];
	config.driver_data = dcdc;
	config.regmap = wm831x->regmap;

	dcdc->regulator = regulator_register(&dcdc->desc, &config);
	if (IS_ERR(dcdc->regulator)) {
		ret = PTR_ERR(dcdc->regulator);
		dev_err(wm831x->dev, "Failed to register DCDC%d: %d\n",
			id + 1, ret);
		goto err;
	}

	irq = wm831x_irq(wm831x, platform_get_irq_byname(pdev, "UV"));
	ret = request_threaded_irq(irq, NULL, wm831x_dcdc_uv_irq,
				   IRQF_TRIGGER_RISING, dcdc->name, dcdc);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request UV IRQ %d: %d\n",
			irq, ret);
		goto err_regulator;
	}

	irq = wm831x_irq(wm831x, platform_get_irq_byname(pdev, "HC"));
	ret = request_threaded_irq(irq, NULL, wm831x_dcdc_oc_irq,
				   IRQF_TRIGGER_RISING, dcdc->name, dcdc);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request HC IRQ %d: %d\n",
			irq, ret);
		goto err_uv;
	}

	platform_set_drvdata(pdev, dcdc);

	return 0;

err_uv:
	free_irq(wm831x_irq(wm831x, platform_get_irq_byname(pdev, "UV")),
		 dcdc);
err_regulator:
	regulator_unregister(dcdc->regulator);
err:
	if (dcdc->dvs_gpio)
		gpio_free(dcdc->dvs_gpio);
	return ret;
}

static int wm831x_buckv_remove(struct platform_device *pdev)
{
	struct wm831x_dcdc *dcdc = platform_get_drvdata(pdev);
	struct wm831x *wm831x = dcdc->wm831x;

	platform_set_drvdata(pdev, NULL);

	free_irq(wm831x_irq(wm831x, platform_get_irq_byname(pdev, "HC")),
			    dcdc);
	free_irq(wm831x_irq(wm831x, platform_get_irq_byname(pdev, "UV")),
			    dcdc);
	regulator_unregister(dcdc->regulator);
	if (dcdc->dvs_gpio)
		gpio_free(dcdc->dvs_gpio);

	return 0;
}

static struct platform_driver wm831x_buckv_driver = {
	.probe = wm831x_buckv_probe,
	.remove = wm831x_buckv_remove,
	.driver		= {
		.name	= "wm831x-buckv",
		.owner	= THIS_MODULE,
	},
};

/*
 * BUCKP specifics
 */

static int wm831x_buckp_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = dcdc->wm831x;
	u16 reg = dcdc->base + WM831X_DCDC_SLEEP_CONTROL;
	int sel;

	sel = regulator_map_voltage_linear(rdev, uV, uV);
	if (sel < 0)
		return sel;

	return wm831x_set_bits(wm831x, reg, WM831X_DC3_ON_VSEL_MASK, sel);
}

static struct regulator_ops wm831x_buckp_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_suspend_voltage = wm831x_buckp_set_suspend_voltage,

	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.get_status = wm831x_dcdc_get_status,
	.get_mode = wm831x_dcdc_get_mode,
	.set_mode = wm831x_dcdc_set_mode,
	.set_suspend_mode = wm831x_dcdc_set_suspend_mode,
};

static int wm831x_buckp_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	struct regulator_config config = { };
	int id;
	struct wm831x_dcdc *dcdc;
	struct resource *res;
	int ret, irq;

	if (pdata && pdata->wm831x_num)
		id = (pdata->wm831x_num * 10) + 1;
	else
		id = 0;
	id = pdev->id - id;

	dev_dbg(&pdev->dev, "Probing DCDC%d\n", id + 1);

	dcdc = devm_kzalloc(&pdev->dev, sizeof(struct wm831x_dcdc),
			    GFP_KERNEL);
	if (dcdc == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	dcdc->wm831x = wm831x;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No REG resource\n");
		ret = -EINVAL;
		goto err;
	}
	dcdc->base = res->start;

	snprintf(dcdc->name, sizeof(dcdc->name), "DCDC%d", id + 1);
	dcdc->desc.name = dcdc->name;

	snprintf(dcdc->supply_name, sizeof(dcdc->supply_name),
		 "DC%dVDD", id + 1);
	dcdc->desc.supply_name = dcdc->supply_name;

	dcdc->desc.id = id;
	dcdc->desc.type = REGULATOR_VOLTAGE;
	dcdc->desc.n_voltages = WM831X_BUCKP_MAX_SELECTOR + 1;
	dcdc->desc.ops = &wm831x_buckp_ops;
	dcdc->desc.owner = THIS_MODULE;
	dcdc->desc.vsel_reg = dcdc->base + WM831X_DCDC_ON_CONFIG;
	dcdc->desc.vsel_mask = WM831X_DC3_ON_VSEL_MASK;
	dcdc->desc.enable_reg = WM831X_DCDC_ENABLE;
	dcdc->desc.enable_mask = 1 << id;
	dcdc->desc.min_uV = 850000;
	dcdc->desc.uV_step = 25000;

	config.dev = pdev->dev.parent;
	if (pdata)
		config.init_data = pdata->dcdc[id];
	config.driver_data = dcdc;
	config.regmap = wm831x->regmap;

	dcdc->regulator = regulator_register(&dcdc->desc, &config);
	if (IS_ERR(dcdc->regulator)) {
		ret = PTR_ERR(dcdc->regulator);
		dev_err(wm831x->dev, "Failed to register DCDC%d: %d\n",
			id + 1, ret);
		goto err;
	}

	irq = wm831x_irq(wm831x, platform_get_irq_byname(pdev, "UV"));
	ret = request_threaded_irq(irq, NULL, wm831x_dcdc_uv_irq,
				   IRQF_TRIGGER_RISING,	dcdc->name, dcdc);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request UV IRQ %d: %d\n",
			irq, ret);
		goto err_regulator;
	}

	platform_set_drvdata(pdev, dcdc);

	return 0;

err_regulator:
	regulator_unregister(dcdc->regulator);
err:
	return ret;
}

static int wm831x_buckp_remove(struct platform_device *pdev)
{
	struct wm831x_dcdc *dcdc = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	free_irq(wm831x_irq(dcdc->wm831x, platform_get_irq_byname(pdev, "UV")),
			    dcdc);
	regulator_unregister(dcdc->regulator);

	return 0;
}

static struct platform_driver wm831x_buckp_driver = {
	.probe = wm831x_buckp_probe,
	.remove = wm831x_buckp_remove,
	.driver		= {
		.name	= "wm831x-buckp",
		.owner	= THIS_MODULE,
	},
};

/*
 * DCDC boost convertors
 */

static int wm831x_boostp_get_status(struct regulator_dev *rdev)
{
	struct wm831x_dcdc *dcdc = rdev_get_drvdata(rdev);
	struct wm831x *wm831x = dcdc->wm831x;
	int ret;

	/* First, check for errors */
	ret = wm831x_reg_read(wm831x, WM831X_DCDC_UV_STATUS);
	if (ret < 0)
		return ret;

	if (ret & (1 << rdev_get_id(rdev))) {
		dev_dbg(wm831x->dev, "DCDC%d under voltage\n",
			rdev_get_id(rdev) + 1);
		return REGULATOR_STATUS_ERROR;
	}

	/* Is the regulator on? */
	ret = wm831x_reg_read(wm831x, WM831X_DCDC_STATUS);
	if (ret < 0)
		return ret;
	if (ret & (1 << rdev_get_id(rdev)))
		return REGULATOR_STATUS_ON;
	else
		return REGULATOR_STATUS_OFF;
}

static struct regulator_ops wm831x_boostp_ops = {
	.get_status = wm831x_boostp_get_status,

	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
};

static int wm831x_boostp_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	struct regulator_config config = { };
	int id = pdev->id % ARRAY_SIZE(pdata->dcdc);
	struct wm831x_dcdc *dcdc;
	struct resource *res;
	int ret, irq;

	dev_dbg(&pdev->dev, "Probing DCDC%d\n", id + 1);

	if (pdata == NULL || pdata->dcdc[id] == NULL)
		return -ENODEV;

	dcdc = devm_kzalloc(&pdev->dev, sizeof(struct wm831x_dcdc), GFP_KERNEL);
	if (dcdc == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	dcdc->wm831x = wm831x;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No REG resource\n");
		ret = -EINVAL;
		goto err;
	}
	dcdc->base = res->start;

	snprintf(dcdc->name, sizeof(dcdc->name), "DCDC%d", id + 1);
	dcdc->desc.name = dcdc->name;
	dcdc->desc.id = id;
	dcdc->desc.type = REGULATOR_VOLTAGE;
	dcdc->desc.ops = &wm831x_boostp_ops;
	dcdc->desc.owner = THIS_MODULE;
	dcdc->desc.enable_reg = WM831X_DCDC_ENABLE;
	dcdc->desc.enable_mask = 1 << id;

	config.dev = pdev->dev.parent;
	if (pdata)
		config.init_data = pdata->dcdc[id];
	config.driver_data = dcdc;
	config.regmap = wm831x->regmap;

	dcdc->regulator = regulator_register(&dcdc->desc, &config);
	if (IS_ERR(dcdc->regulator)) {
		ret = PTR_ERR(dcdc->regulator);
		dev_err(wm831x->dev, "Failed to register DCDC%d: %d\n",
			id + 1, ret);
		goto err;
	}

	irq = wm831x_irq(wm831x, platform_get_irq_byname(pdev, "UV"));
	ret = request_threaded_irq(irq, NULL, wm831x_dcdc_uv_irq,
				   IRQF_TRIGGER_RISING, dcdc->name,
				   dcdc);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request UV IRQ %d: %d\n",
			irq, ret);
		goto err_regulator;
	}

	platform_set_drvdata(pdev, dcdc);

	return 0;

err_regulator:
	regulator_unregister(dcdc->regulator);
err:
	return ret;
}

static int wm831x_boostp_remove(struct platform_device *pdev)
{
	struct wm831x_dcdc *dcdc = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	free_irq(wm831x_irq(dcdc->wm831x, platform_get_irq_byname(pdev, "UV")),
		 dcdc);
	regulator_unregister(dcdc->regulator);

	return 0;
}

static struct platform_driver wm831x_boostp_driver = {
	.probe = wm831x_boostp_probe,
	.remove = wm831x_boostp_remove,
	.driver		= {
		.name	= "wm831x-boostp",
		.owner	= THIS_MODULE,
	},
};

/*
 * External Power Enable
 *
 * These aren't actually DCDCs but look like them in hardware so share
 * code.
 */

#define WM831X_EPE_BASE 6

static struct regulator_ops wm831x_epe_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.get_status = wm831x_dcdc_get_status,
};

static int wm831x_epe_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	struct regulator_config config = { };
	int id = pdev->id % ARRAY_SIZE(pdata->epe);
	struct wm831x_dcdc *dcdc;
	int ret;

	dev_dbg(&pdev->dev, "Probing EPE%d\n", id + 1);

	dcdc = devm_kzalloc(&pdev->dev, sizeof(struct wm831x_dcdc), GFP_KERNEL);
	if (dcdc == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	dcdc->wm831x = wm831x;

	/* For current parts this is correct; probably need to revisit
	 * in future.
	 */
	snprintf(dcdc->name, sizeof(dcdc->name), "EPE%d", id + 1);
	dcdc->desc.name = dcdc->name;
	dcdc->desc.id = id + WM831X_EPE_BASE; /* Offset in DCDC registers */
	dcdc->desc.ops = &wm831x_epe_ops;
	dcdc->desc.type = REGULATOR_VOLTAGE;
	dcdc->desc.owner = THIS_MODULE;
	dcdc->desc.enable_reg = WM831X_DCDC_ENABLE;
	dcdc->desc.enable_mask = 1 << dcdc->desc.id;

	config.dev = pdev->dev.parent;
	if (pdata)
		config.init_data = pdata->epe[id];
	config.driver_data = dcdc;
	config.regmap = wm831x->regmap;

	dcdc->regulator = regulator_register(&dcdc->desc, &config);
	if (IS_ERR(dcdc->regulator)) {
		ret = PTR_ERR(dcdc->regulator);
		dev_err(wm831x->dev, "Failed to register EPE%d: %d\n",
			id + 1, ret);
		goto err;
	}

	platform_set_drvdata(pdev, dcdc);

	return 0;

err:
	return ret;
}

static int wm831x_epe_remove(struct platform_device *pdev)
{
	struct wm831x_dcdc *dcdc = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	regulator_unregister(dcdc->regulator);

	return 0;
}

static struct platform_driver wm831x_epe_driver = {
	.probe = wm831x_epe_probe,
	.remove = wm831x_epe_remove,
	.driver		= {
		.name	= "wm831x-epe",
		.owner	= THIS_MODULE,
	},
};

static int __init wm831x_dcdc_init(void)
{
	int ret;
	ret = platform_driver_register(&wm831x_buckv_driver);
	if (ret != 0)
		pr_err("Failed to register WM831x BUCKV driver: %d\n", ret);

	ret = platform_driver_register(&wm831x_buckp_driver);
	if (ret != 0)
		pr_err("Failed to register WM831x BUCKP driver: %d\n", ret);

	ret = platform_driver_register(&wm831x_boostp_driver);
	if (ret != 0)
		pr_err("Failed to register WM831x BOOST driver: %d\n", ret);

	ret = platform_driver_register(&wm831x_epe_driver);
	if (ret != 0)
		pr_err("Failed to register WM831x EPE driver: %d\n", ret);

	return 0;
}
subsys_initcall(wm831x_dcdc_init);

static void __exit wm831x_dcdc_exit(void)
{
	platform_driver_unregister(&wm831x_epe_driver);
	platform_driver_unregister(&wm831x_boostp_driver);
	platform_driver_unregister(&wm831x_buckp_driver);
	platform_driver_unregister(&wm831x_buckv_driver);
}
module_exit(wm831x_dcdc_exit);

/* Module information */
MODULE_AUTHOR("Mark Brown");
MODULE_DESCRIPTION("WM831x DC-DC convertor driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-buckv");
MODULE_ALIAS("platform:wm831x-buckp");
MODULE_ALIAS("platform:wm831x-epe");
