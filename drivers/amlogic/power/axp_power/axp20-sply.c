/*
 * Battery charger driver for Dialog Semiconductor DA9030
 *
 * Copyright (C) 2008 Compulab, Ltd.
 *  Mike Rapoport <mike@compulab.co.il>
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
#include <linux/utsname.h>

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/input.h>
#include <mach/am_regs.h>
#include <mach/gpio.h>
#include "axp-mfd.h"
#include <linux/amlogic/aml_rtc.h>
#include <linux/amlogic/aml_pmu_common.h>
#include <mach/usbclock.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/wakelock_android.h>
#include <linux/earlysuspend.h>
#endif

#ifdef CONFIG_UBOOT_BATTERY_PARAMETERS
#include <linux/amlogic/battery_parameter.h>
#endif

#define AXP_OCV_BUFFER0					(0xBC)

#define AML_PMU_CALL_BACK_DEMO      0

#include "axp-sply.h"
#include "axp-gpio.h"

#define   BATCAPCORRATE  5

#define ABS(x)				((x) >0 ? (x) : -(x))
#define CHECK_DRIVER()                                                  \
    if (!g_axp20_supply) {                                                    \
        AXP_PMU_DBG("AXP driver has not probed now, please wait\n");    \
        return -ENODEV;                                                 \
    }                                                                   \

#ifdef CONFIG_AMLOGIC_USB
struct later_job {
    int flag;
    int value;
};
static struct later_job axp202_charger_job = {};
#endif

struct battery_parameter *axp_pmu_battery = NULL;
struct axp20_supply      *g_axp20_supply  = NULL;


#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend axp_early_suspend;
static int    in_early_suspend = 0;
static struct wake_lock axp202_lock;
#endif

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

void axp_read_adc(struct axp20_supply *, struct axp_adc_res *);
int  axp20_update_status(struct aml_charger *);
void axp_charger_update(struct axp20_supply *);


#if defined  (CONFIG_AXP_CHARGEINIT)
static int axp_set_charge(struct axp20_supply *supply)
{
    uint8_t val = 0x00;
    int tmp;

    if (axp_pmu_battery->pmu_init_chgvol < 4150000) {               // target charge voltage set to 4.10V
        val &= ~(0x03 << 5);
    } else if (axp_pmu_battery->pmu_init_chgvol < 4200000) {        // target charge voltage set to 4.15V
        val |=  (0x01 << 5); 
    } else if (axp_pmu_battery->pmu_init_chgvol < 4360000) {        // target charge voltage set to 4.20V
        val |=  (0x02 << 5);
    } else {                                                        // target charge voltage set to 4.36V
        val |=  (0x03 << 5);
    }

    tmp = axp_pmu_battery->pmu_init_chgcur;
    if (tmp < 300000) {                                             // set charge current
        tmp = 300000;
    } else if (tmp > 1800000) {
        tmp = 1800000;
    }

    val |= (((tmp - 200001) / 100000) & 0x0f);
    if (axp_pmu_battery->pmu_init_chgend_rate == 10) {              // set charge end rate
        val &= ~(1 << 4);    
    } else {
        val |=  (1 << 4);    
    } 

    if (axp_pmu_battery->pmu_init_chg_enabled) {                    // enable charger
        val |=  (1 << 7);
    } else {
        AXP_PMU_DBG("something wrong with your config, do you sure not open charger?\n");    
        val &= ~(1 << 7);
    }
    axp_write(supply->master, AXP20_CHARGE_CONTROL1, val);

    val = 0;
    tmp = axp_pmu_battery->pmu_init_chg_pretime;
    if (tmp < 40) {                                                 // set pre-charge time out
        tmp = 40;    
    } else if (tmp > 70) {
        tmp = 70;    
    }
    val |= ((((tmp - 40) / 10) << 6) & 0xc0);
    tmp = axp_pmu_battery->pmu_init_chg_csttime;                    // set fast-charge time out
    if (tmp < 360) {
        tmp = 360;    
    } else if (tmp > 720) {
        tmp = 720;    
    }
    val |= (((tmp - 360) / 120) & 0x03);
	return axp_update(supply->master, AXP20_CHARGE_CONTROL2, val, 0xc3);
}
#else
static void axp_set_charge(struct axp20_supply *supply)
{

}
#endif

/*-------------------- Interface for power supply ---------------------*/

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
    POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
    POWER_SUPPLY_PROP_CAPACITY,
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

static void axp_battery_check_status(struct axp20_supply *supply, union power_supply_propval *val)
{
    struct aml_charger *charger = &supply->aml_charger;
    //if (aml_charger->bat_det) {
    if (1) {
        if (charger->ext_valid) {
            if (charger->rest_vol == 100) {
                val->intval = POWER_SUPPLY_STATUS_FULL;
                if (supply->led_control) {
                    axp_update(supply->master, 0x32, 0x08, 0x38);
                    supply->led_control(AXP_LED_CTRL_BATTERY_FULL);
                }
            } else if (charger->rest_vol == 0 && 
            		   charger->charge_status == CHARGER_DISCHARGING && 
                       charger->bat_det) {
                // protect for over-discharging
                val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
            } else {
                val->intval = POWER_SUPPLY_STATUS_CHARGING;
                if (supply->led_control) {
                    axp_clr_bits(supply->master,0x32,0x38);
                    supply->led_control(AXP_LED_CTRL_CHARGING);
                }
            }
        } else {
            val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
            if (supply->led_control) {
                axp_clr_bits(supply->master,0x32,0x38);
                supply->led_control(AXP_LED_CTRL_DISCHARGING);
            }
        }
   //} else {
   //    val->intval = POWER_SUPPLY_STATUS_FULL;
    }
}

static void axp_battery_check_health(struct axp20_supply *supply,
            union power_supply_propval *val)
{
#if 0
    struct aml_charger *charger = &supply->aml_charger;
    if (charger->fault & AXP20_FAULT_LOG_BATINACT) {                // active battery
        val->intval = POWER_SUPPLY_HEALTH_DEAD;
    } else if (charger->fault & AXP20_FAULT_LOG_OVER_TEMP) {        // over temperature
        val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
    } else if (charger->fault & AXP20_FAULT_LOG_COLD) {             // under temperature 
        val->intval = POWER_SUPPLY_HEALTH_COLD;
    } else {
        val->intval = POWER_SUPPLY_HEALTH_GOOD;
    }
#else
    val->intval = POWER_SUPPLY_HEALTH_GOOD;
#endif
}

static int axp_battery_get_property(struct power_supply         *psy,
                                    enum   power_supply_property psp,
                                    union  power_supply_propval *val)
{
    struct axp20_supply *supply;
    struct aml_charger  *charger;
    int ret     = 0;
    supply      = container_of(psy, struct axp20_supply, batt);
    charger     = &supply->aml_charger;

    switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:                              // check battery status
        axp_battery_check_status(supply, val);
        break;
    case POWER_SUPPLY_PROP_HEALTH:
        axp_battery_check_health(supply, val);
        break;
    case POWER_SUPPLY_PROP_TECHNOLOGY:
        val->intval = supply->battery_info->technology;
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
        val->intval = supply->battery_info->voltage_max_design;
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
        val->intval = supply->battery_info->voltage_min_design;
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        val->intval = charger->ocv * 1000;
        break;
    case POWER_SUPPLY_PROP_CURRENT_NOW:                         // positive for charging, negative for discharging
        if (charger->charge_status == CHARGER_CHARGING) {
            val->intval = charger->ibat * 1000;
        } else {
            val->intval = charger->ibat * 1000 * -1;
        }
        break;
    case POWER_SUPPLY_PROP_MODEL_NAME:
        val->strval = supply->batt.name;
        break;
    case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
        val->intval = supply->battery_info->energy_full_design;
        break;
    case POWER_SUPPLY_PROP_CAPACITY:
        val->intval = charger->rest_vol;
        break;
    case POWER_SUPPLY_PROP_ONLINE:
        val->intval = (charger->bat_det) && (!charger->ext_valid);
        break;
    case POWER_SUPPLY_PROP_PRESENT:
        val->intval = charger->bat_det;
        break;
    case POWER_SUPPLY_PROP_TEMP:
        val->intval =  300;
        break;
    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

static int axp_ac_get_property(struct power_supply         *psy,
                               enum   power_supply_property psp,
                               union  power_supply_propval *val)
{
    struct axp20_supply *supply;
    struct aml_charger  *charger;
    int ret = 0;
    supply  = container_of(psy, struct axp20_supply, ac);
    charger = &supply->aml_charger;

    switch(psp){
    case POWER_SUPPLY_PROP_MODEL_NAME:
        val->strval = supply->ac.name;
        break;
    case POWER_SUPPLY_PROP_PRESENT:
        val->intval = charger->dcin_valid;
        break;
    case POWER_SUPPLY_PROP_ONLINE:
        val->intval = charger->dcin_valid;    
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        val->intval = charger->v_dcin * 1000;
        break;
    case POWER_SUPPLY_PROP_CURRENT_NOW:
        val->intval = charger->i_dcin * 1000;
        break;
    default:
        ret = -EINVAL;
        break;
    }
    return ret;
}

static int axp_usb_get_property(struct power_supply         *psy,
                                enum   power_supply_property psp,
                                union  power_supply_propval *val)
{
    struct axp20_supply *supply;
    struct aml_charger  *charger;
    int ret     = 0;
    supply  = container_of(psy, struct axp20_supply, usb);
    charger = &supply->aml_charger;

    switch(psp){
    case POWER_SUPPLY_PROP_MODEL_NAME:
        val->strval = supply->usb.name;
        break;
    case POWER_SUPPLY_PROP_PRESENT:
        val->intval = charger->usb_valid;
        break;
    case POWER_SUPPLY_PROP_ONLINE:
        val->intval = charger->usb_valid;
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        val->intval = charger->v_usb * 1000;
        break;
    case POWER_SUPPLY_PROP_CURRENT_NOW:
        val->intval = charger->i_usb * 1000;
        break;
    default:
        ret = -EINVAL;
        break;
    }
    return ret;
}

static char *supply_list[] = {
    "battery",
};

static void axp_battery_setup_psy(struct axp20_supply *supply)
{
    struct power_supply *batt = &supply->batt;
    struct power_supply *ac   = &supply->ac;
    struct power_supply *usb  = &supply->usb;
    struct power_supply_info *info = supply->battery_info;

    batt->name           = "battery";
    batt->use_for_apm    = info->use_for_apm;
    batt->type           = POWER_SUPPLY_TYPE_BATTERY;
    batt->get_property   = axp_battery_get_property;
    batt->properties     = axp_battery_props;
    batt->num_properties = ARRAY_SIZE(axp_battery_props);

    ac->name             = "ac";
    ac->type             = POWER_SUPPLY_TYPE_MAINS;
    ac->get_property     = axp_ac_get_property;
    ac->supplied_to      = supply_list;
    ac->num_supplicants  = ARRAY_SIZE(supply_list);
    ac->properties       = axp_ac_props;
    ac->num_properties   = ARRAY_SIZE(axp_ac_props);

    usb->name            = "usb";
    usb->type            = POWER_SUPPLY_TYPE_USB;
    usb->get_property    = axp_usb_get_property;
    usb->supplied_to     = supply_list,
    usb->num_supplicants = ARRAY_SIZE(supply_list),
    usb->properties      = axp_usb_props;
    usb->num_properties  = ARRAY_SIZE(axp_usb_props);
};

/*------------------ API for exporting -----------------*/
int axp_set_charge_current(int chgcur)
{
    int tmp;

    CHECK_DRIVER();

    if(chgcur >= 300000 && chgcur <= 1800000){
        tmp = ((chgcur - 200001) / 100000 & 0x0f) | 0x80;                   // enable charge
        axp_update(g_axp20_supply->master, AXP20_CHARGE_CONTROL1, tmp, 0x8F);
    } else if (chgcur == 0) {
        axp_clr_bits(g_axp20_supply->master, AXP20_CHARGE_CONTROL1, 0x80);
    } else {
        AXP_PMU_DBG("Invalid charge current input:%d\n", chgcur);
        return -1;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(axp_set_charge_current);

int axp20_reg_read(int addr, uint8_t *buf)
{
    CHECK_DRIVER();
    return axp_read(g_axp20_supply->master, addr, buf);
}
EXPORT_SYMBOL_GPL(axp20_reg_read);

int axp20_reg_reads(int start_addr, uint8_t *buf, int size)
{
    CHECK_DRIVER();
    return axp_reads(g_axp20_supply->master, start_addr, size, buf);
}
EXPORT_SYMBOL_GPL(axp20_reg_reads);

int axp20_reg_write(int addr, uint8_t val)
{
    CHECK_DRIVER();
    return axp_write(g_axp20_supply->master, addr, val);
}
EXPORT_SYMBOL_GPL(axp20_reg_write);

int axp20_reg_writes(int start_addr, uint8_t *buf, int size)
{
    CHECK_DRIVER();
    return axp_writes(g_axp20_supply->master, start_addr, size, buf);
}
EXPORT_SYMBOL_GPL(axp20_reg_writes);

int axp20_set_bits(int addr, uint8_t bits, uint8_t mask)
{
    uint8_t val; 
    int ret; 
 
    ret = axp20_reg_read(addr, &val); 
    if (ret) { 
        return ret; 
    } 
    val &= ~(mask); 
    val |=  (bits & mask); 
    return axp20_reg_write(addr, val); 
}
EXPORT_SYMBOL_GPL(axp20_set_bits);

int axp_charger_set_usbcur_limit_extern(int usbcur_limit)
{
    uint8_t val;

    CHECK_DRIVER();
	axp_read(g_axp20_supply->master, AXP20_CHARGE_VBUS, &val);
    val &= ~(0x03);
	switch (usbcur_limit) {
        case -1:
		case  0:
			val |= 0x3;
			break;
		case 100:
			val |= 0x2;
			break;
		case 500:
			val |= 0x1;
			break;
		case 900:
			val |= 0x0;
			break;
		default:
		    AXP_PMU_DBG("usbcur_limit=%d, not in 0,100,500,900. please check!\n", usbcur_limit);
		    return -1;
	        break;
	}
	axp_write(g_axp20_supply->master, AXP20_CHARGE_VBUS, val);
	
    return 0;
}
EXPORT_SYMBOL_GPL(axp_charger_set_usbcur_limit_extern);

#if defined  (CONFIG_AXP_CHARGEINIT)
static int axp_battery_adc_set(struct axp20_supply *supply)
{
    int     ret;
	uint8_t val;
    int     freq;

    val = AXP20_ADC_BATVOL_ENABLE  |
          AXP20_ADC_BATCUR_ENABLE  | 
          AXP20_ADC_DCINCUR_ENABLE | 
          AXP20_ADC_DCINVOL_ENABLE | 
          AXP20_ADC_USBVOL_ENABLE  | 
          AXP20_ADC_USBCUR_ENABLE;                                      // enable ADC

    ret = axp_update(supply->master, AXP20_ADC_CONTROL1, val , val);
    if (ret) {
        return ret;
    }
    freq = axp_pmu_battery->pmu_init_adc_freqc;
    if (freq < 25 || freq > 200) {
        AXP_PMU_DBG("%s, invalid pmu_init_adc_freqc:%d, we use 100Hz as default\n",
                    __func__, freq);
        freq = 100;
    }
    ret = axp_read(supply->master, AXP20_ADC_CONTROL3, &val);
    val &= ~(3 << 6);
    switch (freq / 25) {
    case 1:                                                             //  25 Hz
        break;
    case 2:                                                             //  50 Hz
        val |= 1 << 6;
        break;
    case 4:                                                             // 100 Hz
        val |= 2 << 6;
        break;
    case 8:                                                             // 200 Hz
        val |= 3 << 6;
        break;
    default: 
        val |= 2 << 6;                                                  // use 100Hz for default
        break;
    }
    ret = axp_write(supply->master, AXP20_ADC_CONTROL3, val);
    if (ret) {
        return ret;
    }
    return 0;
}
#else
static int axp_battery_adc_set(struct axp20_supply *supply)
{
    return 0;
}
#endif

static int axp_battery_first_init(struct axp20_supply *supply)
{
    int ret;
    
    ret  = axp_set_charge(supply);
    ret |= axp_battery_adc_set(supply);
    return ret;
}

int axp20_set_rdc(int rdc) 
{
    uint32_t rdc_tmp = (rdc * 10000 + 5371) / 10742;

    CHECK_DRIVER();
    axp_set_bits(g_axp20_supply->master, 0xB9, 0x80);                   // stop
    axp_clr_bits(g_axp20_supply->master, 0xBA, 0x80);
    axp_write(g_axp20_supply->master, 0xBB, rdc_tmp & 0xff);
    axp_write(g_axp20_supply->master, 0xBA, (rdc_tmp >> 8) & 0x1F);
    return  axp_clr_bits(g_axp20_supply->master, 0xB9, 0x80);           // start
}

int axp_set_usb_voltage_limit(int voltage)
{
    uint8_t val;

    if (voltage > 4700 || voltage < 4000) {
        AXP_PMU_DBG("%s, Invalid input of voltage limit:%d\n", __func__, voltage);
        return -1;
    }
    CHECK_DRIVER();
    axp_read(g_axp20_supply->master, AXP20_CHARGE_VBUS, &val);
    voltage = ((voltage - 4000) / 100) & 0x07;
    val &= ~(0x38);
    val |=  (voltage << 3);
    return axp_write(g_axp20_supply->master, AXP20_CHARGE_VBUS, val);
}
EXPORT_SYMBOL_GPL(axp_set_usb_voltage_limit);

int axp_set_noe_delay(int delay)
{
    uint8_t val;

    if (delay < 128 || delay > 3000) {
        AXP_PMU_DBG("%s, Invalid n_oe delay input:%d\n", __func__, delay);    
        return -1;
    }
    CHECK_DRIVER();
    axp_read(g_axp20_supply->master, POWER20_OFF_CTL, &val);
    val &= 0xfc;
    val |= ((delay / 1000) & 0x03);
    return axp_write(g_axp20_supply->master, POWER20_OFF_CTL, val);
}
EXPORT_SYMBOL_GPL(axp_set_noe_delay);

int axp_set_pek_on_time(int time)
{
    uint8_t val;
    if (time < 128 || time > 3000) {
        AXP_PMU_DBG("%s, invalid pek_on time:%d\n", __func__, time);
        return -1;
    }
    CHECK_DRIVER();
    axp_read(g_axp20_supply->master,POWER20_PEK_SET,&val);
    val &= 0x3f;
    if (time < 1000) {                                                  // 128ms
        val |= 0x00;
    } else if(time < 2000){                                             // 1S
        val |= 0x80;
    } else if(time < 3000){                                             // 2S
        val |= 0xc0;
    } else {                                                            // 3S
        val |= 0x40;
    }
    return axp_write(g_axp20_supply->master,POWER20_PEK_SET,val);
}
EXPORT_SYMBOL_GPL(axp_set_pek_on_time);

int axp_set_pek_long_time(int time)
{
    uint8_t val;

    if (time < 1000 || time > 2500) {
        AXP_PMU_DBG("%s, invalid pmu_peklong_time:%d\n", __func__, time);
        return -1;
    }
    CHECK_DRIVER();
    axp_read(g_axp20_supply->master,POWER20_PEK_SET,&val);
    val &= 0xcf;
    val |= (((time - 1000) / 500) << 4);
    return axp_write(g_axp20_supply->master,POWER20_PEK_SET,val);
}
EXPORT_SYMBOL_GPL(axp_set_pek_long_time);

int axp_set_pek_off_en(int en)
{
    uint8_t val;

    CHECK_DRIVER();
    axp_read(g_axp20_supply->master,POWER20_PEK_SET,&val);
    val &= 0xf7;
    val |= ((en ? 1 : 0) << 3);
    return axp_write(g_axp20_supply->master,POWER20_PEK_SET,val);
}
EXPORT_SYMBOL_GPL(axp_set_pek_off_en);

int axp_set_pwrok_delay(int delay)
{
    uint8_t val;

    if (delay != 8 && delay != 64) {
        AXP_PMU_DBG("%s, invalid value of pmu_pwrok_time:%d\n", __func__, delay);
    }
    CHECK_DRIVER();
    axp_read(g_axp20_supply->master,POWER20_PEK_SET,&val);
    val &= 0xfb;
    val |= ((delay == 8 ? 0 : 1) << 2);
    return axp_write(g_axp20_supply->master,POWER20_PEK_SET,val);
}
EXPORT_SYMBOL_GPL(axp_set_pwrok_delay);

int axp_set_pek_off_time(int time)
{
    uint8_t val;

    if (time < 4000 || time > 10000) {
        AXP_PMU_DBG("%s, invalid value of pmu_pekoff_time:%d\n", __func__, time);    
    }
    CHECK_DRIVER();
    time = (time - 4000) / 2000;
    axp_read(g_axp20_supply->master,POWER20_PEK_SET,&val);
    val &= 0xfc;
    val |= (time & 0x03);
    return axp_write(g_axp20_supply->master,POWER20_PEK_SET,val);
}
EXPORT_SYMBOL_GPL(axp_set_pek_off_time);

int axp_set_ot_power_off_en(int en)
{
    uint8_t val; 

    CHECK_DRIVER();
    axp_read(g_axp20_supply->master, POWER20_HOTOVER_CTL, &val);
    val &= 0xfb;
    val |= ((en ? 1 : 0) << 2);
    return axp_write(g_axp20_supply->master, POWER20_HOTOVER_CTL, val);
}
EXPORT_SYMBOL_GPL(axp_set_ot_power_off_en);

int axp_get_battery_percent(void)
{
    CHECK_DRIVER();
    return g_axp20_supply->aml_charger.rest_vol;
}
EXPORT_SYMBOL_GPL(axp_get_battery_percent);

int axp_get_battery_voltage(void)
{
    CHECK_DRIVER();
    return g_axp20_supply->aml_charger.vbat;
}
EXPORT_SYMBOL_GPL(axp_get_battery_voltage);

int axp_get_battery_current(void)
{
    CHECK_DRIVER();
    return g_axp20_supply->aml_charger.ibat;
}
EXPORT_SYMBOL_GPL(axp_get_battery_current);

int axp_set_poweroff_voltage(int voltage)
{
    uint8_t val;

    CHECK_DRIVER();
    if (voltage > 3300 || voltage < 2600) {
        AXP_PMU_DBG("Invalid input of voltage:%d\n", voltage);
        return -EINVAL;
    }
    axp_read(g_axp20_supply->master, 0x31, &val);
    val &= ~(0x07);
    voltage = ((voltage - 2600) / 100) & 0x07;
    val |= voltage;
    axp_write(g_axp20_supply->master, 0x31, val);
    return 0;
}
EXPORT_SYMBOL_GPL(axp_set_poweroff_voltage);

static void axp_battery_para_init(struct axp20_supply *supply)
{
    if (axp_pmu_battery->pmu_usbvol_limit) {                            // limit VBUS voltage
        axp_set_usb_voltage_limit(axp_pmu_battery->pmu_usbvol);
    } else {
        axp_clr_bits(supply->master, AXP20_CHARGE_VBUS, (1<<6));       // not limit
    }

#if 0
    if(axp_pmu_battery->pmu_usbcur_limit) {                             // limit USB current
        axp_charger_set_usbcur_limit_extern(axp_pmu_battery->pmu_usbcur);
    } else {
        axp_charger_set_usbcur_limit_extern(0);
    }
#endif

    axp_set_noe_delay(axp_pmu_battery->pmu_pwrnoe_time);                // set n_oe delay
    axp_set_pek_on_time(axp_pmu_battery->pmu_pekon_time);               // set pek_on time
    axp_set_pek_long_time(axp_pmu_battery->pmu_peklong_time);           // set pek long press time
    axp_set_pek_off_en(axp_pmu_battery->pmu_pekoff_en);                 // set pek long press shut down system en
    axp_set_pwrok_delay(axp_pmu_battery->pmu_pwrok_time);               // set pwr button ok delay
    axp_set_pek_off_time(axp_pmu_battery->pmu_pekoff_time);             // set pek long press shut down 
    axp_set_charge_current(axp_pmu_battery->pmu_init_chgcur);	        // set charging current
    axp_set_ot_power_off_en(axp_pmu_battery->pmu_intotp_en);            // set pmu_intotp_en en
    axp_set_poweroff_voltage(axp_pmu_battery->pmu_pwroff_vol);          // set pmu_pwroff_vol value
}

/*------------------------ sysfs for debug ----------------------------*/
static ssize_t chgen_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val;

    axp_read(supply->master, AXP20_CHARGE_CONTROL1, &val);
    return sprintf(buf, "%d\n", val >> 7);
}

static ssize_t chgen_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int var;

    var = simple_strtoul(buf, NULL, 10);
    if(var){
        axp_set_bits(supply->master, AXP20_CHARGE_CONTROL1, 0x80);
    } else{
        axp_clr_bits(supply->master, AXP20_CHARGE_CONTROL1, 0x80);
    }
    return count;
}

static ssize_t chgmicrovol_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val;
    int chgvol = 0;

    axp_read(supply->master, AXP20_CHARGE_CONTROL1, &val);
    switch ((val >> 5) & 0x03){
    case 0: chgvol = 4100000; break;
    case 1: chgvol = 4150000; break;
    case 2: chgvol = 4200000; break;
    case 3: chgvol = 4360000; break;
    }
    return sprintf(buf, "%d\n", chgvol);
}

static ssize_t chgmicrovol_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int var;
    uint8_t tmp, val;

    var = simple_strtoul(buf, NULL, 10);
    switch(var){
    case 4100000:tmp = 0; break;
    case 4150000:tmp = 1; break;
    case 4200000:tmp = 2; break;
    case 4360000:tmp = 3; break;
    default:  tmp = 4; break;
    }
    if(tmp < 4){
        axp_read(supply->master, AXP20_CHARGE_CONTROL1, &val);
        val &= 0x9F;
        val |= tmp << 5;
        axp_write(supply->master, AXP20_CHARGE_CONTROL1, val);
    }
    return count;
}

static ssize_t chgintmicrocur_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val;
    int chgcur = 0;

    axp_read(supply->master, AXP20_CHARGE_CONTROL1, &val);
    chgcur = (val & 0x0F) * 100000 +300000;
    return sprintf(buf, "%d\n", chgcur);
}

static ssize_t chgintmicrocur_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int var;
    uint8_t val,tmp;

    var = simple_strtoul(buf, NULL, 10);
    if(var >= 300000 && var <= 1800000){
        tmp = (var -200001)/100000;
        axp_read(supply->master, AXP20_CHARGE_CONTROL1, &val);
        val &= 0xF0;
        val |= tmp;
        axp_write(supply->master, AXP20_CHARGE_CONTROL1, val);
    }
    return count;
}

static ssize_t chgendcur_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val;
    int chgend = 0;

    axp_read(supply->master, AXP20_CHARGE_CONTROL1, &val);
    chgend = ((val >> 4)& 0x01)? 15 : 10;
    return sprintf(buf, "%d\n", chgend);
}

static ssize_t chgendcur_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int var;

    var = simple_strtoul(buf, NULL, 10);
    if(var == 10 ){
        axp_clr_bits(supply->master ,AXP20_CHARGE_CONTROL1,0x10);
    } else if (var == 15){
        axp_set_bits(supply->master ,AXP20_CHARGE_CONTROL1,0x10);
    }
    return count;
}

static ssize_t chgpretimemin_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val;
    int chgpretime = 0;

    axp_read(supply->master,AXP20_CHARGE_CONTROL2, &val);
    chgpretime = (val >> 6) * 10 +40;
    return sprintf(buf, "%d\n", chgpretime);
}

static ssize_t chgpretimemin_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int     var;
    uint8_t tmp,val;

    var = simple_strtoul(buf, NULL, 10);
    if(var >= 40 && var <= 70){
        tmp = (var - 40)/10;
        axp_read(supply->master,AXP20_CHARGE_CONTROL2,&val);
        val &= 0x3F;
        val |= (tmp << 6);
        axp_write(supply->master,AXP20_CHARGE_CONTROL2,val);
    }
    return count;
}

static ssize_t chgcsttimemin_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val;
    int chgcsttime = 0;

    axp_read(supply->master,AXP20_CHARGE_CONTROL2, &val);
    chgcsttime = (val & 0x03) *120 + 360;
    return sprintf(buf, "%d\n", chgcsttime);
}

static ssize_t chgcsttimemin_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int     var;
    uint8_t tmp,val;

    var = simple_strtoul(buf, NULL, 10);
    if(var >= 360 && var <= 720){
        tmp = (var - 360)/120;
        axp_read(supply->master,AXP20_CHARGE_CONTROL2,&val);
        val &= 0xFC;
        val |= tmp;
        axp_write(supply->master,AXP20_CHARGE_CONTROL2,val);
    }
    return count;
}

static ssize_t adcfreq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val;
    int sample_time = 0;

    axp_read(supply->master, AXP20_ADC_CONTROL3, &val);
    switch ((val >> 6) & 0x03){
    case 0: sample_time = 25;  break;
    case 1: sample_time = 50;  break;
    case 2: sample_time = 100; break;
    case 3: sample_time = 200; break;
    default:break;
    }
    return sprintf(buf, "%d\n", sample_time);
}

static ssize_t adcfreq_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int     var;
    uint8_t val;

    var = simple_strtoul(buf, NULL, 10);
    axp_read(supply->master, AXP20_ADC_CONTROL3, &val);
    val &= ~(3 << 6);
    switch (var / 25) {
    case 1: 
        break;
    case 2: 
        val |= 1 << 6;
        break;
    case 4: 
        val |= 2 << 6;
        break;
    case 8: 
        val |= 3 << 6;
        break;
    default: 
        break;
    }
    axp_write(supply->master, AXP20_ADC_CONTROL3, val);
    return count;
}


static ssize_t vholden_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val;

    axp_read(supply->master,AXP20_CHARGE_VBUS, &val);
    val = (val>>6) & 0x01;
    return sprintf(buf, "%d\n",val);
}

static ssize_t vholden_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int var;

    var = simple_strtoul(buf, NULL, 10);
    if (var) {
        axp_set_bits(supply->master, AXP20_CHARGE_VBUS, 0x40);
    } else {
        axp_clr_bits(supply->master, AXP20_CHARGE_VBUS, 0x40);
    }

    return count;
}

static ssize_t vhold_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val;
    int     vhold;

    axp_read(supply->master,AXP20_CHARGE_VBUS, &val);
    vhold = ((val >> 3) & 0x07) * 100000 + 4000000;
    return sprintf(buf, "%d\n",vhold);
}

static ssize_t vhold_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int     var;
    uint8_t val,tmp;

    var = simple_strtoul(buf, NULL, 10);
    if(var >= 4000000 && var <=4700000){
        tmp = (var - 4000000)/100000;
        axp_read(supply->master, AXP20_CHARGE_VBUS,&val);
        val &= 0xC7;
        val |= tmp << 3;
        axp_write(supply->master, AXP20_CHARGE_VBUS,val);
    }
    return count;
}

static ssize_t iholden_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val;

    axp_read(supply->master,AXP20_CHARGE_VBUS, &val);
    return sprintf(buf, "%d\n",((val & 0x03) == 0x03)?0:1);
}

static ssize_t iholden_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int     var;

    var = simple_strtoul(buf, NULL, 10);
    if (var) {
        axp_clr_bits(supply->master, AXP20_CHARGE_VBUS, 0x01);
    } else {
        axp_set_bits(supply->master, AXP20_CHARGE_VBUS, 0x03);
    }

    return count;
}

static ssize_t ihold_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    uint8_t val,tmp;
    int     ihold;

    axp_read(supply->master,AXP20_CHARGE_VBUS, &val);
    tmp = (val) & 0x03;
    switch(tmp){
    case 0: ihold = 900000;break;
    case 1: ihold = 500000;break;
    case 2: ihold = 100000;break;
    default: ihold = 0;break;
    }
    return sprintf(buf, "%d\n",ihold);
}

static ssize_t ihold_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    int     var;

    var = simple_strtoul(buf, NULL, 10);
    if (var == 900000) {
        axp_clr_bits(supply->master, AXP20_CHARGE_VBUS, 0x03);
    } else if (var == 500000) {
        axp_clr_bits(supply->master, AXP20_CHARGE_VBUS, 0x02);
        axp_set_bits(supply->master, AXP20_CHARGE_VBUS, 0x01);
    } else if (var == 100000) {
        axp_clr_bits(supply->master, AXP20_CHARGE_VBUS, 0x01);
        axp_set_bits(supply->master, AXP20_CHARGE_VBUS, 0x02);
    }

    return count;
}

static ssize_t clear_rtc_mem_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return 0;
}

static ssize_t clear_rtc_mem_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    aml_write_rtc_mem_reg(0, 0);
    axp_power_off();
    return count;
}

static ssize_t dbg_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    struct aml_pmu_api  *api;
    
    api = aml_pmu_get_api();
    if (api && api->pmu_format_dbg_buffer) {
        return api->pmu_format_dbg_buffer(&supply->aml_charger, buf);
    } else {
        return sprintf(buf, "api not found, please insert pmu.ko\n"); 
    }
}

static ssize_t dbg_info_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return 0;                                           /* nothing to do        */
}

static ssize_t battery_para_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply *battery = dev_get_drvdata(dev);
    struct axp20_supply *supply  = container_of(battery, struct axp20_supply, batt);
    struct aml_charger  *charger = &supply->aml_charger;
    int    i = 0; 
    int    size;

    size = sprintf(buf, "\n i,      ocv,    charge,  discharge,\n");
    for (i = 0; i < 16; i++) {
        size += sprintf(buf + size, "%2d,     %4d,       %3d,        %3d,\n",
                        i, 
                        axp_pmu_battery->pmu_bat_curve[i].ocv,
                        axp_pmu_battery->pmu_bat_curve[i].charge_percent,
                        axp_pmu_battery->pmu_bat_curve[i].discharge_percent);
    }
    size += sprintf(buf + size, "\nBattery capability:%4d@3700mAh, RDC:%3d mohm\n", 
                                axp_pmu_battery->pmu_battery_cap, 
                                axp_pmu_battery->pmu_battery_rdc);
    size += sprintf(buf + size, "Charging efficiency:%3d%%, capability now:%3d%%\n", 
                                axp_pmu_battery->pmu_charge_efficiency,
                                charger->rest_vol);
    size += sprintf(buf + size, "ocv_empty:%4d, ocv_full:%4d\n\n",
                                charger->ocv_empty, 
                                charger->ocv_full);
    return size;
}

static ssize_t battery_para_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return 0;                                           /* nothing to do        */    
}

static ssize_t report_delay_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aml_pmu_api *api = aml_pmu_get_api();
    if (api && api->pmu_get_report_delay) {
        return sprintf(buf, "report_delay = %d\n", api->pmu_get_report_delay()); 
    } else {
        return sprintf(buf, "error, api not found\n");
    }
}

static ssize_t report_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct aml_pmu_api *api = aml_pmu_get_api();
    uint32_t tmp = simple_strtoul(buf, NULL, 10);

    if (tmp > 200) {
        AXP_PMU_DBG("input too large, failed to set report_delay\n");    
        return count;
    }
    if (api && api->pmu_set_report_delay) {
        api->pmu_set_report_delay(tmp);
    } else {
        AXP_PMU_DBG("API not found\n");
    }
    return count;
}

static ssize_t driver_version_show (struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "Amlogic AXP202 driver version is %s, build time:%s\n", 
                   AXP_DRIVER_VERSION, init_uts_ns.name.version);    
}
static ssize_t driver_version_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return 0;    
}

static int find_idx(int start, int target, int step, int length)
{
    int i = 0;
    do {
        if (start >= target) {
            break;    
        }    
        start += step;
        i++;
    } while (i < length);
    return i;
}

void axp20_dcdc_voltage(int dcdc, int target) 
{
    int idx_to = 0, idx_cur = 0;
    int addr = 0, val, mask = 0;
    if (dcdc == 2) {
        idx_to = find_idx(700, target, 25, 64);
        addr   = 0x23; 
        mask   = 0x3f;
    } else if (dcdc == 3) {
        idx_to = find_idx(700, target, 25, 128);
        addr   = 0x27; 
        mask   = 0x7f;
    }   
    axp_read(g_axp20_supply->master, addr, (uint8_t *)&val);
    idx_cur = val & mask;
    while (idx_cur != idx_to) {
        if (idx_cur < idx_to) {                                 // adjust to target voltage step by step
            idx_cur++;
        } else {
            idx_cur--;
        }
        val &= ~mask;
        val |= (idx_cur);
        axp_write(g_axp20_supply->master, addr, val);
        udelay(100);                                            // atleast delay 100uS
    }
}

static ssize_t aml_pmu_vddao_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    uint8_t data;
    axp_read(g_axp20_supply->master, 0x2f, &data);

    return sprintf(buf, "Voltage of VDD_AO = %4dmV\n", 700 + 25 * (data & 0x7f));
}

static ssize_t aml_pmu_vddao_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    uint32_t data = simple_strtoul(buf, NULL, 10);
    if (data > 3500 || data < 700) {
        AXP_PMU_DBG("Invalid input value = %d\n", data);
        return -1;
    }
    AXP_PMU_DBG("Set VDD_AO to %4d mV\n", data);
    axp20_dcdc_voltage(3, data);
    return count; 
}

static struct device_attribute axp_supply_attrs[] = {
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
    AXP_CHG_ATTR(dbg_info),
    AXP_CHG_ATTR(battery_para),
    AXP_CHG_ATTR(report_delay),
    AXP_CHG_ATTR(driver_version),
    AXP_CHG_ATTR(clear_rtc_mem),
    AXP_CHG_ATTR(aml_pmu_vddao),
};

int axp_charger_create_attrs(struct power_supply *psy)
{
    int j, ret;
    for (j = 0; j < ARRAY_SIZE(axp_supply_attrs); j++) {
        ret = device_create_file(psy->dev, &axp_supply_attrs[j]);
        if (ret) {
            goto sysfs_failed;
        }
    }
    return ret;

sysfs_failed:
    while (j--) {
        device_remove_file(psy->dev, &axp_supply_attrs[j]);
    }
    return ret;
}

#ifdef CONFIG_AMLOGIC_USB
int axp202_otg_change(struct notifier_block *nb, unsigned long value, void *pdata)
{
    /*
     * right now nothing todo
     */
    return 0;
}

int axp202_usb_charger(struct notifier_block *nb, unsigned long value, void *pdata)
{
    if (!g_axp20_supply) {
        AXP_PMU_DBG("%s, driver is not ready, do it later\n", __func__);
        axp202_charger_job.flag  = 1;
        axp202_charger_job.value = value;
        return 0;
    }
    switch (value) {
        case USB_BC_MODE_DISCONNECT:                                        // disconnect
        case USB_BC_MODE_SDP:                                               // pc
            if (axp_pmu_battery && axp_pmu_battery->pmu_usbcur_limit) {     // limit usb current
                axp_charger_set_usbcur_limit_extern(axp_pmu_battery->pmu_usbcur);
            }
            break;

        case USB_BC_MODE_DCP:                                               // charger
        case USB_BC_MODE_CDP:                                               // PC + charger
            if (axp_pmu_battery) {                                          // limit usb current
                axp_charger_set_usbcur_limit_extern(-1);                    // not limit usb current
            }
            break;

        default:
            break;
    }
    return 0;
}
#endif

/*---------------- battery capacity calculate ---------------------------*/

static inline int axp_vbat_to_mV(uint16_t reg)
{
	return (reg) * 1100 / 1000;
}

static inline int axp_vdc_to_mV(uint16_t reg)
{
	return (reg) * 1700 / 1000;
}

static inline int axp_ibat_to_mA(uint16_t reg)
{
	return (reg) * 500 / 1000;
}

static inline int axp_iac_to_mA(uint16_t reg)
{
	return (reg) * 625 / 1000;
}

static inline int axp_iusb_to_mA(uint16_t reg)
{
	return (reg) * 375 / 1000;
}

/*  AXP  */
#define AXP_STATUS_ACUSBSH			    (1<< 1)
#define AXP_STATUS_BATCURDIR		    (1<< 2)
#define AXP_STATUS_USBLAVHO			    (1<< 3)
#define AXP_STATUS_USBVA			    (1<< 4)
#define AXP_STATUS_USBEN			    (1<< 5)
#define AXP_STATUS_ACVA				    (1<< 6)
#define AXP_STATUS_ACEN				    (1<< 7)

#define AXP_STATUS_CHACURLOEXP		    (1<<10)
#define AXP_STATUS_BATINACT			    (1<<11)

#define AXP_STATUS_BATEN			    (1<<13)
#define AXP_STATUS_INCHAR			    (1<<14)
#define AXP_STATUS_ICTEMOV			    (1<<15)

#define AXP_CHARGE_STATUS				POWER20_STATUS

#define AXP_CHARGE_CONTROL1				POWER20_CHARGE1
#define AXP_CHARGE_CONTROL2				POWER20_CHARGE2

#define AXP_ADC_CONTROL1				POWER20_ADC_EN1
#define AXP_ADC_BATVOL_ENABLE			(1 << 7)
#define AXP_ADC_BATCUR_ENABLE			(1 << 6)
#define AXP_ADC_DCINVOL_ENABLE			(1 << 5)
#define AXP_ADC_DCINCUR_ENABLE			(1 << 4)
#define AXP_ADC_USBVOL_ENABLE			(1 << 3)
#define AXP_ADC_USBCUR_ENABLE			(1 << 2)
#define AXP_ADC_APSVOL_ENABLE			(1 << 1)
#define AXP_ADC_TSVOL_ENABLE			(1 << 0)
#define AXP_ADC_CONTROL3				POWER20_ADC_SPEED

#define AXP_VACH_RES					POWER20_ACIN_VOL_H8
#define AXP_VACL_RES					POWER20_ACIN_VOL_L4
#define AXP_IACH_RES					POWER20_ACIN_CUR_H8
#define AXP_IACL_RES					POWER20_ACIN_CUR_L4
#define AXP_VUSBH_RES					POWER20_VBUS_VOL_H8
#define AXP_VUSBL_RES					POWER20_VBUS_VOL_L4
#define AXP_IUSBH_RES					POWER20_VBUS_CUR_H8
#define AXP_IUSBL_RES					POWER20_VBUS_CUR_L4

#define AXP_VBATH_RES					POWER20_BAT_AVERVOL_H8
#define AXP_VBATL_RES					POWER20_BAT_AVERVOL_L4
#define AXP_ICHARH_RES					POWER20_BAT_AVERCHGCUR_H8
#define AXP_ICHARL_RES					POWER20_BAT_AVERCHGCUR_L5
#define AXP_IDISCHARH_RES				POWER20_BAT_AVERDISCHGCUR_H8
#define AXP_IDISCHARL_RES				POWER20_BAT_AVERDISCHGCUR_L5			//Elvis fool

#define AXP_COULOMB_CONTROL				POWER20_COULOMB_CTL

#define AXP_CCHAR3_RES					POWER20_BAT_CHGCOULOMB3
#define AXP_CCHAR2_RES					POWER20_BAT_CHGCOULOMB2
#define AXP_CCHAR1_RES					POWER20_BAT_CHGCOULOMB1
#define AXP_CCHAR0_RES					POWER20_BAT_CHGCOULOMB0
#define AXP_CDISCHAR3_RES				POWER20_BAT_DISCHGCOULOMB3
#define AXP_CDISCHAR2_RES				POWER20_BAT_DISCHGCOULOMB2
#define AXP_CDISCHAR1_RES				POWER20_BAT_DISCHGCOULOMB1
#define AXP_CDISCHAR0_RES				POWER20_BAT_DISCHGCOULOMB0

#define AXP_IC_TYPE						POWER20_IC_TYPE

#define AXP_CAP							(0xB9)
#define AXP_RDC_BUFFER0					(0xBA)
#define AXP_RDC_BUFFER1					(0xBB)
#define AXP_OCV_BUFFER1					(0xBD)

#define AXP_HOTOVER_CTL					POWER20_HOTOVER_CTL
#define AXP_CHARGE_VBUS					POWER20_IPS_SET
#define AXP_APS_WARNING1				POWER20_APS_WARNING1
#define AXP_APS_WARNING2				POWER20_APS_WARNING2
#define AXP_TIMER_CTL					POWER20_TIMER_CTL
#define AXP_PEK_SET						POWER20_PEK_SET
#define AXP_DATA_NUM					12

static int axp_get_freq(void)
{
	int  ret = 25;
	uint8_t  temp;
    axp_read(g_axp20_supply->master, AXP_ADC_CONTROL3,&temp);
	temp &= 0xc0;
	switch(temp >> 6){
		case 0:	ret = 25; break;
		case 1:	ret = 50; break;
		case 2:	ret = 100;break;
		case 3:	ret = 200;break;
		default:break;
	}
	return ret;
}

int axp20_get_coulomb(struct aml_charger *charger)
{
    struct  axp20_supply *supply = (struct axp20_supply*)container_of(charger, struct axp20_supply, aml_charger);
    uint8_t temp[8];
    int64_t rValue1, rValue2, rValue;
    int     m;

    m = axp_get_freq() * 480;
	axp_reads(supply->master, AXP_CCHAR3_RES,8,temp);
	rValue1 = ((temp[0] << 24) + (temp[1] << 16) + (temp[2] << 8) + temp[3]);
	rValue2 = ((temp[4] << 24) + (temp[5] << 16) + (temp[6] << 8) + temp[7]);

    rValue  = rValue1 * 4369;
    do_div(rValue, m);
    charger->charge_cc = (int)rValue;

    rValue = rValue2 * 4369;
    do_div(rValue, m);
    charger->discharge_cc = (int)rValue;

    return 0;
}

int axp20_clear_coulomb(struct aml_charger *charger)
{
    struct  axp20_supply *supply = (struct axp20_supply*)container_of(charger, struct axp20_supply, aml_charger);
    uint8_t val;

    axp_read(supply->master, 0xb8, &val);
    val |= 0x20;
    axp_write(supply->master, 0xb8, val);
    return 0;
}

void axp_read_adc(struct axp20_supply *supply, struct axp_adc_res *adc)
{
	uint8_t tmp[8];

	axp_reads(supply->master, AXP_VACH_RES, 8, tmp);
	adc->vac_res  = ((uint16_t) tmp[0] << 4) | (tmp[1] & 0x0f);
	adc->iac_res  = ((uint16_t) tmp[2] << 4) | (tmp[3] & 0x0f);
	adc->vusb_res = ((uint16_t) tmp[4] << 4) | (tmp[5] & 0x0f);
	adc->iusb_res = ((uint16_t) tmp[6] << 4) | (tmp[7] & 0x0f);

	axp_reads(supply->master, AXP_VBATH_RES, 6, tmp);
	adc->vbat_res     = ((uint16_t) tmp[0] << 4) | (tmp[1] & 0x0f);
	adc->ichar_res    = ((uint16_t) tmp[2] << 4) | (tmp[3] & 0x0f);
	adc->idischar_res = ((uint16_t) tmp[4] << 5) | (tmp[5] & 0x1f);
}

#if 0
static void reset_charger(struct axp20_supply *supply)
{
	uint8_t val;

	AXP_PMU_DBG("reset_charger\n");
	axp_read(supply->master, AXP20_CHARGE_CONTROL1, &val);
	val &= ~(1 << 7);
	axp_write(supply->master, AXP20_CHARGE_CONTROL1, val);
	mdelay(100);
	val |= 1 << 7;
	axp_write(supply->master, AXP20_CHARGE_CONTROL1, val);
}
#endif

int axp20_update_status(struct aml_charger *charger)
{
    struct  axp20_supply *supply = (struct axp20_supply*)container_of(charger, struct axp20_supply, aml_charger);
	uint8_t        val[2];
	uint16_t       tmp;
    static uint8_t usb_cur_limit_canceled = 0;

    axp_charger_update(supply);
	axp_reads(supply->master, AXP_CHARGE_STATUS, 2, val);
	tmp = (val[1] << 8 )+ val[0];
	charger->bat_det 				= (tmp & AXP_STATUS_BATEN)      ? 1 : 0;
    charger->fault                  = tmp;
	charger->usb_valid 				= (tmp & AXP_STATUS_USBVA)      ? 1 : 0;
	charger->dcin_valid 			= (tmp & AXP_STATUS_ACVA)       ? 1 : 0;
	charger->ext_valid              = charger->dcin_valid | (charger->usb_valid << 1);
	charger->charge_status 	        = (tmp & AXP_STATUS_BATCURDIR) ? CHARGER_CHARGING : CHARGER_DISCHARGING;
    /*
     * when usb is connected && board config tell us do not limit current
     */
    if (charger->usb_valid && !axp_pmu_battery->pmu_usbcur_limit && !usb_cur_limit_canceled) {
        axp_set_bits(supply->master, AXP20_CHARGE_VBUS, 0x03);
        usb_cur_limit_canceled = 1;
    } else if (!charger->usb_valid) {
        usb_cur_limit_canceled = 0;    
    }
    return 0;
}

void axp_charger_update(struct axp20_supply *supply)
{
    struct aml_charger *charger = &supply->aml_charger;
    uint8_t v[2] = {};

	axp_read_adc(supply, &supply->adc);
	charger->vbat   = axp_vbat_to_mV(supply->adc.vbat_res);
	charger->ibat   = ABS(axp_ibat_to_mA(supply->adc.ichar_res) - 
                          axp_ibat_to_mA(supply->adc.idischar_res));
    charger->v_dcin = axp_vdc_to_mV(supply->adc.vac_res);
    charger->i_dcin = axp_iac_to_mA(supply->adc.iac_res);
    charger->v_usb  = axp_vdc_to_mV(supply->adc.vusb_res);
    charger->i_usb  = axp_iusb_to_mA(supply->adc.iusb_res);
    axp_reads(supply->master, AXP_OCV_BUFFER0, 2, v);
    charger->ocv    = ((v[0] << 4) + (v[1] & 0x0f)) * 11 /10 ;
}

#if AML_PMU_CALL_BACK_DEMO
int axp_call_back1(struct aml_charger *charger, void *pdata)
{
    char *str = (char *)pdata;
    AXP_PMU_DBG("%s, %s, battery percent now:%d\n", __func__, str, charger->rest_vol);
    return 0;
}

int axp_call_back2(struct aml_charger *charger, void *pdata)
{
    char *str = (char *)pdata;
    struct aml_pmu_driver *driver = aml_pmu_get_driver();
    AXP_PMU_DBG("%s, %s, battery voltage:%d, cuurent:%d\n", 
                __func__, str, charger->vbat, charger->ibat);
    /*
     * just for demo:
     * when extern power is removed, set usb current limit to 
     * 900mA to avoid large current sink form source
     */
    if (driver && !charger->ext_valid) {
        if (driver->pmu_set_usb_current_limit) {
            driver->pmu_set_usb_current_limit(900); 
        }
    }
    return 0;
}
#endif  /* AML_PMU_CALL_BACK_DEMO */

static void axp_charging_monitor(struct work_struct *work)
{
    struct axp20_supply *supply;
    struct aml_charger  *charger;
    int32_t pre_rest_cap;
    uint8_t pre_chg_status;
    struct aml_pmu_api *api;
    static bool api_flag = false;

    supply  = container_of(work, struct axp20_supply, work.work);
    charger = &supply->aml_charger;
    api     = aml_pmu_get_api();
    if (!api) {
        schedule_delayed_work(&supply->work, supply->interval);
        return ;                                                // KO is not ready
    }
    if (api && !api_flag) {
        api_flag = true;
        if (api->pmu_probe_process) {
            api->pmu_probe_process(charger, axp_pmu_battery);    
        }
    } 
    pre_chg_status = charger->ext_valid;
    pre_rest_cap   = charger->rest_vol;

    api->pmu_update_battery_capacity(charger, axp_pmu_battery);

    if(!charger->ext_valid){                                    // clear charge LED when extern power is removed
        axp_clr_bits(supply->master, POWER20_OFF_CTL, 0x38);
    }
    if((charger->rest_vol - pre_rest_cap) || (pre_chg_status != charger->ext_valid) || charger->resume){
        AXP_PMU_DBG("battery vol change: %d->%d \n", pre_rest_cap, charger->rest_vol);
        if (unlikely(charger->resume)) {
            charger->resume = 0;
        }
        power_supply_changed(&supply->batt);
    #ifdef CONFIG_HAS_EARLYSUSPEND
        if (in_early_suspend && (pre_chg_status != charger->ext_valid)) {
            wake_lock(&axp202_lock);
            AXP_PMU_DBG("%s, usb power status changed in early suspend, wake up now\n", __func__);
            input_report_key(powerkeydev, KEY_POWER, 1);                        // assume power key pressed 
            input_sync(powerkeydev);
        }
    #endif
    }

    schedule_delayed_work(&supply->work, supply->interval);
}



#if defined CONFIG_HAS_EARLYSUSPEND
static int early_power_status = 0;
static void axp_earlysuspend(struct early_suspend *h)
{
    struct axp20_supply *supply = (struct axp20_supply *)h->param;

#if defined (CONFIG_AXP_CHGCHANGE)
    axp_set_charge_current(axp_pmu_battery->pmu_suspend_chgcur);    //set charging current
#endif
    if (axp_pmu_battery) {
        early_power_status = supply->aml_charger.ext_valid;    
    }
    in_early_suspend = 1;
}

static void axp_lateresume(struct early_suspend *h)
{
    struct axp20_supply *supply = (struct axp20_supply*)h->param;

    schedule_work(&supply->work.work); 

#if defined (CONFIG_AXP_CHGCHANGE)
    axp_set_charge_current(axp_pmu_battery->pmu_resume_chgcur);     //set charging current
#endif
    if (axp_pmu_battery) {
        early_power_status = supply->aml_charger.ext_valid;
        input_report_key(powerkeydev, KEY_POWER, 0);                    // cancel power key 
        input_sync(powerkeydev);
    }
    in_early_suspend = 0;
    wake_unlock(&axp202_lock);
}
#endif

static struct aml_pmu_driver axp20_pmu_driver = {
    .name                      = "axp20",
    .pmu_get_coulomb           = axp20_get_coulomb,
    .pmu_clear_coulomb         = axp20_clear_coulomb,
    .pmu_update_status         = axp20_update_status,
    .pmu_set_rdc               = axp20_set_rdc,
    .pmu_set_gpio              = axp_gpio_set_value,
    .pmu_get_gpio              = axp_gpio_get_value,
    .pmu_reg_read              = axp20_reg_read,
    .pmu_reg_write             = axp20_reg_write,
    .pmu_reg_reads             = axp20_reg_reads,
    .pmu_reg_writes            = axp20_reg_writes,
    .pmu_set_bits              = axp20_set_bits, 
    .pmu_set_usb_current_limit = axp_charger_set_usbcur_limit_extern,
    .pmu_set_charge_current    = axp_set_charge_current,
    .pmu_power_off             = axp_power_off, 
};

static int axp_battery_probe(struct platform_device *pdev)
{
	struct axp20_supply *supply;
    struct aml_charger  *charger;
	struct axp_supply_init_data *pdata = pdev->dev.platform_data;
	int      ret;
	uint8_t  tmp, val;
	uint32_t tmp2;

    AXP_PMU_DBG("call %s, in\n", __func__);
	powerkeydev = input_allocate_device();
	if (!powerkeydev) {
		kfree(powerkeydev);
		return -ENODEV;
	}

	powerkeydev->name       = pdev->name;
	powerkeydev->phys       = "m1kbd/input2";
	powerkeydev->id.bustype = BUS_HOST;
	powerkeydev->id.vendor  = 0x0001;
	powerkeydev->id.product = 0x0001;
	powerkeydev->id.version = 0x0100;
	powerkeydev->open       = NULL;
	powerkeydev->close      = NULL;
	powerkeydev->dev.parent = &pdev->dev;

	set_bit(EV_KEY, powerkeydev->evbit);
	set_bit(EV_REL, powerkeydev->evbit);
	set_bit(KEY_POWER, powerkeydev->keybit);

	ret = input_register_device(powerkeydev);
	if(ret) {
	    AXP_PMU_DBG("Unable to Register the power key\n");
	}

    if (pdata == NULL) {
        return -EINVAL;    
    }

#ifdef CONFIG_UBOOT_BATTERY_PARAMETERS 
    if (get_uboot_battery_para_status() == UBOOT_BATTERY_PARA_SUCCESS) {
        AXP_PMU_DBG("Battery parameters has got from uboot\n");
        axp_pmu_battery = get_uboot_battery_para();
    } else {
        AXP_PMU_DBG("Battery parameters has not inited, we use default board config parameters\n");
        axp_pmu_battery = pdata->board_battery;
    }
#else
    axp_pmu_battery = pdata->board_battery;
    AXP_PMU_DBG("use BSP battery parameters\n");
#endif

    /*
     * initialize parameters for supply 
     */
	supply = kzalloc(sizeof(*supply), GFP_KERNEL);
	if (supply == NULL) {
	    return -ENOMEM;
    }
    supply->battery_info = kzalloc(sizeof(struct power_supply_info), GFP_KERNEL);
    if (supply->battery_info == NULL) {
        kfree(supply);
        return -ENOMEM;    
    }
	supply->master = pdev->dev.parent;
    charger        = &supply->aml_charger;
    for (tmp2 = 1; tmp2 < 16; tmp2++) {
        if (!charger->ocv_empty && axp_pmu_battery->pmu_bat_curve[tmp2].discharge_percent > 0) {
            charger->ocv_empty = axp_pmu_battery->pmu_bat_curve[tmp2-1].ocv;
        }
        if (!charger->ocv_full && axp_pmu_battery->pmu_bat_curve[tmp2].discharge_percent == 100) {
            charger->ocv_full = axp_pmu_battery->pmu_bat_curve[tmp2].ocv;    
        }
    }
    aml_pmu_register_driver(&axp20_pmu_driver);

    supply->battery_info->technology         = axp_pmu_battery->pmu_battery_technology;
    supply->battery_info->voltage_max_design = axp_pmu_battery->pmu_init_chgvol;
    supply->battery_info->energy_full_design = axp_pmu_battery->pmu_battery_cap;
    supply->battery_info->voltage_min_design = charger->ocv_empty * 1000;
    supply->battery_info->use_for_apm        = 1;
    supply->battery_info->name               = axp_pmu_battery->pmu_battery_name;

    supply->led_control      = pdata->led_control;
    charger->soft_limit_to99 = pdata->soft_limit_to99;
	charger->coulomb_type    = COULOMB_BOTH;
	g_axp20_supply = supply;

    ret = axp_battery_first_init(supply);
    if (ret) {
        goto err_ps_register;
    }

    axp_battery_setup_psy(supply);                                     // regist power supply interface
    ret = power_supply_register(&pdev->dev, &supply->batt);
    if (ret) {
        goto err_ps_register;
    }
    ret = power_supply_register(&pdev->dev, &supply->ac);
    if (ret){
        power_supply_unregister(&supply->batt);
        goto err_ps_register;
    }
    ret = power_supply_register(&pdev->dev, &supply->usb);
    if (ret){
        power_supply_unregister(&supply->ac);
        power_supply_unregister(&supply->batt);
        goto err_ps_register;
    }

    ret = axp_charger_create_attrs(&supply->batt);
    if(ret){
        return ret;
    }

    platform_set_drvdata(pdev, supply);
    axp_battery_para_init(supply);
    axp_write(supply->master, AXP20_APS_WARNING1, 0x7A);               // 3.555V, warning to system	
    axp20_update_status(charger);

    if (charger->bat_det == 0) {                                        // no battery connect 
        AXP_PMU_DBG("%s, not dectected battery\n", __func__);
	}

	axp_set_bits(supply->master,0xB8,0x80);
    axp_set_bits(supply->master, 0xB9, 0x80);
	axp_clr_bits(supply->master,0xBA,0x80);

    axp20_set_rdc(axp_pmu_battery->pmu_battery_rdc);
    mdelay(500);

    supply->interval = msecs_to_jiffies(2 * 1000);
    INIT_DELAYED_WORK(&supply->work, axp_charging_monitor);
    schedule_delayed_work(&supply->work, supply->interval);
#ifdef CONFIG_AMLOGIC_USB
    if (axp202_charger_job.flag) {     // do later job for usb charger detect
        axp202_usb_charger(NULL, axp202_charger_job.value, NULL);
        axp202_charger_job.flag = 0;
    }
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
    axp_early_suspend.suspend = axp_earlysuspend;
    axp_early_suspend.resume  = axp_lateresume;
    axp_early_suspend.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 2;
    axp_early_suspend.param   = supply;
    register_early_suspend(&axp_early_suspend);
    wake_lock_init(&axp202_lock, WAKE_LOCK_SUSPEND, "axp202");
#endif

    /* axp202 ntc battery low / high temperature alarm function */
    if (axp_pmu_battery->pmu_ntc_enable) {
#define TS_CURRENT_REG 		0x84
        axp_read(supply->master,TS_CURRENT_REG, &val);
        tmp = axp_pmu_battery->pmu_ntc_ts_current << 4;
        val &= ~(3<<4);	//val &= 0xcf;
        val |= tmp;		
        val &= ~(3<<0);
        val |= 0x1;		//TS pin:charge output current	
        axp_write(supply->master,TS_CURRENT_REG,val); 	//set TS pin output current

        if(axp_pmu_battery->pmu_ntc_lowtempvol < 0)
            axp_pmu_battery->pmu_ntc_lowtempvol = 0;
        else if(axp_pmu_battery->pmu_ntc_lowtempvol > 3264)
            axp_pmu_battery->pmu_ntc_lowtempvol = 3264;
        val = (axp_pmu_battery->pmu_ntc_lowtempvol*10)/(16*8);
        axp_write(supply->master,POWER20_VLTF_CHGSET,val);	//set battery low temperature threshold

        if(axp_pmu_battery->pmu_ntc_hightempvol < 0)
            axp_pmu_battery->pmu_ntc_hightempvol = 0;
        else if(axp_pmu_battery->pmu_ntc_hightempvol > 3264)
            axp_pmu_battery->pmu_ntc_hightempvol = 3264;
        val = (axp_pmu_battery->pmu_ntc_hightempvol*10)/(16*8);
        axp_write(supply->master,POWER20_VHTF_CHGSET,val);	//set battery high temperature threshold
    }

#if AML_PMU_CALL_BACK_DEMO
    aml_pmu_register_callback(axp_call_back1, "callback1...",  "demo 1");
    aml_pmu_register_callback(axp_call_back2, "callback2...",  "demo 2");
#endif /* AML_PMU_CALL_BACK_DEMO*/


    return ret;

err_ps_register:
    kfree(supply->battery_info);
    kfree(supply);
    input_unregister_device(powerkeydev);
    kfree(powerkeydev);

    AXP_PMU_DBG("call %s exited, ret:%d\n", __func__, ret);
    return ret;
}

static int axp_battery_remove(struct platform_device *dev)
{
    struct axp20_supply *supply = platform_get_drvdata(dev);

    cancel_delayed_work_sync(&supply->work);
    power_supply_unregister(&supply->usb);
    power_supply_unregister(&supply->ac);
    power_supply_unregister(&supply->batt);

    kfree(supply->battery_info);
    kfree(supply);
    input_unregister_device(powerkeydev);
    kfree(powerkeydev);
    aml_pmu_clear_driver();
#if AML_PMU_CALL_BACK_DEMO
    aml_pmu_unregister_callback("demo 1");
    aml_pmu_unregister_callback("demo 2");
#endif /* AML_PMU_CALL_BACK_DEMO*/

#ifdef CONFIG_HAS_EARLYSUSPEND
    wake_lock_destroy(&axp202_lock);
#endif

    return 0;
}

static int extern_led_ctrl = 0;

static int axp20_suspend(struct platform_device *dev, pm_message_t state)
{
    struct axp20_supply *supply  = platform_get_drvdata(dev);
    struct aml_charger  *charger = &supply->aml_charger;
    struct aml_pmu_api  *api;
    uint8_t irq_w[9];
    uint8_t tmp;
	cancel_delayed_work_sync(&supply->work);

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
    axp_writes(supply->master, POWER20_INTSTS1, 9, irq_w);
    
    /* close all irqs*/
    axp_read(supply->master, POWER20_OFF_CTL, &tmp);
    extern_led_ctrl = tmp & 0x08;
    if (extern_led_ctrl) {
    	axp_clr_bits(supply->master, POWER20_OFF_CTL, 0x08);
    }

    api = aml_pmu_get_api();
    if (api && api->pmu_suspend_process) {
        api->pmu_suspend_process(charger);
    }
#ifdef CONFIG_HAS_EARLYSUSPEND
    if (early_power_status != supply->aml_charger.ext_valid) {
        AXP_PMU_DBG("%s, power status changed, prev:%x, now:%x, exit suspend process\n",
                    __func__, early_power_status, supply->aml_charger.ext_valid);
        input_report_key(powerkeydev, KEY_POWER, 1);                    // assume power key pressed 
        input_sync(powerkeydev);
        return -1;
    }
    in_early_suspend = 0;
#endif
	
    return 0;
}

static int axp20_resume(struct platform_device *dev)
{
    struct axp20_supply *supply = platform_get_drvdata(dev);
    struct aml_charger  *charger = &supply->aml_charger;
    struct aml_pmu_api  *api;

    api = aml_pmu_get_api();
    if (api && api->pmu_resume_process) {
        api->pmu_resume_process(charger, axp_pmu_battery);
    }

    if (extern_led_ctrl) {
        axp_set_bits(supply->master, POWER20_OFF_CTL, 0x08);
        extern_led_ctrl = 0;
    }
	schedule_work(&supply->work.work);

    return 0;
}

static void axp20_shutdown(struct platform_device *dev)
{
    struct axp20_supply *supply = platform_get_drvdata(dev);

    /* not limit usb  voltage*/
  	axp_clr_bits(supply->master, AXP20_CHARGE_VBUS,0x40);

	axp_set_charge_current(axp_pmu_battery->pmu_shutdown_chgcur);	//set charging current
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
    int ret;
    ret = platform_driver_register(&axp_battery_driver);
    AXP_PMU_DBG("call %s, ret = %d\n", __func__, ret);
    return ret;
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
