/*
 * Battery charger driver for Amlogic PMU AML1212
 *
 * Copyright (C) 2012 Amlogic Ltd
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
#include <linux/utsname.h>

#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <mach/am_regs.h>
#include <mach/gpio.h>
#include <linux/amlogic/aml_pmu.h>
#include <linux/amlogic/battery_parameter.h>
#include <linux/amlogic/aml_rtc.h>
#include <mach/irqs.h> 
#include <mach/usbclock.h>
#ifdef CONFIG_RESET_TO_SYSTEM
#include <linux/reboot.h>
#include <linux/notifier.h>
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/wakelock_android.h>
#include <linux/earlysuspend.h>
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend aml_pmu_early_suspend;
static int    in_early_suspend = 0; 
static int    early_power_status = 0;
static struct wake_lock aml1212_lock;
#endif

#define MAX_BUF         100
#define CHECK_DRIVER()      \
    if (!g_aml1212_client) {    \
        AML_PMU_INFO("driver is not ready right now, wait...\n");   \
        dump_stack();       \
        return -ENODEV;     \
    }

#define CHECK_REGISTER_TEST     1
#if CHECK_REGISTER_TEST
int register_wrong_flag = 0;
#endif

#ifdef CONFIG_AMLOGIC_USB
struct later_job {
    int flag;
    int value;
};
static struct later_job aml1212_late_job = {};
#endif

static struct amlogic_pmu_init  *aml1212_init      = NULL;
static struct battery_parameter *aml_pmu_battery   = NULL;
static struct input_dev         *aml_pmu_power_key = NULL;

int      re_charge_cnt  = 0;
int      power_flag     = 0;
int      pmu_version    = 0;

struct aml1212_supply *g1212_supply  = NULL;

int aml_pmu_write(int add, uint8_t val)
{
    int ret;
    uint8_t buf[3] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1212_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1212_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    buf[2] = val & 0xff;
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        AML_PMU_ERR("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_write);

int aml_pmu_write16(int add, uint16_t val)
{
    int ret;
    uint8_t buf[4] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1212_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1212_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    buf[2] = val & 0xff;
    buf[3] = (val >> 8) & 0xff;
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        AML_PMU_ERR("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_write16);

int aml_pmu_writes(int add, uint8_t *buff, int len)
{
    int ret;
    uint8_t buf[MAX_BUF] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1212_ADDR,
            .flags = 0,
            .len   = len + 2,
            .buf   = buf,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1212_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    memcpy(buf + 2, buff, len > MAX_BUF ? MAX_BUF : len);
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        AML_PMU_ERR("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_writes);

int aml_pmu_read(int add, uint8_t *val)
{
    int ret;
    uint8_t buf[2] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1212_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = AML1212_ADDR,
            .flags = I2C_M_RD,
            .len   = 1,
            .buf   = val,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1212_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        AML_PMU_ERR("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_read);

int aml_pmu_read16(int add, uint16_t *val)
{
    int ret;
    uint8_t buf[2] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1212_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = AML1212_ADDR,
            .flags = I2C_M_RD,
            .len   = 2, 
            .buf   = (uint8_t *)val,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1212_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        AML_PMU_ERR("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_read16);

int aml_pmu_reads(int add, uint8_t *buff, int len)
{
    int ret;
    uint8_t buf[2] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1212_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = AML1212_ADDR,
            .flags = I2C_M_RD,
            .len   = len,
            .buf   = buff,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1212_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        AML_PMU_ERR("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_reads);

int aml_pmu_set_bits(int addr, uint8_t bits, uint8_t mask)
{
    uint8_t val; 
    int ret; 
 
    ret = aml_pmu_read(addr, &val); 
    if (ret) { 
        return ret; 
    } 
    val &= ~(mask); 
    val |=  (bits & mask); 
    return aml_pmu_write(addr, val); 
} 
EXPORT_SYMBOL_GPL(aml_pmu_set_bits); 

int aml_pmu_set_dcin(int enable)
{
    uint8_t val;

    aml_pmu_read(AML1212_CHG_CTRL4, &val);
    if (enable) {
        val &= ~0x10;    
    } else {
        val |= 0x10;    
    }    
    return aml_pmu_write(AML1212_CHG_CTRL4, val);
}
EXPORT_SYMBOL_GPL(aml_pmu_set_dcin);

int aml_pmu_set_gpio(int pin, int val)
{
    uint32_t data;

    if (pin <= 0 || pin > 4 || val > 1 || val < 0) {
        AML_PMU_ERR("ERROR, invalid input value, pin = %d, val= %d\n", pin, val);
        return -EINVAL;
    }
    data = (1 << (pin + 11));
    if (val) {
        return aml_pmu_write16(AML1212_PWR_DN_SW_ENABLE,  data);
    } else {
        return aml_pmu_write16(AML1212_PWR_UP_SW_ENABLE, data);
    }
}
EXPORT_SYMBOL_GPL(aml_pmu_set_gpio);

int aml_pmu_get_gpio(int pin, int *val)
{
    int ret;
    uint8_t data;

    if (pin <= 0 || pin > 4 || !val) { 
        AML_PMU_ERR("ERROR, invalid input value, pin = %d, val= %p\n", pin, val);
        return -EINVAL;
    }
    ret = aml_pmu_read(AML1212_GPIO_INPUT_STATUS, &data);
    if (ret) {                                                  // read failed
        return ret;    
    }
    if (data & (1 << (pin - 1))) {
        *val = 1;
    } else {
        *val = 0;    
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_get_gpio);

int aml_pmu_get_voltage(void)
{
    uint8_t buf[2] = {};
    int     result = 0;
    int     tmp, i;

    for (i = 0; i < 4; i++) {
        aml_pmu_write(AML1212_SAR_SW_EN_FIELD, 0x04);
        udelay(10);
        aml_pmu_reads(AML1212_SAR_RD_VBAT_ACTIVE, buf, 2);
        tmp = (((buf[1] & 0x0f) << 8) + buf[0]);
        if (pmu_version == 0x02 || pmu_version == 0x01) {       // VERSION A & B
            tmp = (tmp * 7200) / 2048; 
        } else if (pmu_version == 0x03) {                       // VERSION D
            tmp = (tmp * 4800) / 2048;
        } else {
            tmp = 0;    
        }
        result += tmp;
    }
    return result / 4;
}
EXPORT_SYMBOL_GPL(aml_pmu_get_voltage);

int aml_pmu_get_current(void)
{
    uint8_t  buf[2] = {};
    uint32_t tmp, i;
    int      result = 0;
    int      sign_bit;

    for (i = 0; i < 4; i++) {
        aml_pmu_write(AML1212_SAR_SW_EN_FIELD, 0x40);
        udelay(10);
        aml_pmu_reads(AML1212_SAR_RD_IBAT_LAST, buf, 2);
        tmp = ((buf[1] & 0x0f) << 8) + buf[0];
        sign_bit = tmp & 0x800;
        if (tmp & 0x800) {                                              // complement code
            tmp = (tmp ^ 0xfff) + 1;
        }
        result += (tmp * 4000) / 2048;                                  // LSB of IBAT ADC is 1.95mA
    }
    result /= 4;
    return result;
}
EXPORT_SYMBOL_GPL(aml_pmu_get_current);

static int aml1212_get_coulomb_acc(struct aml_charger *charger)
{
    uint8_t buf[4] = {};
    int result;
    int coulomb;

    aml_pmu_write(AML1212_SAR_SW_EN_FIELD, 0x40);
    aml_pmu_reads(AML1212_SAR_RD_IBAT_ACC, buf, 4);

    result  = (buf[0] <<  0) |
              (buf[1] <<  8) |
              (buf[2] << 16) |
              (buf[3] << 24);
    coulomb = (result) / (3600 * 100);                              // convert to mAh
    coulomb = (coulomb * 4000) / 2048;                              // LSB of current is 1.95mA
    charger->charge_cc    = coulomb;
    charger->discharge_cc = 0;
    return 0;
}

int aml_pmu_get_ibat_cnt(void)
{
    uint8_t buf[4] = {};
    uint32_t cnt = 0;
    
    aml_pmu_write(AML1212_SAR_SW_EN_FIELD, 0x40);
    aml_pmu_reads(AML1212_SAR_RD_IBAT_CNT, buf, 4);

    cnt = (buf[0] <<  0) | 
          (buf[1] <<  8) |
          (buf[2] << 16) |
          (buf[3] << 24);
    return cnt;
}

int aml_pmu_manual_measure_current(void)
{
    uint8_t buf[2] = {};
    int result;
    int tmp;

    aml_pmu_write(0xA2, 0x09);                                      // MSR_SEL = 3
    aml_pmu_write(0xA9, 0x0f);                                      // select proper ADC channel
    aml_pmu_write(0xAA, 0xe0);
    udelay(20);
    aml_pmu_write(AML1212_SAR_SW_EN_FIELD, 0x08);
    udelay(20);
    aml_pmu_reads(AML1212_SAR_RD_MANUAL, buf, 2);
    tmp = ((buf[1] & 0x0f)<< 8) | buf[0];
    if (tmp & 0x800) {                                              // complement code
        tmp = (tmp ^ 0xfff) + 1;
    }
    result = tmp * 4000 / 2048;
    return result;
}

static unsigned int dcdc1_voltage_table[] = {                  // voltage table of DCDC1
    2000, 1980, 1960, 1940, 1920, 1900, 1880, 1860, 
    1840, 1820, 1800, 1780, 1760, 1740, 1720, 1700, 
    1680, 1660, 1640, 1620, 1600, 1580, 1560, 1540, 
    1520, 1500, 1480, 1460, 1440, 1420, 1400, 1380, 
    1360, 1340, 1320, 1300, 1280, 1260, 1240, 1220, 
    1200, 1180, 1160, 1140, 1120, 1100, 1080, 1060, 
    1040, 1020, 1000,  980,  960,  940,  920,  900,  
     880,  860,  840,  820,  800,  780,  760,  740
};

static unsigned int dcdc2_voltage_table[] = {                  // voltage table of DCDC2
    2160, 2140, 2120, 2100, 2080, 2060, 2040, 2020,
    2000, 1980, 1960, 1940, 1920, 1900, 1880, 1860, 
    1840, 1820, 1800, 1780, 1760, 1740, 1720, 1700, 
    1680, 1660, 1640, 1620, 1600, 1580, 1560, 1540, 
    1520, 1500, 1480, 1460, 1440, 1420, 1400, 1380, 
    1360, 1340, 1320, 1300, 1280, 1260, 1240, 1220, 
    1200, 1180, 1160, 1140, 1120, 1100, 1080, 1060, 
    1040, 1020, 1000,  980,  960,  940,  920,  900
};

int find_idx_by_voltage(int voltage, unsigned int *table)
{
    int i;

    /*
     * under this section divide(/ or %) can not be used, may cause exception
     */
    for (i = 0; i < 64; i++) {
        if (voltage >= table[i]) {
            break;    
        }
    }
    if (voltage == table[i]) {
        return i;    
    }
    return i - 1;
}

void aml_pmu_set_voltage(int dcdc, int voltage)
{
    int idx_to = 0xff;
    int idx_cur;
    unsigned char val;
    unsigned char addr;
    unsigned int *table;
    
    if (dcdc < 0 || dcdc > AML_PMU_DCDC2 || voltage > 2100 || voltage < 840) {
        return ;                                                // current only support DCDC1&2 voltage adjust
    }
    if (dcdc == AML_PMU_DCDC1) {
        addr  = 0x2f; 
        table = dcdc1_voltage_table; 
    } else if (dcdc == AML_PMU_DCDC2) {
        addr  = 0x38;    
        table = dcdc2_voltage_table; 
    }
    aml_pmu_read(addr, &val);
    idx_cur = ((val & 0xfc) >> 2);
    idx_to = find_idx_by_voltage(voltage, table);
    AML_PMU_DBG("set idx from %x to %x\n", idx_cur, idx_to);
    while (idx_cur != idx_to) {
        if (idx_cur < idx_to) {                                 // adjust to target voltage step by step
            idx_cur++;    
        } else {
            idx_cur--;
        }
        val &= ~0xfc;
        val |= (idx_cur << 2);
        aml_pmu_write(addr, val);
        udelay(100);                                            // atleast delay 100uS
    }
}
EXPORT_SYMBOL_GPL(aml_pmu_set_voltage);

void aml_pmu_poweroff(void)
{
    uint8_t buf = (1 << 5);                                     // software goto OFF state

#ifdef CONFIG_RESET_TO_SYSTEM
    aml_pmu_write(0x00ff, 0x00);
#endif
    aml_pmu_write(0x0019, 0x10);                                // according Harry, cut usb output
    aml_pmu_write16(0x0084, 0x0001);
  //aml_pmu_write(AML1212_PIN_MUX4, 0x04);                      // according David Wang, for charge cannot stop
    aml_pmu_write(0x0078, 0x04);                                // close LDO6 before power off
    aml_pmu_set_charge_enable(0);                               // close charger before power off
    if (pmu_version == 0x03) {
        aml_pmu_set_bits(0x004a, 0x00, 0x08);                   // close clock of charger for REVD
    }
    AML_PMU_INFO("software goto OFF state\n");
    mdelay(10);
    aml_pmu_write(AML1212_GEN_CNTL1, buf);    
    AML_PMU_ERR("power off PMU failed\n");
    while (1) {
            
    }
}
EXPORT_SYMBOL_GPL(aml_pmu_poweroff);

static int aml1212_update_status(struct aml_charger *charger);

static enum power_supply_property aml_pmu_battery_props[] = {
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

static enum power_supply_property aml_pmu_ac_props[] = {
    POWER_SUPPLY_PROP_MODEL_NAME,
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_ONLINE,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW,
};

static enum power_supply_property aml_pmu_usb_props[] = {
    POWER_SUPPLY_PROP_MODEL_NAME,
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_ONLINE,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW,
};

static void aml_pmu_battery_check_status(struct aml1212_supply       *supply,
                                         union  power_supply_propval *val)
{
    struct aml_charger *charger = &supply->aml_charger;

    if (charger->bat_det) {
        if (charger->ext_valid){
            if( charger->rest_vol == 100) {
                val->intval = POWER_SUPPLY_STATUS_FULL;
            } else if (charger->rest_vol == 0 && 
                       charger->charge_status == CHARGER_DISCHARGING) { 
                // protect for over-discharging
                val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
            } else {
                val->intval = POWER_SUPPLY_STATUS_CHARGING;
            }
        } else {
            val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
        }
    } else {
        val->intval = POWER_SUPPLY_STATUS_FULL;
    }
}

static void aml_pmu_battery_check_health(struct aml1212_supply       *supply,
                                         union  power_supply_propval *val)
{
    // TODO: need implement
#if 0
    struct aml_charger *charger = &supply->charger;
    if (charger->fault & ) {
    } else if (charger->fault & ) {
    } else if (charger->fault & ) {
    } else {
        val->intval = POWER_SUPPLY_HEALTH_GOOD;
    }
#else
    val->intval = POWER_SUPPLY_HEALTH_GOOD;
#endif
}

static int aml_pmu_battery_get_property(struct power_supply *psy,
                                        enum   power_supply_property psp,
                                        union  power_supply_propval *val)
{
    struct aml1212_supply *supply;
    struct aml_charger    *charger;
    int ret = 0;
    supply  = container_of(psy, struct aml1212_supply, batt);
    charger = &supply->aml_charger;
    
    switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:
        aml_pmu_battery_check_status(supply, val);
        break;

    case POWER_SUPPLY_PROP_HEALTH:
        aml_pmu_battery_check_health(supply, val);
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
        val->intval = charger->vbat * 1000; 
        break;

    case POWER_SUPPLY_PROP_CURRENT_NOW:             // charging : +, discharging -;
        if (ABS(charger->ibat) > 20  && 
            !charger->charge_timeout && 
            charger->charge_status != CHARGER_NONE) {
            val->intval = charger->ibat * 1000 * (charger->charge_status == CHARGER_CHARGING ? 1 : -1);
        } else {
            val->intval = 0;                        // when charge time out, report 0
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
        val->intval = charger->bat_det; 
        break;

    case POWER_SUPPLY_PROP_PRESENT:
        val->intval = charger->bat_det;
        break;
    case POWER_SUPPLY_PROP_TEMP:
        val->intval =  300;                         // fixed to 300k, need implement dynamic values
        break;
    default:
        ret = -EINVAL;
        break;
    }
    
    return ret;
}

static int aml_pmu_ac_get_property(struct power_supply *psy,
                                   enum   power_supply_property psp,
                                   union  power_supply_propval *val)
{
    struct aml1212_supply *supply;
    struct aml_charger    *charger;
    int ret = 0;
    supply  = container_of(psy, struct aml1212_supply, ac);
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
        val->intval = 5000 * 1000;          // charger->v_dcin * 1000;
        break;

    case POWER_SUPPLY_PROP_CURRENT_NOW:
        val->intval = 1000 * 1000;          // charger->i_dcin * 1000;
        break;

    default:
        ret = -EINVAL;
        break;
    }
    return ret;
}

static int aml_pmu_usb_get_property(struct power_supply *psy,
           enum power_supply_property psp,
           union power_supply_propval *val)
{
    struct aml1212_supply *supply;
    struct aml_charger    *charger;
    int ret = 0;
    supply  = container_of(psy, struct aml1212_supply, usb);
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
        val->intval = 5000 * 1000;      // charger->v_usb * 1000;
        break;

    case POWER_SUPPLY_PROP_CURRENT_NOW:
        val->intval = 1000 * 1000;      // charger->i_usb * 1000;
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

static void aml_pmu_battery_setup_psy(struct aml1212_supply *supply)
{
    struct power_supply      *batt = &supply->batt;
    struct power_supply      *ac   = &supply->ac;
    struct power_supply      *usb  = &supply->usb;
    struct power_supply_info *info =  supply->battery_info;
    
    batt->name           = "battery";
    batt->use_for_apm    = info->use_for_apm;
    batt->type           = POWER_SUPPLY_TYPE_BATTERY;
    batt->get_property   = aml_pmu_battery_get_property;
    batt->properties     = aml_pmu_battery_props;
    batt->num_properties = ARRAY_SIZE(aml_pmu_battery_props);
    
    ac->name             = "ac";
    ac->type             = POWER_SUPPLY_TYPE_MAINS;
    ac->get_property     = aml_pmu_ac_get_property;
    ac->supplied_to      = supply_list;
    ac->num_supplicants  = ARRAY_SIZE(supply_list);
    ac->properties       = aml_pmu_ac_props;
    ac->num_properties   = ARRAY_SIZE(aml_pmu_ac_props);
    
    usb->name            = "usb";
    usb->type            = POWER_SUPPLY_TYPE_USB;
    usb->get_property    = aml_pmu_usb_get_property;
    usb->supplied_to     = supply_list,
    usb->num_supplicants = ARRAY_SIZE(supply_list),
    usb->properties      = aml_pmu_usb_props;
    usb->num_properties  = ARRAY_SIZE(aml_pmu_usb_props);
}

int aml_pmu_set_usb_current_limit(int curr, int bc_mode)
{
    uint8_t val;

    aml_pmu_read(AML1212_CHG_CTRL3, &val);
    val &= ~(0x30);
    if (bc_mode > 0) {
       g1212_supply->usb_connect_type = bc_mode;
    }
    AML_PMU_INFO("usb connet mode:%d, current limit to:%dmA\n", bc_mode, curr);
    switch (curr) {
    case 0:
        val |= 0x30;                                    // disable limit
        break;

    case 100:
        val |= 0x00;                                    // 100mA
        break;

    case 500:
        val |= 0x10;                                    // 500mA
        break;

    case 900:
        val |= 0x20;                                    // 900mA
        break;

    default:
        AML_PMU_ERR("%s, wrong usb current limit:%d\n", __func__, curr);
        return -1; 
    }
    return aml_pmu_write(AML1212_CHG_CTRL3, val);
}
EXPORT_SYMBOL_GPL(aml_pmu_set_usb_current_limit);

int aml1212_set_usb_current_limit(int curr)
{
    return aml_pmu_set_usb_current_limit(curr, -1);    
}

int aml_pmu_get_battery_percent(void)
{
    CHECK_DRIVER();
    return g1212_supply->aml_charger.rest_vol;
}
EXPORT_SYMBOL_GPL(aml_pmu_get_battery_percent);

int aml_pmu_set_charge_current(int chg_cur)
{
    uint8_t val;

    aml_pmu_read(AML1212_CHG_CTRL4, &val);
    switch (chg_cur) {
    case 1500000:
        val &= ~(0x01 << 5);
        break;

    case 2000000:
        val |= (0x01 << 5);
        break;

    default:
        AML_PMU_ERR("%s, Wrong charge current:%d\n", __func__, chg_cur);
        return -1;
    }
    aml_pmu_write(AML1212_CHG_CTRL4, val);

    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_set_charge_current);

int aml_pmu_set_charge_voltage(int voltage)
{
    uint8_t val;
    uint8_t tmp;
    
    if (voltage > 4400000 || voltage < 4050000) {
        AML_PMU_ERR("%s,Wrong charge voltage:%d\n", __func__, voltage);
        return -1;
    }
    tmp = ((voltage - 4050000) / 50000) & 0x07;
    aml_pmu_read(AML1212_CHG_CTRL0, &val);
    val &= ~(0x07);
    val |= tmp;
    aml_pmu_write(AML1212_CHG_CTRL0, val);

    return 0; 
}
EXPORT_SYMBOL_GPL(aml_pmu_set_charge_voltage);

int aml_pmu_set_charge_end_rate(int rate) 
{
    uint8_t val;

    aml_pmu_read(AML1212_CHG_CTRL4, &val);
    switch (rate) {
    case 10:
        val &= ~(0x01 << 3);
        break;

    case 20:
        val |= (0x01 << 3);
        break;

    default:
        AML_PMU_ERR("%s, Wrong charge end rate:%d\n", __func__, rate);
        return -1;
    }
    aml_pmu_write(AML1212_CHG_CTRL4, val);

    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_set_charge_end_rate);

int aml_pmu_set_adc_freq(int freq)
{
    uint8_t val;
    int32_t time;
    int32_t time_base;
    int32_t time_bit;

    if (freq > 1000 || freq < 10) {
        AML_PMU_ERR("%s, Wrong adc freq:%d\n", __func__, freq);    
        return -1;
    }
    time = 1000 / freq;
    if (time >= 1 && time < 10) {
        time_base = 1;
        time_bit  = 0;
    } else if (time >= 10 && time < 99) {
        time_base = 10;
        time_bit  = 1;
    } else {
        time_base = 100;
        time_bit  = 2;
    }
    time /= time_base;
    val = ((time_bit << 6) | (time - 1)) & 0xff;
    AML_PMU_DBG("%s, set reg[0xA0] to %02x\n", __func__, val);          // TEST
    aml_pmu_write(AML1212_SAR_CNTL_REG5, val);

    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_set_adc_freq);

int aml_pmu_set_precharge_time(int minute)
{
    uint8_t val;

    if (minute > 80 || minute < 30) {
        AML_PMU_ERR("%s, Wrong pre-charge time:%d\n", __func__, minute);
        return -1;
    }
    aml_pmu_read(AML1212_CHG_CTRL3, &val);
    val &= ~(0x03);
    switch (minute) {
    case 30:
        val |= 0x01;
        break;

    case 50:
        val |= 0x02;
        break;

    case 80:
        val |= 0x03;
        break;
    
    default:
        AML_PMU_ERR("%s, Wrong pre-charge time:%d\n", __func__, minute);
        return -1;
    }
    aml_pmu_write(AML1212_CHG_CTRL3, val);

    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_set_precharge_time);

int aml_pmu_set_fastcharge_time(int minute)
{
    uint8_t val;

    if (minute < 360 || minute > 720) {
        AML_PMU_ERR("%s, Wrong fast-charge time:%d\n", __func__, minute);
        return -1;
    }
    aml_pmu_read(AML1212_CHG_CTRL3, &val);
    val &= ~(0xC0);
    switch (minute) {
    case 360:
        val |= (0x01 << 6);
        break;

    case 540:
        val |= (0x02 << 6);
        break;

    case 720:
        val |= (0x03 << 6);
        break;

    default:
        AML_PMU_ERR("%s, Wrong pre-charge time:%d\n", __func__, minute);
        return -1;
    }
    aml_pmu_write(AML1212_CHG_CTRL3, val);

    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_set_fastcharge_time);

int aml_pmu_set_charge_enable(int en)
{
    uint8_t val;
    
    if (pmu_version <= 0x02 && pmu_version != 0) {                      /* reversion A or B             */
        aml_pmu_read(AML1212_OTP_GEN_CONTROL0, &val);
        if (en) {
            val |= (0x01);    
        } else {
            val &= ~(0x01);    
        }
        aml_pmu_write(AML1212_OTP_GEN_CONTROL0, val);
    } else if (pmu_version == 0x03) {                                   /* reversion D                  */
        aml_pmu_set_bits(0x0011, ((en & 0x01) << 7), 0x80); 
    }

    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_set_charge_enable);

int aml_pmu_set_usb_voltage_limit(int voltage)
{
    uint8_t val;

    if (voltage > 4600 || voltage < 4300) {
        AML_PMU_ERR("%s, Wrong usb voltage limit:%d\n", __func__, voltage);    
    }
    aml_pmu_read(AML1212_CHG_CTRL4, &val);
    val &= ~(0xc0);
    switch (voltage) {
    case 4300:
        val |= (0x01 << 6);
        break;

    case 4400:
        val |= (0x02 << 6);
        break;

    case 4500:
        val |= (0x00 << 6);
        break;

    case 4600:
        val |= (0x03 << 6);
        break;
    
    default:
        AML_PMU_ERR("%s, Wrong usb voltage limit:%d\n", __func__, voltage);
        return -1;
    }
    aml_pmu_write(AML1212_CHG_CTRL4, val);

    return 0;
}
EXPORT_SYMBOL_GPL(aml_pmu_set_usb_voltage_limit);

static int aml1212_clear_coulomb(struct aml_charger *charger)
{
	aml_pmu_write(AML1212_SAR_SW_EN_FIELD, 0x80); 
    return 0;
}

int aml_pmu_first_init(struct aml1212_supply *supply)
{
    uint8_t irq_mask[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xf0};

    aml_pmu_write(AML1212_SAR_CNTL_REG2,   0x64);                       // select vref=2.4V
    aml_pmu_write(AML1212_SAR_CNTL_REG3,   0x14);                       // close the useless channel input
    aml_pmu_write(AML1212_SAR_CNTL_REG0,   0x0c);                       // enable IBAT_AUTO, ACCUM
    aml_pmu_writes(AML1212_IRQ_MASK_0, irq_mask, sizeof(irq_mask));     // open all IRQ

    /*
     * initialize charger from battery parameters
     */
    aml_pmu_set_charge_current (aml_pmu_battery->pmu_init_chgcur);
    aml_pmu_set_charge_voltage (aml_pmu_battery->pmu_init_chgvol);
    aml_pmu_set_charge_end_rate(aml_pmu_battery->pmu_init_chgend_rate);
    aml_pmu_set_adc_freq       (aml_pmu_battery->pmu_init_adc_freqc);
    aml_pmu_set_precharge_time (aml_pmu_battery->pmu_init_chg_pretime);
    aml_pmu_set_fastcharge_time(aml_pmu_battery->pmu_init_chg_csttime);
    aml_pmu_set_charge_enable  (aml_pmu_battery->pmu_init_chg_enabled);

    if (aml_pmu_battery->pmu_usbvol_limit) {
        aml_pmu_set_usb_voltage_limit(aml_pmu_battery->pmu_usbvol); 
    }
    if (aml_pmu_battery->pmu_usbcur_limit) {
        aml_pmu_set_usb_current_limit(500, USB_BC_MODE_SDP);            // fix to 500 when init
    }

    return 0;
}

/*
 * add for debug 
 */
static ssize_t dbg_info_show     (struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t dbg_info_store    (struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t battery_para_show (struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t battery_para_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t report_delay_show (struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t report_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

static ssize_t dump_pmu_regs_show   (struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t dump_pmu_regs_store  (struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static int     aml_pmu_regs_base = 0;
static ssize_t aml_pmu_reg_base_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "aml_pmu_regs_base: 0x%02x\n", aml_pmu_regs_base); 
}

static ssize_t aml_pmu_reg_base_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int tmp = simple_strtoul(buf, NULL, 16);
    if (tmp > 255) {
        AML_PMU_ERR("Invalid input value\n");
        return -1;
    }
    aml_pmu_regs_base = tmp;
    AML_PMU_DBG("Set register base to 0x%02x\n", aml_pmu_regs_base);
    return count;
}

static ssize_t aml_pmu_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    uint8_t data;
    aml_pmu_read(aml_pmu_regs_base, &data);
    return sprintf(buf, "reg[0x%02x] = 0x%02x\n", aml_pmu_regs_base, data);
}

static ssize_t aml_pmu_reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    uint8_t data = simple_strtoul(buf, NULL, 16);
    if (data > 255) {
        AML_PMU_ERR("Invalid input value\n");
        return -1;
    }
    aml_pmu_write(aml_pmu_regs_base, data);
    return count; 
}

static ssize_t aml_pmu_reg_16bit_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    uint16_t data;
    if (aml_pmu_regs_base & 1){
        AML_PMU_ERR("Invalid reg base value\n");
        return -1;
    }
    aml_pmu_read16(aml_pmu_regs_base, &data);
    return sprintf(buf, "reg[0x%04x] = 0x%04x\n", aml_pmu_regs_base, data);
}

static ssize_t aml_pmu_reg_16bit_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    uint16_t data = simple_strtoul(buf, NULL, 16);
    if (data > 0xffff) {
        AML_PMU_ERR("Invalid input value\n");
        return -1;
    }
    if (aml_pmu_regs_base & 1){
        AML_PMU_ERR("Invalid reg base value\n");
        return -1;
    }
    aml_pmu_write16(aml_pmu_regs_base, data);
    return count; 
}

static ssize_t aml_pmu_vddao_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    uint8_t data;
    aml_pmu_read(0x2f, &data);

    return sprintf(buf, "Voltage of VDD_AO = %4dmV\n", dcdc1_voltage_table[(data & 0xfc) >> 2]);
}

static ssize_t aml_pmu_vddao_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    uint32_t data = simple_strtoul(buf, NULL, 10);
    if (data > 2000 || data < 740) {
        AML_PMU_ERR("Invalid input value = %d\n", data);
        return -1;
    }
    AML_PMU_INFO("Set VDD_AO to %4d mV\n", data);
    aml_pmu_set_voltage(AML_PMU_DCDC1, data);
    return count; 
}

static ssize_t driver_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "Amlogic PMU Aml1212 driver version is %s, build time:%s\n", 
                   AML1212_DRIVER_VERSION, init_uts_ns.name.version);
}

static ssize_t driver_version_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return count; 
}

static ssize_t clear_rtc_mem_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return 0;
}

static ssize_t clear_rtc_mem_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{ 
    aml_write_rtc_mem_reg(0, 0);
    aml_pmu_poweroff();
    return count; 
}

static ssize_t charge_timeout_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    uint8_t val = 0;

    aml_pmu_read(AML1212_CHG_CTRL3, &val);
    val >>= 6;
    if (val) {
        return sprintf(buf, "charge timeout is %d minutes\n", val * 180 + 180);   
    }
    return 0;
}

static ssize_t charge_timeout_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{ 
    uint32_t data = simple_strtoul(buf, NULL, 10);
    if (data > 720 || data < 360) {
        AML_PMU_ERR("Invalid input value = %d\n", data);
        return -1;
    }
    AML_PMU_INFO("Set charge timeout to %4d minutes\n", data);
    aml_pmu_set_fastcharge_time(data); 
    return count; 
}

static struct device_attribute aml1212_supply_attrs[] = {
    AML_PMU_CHG_ATTR(aml_pmu_reg_base),
    AML_PMU_CHG_ATTR(aml_pmu_reg),
    AML_PMU_CHG_ATTR(aml_pmu_reg_16bit),
    AML_PMU_CHG_ATTR(aml_pmu_vddao),
    AML_PMU_CHG_ATTR(dbg_info),
    AML_PMU_CHG_ATTR(battery_para),
    AML_PMU_CHG_ATTR(report_delay),
    AML_PMU_CHG_ATTR(driver_version),
    AML_PMU_CHG_ATTR(clear_rtc_mem),
    AML_PMU_CHG_ATTR(charge_timeout),
    AML_PMU_CHG_ATTR(dump_pmu_regs),
};

int aml1212_supply_create_attrs(struct power_supply *psy)
{
    int j,ret;
    for (j = 0; j < ARRAY_SIZE(aml1212_supply_attrs); j++) {
        ret = device_create_file(psy->dev, &aml1212_supply_attrs[j]);
        if (ret)
            goto sysfs_failed;
    }
    goto succeed;

sysfs_failed:
    while (j--) {
        device_remove_file(psy->dev, &aml1212_supply_attrs[j]);
    }
succeed:
    return ret;
}

#ifdef CONFIG_AMLOGIC_USB
int aml1212_otg_change(struct notifier_block *nb, unsigned long value, void *pdata)
{
    AML_PMU_INFO("%s, val:%ld\n", __func__, value);
    if (value) {        // open OTG
        if (aml1212_init->vbus_dcin_short_connect) {
            aml_pmu_set_dcin(0);                                            // Disable DCIN
        }    
        aml_pmu_set_bits(0x0027, 0x02, 0x02);                               // close uv fault of boost
        udelay(100);
        aml_pmu_write(0x0019, 0xD0);                                        // Enable boost output
    } else {
        aml_pmu_write(0x0019, 0x10);                                        // Disable boost output
        aml_pmu_set_bits(0x0027, 0x00, 0x02);                               // open uv fault of boost
    }
    return 0;
}

int aml1212_usb_charger(struct notifier_block *nb, unsigned long value, void *pdata)
{
    if (!g1212_supply) {
        AML_PMU_INFO("%s, driver is not ready, do it later\n", __func__);
        aml1212_late_job.flag  = 1;
        aml1212_late_job.value = value;
        return 0;
    }
    switch (value) {
    case USB_BC_MODE_SDP:                                               // pc
        if (aml1212_init->vbus_dcin_short_connect) {
            aml_pmu_set_dcin(0);
        }    
        if (aml_pmu_battery && aml_pmu_battery->pmu_usbcur_limit) {
            aml_pmu_set_usb_current_limit(aml_pmu_battery->pmu_usbcur, value);
        }
        break;

    case USB_BC_MODE_DCP:                                               // charger
    case USB_BC_MODE_CDP:                                               // PC + charger
    case USB_BC_MODE_DISCONNECT:                                        // disconnect
        if (aml1212_init->vbus_dcin_short_connect) {
            if (value != USB_BC_MODE_DISCONNECT) {
                aml_pmu_set_dcin(1);        /* only open DCIN when detect charger       */
            } else {
                aml_pmu_set_dcin(0);        /* dcin should close when usb disconnect    */    
            }    
        }    
        if (aml_pmu_battery) {
            aml_pmu_set_usb_current_limit(900, value);
        }
        break;

    default:
        break;
    }
    return 0;
}
#endif

static int aml_cal_ocv(int ibat, int vbat, int dir)
{
    int result;

    if (dir == 1) {                                                     // charging
        result = vbat - (ibat * aml_pmu_battery->pmu_battery_rdc) / 1000;
    } else if (dir == 2) {                                              // discharging
        result = vbat + (ibat * aml_pmu_battery->pmu_battery_rdc) / 1000;    
    } else {
        result = vbat;    
    }
    return result;
}

static int aml1212_update_status(struct aml_charger *charger)
{
    uint8_t buff[5] = {};
    static int chg_gat_bat_lv = 0;
    struct aml1212_supply *supply = container_of(charger, struct aml1212_supply, aml_charger);

    aml_pmu_reads(AML1212_SP_CHARGER_STATUS0, buff, sizeof(buff));

    if (!(buff[3] & 0x02)) {                                            // CHG_GAT_BAT_LV = 0, discharging
        charger->charge_status = CHARGER_DISCHARGING; 
    } else if ((buff[3] & 0x02) && (buff[2] & 0x04)) {
        charger->charge_status = CHARGER_CHARGING;                      // charging
    } else {
        charger->charge_status = CHARGER_NONE;                          // Not charging 
    }
    charger->bat_det    = 1;                                            // do not check register 0xdf, bug here
    charger->dcin_valid = buff[2] & 0x10 ? 1 : 0;
    charger->usb_valid  = buff[2] & 0x08 ? 1 : 0;
    charger->ext_valid  = buff[2] & 0x18;                               // to differ USB / AC status update 

    charger->fault = ((buff[0] <<  0) | (buff[1] <<  8) |
                      (buff[2] << 16) | (buff[3] << 24));
    if ((!(buff[3] & 0x02)) && !chg_gat_bat_lv) {                       // according David Wang
        AML_PMU_INFO("CHG_GAT_BAT_LV is 0, limit usb current to 500mA\n");
        aml_pmu_set_usb_current_limit(500, supply->usb_connect_type); 
        chg_gat_bat_lv = 1;
    } else if (buff[3] & 0x02 && chg_gat_bat_lv) {
        chg_gat_bat_lv = 0;    
        if (supply->usb_connect_type == USB_BC_MODE_DCP || 
            supply->usb_connect_type == USB_BC_MODE_CDP) {              // reset to 900 when enough current supply
            aml_pmu_set_usb_current_limit(900, supply->usb_connect_type);    
            AML_PMU_INFO("CHG_GAT_BAT_LV is 1, limit usb current to 900mA\n");
        }
    }
    if (buff[1] & 0x40) {                                               // charge timeout detect
        AML_PMU_INFO("Charge timeout detected\n");
        if ((aml1212_init->charge_timeout_retry) &&
            (aml1212_init->charge_timeout_retry > re_charge_cnt)) {
            re_charge_cnt++;
            AML_PMU_INFO("reset charger due to charge timeout occured, ocv :%d, retry:%d\n", 
                        charger->ocv, re_charge_cnt);
            aml_pmu_set_fastcharge_time(360);                           // only retry charge 6 hours, for safe problem
            aml_pmu_set_charge_enable(0);
            msleep(1000);
            aml_pmu_set_charge_enable(1);
        }
        charger->charge_timeout = 1;
    } else {
        charger->charge_timeout = 0;    
    }
    if (charger->ext_valid && !(power_flag & 0x01)) {                   // enable charger when detect extern power
        power_flag |=  0x01;                                            // remember enabled charger 
        power_flag &= ~0x02;
    } else if (!charger->ext_valid && !(power_flag & 0x02)) {
        re_charge_cnt = 0;
        aml_pmu_set_fastcharge_time(aml_pmu_battery->pmu_init_chg_csttime);
        power_flag |=  0x02;                                            // remember disabled charger
        power_flag &= ~0x01;
    }
    if (!charger->ext_valid && aml1212_init->vbus_dcin_short_connect) {
        aml_pmu_set_dcin(0);                                            // disable DCIN when no extern power
    }
    charger->vbat = aml_pmu_get_voltage();
    charger->ibat = aml_pmu_get_current();
    charger->ocv  = aml_cal_ocv(charger->ibat, charger->vbat, charger->charge_status);
    return 0;
}

int dump_pmu_register(char *buf)
{
    uint8_t val[16];
    int     i;
    int     size = 0;

    if (!buf) {
        printk("[AML_PMU] DUMP ALL REGISTERS\n");
        for (i = 0; i < 16; i++) {
            aml_pmu_reads(i*16, val, 16);
            printk("0x%02x - %02x: ", i * 16, i * 16 + 15);
            printk("%02x %02x %02x %02x ",   val[0],  val[1],  val[2],  val[3]);
            printk("%02x %02x %02x %02x   ", val[4],  val[5],  val[6],  val[7]);
            printk("%02x %02x %02x %02x ",   val[8],  val[9],  val[10], val[11]);
            printk("%02x %02x %02x %02x\n",  val[12], val[13], val[14], val[15]);
        }
        return 0;
    }
    
    size += sprintf(buf + size, "%s", "[AML_PMU] DUMP ALL REGISTERS\n");
    for (i = 0; i < 16; i++) {
        aml_pmu_reads(i*16, val, 16);
        size += sprintf(buf + size, "0x%02x - %02x: ", i * 16, i * 16 + 15);
        size += sprintf(buf + size, "%02x %02x %02x %02x ",   val[0],  val[1],  val[2],  val[3]);
        size += sprintf(buf + size, "%02x %02x %02x %02x   ", val[4],  val[5],  val[6],  val[7]);
        size += sprintf(buf + size, "%02x %02x %02x %02x ",   val[8],  val[9],  val[10], val[11]);
        size += sprintf(buf + size, "%02x %02x %02x %02x\n",  val[12], val[13], val[14], val[15]);
    }
    return size;
}
EXPORT_SYMBOL_GPL(dump_pmu_register);

static ssize_t dump_pmu_regs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int size;
    size = dump_pmu_register(buf);
    size += sprintf(buf, "%s", "[AML_PMU] DUMP ALL REGISTERS OVER!\n"); 
    return size;
}
static ssize_t dump_pmu_regs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return count;                                           /* nothing to do        */
}

static ssize_t dbg_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply   *battery = dev_get_drvdata(dev);
    struct aml1212_supply *supply= container_of(battery, struct aml1212_supply, batt); 
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
    return count;                                           /* nothing to do        */
}

static ssize_t battery_para_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply   *battery = dev_get_drvdata(dev);
    struct aml1212_supply *supply  = container_of(battery, struct aml1212_supply, batt); 
    struct aml_charger    *charger = &supply->aml_charger;
    int i = 0; 
    int size;

    size = sprintf(buf, "\n i,      ocv,    charge,  discharge,\n");
    for (i = 0; i < 16; i++) {
        size += sprintf(buf + size, "%2d,     %4d,       %3d,        %3d,\n",
                        i, 
                        aml_pmu_battery->pmu_bat_curve[i].ocv,
                        aml_pmu_battery->pmu_bat_curve[i].charge_percent,
                        aml_pmu_battery->pmu_bat_curve[i].discharge_percent);
    }
    size += sprintf(buf + size, "\nBattery capability:%4d@3700mAh, RDC:%3d mohm\n", 
                                aml_pmu_battery->pmu_battery_cap, 
                                aml_pmu_battery->pmu_battery_rdc);
    size += sprintf(buf + size, "Charging efficiency:%3d%%, capability now:%3d%%\n", 
                                aml_pmu_battery->pmu_charge_efficiency,
                                charger->rest_vol);
    size += sprintf(buf + size, "ocv_empty:%4d, ocv_full:%4d\n\n",
                                charger->ocv_empty, 
                                charger->ocv_full);
    return size;
}

static ssize_t battery_para_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return count;                                                       /* nothing to do        */
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
        AML_PMU_ERR("input too large, failed to set report_delay\n");    
    }    
    if (api && api->pmu_set_report_delay) {
        api->pmu_set_report_delay(tmp);
    } else {
        AML_PMU_ERR("API not found\n");
    }
    return count;
}

static void aml_pmu_charging_monitor(struct work_struct *work)
{
    struct   aml1212_supply *supply;
    struct   aml_charger    *charger;
    static int check_charge_flag = 0; 
    uint8_t  v[2] = {};
    int32_t pre_rest_cap;
    uint8_t pre_chg_status;
    struct aml_pmu_api *api;
    static bool api_flag = false;

    supply  = container_of(work, struct aml1212_supply, work.work);
    charger = &supply->aml_charger;
    pre_chg_status = charger->ext_valid;
    pre_rest_cap   = charger->rest_vol;

    /*
     * 1. update status of PMU and all ADC value
     * 2. read ocv value and calculate ocv percent of battery
     * 3. read coulomb value and calculate movement of energy
     * 4. if battery capacity is larger than 429496 mAh, will cause over flow
     */
    api     = aml_pmu_get_api();
    if (!api) {
        schedule_delayed_work(&supply->work, supply->interval);
        return ;                                                // KO is not ready
    }
    if (api && !api_flag) {
        api_flag = true;
        if (api->pmu_probe_process) {
            api->pmu_probe_process(charger, aml_pmu_battery);
        }
    }
    api->pmu_update_battery_capacity(charger, aml_pmu_battery); 

    if (charger->ocv > 5000) {
        // SAR ADC error, only occur when battery voltage is very low
        AML_PMU_ERR(">> SAR ADC error, ocv:%d, vbat:%d, ibat:%d\n", 
                    charger->ocv, charger->vbat, charger->ibat);
        charger->rest_vol = 0;
    }

    /*
     * work around for cannot stop charge problem, according David Wang
     */
    if (charger->rest_vol >= 99 && charger->ibat < 150 && charger->charge_status == CHARGER_CHARGING && !check_charge_flag) {
        aml_pmu_read(AML1212_SP_CHARGER_STATUS3, v);
        if (!(v[0] & 0x08)) {
            check_charge_flag = 1;
            AML_PMU_INFO("CHG_END_DET = 0 find, close charger for 1 second\n");
            aml_pmu_set_charge_enable(0);
            msleep(1000);
            aml_pmu_set_charge_enable(1);
        }
    } else if (charger->rest_vol < 99) {
        check_charge_flag = 0;
    }

    if((charger->rest_vol - pre_rest_cap) || (pre_chg_status != charger->ext_valid) || charger->resume){
        AML_PMU_INFO("battery vol change: %d->%d \n", pre_rest_cap, charger->rest_vol);
        if (unlikely(charger->resume)) {
            charger->resume = 0;
        }
        power_supply_changed(&supply->batt);
    #ifdef CONFIG_HAS_EARLYSUSPEND
        if (in_early_suspend && (pre_chg_status != charger->ext_valid)) {
            wake_lock(&aml1212_lock);
            AML_PMU_INFO("%s, usb power status changed in early suspend, wake up now\n", __func__);
            input_report_key(aml_pmu_power_key, KEY_POWER, 1);                        // assume power key pressed 
            input_sync(aml_pmu_power_key);
        }
    #endif
    }

    /* reschedule for the next time */
    schedule_delayed_work(&supply->work, supply->interval);
}

#if defined CONFIG_HAS_EARLYSUSPEND
static void aml_pmu_earlysuspend(struct early_suspend *h)
{
    struct  aml1212_supply *supply = (struct aml1212_supply *)h->param;

    if (aml_pmu_battery) {
        early_power_status = supply->aml_charger.ext_valid;
        aml_pmu_set_charge_current(aml_pmu_battery->pmu_suspend_chgcur);
    }
    in_early_suspend = 1;
}

static void aml_pmu_lateresume(struct early_suspend *h)
{
    struct  aml1212_supply *supply = (struct aml1212_supply *)h->param;

    schedule_work(&supply->work.work);                                      // update for upper layer 
    if (aml_pmu_battery) {
        aml_pmu_set_charge_current(aml_pmu_battery->pmu_resume_chgcur);
        early_power_status = supply->aml_charger.ext_valid; 
        input_report_key(aml_pmu_power_key, KEY_POWER, 0);                  // cancel power key 
        input_sync(aml_pmu_power_key);
    }
    in_early_suspend = 0;
    wake_unlock(&aml1212_lock);
}
#endif

irqreturn_t aml_pmu_irq_handler(int irq, void *dev_id)
{
    struct   aml1212_supply *supply= (struct aml1212_supply *)dev_id;

    disable_irq_nosync(supply->irq);
    schedule_work(&supply->irq_work);

    return IRQ_HANDLED;
}

static void aml_pmu_irq_work_func(struct work_struct *work)
{
    struct aml1212_supply *supply = container_of(work, struct aml1212_supply, irq_work);
    uint8_t irq_status[6] = {};

    aml_pmu_reads(AML1212_IRQ_STATUS_CLR_0, irq_status, sizeof(irq_status));
    AML_PMU_DBG("PMU IRQ status: %02x %02x %02x %02x %02x %02x", 
                irq_status[0], irq_status[1], irq_status[2],
                irq_status[3], irq_status[4], irq_status[5]);
    aml_pmu_writes(AML1212_IRQ_STATUS_CLR_0, irq_status, sizeof(irq_status));       // clear IRQ status
    if (irq_status[5] & 0x08) {
        AML_PMU_ERR("Over Temperature is occured, shutdown system\n");
        aml_pmu_poweroff();
    }
    enable_irq(supply->irq);

    return ;
}

static void check_pmu_version(void)
{
    uint8_t val;

    aml_pmu_read(0x007e, &val);
    AML_PMU_INFO("OTP VERSION: 0x%02x\n", val);
    aml_pmu_read(0x007f, &val);
    AML_PMU_INFO("PMU VERSION: 0x%02x\n", val);
    if (val > 0x03 || val == 0x00) {
        AML_PMU_ERR("#### ERROR: unknow pmu version:0x%02x ####\n", val);    
    } else {
        pmu_version = val;
    }
}

#ifdef CONFIG_RESET_TO_SYSTEM
static int aml_pmu_reboot_notifier(struct notifier_block *nb, unsigned long state, void *cmd)
{
    AML_PMU_DBG("%s in\n", __func__);
    aml_pmu_write(0x00ff, 0x00);            // clear boot flag
    return NOTIFY_DONE;
}

static struct notifier_block pmu_reboot_nb;
#endif

struct aml_pmu_driver aml1212_driver = {
    .name                       = "aml1212",
    .pmu_get_coulomb            = aml1212_get_coulomb_acc, 
    .pmu_clear_coulomb          = aml1212_clear_coulomb, 
    .pmu_update_status          = aml1212_update_status, 
    .pmu_set_rdc                = NULL,
    .pmu_set_gpio               = aml_pmu_set_gpio,
    .pmu_get_gpio               = aml_pmu_get_gpio, 
    .pmu_reg_read               = aml_pmu_read, 
    .pmu_reg_write              = aml_pmu_write,
    .pmu_reg_reads              = aml_pmu_reads,
    .pmu_reg_writes             = aml_pmu_writes,
    .pmu_set_bits               = aml_pmu_set_bits,
    .pmu_set_usb_current_limit  = aml1212_set_usb_current_limit,
    .pmu_set_charge_current     = aml_pmu_set_charge_current,
    .pmu_power_off              = aml_pmu_poweroff, 
};

static struct workqueue_struct *aml_pwr_key_work;
static struct delayed_work pwr_key_work;

static void pwr_key_work_func(struct work_struct *work)
{
    static int pressed = 0;
    uint8_t key_value;
    aml_pmu_read(0x00c4, &key_value);
    if (key_value & 0x20) {
        if (pressed) {
            AML_PMU_DBG("pwk key is not pressed\n");
            input_report_key(aml_pmu_power_key, KEY_POWER, 0);
            input_sync(aml_pmu_power_key);
            pressed = 0;
        }
    } else {
        if (!pressed) {
            AML_PMU_DBG("pwk key is pressed\n");
            input_report_key(aml_pmu_power_key, KEY_POWER, 1);
            input_sync(aml_pmu_power_key);
            pressed = 1;
        }    
    }
    queue_delayed_work(aml_pwr_key_work, &pwr_key_work, msecs_to_jiffies(20));
}

static int aml_pmu_battery_probe(struct platform_device *pdev)
{
    struct   aml1212_supply *supply;
    struct   aml_charger    *charger;
    int      ret;
    int      max_diff = 0, tmp_diff;
    uint32_t tmp2;

	AML_PMU_DBG("call %s in", __func__);
    aml1212_init = pdev->dev.platform_data;
    aml_pmu_power_key = input_allocate_device();
    if (!aml_pmu_power_key) {
        kfree(aml_pmu_power_key);
        return -ENODEV;
    }

    aml_pmu_power_key->name       = pdev->name;
    aml_pmu_power_key->phys       = "m1kbd/input2";
    aml_pmu_power_key->id.bustype = BUS_HOST;
    aml_pmu_power_key->id.vendor  = 0x0001;
    aml_pmu_power_key->id.product = 0x0001;
    aml_pmu_power_key->id.version = 0x0100;
    aml_pmu_power_key->open       = NULL;
    aml_pmu_power_key->close      = NULL;
    aml_pmu_power_key->dev.parent = &pdev->dev;

    set_bit(EV_KEY, aml_pmu_power_key->evbit);
    set_bit(EV_REL, aml_pmu_power_key->evbit);
    set_bit(KEY_POWER, aml_pmu_power_key->keybit);

    ret = input_register_device(aml_pmu_power_key);

    if (aml1212_init == NULL) {
        return -EINVAL;    
    }

    aml_pwr_key_work = create_singlethread_workqueue("aml_pwr_key");
    if (!aml_pwr_key_work) {
        AML_PMU_ERR("%s, create workqueue failed\n", __func__);
        return -ENOMEM;    
    }
    INIT_DELAYED_WORK(&pwr_key_work, pwr_key_work_func);
    queue_delayed_work(aml_pwr_key_work, &pwr_key_work, msecs_to_jiffies(2000));
#ifdef CONFIG_UBOOT_BATTERY_PARAMETERS 
    if (get_uboot_battery_para_status() == UBOOT_BATTERY_PARA_SUCCESS) {
        aml_pmu_battery = get_uboot_battery_para();
        AML_PMU_DBG("use uboot passed battery parameters\n");
    } else {
        aml_pmu_battery = aml1212_init->board_battery; 
        AML_PMU_DBG("uboot battery parameter not get, use BSP configed battery parameters\n");
    }
#else
    aml_pmu_battery = aml1212_init->board_battery; 
    AML_PMU_DBG("use BSP configed battery parameters\n");
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

    aml_pmu_register_driver(&aml1212_driver);
    g1212_supply = supply;
    check_pmu_version();
    charger = &supply->aml_charger;
    for (tmp2 = 1; tmp2 < 16; tmp2++) {
        if (!charger->ocv_empty && aml_pmu_battery->pmu_bat_curve[tmp2].discharge_percent > 0) {
            charger->ocv_empty = aml_pmu_battery->pmu_bat_curve[tmp2-1].ocv;
        }
        if (!charger->ocv_full && aml_pmu_battery->pmu_bat_curve[tmp2].discharge_percent == 100) {
            charger->ocv_full = aml_pmu_battery->pmu_bat_curve[tmp2].ocv;    
        }
        tmp_diff = aml_pmu_battery->pmu_bat_curve[tmp2].discharge_percent -
                   aml_pmu_battery->pmu_bat_curve[tmp2].charge_percent;
        if (tmp_diff > max_diff) {
            max_diff = tmp_diff;
        }
    }

    supply->irq = aml_pmu_battery->pmu_irq_id;

    supply->battery_info->technology         = aml_pmu_battery->pmu_battery_technology;
    supply->battery_info->voltage_max_design = aml_pmu_battery->pmu_init_chgvol;
    supply->battery_info->energy_full_design = aml_pmu_battery->pmu_battery_cap;
    supply->battery_info->voltage_min_design = charger->ocv_empty * 1000;
    supply->battery_info->use_for_apm        = 1;
    supply->battery_info->name               = aml_pmu_battery->pmu_battery_name;

    re_charge_cnt            = aml1212_init->charge_timeout_retry;
    charger->soft_limit_to99 = aml1212_init->soft_limit_to99;
    charger->coulomb_type    = COULOMB_SINGLE_CHG_DEC;
    if (supply->irq == AML_PMU_IRQ_NUM) {
        INIT_WORK(&supply->irq_work, aml_pmu_irq_work_func); 
        ret = request_irq(supply->irq, 
                          aml_pmu_irq_handler, 
                          IRQF_DISABLED | IRQF_SHARED,
                          AML1212_IRQ_NAME,
                          supply); 
        if (ret) {
            AML_PMU_DBG("request irq failed, ret:%d, irq:%d\n", ret, supply->irq);    
        }
    }

    ret = aml_pmu_first_init(supply);
    if (ret) {
        goto err_charger_init;
    }

    aml_pmu_battery_setup_psy(supply);
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

    ret = aml1212_supply_create_attrs(&supply->batt);
    if(ret){
        return ret;
    }

    platform_set_drvdata(pdev, supply);

    supply->interval = msecs_to_jiffies(AML_PMU_WORK_CYCLE);
    INIT_DELAYED_WORK(&supply->work, aml_pmu_charging_monitor);
    schedule_delayed_work(&supply->work, supply->interval);

#ifdef CONFIG_HAS_EARLYSUSPEND
    aml_pmu_early_suspend.suspend = aml_pmu_earlysuspend;
    aml_pmu_early_suspend.resume  = aml_pmu_lateresume;
    aml_pmu_early_suspend.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 2;
    aml_pmu_early_suspend.param   = supply;
    register_early_suspend(&aml_pmu_early_suspend);
    wake_lock_init(&aml1212_lock, WAKE_LOCK_SUSPEND, "aml1212");
#endif

#ifdef CONFIG_AMLOGIC_USB
    if (aml1212_late_job.flag) {     // do later job for usb charger detect
        aml1212_usb_charger(NULL, aml1212_late_job.value, NULL);
        aml1212_late_job.flag = 0;
    }
#endif
    power_supply_changed(&supply->batt);                   // update battery status
    
    aml_pmu_set_gpio(1, 0);                                 // open LCD backlight, test
    aml_pmu_set_gpio(2, 0);                                 // open VCCx2, test

    dump_pmu_register(NULL);
#ifdef CONFIG_RESET_TO_SYSTEM
    pmu_reboot_nb.notifier_call = aml_pmu_reboot_notifier;
    ret = register_reboot_notifier(&pmu_reboot_nb);
    if (ret) {
        AML_PMU_ERR("%s, register reboot notifier failed, ret:%d\n", __func__, ret);
    }
#endif
	AML_PMU_DBG("call %s exit, ret:%d", __func__, ret);
    return ret;

err_ps_register:
    free_irq(supply->irq, supply);
    cancel_delayed_work_sync(&supply->work);
    destroy_workqueue(aml_pwr_key_work);

err_charger_init:
    kfree(supply->battery_info);
    kfree(supply);
    input_unregister_device(aml_pmu_power_key);
    kfree(aml_pmu_power_key);
	AML_PMU_DBG("call %s exit, ret:%d", __func__, ret);
    return ret;
}

static int aml_pmu_battery_remove(struct platform_device *dev)
{
    struct aml1212_supply *supply= platform_get_drvdata(dev);

    cancel_work_sync(&supply->irq_work);
    cancel_delayed_work_sync(&supply->work);
    power_supply_unregister(&supply->usb);
    power_supply_unregister(&supply->ac);
    power_supply_unregister(&supply->batt);
    
    free_irq(supply->irq, supply);
    kfree(supply->battery_info);
    kfree(supply);
    input_unregister_device(aml_pmu_power_key);
    kfree(aml_pmu_power_key);
    destroy_workqueue(aml_pwr_key_work);

    return 0;
}


static int aml_pmu_suspend(struct platform_device *dev, pm_message_t state)
{
    struct aml1212_supply *supply  = platform_get_drvdata(dev);
    struct aml_charger    *charger = &supply->aml_charger;
    struct aml_pmu_api    *api;

    cancel_delayed_work_sync(&supply->work);
    if (supply->usb_connect_type != USB_BC_MODE_SDP) {
        aml_pmu_set_usb_current_limit(900, supply->usb_connect_type);  // not pc, set to 900mA when suspend
    } else {
        aml_pmu_set_usb_current_limit(500, supply->usb_connect_type);  // pc, limit to 500mA
    }

    api = aml_pmu_get_api();
    if (api && api->pmu_suspend_process) {
        api->pmu_suspend_process(charger);
    }
#ifdef CONFIG_HAS_EARLYSUSPEND
    if (early_power_status != supply->aml_charger.ext_valid) {
        AML_PMU_DBG("%s, power status changed, prev:%x, now:%x, exit suspend process\n",
                    __func__, early_power_status, supply->aml_charger.ext_valid);
        input_report_key(aml_pmu_power_key, KEY_POWER, 1);              // assume power key pressed 
        input_sync(aml_pmu_power_key);
        return -1;
    }
    in_early_suspend = 0;
#endif

    return 0;
}

static int aml_pmu_resume(struct platform_device *dev)
{
    struct   aml1212_supply *supply  = platform_get_drvdata(dev);
    struct   aml_charger    *charger = &supply->aml_charger;
    struct aml_pmu_api  *api;

    api = aml_pmu_get_api();
    if (api && api->pmu_resume_process) {
        api->pmu_resume_process(charger, aml_pmu_battery);
    }
    schedule_work(&supply->work.work);
#ifdef CONFIG_RESET_TO_SYSTEM
	aml_pmu_write(0x00ff, 0x01); // cann't reset after resume
#endif
    return 0;
}

static void aml_pmu_shutdown(struct platform_device *dev)
{
#if 0
    uint8_t tmp;
    struct aml1212_supply *supply= platform_get_drvdata(dev);
    
    // add code here
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
    wake_lock_destroy(&aml1212_lock);
#endif
}

static struct platform_driver aml_pmu_battery_driver = {
    .driver = {
        .name  = AML1212_SUPPLY_NAME, 
        .owner = THIS_MODULE,
    },
    .probe    = aml_pmu_battery_probe,
    .remove   = aml_pmu_battery_remove,
    .suspend  = aml_pmu_suspend,
    .resume   = aml_pmu_resume,
    .shutdown = aml_pmu_shutdown,
};

static int aml_pmu_battery_init(void)
{
    int ret;
    ret = platform_driver_register(&aml_pmu_battery_driver);
	AML_PMU_DBG("call %s, ret = %d\n", __func__, ret);
	return ret;
}

static void aml_pmu_battery_exit(void)
{
    platform_driver_unregister(&aml_pmu_battery_driver);
}

module_init(aml_pmu_battery_init);
module_exit(aml_pmu_battery_exit);

MODULE_DESCRIPTION("Amlogic PMU AML1212 battery driver");
MODULE_AUTHOR("tao.zeng@amlogic.com, Amlogic, Inc");
MODULE_LICENSE("GPL");
