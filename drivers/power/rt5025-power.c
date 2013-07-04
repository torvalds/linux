/* drivers/power/rt5025-power.c
 * I2C Driver for Richtek RT5025 PMIC
 * Multi function device - multi functional baseband PMIC Power part
 *
 * Copyright (C) 2013
 * Author: CY Huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/mfd/rt5025.h>
#include <linux/power/rt5025-power.h>
#include <linux/power/rt5025-gauge.h>
#include <linux/delay.h>


static enum power_supply_property rt5025_adap_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static char *rt5025_supply_list[] = {
	"rt5025-battery",
};

static int rt5025_set_charging_current_switch (struct i2c_client *i2c, int onoff)
{
	int ret;
	if (onoff)
		ret = rt5025_set_bits(i2c, RT5025_REG_CHGCTL7, RT5025_CHGCEN_MASK);
	else
		ret = rt5025_clr_bits(i2c, RT5025_REG_CHGCTL7, RT5025_CHGCEN_MASK);
	return ret;
}

static int rt5025_set_charging_buck(struct i2c_client *i2c, int onoff)
{
	int ret;
	if (onoff)
		ret = rt5025_set_bits(i2c, RT5025_REG_CHGCTL2, RT5025_CHGBUCKEN_MASK);
	else
		ret = rt5025_clr_bits(i2c, RT5025_REG_CHGCTL2, RT5025_CHGBUCKEN_MASK);
	return ret;
}

static int rt5025_chgstat_changed(struct rt5025_power_info *info, unsigned new_val)
{
	int ret = 0;
	switch (new_val)
	{
		case 0x00:
			rt5025_set_charging_current_switch(info->i2c, 1);
			rt5025_set_charging_buck(info->i2c, 1);
			info->chg_stat = 0x00;
			if (info->event_callback)
				info->event_callback->rt5025_gauge_set_status(POWER_SUPPLY_STATUS_CHARGING);
			break;
		case 0x01:
			//rt5025_set_charging_current_switch(info->i2c, 1);
			info->chg_stat = 0x01;
			if (info->event_callback)
				info->event_callback->rt5025_gauge_set_status(POWER_SUPPLY_STATUS_CHARGING);
			break;
		case 0x02:
			rt5025_set_charging_current_switch(info->i2c, 0);
			info->chg_stat = 0x02;
			if (info->event_callback)
				info->event_callback->rt5025_gauge_set_status(POWER_SUPPLY_STATUS_FULL);
			break;
		case 0x03:
			rt5025_set_charging_buck(info->i2c, 0);
			rt5025_set_charging_current_switch(info->i2c, 0);
			info->chg_stat = 0x03;
			if (info->event_callback)
				info->event_callback->rt5025_gauge_set_status(POWER_SUPPLY_STATUS_DISCHARGING);
			break;
		default:
			break;
	}
	return ret;
}

int rt5025_power_passirq_to_gauge(struct rt5025_power_info *info)
{
	if (info->event_callback)
		info->event_callback->rt5025_gauge_irq_handler();
	return 0;
}
EXPORT_SYMBOL(rt5025_power_passirq_to_gauge);

int rt5025_power_charge_detect(struct rt5025_power_info *info)
{
	int ret = 0;
	unsigned char chgstatval = 0;
	unsigned old_usbval, old_acval, old_chgval, new_usbval, new_acval, new_chgval;

	old_acval = info->ac_online;
	old_usbval = info->usb_online;
	old_chgval = info->chg_stat;
	
	ret = rt5025_reg_read(info->i2c, RT5025_REG_CHGSTAT);
	if (ret<0)
	{
		dev_err(info->dev, "read chg stat reg fail\n");
		return ret;
	}
	chgstatval = ret;

	new_acval = (chgstatval&RT5025_CHG_ACONLINE)>>RT5025_CHG_ACSHIFT;
	if (old_acval != new_acval)
	{
		info->ac_online = new_acval;
		power_supply_changed(&info->ac);
	}
	new_usbval = (chgstatval&RT5025_CHG_USBONLINE)>>RT5025_CHG_USBSHIFT;
	if (old_usbval != new_usbval)
	{
		info->usb_online = new_usbval;
		power_supply_changed(&info->usb);
	}

	new_chgval = (chgstatval&RT5025_CHGSTAT_MASK)>>RT5025_CHGSTAT_SHIFT;
	if (new_acval || new_usbval)
	{
		if (old_chgval != new_chgval)
		{
			ret = rt5025_chgstat_changed(info, new_chgval);
		}
	}
	else
	{
		rt5025_set_charging_buck(info->i2c, 0);
		rt5025_set_charging_current_switch(info->i2c, 0);
		info->chg_stat = RT5025_CHGSTAT_UNKNOWN;
		if (info->event_callback)
			info->event_callback->rt5025_gauge_set_status(POWER_SUPPLY_STATUS_NOT_CHARGING);
	}

	return ret;
}
EXPORT_SYMBOL(rt5025_power_charge_detect);

static int rt5025_adap_get_props(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct rt5025_power_info *info = dev_get_drvdata(psy->dev->parent);
	switch(psp)
	{
		case POWER_SUPPLY_PROP_ONLINE:
			if (psy->type == POWER_SUPPLY_TYPE_MAINS)
				val->intval = info->ac_online;
			else if (psy->type == POWER_SUPPLY_TYPE_USB)
				val->intval = info->usb_online;
			else
				return -EINVAL;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static int __devinit rt5025_init_charger(struct rt5025_power_info *info, struct rt5025_power_data* pd)
{
	info->ac_online = 0;
	info->usb_online =0;
	//init charger buckck & charger current en to disable stat
	info->chg_stat = RT5025_CHGSTAT_UNKNOWN;
	if (info->event_callback)
		info->event_callback->rt5025_gauge_set_status(POWER_SUPPLY_STATUS_DISCHARGING);
	rt5025_set_bits(info->i2c, RT5025_REG_CHGCTL4, RT5025_CHGRST_MASK);
	udelay(200);
	//init register setting
	rt5025_reg_write(info->i2c, RT5025_REG_CHGCTL2, pd->CHGControl2.val);
	rt5025_reg_write(info->i2c, RT5025_REG_CHGCTL3, pd->CHGControl3.val);
	rt5025_reg_write(info->i2c, RT5025_REG_CHGCTL4, pd->CHGControl4.val);
	rt5025_reg_write(info->i2c, RT5025_REG_CHGCTL5, pd->CHGControl5.val);
	rt5025_reg_write(info->i2c, RT5025_REG_CHGCTL6, pd->CHGControl6.val);
	rt5025_reg_write(info->i2c, RT5025_REG_CHGCTL7, pd->CHGControl7.val);
	
	rt5025_power_charge_detect(info);

	return 0;
}

static int __devinit rt5025_power_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_platform_data *pdata = chip->dev->platform_data;
	struct rt5025_power_info *pi;
	int ret = 0;
	printk("%s,line=%d\n", __func__,__LINE__);	

	pi = kzalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi)
		return -ENOMEM;

	pi->i2c = chip->i2c;
	pi->dev = &pdev->dev;

	ret = rt5025_gauge_init(pi);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, pi);

	pi->ac.name = "rt5025-ac";
	pi->ac.type = POWER_SUPPLY_TYPE_MAINS;
	pi->ac.supplied_to = rt5025_supply_list;
	pi->ac.properties = rt5025_adap_props;
	pi->ac.num_properties = ARRAY_SIZE(rt5025_adap_props);
	pi->ac.get_property = rt5025_adap_get_props;
	ret = power_supply_register(&pdev->dev, &pi->ac);
	if (ret)
		goto out;

	pi->usb.name = "rt5025-usb";
	pi->usb.type = POWER_SUPPLY_TYPE_USB;
	pi->ac.supplied_to = rt5025_supply_list;
	pi->usb.properties = rt5025_adap_props;
	pi->usb.num_properties = ARRAY_SIZE(rt5025_adap_props);
	pi->usb.get_property = rt5025_adap_get_props;
	ret = power_supply_register(&pdev->dev, &pi->usb);
	if (ret)
		goto out_usb;

	rt5025_init_charger(pi, pdata->power_data);
	chip->power_info = pi;

	return ret;
out_usb:
	power_supply_unregister(&pi->ac);
out:
	kfree(pi);

	return ret;
}

static int rt5025_power_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rt5025_power_info *pi = platform_get_drvdata(pdev);

	if (pi->event_callback)
		pi->event_callback->rt5025_gauge_suspend();
	return 0;
}

static int rt5025_power_resume(struct platform_device *pdev)
{
	struct rt5025_power_info *pi = platform_get_drvdata(pdev);
	if (pi->event_callback)
		pi->event_callback->rt5025_gauge_resume();
	return 0;
}

static int __devexit rt5025_power_remove(struct platform_device *pdev)
{
	struct rt5025_power_info *pi = platform_get_drvdata(pdev);
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);

	if (pi->event_callback)
		pi->event_callback->rt5025_gauge_remove();
	power_supply_unregister(&pi->usb);
	power_supply_unregister(&pi->ac);
	chip->power_info = NULL;
	kfree(pi);

	return 0;
}

static struct platform_driver rt5025_power_driver = 
{
	.driver = {
		.name = RT5025_DEVICE_NAME "-power",
		.owner = THIS_MODULE,
	},
	.probe = rt5025_power_probe,
	.remove = __devexit_p(rt5025_power_remove),
	.suspend = rt5025_power_suspend,
	.resume = rt5025_power_resume,
};

static int __init rt5025_power_init(void)
{
	return platform_driver_register(&rt5025_power_driver);
}
subsys_initcall_sync(rt5025_power_init);

static void __exit rt5025_power_exit(void)
{
	platform_driver_unregister(&rt5025_power_driver);
}
module_exit(rt5025_power_exit);


MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("Power/Gauge driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEVICE_NAME "-power");
