/* drivers/power/rk2818_battery.c
 *
 * battery detect driver for the rk2818 
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
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/gpio.h>
#include <mach/adc.h>
#include <mach/iomux.h>
#include <mach/board.h>
#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

/*******************以下参数可以修改******************************/
#define	TIMER_MS_COUNTS		50		//定时器的长度ms
#define	SLOPE_SECOND_COUNTS	120		//统计电压斜率的时间间隔s
#define	TIME_UPDATE_STATUS	5000	//更新电池状态的时间间隔ms
#define BATT_MAX_VOL_VALUE	4180	//满电时的电池电压	 FOR A7
#define	BATT_ZERO_VOL_VALUE  3500	//关机时的电池电压
#define BATT_NOMAL_VOL_VALUE  3800
#define THRESHOLD_VOLTAGE_LEVEL0          4050
#define THRESHOLD_VOLTAGE_LEVEL1          3950
#define THRESHOLD_VOLTAGE_LEVEL2          3850
#define THRESHOLD_VOLTAGE_LEVEL3          BATT_ZERO_VOL_VALUE
#define	THRESHOLD_SLOPE_HIGH		10	//斜率值 = 电压降低的速度
#define	THRESHOLD_SLOPE_MID			5	//<	THRESHOLD_SLOPE_HIGH	
#define	THRESHOLD_SLOPE_LOW			0	//< THRESHOLD_SLOPE_MID

/*************************************************************/
#define CHN_BAT_ADC 	0
#define CHN_USB_ADC 	2
#define BATT_LEVEL_EMPTY	0
#define BATT_PRESENT_TRUE	 1
#define BATT_PRESENT_FALSE  0
#define BAT_1V2_VALUE	1270

#define BAT_LOADER_STATUS		0	//用电状态
#define BAT_CHANGE_STATUS		1	//波动状态
#define BAT_CHARGE_STATUS		2	//充电状态
#define	BAT_RELEASE_STATUS		3	//电池耗尽状态

#define	SLOPE_HIGH_LEVEL		0	//电压变化斜率等级
#define	SLOPE_MID_LEVEL			1
#define	SLOPE_LOW_LEVEL			2

#define	VOLTAGE_HIGH_LEVEL		0	//电压高低等级
#define	VOLTAGE_MID_LEVEL		1
#define	VOLTAGE_LOW_LEVEL		2
#define	VOLTAGE_RELEASE_LEVEL	3

#define	NUM_VOLTAGE_SAMPLE	((1000*SLOPE_SECOND_COUNTS) / TIMER_MS_COUNTS)	//存储的采样点个数

static int gBatFullFlag =  0;

static int gBatLastStatus = 0;
static int gBatStatus =  POWER_SUPPLY_STATUS_UNKNOWN;
static int gBatHealth = POWER_SUPPLY_HEALTH_GOOD;
static int gBatLastPresent = 0;
static int gBatPresent = BATT_PRESENT_TRUE;
static int gBatLastVoltage =  0;
static int gBatVoltage =  BATT_NOMAL_VOL_VALUE;
static int gBatLastCapacity = 0;
static int gBatCapacity = ((BATT_NOMAL_VOL_VALUE-BATT_ZERO_VOL_VALUE)*100/(BATT_MAX_VOL_VALUE-BATT_ZERO_VOL_VALUE));

static int gBatVoltageSamples[NUM_VOLTAGE_SAMPLE+2]; //add 2 to handle one bug
static int gBatSlopeValue = 0;
static int gBatVoltageValue[2]={0,0};
static int *pSamples = &gBatVoltageSamples[0];		//采样点指针
static int gFlagLoop = 0;		//采样足够标志
static int gNumSamples = 0;
static int gNumCharge = 0;
static int gMaxCharge = 0;
static int gNumLoader = 0;
static int gMaxLoader = 0;

static int gBatSlopeLevel = SLOPE_LOW_LEVEL;
static int gBatVoltageLevel = VOLTAGE_MID_LEVEL;
static int gBatUseStatus = BAT_LOADER_STATUS;	

static struct regulator *pChargeregulator;
static int gVbuscharge = 0;

extern int dwc_vbus_status(void);
extern int get_msc_connect_flag(void);

struct rk2818_battery_data {
	int irq;
	spinlock_t lock;
	struct work_struct 	timer_work;
	struct timer_list timer;
	struct power_supply battery;
	struct power_supply usb;
	struct power_supply ac;

	int charge_ok_pin;
	int charge_ok_level;
	
	int adc_bat_divider;
	int bat_max;
	int bat_min;
};


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
 struct regulator * rdev = pChargeregulator;
	//DBG("gAdcValue[CHN_USB_ADC]=%d\n",gAdcValue[CHN_USB_ADC]);
    /*if(gAdcValue[CHN_USB_ADC] > 250) 
    {   //about 0.5V 
        return 1;
    } 
    else */
    if((1 == dwc_vbus_status())&& (0 == get_msc_connect_flag())) 
    {
        DBG("CHARGE!\n");
        if(gVbuscharge !=1) {
            if(!IS_ERR(rdev))
                regulator_set_current_limit(rdev,0,1200000);
        }
        gVbuscharge = 1;
        return 1;
    } 
    else 
    {
        DBG("NOT CHARGING!\n");
        if(gVbuscharge !=0 ) {
            if(!IS_ERR(rdev))
            regulator_set_current_limit(rdev,0,475000);     
        }
        gVbuscharge = 0;
        return 0;
    }

}

static void rk2818_get_bat_status(struct rk2818_battery_data *bat)
{
	if(rk2818_get_charge_status() == 1)
	{
	    //local_irq_disable();
	    if (gBatFullFlag == 0) {
            gBatStatus = POWER_SUPPLY_STATUS_CHARGING;
	    } else {
            gBatStatus = POWER_SUPPLY_STATUS_FULL;
	    }
	    //local_irq_enable();
        DBG("Battery is Charging!\n");
	}
	else {
	    gBatFullFlag = 0;
        gBatStatus = POWER_SUPPLY_STATUS_NOT_CHARGING;
        DBG("Battery is Not Charging!\n");
	}
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
	int i,*pSamp,*pStart = &gBatVoltageSamples[0],num = 0;
	int temp[2] = {0,0};
	value = gAdcValue[CHN_BAT_ADC];
	if(0 != gAdcValue[3])
#ifdef 	CONFIG_MACH_RAHO
    gBatVoltage = (value * BAT_1V2_VALUE * 3)/(gAdcValue[3]*2);
#else    
	gBatVoltage = (value * BAT_1V2_VALUE * 2)/gAdcValue[3];	// channel 3 is about 1.42v,need modified
#endif    
    
	/*消除毛刺电压*/
	if(gBatVoltage >= BATT_MAX_VOL_VALUE + 10)
		gBatVoltage = BATT_MAX_VOL_VALUE + 10;
	else if(gBatVoltage <= BATT_ZERO_VOL_VALUE - 10)
		gBatVoltage = BATT_ZERO_VOL_VALUE - 10;
	
	*pSamples = gBatVoltage;
	num = ++pSamples - pStart;
	if(num > NUM_VOLTAGE_SAMPLE)
	{
		pSamples = pStart;
		gFlagLoop = 1;
	}
	
	if(gFlagLoop != 1)		//未采集到 NUM_VOLTAGE_SAMPLE个电压值
	{
		for(i=(num>>1); i<num; i++)
		{
			temp[0] += gBatVoltageSamples[i];
		}

		if(num != 0)
		{
			gBatVoltage = temp[0] / ((num+1)>>1);
			gBatCapacity = ((gBatVoltage - bat->bat_min) * 100) / (bat->bat_max - bat->bat_min);
			if(gBatCapacity >= 100)
			gBatCapacity = 100;
			else if(gBatCapacity < 0)
			gBatCapacity = 0;
		}
		//DBG("gBatVoltage=%d,gBatCapacity=%d,num=%d\n",gBatVoltage,gBatCapacity,num);
	}
	else
	{
		//compute the average voltage after samples-count is larger than NUM_VOLTAGE_SAMPLE
		pSamp = pSamples;
		for(i=0; i<(NUM_VOLTAGE_SAMPLE >> 1); i++)
		{
			temp[0] += *pSamp;
			if((++pSamp - pStart) > NUM_VOLTAGE_SAMPLE)
			pSamp = pStart;
		}
		
		gBatVoltageValue[0] = temp[0] / (NUM_VOLTAGE_SAMPLE >> 1);
		for(i=0; i<(NUM_VOLTAGE_SAMPLE >> 1); i++)
		{
			temp[1] += *pSamp;
			if((++pSamp - pStart) > NUM_VOLTAGE_SAMPLE)
			pSamp = pStart;
		}
		
		gBatVoltageValue[1] = temp[1] / (NUM_VOLTAGE_SAMPLE >> 1);

		gBatVoltage = gBatVoltageValue[1];

		gBatSlopeValue = gBatVoltageValue[0] - gBatVoltageValue[1];
		//DBG("gBatSlopeValue=%d,gBatVoltageValue[1]=%d\n",gBatSlopeValue,gBatVoltageValue[1]);

		if(gBatVoltageValue[1] < BATT_ZERO_VOL_VALUE)
		{
			gBatUseStatus = BAT_RELEASE_STATUS;		//电池耗尽状态
		}
		else
		{
			if(gBatSlopeValue < 0)
			{
				gNumLoader = 0;
					
				//连续多次电压降低率为负表示充电状态
				if(gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_LEVEL0)
				gMaxCharge = 2;
				else if(gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_LEVEL1)
				gMaxCharge = 3;
				else if(gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_LEVEL2)
				gMaxCharge = 4;		
				else if(gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_LEVEL3)
				gMaxCharge = 2;	
				if((++gNumCharge >= gMaxCharge) && (gBatStatus != POWER_SUPPLY_STATUS_NOT_CHARGING))
				{
					gBatUseStatus = BAT_CHARGE_STATUS;		//充电状态
					gNumCharge = gMaxCharge ;
				}
				else
				{
					gBatUseStatus = BAT_CHANGE_STATUS;	//波动状态
				}
				
			}
			else
			{
				gNumCharge = 0;
				//连续多次电压降低率为正表示用电状态
				if(gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_LEVEL0)
				gMaxCharge = 2;
				else if(gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_LEVEL1)
				gMaxCharge = 3;
				else if(gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_LEVEL2)
				gMaxCharge = 4;		
				else if(gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_LEVEL3)
				gMaxLoader = 2;	
				
				if((++gNumLoader >= gMaxLoader) && (gBatStatus == POWER_SUPPLY_STATUS_NOT_CHARGING))
				{		
					gBatUseStatus = BAT_LOADER_STATUS;
					gNumLoader = gMaxLoader;
				}
				else
				{
					gBatUseStatus = BAT_CHANGE_STATUS;	//波动状态
				}

			}
		}
	}

	
}

static void rk2818_get_bat_capacity(struct rk2818_battery_data *bat)
{
	if(gFlagLoop)
	{
		if(gBatUseStatus == BAT_LOADER_STATUS)
		{
			//用电状态下出现负载变小容量变大时，不更新容量值
			if((gBatLastVoltage == 0) || (gBatVoltage <= gBatLastVoltage))
			{
				gBatCapacity = ((gBatVoltage - bat->bat_min) * 100) / (bat->bat_max - bat->bat_min);
				if(gBatCapacity >= 100)
				gBatCapacity = 100;
				else if(gBatCapacity < 0)
				gBatCapacity = 0;
				gBatLastVoltage = gBatVoltage;
			}
	
		}
		else if(gBatUseStatus == BAT_CHARGE_STATUS)
		{
			//充电状态下容量降低时，不更新容量值
			if((gBatLastVoltage == 0) || (gBatVoltage >= gBatLastVoltage))
			{
				gBatCapacity = ((gBatVoltage - bat->bat_min) * 100) / (bat->bat_max - bat->bat_min);
				if(gBatCapacity >= 100)
				gBatCapacity = 100;
				else if(gBatCapacity < 0)
				gBatCapacity = 0;
				gBatLastVoltage = gBatVoltage;
				//DBG("BAT_CHARGE_STATUS\n");
			}

		}

		//变化状态不更新容量
		//DBG("BAT_CHANGE_STATUS\n");
	}
	else
	{
		gBatCapacity = ((gBatVoltage - bat->bat_min) * 100) / (bat->bat_max - bat->bat_min);
		if(gBatCapacity >= 100)
			gBatCapacity = 100;
		else if(gBatCapacity < 0)
			gBatCapacity = 0;
	}
}


static void rk2818_battery_timer_work(struct work_struct *work)
{		
	rk2818_get_bat_status(gBatteryData);
	rk2818_get_bat_health(gBatteryData);
	rk2818_get_bat_present(gBatteryData);
	rk2818_get_bat_voltage(gBatteryData);
	rk2818_get_bat_capacity(gBatteryData);
	
	/*update battery parameter after adc and capacity has been changed*/
	if((gBatStatus != gBatLastStatus) || (gBatPresent != gBatLastPresent) || (gBatCapacity != gBatLastCapacity))
	{
		//gNumSamples = 0;
		gBatLastStatus = gBatStatus;
		gBatLastPresent = gBatPresent;
		gBatLastCapacity = gBatCapacity;
		power_supply_changed(&gBatteryData->battery);

	}
	
}


static void rk2818_batscan_timer(unsigned long data)
{
	gBatteryData->timer.expires  = jiffies + msecs_to_jiffies(TIMER_MS_COUNTS);
	add_timer(&gBatteryData->timer);
	schedule_work(&gBatteryData->timer_work);	
}


static int rk2818_usb_get_property(struct power_supply *psy, 
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	charger_type_t charger;
	charger =  CHARGER_USB;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_USB)
			val->intval = dwc_vbus_status();
		DBG("%s:%d\n",__FUNCTION__,val->intval);
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
	charger =  CHARGER_USB;
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
		{
			if(gAdcValue[CHN_USB_ADC] > 250)
			val->intval = 1;
			else
			val->intval = 0;	
		}
		DBG("%s:%d\n",__FUNCTION__,val->intval);
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
		DBG("gBatStatus=%d\n",val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = gBatHealth;
		DBG("gBatHealth=%d\n",val->intval);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = gBatPresent;
		DBG("gBatPresent=%d\n",val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if(gBatVoltageValue[1] == 0)
		val ->intval = gBatVoltage;
		else
		val ->intval = gBatVoltageValue[1];
		DBG("gBatVoltage=%d\n",val->intval);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;	
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = gBatCapacity;
		DBG("gBatCapacity=%d%%\n",val->intval);
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


#ifdef CONFIG_PM
static int rk2818_battery_suspend(struct platform_device *dev, pm_message_t state)
{
	/* flush all pending status updates */
	flush_scheduled_work();
	return 0;
}

static int rk2818_battery_resume(struct platform_device *dev)
{
	/* things may have changed while we were away */
	schedule_work(&gBatteryData->timer_work);
	return 0;
}
#else
#define rk2818_battery_suspend NULL
#define rk2818_battery_resume NULL
#endif

static irqreturn_t rk2818_battery_interrupt(int irq, void *dev_id)
{
    if((1 == dwc_vbus_status())&& (0 == get_msc_connect_flag())) {//detech when charging
        gBatFullFlag = 1;
    }

    DBG(KERN_INFO "-----battery is full-----\n");

    return 0;
}

static int rk2818_battery_probe(struct platform_device *pdev)
{
	int ret;
	struct rk2818_battery_data *data;
	struct rk2818_battery_platform_data *pdata = pdev->dev.platform_data;
	int irq_flag;

	if (pdata && pdata->io_init) {
		ret = pdata->io_init();
		if (ret) 
			goto err_free_gpio1;		
	}

	ret = gpio_request(pdata->charge_ok_pin, NULL);
	if (ret) {
		printk("failed to request charge_ok gpio\n");
		goto err_free_gpio1;
	}
	
	gpio_pull_updown(pdata->charge_ok_pin, GPIOPullUp);//important
	ret = gpio_direction_input(pdata->charge_ok_pin);
	if (ret) {
		printk("failed to set gpio charge_ok input\n");
		goto err_free_gpio1;
	}
	
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		ret = -ENOMEM;
		goto err_data_alloc_failed;
	}
	spin_lock_init(&data->lock);
	
	memset(gBatVoltageSamples, 0, sizeof(gBatVoltageSamples));
	
	data->battery.properties = rk2818_battery_props;
	data->battery.num_properties = ARRAY_SIZE(rk2818_battery_props);
	data->battery.get_property = rk2818_battery_get_property;
	data->battery.name = "battery";
	data->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	data->adc_bat_divider = 414;
	data->bat_max = BATT_MAX_VOL_VALUE;
	data->bat_min = BATT_ZERO_VOL_VALUE;
	DBG("bat_min = %d\n",data->bat_min);
	
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

	data->charge_ok_pin = pdata->charge_ok_pin;
	data->charge_ok_level = pdata->charge_ok_level;

	irq_flag = (!pdata->charge_ok_level) ? IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING;
	ret = request_irq(gpio_to_irq(pdata->charge_ok_pin), rk2818_battery_interrupt, irq_flag, "rk2818_battery", data);
	if (ret) {
		printk("failed to request irq\n");
		goto err_irq_failed;
	}

	ret = power_supply_register(&pdev->dev, &data->ac);
	if (ret)
	{
		printk(KERN_INFO "fail to power_supply_register\n");
		goto err_ac_failed;
	}

	ret = power_supply_register(&pdev->dev, &data->usb);
	if (ret)
	{
		printk(KERN_INFO "fail to power_supply_register\n");
		goto err_usb_failed;
	}

	ret = power_supply_register(&pdev->dev, &data->battery);
	if (ret)
	{
		printk(KERN_INFO "fail to power_supply_register\n");
		goto err_battery_failed;
	}
	platform_set_drvdata(pdev, data);
	

	pChargeregulator = regulator_get(&pdev->dev, "battery");
	if(IS_ERR(pChargeregulator))
		printk(KERN_ERR"fail to get regulator battery\n");
       else
		regulator_set_current_limit(pChargeregulator,0,475000);

	INIT_WORK(&data->timer_work, rk2818_battery_timer_work);
	gBatteryData = data;
	
	setup_timer(&data->timer, rk2818_batscan_timer, (unsigned long)data);
	data->timer.expires  = jiffies+100;
	add_timer(&data->timer);
	printk(KERN_INFO "rk2818_battery: driver initialized\n");
	
	return 0;

err_battery_failed:
	power_supply_unregister(&data->usb);
err_usb_failed:
	power_supply_unregister(&data->ac);
err_ac_failed:
	free_irq(gpio_to_irq(pdata->charge_ok_pin), data);
err_irq_failed:
	kfree(data);
err_data_alloc_failed:

err_free_gpio1:
	gpio_free(pdata->charge_ok_pin);
	return ret;
}

static int rk2818_battery_remove(struct platform_device *pdev)
{
	struct rk2818_battery_data *data = platform_get_drvdata(pdev);
	struct rk2818_battery_platform_data *pdata = pdev->dev.platform_data;

	power_supply_unregister(&data->battery);
	power_supply_unregister(&data->usb);
	power_supply_unregister(&data->ac);
	free_irq(data->irq, data);
	gpio_free(pdata->charge_ok_pin);
	kfree(data);
	gBatteryData = NULL;
	return 0;
}

static struct platform_driver rk2818_battery_device = {
	.probe		= rk2818_battery_probe,
	.remove		= rk2818_battery_remove,
	.suspend	= rk2818_battery_suspend,
	.resume		= rk2818_battery_resume,
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

MODULE_DESCRIPTION("Battery detect driver for the rk2818");
MODULE_AUTHOR("luowei lw@rock-chips.com");
MODULE_LICENSE("GPL");

