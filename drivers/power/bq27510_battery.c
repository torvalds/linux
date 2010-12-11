/*
 * BQ27510 battery driver
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
#include <linux/slab.h>
#include <asm/unaligned.h>

#define DRIVER_VERSION			"1.1.0"

#define BQ27x00_REG_TEMP		0x06
#define BQ27x00_REG_VOLT		0x08
#define BQ27x00_REG_AI			0x14
#define BQ27x00_REG_FLAGS		0x0A
#define BQ27x00_REG_TTE			0x16
#define BQ27x00_REG_TTF			0x18
#define BQ27x00_REG_TTECP		0x26

#define BQ27000_REG_RSOC		0x0B /* Relative State-of-Charge */
#define BQ27000_FLAG_CHGS		BIT(7)

#define BQ27500_REG_SOC			0x2c
#define BQ27500_FLAG_DSC		BIT(0)
#define BQ27500_FLAG_FC			BIT(9)

#define BQ27510_SPEED 			300 * 1000

#if 0
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif

/* If the system has several batteries we need a different name for each
 * of them...
 */
static DEFINE_MUTEX(battery_mutex);

struct bq27510_device_info {
	struct device 		*dev;
	struct power_supply	bat;
	struct delayed_work work;
	unsigned int interval;
	struct i2c_client	*client;
};

static enum power_supply_property bq27510_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	//POWER_SUPPLY_PROP_TEMP,
	//POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	//POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	//POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
};

/*
 * Common code for BQ27510 devices read
 */
static int bq27510_read(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, BQ27510_SPEED);
	return ret; 
}

/*
 * Return the battery temperature in tenths of degree Celsius
 * Or < 0 if something fails.
 */
static int bq27510_battery_temperature(struct bq27510_device_info *di)
{
	int ret;
	int temp = 0;
	u8 buf[2];
	ret = bq27510_read(di->client,BQ27x00_REG_TEMP,buf,2);
	if (ret<0) {
		dev_err(di->dev, "error reading temperature\n");
		return ret;
	}
	temp = get_unaligned_le16(buf);
	temp = temp - 2731;
	DBG("Enter:%s %d--temp = %d\n",__FUNCTION__,__LINE__,temp);
	return temp;
}

/*
 * Return the battery Voltage in milivolts
 * Or < 0 if something fails.
 */
static int bq27510_battery_voltage(struct bq27510_device_info *di)
{
	int ret;
	u8 buf[2];
	int volt = 0;

	ret = bq27510_read(di->client,BQ27x00_REG_VOLT,buf,2); 
	if (ret<0) {
		dev_err(di->dev, "error reading voltage\n");
		return ret;
	}
	volt = get_unaligned_le16(buf);
	volt = volt * 1000;
	DBG("Enter:%s %d--volt = %d\n",__FUNCTION__,__LINE__,volt);
	return volt;
}

/*
 * Return the battery average current
 * Note that current can be negative signed as well
 * Or 0 if something fails.
 */
static int bq27510_battery_current(struct bq27510_device_info *di)
{
	int ret;
	int curr = 0;
	u8 buf[2];

	ret = bq27510_read(di->client,BQ27x00_REG_AI,buf,2);
	if (ret<0) {
		dev_err(di->dev, "error reading current\n");
		return 0;
	}

	curr = get_unaligned_le16(buf);
	curr = curr * 1000;
	DBG("Enter:%s %d--curr = %d\n",__FUNCTION__,__LINE__,curr);
	return curr;
}

/*
 * Return the battery Relative State-of-Charge
 * Or < 0 if something fails.
 */
static int bq27510_battery_rsoc(struct bq27510_device_info *di)
{
	int ret;
	int rsoc = 0;
	u8 buf[2];
	
	ret = bq27510_read(di->client,BQ27500_REG_SOC,buf,2); 
	if (ret<0) {
		dev_err(di->dev, "error reading relative State-of-Charge\n");
		return ret;
	}
	rsoc = get_unaligned_le16(buf);
	DBG("Enter:%s %d--rsoc = %d\n",__FUNCTION__,__LINE__,rsoc);
	return rsoc;
}

static int bq27510_battery_status(struct bq27510_device_info *di,
				  union power_supply_propval *val)
{
	u8 buf[2];
	int flags = 0;
	int status;
	int ret;

	ret = bq27510_read(di->client,BQ27x00_REG_FLAGS, buf, 2);
	if (ret < 0) {
		dev_err(di->dev, "error reading flags\n");
		return ret;
	}
	flags = get_unaligned_le16(buf);
	if (flags & BQ27500_FLAG_FC)
		status = POWER_SUPPLY_STATUS_FULL;
	else if (flags & BQ27500_FLAG_DSC)
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	else
		status = POWER_SUPPLY_STATUS_CHARGING;

	val->intval = status;
	return 0;
}

/*
 * Read a time register.
 * Return < 0 if something fails.
 */
static int bq27510_battery_time(struct bq27510_device_info *di, int reg,
				union power_supply_propval *val)
{
	u8 buf[2];
	int tval = 0;
	int ret;

	ret = bq27510_read(di->client,reg,buf,2);
	if (ret<0) {
		dev_err(di->dev, "error reading register %02x\n", reg);
		return ret;
	}
	tval = get_unaligned_le16(buf);
	DBG("Enter:%s %d--no battery data\n",__FUNCTION__,__LINE__,val->intval);
	if (tval == 65535)
		return -ENODATA;

	val->intval = tval * 60;
	DBG("Enter:%s %d val->intval = %d\n",__FUNCTION__,__LINE__,val->intval);
	return 0;
}

#define to_bq27510_device_info(x) container_of((x), \
				struct bq27510_device_info, bat);

static int bq27510_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	int ret = 0;
	struct bq27510_device_info *di = to_bq27510_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = bq27510_battery_status(di, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = bq27510_battery_voltage(di);
		if (psp == POWER_SUPPLY_PROP_PRESENT)
			val->intval = val->intval <= 0 ? 0 : 1;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = bq27510_battery_current(di);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = 100; ///bq27510_battery_rsoc(di);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = bq27510_battery_temperature(di);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		ret = bq27510_battery_time(di, BQ27x00_REG_TTE, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = bq27510_battery_time(di, BQ27x00_REG_TTECP, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		ret = bq27510_battery_time(di, BQ27x00_REG_TTF, val);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void bq27510_powersupply_init(struct bq27510_device_info *di)
{
	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = bq27510_battery_props;
	di->bat.num_properties = ARRAY_SIZE(bq27510_battery_props);
	di->bat.get_property = bq27510_battery_get_property;
	di->bat.external_power_changed = NULL;
}

static void bq27510_battery_update_status(struct bq27510_device_info *di)
{
	power_supply_changed(&di->bat);
}

static void bq27510_battery_work(struct work_struct *work)
{
	struct bq27510_device_info *di = container_of(work, struct bq27510_device_info, work.work); 
	bq27510_battery_update_status(di);
	/* reschedule for the next time */
	schedule_delayed_work(&di->work, di->interval);
}

static int bq27510_battery_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct bq27510_device_info *di;
	int retval = 0;

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		retval = -ENOMEM;
		goto batt_failed_2;
	}
	i2c_set_clientdata(client, di);
	di->dev = &client->dev;
	di->bat.name = "bq27510-battery";
	di->client = client;
	/* 4 seconds between monotor runs interval */
	di->interval = msecs_to_jiffies(4 * 1000);
	bq27510_powersupply_init(di);

	retval = power_supply_register(&client->dev, &di->bat);
	if (retval) {
		dev_err(&client->dev, "failed to register battery\n");
		goto batt_failed_4;
	}
	INIT_DELAYED_WORK(&di->work, bq27510_battery_work);
	schedule_delayed_work(&di->work, di->interval);
	dev_info(&client->dev, "support ver. %s enabled\n", DRIVER_VERSION);

	return 0;

batt_failed_4:
	kfree(di);
batt_failed_2:
	return retval;
}

static int bq27510_battery_remove(struct i2c_client *client)
{
	struct bq27510_device_info *di = i2c_get_clientdata(client);

	power_supply_unregister(&di->bat);
	kfree(di->bat.name);
	kfree(di);
	return 0;
}

/*
 * Module stuff
 */

static const struct i2c_device_id bq27510_id[] = {
	{ "bq27510", 0 },
};

static struct i2c_driver bq27510_battery_driver = {
	.driver = {
		.name = "bq27510-battery",
	},
	.probe = bq27510_battery_probe,
	.remove = bq27510_battery_remove,
	.id_table = bq27510_id,
};

static int __init bq27510_battery_init(void)
{
	int ret;

	ret = i2c_add_driver(&bq27510_battery_driver);
	if (ret)
		printk(KERN_ERR "Unable to register BQ27510 driver\n");
	return ret;
}
module_init(bq27510_battery_init);

static void __exit bq27510_battery_exit(void)
{
	i2c_del_driver(&bq27510_battery_driver);
}
module_exit(bq27510_battery_exit);

MODULE_AUTHOR("Rockchip");
MODULE_DESCRIPTION("BQ27510 battery monitor driver");
MODULE_LICENSE("GPL");
