/*
 * Battery charger driver for KrossPower AXP20X
 *
 * Copyright (C) 2011 KrossPower, Ltd.
 *  Zhang Donglu <zhangdonglu@x-powers.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/input.h>
#include <linux/mfd/axp-mfd.h>
#include <asm/div64.h>

#include <plat/sys_config.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include "axp-cfg.h"
#include "axp-sply.h"

#define DBG_AXP_PSY 0
#if  DBG_AXP_PSY
#define DBG_PSY_MSG(format,args...)   printk("[AXP]"format,##args)
#else
#define DBG_PSY_MSG(format,args...)   do {} while (0)
#endif

static int pmu_used2 = 0;
static int gpio_adp_hdle = 0;
static int pmu_suspendpwroff_vol = 0;
static int pmu_earlysuspend_chgcur = 0;
struct axp_adc_res adc;
static int count_rdc = 0;
static int count_dis = 0;
#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend axp_early_suspend;
int early_suspend_flag = 0;
#endif

int ADC_Freq_Get(struct axp_charger *charger)
{
	uint8_t  temp;
	int  rValue = 25;

	axp_read(charger->master, AXP20_ADC_CONTROL3,&temp);
	temp &= 0xc0;
	switch(temp >> 6)
	{
		case 0:
			rValue = 25;
			break;
		case 1:
			rValue = 50;
			break;
		case 2:
			rValue = 100;
			break;
		case 3:
			rValue = 200;
			break;
		default:
			break;
	}
	return rValue;
}

static inline int axp20_vbat_to_mV(uint16_t reg)
{
  return ((int)((( reg >> 8) << 4 ) | (reg & 0x000F))) * 1100 / 1000;
}

static inline int axp20_vdc_to_mV(uint16_t reg)
{
  return ((int)(((reg >> 8) << 4 ) | (reg & 0x000F))) * 1700 / 1000;
}


static inline int axp20_ibat_to_mA(uint16_t reg)
{
    return ((int)(((reg >> 8) << 5 ) | (reg & 0x001F))) * 500 / 1000;
}

static inline int axp20_icharge_to_mA(uint16_t reg)
{
    return ((int)(((reg >> 8) << 4 ) | (reg & 0x000F))) * 500 / 1000;
}

static inline int axp20_iac_to_mA(uint16_t reg)
{
    return ((int)(((reg >> 8) << 4 ) | (reg & 0x000F))) * 625 / 1000;
}

static inline int axp20_iusb_to_mA(uint16_t reg)
{
    return ((int)(((reg >> 8) << 4 ) | (reg & 0x000F))) * 375 / 1000;
}


static inline void axp_read_adc(struct axp_charger *charger,
  struct axp_adc_res *adc)
{
  uint8_t tmp[8];

  axp_reads(charger->master,AXP20_VACH_RES,8,tmp);
  adc->vac_res = ((uint16_t) tmp[0] << 8 )| tmp[1];
  adc->iac_res = ((uint16_t) tmp[2] << 8 )| tmp[3];
  adc->vusb_res = ((uint16_t) tmp[4] << 8 )| tmp[5];
  adc->iusb_res = ((uint16_t) tmp[6] << 8 )| tmp[7];
  axp_reads(charger->master,AXP20_VBATH_RES,6,tmp);
  adc->vbat_res = ((uint16_t) tmp[0] << 8 )| tmp[1];
  adc->ichar_res = ((uint16_t) tmp[2] << 8 )| tmp[3];
  adc->idischar_res = ((uint16_t) tmp[4] << 8 )| tmp[5];
}


static void axp_charger_update_state(struct axp_charger *charger)
{
  uint8_t val[2];
  uint16_t tmp;

  axp_reads(charger->master,AXP20_CHARGE_STATUS,2,val);
  tmp = (val[1] << 8 )+ val[0];
  charger->is_on = (val[1] & AXP20_IN_CHARGE) ? 1 : 0;
  charger->fault = val[1];
  charger->bat_det = (tmp & AXP20_STATUS_BATEN)?1:0;
  charger->ac_det = (tmp & AXP20_STATUS_ACEN)?1:0;
  charger->usb_det = (tmp & AXP20_STATUS_USBEN)?1:0;
  charger->usb_valid = (tmp & AXP20_STATUS_USBVA)?1:0;
  charger->ac_valid = (tmp & AXP20_STATUS_ACVA)?1:0;
  charger->ext_valid = charger->ac_valid | charger->usb_valid;
  charger->bat_current_direction = (tmp & AXP20_STATUS_BATCURDIR)?1:0;
  charger->in_short = (tmp& AXP20_STATUS_ACUSBSH)?1:0;
  charger->batery_active = (tmp & AXP20_STATUS_BATINACT)?1:0;
  charger->low_charge_current = (tmp & AXP20_STATUS_CHACURLOEXP)?1:0;
  charger->int_over_temp = (tmp & AXP20_STATUS_ICTEMOV)?1:0;
 	axp_read(charger->master,AXP20_CHARGE_CONTROL1,val);
 	charger->charge_on = ((val[0] >> 7) & 0x01);
}

static void axp_charger_update(struct axp_charger *charger)
{
  uint16_t tmp;
  uint8_t val[2];
  //struct axp_adc_res adc;
  charger->adc = &adc;
  axp_read_adc(charger, &adc);
  tmp = charger->adc->vbat_res;
  charger->vbat = axp20_vbat_to_mV(tmp);
   //tmp = charger->adc->ichar_res + charger->adc->idischar_res;
  charger->ibat = ABS(axp20_icharge_to_mA(charger->adc->ichar_res)-axp20_ibat_to_mA(charger->adc->idischar_res));
  tmp = charger->adc->vac_res;
  charger->vac = axp20_vdc_to_mV(tmp);
  tmp = charger->adc->iac_res;
  charger->iac = axp20_iac_to_mA(tmp);
  tmp = charger->adc->vusb_res;
  charger->vusb = axp20_vdc_to_mV(tmp);
  tmp = charger->adc->iusb_res;
  charger->iusb = axp20_iusb_to_mA(tmp);
  axp_reads(charger->master,AXP20_INTTEMP,2,val);
  //DBG_PSY_MSG("TEMPERATURE:val1=0x%x,val2=0x%x\n",val[1],val[0]);
  tmp = (val[0] << 4 ) + (val[1] & 0x0F);
  charger->ic_temp = (int) tmp  - 1447;
  if(!charger->ext_valid){
  	charger->disvbat =  charger->vbat;
  	charger->disibat =  charger->ibat;
  }
}

#if defined  (CONFIG_AXP_CHARGEINIT)
static void axp_set_charge(struct axp_charger *charger)
{
  uint8_t val=0x00;
  uint8_t tmp=0x00;
    if(charger->chgvol < 4150000)
      val &= ~(3 << 5);
    else if (charger->chgvol<4200000){
      val &= ~(3 << 5);
      val |= 1 << 5;
      }
    else if (charger->chgvol<4360000){
      val &= ~(3 << 5);
      val |= 1 << 6;
      }
    else
      val |= 3 << 5;

		if(charger->chgcur == 0)
			charger->chgen = 0;

    if(charger->chgcur< 300000)
      charger->chgcur = 300000;
    else if(charger->chgcur > 1800000)
     charger->chgcur = 1800000;

    val |= (charger->chgcur - 200001) / 100000 ;
    if(charger ->chgend == 10){
      val &= ~(1 << 4);
    }
    else {
      val |= 1 << 4;
    }
    val &= 0x7F;
    val |= charger->chgen << 7;
      if(charger->chgpretime < 30)
      charger->chgpretime = 30;
    if(charger->chgcsttime < 360)
      charger->chgcsttime = 360;

    tmp = ((((charger->chgpretime - 40) / 10) << 6)  \
      | ((charger->chgcsttime - 360) / 120));
	axp_write(charger->master, AXP20_CHARGE_CONTROL1,val);
	axp_update(charger->master, AXP20_CHARGE_CONTROL2,tmp,0xC2);
}
#else
static void axp_set_charge(struct axp_charger *charger)
{

}
#endif

static enum power_supply_property axp_battery_props[] = {
  POWER_SUPPLY_PROP_MODEL_NAME,
  POWER_SUPPLY_PROP_STATUS,
  POWER_SUPPLY_PROP_PRESENT,
  POWER_SUPPLY_PROP_ONLINE,
  POWER_SUPPLY_PROP_HEALTH,
  POWER_SUPPLY_PROP_TECHNOLOGY,
  POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
  POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
  POWER_SUPPLY_PROP_VOLTAGE_NOW,
  POWER_SUPPLY_PROP_CURRENT_NOW,
  //POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
  //POWER_SUPPLY_PROP_CHARGE_FULL,
  POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
  POWER_SUPPLY_PROP_CAPACITY,
  //POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
  //POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
  POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_property axp_ac_props[] = {
  POWER_SUPPLY_PROP_MODEL_NAME,
  POWER_SUPPLY_PROP_PRESENT,
  POWER_SUPPLY_PROP_ONLINE,
  POWER_SUPPLY_PROP_VOLTAGE_NOW,
  POWER_SUPPLY_PROP_CURRENT_NOW,
};

static enum power_supply_property axp_usb_props[] = {
  POWER_SUPPLY_PROP_MODEL_NAME,
  POWER_SUPPLY_PROP_PRESENT,
  POWER_SUPPLY_PROP_ONLINE,
  POWER_SUPPLY_PROP_VOLTAGE_NOW,
  POWER_SUPPLY_PROP_CURRENT_NOW,
};

static void axp_battery_check_status(struct axp_charger *charger,
            union power_supply_propval *val)
{
  if (charger->bat_det) {
    if (charger->ext_valid){
    	if( charger->rest_vol == 100)
        val->intval = POWER_SUPPLY_STATUS_FULL;
    	else if(charger->charge_on)
    		val->intval = POWER_SUPPLY_STATUS_CHARGING;
    	else
    		val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
    }
    else
      val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
  }
  else
    val->intval = POWER_SUPPLY_STATUS_FULL;
}

static void axp_battery_check_health(struct axp_charger *charger,
            union power_supply_propval *val)
{
    if (charger->fault & AXP20_FAULT_LOG_BATINACT)
    val->intval = POWER_SUPPLY_HEALTH_DEAD;
  else if (charger->fault & AXP20_FAULT_LOG_OVER_TEMP)
    val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
  else if (charger->fault & AXP20_FAULT_LOG_COLD)
    val->intval = POWER_SUPPLY_HEALTH_COLD;
  else
    val->intval = POWER_SUPPLY_HEALTH_GOOD;
}

static int axp_battery_get_property(struct power_supply *psy,
           enum power_supply_property psp,
           union power_supply_propval *val)
{
  struct axp_charger *charger;
  int ret = 0;
  charger = container_of(psy, struct axp_charger, batt);

  switch (psp) {
  case POWER_SUPPLY_PROP_STATUS:
    axp_battery_check_status(charger, val);
    break;
  case POWER_SUPPLY_PROP_HEALTH:
    axp_battery_check_health(charger, val);
    break;
  case POWER_SUPPLY_PROP_TECHNOLOGY:
    val->intval = charger->battery_info->technology;
    break;
  case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
    val->intval = charger->battery_info->voltage_max_design;
    break;
  case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
    val->intval = charger->battery_info->voltage_min_design;
    break;
  case POWER_SUPPLY_PROP_VOLTAGE_NOW:
    val->intval = charger->ocv * 1000;
    break;
  case POWER_SUPPLY_PROP_CURRENT_NOW:
    val->intval = charger->ibat * 1000;
    break;
  case POWER_SUPPLY_PROP_MODEL_NAME:
    val->strval = charger->batt.name;
    break;
/*  case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
  case POWER_SUPPLY_PROP_CHARGE_FULL:
    val->intval = charger->battery_info->charge_full_design;
        break;
*/
  case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
    val->intval = charger->battery_info->energy_full_design;
  //  DBG_PSY_MSG("POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:%d\n",val->intval);
       break;
  case POWER_SUPPLY_PROP_CAPACITY:
    val->intval = charger->rest_vol;
    break;
/*  case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
    if(charger->bat_det && !(charger->is_on) && !(charger->ext_valid))
      val->intval = charger->rest_time;
    else
      val->intval = 0;
    break;
  case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
    if(charger->bat_det && charger->is_on)
      val->intval = charger->rest_time;
    else
      val->intval = 0;
    break;
*/
  case POWER_SUPPLY_PROP_ONLINE:
    val->intval = (!charger->is_on)&&(charger->bat_det) && (! charger->ext_valid);
    break;
  case POWER_SUPPLY_PROP_PRESENT:
    val->intval = charger->bat_det;
    break;
  case POWER_SUPPLY_PROP_TEMP:
    //val->intval = charger->ic_temp - 200;
    val->intval =  300;
    break;
  default:
    ret = -EINVAL;
    break;
  }

  return ret;
}

static int axp_ac_get_property(struct power_supply *psy,
           enum power_supply_property psp,
           union power_supply_propval *val)
{
  struct axp_charger *charger;
  int ret = 0;
  charger = container_of(psy, struct axp_charger, ac);

  switch(psp){
  case POWER_SUPPLY_PROP_MODEL_NAME:
    val->strval = charger->ac.name;break;
  case POWER_SUPPLY_PROP_PRESENT:
    val->intval = charger->ac_det;
    break;
  case POWER_SUPPLY_PROP_ONLINE:
    val->intval = charger->ac_valid;break;
  case POWER_SUPPLY_PROP_VOLTAGE_NOW:
    val->intval = charger->vac * 1000;
    break;
  case POWER_SUPPLY_PROP_CURRENT_NOW:
    val->intval = charger->iac * 1000;
    break;
  default:
    ret = -EINVAL;
    break;
  }
   return ret;
}

static int axp_usb_get_property(struct power_supply *psy,
           enum power_supply_property psp,
           union power_supply_propval *val)
{
  struct axp_charger *charger;
  int ret = 0;
  charger = container_of(psy, struct axp_charger, usb);

  switch(psp){
  case POWER_SUPPLY_PROP_MODEL_NAME:
    val->strval = charger->usb.name;break;
  case POWER_SUPPLY_PROP_PRESENT:
    val->intval = charger->usb_det;
    break;
  case POWER_SUPPLY_PROP_ONLINE:
    val->intval = charger->usb_valid;
    break;
  case POWER_SUPPLY_PROP_VOLTAGE_NOW:
    val->intval = charger->vusb * 1000;
    break;
  case POWER_SUPPLY_PROP_CURRENT_NOW:
    val->intval = charger->iusb * 1000;
    break;
  default:
    ret = -EINVAL;
    break;
  }
   return ret;
}

static void axp_change(struct axp_charger *charger)
{
  DBG_PSY_MSG("battery state change\n");
  axp_charger_update_state(charger);
  axp_charger_update(charger);
  flag_state_change = 1;
  power_supply_changed(&charger->batt);
}

static void axp_presslong(struct axp_charger *charger)
{
	DBG_PSY_MSG("press long\n");
	input_report_key(powerkeydev, KEY_POWER, 1);
	input_sync(powerkeydev);
	ssleep(2);
	DBG_PSY_MSG("press long up\n");
	input_report_key(powerkeydev, KEY_POWER, 0);
	input_sync(powerkeydev);
}

static void axp_pressshort(struct axp_charger *charger)
{
	DBG_PSY_MSG("press short\n");
  input_report_key(powerkeydev, KEY_POWER, 1);
  input_sync(powerkeydev);
  msleep(100);
  input_report_key(powerkeydev, KEY_POWER, 0);
  input_sync(powerkeydev);
}

static void axp_capchange(struct axp_charger *charger)
{
	uint8_t val;
	int k;

	DBG_PSY_MSG("battery change\n");
	ssleep(2);
  axp_charger_update_state(charger);
  axp_charger_update(charger);
  axp_read(charger->master, AXP20_CAP,&val);
  charger->rest_vol = (int) (val & 0x7F);

  if((charger->bat_det == 0) || (charger->rest_vol == 127)){
  	charger->rest_vol = 100;
  }

  DBG_PSY_MSG("rest_vol = %d\n",charger->rest_vol);
  memset(Bat_Cap_Buffer, 0, sizeof(Bat_Cap_Buffer));
  for(k = 0;k < AXP20_VOL_MAX; k++){
    Bat_Cap_Buffer[k] = charger->rest_vol;
  }
  Total_Cap = charger->rest_vol * AXP20_VOL_MAX;
  power_supply_changed(&charger->batt);
}

static void axp_close(struct axp_charger *charger)
{
	charger->rest_vol = 5;
	axp_write(charger->master,AXP20_DATA_BUFFER1,0x85);
	DBG_PSY_MSG("\n==================event in close==============\n");
	power_supply_changed(&charger->batt);
}


static int axp_battery_event(struct notifier_block *nb, unsigned long event,
        void *data)
{
    struct axp_charger *charger =
    container_of(nb, struct axp_charger, nb);

    uint8_t w[9];

    w[0] = (uint8_t) ((event) & 0xFF);
    w[1] = POWER20_INTSTS2;
    w[2] = (uint8_t) ((event >> 8) & 0xFF);
    w[3] = POWER20_INTSTS3;
    w[4] = (uint8_t) ((event >> 16) & 0xFF);
    w[5] = POWER20_INTSTS4;
    w[6] = (uint8_t) ((event >> 24) & 0xFF);
    w[7] = POWER20_INTSTS5;
    w[8] = (uint8_t) (((uint64_t) event >> 32) & 0xFF);

    if(event & (AXP20_IRQ_BATIN|AXP20_IRQ_BATRE)) {
    	axp_capchange(charger);
    }

    if(event & (AXP20_IRQ_ACIN|AXP20_IRQ_USBIN|AXP20_IRQ_ACOV|AXP20_IRQ_USBOV|AXP20_IRQ_CHAOV
               |AXP20_IRQ_CHAST|AXP20_IRQ_TEMOV|AXP20_IRQ_TEMLO)) {
        axp_change(charger);
    }

    if(event & (AXP20_IRQ_ACRE|AXP20_IRQ_USBRE)) {
    	axp_change(charger);
    	axp_clr_bits(charger->master,0x32,0x38);
    }

    if(event & AXP20_IRQ_PEKLO) {
    	axp_presslong(charger);
    }

    if(event & AXP20_IRQ_PEKSH) {
    	axp_pressshort(charger);
    }

    DBG_PSY_MSG("event = 0x%x\n",(int) event);
    axp_writes(charger->master,POWER20_INTSTS1,9,w);

    return 0;
}

static char *supply_list[] = {
  "battery",
};



static void axp_battery_setup_psy(struct axp_charger *charger)
{
  struct power_supply *batt = &charger->batt;
  struct power_supply *ac = &charger->ac;
  struct power_supply *usb = &charger->usb;
  struct power_supply_info *info = charger->battery_info;

  batt->name = "battery";
  batt->use_for_apm = info->use_for_apm;
  batt->type = POWER_SUPPLY_TYPE_BATTERY;
  batt->get_property = axp_battery_get_property;

  batt->properties = axp_battery_props;
  batt->num_properties = ARRAY_SIZE(axp_battery_props);

  ac->name = "ac";
  ac->type = POWER_SUPPLY_TYPE_MAINS;
  ac->get_property = axp_ac_get_property;

  ac->supplied_to = supply_list,
  ac->num_supplicants = ARRAY_SIZE(supply_list),

  ac->properties = axp_ac_props;
  ac->num_properties = ARRAY_SIZE(axp_ac_props);

  usb->name = "usb";
  usb->type = POWER_SUPPLY_TYPE_USB;
  usb->get_property = axp_usb_get_property;

  usb->supplied_to = supply_list,
  usb->num_supplicants = ARRAY_SIZE(supply_list),

  usb->properties = axp_usb_props;
  usb->num_properties = ARRAY_SIZE(axp_usb_props);
};

#if defined  (CONFIG_AXP_CHARGEINIT)
static int axp_battery_adc_set(struct axp_charger *charger)
{
   int ret ;
   uint8_t val;

  /*enable adc and set adc */
  val= AXP20_ADC_BATVOL_ENABLE | AXP20_ADC_BATCUR_ENABLE
  | AXP20_ADC_DCINCUR_ENABLE | AXP20_ADC_DCINVOL_ENABLE
  | AXP20_ADC_USBVOL_ENABLE | AXP20_ADC_USBCUR_ENABLE;

	ret = axp_update(charger->master, AXP20_ADC_CONTROL1, val , val);
  if (ret)
    return ret;
    ret = axp_read(charger->master, AXP20_ADC_CONTROL3, &val);
  switch (charger->sample_time/25){
  case 1: val &= ~(3 << 6);break;
  case 2: val &= ~(3 << 6);val |= 1 << 6;break;
  case 4: val &= ~(3 << 6);val |= 2 << 6;break;
  case 8: val |= 3 << 6;break;
  default: break;
  }
  ret = axp_write(charger->master, AXP20_ADC_CONTROL3, val);
  if (ret)
    return ret;

  return 0;
}
#else
static int axp_battery_adc_set(struct axp_charger *charger)
{
  return 0;
}
#endif

static int axp_battery_first_init(struct axp_charger *charger)
{
   int ret;
   uint8_t val;
   axp_set_charge(charger);
   ret = axp_battery_adc_set(charger);
   if(ret)
    return ret;

   ret = axp_read(charger->master, AXP20_ADC_CONTROL3, &val);
   switch ((val >> 6) & 0x03){
  case 0: charger->sample_time = 25;break;
  case 1: charger->sample_time = 50;break;
  case 2: charger->sample_time = 100;break;
  case 3: charger->sample_time = 200;break;
  default:break;
  }
  return ret;
}

static int axp_get_rdc(struct axp_charger *charger)
{
	int temp;
	int rdc;
	uint8_t v[2];
	axp_reads(charger->master,0xba,2,v);
	rdc = (((v[0] & 0x1F) << 8) | v[1]) * 10742 / 10000;
	DBG_PSY_MSG("===========================calculate rdc \n");

	if(!charger->bat_det){
		charger->disvbat = 0;
		charger->disibat = 0;
		return rdc;
	}
	if( charger->ext_valid){
		axp_charger_update(charger);
		if( axp20_ibat_to_mA(charger->adc->idischar_res) == 0 && axp20_icharge_to_mA(charger->adc->ichar_res) > 200){
		} else {
			DBG_PSY_MSG("%s->%d\n",__FUNCTION__,__LINE__);
			charger->disvbat = 0;
			charger->disibat = 0;
			return rdc;
		}
		DBG_PSY_MSG("CHARGING:      charger->vbat = %d,   charger->ibat = %d\n",charger->vbat,charger->ibat);
		DBG_PSY_MSG("DISCHARGING:charger->disvbat = %d,charger->disibat = %d\n",charger->disvbat,charger->disibat);
		axp_charger_update_state(charger);
		if(!charger->bat_current_direction){
			charger->disvbat = 0;
			charger->disibat = 0;
			return rdc;
		}
		if(charger->disvbat == 0){
			charger->disvbat = 0;
			charger->disibat = 0;
			return rdc;
		}else{
			temp = 1000 * ABS(charger->vbat - charger->disvbat) / ABS(charger->ibat + charger->disibat);
			DBG_PSY_MSG("CALRDC:temp = %d\n",temp);
			if((temp < 75) || (temp > pmu_battery_rdc * 2)){
				charger->disvbat = 0;
				charger->disibat = 0;
				return rdc;
			} else {
				axp_set_bits(charger->master,0x04,0x08);
				return temp;
			}
		}
	}else{
		charger->disvbat = 0;
		charger->disibat = 0;
		return rdc;
	}
}

static ssize_t chgen_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val;
  axp_read(charger->master, AXP20_CHARGE_CONTROL1, &val);
  charger->chgen  = val >> 7;
  return sprintf(buf, "%d\n",charger->chgen);
}

static ssize_t chgen_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  var = simple_strtoul(buf, NULL, 10);
  if(var){
    charger->chgen = 1;
    axp_set_bits(charger->master,AXP20_CHARGE_CONTROL1,0x80);
  }
  else{
    charger->chgen = 0;
    axp_clr_bits(charger->master,AXP20_CHARGE_CONTROL1,0x80);
  }
  return count;
}

static ssize_t chgmicrovol_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val;
  axp_read(charger->master, AXP20_CHARGE_CONTROL1, &val);
  switch ((val >> 5) & 0x03){
    case 0: charger->chgvol = 4100000;break;
    case 1: charger->chgvol = 4150000;break;
    case 2: charger->chgvol = 4200000;break;
    case 3: charger->chgvol = 4360000;break;
  }
  return sprintf(buf, "%d\n",charger->chgvol);
}

static ssize_t chgmicrovol_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  uint8_t tmp, val;
  var = simple_strtoul(buf, NULL, 10);
  switch(var){
    case 4100000:tmp = 0;break;
    case 4150000:tmp = 1;break;
    case 4200000:tmp = 2;break;
    case 4360000:tmp = 3;break;
    default:  tmp = 4;break;
  }
  if(tmp < 4){
    charger->chgvol = var;
    axp_read(charger->master, AXP20_CHARGE_CONTROL1, &val);
    val &= 0x9F;
    val |= tmp << 5;
    axp_write(charger->master, AXP20_CHARGE_CONTROL1, val);
  }
  return count;
}

static ssize_t chgintmicrocur_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val;
  axp_read(charger->master, AXP20_CHARGE_CONTROL1, &val);
  charger->chgcur = (val & 0x0F) * 100000 +300000;
  return sprintf(buf, "%d\n",charger->chgcur);
}

static ssize_t chgintmicrocur_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  uint8_t val,tmp;
  var = simple_strtoul(buf, NULL, 10);
  if(var >= 300000 && var <= 1800000){
    tmp = (var -200001)/100000;
    charger->chgcur = tmp *100000 + 300000;
    axp_read(charger->master, AXP20_CHARGE_CONTROL1, &val);
    val &= 0xF0;
    val |= tmp;
    axp_write(charger->master, AXP20_CHARGE_CONTROL1, val);
  }
  return count;
}

static ssize_t chgendcur_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val;
  axp_read(charger->master, AXP20_CHARGE_CONTROL1, &val);
  charger->chgend = ((val >> 4)& 0x01)? 15 : 10;
  return sprintf(buf, "%d\n",charger->chgend);
}

static ssize_t chgendcur_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  var = simple_strtoul(buf, NULL, 10);
  if(var == 10 ){
    charger->chgend = var;
    axp_clr_bits(charger->master ,AXP20_CHARGE_CONTROL1,0x10);
  }
  else if (var == 15){
    charger->chgend = var;
    axp_set_bits(charger->master ,AXP20_CHARGE_CONTROL1,0x10);

  }
  return count;
}

static ssize_t chgpretimemin_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val;
  axp_read(charger->master,AXP20_CHARGE_CONTROL2, &val);
  charger->chgpretime = (val >> 6) * 10 +40;
  return sprintf(buf, "%d\n",charger->chgpretime);
}

static ssize_t chgpretimemin_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  uint8_t tmp,val;
  var = simple_strtoul(buf, NULL, 10);
  if(var >= 40 && var <= 70){
    tmp = (var - 40)/10;
    charger->chgpretime = tmp * 10 + 40;
    axp_read(charger->master,AXP20_CHARGE_CONTROL2,&val);
    val &= 0x3F;
    val |= (tmp << 6);
    axp_write(charger->master,AXP20_CHARGE_CONTROL2,val);
  }
  return count;
}

static ssize_t chgcsttimemin_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val;
  axp_read(charger->master,AXP20_CHARGE_CONTROL2, &val);
  charger->chgcsttime = (val & 0x03) *120 + 360;
  return sprintf(buf, "%d\n",charger->chgcsttime);
}

static ssize_t chgcsttimemin_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  uint8_t tmp,val;
  var = simple_strtoul(buf, NULL, 10);
  if(var >= 360 && var <= 720){
    tmp = (var - 360)/120;
    charger->chgcsttime = tmp * 120 + 360;
    axp_read(charger->master,AXP20_CHARGE_CONTROL2,&val);
    val &= 0xFC;
    val |= tmp;
    axp_write(charger->master,AXP20_CHARGE_CONTROL2,val);
  }
  return count;
}

static ssize_t adcfreq_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val;
  axp_read(charger->master, AXP20_ADC_CONTROL3, &val);
  switch ((val >> 6) & 0x03){
     case 0: charger->sample_time = 25;break;
     case 1: charger->sample_time = 50;break;
     case 2: charger->sample_time = 100;break;
     case 3: charger->sample_time = 200;break;
     default:break;
  }
  return sprintf(buf, "%d\n",charger->sample_time);
}

static ssize_t adcfreq_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  uint8_t val;
  var = simple_strtoul(buf, NULL, 10);
  axp_read(charger->master, AXP20_ADC_CONTROL3, &val);
  switch (var/25){
    case 1: val &= ~(3 << 6);charger->sample_time = 25;break;
    case 2: val &= ~(3 << 6);val |= 1 << 6;charger->sample_time = 50;break;
    case 4: val &= ~(3 << 6);val |= 2 << 6;charger->sample_time = 100;break;
    case 8: val |= 3 << 6;charger->sample_time = 200;break;
    default: break;
    }
  axp_write(charger->master, AXP20_ADC_CONTROL3, val);
  return count;
}


static ssize_t vholden_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val;
  axp_read(charger->master,AXP20_CHARGE_VBUS, &val);
  val = (val>>6) & 0x01;
  return sprintf(buf, "%d\n",val);
}

static ssize_t vholden_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  var = simple_strtoul(buf, NULL, 10);
  if(var)
    axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x40);
  else
    axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x40);

  return count;
}

static ssize_t vhold_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val;
  int vhold;
  axp_read(charger->master,AXP20_CHARGE_VBUS, &val);
  vhold = ((val >> 3) & 0x07) * 100000 + 4000000;
  return sprintf(buf, "%d\n",vhold);
}

static ssize_t vhold_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  uint8_t val,tmp;
  var = simple_strtoul(buf, NULL, 10);
  if(var >= 4000000 && var <=4700000){
    tmp = (var - 4000000)/100000;
    //printk("tmp = 0x%x\n",tmp);
    axp_read(charger->master, AXP20_CHARGE_VBUS,&val);
    val &= 0xC7;
    val |= tmp << 3;
    //printk("val = 0x%x\n",val);
    axp_write(charger->master, AXP20_CHARGE_VBUS,val);
  }
  return count;
}

static ssize_t iholden_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val;
  axp_read(charger->master,AXP20_CHARGE_VBUS, &val);
  return sprintf(buf, "%d\n",((val & 0x03) == 0x03)?0:1);
}

static ssize_t iholden_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  var = simple_strtoul(buf, NULL, 10);
  if(var)
    axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x01);
  else
    axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x03);

  return count;
}

static ssize_t ihold_show(struct device *dev,
    struct device_attribute *attr, char *buf)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  uint8_t val,tmp;
  int ihold;
  axp_read(charger->master,AXP20_CHARGE_VBUS, &val);
  tmp = (val) & 0x03;
  switch(tmp){
    case 0: ihold = 900000;break;
    case 1: ihold = 500000;break;
    case 2: ihold = 100000;break;
    default: ihold = 0;break;
  }
  return sprintf(buf, "%d\n",ihold);
}

static ssize_t ihold_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
  struct axp_charger *charger = dev_get_drvdata(dev);
  int var;
  var = simple_strtoul(buf, NULL, 10);
  if(var == 900000)
    axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x03);
  else if (var == 500000){
    axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x02);
    axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x01);
  }
  else if (var == 100000){
    axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x01);
    axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x02);
  }

  return count;
}

static struct device_attribute axp_charger_attrs[] = {
  AXP_CHG_ATTR(chgen),
  AXP_CHG_ATTR(chgmicrovol),
  AXP_CHG_ATTR(chgintmicrocur),
  AXP_CHG_ATTR(chgendcur),
  AXP_CHG_ATTR(chgpretimemin),
  AXP_CHG_ATTR(chgcsttimemin),
  AXP_CHG_ATTR(adcfreq),
  AXP_CHG_ATTR(vholden),
  AXP_CHG_ATTR(vhold),
  AXP_CHG_ATTR(iholden),
  AXP_CHG_ATTR(ihold),
};

#if defined CONFIG_HAS_EARLYSUSPEND
static void axp_earlysuspend(struct early_suspend *h)
{
	uint8_t tmp;
	DBG_PSY_MSG("======early suspend=======\n");

#if defined (CONFIG_AXP_CHGCHANGE)
  	early_suspend_flag = 1;
  	if(pmu_earlysuspend_chgcur == 0)
  		axp_clr_bits(axp_charger->master,AXP20_CHARGE_CONTROL1,0x80);
  	else
  		axp_set_bits(axp_charger->master,AXP20_CHARGE_CONTROL1,0x80);

    if(pmu_earlysuspend_chgcur >= 300000 && pmu_earlysuspend_chgcur <= 1800000){
    	tmp = (pmu_earlysuspend_chgcur -200001)/100000;
    	axp_update(axp_charger->master, AXP20_CHARGE_CONTROL1, tmp,0x0F);
    }
#endif

}
static void axp_lateresume(struct early_suspend *h)
{
	uint8_t tmp;
	DBG_PSY_MSG("======late resume=======\n");

#if defined (CONFIG_AXP_CHGCHANGE)
	early_suspend_flag = 0;
	if(pmu_resume_chgcur == 0)
  		axp_clr_bits(axp_charger->master,AXP20_CHARGE_CONTROL1,0x80);
  else
  		axp_set_bits(axp_charger->master,AXP20_CHARGE_CONTROL1,0x80);

    if(pmu_resume_chgcur >= 300000 && pmu_resume_chgcur <= 1800000){
        tmp = (pmu_resume_chgcur -200001)/100000;
        axp_update(axp_charger->master, AXP20_CHARGE_CONTROL1, tmp,0x0F);
    }
#endif

}
#endif

int axp_charger_create_attrs(struct power_supply *psy)
{
  int j,ret;
  for (j = 0; j < ARRAY_SIZE(axp_charger_attrs); j++) {
    ret = device_create_file(psy->dev,
          &axp_charger_attrs[j]);
    if (ret)
      goto sysfs_failed;
  }
    goto succeed;

sysfs_failed:
  while (j--)
    device_remove_file(psy->dev,
         &axp_charger_attrs[j]);
succeed:
  return ret;
}

int Get_Bat_Coulomb_Count(struct axp_charger *charger)
{
	uint8_t  temp[8];
	int64_t  rValue1,rValue2,rValue;
	int Cur_CoulombCounter_tmp,m;

	axp_reads(charger->master, AXP20_CCHAR3_RES,8,temp);
	rValue1 = ((temp[0] << 24) + (temp[1] << 16) + (temp[2] << 8) + temp[3]);
	rValue2 = ((temp[4] << 24) + (temp[5] << 16) + (temp[6] << 8) + temp[7]);
	DBG_PSY_MSG("%s->%d -     CHARGINGOULB:[0]=0x%x,[1]=0x%x,[2]=0x%x,[3]=0x%x\n",__FUNCTION__,__LINE__,temp[0],temp[1],temp[2],temp[3]);
	DBG_PSY_MSG("%s->%d - DISCHARGINGCLOUB:[4]=0x%x,[5]=0x%x,[6]=0x%x,[7]=0x%x\n",__FUNCTION__,__LINE__,temp[4],temp[5],temp[6],temp[7]);
	rValue = (ABS(rValue1 - rValue2)) * 4369;
	m = ADC_Freq_Get(charger) * 480;
	do_div(rValue,m);
	if(rValue1 >= rValue2)
		Cur_CoulombCounter_tmp = (int)rValue;
	else
		Cur_CoulombCounter_tmp = (int)(0 - rValue);
	DBG_PSY_MSG("Cur_CoulombCounter_tmp = %d\n",Cur_CoulombCounter_tmp);
	return Cur_CoulombCounter_tmp;				//unit mAh
}

static void axp_charging_monitor(struct work_struct *work)
{
    struct axp_charger *charger;
    uint8_t val;
    uint8_t v[5];
    int pre_rest_vol;
    int rdc,k;
    int rt_rest_vol;
    int rest_vol;
    uint16_t tmp;
    int Cur_CoulombCounter;
    int cap_index_p,var;
	int gpio_adp_val,ret;

    charger = container_of(work, struct axp_charger, work.work);

    Cur_CoulombCounter = ABS(Get_Bat_Coulomb_Count(charger));
  	DBG_PSY_MSG("Cur_CoulombCounter = %d\n",Cur_CoulombCounter);

		axp_reads(charger->master,0xbc,2,v);
 		charger->ocv = ((v[0] << 4) + (v[1] & 0x0f)) * 11 /10 ;
    axp_reads(charger->master,AXP20_IC_TYPE,2,v);
    //DBG_PSY_MSG("v[0] = 0x%x,v[1] = 0x%x\n",v[0],v[1]);
    pre_rest_vol = charger->rest_vol;
    axp_charger_update_state(charger);
    axp_charger_update(charger);

    if((v[0] == 0x11 || v[0] == 0x21) && charger->is_on && axp20_icharge_to_mA(charger->adc->ichar_res) > 200 && charger->vbat > 3600 && charger->disvbat != 0){
        if((((v[1] >> 7) == 0) || (((v[1] >> 3) & 0x1) == 0)) && count_rdc >= 3){
            axp_set_bits(charger->master,AXP20_CAP,0x80);
            axp_clr_bits(charger->master,0xBA,0x80);
            rdc = (axp_get_rdc(charger) * 10000 + 5371) / 10742;
            tmp = (uint16_t) rdc;
            axp_write(charger->master,0xBB,tmp & 0x00FF);
            axp_update(charger->master, 0xBA, (tmp >> 8), 0x1F);
            axp_clr_bits(charger->master,AXP20_CAP,0x80);
            axp_set_bits(charger->master,0x04,0x80);
            DBG_PSY_MSG("==============================rdc = %d\n",rdc * 10742 / 10000);
            count_rdc = 0;
        }
        else if((((v[1] >> 7) == 0) || (((v[1] >> 3) & 0x1) == 0)) && count_rdc < 3){
        	DBG_PSY_MSG("==============================%d\n",__LINE__);
            count_rdc ++;  
        }
        else{
            DBG_PSY_MSG("==============================%d\n",__LINE__);
            count_rdc = 0; 
        }
    }
    else{
        count_rdc = 0;
     	DBG_PSY_MSG("==============================%d\n",__LINE__);   
    }
    
	if(flag_state_change){
    	rt_rest_vol = charger->rest_vol;
    	rest_vol = charger->rest_vol;
    	flag_state_change = 0;
    }
    else{
    	axp_read(charger->master, AXP20_CAP,&val);
    	rt_rest_vol = (int) (val & 0x7F);
  		if((charger->bat_det == 0) || (rt_rest_vol == 127) ){
  			rt_rest_vol = 100;
  		}

    	Total_Cap -= Bat_Cap_Buffer[Cap_Index];
    	if(Cap_Index == 0){
    		cap_index_p = AXP20_VOL_MAX - 1;
    	}
    	else{
    		cap_index_p = Cap_Index - 1;
    	}
    	if(ABS(rt_rest_vol - Bat_Cap_Buffer[cap_index_p]) > 5){
    		DBG_PSY_MSG("-----------correct rdc-----------\n");
    		axp_clr_bits(charger->master,0x04,0x08);
    	}

    	Bat_Cap_Buffer[Cap_Index] = rt_rest_vol;
    	Total_Cap += Bat_Cap_Buffer[Cap_Index];
    	Cap_Index++;
    	if(Cap_Index == AXP20_VOL_MAX){
        	Cap_Index = 0;
    	}

    	rest_vol = (Total_Cap + AXP20_VOL_MAX / 2 ) / AXP20_VOL_MAX;

#if DBG_AXP_PSY
    	for(k = 0;k < AXP20_VOL_MAX ; k++){
        	DBG_PSY_MSG("Bat_Cap_Buffer[%d] = %d\n",k,Bat_Cap_Buffer[k]);
    	}
#endif

    	DBG_PSY_MSG("Before Modify:Cap_Index = %d,val = 0x%x,pre_rest_vol = %d,rest_vol = %d\n",Cap_Index,val,pre_rest_vol,rest_vol);

    	if(charger->is_on && (rest_vol < pre_rest_vol)){
        	rest_vol = pre_rest_vol;
    	}
   		else if(!charger->ext_valid && (rest_vol > pre_rest_vol)){
        	rest_vol = pre_rest_vol;
    	}

    	DBG_PSY_MSG("After Modify:val = 0x%x,pre_rest_vol = %d,rest_vol = %d\n",val,pre_rest_vol,rest_vol);

    	/* full */
    	if(charger->ocv >= 4100 && !charger->is_on && charger->ext_valid && charger->charge_on){
        rest_vol = 100;
        for(k = 0;k < AXP20_VOL_MAX; k++){
            Bat_Cap_Buffer[k] = rest_vol;
        }
        Total_Cap = rest_vol * AXP20_VOL_MAX;
        charger->bat_current_direction = 1;
        if(charger->rest_vol < 100)
        	axp_set_bits(charger->master,0x32,0x38);
    	}

    	/* charging*/
    	if(charger->is_on && rest_vol == 100){
        rest_vol = 99;
    	}


			if(rest_vol > pre_rest_vol){
				if(counter >= 3){
					charger->rest_vol ++;
					counter = 0;
				} else
					counter ++;
			} else if(rest_vol < pre_rest_vol){
				if(counter >= 3){
					charger->rest_vol --;
					counter = 0;
				} else
					counter ++;
			} else
				counter = 0;
		}

		if(pmu_usbcur_limit){
			axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x01);
			var = pmu_usbcur * 1000;
			if(var == 900000)
				axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x03);
			else if (var == 500000){
				axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x02);
				axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x01);
 			}
			else if (var == 100000){
				axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x01);
				axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x02);
			}
		}
		else
			axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x03);

		if((count_dis >= 8) && (charger->disvbat != 0)){
			charger->disvbat = 0;
			charger->disibat = 0;
			count_dis = 0;
		}
		if(charger->bat_current_direction == 1){
			count_dis++;
		} else
			count_dis = 0;
			
		
#if  DBG_AXP_PSY
 		DBG_PSY_MSG("charger->ic_temp = %d\n",charger->ic_temp);
 		DBG_PSY_MSG("charger->vbat = %d\n",charger->vbat);
 		DBG_PSY_MSG("charger->ibat = %d\n",charger->ibat);
 		DBG_PSY_MSG("charger->vusb = %d\n",charger->vusb);
 		DBG_PSY_MSG("charger->iusb = %d\n",charger->iusb);
 		DBG_PSY_MSG("charger->vac = %d\n",charger->vac);
 		DBG_PSY_MSG("charger->iac = %d\n",charger->iac);
 		DBG_PSY_MSG("charger->ocv = %d\n",charger->ocv);
 		DBG_PSY_MSG("charger->disvbat = %d\n",charger->disvbat);
 		DBG_PSY_MSG("charger->disibat = %d\n",charger->disibat);
 		DBG_PSY_MSG("rt_rest_vol = %d\n",rt_rest_vol);
 		DBG_PSY_MSG("rest_vol = %d\n",rest_vol);
 		DBG_PSY_MSG("charger->rest_vol = %d\n",charger->rest_vol);
 		axp_reads(charger->master,0xba,2,v);
 		rdc = (((v[0] & 0x1F) << 8) | v[1]) * 10742 / 10000;
 		DBG_PSY_MSG("rdc = %d\n",rdc);
 		DBG_PSY_MSG("charger->is_on = %d\n",charger->is_on);
 		DBG_PSY_MSG("charger->charge_on = %d\n",charger->charge_on);
 		DBG_PSY_MSG("charger->ext_valid = %d\n",charger->ext_valid);
#endif

#if defined (CONFIG_AXP_CHGCHANGE)
		if(pmu_used2){
			gpio_adp_val = gpio_read_one_pin_value(gpio_adp_hdle,"pmu_adpdet");
			DBG_PSY_MSG("GPIO->H2 = %d\n",gpio_adp_val);
  		if(!gpio_adp_val){
  			ret = script_parser_fetch("pmu_para", "pmu_init_chgcur2", &pmu_init_chgcur, sizeof(int));
      	if (ret){
      		printk("axp driver uning configuration failed(%d)\n", __LINE__);
        	pmu_init_chgcur = INTCHGCUR / 1000;
      	}
      	pmu_init_chgcur = pmu_init_chgcur * 1000;
      	ret = script_parser_fetch("pmu_para", "pmu_earlysuspend_chgcur2", &pmu_earlysuspend_chgcur, sizeof(int));
      	if (ret){
      		printk("axp driver uning configuration failed(%d)\n", __LINE__);
        	pmu_earlysuspend_chgcur = SUSCHGCUR / 1000;
      	}
      	pmu_earlysuspend_chgcur = pmu_earlysuspend_chgcur * 1000;
      	ret = script_parser_fetch("pmu_para", "pmu_suspend_chgcur2", &pmu_suspend_chgcur, sizeof(int));
      	if (ret){
      		printk("axp driver uning configuration failed(%d)\n", __LINE__);
        	pmu_suspend_chgcur = SUSCHGCUR / 1000;
      	}
      	pmu_suspend_chgcur = pmu_suspend_chgcur * 1000;
      	ret = script_parser_fetch("pmu_para", "pmu_resume_chgcur2", &pmu_resume_chgcur, sizeof(int));
      	if (ret){
        	printk("axp driver uning configuration failed(%d)\n", __LINE__);
        	pmu_resume_chgcur = RESCHGCUR / 1000;
      	}
     		pmu_resume_chgcur = pmu_resume_chgcur * 1000;
      	ret = script_parser_fetch("pmu_para", "pmu_shutdown_chgcur2", &pmu_shutdown_chgcur, sizeof(int));
      	if (ret){
        	printk("axp driver uning configuration failed(%d)\n", __LINE__);
        	pmu_shutdown_chgcur = CLSCHGCUR / 1000;
      	}
      	pmu_shutdown_chgcur = pmu_shutdown_chgcur * 1000;
  		}
  		else{
  			ret = script_parser_fetch("pmu_para", "pmu_init_chgcur", &pmu_init_chgcur, sizeof(int));
      	if (ret){
      		printk("axp driver uning configuration failed(%d)\n", __LINE__);
        	pmu_init_chgcur = INTCHGCUR / 1000;
      	}
      	pmu_init_chgcur = pmu_init_chgcur * 1000;
      	ret = script_parser_fetch("pmu_para", "pmu_earlysuspend_chgcur", &pmu_earlysuspend_chgcur, sizeof(int));
      	if (ret){
      		printk("axp driver uning configuration failed(%d)\n", __LINE__);
        	pmu_earlysuspend_chgcur = SUSCHGCUR / 1000;
      	}
      	pmu_earlysuspend_chgcur = pmu_earlysuspend_chgcur * 1000;
      	ret = script_parser_fetch("pmu_para", "pmu_suspend_chgcur", &pmu_suspend_chgcur, sizeof(int));
      	if (ret){
      		printk("axp driver uning configuration failed(%d)\n", __LINE__);
        	pmu_suspend_chgcur = SUSCHGCUR / 1000;
      	}
      	pmu_suspend_chgcur = pmu_suspend_chgcur * 1000;
      	ret = script_parser_fetch("pmu_para", "pmu_resume_chgcur", &pmu_resume_chgcur, sizeof(int));
      	if (ret){
        	printk("axp driver uning configuration failed(%d)\n", __LINE__);
        	pmu_resume_chgcur = RESCHGCUR / 1000;
      	}
      	pmu_resume_chgcur = pmu_resume_chgcur * 1000;
      	ret = script_parser_fetch("pmu_para", "pmu_shutdown_chgcur", &pmu_shutdown_chgcur, sizeof(int));
      	if (ret){
        	printk("axp driver uning configuration failed(%d)\n", __LINE__);
        	pmu_shutdown_chgcur = CLSCHGCUR / 1000;
      	}
      	pmu_shutdown_chgcur = pmu_shutdown_chgcur * 1000;
  		}


#if defined CONFIG_HAS_EARLYSUSPEND
  		 	if(early_suspend_flag){
  		 			if(pmu_earlysuspend_chgcur == 0){
  		 				axp_clr_bits(axp_charger->master,AXP20_CHARGE_CONTROL1,0x80);
  		 			}
  		 			else if(pmu_earlysuspend_chgcur >= 300000 && pmu_init_chgcur <= 1800000){
  		 			  axp_set_bits(axp_charger->master,AXP20_CHARGE_CONTROL1,0x80);
							tmp = (pmu_earlysuspend_chgcur -200001)/100000;
							charger->chgcur = tmp *100000 + 300000;
							axp_update(charger->master, AXP20_CHARGE_CONTROL1, tmp, 0x0F);
						}
  		 	}else
#endif
			{
				if(pmu_init_chgcur == 0){
					axp_clr_bits(axp_charger->master,AXP20_CHARGE_CONTROL1,0x80);
				}
				else if (pmu_init_chgcur >= 300000 && pmu_init_chgcur <= 1800000){
					axp_set_bits(axp_charger->master,AXP20_CHARGE_CONTROL1,0x80);
    			tmp = (pmu_init_chgcur -200001)/100000;
    			charger->chgcur = tmp *100000 + 300000;
					axp_update(charger->master, AXP20_CHARGE_CONTROL1, tmp, 0x0F);
    		}
  		}

  	}
#endif

		DBG_PSY_MSG("pmu_init_chgcur           = %d\n",pmu_init_chgcur);
		DBG_PSY_MSG("pmu_earlysuspend_chgcur   = %d\n",pmu_earlysuspend_chgcur);
		DBG_PSY_MSG("pmu_suspend_chgcur        = %d\n",pmu_suspend_chgcur);
		DBG_PSY_MSG("pmu_resume_chgcur         = %d\n",pmu_resume_chgcur);
		DBG_PSY_MSG("pmu_shutdown_chgcur               = %d\n",pmu_shutdown_chgcur);

    /* if battery volume changed, inform uevent */
    if(charger->rest_vol - pre_rest_vol){
        DBG_PSY_MSG("battery vol change: %d->%d \n", pre_rest_vol, charger->rest_vol);
        pre_rest_vol = charger->rest_vol;
        axp_write(charger->master,AXP20_DATA_BUFFER1,charger->rest_vol | 0x80);
        if(charger->rest_vol == 100){
        	axp_clr_bits(charger->master,0x32,0x38);
        }
        power_supply_changed(&charger->batt);
    }

    /* reschedule for the next time */
    schedule_delayed_work(&charger->work, charger->interval);
}

static int axp_battery_probe(struct platform_device *pdev)
{
  struct axp_charger *charger;
  struct axp_supply_init_data *pdata = pdev->dev.platform_data;
  int ret,k,var;
  uint8_t val1,val2,tmp,val;
  uint8_t ocv_cap[31],v[2];
  int Cur_CoulombCounter,rdc;

  powerkeydev = input_allocate_device();
  if (!powerkeydev) {
    kfree(powerkeydev);
    return -ENODEV;
  }

  powerkeydev->name = pdev->name;
  powerkeydev->phys = "m1kbd/input2";
  powerkeydev->id.bustype = BUS_HOST;
  powerkeydev->id.vendor = 0x0001;
  powerkeydev->id.product = 0x0001;
  powerkeydev->id.version = 0x0100;
  powerkeydev->open = NULL;
  powerkeydev->close = NULL;
  powerkeydev->dev.parent = &pdev->dev;

  set_bit(EV_KEY, powerkeydev->evbit);
  set_bit(EV_REL, powerkeydev->evbit);
  //set_bit(EV_REP, powerkeydev->evbit);
  set_bit(KEY_POWER, powerkeydev->keybit);

  ret = input_register_device(powerkeydev);
  if(ret) {
    DBG_PSY_MSG("Unable to Register the power key\n");
    }

  if (pdata == NULL)
    return -EINVAL;

  if (pdata->chgcur > 1800000 ||
      pdata->chgvol < 4100000 ||
      pdata->chgvol > 4360000){
        DBG_PSY_MSG("charger milliamp is too high or target voltage is over range\n");
        return -EINVAL;
    }

  if (pdata->chgpretime < 40 || pdata->chgpretime >70 ||
    pdata->chgcsttime < 360 || pdata->chgcsttime > 720){
            DBG_PSY_MSG("prechaging time or constant current charging time is over range\n");
        return -EINVAL;
  }

  charger = kzalloc(sizeof(*charger), GFP_KERNEL);
  if (charger == NULL)
    return -ENOMEM;

  charger->master = pdev->dev.parent;

  charger->chgcur      = pdata->chgcur;
  charger->chgvol     = pdata->chgvol;
  charger->chgend           = pdata->chgend;
  charger->sample_time          = pdata->sample_time;
  charger->chgen                   = pdata->chgen;
  charger->chgpretime      = pdata->chgpretime;
  charger->chgcsttime = pdata->chgcsttime;
  charger->battery_info         = pdata->battery_info;
  charger->disvbat			= 0;
  charger->disibat			= 0;

  ret = axp_battery_first_init(charger);
  if (ret)
    goto err_charger_init;

  charger->nb.notifier_call = axp_battery_event;
  ret = axp_register_notifier(charger->master, &charger->nb, AXP20_NOTIFIER_ON);
  if (ret)
    goto err_notifier;

  axp_battery_setup_psy(charger);
  ret = power_supply_register(&pdev->dev, &charger->batt);
  if (ret)
    goto err_ps_register;

	axp_read(charger->master,AXP20_CHARGE_STATUS,&val);
	if(!((val >> 1) & 0x01)){
  	ret = power_supply_register(&pdev->dev, &charger->ac);
  	if (ret){
    	power_supply_unregister(&charger->batt);
    	goto err_ps_register;
  	}
  }
  ret = power_supply_register(&pdev->dev, &charger->usb);
  if (ret){
    power_supply_unregister(&charger->ac);
    power_supply_unregister(&charger->batt);
    goto err_ps_register;
  }

  ret = axp_charger_create_attrs(&charger->batt);
  if(ret){
    return ret;
  }


  platform_set_drvdata(pdev, charger);

  /* initial restvol*/

  /* usb current and voltage limit */
  if(pmu_usbvol_limit){
    axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x40);
  	var = pmu_usbvol * 1000;
  	if(var >= 4000000 && var <=4700000){
    	tmp = (var - 4000000)/100000;
    	axp_read(charger->master, AXP20_CHARGE_VBUS,&val);
    	val &= 0xC7;
    	val |= tmp << 3;
    	axp_write(charger->master, AXP20_CHARGE_VBUS,val);
  	}
  }
  else
    axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x40);

  if(pmu_usbcur_limit){
    axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x01);
    var = pmu_usbcur * 1000;
  	if(var == 900000)
    	axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x03);
  	else if (var == 500000){
    	axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x02);
    	axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x01);
  	}
  	else if (var == 100000){
    	axp_clr_bits(charger->master, AXP20_CHARGE_VBUS, 0x01);
    	axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x02);
  	}
  }
  else
    axp_set_bits(charger->master, AXP20_CHARGE_VBUS, 0x03);


  /* set lowe power warning/shutdown voltage*/
  var = script_parser_fetch("pmu_para", "pmu_suspendpwroff_vol", &pmu_suspendpwroff_vol, sizeof(int));
  if (var)
  {
     printk("[AXP]axp driver uning configuration failed(%d)\n", __LINE__);
     pmu_suspendpwroff_vol = 3500;
     printk("[AXP]pmu_suspendpwroff_vol = %d\n",pmu_suspendpwroff_vol);
  }
  pmu_suspendpwroff_vol = pmu_suspendpwroff_vol * 1000;
	
	if(pmu_suspendpwroff_vol >= 2867200 && pmu_suspendpwroff_vol <= 4200000){
		val = (pmu_suspendpwroff_vol - 2867200) / 5600;	
	}

  /* 3.5552V--%5 close*/
  axp_write(charger->master, AXP20_APS_WARNING1,val);
  axp_write(charger->master, AXP20_APS_WARNING2,(val - 0x0a));
  ocv_cap[0]  = pmu_bat_para1;
  ocv_cap[1]  = 0xC1;
  ocv_cap[2]  = pmu_bat_para2;
  ocv_cap[3]  = 0xC2;
  ocv_cap[4]  = pmu_bat_para3;
  ocv_cap[5]  = 0xC3;
  ocv_cap[6]  = pmu_bat_para4;
  ocv_cap[7]  = 0xC4;
  ocv_cap[8]  = pmu_bat_para5;
  ocv_cap[9]  = 0xC5;
  ocv_cap[10] = pmu_bat_para6;
  ocv_cap[11] = 0xC6;
  ocv_cap[12] = pmu_bat_para7;
  ocv_cap[13] = 0xC7;
  ocv_cap[14] = pmu_bat_para8;
  ocv_cap[15] = 0xC8;
  ocv_cap[16] = pmu_bat_para9;
  ocv_cap[17] = 0xC9;
  ocv_cap[18] = pmu_bat_para10;
  ocv_cap[19] = 0xCA;
  ocv_cap[20] = pmu_bat_para11;
  ocv_cap[21] = 0xCB;
  ocv_cap[22] = pmu_bat_para12;
  ocv_cap[23] = 0xCC;
  ocv_cap[24] = pmu_bat_para13;
  ocv_cap[25] = 0xCD;
  ocv_cap[26] = pmu_bat_para14;
  ocv_cap[27] = 0xCE;
  ocv_cap[28] = pmu_bat_para15;
  ocv_cap[29] = 0xCF;
  ocv_cap[30] = pmu_bat_para16;
  axp_writes(charger->master, 0xC0,31,ocv_cap);

  /* open/close set */
  DBG_PSY_MSG("pmu_pekoff_time = %d\n",pmu_pekoff_time);
  DBG_PSY_MSG("pmu_pekoff_en = %d\n",pmu_pekoff_en);
  DBG_PSY_MSG("pmu_peklong_time = %d\n",pmu_peklong_time);
  DBG_PSY_MSG("pmu_pekon_time = %d\n",pmu_pekon_time);
  DBG_PSY_MSG("pmu_pwrok_time = %d\n",pmu_pwrok_time);
  DBG_PSY_MSG("pmu_pwrnoe_time = %d\n",pmu_pwrnoe_time);
  DBG_PSY_MSG("pmu_intotp_en = %d\n",pmu_intotp_en);

  /* n_oe delay time set */
	if (pmu_pwrnoe_time < 1000)
		pmu_pwrnoe_time = 128;
	if (pmu_pwrnoe_time > 3000)
		pmu_pwrnoe_time = 3000;
	axp_read(charger->master,POWER20_OFF_CTL,&val);
	val &= 0xfc;
	val |= ((pmu_pwrnoe_time) / 1000);
	axp_write(charger->master,POWER20_OFF_CTL,val);
	DBG_PSY_MSG("%d-->0x%x\n",__LINE__,val);

	/* pek open time set */
	axp_read(charger->master,POWER20_PEK_SET,&val);
	if (pmu_pekon_time < 1000)
		val &= 0x3f;
	else if(pmu_pekon_time < 2000){
		val &= 0x3f;
		val |= 0x80;
	}
	else if(pmu_pekon_time < 3000){
		val &= 0x3f;
		val |= 0xc0;
	}
	else {
		val &= 0x3f;
		val |= 0x40;
	}
	axp_write(charger->master,POWER20_PEK_SET,val);
	DBG_PSY_MSG("%d-->0x%x\n",__LINE__,val);

	/* pek long time set*/
	if(pmu_peklong_time < 1000)
		pmu_peklong_time = 1000;
	if(pmu_peklong_time > 2500)
		pmu_peklong_time = 2500;
	axp_read(charger->master,POWER20_PEK_SET,&val);
	val &= 0xcf;
	val |= (((pmu_peklong_time - 1000) / 500) << 4);
	axp_write(charger->master,POWER20_PEK_SET,val);
	DBG_PSY_MSG("%d-->0x%x\n",__LINE__,val);

	/* pek en set*/
	if(pmu_pekoff_en)
		pmu_pekoff_en = 1;
	axp_read(charger->master,POWER20_PEK_SET,&val);
	val &= 0xf7;
	val |= (pmu_pekoff_en << 3);
	axp_write(charger->master,POWER20_PEK_SET,val);
	DBG_PSY_MSG("%d-->0x%x\n",__LINE__,val);

	/* pek delay set */
	if(pmu_pwrok_time <= 8)
		pmu_pwrok_time = 0;
	else
		pmu_pwrok_time = 1;
	axp_read(charger->master,POWER20_PEK_SET,&val);
	val &= 0xfb;
	val |= pmu_pwrok_time << 2;
	axp_write(charger->master,POWER20_PEK_SET,val);
	DBG_PSY_MSG("%d-->0x%x\n",__LINE__,val);

	/* pek off time set */
	if(pmu_pekoff_time < 4000)
		pmu_pekoff_time = 4000;
	if(pmu_pekoff_time > 10000)
		pmu_pekoff_time =10000;
	pmu_pekoff_time = (pmu_pekoff_time - 4000) / 2000 ;
	axp_read(charger->master,POWER20_PEK_SET,&val);
	val &= 0xfc;
	val |= pmu_pekoff_time ;
	axp_write(charger->master,POWER20_PEK_SET,val);
	DBG_PSY_MSG("%d-->0x%x\n",__LINE__,val);

	/* enable overtemperture off */
	if(pmu_intotp_en)
		pmu_intotp_en = 1;
	axp_read(charger->master,POWER20_HOTOVER_CTL,&val);
	val &= 0xfb;
	val |= pmu_intotp_en << 2;
	axp_write(charger->master,POWER20_HOTOVER_CTL,val);
	DBG_PSY_MSG("%d-->0x%x\n",__LINE__,val);
	
	/* disable */
  axp_set_bits(charger->master,AXP20_CAP,0x80);
  axp_clr_bits(charger->master,0xBA,0x80);
  axp_reads(charger->master,0xbA,2,v);
  rdc = (((v[0] & 0x1F) << 8) | v[1]) * 10742 / 10000;
  axp_read(charger->master,AXP20_DATA_BUFFER0,&val1);
  if((((val1 >> 7) & 0x1) == 0)||(rdc > pmu_battery_rdc * 2)){
  	rdc = (pmu_battery_rdc * 10000 + 5371) / 10742;
  	axp_write(charger->master,0xBB,rdc & 0x00FF);
  	axp_update(charger->master, 0xBA, (rdc >> 8), 0x1F);
  }
  axp_clr_bits(charger->master,AXP20_CAP,0x80);

  axp_set_bits(charger->master,0x8F,0x88);
  axp_clr_bits(charger->master,0x81,0x04);

	axp_charger_update_state(charger);

  axp_read(charger->master,AXP20_DATA_BUFFER1,&val1);
  charger->rest_vol = (int) (val1 & 0x7F);

  axp_read(charger->master, AXP20_CAP,&val2);

  Cur_CoulombCounter = ABS(Get_Bat_Coulomb_Count(charger));
  DBG_PSY_MSG("Cur_CoulombCounter = %d\n",Cur_CoulombCounter);
  //if(ABS(charger->rest_vol-(val2 & 0x7F)) >= 3 && (val1 >> 7)){
  if(ABS(charger->rest_vol-(val2 & 0x7F)) >= 10 || Cur_CoulombCounter > 50){
    charger->rest_vol = (int) (val2 & 0x7F);
  }
  if((charger->bat_det == 0) || (charger->rest_vol == 127)){
  	charger->rest_vol = 100;
  }

  DBG_PSY_MSG("last_rest_vol = %d, now_rest_vol = %d\n",(val1 & 0x7F),(val2 & 0x7F));
  memset(Bat_Cap_Buffer, 0, sizeof(Bat_Cap_Buffer));
  for(k = 0;k < AXP20_VOL_MAX; k++){
    Bat_Cap_Buffer[k] = charger->rest_vol;
  }
  Total_Cap = charger->rest_vol * AXP20_VOL_MAX;

  charger->interval = msecs_to_jiffies(10 * 1000);
  INIT_DELAYED_WORK(&charger->work, axp_charging_monitor);
  schedule_delayed_work(&charger->work, charger->interval);

  var = script_parser_fetch("pmu_para", "pmu_used2", &pmu_used2, sizeof(int));
  if (var)
  {
     DBG_PSY_MSG("axp driver uning configuration failed(%d)\n", __LINE__);
     DBG_PSY_MSG("pmu_used2 = %d\n",pmu_used2);
  }

  var = script_parser_fetch("pmu_para", "pmu_earlysuspend_chgcur", &pmu_earlysuspend_chgcur, sizeof(int));
  if (var)
  {
     DBG_PSY_MSG("axp driver uning configuration failed(%d)\n", __LINE__);
     pmu_earlysuspend_chgcur = pmu_suspend_chgcur / 1000;
     DBG_PSY_MSG("pmu_earlysuspend_chgcur = %d\n",pmu_earlysuspend_chgcur);
  }
  pmu_earlysuspend_chgcur = pmu_earlysuspend_chgcur * 1000;

#if defined (CONFIG_AXP_CHGCHANGE)
  if(pmu_used2){
  	gpio_adp_hdle = gpio_request_ex("pmu_para", "pmu_adpdet");
  	if (!gpio_adp_hdle)
    {
        DBG_PSY_MSG("get adapter parameter failed\n");
    }
  }
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	axp_charger = charger;
    axp_early_suspend.suspend = axp_earlysuspend;
    axp_early_suspend.resume = axp_lateresume;
    axp_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 2;
    register_early_suspend(&axp_early_suspend);
#endif

  return ret;

err_ps_register:
  axp_unregister_notifier(charger->master, &charger->nb, AXP20_NOTIFIER_ON);

err_notifier:
  cancel_delayed_work_sync(&charger->work);

err_charger_init:
  kfree(charger);
  input_unregister_device(powerkeydev);
  kfree(powerkeydev);

  return ret;
}

static int axp_battery_remove(struct platform_device *dev)
{
    struct axp_charger *charger = platform_get_drvdata(dev);

    if(main_task){
        kthread_stop(main_task);
        main_task = NULL;
    }

    axp_unregister_notifier(charger->master, &charger->nb, AXP20_NOTIFIER_ON);
    cancel_delayed_work_sync(&charger->work);
    power_supply_unregister(&charger->usb);
    power_supply_unregister(&charger->ac);
    power_supply_unregister(&charger->batt);

    kfree(charger);
    input_unregister_device(powerkeydev);
    kfree(powerkeydev);

    return 0;
}


static int axp20_suspend(struct platform_device *dev, pm_message_t state)
{
    uint8_t irq_w[9];
    uint8_t tmp;

    struct axp_charger *charger = platform_get_drvdata(dev);


	cancel_delayed_work_sync(&charger->work);

    /*clear all irqs events*/
    irq_w[0] = 0xff;
    irq_w[1] = POWER20_INTSTS2;
    irq_w[2] = 0xff;
    irq_w[3] = POWER20_INTSTS3;
    irq_w[4] = 0xff;
    irq_w[5] = POWER20_INTSTS4;
    irq_w[6] = 0xff;
    irq_w[7] = POWER20_INTSTS5;
    irq_w[8] = 0xff;
    axp_writes(charger->master, POWER20_INTSTS1, 9, irq_w);

    /* close all irqs*/
    axp_unregister_notifier(charger->master, &charger->nb, AXP20_NOTIFIER_ON);

#if defined (CONFIG_AXP_CHGCHANGE)
		if(pmu_suspend_chgcur == 0)
  		axp_clr_bits(charger->master,AXP20_CHARGE_CONTROL1,0x80);
  	else
  		axp_set_bits(charger->master,AXP20_CHARGE_CONTROL1,0x80);

  	DBG_PSY_MSG("pmu_suspend_chgcur = %d\n", pmu_suspend_chgcur);

    if(pmu_suspend_chgcur >= 300000 && pmu_suspend_chgcur <= 1800000){
    tmp = (pmu_suspend_chgcur -200001)/100000;
    charger->chgcur = tmp *100000 + 300000;
    axp_update(charger->master, AXP20_CHARGE_CONTROL1, tmp,0x0F);
    }
#endif
		/* timer */
		axp_write(charger->master, 0x8A, 0x80);
		axp_write(charger->master, 0x8A, 0x02);

		/* clear and enable coulomb */
		tmp = 0xff;
		axp_read(charger->master, POWER20_COULOMB_CTL, &tmp);

		//tmp |= 0x20;
		tmp &= 0x3f;

		axp_write(charger->master, POWER20_COULOMB_CTL, tmp);
		tmp |= 0x80;
		tmp &= 0xbf;

		axp_write(charger->master, POWER20_COULOMB_CTL, tmp);

		
    return 0;
}

static int axp20_resume(struct platform_device *dev)
{
    struct axp_charger *charger = platform_get_drvdata(dev);

    int pre_rest_vol,k;
    uint8_t val,val1,tmp;
    uint8_t v[2];
    int rt_rest_vol;
    int Cur_CoulombCounter;

    axp_register_notifier(charger->master, &charger->nb, AXP20_NOTIFIER_ON);

    axp_charger_update_state(charger);

    axp_reads(charger->master,0xbc,2,v);
 		charger->ocv = ((v[0] << 4) + (v[1] & 0x0f)) * 11 /10 ;

    axp_read(charger->master, 0x8A, &val1);
		pre_rest_vol = charger->rest_vol;

		axp_read(charger->master, AXP20_CAP,&val);

		Cur_CoulombCounter = ABS(Get_Bat_Coulomb_Count(charger));

    if((val1 >> 7)){
    	charger->rest_vol = (int) (val & 0x7F);
  		if((charger->bat_det == 0) || (charger->rest_vol == 127)){
  			charger->rest_vol = 100;
  		}

  		if ((Cur_CoulombCounter <= 5) && (charger->rest_vol > pre_rest_vol)){
  			charger->rest_vol = pre_rest_vol;
  		}

  		if( ((pre_rest_vol - charger->rest_vol) < 10) && (pre_rest_vol > charger->rest_vol )){
  			charger->rest_vol = --pre_rest_vol;
  		}

    if(charger->is_on && (charger->rest_vol < pre_rest_vol)){
        charger->rest_vol = pre_rest_vol;
    }

    DBG_PSY_MSG("val = 0x%x,pre_rest_vol = %d,rest_vol = %d\n",val,pre_rest_vol,charger->rest_vol);

    	/* full */
    	if((charger->ocv) >= 4100 && !charger->is_on && charger->ext_valid && charger->charge_on){
        charger->rest_vol = 100;
    }

    /* charging*/
    if(charger->is_on && charger->rest_vol == 100){
        charger->rest_vol = 99;
    }

    memset(Bat_Cap_Buffer, 0, sizeof(Bat_Cap_Buffer));
    for(k = 0;k < AXP20_VOL_MAX; k++){
        Bat_Cap_Buffer[k] = charger->rest_vol;
    	}
    	Total_Cap = charger->rest_vol * AXP20_VOL_MAX;
		}
		else if (charger->ext_valid && charger->charge_on){
		  rt_rest_vol = (int) (val & 0x7F);
			if((pre_rest_vol < 99) && (rt_rest_vol > pre_rest_vol)){
				charger->rest_vol ++;
			}
		}

    /* if battery volume changed, inform uevent */
    if(charger->rest_vol - pre_rest_vol){
        DBG_PSY_MSG("battery vol change: %d->%d \n", pre_rest_vol, charger->rest_vol);
        pre_rest_vol = charger->rest_vol;
        axp_write(charger->master,AXP20_DATA_BUFFER1,charger->rest_vol | 0x80);
        if(charger->rest_vol == 100){
        	axp_clr_bits(charger->master,0x32,0x38);
        }
        power_supply_changed(&charger->batt);
    }
	
	axp_read(charger->master, 0x0e,&val);
	printk("[AXP]=======================val = 0x%x\n",val);
	if(val){
		axp_close(charger);
		axp_set_bits(charger->master,0x4b,0x03);
	}
	axp_write(charger->master, 0x0e,0x00);
#if defined (CONFIG_AXP_CHGCHANGE)
  	if(pmu_resume_chgcur == 0)
  		axp_clr_bits(charger->master,AXP20_CHARGE_CONTROL1,0x80);
  	else
  		axp_set_bits(charger->master,AXP20_CHARGE_CONTROL1,0x80);

  	DBG_PSY_MSG("pmu_resume_chgcur = %d\n", pmu_resume_chgcur);

    if(pmu_resume_chgcur >= 300000 && pmu_resume_chgcur <= 1800000){
        tmp = (pmu_resume_chgcur -200001)/100000;
        charger->chgcur = tmp *100000 + 300000;
        axp_update(charger->master, AXP20_CHARGE_CONTROL1, tmp,0x0F);
    }
#endif

	charger->disvbat = 0;
	charger->disibat = 0;
    schedule_delayed_work(&charger->work, charger->interval);

    return 0;
}

static void axp20_shutdown(struct platform_device *dev)
{
    uint8_t tmp;
    struct axp_charger *charger = platform_get_drvdata(dev);
    
    cancel_delayed_work_sync(&charger->work);
  	axp_clr_bits(charger->master,AXP20_CAP,0x80);

#if defined (CONFIG_AXP_CHGCHANGE)
  	if(pmu_shutdown_chgcur == 0)
  		axp_clr_bits(charger->master,AXP20_CHARGE_CONTROL1,0x80);
  	else
  		axp_set_bits(charger->master,AXP20_CHARGE_CONTROL1,0x80);

		DBG_PSY_MSG("pmu_shutdown_chgcur = %d\n", pmu_shutdown_chgcur);

    if(pmu_shutdown_chgcur >= 300000 && pmu_shutdown_chgcur <= 1800000){
    	tmp = (pmu_shutdown_chgcur -200001)/100000;
    	charger->chgcur = tmp *100000 + 300000;
    	axp_update(charger->master, AXP20_CHARGE_CONTROL1, tmp, 0x0F);
    }
#endif
}

static struct platform_driver axp_battery_driver = {
  .driver = {
    .name = "axp20-supplyer",
    .owner  = THIS_MODULE,
  },
  .probe = axp_battery_probe,
  .remove = axp_battery_remove,
  .suspend = axp20_suspend,
  .resume = axp20_resume,
  .shutdown = axp20_shutdown,
};

static int axp_battery_init(void)
{
  return platform_driver_register(&axp_battery_driver);
}

static void axp_battery_exit(void)
{
  platform_driver_unregister(&axp_battery_driver);
}

module_init(axp_battery_init);
module_exit(axp_battery_exit);

MODULE_DESCRIPTION("axp20 battery charger driver");
MODULE_AUTHOR("Donglu Zhang, Krosspower");
MODULE_LICENSE("GPL");
