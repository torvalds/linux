/*
 *  max17047_battery.c
 *  fuel-gauge systems for lithium-ion (Li+) batteries
 *
 *  Copyright (C) 2012 Hardkernel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/power/max17047_battery.h>
#include <linux/slab.h>

#define	REPORT_STATUS			0x00
#define	REPORT_SOC				0x06
#define	REPORT_VCELL			0x09

#define	REPORT_CURRENT			0x0A
#define	REPORT_CURRENT_AV		0x0B
#define	REPORT_SOC_AV			0x0E
#define	REPORT_VCELL_AV			0x19

#define	REPORT_VERSION			0x21

#define MAX17047_DELAY			msecs_to_jiffies(3000)
#define MAX17047_BATTERY_FULL	99
#define	POWER_OFF_VOLTAGE		3300000
#define	POWER_MAX_VOLTAGE		4180000

//#define	DEBUG_MAX17047
#define	MAX17047_AVERAGE_USED

#if defined(CONFIG_MACH_ODROID_4X12)||defined(CONFIG_MACH_ODROID_4210)
	extern	void max17047_set_config(struct i2c_client *client);
#endif

struct max17047_chip {
	struct i2c_client				*client;
	struct delayed_work				work;
	struct power_supply				battery;
	struct max17047_platform_data	*pdata;

	/* State Of Connect */
	int online;
	/* battery voltage */
	int vcell;
	/* battery capacity */
	int soc;
	/* State Of Charge */
	int status;
};

static int max17047_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max17047_chip *chip = container_of(psy,
				struct max17047_chip, battery);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = chip->status;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = chip->vcell;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = chip->soc;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int max17047_read(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_word_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}

static void max17047_get_vcell(struct i2c_client *client)
{
	struct max17047_chip *chip = i2c_get_clientdata(client);

	#if defined(MAX17047_AVERAGE_USED)
		chip->vcell = (max17047_read(client, REPORT_VCELL_AV) >> 3) & 0x1FFF;
	#else	
		chip->vcell = (max17047_read(client, REPORT_VCELL) >> 3) & 0x1FFF;
	#endif	

	chip->vcell = (chip->vcell * 625);		// unit = uV

	#if defined(DEBUG_MAX17047)
		printk("chip->vcell = %d\n", chip->vcell);
	#endif
}

static void max17047_get_soc(struct i2c_client *client)
{
	struct max17047_chip *chip = i2c_get_clientdata(client);

	#if defined(MAX17047_AVERAGE_USED)
		chip->soc = (max17047_read(client, REPORT_SOC_AV) >> 8) & 0x00FF;
	#else
		chip->soc = (max17047_read(client, REPORT_SOC) >> 8) & 0x00FF;
	#endif	

	if(chip->soc)	{
		if(chip->vcell < POWER_OFF_VOLTAGE)		chip->soc = 0;
		if(chip->soc > MAX17047_BATTERY_FULL)	{
			if(chip->vcell < POWER_MAX_VOLTAGE)	chip->soc = MAX17047_BATTERY_FULL;
			else								chip->soc = MAX17047_BATTERY_FULL + 1;
		}
	}
	else	{
		if(chip->vcell >= POWER_OFF_VOLTAGE)	chip->soc = 1;
	}

	#if defined(DEBUG_MAX17047)
		printk("chip->soc = %d\n", chip->soc);
	#endif
}

static void max17047_get_version(struct i2c_client *client)
{
	dev_info(&client->dev, "MAX17047 Fuel-Gauge Ver 0x%04X\n", max17047_read(client, REPORT_VERSION));
}

static void max17047_get_online(struct i2c_client *client)
{
	struct max17047_chip *chip = i2c_get_clientdata(client);

	chip->online = 1;
	if(chip->pdata)	{
		if (chip->pdata->battery_online)
			chip->online = chip->pdata->battery_online();
	}
}

static void max17047_get_status(struct i2c_client *client)
{
	struct max17047_chip *chip = i2c_get_clientdata(client);

	if (chip->soc > MAX17047_BATTERY_FULL)
		chip->status = POWER_SUPPLY_STATUS_FULL;

	if (chip->pdata)	{
		if (!chip->pdata->charger_online || !chip->pdata->charger_enable) {
			chip->status = POWER_SUPPLY_STATUS_UNKNOWN;
			return;
		}
	
		if (chip->pdata->charger_online()) {
			if (chip->pdata->charger_enable())	{
				chip->status = POWER_SUPPLY_STATUS_CHARGING;
				#if defined(DEBUG_MAX17047)
					printk("POWER_SUPPLY_STATUS_CHARGING\n");
				#endif
			}
			else	{
				chip->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
				#if defined(DEBUG_MAX17047)
					printk("POWER_SUPPLY_STATUS_NOT_CHARGING\n");
				#endif
			}
		} else {
			chip->status = POWER_SUPPLY_STATUS_DISCHARGING;
			#if defined(DEBUG_MAX17047)
				printk("POWER_SUPPLY_STATUS_DISCHARGING\n");
			#endif
		}
	}
	else	{
		chip->status = POWER_SUPPLY_STATUS_DISCHARGING;
		#if defined(DEBUG_MAX17047)
			printk("POWER_SUPPLY_STATUS_DISCHARGING\n");
		#endif
	}
}

static void max17047_work(struct work_struct *work)
{
	struct max17047_chip *chip = container_of(work, struct max17047_chip, work.work);
	
	int old_vcell = chip->vcell, old_soc = chip->soc;

	max17047_get_vcell(chip->client);
	max17047_get_soc(chip->client);
	max17047_get_online(chip->client);
	max17047_get_status(chip->client);

	if((old_vcell != chip->vcell) || (old_soc != chip->soc))	{
		power_supply_changed(&chip->battery);
		#if defined(DEBUG_MAX17047)
			printk("%s : power_supply_changed!\n", __func__);
		#endif
	}

	schedule_delayed_work(&chip->work, MAX17047_DELAY);
}

static enum power_supply_property max17047_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int __devinit max17047_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct max17047_chip *chip;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->pdata = client->dev.platform_data;

	i2c_set_clientdata(client, chip);

	chip->battery.name				= "battery";
	chip->battery.type				= POWER_SUPPLY_TYPE_BATTERY;
	chip->battery.get_property		= max17047_get_property;
	chip->battery.properties		= max17047_battery_props;
	chip->battery.num_properties	= ARRAY_SIZE(max17047_battery_props);

	ret = power_supply_register(&client->dev, &chip->battery);
	if (ret) {
		dev_err(&client->dev, "failed: power supply register\n");
		kfree(chip);
		return ret;
	}

#if defined(CONFIG_MACH_ODROID_4X12)||defined(CONFIG_MACH_ODROID_4210)
	max17047_set_config(client);
#endif
		
	max17047_get_version(client);

	dev_info(&client->dev, "power supply max17047-battery registerd.\n");

	INIT_DELAYED_WORK_DEFERRABLE(&chip->work, max17047_work);
	schedule_delayed_work(&chip->work, MAX17047_DELAY);

	return 0;
}

static int __devexit max17047_remove(struct i2c_client *client)
{
	struct max17047_chip *chip = i2c_get_clientdata(client);

	power_supply_unregister(&chip->battery);
	cancel_delayed_work(&chip->work);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM

static int max17047_suspend(struct i2c_client *client,
		pm_message_t state)
{
	struct max17047_chip *chip = i2c_get_clientdata(client);

	cancel_delayed_work(&chip->work);
	return 0;
}

static int max17047_resume(struct i2c_client *client)
{
	struct max17047_chip *chip = i2c_get_clientdata(client);

	schedule_delayed_work(&chip->work, MAX17047_DELAY);
	return 0;
}

#else

#define max17047_suspend NULL
#define max17047_resume NULL

#endif /* CONFIG_PM */

static const struct i2c_device_id max17047_id[] = {
	{ "max17047", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max17047_id);

static struct i2c_driver max17047_i2c_driver = {
	.driver	= {
		.name	= "max17047",
	},
	.probe		= max17047_probe,
	.remove		= __devexit_p(max17047_remove),
	.suspend	= max17047_suspend,
	.resume		= max17047_resume,
	.id_table	= max17047_id,
};

static int __init max17047_init(void)
{
	return i2c_add_driver(&max17047_i2c_driver);
}
module_init(max17047_init);

static void __exit max17047_exit(void)
{
	i2c_del_driver(&max17047_i2c_driver);
}
module_exit(max17047_exit);

MODULE_AUTHOR("Hardkernel Co,.Ltd");
MODULE_DESCRIPTION("MAX17047 Fuel Gauge");
MODULE_LICENSE("GPL");
