// SPDX-License-Identifier: GPL-2.0-only
/*
 * Backup battery driver for Wolfson Microelectronics wm831x PMICs
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/auxadc.h>
#include <linux/mfd/wm831x/pmu.h>
#include <linux/mfd/wm831x/pdata.h>

struct wm831x_backup {
	struct wm831x *wm831x;
	struct power_supply *backup;
	struct power_supply_desc backup_desc;
	char name[20];
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
	struct wm831x_backup *devdata = dev_get_drvdata(psy->dev.parent);
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

static int wm831x_backup_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *wm831x_pdata = wm831x->dev->platform_data;
	struct wm831x_backup *devdata;

	devdata = devm_kzalloc(&pdev->dev, sizeof(struct wm831x_backup),
				GFP_KERNEL);
	if (devdata == NULL)
		return -ENOMEM;

	devdata->wm831x = wm831x;

	/* We ignore configuration failures since we can still read
	 * back the status without enabling the charger (which may
	 * already be enabled anyway).
	 */
	wm831x_config_backup(wm831x);

	if (wm831x_pdata && wm831x_pdata->wm831x_num)
		snprintf(devdata->name, sizeof(devdata->name),
			 "wm831x-backup.%d", wm831x_pdata->wm831x_num);
	else
		snprintf(devdata->name, sizeof(devdata->name),
			 "wm831x-backup");

	devdata->backup_desc.name = devdata->name;
	devdata->backup_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	devdata->backup_desc.properties = wm831x_backup_props;
	devdata->backup_desc.num_properties = ARRAY_SIZE(wm831x_backup_props);
	devdata->backup_desc.get_property = wm831x_backup_get_prop;
	devdata->backup = devm_power_supply_register(&pdev->dev,
						     &devdata->backup_desc, NULL);

	return PTR_ERR_OR_ZERO(devdata->backup);
}

static struct platform_driver wm831x_backup_driver = {
	.probe = wm831x_backup_probe,
	.driver = {
		.name = "wm831x-backup",
	},
};

module_platform_driver(wm831x_backup_driver);

MODULE_DESCRIPTION("Backup battery charger driver for WM831x PMICs");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-backup");
