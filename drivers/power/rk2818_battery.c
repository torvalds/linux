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
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <mach/adc.h>

#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

#define CHN_BAT_ADC 	0
#define CHN_USB_ADC 	2
#define BATT_LEVEL_EMPTY	0
#define BATT_PRESENT_TRUE	 1
#define BATT_PRESENT_FALSE  0
#define BATT_NOMAL_VOL_VALUE	3900

static int gBatStatus =  POWER_SUPPLY_STATUS_UNKNOWN;
static int gBatHealth = POWER_SUPPLY_HEALTH_GOOD;
static int gBatLastCapacity = 0;
static int gBatCapacity = BATT_LEVEL_EMPTY;
static int gBatPresent = BATT_PRESENT_TRUE;
static int gBatVoltage =  BATT_NOMAL_VOL_VALUE;

#if 0
#define NUM_BAT 	3
#define NUM_ELECTRICITY 	10

#define BAT_CAP_1500MAH		0
#define BAT_CAP_1200MAH		1
#define BAT_CAP_1100MAH		2

#define ELECTRICITY_1000MA 	0
#define ELECTRICITY_900MA 	1
#define ELECTRICITY_800MA 	2
#define ELECTRICITY_700MA 	3
#define ELECTRICITY_600MA 	4
#define ELECTRICITY_500MA 	5
#define ELECTRICITY_400MA 	6
#define ELECTRICITY_300MA 	7
#define ELECTRICITY_200MA 	8
#define ELECTRICITY_100MA 	9

#define BAT_SELECT	BAT_CAP_1200MAH
#define ELECTRICITY_SELECT	ELECTRICITY_200MA

//about 10 minutes before battery is exhaust for different bat and electricity
static int BatMinVoltage[NUM_BAT][NUM_ELECTRICITY] = 
{
{3410, 3450, 3480, 3460, 3480, 3500, 3510, 3470, 3420, 3430},
{3360, 3400, 3410, 3400, 3430, 3430, 3460, 3480, 3440, 3330},
{3360, 3400, 3410, 3410, 3440, 3460, 3480, 3470, 3440, 3360},
};

int gBatZeroVol	= BatMinVoltage[BAT_SELECT][ELECTRICITY_SELECT];
int gBatMaxVol	= 4300;

#else
int gBatMaxVol	= 4200;
int gBatZeroVol	= 3350;

#endif

/*******************以下参数可以修改******************************/
#define	TIMER_MS_COUNTS		50		//定时器的长度ms
#define	SLOPE_SECOND_COUNTS	60		//统计电压斜率的时间间隔s
#define	TIME_UPDATE_STATUS	3000	//更新电池状态的时间间隔ms
#define	THRESHOLD_VOLTAGE_HIGH		3850
#define	THRESHOLD_VOLTAGE_MID		3450
#define	THRESHOLD_VOLTAGE_LOW		gBatZeroVol
#define	THRESHOLD_SLOPE_HIGH		8	//真实斜率值
#define	THRESHOLD_SLOPE_MID			3		
#define	THRESHOLD_SLOPE_LOW			0

/*************************************************************/
#define LODER_CHARGE_LEVEL		0	//负荷状态等级
#define	LODER_HIGH_LEVEL		1
#define	LODER_MID_LEVEL			2
#define	LOADER_RELEASE_LEVEL	3	//电池即将耗尽状态

#define	SLOPE_HIGH_LEVEL		0	//电压变化斜率等级
#define	SLOPE_MID_LEVEL			1
#define	SLOPE_LOW_LEVEL			2

#define	VOLTAGE_HIGH_LEVEL		0	//电压高低等级
#define	VOLTAGE_MID_LEVEL		1
#define	VOLTAGE_LOW_LEVEL		2
#define	VOLTAGE_RELEASE_LEVEL	3

#define	NUM_VOLTAGE_SAMPLE	((1000*SLOPE_SECOND_COUNTS) / TIMER_MS_COUNTS)	//存储的采样点个数
int gBatVoltageSamples[NUM_VOLTAGE_SAMPLE];
int gBatSlopeValue = 0;
int	gBatVoltageValue[2]={0,0};
int *pSamples = &gBatVoltageSamples[0];		//采样点指针
int gFlagLoop = 0;		//采样足够标志
int gNumSamples = 0;

int gBatSlopeLevel = SLOPE_LOW_LEVEL;
int gBatVoltageLevel = VOLTAGE_MID_LEVEL;
int gBatLastLoaderLevel = LODER_MID_LEVEL;
int gBatLoaderLevel = LODER_MID_LEVEL;	


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
	int i,*pSamp,*pStart = &gBatVoltageSamples[0];
	int temp[2] = {0,0};
	value = gAdcValue[CHN_BAT_ADC];
	gBatVoltage = (value * 1422 * 2)/gAdcValue[3];	// channel 3 is about 1.42v,need modified
	*pSamples = gBatVoltage;
	if((++pSamples - pStart) > NUM_VOLTAGE_SAMPLE)
	{
		pSamples = pStart;
		gFlagLoop = 1;
	}

	//compute the average voltage after samples-count is larger than NUM_VOLTAGE_SAMPLE
	if(gFlagLoop)
	{
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

		gBatSlopeValue = gBatVoltageValue[0] - gBatVoltageValue[1];	
		//DBG("gBatSlopeValue=%d,gBatVoltageValue[1]=%d\n",gBatSlopeValue,gBatVoltageValue[1]);
		if(gBatSlopeValue >= 0)	//用电状态
		{

			if(gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_HIGH)
			gBatVoltageLevel = 	VOLTAGE_HIGH_LEVEL;	
			else if((gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_MID) && (gBatVoltageValue[1] < THRESHOLD_VOLTAGE_HIGH))
			gBatVoltageLevel = 	VOLTAGE_MID_LEVEL;
			else if((gBatVoltageValue[1] >= THRESHOLD_VOLTAGE_LOW) && (gBatVoltageValue[1] < THRESHOLD_VOLTAGE_MID))
			gBatVoltageLevel = VOLTAGE_LOW_LEVEL;
			else
			gBatVoltageLevel = VOLTAGE_RELEASE_LEVEL;

			if(gBatSlopeValue >= THRESHOLD_SLOPE_HIGH)
			gBatSlopeLevel = SLOPE_HIGH_LEVEL;	
			else if((gBatSlopeValue >= THRESHOLD_SLOPE_MID) && (gBatSlopeValue < THRESHOLD_SLOPE_HIGH))
			gBatSlopeLevel = SLOPE_MID_LEVEL;	
			else if(gBatSlopeValue >= THRESHOLD_SLOPE_LOW)
			gBatSlopeLevel = SLOPE_LOW_LEVEL;
			
			/*电压中且斜率高、 电压高且斜率高或中*/
			if(((gBatVoltageLevel == VOLTAGE_MID_LEVEL) && (gBatSlopeLevel == SLOPE_HIGH_LEVEL)) \
				|| ((gBatVoltageLevel == VOLTAGE_HIGH_LEVEL) && ((gBatSlopeLevel == SLOPE_HIGH_LEVEL) || (gBatSlopeLevel == SLOPE_MID_LEVEL))))
			{
				gBatLoaderLevel = LODER_HIGH_LEVEL;
				DBG("gBatLoaderLevel = LODER_HIGH_LEVEL\n");
			}
			
			/*电压中且斜率中或低、电压高且斜率低、 电压低且斜率低*/
			else if(((gBatVoltageLevel != VOLTAGE_RELEASE_LEVEL) && (gBatSlopeLevel == SLOPE_LOW_LEVEL)) \
				|| ((gBatVoltageLevel == VOLTAGE_MID_LEVEL) && (gBatSlopeLevel == SLOPE_MID_LEVEL)))
			{
				gBatLoaderLevel = LODER_MID_LEVEL;
				DBG("gBatLoaderLevel = LODER_MID_LEVEL\n");
			}
			
			/*电压低且斜率高或中、 电压超低*/
			else if(((gBatVoltageLevel == VOLTAGE_LOW_LEVEL) && ((gBatSlopeLevel == SLOPE_MID_LEVEL) || (gBatSlopeLevel == SLOPE_MID_LEVEL))) \
				|| (gBatVoltageLevel == VOLTAGE_RELEASE_LEVEL))
			{
				gBatLoaderLevel = LOADER_RELEASE_LEVEL;	//电池已耗尽
				DBG("gBatLoaderLevel = LOADER_RELEASE_LEVEL\n");
			}

		}
		else	//充电状态
		{
			//to do
			gBatLoaderLevel = LODER_CHARGE_LEVEL;
		}
		
	}
	
}

static void rk2818_get_bat_capacity(struct rk2818_battery_data *bat)
{
	if(gFlagLoop)
	{
		//出现负载变小时容量变大的情况时，不更新容量值
		if((gBatLastCapacity ==0) \
			|| (gBatLoaderLevel <= gBatLastLoaderLevel) \
			|| ((gBatLoaderLevel > gBatLastLoaderLevel)&&(gBatCapacity <= gBatLastCapacity)))	
		{
			gBatCapacity = ((gBatVoltageValue[1] - bat->bat_min) * 100) / (bat->bat_max - bat->bat_min);
			gBatLastCapacity = gBatCapacity;
			gBatLastLoaderLevel = gBatLoaderLevel;
		}

	}
	else
	{
		gBatCapacity = ((gBatVoltage - bat->bat_min) * 100) / (bat->bat_max - bat->bat_min);
	}
}


static void rk2818_batscan_timer(unsigned long data)
{
	gBatteryData->timer.expires  = jiffies + msecs_to_jiffies(TIMER_MS_COUNTS);
	add_timer(&gBatteryData->timer);	
	rk2818_get_bat_status(gBatteryData);
	rk2818_get_bat_health(gBatteryData);
	rk2818_get_bat_present(gBatteryData);
	rk2818_get_bat_voltage(gBatteryData);
	rk2818_get_bat_capacity(gBatteryData);

	if(++gNumSamples > TIME_UPDATE_STATUS/TIMER_MS_COUNTS)
	{
		gNumSamples = 0;
		if(gBatVoltage != 0)	//update battery parameter after adc
		{
			power_supply_changed(&gBatteryData->battery);
			DBG("voltage has changed\n");
		}
	}
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
		if(gBatLastCapacity == 0)
		val->intval = gBatCapacity;
		else
		val->intval = gBatLastCapacity;	
		DBG("gBatCapacity=%d%\n",val->intval);
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

	memset(gBatVoltageSamples, 0, sizeof(gBatVoltageSamples));
	
	data->battery.properties = rk2818_battery_props;
	data->battery.num_properties = ARRAY_SIZE(rk2818_battery_props);
	data->battery.get_property = rk2818_battery_get_property;
	data->battery.name = "battery";
	data->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	data->adc_bat_divider = 414;
	data->bat_max = gBatMaxVol;
	data->bat_min = gBatZeroVol;
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
	data->timer.expires  = jiffies+500;
	add_timer(&data->timer);
	printk(KERN_INFO "rk2818_battery: driver initialized\n");
	
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

MODULE_DESCRIPTION("Battery detect driver for the rk2818");
MODULE_AUTHOR("luowei lw@rock-chips.com");
MODULE_LICENSE("GPL");

