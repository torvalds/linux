/* drivers/power/stc3100_battery
 * STC31000 battery driver
 *
 * Copyright (C) 2009 Rockchip Corporation.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <asm/unaligned.h>
#include <mach/gpio.h>
#define DRIVER_VERSION			"1.0.0"


#define DC_CHECKPIN				RK29_PIN4_PA4 ///da check charge pin
#define BATTAERY_CAPACITY_MAH   2000   ///  define battery capacity mah
#define RSENSE_RESISTANCE		10     ///  Rsense resistance m¶∏

#define STC3100_REG_MODE		0x00 
#define STC3100_REG_CTRL		0x01 
#define STC3100_REG_RSOCL		0x02 /* Relative State-of-Charge */
#define STC3100_REG_RSOCH		0x03
#define STC3100_REG_AIL			0x06
#define STC3100_REG_AIH			0x07
#define STC3100_REG_VOLTL		0x08
#define STC3100_REG_VOLTH		0x09
#define STC3100_REG_TEMPL		0x0A
#define STC3100_REG_TEMPH		0x0B

#define STC3100_SPEED 	300 * 1000

struct stc3100_device_info {
	struct device 		*dev;
	struct power_supply	bat;
	struct delayed_work work;
	unsigned int interval;
	struct i2c_client	*client;
};

static enum power_supply_property stc3100_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
};


static int stc3100_write_regs(struct i2c_client *client, u8 reg, u8 const buf[], __u16 len)
{
	int ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, STC3100_SPEED);
	return ret;
}
/*
 * Common code for STC3100 devices read
 */
static int stc3100_read_regs(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, STC3100_SPEED);
	return ret; 
}

/*
 * Return the battery temperature in Celsius degrees
 * Or < 0 if something fails.
 */
static int stc3100_battery_temperature(struct stc3100_device_info *di)
{
	int ret;
	u8 regs[2];

	ret = stc3100_read_regs(di->client,STC3100_REG_TEMPL,regs,2);
	if (ret<0) {
		dev_err(di->dev, "error reading temperature\n");
		return ret;
	}

	return (((((int)regs[1]&0xf)<<8) & (int)regs[0])>>3);  ///temperature (°„C) = Temperature_code * 0.125.
}

/*
 * Return the battery Voltage in milivolts
 * Or < 0 if something fails.
 */
static int stc3100_battery_voltage(struct stc3100_device_info *di)
{
	int ret;
	u8 regs[2];

	ret = stc3100_read_regs(di->client,STC3100_REG_VOLTL,regs,2);
	if (ret<0) {
		dev_err(di->dev, "error reading voltage\n");
		return ret;
	}

	return (((((int)regs[1]&0xf)<<8) & (int)regs[0])*61/25);   //voltage (mV) = Voltage_code * 2.44.
}

/*
 * Return the battery average current
 * Note that current can be negative signed as well
 * Or 0 if something fails.
 */
static int stc3100_battery_current(struct stc3100_device_info *di)
{
	int ret;
	u8 regs[2];

	ret = stc3100_read_regs(di->client,STC3100_REG_AIL,regs,2);
	if (ret<0) {
		dev_err(di->dev, "error reading current\n");
		return 0;
	}
	
	return (((((int)regs[1]&0xf)<<8) & (int)regs[0])*1177/(100*RSENSE_RESISTANCE));   ///current (mA) = current_code* 11.77 / Rsense (m¶∏)
}

/*
 * Return the battery Relative State-of-Charge
 * Or < 0 if something fails.
 */
static int stc3100_battery_rsoc(struct stc3100_device_info *di)
{
	int ret;
	u8 regs[2];

	ret = stc3100_read_regs(di->client,STC3100_REG_RSOCL,regs,2);
	if (ret<0) {
		dev_err(di->dev, "error reading relative State-of-Charge\n");
		return ret;
	}

	return (((((int)regs[1])<<8) & (int)regs[0])*67/(10*RSENSE_RESISTANCE))*100/BATTAERY_CAPACITY_MAH;    ////charge data (mA.h) = 6.70 * charge_code / Rsense (m¶∏).
}

static int dc_charge_status(void)
{
	if(gpio_get_value(DC_CHECKPIN))
		return POWER_SUPPLY_STATUS_CHARGING;
	else
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int stc3100_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct stc3100_device_info *di = container_of(psy, struct stc3100_device_info, bat);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = dc_charge_status();
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = stc3100_battery_voltage(di);
		if (psp == POWER_SUPPLY_PROP_PRESENT)
			val->intval = val->intval <= 0 ? 0 : 1;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = stc3100_battery_current(di);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = stc3100_battery_rsoc(di);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = stc3100_battery_temperature(di);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void stc3100_powersupply_init(struct stc3100_device_info *di)
{
	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = stc3100_battery_props;
	di->bat.num_properties = ARRAY_SIZE(stc3100_battery_props);
	di->bat.get_property = stc3100_battery_get_property;
	di->bat.external_power_changed = NULL;
}

static void stc3100_battery_update_status(struct stc3100_device_info *di)
{
	power_supply_changed(&di->bat);
}

static void stc3100_battery_work(struct work_struct *work)
{
	struct stc3100_device_info *di = container_of(work, struct stc3100_device_info, work.work); 

	stc3100_battery_update_status(di);
	/* reschedule for the next time */
	schedule_delayed_work(&di->work, di->interval);
}

static int stc3100_battery_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct stc3100_device_info *di;
	int retval = 0;
	u8 regs[2] = {0x10,0x1d};  ///init regs mode ctrl

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		retval = -ENOMEM;
		goto batt_failed_2;
	}

	i2c_set_clientdata(client, di);
	di->dev = &client->dev;
	di->bat.name = "stc3100-battery";
	di->client = client;
	stc3100_write_regs(client, STC3100_REG_MODE, regs, 2);
	/* 4 seconds between monotor runs interval */
	di->interval = msecs_to_jiffies(4 * 1000);
	stc3100_powersupply_init(di);

	retval = power_supply_register(&client->dev, &di->bat);
	if (retval) {
		dev_err(&client->dev, "failed to register battery\n");
		goto batt_failed_3;
	}
	
	INIT_DELAYED_WORK(&di->work, stc3100_battery_work);
	schedule_delayed_work(&di->work, di->interval);
	
	dev_info(&client->dev, "support ver. %s enabled\n", DRIVER_VERSION);

	return 0;

batt_failed_3:
	kfree(di);
batt_failed_2:
	return retval;
}

static int stc3100_battery_remove(struct i2c_client *client)
{
	struct stc3100_device_info *di = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&di->work);
	power_supply_unregister(&di->bat);

	kfree(di->bat.name);

	kfree(di);

	return 0;
}

/*
 * Module stuff
 */

static const struct i2c_device_id stc3100_id[] = {
	{ "stc3100", 0 },
	{},
};

static struct i2c_driver stc3100_battery_driver = {
	.driver = {
		.name = "stc3100-battery",
	},
	.probe = stc3100_battery_probe,
	.remove = stc3100_battery_remove,
	.id_table = stc3100_id,
};

static int __init stc3100_battery_init(void)
{
	int ret;

	ret = i2c_add_driver(&stc3100_battery_driver);
	if (ret)
		printk(KERN_ERR "Unable to register BQ27200 driver\n");

	return ret;
}
module_init(stc3100_battery_init);

static void __exit stc3100_battery_exit(void)
{
	i2c_del_driver(&stc3100_battery_driver);
}
module_exit(stc3100_battery_exit);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("BQ27x00 battery monitor driver");
MODULE_LICENSE("GPL");
