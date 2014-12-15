/*
 * Battery charger driver for Dialog Semiconductor DA9030
 *
 * Copyright (C) 2008 Compulab, Ltd.
 * 	Mike Rapoport <mike@compulab.co.il>
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
#include <linux/sched.h>
#include <linux/input.h>

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "axp-mfd.h"

#include "axp-cfg.h"
#include "axp-sply.h"

static inline int axp18_vbat_to_vbat(uint8_t reg)
{
	return reg * 8 + 2500;
}

static inline int axp18_vbat_to_reg(int vbat)
{
	return (vbat - 2500) / 8;
}

static inline int axp18_vac_to_vbat(uint8_t reg)
{
	return reg * 12 + 3750;
}

static inline int axp18_vac_to_reg(int vbat)
{
	return (vbat - 3750) / 12;
}

static inline int axp18_i_to_ibat(uint8_t reg)
{
	return reg * 2000 / 300 ;
}

static inline int axp18_i_to_reg(int ibat)
{
	return ibat * 300 / 2000;
}

static inline void axp_read_adc(struct axp_charger *charger,
				   struct axp_adc_res *adc)
{
	uint8_t tmp;
	//axp_reads(charger->master, AXP18_VBAT_RES,sizeof(*adc), (uint8_t *)adc);//axp18 can't support muti-reads
	axp_read(charger->master,AXP18_VBAT_RES,&tmp);
	adc->vbat_res = tmp;
	axp_read(charger->master,AXP18_IBAT_RES,&tmp);
	adc->ibat_res = tmp;
	axp_read(charger->master,AXP18_VAC_RES,&tmp);
	adc->vac_res = tmp;
	axp_read(charger->master,AXP18_IAC_RES,&tmp);
	adc->iac_res = tmp;
}

static void axp_charger_update_state(struct axp_charger *charger)
{
	uint8_t val,tmp;

	axp_read(charger->master, AXP18_CHARGE_STATUS, &val);
	charger->is_on = (val & AXP18_IN_CHARGE) ? 1 : 0;

	axp_read(charger->master,AXP18_FAULT_LOG1,&charger->fault);
	axp_read(charger->master, AXP18_FAULT_LOG2, &val);
	charger->is_finish = (val & AXP18_FINISH_CHARGE) ? 1 : 0;
	tmp = val & 0x22;
	val = tmp >> 5 | tmp << 5;
	charger->fault |= val;

	axp_read(charger->master, AXP18_STATUS, &val);
	charger->bat_det = (val & AXP18_STATUS_BATEN) ? 1 : 0;
	charger->ac_det = (val & AXP18_STATUS_DCIEN) ? 1 : 0;
	charger->usb_det = (val & AXP18_STATUS_USBEN) ? 1 : 0;
	charger->ext_valid = (val & AXP18_STATUS_EXTVA) ? 1 : 0;
}

static void axp_charger_update(struct axp_charger *charger)
{
	uint8_t tmp;
	struct axp_adc_res adc;
	charger->adc = &adc;
	axp_read_adc(charger, &adc);

	tmp = charger->adc->vbat_res;
	charger->vbat = axp18_vbat_to_vbat(tmp);
	tmp = charger->adc->ibat_res;
	charger->ibat = axp18_i_to_ibat(tmp);
	tmp = charger->adc->vac_res;
	charger->vac = axp18_vac_to_vbat(tmp);
	tmp = charger->adc->iac_res;
	charger->iac = axp18_i_to_ibat(tmp);
}

#if defined  (CONFIG_AXP_CHARGEINIT)
static void axp_set_charge(struct axp_charger *charger)
{
	uint8_t val,tmp;
	val = 0x00;

	if(charger->chgvol < 4200)
		val &= ~(3 << 5);
	else if (charger->chgvol<4360){
		val &= ~(3 << 5);
		val |= 1 << 6;
	}
	else
		val |= 3 << 5;
	if(charger->limit_on)
		val |= ((charger->chgcur - 100) / 200) | (1 << 3);
	else
		val |= ((charger->chgcur - 100) / 200) ;
	val &= 0x7F;
	val |= charger->chgen << 7;
	axp_read(charger->master, AXP18_CHARGE_CONTROL2, &tmp);
		tmp &= 0x3C;
	if(charger->chgpretime < 30)
		charger->chgpretime = 30;
	if(charger->chgcsttime < 420)
		charger->chgcsttime = 420;
	tmp |= ((charger->chgpretime - 30) / 10) << 6  \
			| (charger->chgcsttime - 420) / 60;

	axp_write(charger->master, AXP18_CHARGE_CONTROL1, val);
	axp_write(charger->master, AXP18_CHARGE_CONTROL2, tmp);

	axp_read(charger->master, AXP18_CHARGE_STATUS, &val);

	if(charger ->chgend == 10)
		val &= ~(1 << 6);
	else
		val |= 1 << 6;
	axp_write(charger->master, AXP18_CHARGE_STATUS, val);
}
#else
static void axp_set_charge(struct axp_charger *charger)
{
}
#endif

static enum power_supply_property axp_battery_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
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
};


static void axp_battery_check_status(struct axp_charger *charger,
				    union power_supply_propval *val)
{
	if (charger->bat_det) {
		if (charger->is_on)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (charger->rest_vol == 100 && charger->ext_valid)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (charger->ext_valid)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
	}
	else
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
}

static void axp_battery_check_health(struct axp_charger *charger,
				    union power_supply_propval *val)
{
	if (charger->fault & AXP18_FAULT_LOG_BATINACT)
		val->intval = POWER_SUPPLY_HEALTH_DEAD;
	else if (charger->fault & AXP18_FAULT_LOG_OVER_TEMP)
		val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (charger->fault & AXP18_FAULT_LOG_COLD)
		val->intval = POWER_SUPPLY_HEALTH_COLD;
	/* low voltage worning */
	else if (charger->fault & AXP18_FAULT_LOG_VBAT_LOW)
		val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	else if (charger->fault & AXP18_FAULT_LOG_VBAT_OVER)
		val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
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
		val->intval = charger->vbat * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = charger->ibat * 1000;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = charger->battery_info->name;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = charger->battery_info->charge_full_design;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = charger->battery_info->charge_full_design;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = charger->rest_vol;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
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
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = charger->bat_det;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = (!charger->is_on) && (charger->bat_det)&& (! charger->ext_valid);
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
		val->strval = charger->ac.name;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = (charger->ac_det) && (charger->ext_valid);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = charger->ac_det;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = charger->vac;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = charger->iac;
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
		val->strval = charger->usb.name;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = charger->usb_det;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval =(charger->usb_det)&&(charger->ext_valid);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}


static int axp_battery_event(struct notifier_block *nb, unsigned long event,
				void *data)
{
	struct axp_charger *charger =
		container_of(nb, struct axp_charger, nb);

	switch (event) {
	case AXP18_IRQ_BATIN:
	case AXP18_IRQ_EXTIN:
		axp_set_bits(charger->master, AXP18_CHARGE_CONTROL1, 0x80);
		break;
	case AXP18_IRQ_BATRE:
	case AXP18_IRQ_EXTOV:
	case AXP18_IRQ_EXTRE:
	case AXP18_IRQ_TEMOV:
	case AXP18_IRQ_TEMLO:
		axp_clr_bits(charger->master, AXP18_CHARGE_CONTROL1, 0x80);
		break;
	default:
		break;
	}
	return 0;
}

static void axp_battery_setup_psy(struct axp_charger *charger)
{
	struct power_supply *batt = &charger->batt;
	struct power_supply *ac = &charger->ac;
	struct power_supply *usb = &charger->usb;
	struct power_supply_info *info = charger->battery_info;

	batt->name = "battery";
	batt->type = POWER_SUPPLY_TYPE_BATTERY;
	batt->get_property = axp_battery_get_property;
	batt->use_for_apm = info->use_for_apm;

	batt->properties = axp_battery_props;
	batt->num_properties = ARRAY_SIZE(axp_battery_props);

	ac->name = "ac";
	ac->type = POWER_SUPPLY_TYPE_MAINS;
	ac->get_property = axp_ac_get_property;

	ac->properties = axp_ac_props;
	ac->num_properties = ARRAY_SIZE(axp_ac_props);

	usb->name = "usb";
	usb->type = POWER_SUPPLY_TYPE_USB;
	usb->get_property = axp_usb_get_property;

	usb->properties = axp_usb_props;
	usb->num_properties = ARRAY_SIZE(axp_usb_props);
}

#if defined  (CONFIG_AXP_CHARGEINIT)
static int axp_battery_adc_set(struct axp_charger *charger)
{
	int ret ;
	uint8_t val;

	/*enable adc and set adc */
	val=(charger->sample_time / 8 - 1) << 2 | AXP18_ADC_BATVOL_ENABLE
		| AXP18_ADC_BATCUR_ENABLE | AXP18_ADC_ACCUR_ENABLE
		| AXP18_ADC_ACVOL_ENABLE;

	ret = axp_write(charger->master, AXP18_ADC_CONTROL, val);

	return ret;
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
	axp_set_charge(charger);
	ret = axp_battery_adc_set(charger);
	return ret;
}

static int axp_get_rdc(struct axp_charger *charger)
{
	uint8_t val[3];
	unsigned int i,temp,pre_temp;
	int averPreVol = 0, averPreCur = 0,averNextVol = 0,averNextCur = 0;

	//axp_reads(charger->master,AXP18_DATA_BUFFER1,2,val);
	axp_read(charger->master,AXP18_DATA_BUFFER1,val);
	axp_read(charger->master,AXP18_DATA_BUFFER2,val+1);
	pre_temp = (((val[0] & 0x7F) << 8 ) + val[1]);

	printk("%d:pre_temp = %d\n",__LINE__,pre_temp);

	if( charger->is_on){
		for(i = 0; i< AXP18_RDC_COUNT; i++){
			axp_charger_update(charger);
			averPreVol += charger->vbat;
			averPreCur += charger->ibat;
			msleep(50);
		}
		averPreVol /= AXP18_RDC_COUNT;
		averPreCur /= AXP18_RDC_COUNT;
		axp_clr_bits(charger->master,AXP18_CHARGE_CONTROL2,0x80);
		msleep(500);
		for(i = 0; i< AXP18_RDC_COUNT; i++){
			axp_charger_update(charger);
			averNextVol += charger->vbat;
			averNextCur += charger->ibat;
			msleep(50);
		}
		averNextVol /= AXP18_RDC_COUNT;
		averNextVol /= AXP18_RDC_COUNT;
		axp_set_bits(charger->master,AXP18_CHARGE_CONTROL2,0x80);
		msleep(500);
		if(ABS(averPreCur - averNextCur) > 200){
			temp = 1000 * ABS(averPreVol - averNextVol) / ABS(averPreCur);
			if((temp < 5) || (temp > 5000)){
				return pre_temp;
			}
			else {
				temp += pre_temp;
				temp >>= 1;
				val[0] = ((temp & 0xFF00) | 0x8000) >> 8;
				val[1] = AXP18_DATA_BUFFER2;
				val[2] =  temp & 0x00FF;
				axp_writes(charger->master,AXP18_DATA_BUFFER1,3,val );
				return temp;
			}
		}
		else
			return pre_temp;
	}
	else
		return pre_temp;
}

static int axp_cal_restvol(int vol)
{
    if(vol > 4150)
    {
        return 100;
    }
    else if(vol < 2700)
    {
        return 0;
    }
    else if(vol < 3200)
    {
        return (10 * (vol - 2700) / 5000);
    }
    else if(vol < 3650)
    {
        return (1500+ 17000 * (vol - 3200) / 450)/1000;
    }
    else if(vol < 3750)
    {
        return (18500 + 1500 * (vol - 3650) / 10)/1000;              //20%改为18%
    }
    else if(vol < 3830)
    {
        return (33500 + (1500 * (vol - 3750)/(383 - 375)))/1000;
    }
    else if(vol < 4000)
    {
        return (48500 + (4000 * (vol - 3830)/(400 - 383)))/1000;    //40%改为37%
    }
    else
    {
        if(vol > 4150)
        {
            vol = 4150;
        }
        return (855 + (150 * (vol - 4000)/150))/10;                 //4200-3950 = 250，13%改为15%
    }
}

int Bat_Pre_Cur = 1;


static void axp_cal_rest(struct axp_charger *charger, int this_rdc)
{
    int battery_cap;
	uint16_t Iconst_current = 1;
	uint8_t  DCIN_Presence, DCIN_Pre_Presence = 0;
	battery_cap = charger->battery_info->charge_full_design;

	if(charger->vac < 4200){
        charger->ac_not_enough = 1;
	}
	else {
        charger->ac_not_enough = 0;
	}
	if(charger->bat_det){
        int Ichgset, total_vol = 0, Iendchg, Tcv_Rest_Vol, Tcv = 0;
		int Internal_Ibat = 1;
		if(charger->ibat == 0){
            charger->ibat = 1;
		}
		total_vol = charger->vbat;
		Internal_Ibat = charger->ibat;
		Ichgset = charger->chgcur;
		Iendchg = Ichgset * charger->chgend/100;
		DCIN_Presence = charger->ac_det;
		if((charger->vac < charger->vbat + 200) || (charger->vac < 4200)){
            if((charger->ibat < (3 * Ichgset / 5)) && (charger->ext_valid)){
				charger->ac_not_enough = 1;
            }
			else {
                charger->ac_not_enough = 0;
			}
		}
		else {
            charger->ac_not_enough = 0;
		}
		if(charger->ext_valid){
            total_vol -= charger->ibat * this_rdc * CHG_RDC_RATE / 100000;
			charger->vbat = total_vol;
		}
		else {
            charger->ibat *= DISCHARGE_CUR_RATE / 10;
			if(charger->ibat > (MAX_BAT_CUR * Ichgset / 10)){
                charger->ibat = 10 * charger->ibat / DISCHARGE_CUR_RATE;
			}
			charger->ibat = (charger->ibat + Bat_Pre_Cur)/2;
			if(DCIN_Pre_Presence != DCIN_Presence){
                charger->ibat = Internal_Ibat;
			}
			total_vol += charger->ibat * (this_rdc - DISCHARGE_RDC_CAL) / 1000;
			charger->vbat = total_vol;
		}
		Bat_Pre_Cur = charger->ibat;
		DCIN_Pre_Presence = DCIN_Presence;
		charger->rest_vol = axp_cal_restvol(total_vol);
		if(charger->ext_valid && charger->is_on){
            if(charger->vbat < 4190){
                Tcv_Rest_Vol = axp_cal_restvol(4200 - charger->ibat * this_rdc / 1000);
				Iconst_current = charger->ibat;
				if(Tcv_Rest_Vol < 70){
                    Tcv = 60 * (100 - Tcv_Rest_Vol) * battery_cap / (45 * charger->ibat);
				}
				else {
                    Tcv = 60 * (100 - Tcv_Rest_Vol) * battery_cap / (35 * charger->ibat);
				}
				charger->rest_time = 6 * battery_cap * ABS(Tcv_Rest_Vol - charger->rest_vol) \
					/ charger->ibat / 10 + Tcv ;
			}
			else {
                if(Iconst_current == 1){
                    Iconst_current = Ichgset;
				}
				if(Tcv == 0){
                    Tcv_Rest_Vol = axp_cal_restvol(4200 - charger->ibat * this_rdc / 1000);
					if(Tcv_Rest_Vol < 70){
                        Tcv = 60 * (100 - Tcv_Rest_Vol) * battery_cap / (45 * charger->ibat);
				    }
				    else {
                        Tcv = 60 * (100 - Tcv_Rest_Vol) * battery_cap / (35 * charger->ibat);
				    }
				}
				if(charger->ibat < Iendchg){
                    charger->rest_time = 1;
				}
				else {
                    charger->rest_time = Tcv * (90 + 100 * Iendchg / charger->ibat) *     \
						(90 + 100 * Iendchg / charger->ibat) * ABS(charger->ibat - Iendchg) \
						/ Iconst_current / 10000;
				}
			}
		}
		else {
            if(total_vol < 3000){
                charger->rest_time = 0;
			}
			else {
                charger->rest_time = (60 * battery_cap * ABS(charger->rest_vol - 6) / charger->ibat \
					+ 50) / 102;
			}
		}
	}
	else {
        charger->vbat = 2500;
		charger->ibat = 0;
		charger->rest_time = 0;
		charger->rest_vol = 0;
	}
}

static int axp_main_task(void *arg)
{
    struct axp_charger *charger = arg;
	int batcap_count = 0, battime_count = 0;
    uint16_t batcap[AXP18_VOL_MAX], battime[AXP18_TIME_MAX];
    uint16_t pre_batcap = 0;
    uint8_t rdc_flag = 0, tmp_value[2];
    uint8_t pre_charge_status = 0;
    uint16_t batcap_index = 0, battime_index = 0;
    int total_vol = 0, total_time = 0;
    int this_rdc;
	uint8_t v[3] = {0, 0, 0};
	uint8_t w[5] = {0, 0, 0, 0, 0};
	int events;
    bool peklong;
    bool pekshort;
    uint8_t long_cnt = 0;
	bool status_usb, pre_status_usb;
    bool status_ac, pre_status_ac;
    bool status_bat, pre_status_bat;
	bool pre_rdcflag;
	status_usb = 0;
    pre_status_usb = 0;
    status_ac = 0;
    pre_status_ac = 0;
    status_bat = 0;
    pre_status_bat =0;

	//axp_reads(charger->master,AXP18_DATA_BUFFER1,2,tmp_value);
	axp_read(charger->master,AXP18_DATA_BUFFER1,tmp_value);
	axp_read(charger->master,AXP18_DATA_BUFFER2,tmp_value+1);
	this_rdc = (tmp_value[0] & 0x7F << 8) + tmp_value[1];
	pre_rdcflag = tmp_value[0] >> 7;
    if(this_rdc > 5000 || pre_rdcflag == 0)
		this_rdc = BATRDC;

	while(1){
		if(kthread_should_stop()) break;
        axp_charger_update_state(charger);
		axp_charger_update(charger);

	//axp_reads(charger->master,POWER18_INTSTS1, 3, v);
		axp_read(charger->master,POWER18_INTSTS1,v);
		axp_read(charger->master,POWER18_INTSTS2,v+1);
		axp_read(charger->master,POWER18_INTSTS3,v+2);
		events = (v[2] << 16) | (v[1] << 8) | v[0];
		w[0] = v[0];
		w[1] = POWER18_INTSTS2;
		w[2] = v[1];
		w[3] = POWER18_INTSTS3;
		w[4] = v[2];

		peklong = (events & AXP18_IRQ_PEKLO)? 1 : 0;
		pekshort = (events & AXP18_IRQ_PEKSH )? 1 : 0;

		status_ac = charger->ac_det;
		status_usb = charger->usb_det;
        status_bat = (!charger->is_on)&&(charger->bat_det);

        if(status_usb != pre_status_usb || status_ac != pre_status_ac || status_bat != pre_status_bat )
        {
            power_supply_changed(&charger->batt);
			pre_status_ac =  status_ac;
			pre_status_usb = status_usb;
			pre_status_bat = status_bat;
         }

		/* simulate a key_up after peklong*/
		if(long_cnt)
        {
			long_cnt--;
			if(long_cnt == 0 )
            {
				printk("press long up\n");
				input_report_key(powerkeydev, KEY_POWER, 0);
				input_sync(powerkeydev);
		    }

        }

		if(peklong)
		{
			printk("press long\n");
			axp_writes(charger->master,POWER18_INTSTS1,5,w);
			input_report_key(powerkeydev, KEY_POWER, 1);
			input_sync(powerkeydev);
			long_cnt = 5;
			//msleep(100);
			//input_report_key(powerkeydev, KEY_POWER, 0);
			//input_sync(powerkeydev);
		}

		if(pekshort)
		{
			printk("press short\n");
			axp_writes(charger->master,POWER18_INTSTS1,5,w);

			input_report_key(powerkeydev, KEY_POWER, 1);
			input_sync(powerkeydev);
			msleep(100);
			input_report_key(powerkeydev, KEY_POWER, 0);
			input_sync(powerkeydev);
		}

		if((charger->is_on)&&(!rdc_flag)){
			if(charger->ibat > 220){
                rdc_flag = 1;
				this_rdc = axp_get_rdc(charger);
			}
		}
		if(charger->bat_det == 0){
            charger->rest_time = 0;
			charger->rest_vol  = 0;
		}
		else{
			axp_cal_rest(charger, this_rdc);
			if(battime_index == AXP18_TIME_MAX){
            	battime_index = 0;
			}
			if(battime_count < AXP18_TIME_MAX){
            	battime[battime_index ++ ] = charger->rest_time;
            	total_time += charger->rest_time;
            	battime_count ++;
			}
			else{
            	total_time -= battime[battime_index];
            	total_time += charger->rest_time;
            	battime[battime_index ++ ] = charger->rest_time;
        	}
			charger->rest_time = total_time / battime_count;
			if(batcap_index == AXP18_VOL_MAX){
            	batcap_index = 0;
			}
			if(batcap_count < AXP18_VOL_MAX){
              	batcap[batcap_index ++ ] = charger->rest_vol;
              	total_vol += charger->rest_vol;
              	batcap_count ++;
        	}
        	else{
              	total_vol -= batcap[batcap_index];
              	total_vol += charger->rest_vol;
              	batcap[batcap_index ++ ] = charger->rest_vol;
        	}
			charger->rest_vol = total_vol / batcap_count;

			//printk("charger->rest_vol = %d\n",charger->rest_vol);
			if((charger->is_on) && (charger->rest_vol == 100)){
            	charger->rest_vol = 99;
			}

			if((charger->is_on) && (batcap_count == AXP18_VOL_MAX)){
           		if(charger->rest_vol < pre_batcap){
              		charger->rest_vol = pre_batcap;
           		}
			}
			if((!charger->is_on) && (batcap_count == AXP18_VOL_MAX)){
           		if(charger->rest_vol > pre_batcap){
              		charger->rest_vol = pre_batcap;
           		}
			}

			if((pre_charge_status == 1) && (!charger->is_on) && (charger->bat_det) && (charger->ext_valid)){//充电结束时刷新为100
            	charger->rest_vol = total_vol / batcap_count;
			}

			pre_charge_status = charger->is_on;

			//printk("charger->rest_vol = %d\n",charger->rest_vol);

			/* if battery volume changed, inform uevent */
			if(charger->rest_vol - pre_batcap)
			{
				printk("battery vol change: %d, %d \n", pre_batcap, charger->rest_vol);
				pre_batcap = charger->rest_vol;
				power_supply_changed(&charger->batt);
			}
		}
		ssleep(1);
	}
	return 0;
}

static ssize_t chgen_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;
	axp_read(charger->master, AXP18_CHARGE_CONTROL1, &val);
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
		axp_set_bits(charger->master, AXP18_CHARGE_CONTROL1, 0x80);
	}
	else{
		charger->chgen = 0;
		axp_clr_bits(charger->master, AXP18_CHARGE_CONTROL1, 0x80);
	}
	return count;
}

static ssize_t chgcurlimen_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	char val;
	axp_read(charger->master, AXP18_CHARGE_CONTROL1, &val);
	charger->limit_on = val >> 3 & 0x01;
	return sprintf(buf, "%d\n",charger->limit_on);
}

static ssize_t chgcurlimen_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	var = simple_strtoul(buf, NULL, 10);
	if(var){
		charger->limit_on = 1;
		axp_set_bits(charger->master, AXP18_CHARGE_CONTROL1, 0x08);
	}
	else{
		charger->limit_on = 0;
		axp_clr_bits(charger->master, AXP18_CHARGE_CONTROL1, 0x08);
	}
	return count;
}

static ssize_t chgmicrovol_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;
	axp_read(charger->master, AXP18_CHARGE_CONTROL1, &val);
    switch ((val >> 5) & 0x03){
		case 0: charger->chgvol = 4100000;break;
		case 1: charger->chgvol = 4200000;break;
		case 2: charger->chgvol = 4200000;break;
		case 3: charger->chgvol = 4360000;break;
		default:break;
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
		case 4200000:tmp = 2;break;
		case 4360000:tmp = 3;break;
		default:  tmp = 4;break;
	}
	if(tmp < 4){
		charger->chgvol = var;
		axp_read(charger->master, AXP18_CHARGE_CONTROL1, &val);
		val &= 0x9F;
		val |= tmp << 5;
		axp_write(charger->master, AXP18_CHARGE_CONTROL1, val);
	}
	return count;
}

static ssize_t chgmicrocur_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;
	axp_read(charger->master, AXP18_CHARGE_CONTROL1, &val);
	charger->chgcur = (val & 0x07) * 200000 +100000;
	return sprintf(buf, "%d\n",charger->chgcur);
}

static ssize_t chgmicrocur_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t val;
	var = simple_strtoul(buf, NULL, 10);
	if(var >= 100000 && var <= 1500000){
		val = (var -100000)/200000;
		charger->chgcur = val *200000 + 100000;
		axp_read(charger->master, AXP18_CHARGE_CONTROL1, &val);
		val &= 0xF8;
		val |= val;
		axp_write(charger->master, AXP18_CHARGE_CONTROL1, val);
	}
	return count;
}

static ssize_t chgendcur_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;
	axp_read(charger->master, AXP18_CHARGE_STATUS, &val);
	charger->chgend = ((val >> 6)& 0x01)? 15 : 10;
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
		axp_clr_bits(charger->master ,AXP18_CHARGE_STATUS,0x40);
	}
	else if (var == 15){
		charger->chgend = var;
		axp_set_bits(charger->master ,AXP18_CHARGE_STATUS,0x40);

	}
	return count;
}

static ssize_t chgpretimemin_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;
	axp_read(charger->master,AXP18_CHARGE_CONTROL2, &val);
 	charger->chgpretime = (val >> 6) * 10 +30;
	return sprintf(buf, "%d\n",charger->chgpretime);
}

static ssize_t chgpretimemin_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t val,tmp;
	var = simple_strtoul(buf, NULL, 10);
	if(var >= 30 && var <= 60){
		tmp = (var - 30)/10;
		charger->chgpretime = tmp * 10 + 30;
		axp_read(charger->master,AXP18_CHARGE_CONTROL2,&val);
		val &= 0x3F;
		val |= (tmp << 6);
		axp_write(charger->master,AXP18_CHARGE_CONTROL2,val);
	}
	return count;
}

static ssize_t chgcsttimemin_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;
	axp_read(charger->master,AXP18_CHARGE_CONTROL2, &val);
	charger->chgcsttime = (val & 0x03) *60 + 420;
	return sprintf(buf, "%d\n",charger->chgcsttime);
}

static ssize_t chgcsttimemin_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t val,tmp;
	var = simple_strtoul(buf, NULL, 10);
	if(var >= 420 && var <= 600){
		tmp = (var - 420)/60;
		charger->chgcsttime = tmp * 60 + 420;
		axp_read(charger->master,AXP18_CHARGE_CONTROL2,&val);
		val &= 0xFC;
		val |= tmp;
		axp_write(charger->master,AXP18_CHARGE_CONTROL2,val);
	}
	return count;
}

static ssize_t adcfreq_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;
	axp_read(charger->master, AXP18_ADC_CONTROL, &val);
	switch ((val >> 2) & 0x03){
		 case 0: charger->sample_time = 8;break;
		 case 1: charger->sample_time = 16;break;
		 case 2: charger->sample_time = 25;break;
		 case 3: charger->sample_time = 32;break;
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
	axp_read(charger->master, AXP18_ADC_CONTROL, &val);
	switch (var){
		case 8: val &= ~(3 << 2);charger->sample_time = 8;break;
		case 16: val &= ~(3 << 2);val |= 1 << 2;charger->sample_time = 16;break;
		case 25: val &= ~(3 << 2);val |= 2 << 2;charger->sample_time = 25;break;
		case 32: val |= 3 << 2;charger->sample_time = 32;break;
		default: break;
		}
	axp_write(charger->master, AXP18_ADC_CONTROL, val);
	return count;
}

static ssize_t vholden_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;
	axp_read(charger->master,AXP18_CHARGE_VBUS, &val);
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
		axp_set_bits(charger->master, AXP18_CHARGE_VBUS, 0x40);
	else
		axp_clr_bits(charger->master, AXP18_CHARGE_VBUS, 0x40);

	return count;
}

static ssize_t vhold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	uint8_t val;
	int vhold;
	axp_read(charger->master,AXP18_CHARGE_VBUS, &val);
	switch((val>>4)& 0x03)
	{
		case 0: vhold = 4220000;break;
		case 1: vhold = 4400000;break;
		case 2: vhold = 4550000;break;
		case 3: vhold = 4700000;break;
		default:return -EINVAL;
	}
	return sprintf(buf, "%d\n",vhold);
}

static ssize_t vhold_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	struct axp_charger *charger = dev_get_drvdata(dev);
	int var;
	uint8_t val,tmp;
	var = simple_strtoul(buf, NULL, 10);
	if(var >= 4220000 && var <=4700000){
		if(var == 4220000)
			tmp = 0;
		else if(val <= 4400000)
			tmp = 1;
		else if(val <= 4550000)
			tmp = 2;
		else
			tmp = 3;
		axp_read(charger->master, AXP19_CHARGE_VBUS,&val);
		val &= 0xCF;
		val |= tmp << 4;
		axp_write(charger->master, AXP19_CHARGE_VBUS,val);
	}
	return count;
}
static struct device_attribute axp_charger_attrs[] = {
	AXP_CHG_ATTR(chgen),
	AXP_CHG_ATTR(chgcurlimen),
	AXP_CHG_ATTR(chgmicrovol),
	AXP_CHG_ATTR(chgmicrocur),
	AXP_CHG_ATTR(chgendcur),
	AXP_CHG_ATTR(chgpretimemin),
	AXP_CHG_ATTR(chgcsttimemin),
	AXP_CHG_ATTR(adcfreq),
	AXP_CHG_ATTR(vholden),
	AXP_CHG_ATTR(vhold),
};

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


static int axp_battery_probe(struct platform_device *pdev)
{
	struct axp_charger *charger;
	struct axp_supply_init_data *pdata = pdev->dev.platform_data;
	int ret;

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
	set_bit(KEY_POWER, powerkeydev->keybit);

	ret = input_register_device(powerkeydev);
	if(ret)
	{
		printk("Unable to Register the power key\n");
	}

	if (pdata == NULL)
		return -EINVAL;

	if (pdata->chgcur > 1500 ||
	    pdata->chgvol < 4100 ||
	    pdata->chgvol > 4360){
            printk("charger milliamp is too high or target voltage is over range\n");
		    return -EINVAL;
		}

	if (pdata->chgpretime < 30 || pdata->chgpretime >60 ||
		pdata->chgcsttime < 420 || pdata->chgcsttime > 600){
            printk("prechaging time or constant current charging time is over range\n");
		    return -EINVAL;
		}

	charger = kzalloc(sizeof(*charger), GFP_KERNEL);
	if (charger == NULL)
		return -ENOMEM;

	charger->master = pdev->dev.parent;

	charger->chgcur				= pdata->chgcur;
	charger->chgvol				= pdata->chgvol;
	charger->chgend				= pdata->chgend;
	charger->sample_time		= pdata->sample_time;
	charger->chgen				= pdata->chgen;
	charger->limit_on			= pdata->limit_on;
	charger->chgpretime			= pdata->chgpretime;
	charger->chgcsttime			= pdata->chgcsttime;
	charger->battery_info		= pdata->battery_info;
	charger->battery_low		= pdata->battery_low;
	charger->battery_critical	= pdata->battery_critical;

	ret = axp_battery_first_init(charger);
	if (ret)
		goto err_charger_init;

	charger->nb.notifier_call = axp_battery_event;
	ret = axp_register_notifier(charger->master, &charger->nb, AXP18_NOTIFIER_ON);
	if (ret)
		goto err_notifier;

	axp_battery_setup_psy(charger);
	ret = power_supply_register(&pdev->dev, &charger->batt);
	if (ret)
		goto err_ps_register;

	ret = power_supply_register(&pdev->dev, &charger->ac);
	if (ret){
		power_supply_unregister(&charger->batt);
		goto err_ps_register;
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

  	main_task = kthread_run(axp_main_task,charger,"kaxp18");
	if(IS_ERR(main_task)){

      printk("Unable to start main task.\n");

      ret = PTR_ERR(main_task);

      main_task = NULL;

      return ret;

    }
    return 0;

err_ps_register:
	axp_unregister_notifier(charger->master, &charger->nb, AXP18_NOTIFIER_ON);

err_notifier:
	//cancel_delayed_work(&charger->work);

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

	axp_unregister_notifier(charger->master, &charger->nb, AXP18_NOTIFIER_ON);
	//cancel_delayed_work(&charger->work);
	power_supply_unregister(&charger->usb);
	power_supply_unregister(&charger->ac);
	power_supply_unregister(&charger->batt);

	kfree(charger);
	input_unregister_device(powerkeydev);
	kfree(powerkeydev);

	return 0;
}

static struct platform_driver axp_battery_driver = {
	.driver	= {
		.name	= "axp18-supplyer",
		.owner	= THIS_MODULE,
	},
	.probe = axp_battery_probe,
	.remove = axp_battery_remove,
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

MODULE_DESCRIPTION("AXP18 battery charger driver");
MODULE_AUTHOR("Donglu Zhang, Krosspower");
MODULE_LICENSE("GPL");
