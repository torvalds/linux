/* drivers/power/rt5025-battery.c
 * I2C Driver for Richtek RT5025 PMIC
 * Multi function device - multi functional baseband PMIC Battery part
 *
 * Copyright (C) 2013
 * Author: Nick Hung <nick_hung@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/android_alarm.h>
#include <linux/mfd/rt5025.h>
#include <linux/power/rt5025-battery.h>

#define VOLTAGE_ALERT 0
#define TEMPERATURE_ALERT 0

#define RT5025_CSV 0
#define RT5025_B 1
#define RT5025_TEST_WAKE_LOCK 0

u8 irq_thres[LAST_TYPE];

static int rt5025_battery_parameter_backup(struct rt5025_battery_info *);

void rt5025_gauge_set_status(struct rt5025_battery_info *bi, int status)
{
  bi->status = status;
  if (status == POWER_SUPPLY_STATUS_FULL)
  {
	bi->tp_flag = true;
	bi->last_tp_flag = true;
  }
  else
	power_supply_changed(&bi->battery);
  wake_lock_timeout(&bi->status_wake_lock, 1.5*HZ);
  schedule_delayed_work(&bi->monitor_work, 0);
}
EXPORT_SYMBOL(rt5025_gauge_set_status);

void rt5025_gauge_set_online(struct rt5025_battery_info *bi, bool present)
{
  bi->online = present;
}
EXPORT_SYMBOL(rt5025_gauge_set_online);

static int rt5025_read_reg(struct i2c_client *client,
				u8 reg, u8 *data, u8 len)
{
	#if 1
	return rt5025_reg_block_read(client, reg, len, data);
	#else
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msgs[2];
	int ret;
	
	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 1;
	msgs[0].buf = &reg;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = data;
	
	ret = i2c_transfer(adap, msgs, 2);
	 
	return (ret == 2)? len : ret;
	#endif
}

static int rt5025_write_reg(struct i2c_client *client,
				u8 reg, u8 *data, u8 len)
{
	#if 1
	return rt5025_reg_block_write(client, reg, len, data);
	#else
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	int ret;
	char *tx_buf = (char *)kmalloc(len + 1, GFP_KERNEL);
	
	if(!tx_buf)
		return -ENOMEM;
	tx_buf[0] = reg;
	memcpy(tx_buf+1, data, len);
	
	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len + 1;
	msg.buf = (char *)tx_buf;

	ret = i2c_transfer(adap, &msg, 1);
	kfree(tx_buf);
	return (ret == 1) ? len : ret;
	#endif
}

static void rt5025_gauge_alarm(struct alarm *alarm)
{
	struct rt5025_battery_info *bi = (struct rt5025_battery_info *)container_of(alarm, struct rt5025_battery_info, wakeup_alarm);

	//wake_lock(&bi->monitor_wake_lock);
	schedule_delayed_work(&bi->monitor_work, 0);
}

static void rt5025_program_alarm(struct rt5025_battery_info *bi)
{
	ktime_t low_interval = ktime_set(bi->update_time, 0);
	//ktime_t slack = ktime_set(20, 0);
	ktime_t next;

	next = ktime_add(bi->last_poll, low_interval);
	//alarm_start_range(&bi->wakeup_alarm, next, ktime_add(next, slack));
}

#if 0
static void rt5025_run_time(struct rt5025_battery_info *bi)
{
	if(bi->curr <= 0)
	{
		bi->time_to_empty = bi->rm / (bi->curr*(-1));
	}
	else
	{
		bi->time_to_full = (bi->fcc * 3600 - bi->rm) / bi->curr;
	}
	RTINFO("RTTF = %d\n",bi->time_to_full);
	RTINFO("RTTE = %d\n",bi->time_to_empty);
}
#endif

static int rt5025_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
  struct rt5025_battery_info *bi = dev_get_drvdata(psy->dev->parent);

  switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:
      val->intval = bi->status;
      //val->intval = POWER_SUPPLY_STATUS_CHARGING;
      break;
    case POWER_SUPPLY_PROP_HEALTH:
      val->intval = bi->health;
      break;
    case POWER_SUPPLY_PROP_PRESENT:
      val->intval = bi->present;
      break;
    case POWER_SUPPLY_PROP_TEMP:
      val->intval = bi->ext_temp;
      break;
    case POWER_SUPPLY_PROP_ONLINE:
      val->intval = bi->online;
      break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
      val->intval = bi->vcell * 1000; //uV
      break;
    case POWER_SUPPLY_PROP_CURRENT_NOW:
      val->intval = bi->curr * 1000; //uA
      break;
    case POWER_SUPPLY_PROP_CAPACITY:
      val->intval = bi->soc;
			//val->intval = 50;
      if (val->intval > 100)
				val->intval = 100;
      break;
    case POWER_SUPPLY_PROP_TECHNOLOGY:
      val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
      break;
    #if 0
    case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
			val->intval = bi->time_to_empty;
    break;
    case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
			val->intval = bi->time_to_full;
    #endif
    break;
    default:
      return -EINVAL;
  }
  return 0;
}

static void rt5025_get_vcell(struct rt5025_battery_info *bi)
{
  u8 data[2];
	
  if (bi->init_once)
  {
    rt5025_clr_bits(bi->client, 0x07, 0x10);
    RTINFO("set_current switch off\n");
    mdelay(1000);
  }

  if (rt5025_read_reg(bi->client, RT5025_REG_VCELL_MSB, data, 2) < 0){
    printk(KERN_ERR "%s: Failed to read Voltage\n", __func__);
  }

  if (bi->init_once)
  {
    rt5025_set_bits(bi->client, 0x07, 0x10);
    RTINFO("set_current switch on\n");
  }

  if (bi->avg_flag)
    bi->vcell = ((data[0] << 8) + data[1]) * 61 / 100;
  else 
	  bi->vcell = (bi->vcell + ((data[0] << 8) + data[1]) * 61 / 100) / 2;
#if RT5025_B
	bi->curr_offset = (15444 * bi->vcell - 27444000) / 10000;
#else 
  if (37 * bi->vcell > 92000)
		bi->curr_offset = (37 * bi->vcell - 92000) / 1000;
	else
		bi->curr_offset = 0;
#endif
		
#if RT5025_CSV
 // if (!bi->avg_flag)
  //  pr_info("%d,%d,", bi->vcell, bi->curr_offset);
#else  
  if (bi->avg_flag)
		RTINFO("vcell_pre: %d, offset: %d\n", bi->vcell, bi->curr_offset);
  else
		RTINFO("vcell_avg: %d, offset: %d\n", bi->vcell, bi->curr_offset);
#endif
}

static void rt5025_get_current(struct rt5025_battery_info *bi)
{
  u8 data[2];
  s32 temp;
  int sign = 0;
  u8 curr_region;

  if (rt5025_read_reg(bi->client, RT5025_REG_CURRENT_MSB, data, 2) < 0) {
    printk(KERN_ERR "%s: Failed to read CURRENT\n", __func__);
  }
#if RT5025_B
  temp = (data[0]<<8) | data[1];
  bi->curr_raw = ((temp & 0x7FFF) * 3125) / 10000;
    
  if (data[0] & (1 << 7)) {
    sign = 1;
    temp = (((temp & 0x7FFF) * 3125) / 10 + bi->curr_offset) / 1000;
  }else{
		if ((temp * 3125) / 10 > bi->curr_offset)
			temp = ((temp * 3125) / 10 - bi->curr_offset) / 1000;
	}
	
  if (temp < DEADBAND)
		temp = 0;
  
  if (sign){
    temp *= -1;
    bi->curr_raw *= -1;
	}
#else
  temp = (data[0]<<8) | data[1];
  if (data[0] & (1 << 7)) {
    sign = 1;
    temp = temp & 0x7FFF;
    if(temp > bi->curr_offset)
			temp = temp - bi->curr_offset;
  }else {
    temp = temp + bi->curr_offset;
	}

  temp = (temp * 37375) / 100000; //Unit: 0.3125mA
  if (temp < DEADBAND)
		temp = 0;
  
  if (sign)
    temp *= -1;
#endif

  if (bi->avg_flag) 
    bi->curr = temp;
  else
	  bi->curr = (bi->curr + temp) / 2;

  if (bi->curr > -500)
	curr_region = 0;
  else if (bi->curr <= -500 && bi->curr > -1500)
	curr_region = 1;
  else
	curr_region = 2;

  if (curr_region != bi->edv_region)
  {
	switch (curr_region)
	{
		case 0:
			bi->empty_edv = rt5025_battery_param2[4].x;
			break;
		case 1:
			bi->empty_edv = rt5025_battery_param2[4].x - 75;
			break;
		case 2:
			bi->empty_edv = rt5025_battery_param2[4].x - 100 ;
			break;
	}
	bi->edv_region = curr_region;
  }
  RTINFO("empty_voltage=%d\n", bi->empty_edv);

  if(bi->curr > 0)
  {
    bi->internal_status = POWER_SUPPLY_STATUS_CHARGING;
    bi->last_tp_flag = false;
  }
  else
    bi->internal_status = POWER_SUPPLY_STATUS_DISCHARGING;
  RTINFO("current=%d, internal_status=%d\n", bi->curr, bi->internal_status);

#if RT5025_CSV
 // if (!bi->avg_flag)
  //  pr_info("%d,",bi->curr);
#else
  if (bi->avg_flag)
		RTINFO("current_pre: %d\n", bi->curr);
  else  
		RTINFO("current_avg: %d\n", bi->curr);
#endif
}

static void rt5025_get_internal_temp(struct rt5025_battery_info *bi)
{
	u8 data[2];
	s32 temp;
	if (rt5025_read_reg(bi->client, RT5025_REG_INT_TEMPERATUE_MSB, data, 2) < 0){
		printk(KERN_ERR "%s: Failed to read internal TEMPERATURE\n", __func__);
	}

	temp = ((data[0]&0x1F)<<8) + data[1];
	temp *= 15625;
	temp /= 100000;

	temp = (data[0]&0x20)?-temp:temp;
	bi->int_temp = temp;
	RTINFO("internal temperature: %d\n", bi->int_temp);
}
	
static void rt5025_get_external_temp(struct rt5025_battery_info *bi)
{
  u8 data[2];
  s32 temp;

  if (rt5025_read_reg(bi->client, RT5025_REG_EXT_TEMPERATUE_MSB, data, 2) < 0) {
    printk(KERN_ERR "%s: Failed to read TEMPERATURE\n", __func__);
  }
  bi->ain_volt = (data[0] * 256 + data[1]) * 61 / 100;
  if(bi->ain_volt < 1150) 
  {
	bi->present = 1;
  }
  else
  {
	bi->present = 0;  
  }
  
  temp =  (bi->ain_volt * (-91738) + 81521000) / 100000;
  bi->ext_temp = (int)temp;
	//test
	//bi->ext_temp = 250;
	
	if (bi->ext_temp >= HIGH_TEMP_THRES) {
		if (bi->health != POWER_SUPPLY_HEALTH_OVERHEAT)
			bi->temp_high_cnt++;
	} else if (bi->ext_temp <= HIGH_TEMP_RECOVER && bi->ext_temp >= LOW_TEMP_RECOVER) {
		if (bi->health == POWER_SUPPLY_HEALTH_OVERHEAT ||
		    bi->health == POWER_SUPPLY_HEALTH_COLD)
			bi->temp_recover_cnt++;
	} else if (bi->ext_temp <= LOW_TEMP_THRES) {
		if (bi->health != POWER_SUPPLY_HEALTH_COLD)
			bi->temp_low_cnt++;
	}else {
		bi->temp_high_cnt = 0;
		bi->temp_low_cnt = 0;
		bi->temp_recover_cnt = 0;
	}
	
	if (bi->temp_high_cnt >= TEMP_ABNORMAL_COUNT) {
	 bi->health = POWER_SUPPLY_HEALTH_OVERHEAT;
	 bi->temp_high_cnt = 0;
	} else if (bi->temp_low_cnt >= TEMP_ABNORMAL_COUNT) {
	 bi->health = POWER_SUPPLY_HEALTH_COLD;
	 bi->temp_low_cnt = 0;
	} else if (bi->temp_recover_cnt >= TEMP_ABNORMAL_COUNT) {
	 bi->health = POWER_SUPPLY_HEALTH_GOOD;
	 bi->temp_recover_cnt = 0;
	}
	RTINFO("external temperature: %d\n", bi->ext_temp);
}

static void rt5025_clear_cc(struct rt5025_battery_info *bi, operation_mode mode)
{  
  u8 data[2];
	
  if (rt5025_read_reg(bi->client, RT5025_REG_CHANNEL_MSB, data, 2) < 0){
    pr_err("%s: failed to read channel\n", __func__);
  }

  if (mode == CHG)
		data[0] = data[0] | CHANNEL_H_BIT_CLRQCHG;
	else
		data[0] = data[0] | CHANNEL_H_BIT_CLRQDCHG;
		
  if (rt5025_write_reg(bi->client, RT5025_REG_CHANNEL_MSB, data, 2) < 0){
    pr_err("%s: failed to write channel\n", __func__);
  }
}

static void rt5025_get_chg_cc(struct rt5025_battery_info *bi)
{
  u8 data[4];
  u32 qh_old,ql_old,qh_new,ql_new;
  u32 cc_masec,offset=0;
  
  if (rt5025_read_reg(bi->client, RT5025_REG_QCHGH_MSB, data, 4) < 0){
    pr_err("%s: Failed to read QCHG\n", __func__);
  }
  qh_old = (data[0]<<8) + data[1];
  ql_old = (data[2]<<8) + data[3];
  RTINFO("qh_old=%d, ql_old=%d\n", qh_old, ql_old);
  
  if (rt5025_read_reg(bi->client, RT5025_REG_QCHGH_MSB, data, 4) < 0){
    pr_err("%s: Failed to read QCHG\n", __func__);
  }
  qh_new = (data[0]<<8) + data[1];
  ql_new = (data[2]<<8) + data[3];
  RTINFO("qh_new=%d, ql_new=%d\n", qh_new, ql_new);

#if RT5025_B
  if (qh_new > qh_old){
     //cc_masec = (((qh_new<<16) + ql_new) * 50134) / 10;
     cc_masec = qh_new*328558+(qh_new*1824+ql_new*50134)/10000;
  }else if (qh_new == qh_old){
    if (ql_new >= ql_old){
     //cc_masec = (((qh_new<<16) + ql_new) * 50134) / 10;
     cc_masec = qh_new*328558+(qh_new*1824+ql_new*50134)/10000;
    }else {  
      //cc_masec = (((qh_old<<16) + ql_old) * 50134) / 10;
     cc_masec = qh_old*328558+(qh_old*1824+ql_old*50134)/10000;
		}
  }	
  
  if (!bi->init_once)
	offset = bi->curr_offset * bi->time_interval;
		   	  
  if (cc_masec > offset){
		cc_masec = cc_masec - (offset / 1000);
	}
#else   	
  if (qh_new > qh_old){
     cc_masec = (((qh_new<<16) + ql_new) * 5996) / 1000;
  }else if (qh_new == qh_old){
    if (ql_new >= ql_old){
      cc_masec = (((qh_new<<16) + ql_new) * 5996) / 1000;
    }else {  
      cc_masec = (((qh_old<<16) + ql_old) * 5996) / 1000;
		}
  }	
  
	offset = (bi->curr_offset * bi->time_interval * 37375) / 100000;
		   	  
  if (cc_masec != 0){
		cc_masec = cc_masec - offset;
	}
#endif
	if (cc_masec < (DEADBAND * bi->time_interval))
		cc_masec = 0;

#if RT5025_CSV
  //pr_info("%d,\n", cc_masec);
#else
	RTINFO("chg_cc_mAsec: %d\n", cc_masec);
#endif

	//if (!bi->init_once)
		bi->chg_cc = cc_masec;
	//bi->chg_cc = (cc_masec + bi->chg_cc_unuse) / 3600;
  //bi->chg_cc_unuse = (cc_masec + bi->chg_cc_unuse) % 3600;
  rt5025_clear_cc(bi, CHG);
}

static void rt5025_get_dchg_cc(struct rt5025_battery_info *bi)
{
  u8 data[4];
  u32 qh_old,ql_old,qh_new,ql_new;
  u32 cc_masec,offset=0;
  
  if (rt5025_read_reg(bi->client, RT5025_REG_QDCHGH_MSB, data, 4) < 0){
    printk(KERN_ERR "%s: Failed to read QDCHG\n", __func__);
  }
  qh_old = (data[0]<<8) + data[1];
  ql_old = (data[2]<<8) + data[3];
  
  if (rt5025_read_reg(bi->client, RT5025_REG_QDCHGH_MSB, data, 4) < 0){
    printk(KERN_ERR "%s: Failed to read QDCHG\n", __func__);
  }
  qh_new = (data[0]<<8) + data[1];
  ql_new = (data[2]<<8) + data[3];
  
#if RT5025_B
  if (qh_new > qh_old){
     //cc_masec = (((qh_new<<16) + ql_new) * 50134) / 10;
     cc_masec = qh_new*328558+(qh_new*1824+ql_new*50134)/10000;
  }else if (qh_new == qh_old){
    if (ql_new >= ql_old){
     //cc_masec = (((qh_new<<16) + ql_new) * 50134) / 10;
     cc_masec = qh_new*328558+(qh_new*1824+ql_new*50134)/10000;
    }else {  
     //cc_masec = (((qh_old<<16) + ql_old) * 50134) / 10;
     cc_masec = qh_old*328558+(qh_old*1824+ql_old*50134)/10000;
		}
  }	
  
  if (!bi->init_once)
	offset = bi->curr_offset * bi->time_interval;
		   	  
  if (cc_masec != 0){
		cc_masec = cc_masec + (offset / 1000);
	}
#else  
  if (qh_new > qh_old){
     cc_masec = (((qh_new<<16) + ql_new) * 5996) / 1000;
  }else if (qh_new == qh_old){
    if (ql_new >= ql_old){
      cc_masec = (((qh_new<<16) + ql_new) * 5996) / 1000;
    }else {  
      cc_masec = (((qh_old<<16) + ql_old) * 5996) / 1000;
		}
  }
  
	offset = (bi->curr_offset * bi->time_interval * 37375) / 100000;
		   	  
  if (cc_masec > offset){
		cc_masec = cc_masec - offset;
	}
#endif
	if (cc_masec < (DEADBAND * bi->time_interval))
		cc_masec = 0;
		
#if RT5025_CSV
  //pr_info("%d,", cc_masec);
#else
	RTINFO("dchg_cc_mAsec: %d\n", cc_masec);
#endif
	bi->dchg_cc = cc_masec;
	if (bi->last_tp_flag)
		bi->cal_fcc += cc_masec;
	else
		bi->cal_fcc = 0;
	RTINFO("bi->cal_fcc=%d, bi->last_tp_flag=%d\n", bi->cal_fcc, bi->last_tp_flag);
	//bi->dchg_cc = (cc_masec + bi->dchg_cc_unuse) / 3600;
  //bi->dchg_cc_unuse = (cc_masec + bi->dchg_cc_unuse) % 3600;
	rt5025_clear_cc(bi, DCHG);
	
}

static void rt5025_cycle_count(struct rt5025_battery_info *bi)
{
	bi->acc_dchg_cap +=  bi->dchg_cc;
	if(bi->acc_dchg_cap >= (bi->dc * 3600)){
		bi->cycle_cnt++;
		bi->acc_dchg_cap -= (bi->dc * 3600);
	}
}

static void rt5025_get_irq_flag(struct rt5025_battery_info *bi, u8 flag)
{
#if 0
  u8 data[1];

  if (rt5025_read_reg(bi->client, RT5025_REG_IRQ_FLAG, data, 1) < 0){
    pr_err("%s: Failed to read irq_flag\n", __func__);
  }
#endif
		
  bi->irq_flag = flag;
	//RTINFO("IRQ_FLG 0x%x\n", bi->irq_flag);
}

static void rt5025_get_timer(struct rt5025_battery_info *bi)
{
  u8 data[2];
  //frankie
	//u16 gauge_timer;
	
  if (rt5025_read_reg(bi->client, RT5025_REG_TIMER, data, 2) < 0){
    	pr_err("%s: Failed to read Timer\n", __func__);
  }
		
  bi->gauge_timer = (data[0] << 8) + data[1];
  if (!bi->device_suspend){
    if (bi->gauge_timer > bi->pre_gauge_timer)
		  bi->time_interval = bi->gauge_timer - bi->pre_gauge_timer;
	  else	
		  bi->time_interval = 65536 - bi->pre_gauge_timer + bi->gauge_timer;
  }
    
  bi->pre_gauge_timer = bi->gauge_timer;
#if RT5025_CSV
 // pr_info("%d,%d,", bi->gauge_timer,bi->time_interval);
#else
	RTINFO("timer %d , interval %d\n", bi->gauge_timer,bi->time_interval);
#endif
}

static void rt5025_alert_setting(struct rt5025_battery_info *bi, alert_type type, bool enable)
{
	u8 data[1];
	
	if (rt5025_read_reg(bi->client, RT5025_REG_IRQ_CTL, data, 1) < 0){
		printk(KERN_ERR "%s: Failed to read CONFIG\n", __func__);
	}

	if(enable){
		switch(type){
			case MAXTEMP:
				data[0] |= IRQ_CTL_BIT_TMX; //Enable max temperature alert
				bi->max_temp_irq = true;
				//RTDBG("Enable min temperature alert");
				break;
			case MINTEMP:
				data[0] |= IRQ_CTL_BIT_TMN; //Enable min temperature alert
				bi->min_temp_irq = true;  
				//RTDBG("Enable max temperature alert");
				break;
			case MAXVOLT:
				data[0] |= IRQ_CTL_BIT_VMX; //Enable max voltage alert
				bi->max_volt_irq = true;
				//RTDBG("Enable max voltage alert");
				break;
			case MINVOLT1:
				data[0] |= IRQ_CTL_BIT_VMN1; //Enable min1 voltage alert	
				bi->min_volt1_irq = true;
				//RTDBG("Enable min1 voltage alert");
				break;
			case MINVOLT2:
				data[0] |= IRQ_CTL_BIT_VMN2; //Enable min2 voltage alert
				bi->min_volt2_irq = true;
				//RTDBG("Enable min2 voltage alert");
				break;
			default:
				break;
		}
	}else{
		switch(type){
			case MAXTEMP:
				data[0] = data[0] &~ IRQ_CTL_BIT_TMX; //Disable max temperature alert
				bi->max_temp_irq = false;
				//RTDBG("Disable min temperature alert");
				break;
			case MINTEMP:
				data[0] = data[0] &~ IRQ_CTL_BIT_TMN; //Disable min temperature alert
				bi->min_temp_irq = false;
				//RTDBG("Disable max temperature alert");
				break;
			case MAXVOLT:
				data[0] = data[0] &~ IRQ_CTL_BIT_VMX; //Disable max voltage alert
				bi->max_volt_irq = false;
				//RTDBG("Disable max voltage alert");
				break;
			case MINVOLT1:
				data[0] = data[0] &~ IRQ_CTL_BIT_VMN1; //Disable min1 voltage alert	
				bi->min_volt1_irq = false;
				//RTDBG("Disable min1 voltage alert");
				break;
			case MINVOLT2:
				data[0] = data[0] &~ IRQ_CTL_BIT_VMN2; //Disable min2 voltage alert
				bi->min_volt2_irq = false;
				//RTDBG("Disable min2 voltage alert");
				break;
			default:
				break;
		}
	}
	if (rt5025_write_reg(bi->client, RT5025_REG_IRQ_CTL, data, 1) < 0)
		pr_err("%s: failed to write IRQ control\n", __func__);
}

static void rt5025_alert_threshold_init(struct i2c_client *client)
{
	u8 data[1];

	#if 0 //change the operating right to jeita driver
	/* TALRT MAX threshold setting */
	data[0] = irq_thres[MAXTEMP];
	if (rt5025_write_reg(client, RT5025_REG_TALRT_MAXTH, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write TALRT MAX threshold\n", __func__);	
	/* TALRT MIN threshold setting */
	data[0] = irq_thres[MINTEMP];
	if (rt5025_write_reg(client, RT5025_REG_TALRT_MINTH, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write TALRT MIN threshold\n", __func__);	
	#endif
	/* VALRT MAX threshold setting */
	data[0] = irq_thres[MAXVOLT];
	if (rt5025_write_reg(client, RT5025_REG_VALRT_MAXTH, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write VALRT MAX threshold\n", __func__);	
	/* VALRT MIN1 threshold setting */
	data[0] = irq_thres[MINVOLT1];
	if (rt5025_write_reg(client, RT5025_REG_VALRT_MIN1TH, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write VALRT MIN1 threshold\n", __func__);	
	/* VALRT MIN2 threshold setting */
	data[0] = irq_thres[MINVOLT2];
	if (rt5025_write_reg(client, RT5025_REG_VALRT_MIN2TH, data, 1) < 0)
		printk(KERN_ERR "%s: failed to write VALRT MIN2 threshold\n", __func__);	
}

static void rt5025_alert_init(struct rt5025_battery_info *bi)
{

	/* Set RT5025 gauge alert configuration */
	rt5025_alert_threshold_init(bi->client);
	/* Enable gauge alert function */
 	//rt5025_alert_setting(bi, MINVOLT2,VOLTAGE_ALERT);	
}

void rt5025_gauge_irq_handler(struct rt5025_battery_info *bi, u8 irq_flag)
{
  rt5025_get_irq_flag(bi, irq_flag);

  if ((bi->irq_flag) & IRQ_FLG_BIT_TMX){	
		//printk(KERN_INFO "[RT5025]: Min temperature IRQ received\n");
		rt5025_alert_setting(bi,MAXTEMP,false);
		bi->max_temp_irq = false;
	}
  if ((bi->irq_flag) & IRQ_FLG_BIT_TMN){
		//printk(KERN_INFO "[RT5025]: Max temperature IRQ received\n");
		rt5025_alert_setting(bi,MINTEMP,false);
		bi->min_temp_irq = false; 
	}
  if ((bi->irq_flag) & IRQ_FLG_BIT_VMX){
		//printk(KERN_INFO "[RT5025]: Max voltage IRQ received\n");
		rt5025_alert_setting(bi,MAXVOLT,false);
		bi->max_volt_irq = false;
	}
  if ((bi->irq_flag) & IRQ_FLG_BIT_VMN1){
		//printk(KERN_INFO "[RT5025]: Min voltage1 IRQ received\n");
		rt5025_alert_setting(bi,MINVOLT1,false);
		bi->min_volt1_irq = false;
	}
  if ((bi->irq_flag) & IRQ_FLG_BIT_VMN2){
		//printk(KERN_INFO "[RT5025]: Min voltage2 IRQ received\n");
		rt5025_alert_setting(bi,MINVOLT2,false);
		bi->min_volt2_irq = false;
		bi->min_volt2_alert = true;
		wake_lock_timeout(&bi->low_battery_wake_lock, msecs_to_jiffies(LOW_BAT_WAKE_LOK_TIME*MSEC_PER_SEC));
	}
	
	//wake_lock(&bi->monitor_wake_lock);
	wake_lock_timeout(&bi->status_wake_lock, 1.5*HZ);
	schedule_delayed_work(&bi->monitor_work, 0);
}
EXPORT_SYMBOL(rt5025_gauge_irq_handler);

static void rt5025_convert_masec_to_permille(struct rt5025_battery_info *bi)
{
  bi->permille = bi->rm / 3600 * 1000 / bi->fcc;
  RTINFO("permille=%d\n", bi->permille);
  return;
}

static void rt5025_convert_permille_to_masec(struct rt5025_battery_info *bi)
{ 
  bi->rm = bi->permille * bi->fcc / 1000 * 3600;
  return;
}

static void rt5025_init_capacity(struct rt5025_battery_info *bi)
{
  int i = 1;
  int size;
  int slope, const_term;
  int delta_y, delta_x;

  size = ARRAY_SIZE(rt5025_battery_param1);
  while((bi->vcell < rt5025_battery_param1[i].x) &&
				(i < (size - 1))){
    i++;
  }

  delta_x = rt5025_battery_param1[i-1].x - rt5025_battery_param1[i].x;
  delta_y = (rt5025_battery_param1[i-1].y - rt5025_battery_param1[i].y);

  slope = delta_y  * 1000 / delta_x;

  const_term = (rt5025_battery_param1[i].y) - ((rt5025_battery_param1[i].x * slope) / 1000);

  if (bi->vcell >= rt5025_battery_param1[0].x)
    bi->permille = rt5025_battery_param1[0].y; 
  else if (bi->vcell <= rt5025_battery_param1[size-1].x)
    bi->permille = rt5025_battery_param1[size-1].y;
  else
    bi->permille = (bi->vcell * slope) / 1000 + const_term;
  rt5025_convert_permille_to_masec(bi);
  bi->soc = bi->rm /36/bi->fcc_aging;
	bi->init_cap = false;

  rt5025_battery_parameter_backup(bi);

  //pr_err("[rt5025] i=%d, delta_x=%d, delta_y=%d, slope=%d, const_term=%d\n", i, delta_x, delta_y, slope, const_term);
	RTINFO("voltage=%d, permille=%d, soc=%d, rm=%d\n", bi->vcell, bi->permille, bi->soc, bi->rm);
  return;		
}

static void rt5025_smooth_soc(struct rt5025_battery_info *bi)
{
	if ((bi->internal_status == POWER_SUPPLY_STATUS_CHARGING || bi->tp_flag == 1) &&
	    (bi->soc < 100))
	{
		bi->soc++;
		bi->rm = bi->fcc * bi->soc * 36;
		rt5025_convert_masec_to_permille(bi);
	}
	else if ((bi->internal_status == POWER_SUPPLY_STATUS_DISCHARGING) &&
	          (bi->soc > 0))
	{
		bi->soc--;
		bi->rm = bi->fcc * bi->soc * 36;
		rt5025_convert_masec_to_permille(bi);
	}
	else
	{
	  bi->smooth_flag = false;
	  bi->update_time = NORMAL_POLL;
  }
}


static void rt5025_soc_irreversible(struct rt5025_battery_info *bi)
{
	// Prevent inverse
	//if (!bi->init_cap){
	if (!bi->init_once){
		if (bi->internal_status == POWER_SUPPLY_STATUS_CHARGING)
		{
			if (bi->soc < bi->pre_soc)
				bi->soc = bi->pre_soc;
		}
		else if ((bi->internal_status == POWER_SUPPLY_STATUS_DISCHARGING) && 
						(bi->tp_flag == 0))
		{
			if (bi->soc > bi->pre_soc) 
				bi->soc = bi->pre_soc;
		}
	}
	else
		bi->init_once = false;

	if (bi->pre_soc != bi->soc)
		rt5025_battery_parameter_backup(bi);

	bi->pre_soc = bi->soc;
	RTINFO("pre_soc=%d, soc=%d, internal status=%d\n", bi->pre_soc,bi->soc,bi->internal_status);
}

static void rt5025_soc_lock(struct rt5025_battery_info *bi)
{
	 // lock 99%
  RTINFO("internal status=%d, tp_flag=%d, soc=%d\n", bi->internal_status, bi->tp_flag, bi->soc);
  if (bi->soc >= 99) 
  {
  	if (bi->tp_flag)
      		bi->soc = 100;
		else if(bi->internal_status == POWER_SUPPLY_STATUS_CHARGING)
		{
				bi->soc = 99;
				bi->pre_soc = 99;
		}
  }
  else if ((bi->soc < 99) && (bi->tp_flag))
  {
		if (!bi->last_suspend)
		{
			bi->update_time = SMOOTH_POLL;
			bi->smooth_flag = true;
			rt5025_smooth_soc(bi);
		}
		else
			bi->last_suspend=false;
  }
  else
  {
		bi->tp_flag = false;
  }

  // lock 1%   
  if ((bi->soc <= 1) &&
      (bi->internal_status == POWER_SUPPLY_STATUS_DISCHARGING)){
    if (bi->edv_flag)
      bi->soc = 0;
    else
    {
			bi->soc = 1;
			bi->pre_soc = 1; 
		}
      
	}else if ((bi->soc > 1) &&
            (bi->internal_status == POWER_SUPPLY_STATUS_DISCHARGING) &&
            (bi->edv_flag)){
		if (!bi->last_suspend)
		{
			bi->update_time = SMOOTH_POLL;
			bi->smooth_flag = true;
			rt5025_smooth_soc(bi);
		}
		else
			bi->last_suspend=false;
	}else{
		bi->edv_flag = false;
	}
}

static void rt5025_get_soc(struct rt5025_battery_info *bi)
{
  if (bi->smooth_flag){
    bi->smooth_flag = false;
    bi->update_time = NORMAL_POLL;
  }
	RTINFO("before rm=%d\n", bi->rm);
	if ((!bi->tp_flag) &&
			(!bi->edv_flag)){
		bi->rm = (bi->rm + bi->chg_cc) > bi->dchg_cc ? 
		            bi->rm + bi->chg_cc - bi->dchg_cc : 0;
		if (bi->rm > (bi->fcc * 3600))
					bi->rm = bi->fcc * 3600;
		rt5025_convert_masec_to_permille(bi);
		bi->soc = DIV_ROUND_UP(bi->permille, 10);
#if RT5025_CSV
    bi->temp_soc = bi->soc;
    //pr_info("%d", bi->soc);
#else
	RTINFO("after rm=%d\n", bi->rm);
		RTINFO("temp_soc=%d\n", bi->soc);
#endif
  }
#if RT5025_CSV
	RTINFO("soc=%d, permille=%d, rm=%d, fcc=%d, smooth_flag=%d\n", bi->soc,bi->permille,bi->rm,bi->fcc,bi->smooth_flag);
 // pr_info("%d,%d,%d,%d,%d", bi->soc,bi->permille,bi->rm,bi->fcc,bi->smooth_flag);
#else  
	RTINFO("soc=%d, permille=%d, rm=%d, fcc=%d, smooth_flag=%d\n", bi->soc,bi->permille,bi->rm,bi->fcc,bi->smooth_flag);
#endif
  return;
}

static void rt5025_soc_relearn_check(struct rt5025_battery_info *bi)
{
  // TP relearn
/*  if ((bi->vcell >= TP_VOLT) &&
			(bi->curr <= TP_CURR) &&
			(bi->status == POWER_SUPPLY_STATUS_CHARGING) &&
			(!bi->tp_flag)){
		bi->tp_cnt++;
		bi->update_time = TP_POLL;
	}else {
		bi->tp_cnt = 0;
		bi->update_time = NORMAL_POLL;
	}
   
  if (bi->tp_cnt == TP_TOTAL_COUNT){
		bi->tp_cnt = 0;
		bi->tp_flag = true;
		bi->rm = bi->fcc * 3600;
		rt5025_convert_masec_to_permille();
		bi->update_time = NORMAL_POLL;
	}*/
	
	// if EOC happened, the tp_flag should be set 1.
	if(bi->tp_flag == true)
	{
		bi->rm = bi->fcc * 3600;
		rt5025_convert_masec_to_permille(bi);
		bi->update_time = NORMAL_POLL;
	}

	if (bi->vcell <= bi->empty_edv)
	{
		if (bi->edv_cnt < 2)
			bi->edv_cnt++;
	}
	else
		bi->edv_cnt=0;
	
	if(bi->empty_edv < bi->vcell && bi->vcell <= bi->empty_edv+300)
	{
		bi->update_time = EDV_POLL;
		bi->edv_detection = true;
	}
	else if((bi->vcell >= bi->empty_edv + 300 +EDV_HYS) && (bi->edv_detection == true))
	{
		 bi->update_time = NORMAL_POLL;
		 bi->edv_detection = false;		 
	}
	else if((bi->vcell <= bi->empty_edv && bi->edv_cnt == 2)) //&& (bi->min_volt2_alert == true))
	{
		bi->edv_flag = true;
		bi->rm = 0;
		rt5025_convert_masec_to_permille(bi);
		bi->edv_detection = false;
		bi->update_time = NORMAL_POLL;
	}
	else if((bi->vcell > bi->empty_edv + EDV_HYS)) //&& (bi->min_volt2_alert == true))
	{
		bi->min_volt2_alert = false;
		bi->edv_flag = false;
	}
	
	/*// EDV relearn
  if (bi->vcell <= EDV_VOLT){
		if ((bi->curr <= EDV_CURR) ||
				(bi->vcell <= rt5025_battery_param2[4].x))
			bi->edv_cnt++;
		else
		{
			bi->edv_cnt = 0;	
		}
		bi->edv_detection = true;
		bi->update_time = EDV_POLL;
	}else if ((bi->vcell > (EDV_VOLT + EDV_HYS)) &&
	          (bi->edv_detection)) {
		bi->edv_cnt = 0;
		bi->edv_detection = false;
		bi->edv_flag = false;
		bi->update_time = NORMAL_POLL;
	}
	else
	{
		bi->edv_cnt = 0;
	}
   
  if (bi->edv_cnt == EDV_TOTAL_COUNT){
		bi->edv_cnt = 0;
		bi->edv_flag = true;
		bi->rm = 0;
		rt5025_convert_masec_to_permille();
		bi->edv_detection = false;
		bi->update_time = NORMAL_POLL;
	}
  if(bi->edv_detection)
  {
	bi->update_time = EDV_POLL;
  }*/
  if (bi->internal_status == POWER_SUPPLY_STATUS_CHARGING) 
	  bi->edv_flag = false;
  if (bi->status != POWER_SUPPLY_STATUS_FULL)
    bi->tp_flag = false;

#if RT5025_CSV
  //pr_err("%d,%d,%d,%d,%d", 
   // bi->tp_cnt,bi->tp_flag,bi->edv_detection,bi->edv_cnt,bi->edv_flag);
#else  
	RTINFO("tp_cnt=%d, tp_flag=%d, edv_detection=%d, edv_cnt=%d, edv_flag=%d\n", 
    bi->tp_cnt,bi->tp_flag,bi->edv_detection,bi->edv_cnt,bi->edv_flag);
#endif

  return;
}

#if 0
static void rt5025_channel_cc(struct rt5025_battery_info *bi, bool enable)
{
  u8 data[1];
	
  if (rt5025_read_reg(bi->client, RT5025_REG_CHANNEL_LSB, data, 1) < 0){
    printk(KERN_INFO "%s: failed to read channel\n", __func__);
  }

  if (enable){
    data[0] = data[0] | 0x80;
  }else { 
    data[0] = data[0] & 0x7F;
  }
    
  if (rt5025_write_reg(bi->client, RT5025_REG_CHANNEL_LSB, data, 1) < 0){
    printk(KERN_INFO "%s: failed to write channel\n", __func__);
  }
}
#endif

#if 0
static void rt5025_pretrim(struct rt5025_battery_info *bi)
{
	u8 data0[2];
	u8 data1[1];
	
	data0[0] = 0x55;
	data0[1] = 0xAA;
  if (rt5025_write_reg(bi->client, 0xF0, data0, 2) < 0){
    printk(KERN_ERR "%s: failed to write channel\n", __func__);
  }
	data0[0] = 0x07;
  if (rt5025_write_reg(bi->client, 0xF1, data0, 1) < 0){
    printk(KERN_ERR "%s: failed to write channel\n", __func__);
  }
  // write trim data D0
	data0[0] = 0xDE;
  if (rt5025_write_reg(bi->client, 0xD0, data0, 1) < 0){
    printk(KERN_ERR "%s: failed to write channel\n", __func__);
  }
  // Read back to verify
  if (rt5025_read_reg(bi->client, 0xD0, data1, 1) < 0){
    printk(KERN_ERR "%s: failed to read channel\n", __func__);
  }
  if (data1[0] != data0[0])
		printk(KERN_ERR "%s: 0xD0 write fail\n", __func__);
	// write trim data D1
	data0[0] = 0x01;
  if (rt5025_write_reg(bi->client, 0xD1, data0, 1) < 0){
    printk(KERN_ERR "%s: failed to write channel\n", __func__);
  }
  // Read back to verify
  if (rt5025_read_reg(bi->client, 0xD1, data1, 1) < 0){
    printk(KERN_ERR "%s: failed to read channel\n", __func__);
  }
  if (data1[0] != data0[0])
		printk(KERN_ERR "%s: 0xD1 write fail\n", __func__);
  // write trim data D2
	data0[0] = 0x10;
  if (rt5025_write_reg(bi->client, 0xD2, data0, 1) < 0){
    printk(KERN_ERR "%s: failed to write channel\n", __func__);
  }
  // Read back to verify
  if (rt5025_read_reg(bi->client, 0xD2, data1, 1) < 0){
    printk(KERN_ERR "%s: failed to read channel\n", __func__);
  }
  if(data1[0] != data0[0])
		printk(KERN_ERR "%s: 0xD2 write fail\n", __func__);
  // write trim data D3
	data0[0] = 0x89;
  if (rt5025_write_reg(bi->client, 0xD3, data0, 1) < 0){
    printk(KERN_ERR "%s: failed to write channel\n", __func__);
  }
  // Read back to verify
  if (rt5025_read_reg(bi->client, 0xD3, data1, 1) < 0){
    printk(KERN_ERR "%s: failed to read channel\n", __func__);
  }
  if(data1[0] != data0[0])
		printk(KERN_ERR "%s: 0xD3 write fail\n", __func__);
  // write trim data D4
	data0[0] = 0xF2;
  if (rt5025_write_reg(bi->client, 0xD4, data0, 1) < 0){
    printk(KERN_ERR "%s: failed to write channel\n", __func__);
  }
  // Read back to verify
  if (rt5025_read_reg(bi->client, 0xD4, data1, 1) < 0){
    printk(KERN_ERR "%s: failed to read channel\n", __func__);
  }
  if (data1[0] != data0[0])
		printk(KERN_ERR "%s: 0xD4 write fail\n", __func__);				
			 
	data0[0] = 0x55;
	data0[1] = 0x55;
  if (rt5025_write_reg(bi->client, 0xF0, data0, 2) < 0){
    printk(KERN_ERR "%s: failed to write channel\n", __func__);
  }
}
#endif

static u16 get_crc16_value(u8* data, int size)
{
	u16 fcs = 0xffff;
	int len = size;
	int i = 0;
	u16 temp = 0;

	while (len > 0)
	{
		fcs = (u16)((fcs>>8)^crctab16[(fcs^data[i])&0xff]);
		len--;
		i++;
	}
	temp = (u16)~fcs;
	return temp;
}

static int IsCrc16Good(u8* data, int size)
{
	u16 fcs = 0xffff;
	int len = size;
	int i = 0;

	while (len>0)
	{
		fcs = (u16)((fcs>>8)^crctab16[((fcs^data[i])&0xff)]);
		len--;
		i++;
	}
	return (fcs == 0xf0b8);
}

static int rt5025_battery_parameter_backup(struct rt5025_battery_info *bi)
{
	u16 crc_value = 0;
	u8 data[12]= {0};
	RTINFO("\n");
	//backup fcc_aging, rm, cycle_count, acc_dchg_cap
	//fcc_aging
	data[0] = (bi->fcc_aging>>8)&0xff;
	data[1] = (bi->fcc_aging)&0xff;
	rt5025_write_reg(bi->client, 0x21, &data[0], 2);
	//acc_dchg_cap
	data[2] = (bi->acc_dchg_cap>>24)&0xff;
	data[3] = (bi->acc_dchg_cap>>16)&0xff;
	data[4] = (bi->acc_dchg_cap>>8)&0xff;
	data[5] = (bi->acc_dchg_cap)&0xff;
	rt5025_write_reg(bi->client, 0x23, &data[2], 4);
	//cycle_count
	data[6] = (bi->cycle_cnt)&0xff;
	rt5025_write_reg(bi->client, 0x27, &data[6], 1);
	//soc
	data[7] = (bi->soc)&0xff;
	rt5025_write_reg(bi->client, 0x28, &data[7], 1);
	//gauge_timer
	data[8] = (bi->pre_gauge_timer>>8)&0xff;
	data[9] = bi->pre_gauge_timer&0xff;
	rt5025_write_reg(bi->client, 0x29, &data[8], 2);
	//CRC value
	crc_value = get_crc16_value(data, 10);
	data[10] = crc_value&0xff;
	data[11] = (crc_value>>8)&0xff;
	rt5025_write_reg(bi->client, 0x2b, &data[10], 2);
	return 0;
}

static int rt5025_battery_parameter_restore(struct rt5025_battery_info *bi)
{
	u8 data[4];
	RTINFO("\n");
	//restore fcc_aging, rm ,cycle_count, acc_dchg_cap
	//fcc_aging
	rt5025_read_reg(bi->client, 0x21, data, 2);
	bi->fcc = bi->fcc_aging = data[0]<<8 | data[1];
	//acc_dchg_cap
	rt5025_read_reg(bi->client, 0x23, data, 4);
	bi->acc_dchg_cap = data[0]<<24 | data[1]<<16 | data[2]<<8 | data[3];
	//cycle_count
	rt5025_read_reg(bi->client, 0x27, data, 1);
	bi->cycle_cnt = data[0];
	//soc
	rt5025_read_reg(bi->client, 0x28, data, 1);
	bi->soc = bi->pre_soc = data[0];
	//pre_gauge_timer
	rt5025_read_reg(bi->client, 0x29, data, 2);
	bi->pre_gauge_timer = bi->gauge_timer = (data[0]<<8) + data[1];
	
	return 0;
}


// return value; 1-> initialized, 0-> no initial value
static int rt5025_battery_parameter_initcheck(struct rt5025_battery_info *bi)
{
	u8 data[12] = {0};
	int ret = 0;

	if (rt5025_read_reg(bi->client, 0x21, data, 12)<0)
	{
		pr_err("%s: check initial value error\n", __func__);
		return 0;
	}
	else
	{
		ret = IsCrc16Good(data, 12);
	}
	RTINFO("initial check = %d\n", ret);

	return ret;
}

static void rt5025_register_init(struct rt5025_battery_info *bi)
{  
	u8 data[1];

	/* enable the channel of current,qc,ain,vbat and vadc */
	if (rt5025_read_reg(bi->client, RT5025_REG_CHANNEL_LSB, data, 1) < 0){
		pr_err("%s: failed to read channel\n", __func__);
	}
	RTINFO("initial change enable=%02x\n", data[0]);
	data[0] = data[0] | CHANNEL_L_BIT_CADC_EN | CHANNEL_L_BIT_AINCH | \
		CHANNEL_L_BIT_VBATSCH | CHANNEL_L_BIT_VADC_EN | CHANNEL_L_BIT_INTEMPCH;
	if (rt5025_write_reg(bi->client, RT5025_REG_CHANNEL_LSB, data, 1) < 0){
		pr_err("%s: failed to write channel\n", __func__);
	}
	/* set the alert threshold value */
	irq_thres[MINVOLT2] = VALRTMIN2_VALUE;
	irq_thres[VOLT_RLS] = VRLS_VALUE;

	bi->chg_cc_unuse = 0;
	bi->dchg_cc_unuse = 0;
	bi->pre_gauge_timer = 0;
	bi->online = 1;
	bi->status = bi->internal_status = POWER_SUPPLY_STATUS_DISCHARGING;
	bi->health = POWER_SUPPLY_HEALTH_GOOD;
	
	bi->init_cap = true;
	bi->avg_flag = true;

	bi->fcc_aging = rt5025_battery_param2[4].y;
	bi->fcc = rt5025_battery_param2[4].y;
	bi->dc = rt5025_battery_param2[4].y;
	bi->rm = 0;

	bi->edv_cnt = 0;
	bi->edv_flag = false;
	bi->edv_detection = false;
	bi->init_once = true;

	bi->tp_cnt = 0;
	bi->tp_flag = false;
  
	bi->acc_dchg_cap = 0;
	bi->cycle_cnt = 0;
	bi->empty_edv = rt5025_battery_param2[4].x;
	bi->edv_region = 0;

	// if has initial data, rewrite to the stored data
	if (rt5025_battery_parameter_initcheck(bi))
	{
		bi->init_cap = false;
		rt5025_battery_parameter_restore(bi);
		//bi->soc = bi->rm/36/bi->fcc_aging;
		bi->rm = bi->soc*bi->fcc_aging*36;
	}

	bi->update_time = NORMAL_POLL;
	bi->device_suspend = false;
	RTINFO("register initialized\n");
}

static void rt5025_soc_aging(struct rt5025_battery_info *bi)
{
	if (bi->cycle_cnt >= rt5025_battery_param2[3].x)
	{
		bi->fcc_aging = bi->fcc_aging*(1000-rt5025_battery_param2[3].y)/1000;
		bi->rm = bi->rm*(1000-rt5025_battery_param2[3].y)/1000;
		bi->cycle_cnt -= rt5025_battery_param2[3].x;
	}
	RTINFO("fcc_aging=%d, rm=%d, cycle_cnt=%d\n", bi->fcc_aging, bi->rm, bi->cycle_cnt);
}

static void rt5025_temp_comp(struct rt5025_battery_info *bi)
{
	int i = 1;
  int size;
  int slope, const_term;
  int delta_y, delta_x;	
  
  size = 3;
  while((bi->ext_temp < rt5025_battery_param2[i].x) &&
				(i < (size - 1))){
    i++;
  }

  delta_x = rt5025_battery_param2[i-1].x - rt5025_battery_param2[i].x;
  delta_y = (rt5025_battery_param2[i-1].y - rt5025_battery_param2[i].y);

  slope = delta_y  * 1000 / delta_x;

  const_term = (rt5025_battery_param2[i].y) - ((rt5025_battery_param2[i].x * slope) / 1000);

  if (bi->ext_temp >= rt5025_battery_param2[0].x)
    bi->tempcmp = rt5025_battery_param2[0].y; 
  else if (bi->ext_temp <= rt5025_battery_param2[size-1].x)
    bi->tempcmp = rt5025_battery_param2[size-1].y;
  else
    bi->tempcmp = (bi->ext_temp * slope) / 1000 + const_term;
	
  bi->fcc = bi->fcc_aging + bi->fcc_aging * bi->tempcmp /1000;
  if (bi->fcc >= (bi->dc*3>>1))
	bi->fcc = bi->dc*3>>1;
  if (bi->fcc <= (bi->dc>>1))
	bi->fcc = bi->dc>>1;
  bi->rm = bi->fcc * bi->soc * 36;
  //pr_err("[rt5025] i=%d, delta_x=%d, delta_y=%d, slope=%d, const_term=%d\n", i, delta_x, delta_y, slope, const_term);
  RTINFO("tempcmp=%d, ext_temp=%d, fcc=%d, rm=%d\n", bi->tempcmp, bi->ext_temp, bi->fcc, bi->rm);
  return;		
}

static void rt5025_soc_temp_comp(struct rt5025_battery_info *bi)
{
  RTINFO("soc->%d++\n", bi->soc);
  bi->temp_range_0_5 = false;
  bi->temp_range_5_10 = false;
  bi->temp_range_10_15 = false;
  bi->temp_range_15_20 = false;
  bi->temp_range_20_30 = false; 
  bi->temp_range_30_35 = false;
  bi->temp_range_35_40 = false;
  bi->temp_range_40_45 = false;
  bi->temp_range_45_50 = false;
  
  if (bi->ext_temp < 50)
	bi->temp_range_0_5 = true;
  else if (50 <= bi->ext_temp && bi->ext_temp < 100)
  	bi->temp_range_5_10 = true;
  else if (100 <= bi->ext_temp && bi->ext_temp < 150)
  	bi->temp_range_10_15 = true;
  else if (150 <= bi->ext_temp && bi->ext_temp < 200)
 		bi->temp_range_15_20 = true;
  else if (200 <= bi->ext_temp && bi->ext_temp <= 300)
  	bi->temp_range_20_30 = true;
  else if (300 < bi->ext_temp && bi->ext_temp <= 350)
  	bi->temp_range_30_35 = true;
  else if (350 < bi->ext_temp && bi->ext_temp <= 400)
  	bi->temp_range_35_40 = true;
  else if (400 < bi->ext_temp && bi->ext_temp <= 450)
  	bi->temp_range_40_45 = true;
  else if (450 < bi->ext_temp)
  	bi->temp_range_45_50 = true;
  	
  if((bi->temp_range_0_5 == true) && (bi->range_0_5_done == false)) 	
  {
  	rt5025_temp_comp(bi);
  	bi->range_0_5_done = true;
  	bi->range_5_10_done = false;
  	bi->range_10_15_done = false;
  	bi->range_15_20_done = false;
  	bi->range_20_30_done = false;
  	bi->range_30_35_done = false;
  	bi->range_35_40_done = false;
  	bi->range_40_45_done = false;
  	bi->range_45_50_done = false;
  }
	else if((bi->temp_range_5_10 == true) && (bi->range_5_10_done == false)) 	
  {
  	rt5025_temp_comp(bi);
  	bi->range_0_5_done = false;
  	bi->range_5_10_done = true;
  	bi->range_10_15_done = false;
  	bi->range_15_20_done = false;
  	bi->range_20_30_done = false;
  	bi->range_30_35_done = false;
  	bi->range_35_40_done = false;
  	bi->range_40_45_done = false;
  	bi->range_45_50_done = false;
  }
  else if((bi->temp_range_10_15 == true) && (bi->range_10_15_done == false)) 	
  {
    rt5025_temp_comp(bi);
    bi->range_0_5_done = false;
  	bi->range_5_10_done = false;
  	bi->range_10_15_done = true;
  	bi->range_15_20_done = false;
  	bi->range_20_30_done = false;
  	bi->range_30_35_done = false;
  	bi->range_35_40_done = false;
  	bi->range_40_45_done = false;
  	bi->range_45_50_done = false;
  }
  else if((bi->temp_range_15_20 == true) && (bi->range_15_20_done == false)) 	
  {
  	rt5025_temp_comp(bi);
  	bi->range_0_5_done = false;
  	bi->range_5_10_done = false;
  	bi->range_10_15_done = false;
  	bi->range_15_20_done = true;
  	bi->range_20_30_done = false;
  	bi->range_30_35_done = false;
  	bi->range_35_40_done = false;
  	bi->range_40_45_done = false;
  	bi->range_45_50_done = false;
  }
  else if((bi->temp_range_20_30 == true) && (bi->range_20_30_done == false)) 	
  {
  	bi->fcc = bi->fcc_aging;
	bi->rm = bi->fcc*bi->soc*36;
  	bi->range_0_5_done = false;
  	bi->range_5_10_done = false;
  	bi->range_10_15_done = false;
  	bi->range_15_20_done = false;
  	bi->range_20_30_done = true;
  	bi->range_30_35_done = false;
  	bi->range_35_40_done = false;
  	bi->range_40_45_done = false;
  	bi->range_45_50_done = false;
  }  
  else if((bi->temp_range_30_35 == true) && (bi->range_30_35_done == false)) 	
  {
  	rt5025_temp_comp(bi);
  	bi->range_0_5_done = false;
  	bi->range_5_10_done = false;
  	bi->range_10_15_done = false;
  	bi->range_15_20_done = false;
  	bi->range_20_30_done = false;
  	bi->range_30_35_done = true;
  	bi->range_35_40_done = false;
  	bi->range_40_45_done = false;
  	bi->range_45_50_done = false;
  }  
  else if((bi->temp_range_35_40 == true) && (bi->range_35_40_done == false)) 	
  {
  	rt5025_temp_comp(bi);
  	bi->range_0_5_done = false;
  	bi->range_5_10_done = false;
  	bi->range_10_15_done = false;
  	bi->range_15_20_done = false;
  	bi->range_20_30_done = false;
  	bi->range_30_35_done = false;
  	bi->range_35_40_done = true;
  	bi->range_40_45_done = false;
  	bi->range_45_50_done = false;
  }  
  else if((bi->temp_range_40_45 == true) && (bi->range_40_45_done == false)) 	
  {
  	rt5025_temp_comp(bi);
  	bi->range_0_5_done = false;
  	bi->range_5_10_done = false;
  	bi->range_10_15_done = false;
  	bi->range_15_20_done = false;
  	bi->range_20_30_done = false;
  	bi->range_30_35_done = false;
  	bi->range_35_40_done = false;
  	bi->range_40_45_done = true;
  	bi->range_45_50_done = false;
  }  
  else if((bi->temp_range_45_50 == true) && (bi->range_45_50_done == false)) 	
  {
  	rt5025_temp_comp(bi);
  	bi->range_0_5_done = false;
  	bi->range_5_10_done = false;
  	bi->range_10_15_done = false;
  	bi->range_15_20_done = false;
  	bi->range_20_30_done = false;
  	bi->range_30_35_done = false;
  	bi->range_35_40_done = false;
  	bi->range_40_45_done = false;
  	bi->range_45_50_done = true;
  } 
  RTINFO("soc->%d--\n", bi->soc);
}

static void rt5025_update(struct rt5025_battery_info *bi)
{
  /* Update voltage */
  rt5025_get_vcell(bi);
  /* Update current */
  rt5025_get_current(bi);
  /* Update internal temperature */
  rt5025_get_internal_temp(bi);	
  /* Update external temperature */
  rt5025_get_external_temp(bi);
  /* Read timer */
  rt5025_get_timer(bi);
  /* Update chg cc */
  rt5025_get_chg_cc(bi);
  /* Update dchg cc */
  rt5025_get_dchg_cc(bi);
  /* Update cycle count check */
  rt5025_cycle_count(bi);
  /* Calculate cycle count */
  rt5025_soc_aging(bi);
  /* calculate initial soc */
  if (bi->init_cap){  
    rt5025_init_capacity(bi);
#if RT5025_CSV
    pr_info("vcell,offset,current,timer,interval,QCHG,QDCHG,tp_cnt,tp_flag,edv_det,edv_cnt,edv_flag,soc,permille,RM,FCC,smooth_flag,acc_QD,cycle,update_time\n");
#endif
  }

	/* Relearn SOC */
	rt5025_soc_relearn_check(bi);
	/* SOC_Temp_Comp*/
	rt5025_soc_temp_comp(bi);
  /* Update SOC */
  rt5025_get_soc(bi);
  
  /* SOC Control Process */
  rt5025_soc_lock(bi);
  rt5025_soc_irreversible(bi);
  
  /* Update RTTF or RTTE */
  //rt5025_run_time(bi);
	
#if TEMPERATURE_ALERT 
  if ((bi->max_temp_irq == false) &&
		  (((irq_thres[MAXTEMP] * IRQ_THRES_UNIT) / 100 - bi->ain_volt) > irq_thres[TEMP_RLS])){
		rt5025_alert_setting(bi,MAXTEMP,true);
	}else if ((bi->min_temp_irq == false) &&
					  ((bi->ain_volt - (irq_thres[MINTEMP] * IRQ_THRES_UNIT) / 100) > irq_thres[TEMP_RLS])){
		rt5025_alert_setting(bi,MINTEMP,true);
  }
#endif  
#if 0		
	}else if ((bi->min_volt1_irq == false) &&
					((bi->vcell - ((irq_thres[MINVOLT1] * IRQ_THRES_UNIT) / 100)) > irq_thres[VOLT_RLS])){
		rt5025_alert_setting(bi,MINVOLT1,true);				
	}else if ((bi->min_volt2_irq == false) &&
					((bi->vcell - ((irq_thres[MINVOLT2] * IRQ_THRES_UNIT) / 100)) > irq_thres[VOLT_RLS])){
		rt5025_alert_setting(bi,MINVOLT2,true);						
	}
#endif
	
#if VOLTAGE_ALERT
	if ((bi->min_volt2_irq == false) &&
		  	  	(bi->vcell > (bi->empty_edv + EDV_HYS))){	
		rt5025_alert_setting(bi,MINVOLT2,true);						
	}
#endif
#if RT5025_CSV
  printk(KERN_INFO "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",bi->vcell,bi->curr_offset,bi->curr,bi->gauge_timer,bi->time_interval,bi->chg_cc,
		bi->dchg_cc,bi->tp_cnt,bi->tp_flag,bi->edv_detection,bi->edv_cnt,bi->edv_flag,bi->soc,
		bi->permille,bi->rm,bi->fcc,bi->smooth_flag,bi->acc_dchg_cap,bi->cycle_cnt,bi->update_time);
#else
  RTINFO("[RT5025] update_time=%d\n",bi->update_time);
  RTINFO("\n");
#endif
}

static void rt5025_update_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = (struct delayed_work *)container_of(work, struct delayed_work, work);
	struct rt5025_battery_info *bi = (struct rt5025_battery_info *)container_of(delayed_work, struct rt5025_battery_info, monitor_work);
	unsigned long flags;

	wake_lock(&bi->monitor_wake_lock);	
	rt5025_update(bi);
	power_supply_changed(&bi->battery);
  
	/* prevent suspend before starting the alarm */
	local_irq_save(flags);
	bi->last_poll = alarm_get_elapsed_realtime();
	rt5025_program_alarm(bi);
	local_irq_restore(flags);

	wake_unlock(&bi->monitor_wake_lock);
	if (!bi->device_suspend)
		schedule_delayed_work(&bi->monitor_work, bi->update_time*HZ);
}

static enum power_supply_property rt5025_battery_props[] = {
  POWER_SUPPLY_PROP_STATUS,
  POWER_SUPPLY_PROP_HEALTH,
  POWER_SUPPLY_PROP_PRESENT,
  POWER_SUPPLY_PROP_TEMP,
  POWER_SUPPLY_PROP_ONLINE,
  POWER_SUPPLY_PROP_VOLTAGE_NOW,
  POWER_SUPPLY_PROP_CURRENT_NOW,
  POWER_SUPPLY_PROP_CAPACITY,
  POWER_SUPPLY_PROP_TECHNOLOGY,
  #if 0
  POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
  POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
  #endif
};

static int rt5025_battery_sleepvth_setting(struct rt5025_battery_info* bi)
{
	u32 temp;
	u8 vmax_th, vmin_th;
	u8 vbat[2];
	RTINFO("\n");
	rt5025_read_reg(bi->client, RT5025_REG_VCELL_MSB, vbat, 2);
	temp = ((vbat[0]<<8) + vbat[1])*61;
	vmax_th = (temp+5000)/1953;
	vmin_th = (temp-5000)/1953;

	rt5025_write_reg(bi->client, RT5025_REG_VALRT_MAXTH, &vmax_th, 1);
	rt5025_write_reg(bi->client, RT5025_REG_VALRT_MIN1TH, &vmin_th, 1);

	RTINFO("vmax_th=0x%02x, vmin_th=0x%02x\n", vmax_th, vmin_th);
	return 0;
}

static int rt5025_battery_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rt5025_battery_info *bi = platform_get_drvdata(pdev);
	RTINFO("\n");
	//rt5025_get_timer(bi);
	//bi->last_event = ktime_get();
	bi->last_event = current_kernel_time();

	//cy add for battery parameter backup
	//rt5025_battery_parameter_backup(bi);

	//rt5025_channel_cc(bi, false);
	//rt5025_update(bi);
	bi->device_suspend = true;
	cancel_delayed_work_sync(&bi->monitor_work);
	/* prevent suspend before starting the alarm */
	//bi->update_time = SUSPEND_POLL;
	rt5025_alert_setting(bi,MAXVOLT, false);
	rt5025_alert_setting(bi,MINVOLT1,false);
	rt5025_battery_sleepvth_setting(bi);
	rt5025_alert_setting(bi,MAXVOLT, true);
	rt5025_alert_setting(bi,MINVOLT1,true);
	RTINFO("RM=%d\n",bi->rm);
	return 0;
}

static int rt5025_battery_resume(struct platform_device *pdev)
{
	struct rt5025_battery_info *bi = platform_get_drvdata(pdev);
	//ktime_t now;
	//struct timespec now = current_kernel_time();
	//struct timeval tv;
	//long time_interval;

	//now = ktime_get();
	//tv = ktime_to_timeval(ktime_sub(now, bi->last_event));
	//RTINFO("Sleep time = %d\n",(u32)tv.tv_sec);
	//bi->rm = bi->rm - ((u32)tv.tv_sec * SLEEP_CURRENT);

	//time_interval = now.tv_sec - bi->last_event.tv_sec;
	//bi->rm = bi->rm - (time_interval * SLEEP_CURRENT);
	//RTINFO("Sleep time=%d, RM=%d",(int)time_interval,bi->rm);  

	//rt5025_channel_cc(bi, true);
	bi->last_suspend = true;
	bi->device_suspend = false;
	schedule_delayed_work(&bi->monitor_work, 0);
	RTINFO("\n");
	return 0;
}

static int rt5025_battery_remove(struct platform_device *pdev)
{
	struct rt5025_battery_info *bi = platform_get_drvdata(pdev);

	power_supply_unregister(&bi->battery);
	cancel_delayed_work(&bi->monitor_work);
	wake_lock_destroy(&bi->monitor_wake_lock);
	kfree(bi);
	RTINFO("\n");
	return 0;
}

static int rt5025_battery_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_battery_info *bi;
	int ret;

  	bi = kzalloc(sizeof(*bi), GFP_KERNEL);
  	if (!bi)
    		return -ENOMEM;
   
	bi->client = chip->i2c;
	bi->chip = chip;


	bi->last_poll = alarm_get_elapsed_realtime();
	alarm_init(&bi->wakeup_alarm, ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP,
		rt5025_gauge_alarm);

	INIT_DELAYED_WORK(&bi->monitor_work, rt5025_update_work);
	
	wake_lock_init(&bi->monitor_wake_lock, WAKE_LOCK_SUSPEND, "rt-battery-monitor");
	wake_lock_init(&bi->low_battery_wake_lock, WAKE_LOCK_SUSPEND, "low_battery_wake_lock");
	wake_lock_init(&bi->status_wake_lock, WAKE_LOCK_SUSPEND, "battery-status-changed");
#if RT5025_TEST_WAKE_LOCK
	wake_lock_init(&bi->test_wake_lock, WAKE_LOCK_SUSPEND, "rt-test");
#endif
  	/* Write trimed data */
	//rt5025_pretrim(client);
	/* enable channel */
	rt5025_register_init(bi);
	/* enable gauge IRQ */
	rt5025_alert_init(bi);
  
	/* register callback functions */
	/*
	chip->cb.rt5025_gauge_irq_handler = rt5025_irq_handler;
	chip->cb.rt5025_gauge_set_status = rt5025_set_status;
	chip->cb.rt5025_gauge_set_online = rt5025_set_online;
	chip->cb.rt5025_gauge_suspend = rt5025_gauge_suspend;
	chip->cb.rt5025_gauge_resume = rt5025_gauge_resume;
	chip->cb.rt5025_gauge_remove = rt5025_gauge_remove;
	
	rt5025_register_gauge_callbacks(&chip->cb);
	*/

	platform_set_drvdata(pdev, bi);

	bi->battery.name = "rt5025-battery";
	bi->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	bi->battery.get_property = rt5025_get_property;
	bi->battery.properties = rt5025_battery_props;
	bi->battery.num_properties = ARRAY_SIZE(rt5025_battery_props);
  
	ret = power_supply_register(&pdev->dev, &bi->battery);
	if (ret) {
		printk(KERN_ERR "[RT5025] power supply register failed\n");
		goto err_wake_lock;
	}
  

	//wake_lock(&bi->monitor_wake_lock);
#if RT5025_TEST_WAKE_LOCK
	wake_lock(&bi->test_wake_lock);
#endif
	schedule_delayed_work(&bi->monitor_work, msecs_to_jiffies(INIT_POLL*MSEC_PER_SEC));
	chip->battery_info = bi;

	pr_info("rt5025-battery driver is successfully loaded\n");
	
  return 0;

err_wake_lock:
	wake_lock_destroy(&bi->monitor_wake_lock);
	kfree(bi);

	return ret;
}

static void rt5025_battery_shutdown(struct platform_device *pdev)
{
	struct rt5025_battery_info *bi = platform_get_drvdata(pdev);
	RTINFO("\n");
	if (bi->soc == 0 && bi->cal_fcc != 0 )
	{
		bi->fcc_aging = bi->cal_fcc/3600 - (bi->fcc -bi->fcc_aging);
		RTINFO("bi->cal_fcc=%d\n", bi->cal_fcc);
	}
	rt5025_battery_parameter_backup(bi);
	RTINFO("\n");
}

static struct platform_driver rt5025_battery_driver = 
{
	.driver = {
		.name = RT5025_DEVICE_NAME "-battery",
		.owner = THIS_MODULE,
	},
	.probe = rt5025_battery_probe,
	.remove = __devexit_p(rt5025_battery_remove),
	.shutdown = rt5025_battery_shutdown,
	.suspend = rt5025_battery_suspend,
	.resume = rt5025_battery_resume,
};

static int __init rt5025_battery_init(void)
{
	return platform_driver_register(&rt5025_battery_driver);
}
module_init(rt5025_battery_init);

static void __exit rt5025_battery_exit(void)
{
	platform_driver_unregister(&rt5025_battery_driver);
}
module_exit(rt5025_battery_exit);


MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nick Hung <nick_hung@richtek.com");
MODULE_DESCRIPTION("battery gauge driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEVICE_NAME "-battery");
MODULE_VERSION(RT5025_DRV_VER);
