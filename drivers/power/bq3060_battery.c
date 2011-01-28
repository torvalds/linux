/*
 * bq3060 battery driver
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
#include <mach/gpio.h>

#define DRIVER_VERSION			"1.1.0"

#define BQ3060_REG_TEMP		0x08
#define BQ3060_REG_VOLT		0x09
#define BQ3060_REG_AI			0x0a   ///0x14
#define BQ3060_REG_STATUS		0x16
#define BQ3060_REG_TTE			0x06 ///0x16
#define BQ3060_REG_TTF			0x05 //0x18
#define BQ3060_REG_TTECP		0x12 //0x26
#define BQ3060_REG_DESIGNCAPACITY	0x18 //0x26

#define BQ3060_REG_RSOC		0x0B /* Relative State-of-Charge */
#define BQ3060_FLAG_CHGS		BIT(7)

#define BQ3060_REG_CAPACITY	0x0f  ///0x0E
#define BQ3060_FLAG_DSC		BIT(0)
#define BQ3060_FLAG_FC			BIT(9)

#define bq3060_SPEED 			200 * 1000

#define DC_CHECK_PIN			RK29_PIN4_PA1

/* manufacturer access defines */
#define MANUFACTURER_ACCESS_STATUS 0x0006
#define MANUFACTURER_ACCESS_SLEEP 0x0011

/* battery status value bits */
#define BATTERY_DISCHARGING		0x40
#define BATTERY_FULL_CHARGED		0x20
#define BATTERY_FULL_DISCHARGED 	0x10


#if 0
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif

/* If the system has several batteries we need a different name for each
 * of them...
 */
static DEFINE_MUTEX(battery_mutex);

struct bq3060_device_info {
	struct device 		*dev;
	struct power_supply	bat;
	struct power_supply	ac;
	struct power_supply	usb;
	struct delayed_work work;
	unsigned int interval;
	struct i2c_client	*client;
};

static enum power_supply_property bq3060_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	//POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	//POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	//POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
};

static enum power_supply_property rk29_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};


/*
 * Common code for bq3060 devices read
 */
static int bq3060_read(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msgs[2];
	int ret;
	char reg_buf = reg;
	
	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 1;
	msgs[0].buf = &reg_buf;
	msgs[0].scl_rate = bq3060_SPEED;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = (char *)buf;
	msgs[1].scl_rate = bq3060_SPEED;

	ret = i2c_transfer(adap, msgs, 2);

	return (ret == 2)? len : ret;
}
static int bq3060_write(struct i2c_client *client, u8 reg, u8 const buf[], unsigned len)
{
	int ret; 
	///return 0;
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, bq3060_SPEED);
	return ret;
}
/*
 * Return the battery temperature in tenths of degree Celsius
 * Or < 0 if something fails.
 */
static int bq3060_battery_temperature(struct bq3060_device_info *di)
{
	int ret;
	int temp = 0;
	u8 buf[2];
	ret = bq3060_read(di->client,BQ3060_REG_TEMP,buf,2);
	if (ret<0) {
		dev_err(di->dev, "error reading temperature\n");
		return ret;
	}
	temp = get_unaligned_le16(buf);
	temp = temp - 2731;
	//#if CONFIG_NO_BATTERY_IC
	temp = 258;
	//#endif
	DBG("Enter:%s %d--temp = %d\n",__FUNCTION__,__LINE__,temp);
	return temp;
}

/*
 * Return the battery Voltage in milivolts
 * Or < 0 if something fails.
 */
static int bq3060_battery_voltage(struct bq3060_device_info *di)
{
	int ret;
	u8 buf[2];
	int volt = 0;

	ret = bq3060_read(di->client,BQ3060_REG_VOLT,buf,2); 
	if (ret<0) {
		dev_err(di->dev, "error reading voltage\n");
		return ret;
	}
	volt = get_unaligned_le16(buf);
	volt = volt;
	DBG("Enter:%s %d--volt = %d\n",__FUNCTION__,__LINE__,volt);
	return volt;
}

/*
 * Return the battery average current
 * Note that current can be negative signed as well
 * Or 0 if something fails.
 */
static int bq3060_battery_current(struct bq3060_device_info *di)
{
	int ret;
	int curr = 0;
	u8 buf[2];

	ret = bq3060_read(di->client,BQ3060_REG_AI,buf,2);
	if (ret<0) {
		dev_err(di->dev, "error reading current\n");
		return 0;
	}

	curr = get_unaligned_le16(buf);
	if(curr>0x8000){
		curr = 0xFFFF^(curr-1);
	}
	curr = curr * 1000;
	DBG("Enter:%s %d--curr = %d\n",__FUNCTION__,__LINE__,curr);
	return curr;
}

/*
 * Return the battery Relative State-of-Charge
 * Or < 0 if something fails.
 */
static int bq3060_battery_capacity(struct bq3060_device_info *di)
{
	int ret;
	int rsoc = 0;
	#if 1
	int designcapacity=0;
	#endif
	u8 buf[2];
	
	ret = bq3060_read(di->client,BQ3060_REG_CAPACITY,buf,2); 
	if (ret<0) {
		dev_err(di->dev, "error reading relative State-of-Charge\n");
		return ret;
	}
	rsoc = get_unaligned_le16(buf);
	DBG("Enter:%s %d--capacity = %d\n",__FUNCTION__,__LINE__,rsoc);
	#if CONFIG_NO_BATTERY_IC
	rsoc = 100;
	#endif
	#if 1
	ret = bq3060_read(di->client,BQ3060_REG_DESIGNCAPACITY,buf,2);
	designcapacity = get_unaligned_le16(buf);
	DBG("Enter:%s %d--designcapacity = %d\n",__FUNCTION__,__LINE__,designcapacity);
	#endif
	
	if((rsoc<150)|(designcapacity<=200))
		return 0;
	rsoc = ((rsoc - 100)*100) / (designcapacity -200);
	if(rsoc>100)
		rsoc = 100;
	DBG("Enter:%s %d--capacity = %d\n",__FUNCTION__,__LINE__,rsoc);	
	return rsoc;
}

static int bq3060_battery_status(struct bq3060_device_info *di,
				  union power_supply_propval *val)
{
	u8 buf[2];
	int flags = 0;
	int status;
	int ret;

	ret = bq3060_read(di->client,BQ3060_REG_STATUS, buf, 2);
	if (ret < 0) {
		dev_err(di->dev, "error reading flags\n");
		return ret;
	}
	flags = get_unaligned_le16(buf);
	DBG("Enter:%s %d--flags = %x\n",__FUNCTION__,__LINE__,flags);
	if (flags & 0x20 )
		status = POWER_SUPPLY_STATUS_FULL;
	else if (flags & 0x40 )
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	else
		status = POWER_SUPPLY_STATUS_CHARGING;

	val->intval = status;
	return status;
}

/*
 * Read a time register.
 * Return < 0 if something fails.
 */
static int bq3060_battery_time(struct bq3060_device_info *di, int reg,
				union power_supply_propval *val)
{
	u8 buf[2];
	int tval = 0;
	int ret;

	ret = bq3060_read(di->client,reg,buf,2);
	if (ret<0) {
		dev_err(di->dev, "error reading register %02x\n", reg);
		return ret;
	}
	tval = get_unaligned_le16(buf);
	DBG("Enter:%s %d--tval=%d\n",__FUNCTION__,__LINE__,tval);
	if (tval == 65535)
		return -ENODATA;

	val->intval = tval * 60;
	DBG("Enter:%s %d val->intval = %d\n",__FUNCTION__,__LINE__,val->intval);
	return 0;
}

#define to_bq3060_device_info(x) container_of((x), \
				struct bq3060_device_info, bat);

static int bq3060_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	int ret = 0;
	struct bq3060_device_info *di = to_bq3060_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = bq3060_battery_status(di, val);
		if(val->intval < 0)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_HEALTH:		
		val->intval = POWER_SUPPLY_HEALTH_GOOD;		
		break;	
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval =1;// bq3060_battery_voltage(di);
	
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = bq3060_battery_current(di);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = bq3060_battery_capacity(di);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = bq3060_battery_temperature(di);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:		
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION; 	
		break;		
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		ret = bq3060_battery_time(di, BQ3060_REG_TTE, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = bq3060_battery_time(di, BQ3060_REG_TTECP, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		ret = bq3060_battery_time(di, BQ3060_REG_TTF, val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk29_ac_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	int ret = 0;
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS){
			if(gpio_get_value(DC_CHECK_PIN))
				val->intval = 0;
			else
				val->intval = 1;	
		}
		DBG("%s:%d val->intval = %d\n",__FUNCTION__,__LINE__,val->intval);
		break;
		
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static void bq3060_powersupply_init(struct bq3060_device_info *di)
{
	di->bat.name = "battery";
	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = bq3060_battery_props;
	di->bat.num_properties = ARRAY_SIZE(bq3060_battery_props);
	di->bat.get_property = bq3060_battery_get_property;

	di->ac.name = "ac";
	di->ac.type = POWER_SUPPLY_TYPE_MAINS;
	di->ac.properties = rk29_ac_props;
	di->ac.num_properties = ARRAY_SIZE(rk29_ac_props);
	di->ac.get_property = rk29_ac_get_property;

	di->usb.name = "usb";
	di->usb.type = POWER_SUPPLY_TYPE_USB;
	di->usb.properties = rk29_ac_props;
	di->usb.num_properties = ARRAY_SIZE(rk29_ac_props);
	di->usb.get_property = bq3060_battery_get_property;
}


static void bq3060_battery_update_status(struct bq3060_device_info *di)
{
	power_supply_changed(&di->bat);
}

static void bq3060_battery_work(struct work_struct *work)
{
	struct bq3060_device_info *di = container_of(work, struct bq3060_device_info, work.work); 
	bq3060_battery_update_status(di);
	/* reschedule for the next time */
	schedule_delayed_work(&di->work, di->interval);
}

static int bq3060_battery_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct bq3060_device_info *di;
	int retval = 0;
	
	u8 buf[2];
	 
	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		retval = -ENOMEM;
		goto batt_failed_2;
	}
	i2c_set_clientdata(client, di);
	di->dev = &client->dev;
	di->bat.name = "bq3060-battery";
	di->client = client;
	/* 4 seconds between monotor runs interval */
	di->interval = msecs_to_jiffies(1 * 1000);
	
	gpio_request(DC_CHECK_PIN,"dc_check");
	gpio_direction_input(DC_CHECK_PIN);
	bq3060_powersupply_init(di);
	buf[0] = 0x41;
	buf[1] = 0x00;
	bq3060_write(di->client,0x00,buf,2);
	buf[0] = 0x21;
	buf[1] = 0x00;
	bq3060_write(di->client,0x00,buf,2);
	retval = power_supply_register(&client->dev, &di->bat);
	if (retval) {
		dev_err(&client->dev, "failed to register battery\n");
		goto batt_failed_4;
	}
	//retval = power_supply_register(&client->dev, &di->usb);
	if (retval) {
		dev_err(&client->dev, "failed to register usb battery\n");
		goto batt_failed_4;
	}
	retval = power_supply_register(&client->dev, &di->ac);
	if (retval) {
		dev_err(&client->dev, "failed to register ac adapter\n");
		goto batt_failed_4;
	}
	INIT_DELAYED_WORK(&di->work, bq3060_battery_work);
	schedule_delayed_work(&di->work, di->interval);
	dev_info(&client->dev, "support ver. %s enabled\n", DRIVER_VERSION);

	return 0;

batt_failed_4:
	kfree(di);
batt_failed_2:
	return retval;
}

static int bq3060_battery_remove(struct i2c_client *client)
{
	struct bq3060_device_info *di = i2c_get_clientdata(client);

	power_supply_unregister(&di->bat);
	kfree(di->bat.name);
	kfree(di);
	return 0;
}

/*
 * Module stuff
 */

static const struct i2c_device_id bq3060_id[] = {
	{ "bq3060", 0 },
};

static struct i2c_driver bq3060_battery_driver = {
	.driver = {
		.name = "bq3060-battery",
	},
	.probe = bq3060_battery_probe,
	.remove = bq3060_battery_remove,
	.id_table = bq3060_id,
};

static int __init bq3060_battery_init(void)
{
	int ret;

	ret = i2c_add_driver(&bq3060_battery_driver);
	if (ret)
		printk(KERN_ERR "Unable to register bq3060 driver\n");
	return ret;
}
module_init(bq3060_battery_init);

static void __exit bq3060_battery_exit(void)
{
	i2c_del_driver(&bq3060_battery_driver);
}
module_exit(bq3060_battery_exit);

MODULE_AUTHOR("Rockchip");
MODULE_DESCRIPTION("bq3060 battery monitor driver");
MODULE_LICENSE("GPL");
