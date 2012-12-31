/*
 * max8677_charger.c - Maxim 8677 USB/Adapter Charger Driver
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
#include <linux/power/max8677_charger.h>

#include <mach/gpio.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>

#define MAX8677_DELAY			msecs_to_jiffies(3000)
//#define	DEBUG_MAX8677

struct max8677_data {
	struct max8677_pdata 	*pdata;
	struct device 			*dev;
	struct power_supply		usb;
	struct delayed_work		work;
	bool usb_in;
};

static enum power_supply_property max8677_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS, /* Charger status output */
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE, /* External power source */
	POWER_SUPPLY_PROP_HEALTH, /* Fault or OK */
};

static int max8677_get_usb_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct max8677_data *data = container_of(psy,
			struct max8677_data, usb);

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
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void max8677_usb_status(struct max8677_data *data)
{
	bool usb_in;
	struct max8677_pdata *pdata = data->pdata;

	usb_in = gpio_get_value(pdata->uok) ? false : true;

	if (data->usb_in == usb_in)		return;

	data->usb_in = usb_in;

	/* Do not touch Current-Limit-Mode */

	/* Charger Enable / Disable (cen is negated) */
	if (pdata->cen)
		gpio_set_value(pdata->cen, usb_in ? 0 : 1);

	dev_dbg(data->dev, "USB Charger %s.\n", usb_in ?
			"Connected" : "Disconnected");

	power_supply_changed(&data->usb);

#if defined(DEBUG_MAX8677)
	printk("%s : power_supply_changed!\n", __func__);
#endif	
}

static void max8677_work(struct work_struct *work)
{
	struct max8677_data *data = container_of(work, struct max8677_data, work.work);

	max8677_usb_status(data);

#if defined(DEBUG_MAX8677)
	if(data->pdata->done)	{
		printk("USB Charger Status = %d\n",	gpio_get_value(data->pdata->done));
	}
#endif
	schedule_delayed_work(&data->work, MAX8677_DELAY);
}

static __devinit int max8677_probe(struct platform_device *pdev)
{
	struct max8677_data *data;
	struct device *dev = &pdev->dev;
	struct max8677_pdata *pdata = pdev->dev.platform_data;
	int ret = 0;

	data = kzalloc(sizeof(struct max8677_data), GFP_KERNEL);
	if (data == NULL) {
		dev_err(dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}
	if(pdata == NULL)	{
		dev_err(dev, "Cannot find platform data.\n");
		return -EIO;
	}

	data->pdata = pdata;
	data->dev = dev;
	platform_set_drvdata(pdev, data);

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
	
	if (pdata->cen) {
		if(gpio_request(pdata->cen, "Charger CEN"))	{
			dev_err(dev, "Charger CEN GPIO Request fail\n");
			ret = -EINVAL;
			goto err_cen;
		}
		gpio_direction_output(pdata->cen, 1);
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

	if (pdata->done) {
		if(gpio_request(pdata->done, "Charger DONE"))	{
			dev_err(dev, "Charger DONE GPIO Request fail\n");
			ret = -EINVAL;
			goto err_chg;
		}
		gpio_direction_input(pdata->done);
		s3c_gpio_setpull(pdata->done, S3C_GPIO_PULL_NONE);
	}
	else	{
		dev_err(dev, "Charger DONE GPIO define error!\n");	
		ret = -EINVAL;
		goto err_done;
	}

	data->usb_in 	= false;

	data->usb.name 				= "max8677_charger-usb";
	data->usb.type 				= POWER_SUPPLY_TYPE_USB;
	data->usb.get_property		= max8677_get_usb_property;
	data->usb.properties 		= max8677_charger_props;
	data->usb.num_properties 	= ARRAY_SIZE(max8677_charger_props);

	if ((ret = power_supply_register(dev, &data->usb))) {
		dev_err(dev, "failed: power supply max8677-usb register.\n");
		goto err;
	}
	else	{
		dev_info(dev, "power supply max8677-usb registerd.\n");
	}

	INIT_DELAYED_WORK_DEFERRABLE(&data->work, max8677_work);
	schedule_delayed_work(&data->work, MAX8677_DELAY);

	return 0;

err:
err_done:
	if(pdata->done)		gpio_free(pdata->done);
err_chg:
	if(pdata->chg)		gpio_free(pdata->chg);
err_cen:
	if(pdata->cen)		gpio_free(pdata->cen);
err_uok:
	if(pdata->uok)		gpio_free(pdata->uok);
	kfree(data);

	return ret;
}

static __devexit int max8677_remove(struct platform_device *pdev)
{
	struct max8677_data *data = platform_get_drvdata(pdev);

	if (data) {
		struct max8677_pdata *pdata = data->pdata;

		if(pdata->chg)		gpio_free(pdata->chg);
		if(pdata->cen)		gpio_free(pdata->cen);
		if(pdata->uok)		gpio_free(pdata->uok);
		if(pdata->done)		gpio_free(pdata->done);

		cancel_delayed_work(&data->work);

		kfree(data);
	}

	return 0;
}

static struct platform_driver max8677_driver = {
	.probe	= max8677_probe,
	.remove	= __devexit_p(max8677_remove),
	.driver = {
		.name	= "max8677-charger",
		.owner	= THIS_MODULE,
	},
};

static int __init max8677_init(void)
{
	return platform_driver_register(&max8677_driver);
}
module_init(max8677_init);

static void __exit max8677_exit(void)
{
	platform_driver_unregister(&max8677_driver);
}
module_exit(max8677_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("max8677 Charger Driver");
MODULE_AUTHOR("Hardkernel Co,. LTD");
MODULE_ALIAS("max8677-charger");
