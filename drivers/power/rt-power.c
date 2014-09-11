/*
 *  drivers/power/rt-power.c
 *  Driver for Richtek RT PMIC Power driver
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>

#include <linux/power/rt-power.h>

struct rt_power_info {
	struct device *dev;
	struct power_supply ac_psy;
	struct power_supply usb_psy;
	struct wake_lock usbdet_wakelock;
	struct delayed_work usbdet_work;
	int chg_volt;
	int acchg_icc;
	int usbtachg_icc;
	int usbchg_icc;
	unsigned char ac_online:1;
	unsigned char usbta_online:1;
	unsigned char usb_online:1;
	unsigned char suspend:1;
	unsigned char usbcnt;
};

#define RT_USBCNT_MAX 60

static char *rtpower_supply_list[] = {
	"battery",
};

static enum power_supply_property rtpower_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int rtpower_set_charger(struct rt_power_info *pi)
{
	struct power_supply *chg_psy;
	union power_supply_propval pval;
	int rc = 0, is_chg_on = 0;

	chg_psy = power_supply_get_by_name("rt-charger");
	if (chg_psy) {
		rc = chg_psy->get_property(chg_psy, POWER_SUPPLY_PROP_ONLINE,
					   &pval);
		if (rc < 0)
			dev_err(pi->dev, "get chg online prop fail\n");
		else
			is_chg_on = pval.intval;
		if (pi->ac_online) {
			pval.intval = pi->acchg_icc;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CURRENT_AVG,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set acchg aicr fail\n");
			pval.intval = 500;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CURRENT_NOW,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set acchg icc fail\n");
			pval.intval = POWER_SUPPLY_TYPE_MAINS;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CHARGE_NOW,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set charge cable fail\n");
			if (!is_chg_on) {
				pval.intval = pi->chg_volt;
				rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_VOLTAGE_NOW,
					&pval);
				if (rc < 0)
					dev_err(pi->dev,
						"set chg voltage fail\n");
				pval.intval = 1;
				rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_ONLINE,
					&pval);
				if (rc < 0)
					dev_err(pi->dev,
						"set charger online fail\n");
			}
			pval.intval = 1;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_PRESENT,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set charger present fail\n");
		} else if (pi->usbta_online) {
			pval.intval = pi->usbtachg_icc;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CURRENT_AVG,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set usbtachg aicr fail\n");
			pval.intval = 500;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CURRENT_NOW,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set usbtachg icc fail\n");
			pval.intval = POWER_SUPPLY_TYPE_USB_DCP;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CHARGE_NOW,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set charge cable fail\n");
			if (!is_chg_on) {
				pval.intval = pi->chg_volt;
				rc = chg_psy->set_property(chg_psy,
						POWER_SUPPLY_PROP_VOLTAGE_NOW,
						&pval);
				if (rc < 0)
					dev_err(pi->dev,
						"set chg voltage fail\n");
				pval.intval = 1;
				rc = chg_psy->set_property(chg_psy,
						POWER_SUPPLY_PROP_ONLINE,
						&pval);
				if (rc < 0)
					dev_err(pi->dev,
						"set charger online fail\n");
			}
			pval.intval = 1;
			rc = chg_psy->set_property(chg_psy,
						POWER_SUPPLY_PROP_PRESENT,
						&pval);
			if (rc < 0)
				dev_err(pi->dev, "set charger present fail\n");
		} else if (pi->usb_online) {
			pval.intval = pi->usbchg_icc;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CURRENT_AVG,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set usbchg aicr fail\n");
			pval.intval = 500;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CURRENT_NOW,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set usbchg icc fail\n");
			pval.intval = POWER_SUPPLY_TYPE_USB;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CHARGE_NOW,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set charge cable fail\n");
			if (!is_chg_on) {
				pval.intval = pi->chg_volt;
				rc = chg_psy->set_property(chg_psy,
						POWER_SUPPLY_PROP_VOLTAGE_NOW,
						&pval);
				if (rc < 0)
					dev_err(pi->dev,
						"set chg voltage fail\n");
				pval.intval = 1;
				rc = chg_psy->set_property(chg_psy,
						POWER_SUPPLY_PROP_ONLINE,
						&pval);
				if (rc < 0)
					dev_err(pi->dev,
						"set charger online fail\n");
			}
			pval.intval = 1;
			rc = chg_psy->set_property(chg_psy,
						   POWER_SUPPLY_PROP_PRESENT,
						   &pval);
			if (rc < 0)
				dev_err(pi->dev, "set charger present fail\n");
		} else {
			pval.intval = 0;
			rc = chg_psy->set_property(chg_psy,
						   POWER_SUPPLY_PROP_ONLINE,
						   &pval);
			if (rc < 0)
				dev_err(pi->dev, "set charger online fail\n");
			pval.intval = 0;
			rc = chg_psy->set_property(chg_psy,
						   POWER_SUPPLY_PROP_CHARGE_NOW,
						   &pval);
			if (rc < 0)
				dev_err(pi->dev, "set charge cable fail\n");
			pval.intval = pi->chg_volt;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_VOLTAGE_NOW,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set chg voltage fail\n");
			pval.intval = 500;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CURRENT_AVG,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set chg aicr fail\n");
			pval.intval = 500;
			rc = chg_psy->set_property(chg_psy,
					POWER_SUPPLY_PROP_CURRENT_NOW,
					&pval);
			if (rc < 0)
				dev_err(pi->dev, "set chg icc fail\n");
		}
		power_supply_changed(chg_psy);
	} else {
		rc = -EINVAL;
		dev_err(pi->dev, "cannot get rt-charger psy\n");
	}
	return rc;
}

static int rtpower_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct rt_power_info *pi = dev_get_drvdata(psy->dev->parent);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
			val->intval = (pi->ac_online
				       || pi->usbta_online) ? 1 : 0;
		else if (psy->type == POWER_SUPPLY_TYPE_USB)
			val->intval = pi->usb_online;
		else
			rc = -EINVAL;
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static int rtpower_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct rt_power_info *pi = dev_get_drvdata(psy->dev->parent);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS) {
			if (pi->ac_online != val->intval) {
				pi->ac_online = val->intval;
				rc = rtpower_set_charger(pi);
			}
		} else if (psy->type == POWER_SUPPLY_TYPE_USB) {
			if (pi->usb_online != val->intval) {
				pi->usb_online = val->intval;
				if (val->intval) {
					pi->usbcnt = 0;
					wake_lock(&pi->usbdet_wakelock);
					schedule_delayed_work(&pi->usbdet_work,
							      1 * HZ);
				} else {
					pi->usbcnt = RT_USBCNT_MAX;
					schedule_delayed_work(&pi->usbdet_work,
							      0);
					if (pi->usbta_online) {
						pi->usbta_online = 0;
						power_supply_changed
						    (&pi->ac_psy);
					}
				}
				rc = rtpower_set_charger(pi);
			}
		} else {
				rc = -EINVAL;
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

extern int dwc_otg_check_dpdm(bool wait);
static void usbdet_work_func(struct work_struct *work)
{
	struct rt_power_info *pi = container_of(work, struct rt_power_info, \
			usbdet_work.work);
	int usb_det = dwc_otg_check_dpdm(0);

	switch (usb_det) {
	case 2:
		dev_info(pi->dev, "usb ta checked\n");
		if (pi->usb_online) {
			pi->usbta_online = 1;
			rtpower_set_charger(pi);
			power_supply_changed(&pi->ac_psy);
		}
		pi->usbcnt = RT_USBCNT_MAX;
		break;
	case 1:
	case 0:
		dev_info(pi->dev, "normal usb\n");
		break;
	default:
		break;
	}
	if (pi->usbcnt < RT_USBCNT_MAX) {
		pi->usbcnt++;
		schedule_delayed_work(&pi->usbdet_work, 1*HZ);
	} else {
		wake_unlock(&pi->usbdet_wakelock);
	}
}

static int rt_power_probe(struct platform_device *pdev)
{
	struct rt_power_info *pi;
	struct rt_power_data *rt_power_pdata = pdev->dev.platform_data;
	int ret = 0;

	pi = devm_kzalloc(&pdev->dev, sizeof(*pi), GFP_KERNEL);
	if (!pi)
		return -ENOMEM;
	pi->dev = &pdev->dev;
	wake_lock_init(&pi->usbdet_wakelock, WAKE_LOCK_SUSPEND, "rt-usb-det");
	INIT_DELAYED_WORK(&pi->usbdet_work, usbdet_work_func);
	pi->chg_volt = rt_power_pdata->chg_volt;
	pi->acchg_icc = rt_power_pdata->acchg_icc;
	pi->usbtachg_icc = rt_power_pdata->usbtachg_icc;
	pi->usbchg_icc = rt_power_pdata->usbchg_icc;
	platform_set_drvdata(pdev, pi);

	/* ac power supply register*/
	pi->ac_psy.name = RT_AC_NAME;
	pi->ac_psy.type = POWER_SUPPLY_TYPE_MAINS;
	pi->ac_psy.supplied_to = rtpower_supply_list;
	pi->ac_psy.properties = rtpower_props;
	pi->ac_psy.num_properties = ARRAY_SIZE(rtpower_props);
	pi->ac_psy.get_property = rtpower_get_property;
	pi->ac_psy.set_property = rtpower_set_property;
	ret = power_supply_register(&pdev->dev, &pi->ac_psy);
	if (ret < 0) {
		dev_err(&pdev->dev, " create ac power supply fail\n");
		goto err_init;
	}
	/*usb power supply register*/
	pi->usb_psy.name = RT_USB_NAME;
	pi->usb_psy.type = POWER_SUPPLY_TYPE_USB;
	pi->usb_psy.supplied_to = rtpower_supply_list;
	pi->usb_psy.properties = rtpower_props;
	pi->usb_psy.num_properties = ARRAY_SIZE(rtpower_props);
	pi->usb_psy.get_property = rtpower_get_property;
	pi->usb_psy.set_property = rtpower_set_property;
	ret = power_supply_register(&pdev->dev, &pi->usb_psy);
	if (ret < 0) {
		dev_err(&pdev->dev, " create usb power supply fail\n");
		goto err_acpsy;
	}
	return 0;

err_acpsy:
	power_supply_unregister(&pi->ac_psy);
err_init:
	wake_lock_destroy(&pi->usbdet_wakelock);
	return ret;
}

static int rt_power_remove(struct platform_device *pdev)
{
	struct rt_power_info *pi = platform_get_drvdata(pdev);

	power_supply_unregister(&pi->usb_psy);
	power_supply_unregister(&pi->ac_psy);
	wake_lock_destroy(&pi->usbdet_wakelock);
	return 0;
}

static int rt_power_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rt_power_info *pi = platform_get_drvdata(pdev);

	pi->suspend = 1;
	return 0;
}

static int rt_power_resume(struct platform_device *pdev)
{
	struct rt_power_info *pi = platform_get_drvdata(pdev);

	pi->suspend = 0;
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{ .compatible = "rt,rt-power",},
	{},
};

static struct platform_driver rt_power_driver = {
	.driver = {
		.name = "rt-power",
		.owner = THIS_MODULE,
		.of_match_table = rt_match_table,
	},
	.probe = rt_power_probe,
	.remove = rt_power_remove,
	.suspend = rt_power_suspend,
	.resume = rt_power_resume,
};
static int rt_power_init(void)
{
	return platform_driver_register(&rt_power_driver);
}
subsys_initcall(rt_power_init);

static void rt_power_exit(void)
{
	platform_driver_unregister(&rt_power_driver);
}
module_exit(rt_power_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("RT Power driver");
MODULE_ALIAS("platform:rt-power");
MODULE_VERSION("1.0.0_G");
