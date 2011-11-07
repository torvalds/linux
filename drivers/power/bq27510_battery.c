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
#include <mach/gpio.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <mach/board.h>


#define DRIVER_VERSION			"1.1.0"
#define BQ27x00_REG_TEMP		0x06
#define BQ27x00_REG_VOLT		0x08
#define BQ27x00_REG_AI			0x14
#define BQ27x00_REG_FLAGS		0x0A
#define BQ27x00_REG_TTE			0x16
#define BQ27x00_REG_TTF			0x18
#define BQ27x00_REG_TTECP		0x26
#define BQ27000_REG_RSOC		0x0B /* Relative State-of-Charge */
#define BQ27500_REG_SOC			0x2c

#define BQ27500_FLAG_DSC		BIT(0)
#define BQ27000_FLAG_CHGS		BIT(8)
#define BQ27500_FLAG_FC			BIT(9)
#define BQ27500_FLAG_OTD		BIT(14)
#define BQ27500_FLAG_OTC		BIT(15)

#define BQ27510_SPEED 			300 * 1000
int  virtual_battery_enable = 0;
extern int dwc_vbus_status(void);
static void bq27510_set(void);

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
	struct power_supply	ac;
	struct delayed_work work;
	struct i2c_client	*client;
	unsigned int interval;
	unsigned int dc_check_pin;
	unsigned int bat_num;
};

static struct bq27510_device_info *bq27510_di;
static enum power_supply_property bq27510_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_HEALTH,
	//POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	//POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	//POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
};

static enum power_supply_property rk29_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static ssize_t battery_proc_write(struct file *file,const char __user *buffer,
			 size_t count, loff_t *ppos)
{
	char c;
	int rc;
	printk("USER:\n");
	printk("echo x >/proc/driver/power\n");
	printk("x=1,means just print log ||x=2,means log and data ||x= other,means close log\n");

	rc = get_user(c,buffer);
	if(rc)
		return rc;
	
	//added by zwp,c='8' means check whether we need to download firmware to bq27xxx,return 0 means yes.
	if(c == '8'){
		printk("%s,bq27510 need to download firmware\n",__FUNCTION__);
	}
	
	if(c == '1')
		virtual_battery_enable = 1;
	else if(c == '2')
		virtual_battery_enable = 2;
	else if(c == '3')
		virtual_battery_enable = 3;
	else if(c == '9'){
		printk("%s:%d>>bq27510 set\n",__FUNCTION__,__LINE__);
		bq27510_set();
	}
	else 
		virtual_battery_enable = 0;
	printk("%s,count(%d),virtual_battery_enable(%d)\n",__FUNCTION__,(int)count,virtual_battery_enable);
	return count;
}

static const struct file_operations battery_proc_fops = {
	.owner		= THIS_MODULE, 
	.write		= battery_proc_write,
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

static int bq27510_write(struct i2c_client *client, u8 reg, u8 const buf[], unsigned len)
{
	int ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, BQ27510_SPEED);
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

	#if defined (CONFIG_NO_BATTERY_IC)
	return 258;
	#endif

	if(virtual_battery_enable == 1)
		return 125/*258*/;
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

	#if defined (CONFIG_NO_BATTERY_IC)
		return 4000000;
	#endif
	if(virtual_battery_enable == 1)
		return 2000000/*4000000*/;

	ret = bq27510_read(di->client,BQ27x00_REG_VOLT,buf,2); 
	if (ret<0) {
		dev_err(di->dev, "error reading voltage\n");
		return ret;
	}
	volt = get_unaligned_le16(buf);

	//bp27510 can only measure one li-lion bat
	if(di->bat_num == 2){
		volt = volt * 1000 * 2;
	}else{
		volt = volt * 1000;
	}

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

	#if defined (CONFIG_NO_BATTERY_IC)
		return 22000;
	#endif
	if(virtual_battery_enable == 1)
		return 11000/*22000*/;
	ret = bq27510_read(di->client,BQ27x00_REG_AI,buf,2);
	if (ret<0) {
		dev_err(di->dev, "error reading current\n");
		return 0;
	}

	curr = get_unaligned_le16(buf);
	DBG("curr = %x \n",curr);
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
static int bq27510_battery_rsoc(struct bq27510_device_info *di)
{
	int ret;
	int rsoc = 0;
	#if 0
	int nvcap = 0,facap = 0,remcap=0,fccap=0,full=0,cnt=0;
	int art = 0, artte = 0, ai = 0, tte = 0, ttf = 0, si = 0;
	int stte = 0, mli = 0, mltte = 0, ae = 0, ap = 0, ttecp = 0, cc = 0;
	#endif
	u8 buf[2];

	#if defined (CONFIG_NO_BATTERY_IC)
		return 100;
	#endif
	if(virtual_battery_enable == 1)
		return 50/*100*/;
	
	ret = bq27510_read(di->client,BQ27500_REG_SOC,buf,2); 
	if (ret<0) {
		dev_err(di->dev, "error reading relative State-of-Charge\n");
		return ret;
	}
	rsoc = get_unaligned_le16(buf);
	DBG("Enter:%s %d--rsoc = %d\n",__FUNCTION__,__LINE__,rsoc);

	#if defined (CONFIG_NO_BATTERY_IC)
	rsoc = 100;
	#endif
	#if 0     //other register information, for debug use
	ret = bq27510_read(di->client,0x0c,buf,2);		//NominalAvailableCapacity
	nvcap = get_unaligned_le16(buf);
	DBG("\nEnter:%s %d--nvcap = %d\n",__FUNCTION__,__LINE__,nvcap);
	ret = bq27510_read(di->client,0x0e,buf,2);		//FullAvailableCapacity
	facap = get_unaligned_le16(buf);
	DBG("Enter:%s %d--facap = %d\n",__FUNCTION__,__LINE__,facap);
	ret = bq27510_read(di->client,0x10,buf,2);		//RemainingCapacity
	remcap = get_unaligned_le16(buf);
	DBG("Enter:%s %d--remcap = %d\n",__FUNCTION__,__LINE__,remcap);
	ret = bq27510_read(di->client,0x12,buf,2);		//FullChargeCapacity
	fccap = get_unaligned_le16(buf);
	DBG("Enter:%s %d--fccap = %d\n",__FUNCTION__,__LINE__,fccap);
	ret = bq27510_read(di->client,0x3c,buf,2);		//DesignCapacity
	full = get_unaligned_le16(buf);
	DBG("Enter:%s %d--DesignCapacity = %d\n",__FUNCTION__,__LINE__,full);
	
	buf[0] = 0x00;						//CONTROL_STATUS
	buf[1] = 0x00;
	bq27510_write(di->client,0x00,buf,2);
	ret = bq27510_read(di->client,0x00,buf,2);
	cnt = get_unaligned_le16(buf);
	DBG("Enter:%s %d--Control status = %x\n",__FUNCTION__,__LINE__,cnt);

	ret = bq27510_read(di->client,0x02,buf,2);		//AtRate
	art = get_unaligned_le16(buf);
	DBG("Enter:%s %d--AtRate = %d\n",__FUNCTION__,__LINE__,art);
	ret = bq27510_read(di->client,0x04,buf,2);		//AtRateTimeToEmpty
	artte = get_unaligned_le16(buf);
	DBG("Enter:%s %d--AtRateTimeToEmpty = %d\n",__FUNCTION__,__LINE__,artte);
	ret = bq27510_read(di->client,0x14,buf,2);		//AverageCurrent
	ai = get_unaligned_le16(buf);
	DBG("Enter:%s %d--AverageCurrent = %d\n",__FUNCTION__,__LINE__,ai);
	ret = bq27510_read(di->client,0x16,buf,2);		//TimeToEmpty
	tte = get_unaligned_le16(buf);
	DBG("Enter:%s %d--TimeToEmpty = %d\n",__FUNCTION__,__LINE__,tte);
	ret = bq27510_read(di->client,0x18,buf,2);		//TimeToFull
	ttf = get_unaligned_le16(buf);
	DBG("Enter:%s %d--TimeToFull = %d\n",__FUNCTION__,__LINE__,ttf);
	ret = bq27510_read(di->client,0x1a,buf,2);		//StandbyCurrent
	si = get_unaligned_le16(buf);
	DBG("Enter:%s %d--StandbyCurrent = %d\n",__FUNCTION__,__LINE__,si);
	ret = bq27510_read(di->client,0x1c,buf,2);		//StandbyTimeToEmpty
	stte = get_unaligned_le16(buf);
	DBG("Enter:%s %d--StandbyTimeToEmpty = %d\n",__FUNCTION__,__LINE__,stte);
	ret = bq27510_read(di->client,0x1e,buf,2);		//MaxLoadCurrent
	mli = get_unaligned_le16(buf);
	DBG("Enter:%s %d--MaxLoadCurrent = %d\n",__FUNCTION__,__LINE__,mli);
	ret = bq27510_read(di->client,0x20,buf,2);		//MaxLoadTimeToEmpty
	mltte = get_unaligned_le16(buf);
	DBG("Enter:%s %d--MaxLoadTimeToEmpty = %d\n",__FUNCTION__,__LINE__,mltte);
	ret = bq27510_read(di->client,0x22,buf,2);		//AvailableEnergy
	ae = get_unaligned_le16(buf);
	DBG("Enter:%s %d--AvailableEnergy = %d\n",__FUNCTION__,__LINE__,ae);
	ret = bq27510_read(di->client,0x24,buf,2);		//AveragePower
	ap = get_unaligned_le16(buf);
	DBG("Enter:%s %d--AveragePower = %d\n",__FUNCTION__,__LINE__,ap);
	ret = bq27510_read(di->client,0x26,buf,2);		//TTEatConstantPower
	ttecp = get_unaligned_le16(buf);
	DBG("Enter:%s %d--TTEatConstantPower = %d\n",__FUNCTION__,__LINE__,ttecp);
	ret = bq27510_read(di->client,0x2a,buf,2);		//CycleCount
	cc = get_unaligned_le16(buf);
	DBG("Enter:%s %d--CycleCount = %d\n",__FUNCTION__,__LINE__,cc);
	#endif
	return rsoc;
}

static int bq27510_battery_status(struct bq27510_device_info *di,
				  union power_supply_propval *val)
{
	u8 buf[2];
	int flags = 0;
	int status;
	int ret;

	#if defined (CONFIG_NO_BATTERY_IC)
		val->intval = POWER_SUPPLY_STATUS_FULL;
	return 0;
	#endif

	if(virtual_battery_enable == 1)
	{
		val->intval = POWER_SUPPLY_STATUS_FULL;
		return 0;
	}
	ret = bq27510_read(di->client,BQ27x00_REG_FLAGS, buf, 2);
	if (ret < 0) {
		dev_err(di->dev, "error reading flags\n");
		return ret;
	}
	flags = get_unaligned_le16(buf);
	DBG("Enter:%s %d--status = %x\n",__FUNCTION__,__LINE__,flags);
	if (flags & BQ27500_FLAG_FC)
		status = POWER_SUPPLY_STATUS_FULL;
	else if (flags & BQ27500_FLAG_DSC)
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	else
		status = POWER_SUPPLY_STATUS_CHARGING;

	val->intval = status;
	return 0;
}

static int bq27510_health_status(struct bq27510_device_info *di,
				  union power_supply_propval *val)
{
	u8 buf[2];
	int flags = 0;
	int status;
	int ret;
	
	#if defined (CONFIG_NO_BATTERY_IC)
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
	return 0;
	#endif

	if(virtual_battery_enable == 1)
	{
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	}
	ret = bq27510_read(di->client,BQ27x00_REG_FLAGS, buf, 2);
	if (ret < 0) {
		dev_err(di->dev, "error reading flags\n");
		return ret;
	}
	flags = get_unaligned_le16(buf);
	DBG("Enter:%s %d--status = %x\n",__FUNCTION__,__LINE__,flags);
	if ((flags & BQ27500_FLAG_OTD)||(flags & BQ27500_FLAG_OTC))
		status = POWER_SUPPLY_HEALTH_OVERHEAT;
	else
		status = POWER_SUPPLY_HEALTH_GOOD;

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
	DBG("Enter:%s %d--tval=%d\n",__FUNCTION__,__LINE__,tval);
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
	DBG("Enter:%s %d psp= %d\n",__FUNCTION__,__LINE__,psp);
	
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
		val->intval = bq27510_battery_rsoc(di);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = bq27510_battery_temperature(di);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;	
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = bq27510_health_status(di, val);
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

static int rk29_ac_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	int ret = 0;
	struct bq27510_device_info *di = container_of(psy, struct bq27510_device_info, ac);
	
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS){
			if(gpio_get_value(di->dc_check_pin))
				val->intval = 0;	/*discharging*/
			else
				val->intval = 1;	/*charging*/
		}
		DBG("%s:%d val->intval = %d\n",__FUNCTION__,__LINE__,val->intval);
		break;
		
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static void bq27510_powersupply_init(struct bq27510_device_info *di)
{
	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = bq27510_battery_props;
	di->bat.num_properties = ARRAY_SIZE(bq27510_battery_props);
	di->bat.get_property = bq27510_battery_get_property;
	
	di->ac.name = "ac";
	di->ac.type = POWER_SUPPLY_TYPE_MAINS;
	di->ac.properties = rk29_ac_props;
	di->ac.num_properties = ARRAY_SIZE(rk29_ac_props);
	di->ac.get_property = rk29_ac_get_property;
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

static void bq27510_set(void)
{
	struct bq27510_device_info *di;
        int i = 0;
	u8 buf[2];

	di = bq27510_di;
        printk("enter 0x41\n");
	buf[0] = 0x41;
	buf[1] = 0x00;
	bq27510_write(di->client,0x00,buf,2);
	
        msleep(1500);
		
        printk("enter 0x21\n");
	buf[0] = 0x21;
	buf[1] = 0x00;
	bq27510_write(di->client,0x00,buf,2);

	buf[0] = 0;
	buf[1] = 0;
	bq27510_read(di->client,0x00,buf,2);

      	// printk("%s: Enter:BUF[0]= 0X%x   BUF[1] = 0X%x\n",__FUNCTION__,buf[0],buf[1]);

      	while((buf[0] & 0x04)&&(i<5))	
       	{
        	printk("enter more 0x21 times i = %d\n",i);
              	mdelay(1000);
       		buf[0] = 0x21;
		buf[1] = 0x00;
		bq27510_write(di->client,0x00,buf,2);

		buf[0] = 0;
		buf[1] = 0;
		bq27510_read(di->client,0x00,buf,2);
		i++;
       	}

      	if(i>5)
	   	printk("write 0x21 error\n");
	else
		printk("bq27510 write 0x21 success\n");
}

static int bq27510_battery_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct bq27510_device_info *di;
	int retval = 0;
	struct bq27510_platform_data *pdata;
	
	pdata = client->dev.platform_data;
	
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
	
	di->bat_num = pdata->bat_num;
	di->dc_check_pin = pdata->dc_check_pin;
	
	if (pdata->init_dc_check_pin)
		pdata->init_dc_check_pin( );
	
	bq27510_powersupply_init(di);
	
	retval = power_supply_register(&client->dev, &di->bat);
	if (retval) {
		dev_err(&client->dev, "failed to register battery\n");
		goto batt_failed_4;
	}
	bq27510_di = di;
	retval = power_supply_register(&client->dev, &di->ac);
	if (retval) {
		dev_err(&client->dev, "failed to register ac\n");
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

static const struct i2c_device_id bq27510_id[] = {
	{ "bq27510", 0 },
};

static struct i2c_driver bq27510_battery_driver = {
	.driver = {
		.name = "bq27510",
	},
	.probe = bq27510_battery_probe,
	.remove = bq27510_battery_remove,
	.id_table = bq27510_id,
};

static int __init bq27510_battery_init(void)
{
	int ret;
	struct proc_dir_entry * battery_proc_entry;
	
	ret = i2c_add_driver(&bq27510_battery_driver);
	if (ret)
		printk(KERN_ERR "Unable to register BQ27510 driver\n");
	
	battery_proc_entry = proc_create("driver/power",0777,NULL,&battery_proc_fops);
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
