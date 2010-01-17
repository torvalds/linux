/*
 * Backup battery driver for Wolfson Microelectronics wm831x PMICs
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/auxadc.h>
#include <linux/mfd/wm831x/pmu.h>
#include <linux/mfd/wm831x/pdata.h>

struct wm831x_backup {
	struct wm831x *wm831x;
	struct power_supply backup;
};

static int wm831x_backup_read_voltage(struct wm831x *wm831x,
				     enum wm831x_auxadc src,
				     union power_supply_propval *val)
{
	int ret;

	ret = wm831x_auxadc_read_uv(wm831x, src);
	if (ret >= 0)
		val->intval = ret;

	return ret;
}

/*********************************************************************
 *		Backup supply properties
 *********************************************************************/

static void wm831x_config_backup(struct wm831x *wm831x)
{
	struct wm831x_pdata *wm831x_pdata = wm831x->dev->platform_data;
	struct wm831x_backup_pdata *pdata;
	int ret, reg;

	if (!wm831x_pdata || !wm831x_pdata->backup) {
		dev_warn(wm831x->dev,
			 "No backup battery charger configuration\n");
		return;
	}

	pdata = wm831x_pdata->backup;

	reg = 0;

	if (pdata->charger_enable)
		reg |= WM831X_BKUP_CHG_ENA | WM831X_BKUP_BATT_DET_ENA;
	if (pdata->no_constant_voltage)
		reg |= WM831X_BKUP_CHG_MODE;

	switch (pdata->vlim) {
	case 2500:
		break;
	case 3100:
		reg |= WM831X_BKUP_CHG_VLIM;
		break;
	default:
		dev_err(wm831x->dev, "Invalid backup voltage limit %dmV\n",
			pdata->vlim);
	}

	switch (pdata->ilim) {
	case 100:
		break;
	case 200:
		reg |= 1;
		break;
	case 300:
		reg |= 2;
		break;
	case 400:
		reg |= 3;
		break;
	default:
		dev_err(wm831x->dev, "Invalid backup current limit %duA\n",
			pdata->ilim);
	}

	ret = wm831x_reg_unlock(wm831x);
	if (ret != 0) {
		dev_err(wm831x->dev, "Failed to unlock registers: %d\n", ret);
		return;
	}

	ret = wm831x_set_bits(wm831x, WM831X_BACKUP_CHARGER_CONTROL,
			      WM831X_BKUP_CHG_ENA_MASK |
			      WM831X_BKUP_CHG_MODE_MASK |
			      WM831X_BKUP_BATT_DET_ENA_MASK |
			      WM831X_BKUP_CHG_VLIM_MASK |
			      WM831X_BKUP_CHG_ILIM_MASK,
			      reg);
	if (ret != 0)
		dev_err(wm831x->dev,
			"Failed to set backup charger config: %d\n", ret);

	wm831x_reg_lock(wm831x);
}

static int wm831x_backup_get_prop(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct wm831x_backup *devdata = dev_get_drvdata(psy->dev->parent);
	struct wm831x *wm831x = devdata->wm831x;
	int ret = 0;

	ret = wm831x_reg_read(wm831x, WM831X_BACKUP_CHARGER_CONTROL);
	if (ret < 0)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (ret & WM831X_BKUP_CHG_STS)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = wm831x_backup_read_voltage(wm831x, WM831X_AUX_BKUP_BATT,
						val);
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		if (ret & WM831X_BKUP_CHG_STS)
			val->intval = 1;
		else
			val->intval = 0;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property wm831x_backup_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PRESENT,
};

/*********************************************************************
 *		Initialisation
 *********************************************************************/

static __devinit int wm831x_backup_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_backup *devdata;
	struct power_supply *backup;
	int ret;

	devdata = kzalloc(sizeof(struct wm831x_backup), GFP_KERNEL);
	if (devdata == NULL)
		return -ENOMEM;

	devdata->wm831x = wm831x;
	platform_set_drvdata(pdev, devdata);

	backup = &devdata->backup;

	/* We ignore configuration failures since we can still read
	 * back the status without enabling the charger (which may
	 * already be enabled anyway).
	 */
	wm831x_config_backup(wm831x);

	backup->name = "wm831x-backup";
	backup->type = POWER_SUPPLY_TYPE_BATTERY;
	backup->properties = wm831x_backup_props;
	backup->num_properties = ARRAY_SIZE(wm831x_backup_props);
	backup->get_property = wm831x_backup_get_prop;
	ret = power_supply_register(&pdev->dev, backup);
	if (ret)
		goto err_kmalloc;

	return ret;

err_kmalloc:
	kfree(devdata);
	return ret;
}

static __devexit int wm831x_backup_remove(struct platform_device *pdev)
{
	struct wm831x_backup *devdata = platform_get_drvdata(pdev);

	power_supply_unregister(&devdata->backup);
	kfree(devdata);

	return 0;
}

static struct platform_driver wm831x_backup_driver = {
	.probe = wm831x_backup_probe,
	.remove = __devexit_p(wm831x_backup_remove),
	.driver = {
		.name = "wm831x-backup",
	},
};

static int __init wm831x_backup_init(void)
{
	return platform_driver_register(&wm831x_backup_driver);
}
module_init(wm831x_backup_init);

static void __exit wm831x_backup_exit(void)
{
	platform_driver_unregister(&wm831x_backup_driver);
}
module_exit(wm831x_backup_exit);

MODULE_DESCRIPTION("Backup battery charger driver for WM831x PMICs");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-backup");
