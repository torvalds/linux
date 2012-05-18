/* drivers/power/rk29_adc_battery.c
 *
 * battery detect driver for the rk29 
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
#include <linux/adc.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/wakelock.h>

#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

static int rk29_battery_dbg_level = 0;
module_param_named(dbg_level, rk29_battery_dbg_level, int, 0644);

/*******************以下参数可以修改******************************/
#define	TIMER_MS_COUNTS		 1000	//定时器的长度ms
//以下参数需要根据实际测试调整
#define	SLOPE_SECOND_COUNTS	               15	//统计电压斜率的时间间隔s
#define	DISCHARGE_MIN_SECOND	               45	//最快放电电1%时间
#define	CHARGE_MIN_SECOND	               45	//最快充电电1%时间
#define	CHARGE_MID_SECOND	               90	//普通充电电1%时间
#define	CHARGE_MAX_SECOND	               250	//最长充电电1%时间
#define   CHARGE_FULL_DELAY_TIMES          10          //充电满检测防抖时间
#define    USBCHARGE_IDENTIFY_TIMES        5           //插入USB混流，pc识别检测时间

#define	NUM_VOLTAGE_SAMPLE	                       ((SLOPE_SECOND_COUNTS * 1000) / TIMER_MS_COUNTS)	 
#define	NUM_DISCHARGE_MIN_SAMPLE	         ((DISCHARGE_MIN_SECOND * 1000) / TIMER_MS_COUNTS)	 
#define	NUM_CHARGE_MIN_SAMPLE	         ((CHARGE_MIN_SECOND * 1000) / TIMER_MS_COUNTS)	    
#define	NUM_CHARGE_MID_SAMPLE	         ((CHARGE_MID_SECOND * 1000) / TIMER_MS_COUNTS)	     
#define	NUM_CHARGE_MAX_SAMPLE	         ((CHARGE_MAX_SECOND * 1000) / TIMER_MS_COUNTS)	  
#define   NUM_CHARGE_FULL_DELAY_TIMES         ((CHARGE_FULL_DELAY_TIMES * 1000) / TIMER_MS_COUNTS)	//充电满状态持续时间长度
#define    NUM_USBCHARGE_IDENTIFY_TIMES      ((USBCHARGE_IDENTIFY_TIMES * 1000) / TIMER_MS_COUNTS)	//充电满状态持续时间长度

#define BAT_2V5_VALUE	                                     2500

//定义ADC采样分压电阻，以实际值为准，单位K
#define BAT_PULL_UP_R                                         300  ////200

#define BAT_PULL_DOWN_R                                    100// 200
#define adc_to_voltage(adc_val)                           ((adc_val * BAT_2V5_VALUE * (BAT_PULL_UP_R + BAT_PULL_DOWN_R)) / (1024 * BAT_PULL_DOWN_R))
#define BATT_NUM                                                   11
#define BATT_FILENAME "/data/bat_last_capacity.dat"

static struct wake_lock batt_wake_lock;


struct batt_vol_cal{
//	u32 disp_cal;
	u32 dis_charge_vol;
	u32 charge_vol;
};

#ifdef CONFIG_BATTERY_RK29_VOL3V8

#define BATT_MAX_VOL_VALUE                              4200               	//满电时的电池电压	 
#define BATT_ZERO_VOL_VALUE                            3400              	//关机时的电池电压
#define BATT_NOMAL_VOL_VALUE                         3800               
static struct batt_vol_cal  batt_table[BATT_NUM] = {
	{3400,3520},
	{3610,3715},
	{3672,3790},
	{3705,3825},
	{3734,3841},
	{3764,3864},
	{3808,3930},
	{3845,3997},
	{3964,4047},
	{4034,4144},
	{4120,4200},
};
/*******************************************************************************/

#else

#define BATT_MAX_VOL_VALUE                              8200               	//满电时的电池电压	 
#define BATT_ZERO_VOL_VALUE                            6800            	//关机时的电池电压
#define BATT_NOMAL_VOL_VALUE                         7600           
static struct batt_vol_cal  batt_table[BATT_NUM] = {
	{6800,7400},  
	{7220,7720},
	{7344,7844},
	{7410,7910},//500
	{7468,7975}, 
	{7528,8044},  
	{7618,8075}, 
	{7744,8100}, //400
	{7900,8180},  
	{8110,8260},   
	{8200 ,8310},//110

};
#endif
/********************************************************************************/

extern int dwc_vbus_status(void);
extern int get_msc_connect_flag(void);

struct rk29_adc_battery_data {
	int irq;
	
	//struct timer_list       timer;
	struct workqueue_struct *wq;
	struct delayed_work 	    delay_work;
	struct work_struct 	    dcwakeup_work;
	struct work_struct                   lowerpower_work;
	bool                    resume;
	
	struct rk29_adc_battery_platform_data *pdata;

	int                     full_times;
	
	struct adc_client       *client; 
	int                     adc_val;
	int                     adc_samples[NUM_VOLTAGE_SAMPLE+2];
	
	int                     bat_status;
	int                     bat_status_cnt;
	int                     bat_health;
	int                     bat_present;
	int                     bat_voltage;
	int                     bat_capacity;
	int                     bat_change;
	
	int                     old_charge_level;
	int                    *pSamples;
	int                     gBatCapacityDisChargeCnt;
	int                     gBatCapacityChargeCnt;
	int 	          capacitytmp;
	int                     poweron_check;
	int                     suspend_capacity;

};
static struct rk29_adc_battery_data *gBatteryData;

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





static int rk29_adc_battery_load_capacity(void)
{
	char value[4];
	int* p = (int *)value;
	long fd = sys_open(BATT_FILENAME,O_RDONLY,0);

	if(fd < 0){
		printk("rk29_adc_battery_load_capacity: open file /data/bat_last_capacity.dat failed\n");
		return -1;
	}

	sys_read(fd,(char __user *)value,4);
	sys_close(fd);

	return (*p);
}

static void rk29_adc_battery_put_capacity(int loadcapacity)
{
	char value[4];
	int* p = (int *)value;
	long fd = sys_open(BATT_FILENAME,O_CREAT | O_RDWR,0);

	if(fd < 0){
		printk("rk29_adc_battery_put_capacity: open file /data/bat_last_capacity.dat failed\n");
		return;
	}
	
	*p = loadcapacity;
	sys_write(fd, (const char __user *)value, 4);

	sys_close(fd);
}

static void rk29_adc_battery_charge_enable(struct rk29_adc_battery_data *bat)
{
	struct rk29_adc_battery_platform_data *pdata = bat->pdata;

	if (pdata->charge_set_pin != INVALID_GPIO){
		gpio_direction_output(pdata->charge_set_pin, pdata->charge_set_level);
	}
}

static void rk29_adc_battery_charge_disable(struct rk29_adc_battery_data *bat)
{
	struct rk29_adc_battery_platform_data *pdata = bat->pdata;

	if (pdata->charge_set_pin != INVALID_GPIO){
		gpio_direction_output(pdata->charge_set_pin, 1 - pdata->charge_set_level);
	}
}

//extern int suspend_flag;
static int rk29_adc_battery_get_charge_level(struct rk29_adc_battery_data *bat)
{
	int charge_on = 0;
	struct rk29_adc_battery_platform_data *pdata = bat->pdata;

#if defined (CONFIG_BATTERY_RK29_AC_CHARGE)
	if (pdata->dc_det_pin != INVALID_GPIO){
		if (gpio_get_value (pdata->dc_det_pin) == pdata->dc_det_level){
			charge_on = 1;
		}
	}
#endif

#if defined  (CONFIG_BATTERY_RK29_USB_CHARGE)
	if (charge_on == 0){
		if (suspend_flag)
			return;
		if (1 == dwc_vbus_status()) {          //检测到USB插入，但是无法识别是否是充电器
		                                 //通过延时检测PC识别标志，如果超时检测不到，说明是充电
			if (0 == get_msc_connect_flag()){                               //插入充电器时间大于一定时间之后，开始进入充电状态
				if (++gBatUsbChargeCnt >= NUM_USBCHARGE_IDENTIFY_TIMES){
					gBatUsbChargeCnt = NUM_USBCHARGE_IDENTIFY_TIMES + 1;
					charge_on = 1;
				}
			}                               //否则，不进入充电模式
		}                   
		else{
			gBatUsbChargeCnt = 0;
			if (2 == dwc_vbus_status()) {
				charge_on = 1;
			}
		}
	}
#endif
	return charge_on;
}

//int old_charge_level;
static int rk29_adc_battery_status_samples(struct rk29_adc_battery_data *bat)
{
	int charge_level;
	
	struct rk29_adc_battery_platform_data *pdata = bat->pdata;

	charge_level = rk29_adc_battery_get_charge_level(bat);

	//检测充电状态变化情况
	if (charge_level != bat->old_charge_level){
		bat->old_charge_level = charge_level;
		bat->bat_change  = 1;
		
		if(charge_level) {            
			rk29_adc_battery_charge_enable(bat);
		}
		else{
			rk29_adc_battery_charge_disable(bat);
		}
		bat->bat_status_cnt = 0;        //状态变化开始计数
	}

	if(charge_level == 0){   
	//discharge
		bat->full_times = 0;
		bat->bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}
	else{
	//CHARGE	    
		if (pdata->charge_ok_pin == INVALID_GPIO){  //no charge_ok_pin

			if (bat->bat_capacity == 100){
				if (bat->bat_status != POWER_SUPPLY_STATUS_FULL){
					bat->bat_status = POWER_SUPPLY_STATUS_FULL;
					bat->bat_change  = 1;
				}
			}
			else{
				bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
			}
		}
		else{  // pin of charge_ok_pin
			if (gpio_get_value(pdata->charge_ok_pin) != pdata->charge_ok_level){

				bat->full_times = 0;
				bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
			}
			else{
	//检测到充电满电平标志
				bat->full_times++;

				if (bat->full_times >= NUM_CHARGE_FULL_DELAY_TIMES) {
					bat->full_times = NUM_CHARGE_FULL_DELAY_TIMES + 1;
				}

				if ((bat->full_times >= NUM_CHARGE_FULL_DELAY_TIMES) && (bat->bat_capacity >= 99)){
					if (bat->bat_status != POWER_SUPPLY_STATUS_FULL){
						bat->bat_status = POWER_SUPPLY_STATUS_FULL;
						bat->bat_capacity = 100;
						bat->bat_change  = 1;
					}
				}
				else{
					bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
				}
			}
		}
	}

	return charge_level;
}

static int *pSamples;
static void rk29_adc_battery_voltage_samples(struct rk29_adc_battery_data *bat)
{
	int value;
	int i,*pStart = bat->adc_samples, num = 0;
	int level = rk29_adc_battery_get_charge_level(bat);


	value = bat->adc_val;
	adc_async_read(bat->client);

	*pSamples++ = adc_to_voltage(value);

	bat->bat_status_cnt++;
	if (bat->bat_status_cnt > NUM_VOLTAGE_SAMPLE)  bat->bat_status_cnt = NUM_VOLTAGE_SAMPLE + 1;

	num = pSamples - pStart;
	
	if (num >= NUM_VOLTAGE_SAMPLE){
		pSamples = pStart;
		num = NUM_VOLTAGE_SAMPLE;

	}

	value = 0;
	for (i = 0; i < num; i++){
		value += bat->adc_samples[i];
	}
	bat->bat_voltage = value / num;

	/*消除毛刺电压*/
	if(1 == level){
		if(bat->bat_voltage >= batt_table[BATT_NUM-1].charge_vol+ 10)
			bat->bat_voltage = batt_table[BATT_NUM-1].charge_vol  + 10;
		else if(bat->bat_voltage <= batt_table[0].charge_vol  - 10)
			bat->bat_voltage =  batt_table[0].charge_vol - 10;
	}
	else{
		if(bat->bat_voltage >= batt_table[BATT_NUM-1].dis_charge_vol+ 10)
			bat->bat_voltage = batt_table[BATT_NUM-1].dis_charge_vol  + 10;
		else if(bat->bat_voltage <= batt_table[0].dis_charge_vol  - 10)
			bat->bat_voltage =  batt_table[0].dis_charge_vol - 10;

	}
}
static int rk29_adc_battery_voltage_to_capacity(struct rk29_adc_battery_data *bat, int BatVoltage)
{
	int i = 0;
	int capacity = 0;

	struct batt_vol_cal *p;
	p = batt_table;

	if (rk29_adc_battery_get_charge_level(bat)){  //charge
		if(BatVoltage >= (p[BATT_NUM - 1].charge_vol)){
			capacity = 100;
		}	
		else{
			if(BatVoltage <= (p[0].charge_vol)){
				capacity = 0;
			}
			else{
				for(i = 0; i < BATT_NUM - 1; i++){

					if(((p[i].charge_vol) <= BatVoltage) && (BatVoltage < (p[i+1].charge_vol))){
						capacity =  i * 10 + ((BatVoltage - p[i].charge_vol) * 10) / (p[i+1].charge_vol- p[i].charge_vol);
						break;
					}
				}
			}  
		}

	}
	else{  //discharge
		if(BatVoltage >= (p[BATT_NUM - 1].dis_charge_vol)){
			capacity = 100;
		}	
		else{
			if(BatVoltage <= (p[0].dis_charge_vol)){
				capacity = 0;
			}
			else{
				for(i = 0; i < BATT_NUM - 1; i++){
					if(((p[i].dis_charge_vol) <= BatVoltage) && (BatVoltage < (p[i+1].dis_charge_vol))){
						capacity =   i * 10 + ((BatVoltage - p[i].dis_charge_vol) * 10) / (p[i+1].dis_charge_vol- p[i].dis_charge_vol); ;
						break;
					}
				}
			}  

		}


	}
    return capacity;
}

static void rk29_adc_battery_capacity_samples(struct rk29_adc_battery_data *bat)
{
	int capacity = 0;
	struct rk29_adc_battery_platform_data *pdata = bat->pdata;

	//充放电状态变化后，Buffer填满之前，不更新
	if (bat->bat_status_cnt < NUM_VOLTAGE_SAMPLE)  {
		bat->gBatCapacityDisChargeCnt = 0;
		bat->gBatCapacityChargeCnt    = 0;
		return;
	}
	
	capacity = rk29_adc_battery_voltage_to_capacity(bat, bat->bat_voltage);
	    
	if (rk29_adc_battery_get_charge_level(bat)){
		if (capacity > bat->bat_capacity){
			//实际采样到的容量比显示的容量大，逐级上升
			if (++(bat->gBatCapacityDisChargeCnt) >= NUM_CHARGE_MIN_SAMPLE){
				bat->gBatCapacityDisChargeCnt  = 0;
				if (bat->bat_capacity < 99){
					bat->bat_capacity++;
					bat->bat_change  = 1;
				}
			}
			bat->gBatCapacityChargeCnt = 0;
		}
		else{  //   实际的容量比采样比 显示的容量小
		            bat->gBatCapacityDisChargeCnt = 0;
		            (bat->gBatCapacityChargeCnt)++;
            
			if (pdata->charge_ok_pin != INVALID_GPIO){
				if (gpio_get_value(pdata->charge_ok_pin) == pdata->charge_ok_level){
				//检测到电池充满标志，同时长时间内充电电压无变化，开始启动计时充电，快速上升容量
					if (bat->gBatCapacityChargeCnt >= NUM_CHARGE_MIN_SAMPLE){
						bat->gBatCapacityChargeCnt = 0;
						if (bat->bat_capacity < 99){
							bat->bat_capacity++;
							bat->bat_change  = 1;
						}
					}
				}
				else{
#if 0					
					if (capacity > capacitytmp){
					//过程中如果电压有增长，定时器复位，防止定时器模拟充电比实际充电快
						gBatCapacityChargeCnt = 0;
					}
					else if (/*bat->bat_capacity >= 85) &&*/ (gBatCapacityChargeCnt > NUM_CHARGE_MAX_SAMPLE)){
						gBatCapacityChargeCnt = (NUM_CHARGE_MAX_SAMPLE - NUM_CHARGE_MID_SAMPLE);

						if (bat->bat_capacity < 99){
						bat->bat_capacity++;
						bat->bat_change  = 1;
						}
					}
				}
#else			//  防止电池老化后出现冲不满的情况，
					if (capacity > bat->capacitytmp){
					//过程中如果电压有增长，定时器复位，防止定时器模拟充电比实际充电快
						bat->gBatCapacityChargeCnt = 0;
					}
					else{

						if ((bat->bat_capacity >= 85) &&((bat->gBatCapacityChargeCnt) > NUM_CHARGE_MAX_SAMPLE)){
							bat->gBatCapacityChargeCnt = (NUM_CHARGE_MAX_SAMPLE - NUM_CHARGE_MID_SAMPLE);

							if (bat->bat_capacity < 99){
								bat->bat_capacity++;
								bat->bat_change  = 1;
							}
						}
					}
				}
#endif

			}
			else{
			//没有充电满检测脚，长时间内电压无变化，定时器模拟充电
				if (capacity > bat->capacitytmp){
				//过程中如果电压有增长，定时器复位，防止定时器模拟充电比实际充电快
					bat->gBatCapacityChargeCnt = 0;
				}
				else{

					if ((bat->bat_capacity >= 85) &&(bat->gBatCapacityChargeCnt > NUM_CHARGE_MAX_SAMPLE)){
						bat->gBatCapacityChargeCnt = (NUM_CHARGE_MAX_SAMPLE - NUM_CHARGE_MID_SAMPLE);

						if (bat->bat_capacity < 99){
							bat->bat_capacity++;
							bat->bat_change  = 1;
						}
					}
				}
				

			}            
		}
	}    
	else{   
	//放电时,只允许电压下降
		if (capacity < bat->bat_capacity){
			if (++(bat->gBatCapacityDisChargeCnt) >= NUM_DISCHARGE_MIN_SAMPLE){
				bat->gBatCapacityDisChargeCnt = 0;
				if (bat->bat_capacity > 0){
					bat->bat_capacity-- ;
					bat->bat_change  = 1;
				}
			}
		}
		else{
			bat->gBatCapacityDisChargeCnt = 0;
		}
		bat->gBatCapacityChargeCnt = 0;
	}
		bat->capacitytmp = capacity;
}

//static int poweron_check = 0;
static void rk29_adc_battery_poweron_capacity_check(void)
{

	int new_capacity, old_capacity;

	new_capacity = gBatteryData->bat_capacity;
	old_capacity = rk29_adc_battery_load_capacity();
	if ((old_capacity <= 0) || (old_capacity >= 100)){
		old_capacity = new_capacity;
	}    

	if (gBatteryData->bat_status == POWER_SUPPLY_STATUS_FULL){
		if (new_capacity > 80){
			gBatteryData->bat_capacity = 100;
		}
	}
	else if (gBatteryData->bat_status != POWER_SUPPLY_STATUS_NOT_CHARGING){
	//chargeing state
	//问题：
//	//1）长时间关机放置后，开机后读取的容量远远大于实际容量怎么办？
//	//2）如果不这样做，短时间关机再开机，前后容量不一致又该怎么办？
//	//3）一下那种方式合适？
	//gBatteryData->bat_capacity = new_capacity;
		gBatteryData->bat_capacity = (new_capacity > old_capacity) ? new_capacity : old_capacity;
	}else{

		if(new_capacity > old_capacity + 50 )
			gBatteryData->bat_capacity = new_capacity;
		else
			gBatteryData->bat_capacity = (new_capacity < old_capacity) ? new_capacity : old_capacity;  //avoid the value of capacity increase 
	}


	//printk("capacity = %d, new_capacity = %d, old_capacity = %d\n",gBatteryData->bat_capacity, new_capacity, old_capacity); 

	gBatteryData->bat_change = 1;
}
#if 0
static void rk29_adc_battery_scan_timer(unsigned long data)
{
	gBatteryData->timer.expires  = jiffies + msecs_to_jiffies(TIMER_MS_COUNTS);
	add_timer(&gBatteryData->timer);

	schedule_work(&gBatteryData->timer_work);	
}
#endif

#if defined(CONFIG_BATTERY_RK29_USB_CHARGE)
static int rk29_adc_battery_get_usb_property(struct power_supply *psy, 
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	charger_type_t charger;
	charger =  CHARGER_USB;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_USB)
			val->intval = get_msc_connect_flag();
		printk("%s:%d\n",__FUNCTION__,val->intval);
		break;

	default:
		return -EINVAL;
	}
	
	return 0;

}

static enum power_supply_property rk29_adc_battery_usb_props[] = {
    
	POWER_SUPPLY_PROP_ONLINE,
};

static struct power_supply rk29_usb_supply = 
{
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,

	.get_property   = rk29_adc_battery_get_usb_property,

	.properties     = rk29_adc_battery_usb_props,
	.num_properties = ARRAY_SIZE(rk29_adc_battery_usb_props),
};
#endif

#if defined(CONFIG_BATTERY_RK29_AC_CHARGE)
static irqreturn_t rk29_adc_battery_dc_wakeup(int irq, void *dev_id)
{   
	queue_work(gBatteryData->wq, &gBatteryData->dcwakeup_work);
	return IRQ_HANDLED;
}


static int rk29_adc_battery_get_ac_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	int ret = 0;
	charger_type_t charger;
	charger =  CHARGER_USB;
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
		{
	//		printk("POWER_SUPPLY_TYPE_MAINS\n");
			if (rk29_adc_battery_get_charge_level(gBatteryData))
			{
				val->intval = 1;
				}
			else
				{
				val->intval = 0;	
				}
		}
		DBG("%s:%d\n",__FUNCTION__,val->intval);
		break;
		
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property rk29_adc_battery_ac_props[] = 
{
	POWER_SUPPLY_PROP_ONLINE,
};

static struct power_supply rk29_ac_supply = 
{
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,

	.get_property   = rk29_adc_battery_get_ac_property,

	.properties     = rk29_adc_battery_ac_props,
	.num_properties = ARRAY_SIZE(rk29_adc_battery_ac_props),
};

static void rk29_adc_battery_dcdet_delaywork(struct work_struct *work)
{
	int ret;
	struct rk29_adc_battery_platform_data *pdata;
	int irq;
	int irq_flag;
	//printk("DC_WAKEUP\n");
	pdata    = gBatteryData->pdata;
	irq        = gpio_to_irq(pdata->dc_det_pin);
	irq_flag = gpio_get_value (pdata->dc_det_pin) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;

	rk28_send_wakeup_key(); // wake up the system

	free_irq(irq, NULL);
	ret = request_irq(irq, rk29_adc_battery_dc_wakeup, irq_flag, "ac_charge_irq", NULL);// reinitialize the DC irq 
	if (ret) {
		free_irq(irq, NULL);
	}

	power_supply_changed(&rk29_ac_supply);

	gBatteryData->bat_status_cnt = 0;        //the state of battery is change

	wake_lock_timeout(&batt_wake_lock, 29 * HZ);

}


#endif

static int rk29_adc_battery_get_status(struct rk29_adc_battery_data *bat)
{
	return (bat->bat_status);
}

static int rk29_adc_battery_get_health(struct rk29_adc_battery_data *bat)
{
	return POWER_SUPPLY_HEALTH_GOOD;
}

static int rk29_adc_battery_get_present(struct rk29_adc_battery_data *bat)
{
	return (bat->bat_voltage < BATT_MAX_VOL_VALUE) ? 0 : 1;
}

static int rk29_adc_battery_get_voltage(struct rk29_adc_battery_data *bat)
{
	return (bat->bat_voltage );
}

static int rk29_adc_battery_get_capacity(struct rk29_adc_battery_data *bat)
{
	return (bat->bat_capacity);
}

static int rk29_adc_battery_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{		
	int ret = 0;

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = rk29_adc_battery_get_status(gBatteryData);
			DBG("gBatStatus=%d\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			val->intval = rk29_adc_battery_get_health(gBatteryData);
			DBG("gBatHealth=%d\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = rk29_adc_battery_get_present(gBatteryData);
			DBG("gBatPresent=%d\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val ->intval = rk29_adc_battery_get_voltage(gBatteryData);
			DBG("gBatVoltage=%d\n",val->intval);
			break;
		//	case POWER_SUPPLY_PROP_CURRENT_NOW:
		//		val->intval = 1100;
		//		break;
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = rk29_adc_battery_get_capacity(gBatteryData);
			DBG("gBatCapacity=%d%%\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_TECHNOLOGY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;	
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
			val->intval = BATT_MAX_VOL_VALUE;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
			val->intval = BATT_ZERO_VOL_VALUE;
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

static enum power_supply_property rk29_adc_battery_props[] = {

	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
//	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
};

static struct power_supply rk29_battery_supply = 
{
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,

	.get_property   = rk29_adc_battery_get_property,

	.properties     = rk29_adc_battery_props,
	.num_properties = ARRAY_SIZE(rk29_adc_battery_props),
};

#ifdef CONFIG_PM
//int suspend_capacity = 0;
static void rk29_adc_battery_resume_check(void)
{
	int i;
	int level,oldlevel;
	int new_capacity, old_capacity;
	struct rk29_adc_battery_data *bat = gBatteryData;

	bat->old_charge_level = -1;
	pSamples = bat->adc_samples;

	adc_sync_read(bat->client);                             //start adc sample
	level = oldlevel = rk29_adc_battery_status_samples(bat);//init charge status

	for (i = 0; i < NUM_VOLTAGE_SAMPLE; i++) {               //0.3 s   
	
		mdelay(1);
		rk29_adc_battery_voltage_samples(bat);              //get voltage
	level = rk29_adc_battery_status_samples(bat);       //check charge status
		if (oldlevel != level){		
		    oldlevel = level;                               //if charge status changed, reset sample
		    i = 0;
		}        
	}
	new_capacity = rk29_adc_battery_voltage_to_capacity(bat, bat->bat_voltage);
	old_capacity =gBatteryData-> suspend_capacity;

	if (bat->bat_status != POWER_SUPPLY_STATUS_NOT_CHARGING){
	//chargeing state
		bat->bat_capacity = (new_capacity > old_capacity) ? new_capacity : old_capacity;
	}
	else{
		bat->bat_capacity = (new_capacity < old_capacity) ? new_capacity : old_capacity;  // aviod the value of capacity increase    dicharge
	}

	//printk("rk29_adc_battery_resume: status = %d, voltage = %d, capacity = %d, new_capacity = %d, old_capacity = %d\n",
	//                             bat->bat_status, bat->bat_voltage, bat->bat_capacity, new_capacity, old_capacity); //xsf

//	wake_lock_timeout(&batt_wake_lock, 5 * HZ); //5s
}

static int rk29_adc_battery_suspend(struct platform_device *dev, pm_message_t state)
{
	gBatteryData->suspend_capacity = gBatteryData->bat_capacity;
	return 0;
}

static int rk29_adc_battery_resume(struct platform_device *dev)
{
	gBatteryData->resume = true;
	return 0;
}
#else
#define rk29_adc_battery_suspend NULL
#define rk29_adc_battery_resume NULL
#endif


unsigned long AdcTestCnt = 0;
static void rk29_adc_battery_timer_work(struct work_struct *work)
{
#ifdef CONFIG_PM
	if (gBatteryData->resume) {
		rk29_adc_battery_resume_check();
		gBatteryData->resume = false;
	}
#endif

	rk29_adc_battery_status_samples(gBatteryData);

	if (gBatteryData->poweron_check){   
		gBatteryData->poweron_check = 0;
		rk29_adc_battery_poweron_capacity_check();
	}

	rk29_adc_battery_voltage_samples(gBatteryData);
	
	rk29_adc_battery_capacity_samples(gBatteryData);


	/*update battery parameter after adc and capacity has been changed*/
	if(gBatteryData->bat_change){
		gBatteryData->bat_change = 0;
		rk29_adc_battery_put_capacity(gBatteryData->bat_capacity);
		power_supply_changed(&rk29_battery_supply);
	}

	if (rk29_battery_dbg_level)
	{
		if (++AdcTestCnt >= 2)
			{
			AdcTestCnt = 0;

			printk("Status = %d, RealAdcVal = %d, RealVol = %d,gBatVol = %d, gBatCap = %d, RealCapacity = %d, dischargecnt = %d, chargecnt = %d\n", 
			gBatteryData->bat_status, gBatteryData->adc_val, adc_to_voltage(gBatteryData->adc_val), 
			gBatteryData->bat_voltage, gBatteryData->bat_capacity, gBatteryData->capacitytmp, gBatteryData->gBatCapacityDisChargeCnt,gBatteryData-> gBatCapacityChargeCnt);

		}
	}
	queue_delayed_work(gBatteryData->wq, &gBatteryData->delay_work, msecs_to_jiffies(TIMER_MS_COUNTS));

}


static int rk29_adc_battery_io_init(struct rk29_adc_battery_platform_data *pdata)
{
	int ret = 0;
	
	if (pdata->io_init) {
		pdata->io_init();
	}
	
	//charge control pin
	if (pdata->charge_set_pin != INVALID_GPIO){
	    	ret = gpio_request(pdata->charge_set_pin, NULL);
	    	if (ret) {
	    		printk("failed to request dc_det gpio\n");
	    		goto error;
		    	}
	    	gpio_direction_output(pdata->charge_set_pin, 1 - pdata->charge_set_level);
	}
	
	//dc charge detect pin
	if (pdata->dc_det_pin != INVALID_GPIO){
	    	ret = gpio_request(pdata->dc_det_pin, NULL);
	    	if (ret) {
	    		printk("failed to request dc_det gpio\n");
	    		goto error;
	    	}
	
	    	gpio_pull_updown(pdata->dc_det_pin, GPIOPullUp);//important
	    	ret = gpio_direction_input(pdata->dc_det_pin);
	    	if (ret) {
	    		printk("failed to set gpio dc_det input\n");
	    		goto error;
	    	}
	}
	
	//charge ok detect
	if (pdata->charge_ok_pin != INVALID_GPIO){
 		ret = gpio_request(pdata->charge_ok_pin, NULL);
	    	if (ret) {
	    		printk("failed to request charge_ok gpio\n");
	    		goto error;
	    	}
	
	    	gpio_pull_updown(pdata->charge_ok_pin, GPIOPullUp);//important
	    	ret = gpio_direction_input(pdata->charge_ok_pin);
	    	if (ret) {
	    		printk("failed to set gpio charge_ok input\n");
	    		goto error;
	    	}
	}
	//batt low pin
	if( pdata->batt_low_pin != INVALID_GPIO){
 		ret = gpio_request(pdata->batt_low_pin, NULL);
	    	if (ret) {
	    		printk("failed to request batt_low_pin gpio\n");
	    		goto error;
	    	}
	
	    	gpio_pull_updown(pdata->batt_low_pin, GPIOPullUp); 
	    	ret = gpio_direction_input(pdata->batt_low_pin);
	    	if (ret) {
	    		printk("failed to set gpio batt_low_pin input\n");
	    		goto error;
	    	}
	}
    
	return 0;
error:
	return -1;
}
#define POWER_ON_PIN    RK29_PIN4_PA4
#define LOOP(loops) do { unsigned int i = loops; barrier(); while (--i) barrier(); } while (0)

//extern void kernel_power_off(void);
static void rk29_adc_battery_check(struct rk29_adc_battery_data *bat)
{
	int i;
	int tmp = 0;
	int level,oldlevel;
	struct rk29_adc_battery_platform_data *pdata = bat->pdata;
	//printk("%s--%d:\n",__FUNCTION__,__LINE__);

	bat->old_charge_level = -1;
	bat->capacitytmp = 0;
	bat->suspend_capacity = 0;
	
	pSamples = bat->adc_samples;

	adc_sync_read(bat->client);                             //start adc sample
	level = oldlevel = rk29_adc_battery_status_samples(bat);//init charge status

	bat->full_times = 0;
	for (i = 0; i < NUM_VOLTAGE_SAMPLE; i++){                //0.3 s
		mdelay(1);
		rk29_adc_battery_voltage_samples(bat);              //get voltage
		//level = rk29_adc_battery_status_samples(bat);       //check charge status
		level = rk29_adc_battery_get_charge_level(bat);

		if (oldlevel != level){
			oldlevel = level;                               //if charge status changed, reset sample
			i = 0;
		}        
	}

	bat->bat_capacity = rk29_adc_battery_voltage_to_capacity(bat, bat->bat_voltage);  //init bat_capacity
	
	bat->bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	if (rk29_adc_battery_get_charge_level(bat)){
		bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;

		if (pdata->charge_ok_pin != INVALID_GPIO){
			if (gpio_get_value(pdata->charge_ok_pin) == pdata->charge_ok_level){
				bat->bat_status = POWER_SUPPLY_STATUS_FULL;
				bat->bat_capacity = 100;
			}
		}
	}

#if 1
	rk29_adc_battery_poweron_capacity_check();
#else
	gBatteryData->poweron_check = 1;
#endif
	gBatteryData->poweron_check = 0;

/*******************************************
//开机采样到的电压和上次关机保存电压相差较大，怎么处理？
if (bat->bat_capacity > old_capacity)
{
if ((bat->bat_capacity - old_capacity) > 20)
{

}
}
else if (bat->bat_capacity < old_capacity)
{
if ((old_capacity > bat->bat_capacity) > 20)
{

}
}
*********************************************/
	if (bat->bat_capacity == 0) bat->bat_capacity = 1;

	if (bat->bat_voltage <= BATT_ZERO_VOL_VALUE + 500){
		printk("low battery: powerdown\n");
		gpio_direction_output(POWER_ON_PIN, GPIO_LOW);
		tmp = 0;
		while(1){
			if(gpio_get_value(POWER_ON_PIN) == GPIO_HIGH){
				gpio_set_value(POWER_ON_PIN,GPIO_LOW);
			}
			mdelay(500);
			if (++tmp > 50)
				break;
		}

	}
	gpio_direction_output(POWER_ON_PIN, GPIO_HIGH);

}

static void rk29_adc_battery_callback(struct adc_client *client, void *param, int result)
{
#if 0
	struct rk29_adc_battery_data  *info = container_of(client, struct rk29_adc_battery_data,
		client);
	info->adc_val = result;
#endif
	gBatteryData->adc_val = result;
	return;
}

#if 0
static void rk29_adc_battery_lowerpower_delaywork(struct work_struct *work)
{
	struct rk29_adc_battery_platform_data *pdata;
	int irq;
	printk("lowerpower\n");
	pdata    = gBatteryData->pdata;
	irq        = gpio_to_irq(pdata->dc_det_pin);
	rk28_send_wakeup_key(); // wake up the system
	free_irq(irq, NULL);
	return;
}


static irqreturn_t rk29_adc_battery_low_wakeup(int irq,void *dev_id)
{

	schedule_work(&gBatteryData->lowerpower_work);	
	return IRQ_HANDLED;
}

#endif

static int rk29_adc_battery_probe(struct platform_device *pdev)
{
	int    ret;
	int    irq;
	int    irq_flag;
	struct adc_client                   *client;
	struct rk29_adc_battery_data          *data;
	struct rk29_adc_battery_platform_data *pdata = pdev->dev.platform_data;

	//printk("%s--%d:\n",__FUNCTION__,__LINE__);
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		ret = -ENOMEM;
		goto err_data_alloc_failed;
	}
	gBatteryData = data;

	platform_set_drvdata(pdev, data);

   	 data->pdata = pdata;
	 
	ret = rk29_adc_battery_io_init(pdata);
	 if (ret) {
	 	goto err_io_init;
	}
    
	memset(data->adc_samples, 0, sizeof(int)*(NUM_VOLTAGE_SAMPLE + 2));

	 //register adc for battery sample
	client = adc_register(0, rk29_adc_battery_callback, NULL);  //pdata->adc_channel = ani0
	if(!client)
		goto err_adc_register_failed;
	    
	 //variable init
	data->client  = client;
	data->adc_val = adc_sync_read(client);

	ret = power_supply_register(&pdev->dev, &rk29_battery_supply);
	if (ret){
		printk(KERN_INFO "fail to battery power_supply_register\n");
		goto err_battery_failed;
	}
		

#if defined (CONFIG_BATTERY_RK29_USB_CHARGE)
	ret = power_supply_register(&pdev->dev, &rk29_usb_supply);
	if (ret){
		printk(KERN_INFO "fail to usb power_supply_register\n");
		goto err_usb_failed;
	}
#endif
 	wake_lock_init(&batt_wake_lock, WAKE_LOCK_SUSPEND, "batt_lock");	

	data->wq = create_singlethread_workqueue("adc_battd");
	INIT_DELAYED_WORK(&data->delay_work, rk29_adc_battery_timer_work);
	//Power on Battery detect
	rk29_adc_battery_check(data);
	queue_delayed_work(data->wq, &data->delay_work, msecs_to_jiffies(TIMER_MS_COUNTS));

#if  defined (CONFIG_BATTERY_RK29_AC_CHARGE)
	ret = power_supply_register(&pdev->dev, &rk29_ac_supply);
	if (ret) {
		printk(KERN_INFO "fail to ac power_supply_register\n");
		goto err_ac_failed;
	}
	//init dc dectet irq & delay work
	if (pdata->dc_det_pin != INVALID_GPIO){
		INIT_WORK(&data->dcwakeup_work, rk29_adc_battery_dcdet_delaywork);
		
		irq = gpio_to_irq(pdata->dc_det_pin);	        
		irq_flag = gpio_get_value (pdata->dc_det_pin) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
	    	ret = request_irq(irq, rk29_adc_battery_dc_wakeup, irq_flag, "ac_charge_irq", NULL);
	    	if (ret) {
	    		printk("failed to request dc det irq\n");
	    		goto err_dcirq_failed;
	    	}
	    	enable_irq_wake(irq);  
    	
	}
#endif
#if 0
	// batt low irq lowerpower_work
	if( pdata->batt_low_pin != INVALID_GPIO){
		INIT_WORK(&data->lowerpower_work, rk29_adc_battery_lowerpower_delaywork);
		
		irq = gpio_to_irq(pdata->batt_low_pin);
	    	ret = request_irq(irq, rk29_adc_battery_low_wakeup, IRQF_TRIGGER_LOW, "batt_low_irq", NULL);

	    	if (ret) {
	    		printk("failed to request batt_low_irq irq\n");
	    		goto err_lowpowerirq_failed;
	    	}
	    	enable_irq_wake(irq);
    	}
#endif

//	printk(KERN_INFO "rk29_adc_battery: driver initialized\n");
	
	return 0;
	
#if defined (CONFIG_BATTERY_RK29_USB_CHARGE)
err_usb_failed:
	power_supply_unregister(&rk29_usb_supply);
#endif

#if defined (CONFIG_BATTERY_RK29_AC_CHARGE)
err_ac_failed:
	power_supply_unregister(&rk29_ac_supply);
err_dcirq_failed:
	free_irq(gpio_to_irq(pdata->dc_det_pin), data);
#endif

err_battery_failed:
	power_supply_unregister(&rk29_battery_supply);
    
#if 0
 err_lowpowerirq_failed:
	free_irq(gpio_to_irq(pdata->batt_low_pin), data);
#endif
err_adc_register_failed:
err_io_init:    
err_data_alloc_failed:
	kfree(data);

	printk("rk29_adc_battery: error!\n");
    
	return ret;
}

static int rk29_adc_battery_remove(struct platform_device *pdev)
{
	struct rk29_adc_battery_data *data = platform_get_drvdata(pdev);
	struct rk29_adc_battery_platform_data *pdata = pdev->dev.platform_data;

	cancel_delayed_work(&gBatteryData->delay_work);	
#if defined(CONFIG_BATTERY_RK29_USB_CHARGE)
	power_supply_unregister(&rk29_usb_supply);
#endif
#if defined(CONFIG_BATTERY_RK29_AC_CHARGE)
	power_supply_unregister(&rk29_ac_supply);
#endif
	power_supply_unregister(&rk29_battery_supply);

	free_irq(gpio_to_irq(pdata->dc_det_pin), data);

	kfree(data);
	
	return 0;
}

static struct platform_driver rk29_adc_battery_driver = {
	.probe		= rk29_adc_battery_probe,
	.remove		= rk29_adc_battery_remove,
	.suspend		= rk29_adc_battery_suspend,
	.resume		= rk29_adc_battery_resume,
	.driver = {
		.name = "rk2918-battery",
		.owner	= THIS_MODULE,
	}
};

static int __init rk29_adc_battery_init(void)
{
	return platform_driver_register(&rk29_adc_battery_driver);
}

static void __exit rk29_adc_battery_exit(void)
{
	platform_driver_unregister(&rk29_adc_battery_driver);
}

subsys_initcall(rk29_adc_battery_init);//subsys_initcall(rk29_adc_battery_init);
module_exit(rk29_adc_battery_exit);

MODULE_DESCRIPTION("Battery detect driver for the rk29");
MODULE_AUTHOR("luowei lw@rock-chips.com");
MODULE_LICENSE("GPL");
