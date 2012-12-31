/*
 * max8903_charger.c - Maxim 8903 USB/Adapter Charger Driver
 *
 * Copyright (C) 2011 Samsung Electronics
 * MyungJoo Ham <myungjoo.ham@samsung.com>
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
 *
 */

#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/power/max8903_charger.h>

#include <mach/gpio.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>

#define MAX8903_DELAY			msecs_to_jiffies(3000)
//#define	DEBUG_MAX8093

struct max8903_data {
	struct max8903_pdata 	*pdata;
	struct device 			*dev;
	struct power_supply		ac;
	struct power_supply		usb;
	struct delayed_work		work;
	bool fault;
	bool usb_in;
	bool ta_in;
};

static enum power_supply_property max8903_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS, /* Charger status output */
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE, /* External power source */
	POWER_SUPPLY_PROP_HEALTH, /* Fault or OK */
};

static int max8903_get_ac_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct max8903_data *data = container_of(psy,
			struct max8903_data, ac);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		if (data->pdata->chg) {
			if (gpio_get_value(data->pdata->chg) == 0)	{
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			}
			else	{
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			}
		}
		break;

	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		if(data->ta_in)		val->intval = 1;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		if (data->fault)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int max8903_get_usb_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct max8903_data *data = container_of(psy,
			struct max8903_data, usb);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		if (data->pdata->chg) {
			if (gpio_get_value(data->pdata->chg) == 0)	{
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			}
			else	{
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			}
		}
		break;
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		if(data->usb_in)	val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		if (data->fault)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void max8903_ac_status(struct max8903_data *data)
{
	bool	ta_in;
	struct max8903_pdata *pdata = data->pdata;

	ta_in = gpio_get_value(pdata->dok) ? false : true;

	if (data->ta_in == ta_in)		return;
	
	data->ta_in = ta_in;

	/* Set Current-Limit-Mode 1:DC 0:USB */
	if (pdata->dcm)
		gpio_set_value(pdata->dcm, ta_in ? 1 : 0);

	/* Charger Enable / Disable (cen is negated) */
	if (pdata->cen)
		gpio_set_value(pdata->cen, ta_in ? 0 : (data->usb_in ? 0 : 1));

	dev_dbg(data->dev, "TA(DC-IN) Charger %s.\n", ta_in ?
			"Connected" : "Disconnected");

	power_supply_changed(&data->ac);

#if defined(DEBUG_MAX8093)
	printk("%s : power_supply_changed!\n", __func__);
#endif
}

static void max8903_usb_status(struct max8903_data *data)
{
	bool usb_in;
	struct max8903_pdata *pdata = data->pdata;

	usb_in = gpio_get_value(pdata->uok) ? false : true;

	if (data->usb_in == usb_in)		return;

	data->usb_in = usb_in;

	/* Do not touch Current-Limit-Mode */

	/* Charger Enable / Disable (cen is negated) */
	if (pdata->cen)
		gpio_set_value(pdata->cen, usb_in ? 0 : (data->ta_in ? 0 : 1));

	dev_dbg(data->dev, "USB Charger %s.\n", usb_in ?
			"Connected" : "Disconnected");

	power_supply_changed(&data->usb);

#if defined(DEBUG_MAX8093)
	printk("%s : power_supply_changed!\n", __func__);
#endif	
}

static void max8903_work(struct work_struct *work)
{
	struct max8903_data *data = container_of(work, struct max8903_data, work.work);
	struct max8903_pdata *pdata = data->pdata;

	if(pdata->dc_valid)		max8903_ac_status(data);
	if(pdata->usb_valid)	max8903_usb_status(data);

	schedule_delayed_work(&data->work, MAX8903_DELAY);
}

static __devinit int max8903_probe(struct platform_device *pdev)
{
	struct max8903_data *data;
	struct device *dev = &pdev->dev;
	struct max8903_pdata *pdata = pdev->dev.platform_data;
	int ret = 0;
	int ta_in = 0;
	int usb_in = 0;

	data = kzalloc(sizeof(struct max8903_data), GFP_KERNEL);
	if (data == NULL) {
		dev_err(dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}
	data->pdata = pdata;
	data->dev = dev;
	platform_set_drvdata(pdev, data);

	if (pdata->dc_valid == false && pdata->usb_valid == false) {
		dev_err(dev, "No valid power sources.\n");
		ret = -EINVAL;
		goto err;
	}

	if (pdata->dc_valid) {
		if(pdata->dok)	{
			if(gpio_request(pdata->dok, "Charger DOK"))	{
				dev_err(dev, "Charger DOK GPIO Request fail\n");
				ret = -EINVAL;
				goto err_dok;
			}
			gpio_direction_input(pdata->dok);
			s3c_gpio_setpull(pdata->dok, S3C_GPIO_PULL_NONE);
		}
		else	{
			dev_err(dev, "DC Check GPIO define error!\n");	
			ret = -EINVAL;
			goto err_dok;
		}
		
		if(pdata->dcm)	{
			if(pdata->dcm)	{
				if(gpio_request(pdata->dcm, "Charger DCM"))	{
					dev_err(dev, "Charger DCM GPIO Request fail\n");
					ret = -EINVAL;
					goto err_dcm;
				}
				gpio_direction_output(pdata->dcm, 0);
				s3c_gpio_setpull(pdata->dcm, S3C_GPIO_PULL_NONE);
			}
		}
		else	{
			dev_info(dev, "Can't control Current-Limit-Mode. DCM Always High!\n");
		}
		
		// DC In check
		ta_in = gpio_get_value(pdata->dok) ? 0 : 1;
		
		// Current mode setting
		if(pdata->dcm)		gpio_set_value(pdata->dcm, ta_in);
	}
	
	if (pdata->usb_valid) {
		if(pdata->uok)	{
			if(gpio_request(pdata->uok, "Charger UOK"))	{
				dev_err(dev, "Charger UOK GPIO Request fail\n");
				ret = -EINVAL;
				goto err_uok;
			}
			gpio_direction_input(pdata->uok);
			s3c_gpio_setpull(pdata->uok, S3C_GPIO_PULL_NONE);
		}
		else	{
			dev_err(dev, "USB Check GPIO define error!\n");	
			ret = -EINVAL;
			goto err_uok;
		}
		
		// USB In Check
		usb_in = gpio_get_value(pdata->uok) ? 0 : 1;
	}

	if (pdata->cen) {
		if(gpio_request(pdata->cen, "Charger CEN"))	{
			dev_err(dev, "Charger CEN GPIO Request fail\n");
			ret = -EINVAL;
			goto err_cen;
		}
		gpio_direction_output(pdata->cen, ((ta_in || usb_in) ? 0 : 1));
		s3c_gpio_setpull(pdata->cen, S3C_GPIO_PULL_NONE);
	}
	else	{
		dev_err(dev, "Charger CEN GPIO define error!\n");	
		ret = -EINVAL;
		goto err_cen;
	}

	if (pdata->chg) {
		if(gpio_request(pdata->chg, "Charger CHG"))	{
			dev_err(dev, "Charger CHG GPIO Request fail\n");
			ret = -EINVAL;
			goto err_chg;
		}
		gpio_direction_input(pdata->chg);
		s3c_gpio_setpull(pdata->chg, S3C_GPIO_PULL_NONE);
	}
	else	{
		dev_err(dev, "Charger CHG GPIO define error!\n");	
		ret = -EINVAL;
		goto err_chg;
	}

	if (pdata->flt) {
		if(gpio_request(pdata->flt, "Charger FLT"))	{
			dev_err(dev, "Charger FLT GPIO Request fail\n");
			ret = -EINVAL;
			goto err_flt;
		}
		gpio_direction_input(pdata->flt);
		s3c_gpio_setpull(pdata->flt, S3C_GPIO_PULL_NONE);
	}

	if (pdata->usus) {
		if(gpio_request(pdata->usus, "Charger USUS"))	{
			dev_err(dev, "Charger USUS GPIO Request fail\n");
			ret = -EINVAL;
			goto err_usus;
		}
		/* USB Suspend Input (1: suspended) */
		gpio_direction_output(pdata->usus, 0);
		s3c_gpio_setpull(pdata->usus, S3C_GPIO_PULL_NONE);
	}
	data->fault 	= false;
	data->ta_in 	= false;
	data->usb_in 	= false;

	if (pdata->dc_valid) {
		data->ac.name 				= "max8903_charger-ac";
		data->ac.type 				= POWER_SUPPLY_TYPE_MAINS;
		data->ac.get_property		= max8903_get_ac_property;
		data->ac.properties 		= max8903_charger_props;
		data->ac.num_properties 	= ARRAY_SIZE(max8903_charger_props);
	
		if ((ret = power_supply_register(dev, &data->ac))) {
			dev_err(dev, "failed: power supply max8903-ac register.\n");
			goto err;
		}
		else	{
			dev_info(dev, "power supply max8903-ac registerd.\n");
		}
	}

	if (pdata->usb_valid) {
		data->usb.name 				= "max8903_charger-usb";
		data->usb.type 				= POWER_SUPPLY_TYPE_USB;
		data->usb.get_property		= max8903_get_usb_property;
		data->usb.properties 		= max8903_charger_props;
		data->usb.num_properties 	= ARRAY_SIZE(max8903_charger_props);
	
		if ((ret = power_supply_register(dev, &data->usb))) {
			dev_err(dev, "failed: power supply max8903-usb register.\n");
			goto err;
		}
		else	{
			dev_info(dev, "power supply max8903-usb registerd.\n");
		}
	}

	if(pdata->dc_valid || pdata->usb_valid)	{
		INIT_DELAYED_WORK_DEFERRABLE(&data->work, max8903_work);
		schedule_delayed_work(&data->work, MAX8903_DELAY);
	}

	return 0;

err:
err_usus:
	if(pdata->usus)		gpio_free(pdata->usus);
err_flt:
	if(pdata->flt)		gpio_free(pdata->flt);
err_chg:
	if(pdata->chg)		gpio_free(pdata->chg);
err_cen:
	if(pdata->cen)		gpio_free(pdata->cen);
err_uok:
	if(pdata->uok)		gpio_free(pdata->uok);
err_dcm:
	if(pdata->dcm)		gpio_free(pdata->dcm);
err_dok:
	if(pdata->dok)		gpio_free(pdata->dok);
	kfree(data);

	return ret;
}

static __devexit int max8903_remove(struct platform_device *pdev)
{
	struct max8903_data *data = platform_get_drvdata(pdev);

	if (data) {
		struct max8903_pdata *pdata = data->pdata;

		if(pdata->usus)		gpio_free(pdata->usus);
		if(pdata->flt)		gpio_free(pdata->flt);
		if(pdata->chg)		gpio_free(pdata->chg);
		if(pdata->cen)		gpio_free(pdata->cen);
		if(pdata->uok)		gpio_free(pdata->uok);
		if(pdata->dcm)		gpio_free(pdata->dcm);
		if(pdata->dok)		gpio_free(pdata->dok);

		if(pdata->dc_valid)		power_supply_unregister(&data->ac);
		if(pdata->usb_valid)	power_supply_unregister(&data->usb);

		cancel_delayed_work(&data->work);

		kfree(data);
	}

	return 0;
}

static struct platform_driver max8903_driver = {
	.probe	= max8903_probe,
	.remove	= __devexit_p(max8903_remove),
	.driver = {
		.name	= "max8903-charger",
		.owner	= THIS_MODULE,
	},
};

static int __init max8903_init(void)
{
	return platform_driver_register(&max8903_driver);
}
module_init(max8903_init);

static void __exit max8903_exit(void)
{
	platform_driver_unregister(&max8903_driver);
}
module_exit(max8903_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MAX8903 Charger Driver");
MODULE_AUTHOR("Hardkernel Co,. LTD");
MODULE_ALIAS("max8903-charger");
