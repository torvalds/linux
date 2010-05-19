/* drivers/power/rk2818_battery.c
 *
 * Power supply driver for the rk2818 emulator
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <mach/adc.h>

#if 1
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

#define CHN_BAT_ADC 	0
#define CHN_USB_ADC 	2
#define BATT_LEVEL_EMPTY	0
#define BATT_PRESENT_TRUE	 1
#define BATT_PRESENT_FALSE  0
#define BATT_NOMAL_VOL_VALUE	4000

static int gBatStatus =  POWER_SUPPLY_STATUS_UNKNOWN;
static int gBatHealth = POWER_SUPPLY_HEALTH_GOOD;
static int gBatCapacity = BATT_LEVEL_EMPTY;
static int gBatPresent = BATT_PRESENT_TRUE;
static int gBatVoltage =  BATT_NOMAL_VOL_VALUE;
extern int dwc_vbus_status(void);

struct rk2818_battery_data {
	int irq;
	spinlock_t lock;
	struct timer_list timer;
	struct power_supply battery;
	struct power_supply usb;
	struct power_supply ac;
	
	int adc_bat_divider;
	int bat_max;
	int bat_min;
};

#define RK2818_BATTERY_WRITE(data, addr, x)   (writel(x, data->reg_base + addr))


/* temporary variable used between rk2818_battery_probe() and rk2818_battery_open() */
static struct rk2818_battery_data *gBatteryData;

enum {
	BATTERY_STATUS          = 0,
	BATTERY_HEALTH          = 1,
	BATTERY_PRESENT         = 2,
	BATTERY_CAPACITY        = 3,
	BATTERY_AC_ONLINE       = 4,
	BATTERY_STATUS_CHANGED	= 5,
	AC_STATUS_CHANGED   	= 6,
	BATTERY_INT_STATUS	    = 7,
	BATTERY_INT_ENABLE	    = 8,
};

typedef enum {
	CHARGER_BATTERY = 0,
	CHARGER_USB,
	CHARGER_AC
} charger_type_t;

static int rk2818_get_charge_status(void)
{
	//return dwc_vbus_status();
	return 1;
}

static void rk2818_get_bat_status(struct rk2818_battery_data *bat)
{
	if(rk2818_get_charge_status() == 1)
	gBatStatus = POWER_SUPPLY_STATUS_CHARGING;
	else
	gBatStatus = POWER_SUPPLY_STATUS_NOT_CHARGING;	
}

static void rk2818_get_bat_health(struct rk2818_battery_data *bat)
{
	gBatHealth = POWER_SUPPLY_HEALTH_GOOD;
}

static void rk2818_get_bat_present(struct rk2818_battery_data *bat)
{
	if(gBatVoltage < bat->bat_min)
	gBatPresent = 0;
	else
	gBatPresent = 1;
}

static void rk2818_get_bat_voltage(struct rk2818_battery_data *bat)
{
	unsigned long value;
	value = gAdcValue[CHN_BAT_ADC];
	gBatVoltage = value * 1000000 / bat->adc_bat_divider;
}

static void rk2818_get_bat_capacity(struct rk2818_battery_data *bat)
{
	gBatCapacity = (gBatVoltage * 100) / bat->bat_max;
}


static void rk2818_batscan_timer(unsigned long data)
{
	gBatteryData->timer.expires  = jiffies + msecs_to_jiffies(20);
	add_timer(&gBatteryData->timer);	
	rk2818_get_bat_status(gBatteryData);
	rk2818_get_bat_health(gBatteryData);
	rk2818_get_bat_present(gBatteryData);
	rk2818_get_bat_voltage(gBatteryData);
	rk2818_get_bat_capacity(gBatteryData);

}


static int rk2818_usb_get_property(struct power_supply *psy, 
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	charger_type_t charger;
	//todo 
	charger =  CHARGER_USB;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_USB)
			val->intval = (charger ==  CHARGER_AC ? 1 : 0);
		else
			val->intval = 0;
		break;
	default:
		return -EINVAL;
	}
	
	return 0;

}


static int rk2818_ac_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
//	struct rk2818_battery_data *data = container_of(psy,
//		struct rk2818_battery_data, ac);
	int ret = 0;
	charger_type_t charger;
	//todo 
	charger =  CHARGER_USB;
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
			val->intval = (charger ==  CHARGER_AC ? 1 : 0);
		else if (psy->type == POWER_SUPPLY_TYPE_USB)
			val->intval = (charger ==  CHARGER_USB ? 1 : 0);
		else
			val->intval = 0;
		//val->intval = RK2818_BATTERY_READ(data, BATTERY_AC_ONLINE);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int rk2818_battery_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct rk2818_battery_data *data = container_of(psy,
		struct rk2818_battery_data, battery);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = gBatStatus;
		DBG("gBatStatus=0x%x\n",val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = gBatHealth;
		DBG("gBatHealth=0x%x\n",val->intval);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = gBatPresent;
		DBG("gBatPresent=0x%x\n",val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val ->intval = gBatVoltage;
		DBG("gBatVoltage=0x%x\n",val->intval);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;	
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = gBatCapacity;
		DBG("gBatCapacity=0x%x\n",val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = data->bat_max;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = data->bat_min;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property rk2818_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
};

static enum power_supply_property rk2818_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};


static enum power_supply_property rk2818_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};
#if 0
static irqreturn_t rk2818_battery_interrupt(int irq, void *dev_id)
{

	unsigned long irq_flags;
	struct rk2818_battery_data *data = dev_id;
	uint32_t status;

	spin_lock_irqsave(&data->lock, irq_flags);
	/* read status flags, which will clear the interrupt */
	//status = RK2818_BATTERY_READ(data, BATTERY_INT_STATUS);
	status &= BATTERY_INT_MASK;

	if (status & BATTERY_STATUS_CHANGED)
		power_supply_changed(&data->battery);
	if (status & AC_STATUS_CHANGED)
		power_supply_changed(&data->ac);

	spin_unlock_irqrestore(&data->lock, irq_flags);
	return status ? IRQ_HANDLED : IRQ_NONE;

	return IRQ_HANDLED;
}
#endif

static int rk2818_battery_probe(struct platform_device *pdev)
{
	int ret;
	//struct resource *r;
	struct rk2818_battery_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		ret = -ENOMEM;
		goto err_data_alloc_failed;
	}
	spin_lock_init(&data->lock);

	data->battery.properties = rk2818_battery_props;
	data->battery.num_properties = ARRAY_SIZE(rk2818_battery_props);
	data->battery.get_property = rk2818_battery_get_property;
	data->battery.name = "battery";
	data->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	//data->adc_bat_divider = gAdcValue[3];
	data->adc_bat_divider = 414;
	DBG("adc_bat_divider=%d\n",data->adc_bat_divider);
	data->bat_max = 4310000;
	data->bat_min = 1551 * 1000000 / 414;

	data->usb.properties = rk2818_usb_props;
	data->usb.num_properties = ARRAY_SIZE(rk2818_ac_props);
	data->usb.get_property = rk2818_usb_get_property;
	data->usb.name = "usb";
	data->usb.type = POWER_SUPPLY_TYPE_USB;

	data->ac.properties = rk2818_ac_props;
	data->ac.num_properties = ARRAY_SIZE(rk2818_ac_props);
	data->ac.get_property = rk2818_ac_get_property;
	data->ac.name = "ac";
	data->ac.type = POWER_SUPPLY_TYPE_MAINS;
	
#if 0
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		printk(KERN_ERR "%s: platform_get_resource failed\n", pdev->name);
		ret = -ENODEV;
		goto err_no_io_base;
	}

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0) {
		printk(KERN_ERR "%s: platform_get_irq failed\n", pdev->name);
		ret = -ENODEV;
		goto err_no_irq;
	}

	ret = request_irq(data->irq, rk2818_battery_interrupt, IRQF_SHARED, pdev->name, data);
	if (ret)
		goto err_request_irq_failed;
#endif
	ret = power_supply_register(&pdev->dev, &data->ac);
	if (ret)
		goto err_ac_failed;

	ret = power_supply_register(&pdev->dev, &data->usb);
	if (ret)
		goto err_usb_failed;

	ret = power_supply_register(&pdev->dev, &data->battery);
	if (ret)
		goto err_battery_failed;

	platform_set_drvdata(pdev, data);
	gBatteryData = data;

	setup_timer(&data->timer, rk2818_batscan_timer, (unsigned long)data);
	data->timer.expires  = jiffies+50;
	add_timer(&data->timer);
	printk(KERN_INFO "rk2818_battery: driver initialized\n");
	//RK2818_BATTERY_WRITE(data, BATTERY_INT_ENABLE, BATTERY_INT_MASK);
	
	return 0;

err_battery_failed:
	power_supply_unregister(&data->usb);
err_usb_failed:
	power_supply_unregister(&data->ac);
err_ac_failed:
	//free_irq(data->irq, data);
//err_request_irq_failed:
//err_no_irq:
//err_no_io_base:
	kfree(data);
err_data_alloc_failed:
	return ret;
}

static int rk2818_battery_remove(struct platform_device *pdev)
{
	struct rk2818_battery_data *data = platform_get_drvdata(pdev);

	power_supply_unregister(&data->battery);
	power_supply_unregister(&data->ac);

	free_irq(data->irq, data);
	kfree(data);
	gBatteryData = NULL;
	return 0;
}

static struct platform_driver rk2818_battery_device = {
	.probe		= rk2818_battery_probe,
	.remove		= rk2818_battery_remove,
	.driver = {
		.name = "rk2818-battery",
		.owner	= THIS_MODULE,
	}
};

static int __init rk2818_battery_init(void)
{
	return platform_driver_register(&rk2818_battery_device);
}

static void __exit rk2818_battery_exit(void)
{
	platform_driver_unregister(&rk2818_battery_device);
}

module_init(rk2818_battery_init);
module_exit(rk2818_battery_exit);

MODULE_AUTHOR("Mike Lockwood lockwood@android.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Battery driver for the Goldfish emulator");

