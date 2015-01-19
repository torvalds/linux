/*
 * Implement driver of RN5T618 PMU
 * Author: tao.zeng@amlogic.com
 *
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
#include <linux/i2c.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/input.h>
#include <mach/am_regs.h>
#include <mach/irqs.h>
#include <mach/gpio.h>
#include <linux/amlogic/aml_rtc.h>
#include <linux/amlogic/ricoh_pmu.h>
#include <mach/usbclock.h>
#include <linux/reboot.h>
#include <linux/notifier.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/wakelock_android.h>
#include <linux/earlysuspend.h>
#endif

#ifdef CONFIG_UBOOT_BATTERY_PARAMETERS
#include <linux/amlogic/battery_parameter.h>
#endif

#define CHECK_DRIVER()      \
    if (!g_rn5t618_supply) {        \
        RICOH_INFO("driver is not ready right now, wait...\n");   \
        dump_stack();       \
        return -ENODEV;     \
    }

#define EXT_POWER_LOW_THRESHOLD             4500
#define EXT_POWER_HIGH_THRESHOLD            4600

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend rn5t618_early_suspend;
static int in_early_suspend = 0;
static struct wake_lock rn5t618_lock;
#endif
struct rn5t618_supply      *g_rn5t618_supply  = NULL;
struct ricoh_pmu_init_data *g_rn5t618_init    = NULL;
struct battery_parameter   *rn5t618_battery   = NULL;
struct input_dev           *rn5t618_power_key = NULL;
static int rn5t618_curr_dir   = 0;
static int power_protection   = 0;
static int over_discharge_cnt = 0;

#ifdef CONFIG_AMLOGIC_USB
struct later_job {
    int flag;
    int value;
};
static struct later_job rn5t618_charger_job = {};
static struct later_job rn5t618_otg_job = {};
static int rn5t618_otg_value = -1;
static int otg_mask = 0;
#endif

static int rn5t618_update_state(struct aml_charger *charger);

int rn5t618_get_battery_voltage(void)
{
    uint8_t val[2];
    int result;

    rn5t618_set_bits(0x0066, 0x01, 0x07);                   // select vbat channel
    udelay(200);
    rn5t618_reads(0x006A, val, 2);
    result = (val[0] << 4) | (val[1] & 0x0f);
    result = (result * 5000) / 4096;                        // resolution: 1.221mV
    return result;
}
EXPORT_SYMBOL_GPL(rn5t618_get_battery_voltage);

int rn5t618_get_vbus_voltage(void)
{
    uint8_t val[2];
    int result;

    rn5t618_set_bits(0x0066, 0x03, 0x07);                   // select vbat channel
    udelay(200);
    rn5t618_reads(0x006E, val, 2);
    result = (val[0] << 4) | (val[1] & 0x0f);
    result = (result * 7500) / 4096;                        // resolution
    return result;
}

int rn5t618_get_dcin_voltage(void)
{
    uint8_t val[2];
    int result;

    rn5t618_set_bits(0x0066, 0x02, 0x07);                   // select vbat channel
    udelay(200);
    rn5t618_reads(0x006c, val, 2);
    result = (val[0] << 4) | (val[1] & 0x0f);
    result = (result * 7500) / 4096;                        // resolution
    return result;
}

int rn5t618_get_battery_temperature(void)
{
    uint8_t val[2];
    int result;

    rn5t618_reads(0x0072, val, 2);
    result = ((val[0] & 0xf) << 4) | val[1];
    result = result / 16;

    /*
     * TODO: need add method to calculate temperature according voltage
     */
    return result;
}
EXPORT_SYMBOL_GPL(rn5t618_get_battery_temperature);

int rn5t618_get_battery_current(void)
{
    uint8_t val[2];
    int result;

#if 0
    if (g_rn5t618_supply->charge_status == CHARGER_CHARGING) {
       rn5t618_set_bits(0x0066, 0x00, 0x07);                    // select vbat channel
       udelay(200);
       rn5t618_reads(0x0068, val, 2);
       result = (val[0] << 4) | (val[1] & 0x0f);
       result = (result * 5000) / 4096;                         // resolution: 1.221mA
    } else {
#else
        rn5t618_reads(0x00FB, val, 2);
        result = ((val[0] & 0x3f) << 8) | (val[1]);
        if (result & 0x2000) {                                  // discharging, complement code
            result = result ^ 0x3fff;
            rn5t618_curr_dir = -1;
        } else {
            rn5t618_curr_dir = 1;
        }
#if 0
    }
#endif
#endif

    return result;
}
EXPORT_SYMBOL_GPL(rn5t618_get_battery_current);

int rn5t618_get_charge_status()
{
    uint8_t val;
    rn5t618_read(0x00BD, &val);
    return val & 0x1f;
}
EXPORT_SYMBOL_GPL(rn5t618_get_charge_status);

int rn5t618_set_gpio(int gpio, int output)
{
    int val = output ? 1 : 0;
    if (gpio < 0 || gpio > 3) {
        RICOH_ERR("%s, wrong input of GPIO:%d\n", __func__, gpio);
        return -1;
    }
    RICOH_DBG("%s, gpio:%d, output:%d\n", __func__, gpio, output);
    rn5t618_set_bits(0x0091, val << gpio, 1 << gpio);       // set out put value
    rn5t618_set_bits(0x0090, 1 << gpio, 1 << gpio);         // set pin to output mode
    return 0;
}
EXPORT_SYMBOL_GPL(rn5t618_set_gpio);

int rn5t618_get_gpio(int gpio, int *val)
{
    int value;
    if (gpio < 0 || gpio > 3) {
        RICOH_ERR("%s, wrong input of GPIO:%d\n", __func__, gpio);
        return -1;
    }
    rn5t618_read(0x0097, (uint8_t *)&value);                    // read status
    *val = (value & (1 << gpio)) ? 1 : 0;
    return 0;
}
EXPORT_SYMBOL_GPL(rn5t618_get_gpio);

void rn5t618_power_off()
{
    if (g_rn5t618_init->reset_to_system) {
        rn5t618_set_bits(0x0007, 0x00, 0x01);    
    }
    rn5t618_set_gpio(0, 1);
    rn5t618_set_gpio(1, 1);
    msleep(100);
    rn5t618_set_bits(0x00EF, 0x00, 0x10);                       // disable coulomb counter
    rn5t618_set_bits(0x00E0, 0x00, 0x01);                       // disable fuel gauge
    RICOH_INFO("%s, send power off command\n", __func__);
    rn5t618_set_bits(0x000f, 0x00, 0x01);                       // do not re-power-on system
    rn5t618_set_bits(0x000E, 0x01, 0x01);                       // software power off PMU
    udelay(1000);
    while (1) {
        msleep(1000);
        RICOH_ERR("%s, error\n", __func__);
    }
}
EXPORT_SYMBOL_GPL(rn5t618_power_off);

int rn5t618_set_usb_current_limit(int limit)
{
    int val;
    if ((limit < 100 || limit > 1500) && limit != -1){
        RICOH_ERR("%s, wrong usb current limit:%d\n", __func__, limit);
        return -1;
    }
    if (limit == -1) {                                          // -1 means not limit
        val   = 0x0E;                                           // RN5T618 max usb vbus current is 1.5A 
        limit = 1500;
    } else {
        val = (limit / 100) - 1;
    }
    RICOH_INFO("%s, set usb current limit to %d mA\n", __func__, limit);
    return rn5t618_set_bits(0x00B7, val, 0x1f);
}
EXPORT_SYMBOL_GPL(rn5t618_set_usb_current_limit);

int rn5t618_set_dcin_current_limit(int limit)
{
    int val;
    if (limit < 100 || limit > 2500) {
        RICOH_ERR("%s, wrong usb current limit:%d\n", __func__, limit);
        return -1;
    }
    val = (limit / 100) - 1;
    RICOH_DBG("%s, set dcin current limit to %d\n", __func__, limit);
    return rn5t618_set_bits(0x00B6, val, 0x1f);
}
EXPORT_SYMBOL_GPL(rn5t618_set_dcin_current_limit);

int rn5t618_set_usb_voltage_limit(int voltage)
{
    int bits;

    if (voltage < 4100 || voltage > 4400) {
        RICOH_ERR("%s, invalid input voltage:%d\n", __func__, voltage);
        return -EINVAL;
    }
    bits = ((voltage - 4100) / 100) << 2;
    return rn5t618_set_bits(0x00B4, bits, 0x0c);
}
EXPORT_SYMBOL_GPL(rn5t618_set_usb_voltage_limit);

int rn5t618_set_charge_enable(int enable)
{
    int bits = enable ? 0x03 : 0x00;

    return rn5t618_set_bits(0x00B3, bits, 0x03); 
}
EXPORT_SYMBOL_GPL(rn5t618_set_charge_enable);

int rn5t618_set_charge_current(int curr)
{
    int bits;

    if (curr < 0 || curr > 1800000) {
        RICOH_ERR("%s, invalid charge current:%d\n", __func__, curr);
        return -EINVAL;
    }
    if (curr > 100) {                           // input is uA
        curr = curr / 1000;
    } else {                                    // input is charge ratio
        curr = (curr * rn5t618_battery->pmu_battery_cap) / 100 + 100; 
    }    
    if (curr > 1600) {
        // for safety, do not let charge current large than 90% of max charge current
        curr = 1600;    
    }
    bits = (curr - 100) / 100;
    return rn5t618_set_bits(0x00B8, bits, 0x1f);
}
EXPORT_SYMBOL_GPL(rn5t618_set_charge_current);

int rn5t618_set_trickle_time(int minutes)
{
    int bits;

    if (minutes != 40 && minutes != 80) {
        RICOH_ERR("%s, invalid trickle time:%d\n", __func__, minutes);
        return -EINVAL;
    }
    bits = (minutes == 40) ? 0x00 : 0x10;
    return rn5t618_set_bits(0x00B9, bits, 0x30);
}

int rn5t618_set_long_press_time(int ms)
{
    int bits;

    if (ms < 1000 || ms > 12000) {
        RICOH_ERR("%s, invalid long press time:%d\n", __func__, ms);
        return -EINVAL;
    }
    switch(ms) {
    case  1000: bits = 0x10; break;
    case  2000: bits = 0x20; break;
    case  4000: bits = 0x30; break;
    case  6000: bits = 0x40; break;
    case  8000: bits = 0x50; break;
    case 10000: bits = 0x60; break;
    case 12000: bits = 0x70; break;
    default : return -EINVAL;
    } 
    return rn5t618_set_bits(0x0010, bits, 0x70);
}

int rn5t618_set_rapid_time(int minutes)
{
    int bits;

    if (minutes > 300 || minutes < 120) {
        RICOH_ERR("%s, invalid rapid charge time:%d\n", __func__, minutes);    
        return -EINVAL;
    }
    bits = (minutes - 120) / 60;
    return rn5t618_set_bits(0x00B9, bits, 0x03);
}

int rn5t618_set_full_charge_voltage(int voltage)
{
    int bits;

    if (voltage > 4350 * 1000 || voltage < 4050 * 1000) {
        RICOH_ERR("%s, invalid target charge voltage:%d\n", __func__, voltage);
        return -EINVAL;
    }
    if (voltage == 4350000) {
        bits = 0x40;    
    } else {
        bits = ((voltage - 4050000) / 50000) << 4;
    }
    return rn5t618_set_bits(0x00BB, bits, 0x70);
}

int rn5t618_set_charge_end_current(int curr)
{
    int bits;

    if (curr < 50000 || curr > 200000) {
        RICOH_ERR("%s, invalid charge end current:%d\n", __func__, curr);
    }
    bits = (curr / 50000 - 1) << 6;
    return rn5t618_set_bits(0x00B8, bits, 0xc0);
}

int rn5t618_set_recharge_voltage(int voltage)
{
    int bits;

    if (voltage < 3850 || voltage > 4100) {
        RICOH_ERR("%s, invalid recharge volatage:%d\n", __func__, voltage);
        return -EINVAL;
    }
    if (voltage == 4100) {
        bits = 0x04;    
    } else {
        bits = ((voltage - 3850) / 50);
    }
    return rn5t618_set_bits(0x00BB, bits, 0x07);
}

int rn5t618_get_coulomber_counter(void)
{
    uint8_t val[3];
    int result;

    result = rn5t618_reads(0x00F0, val, 3);
    if (result) {
        RICOH_ERR("%s, failed: %d\n", __func__, __LINE__);
        return result;
    }

    result = val[2] | (val[1] << 8) | (val[0] << 16);
    RICOH_DBG("%s, counter:%d\n", __func__, result);
    return result;
}

/*
 * get saved coulomb counter form PMU registers for RICOH
 */
static int rn5t618_save_coulomb = 0;
static int rn5t618_coulomb_flag = 0;
#define POWER_OFF_FLAG          0x40
#define REBOOT_FLAG             0x20
int rn5t618_get_saved_coulomb(void)
{
    uint8_t val[4];

    rn5t618_read(0x01, &val[0]);
    if (val[0] <= 0x06) {
        RICOH_DBG("Chip version is RN5T618F, nothing todo\n");
        rn5t618_coulomb_flag = REBOOT_FLAG; 
        return -1;
    }
    rn5t618_read(0x07, &val[0]);
    rn5t618_coulomb_flag = (val[0] & 0x60);
    RICOH_DBG("coulomb_flag:0x%02x\n", rn5t618_coulomb_flag);
    if (rn5t618_coulomb_flag & POWER_OFF_FLAG) {
        rn5t618_write(0x00ff, 0x01);                                // register bank set to 1
        rn5t618_read(0x00bd, &val[0]);
        rn5t618_read(0x00bf, &val[1]);
        rn5t618_read(0x00c1, &val[2]);
        rn5t618_read(0x00c3, &val[3]);
        rn5t618_save_coulomb = val[3] | (val[2] << 8) | (val[1] << 16) | (val[0] << 24);
        RICOH_DBG("saved coulomb counter:0x%02x %02x %02x %02x\n",
                  val[0], val[1], val[2], val[3]);
        rn5t618_write(0x00ff, 0x00);                                // register bank set to 0
        rn5t618_set_bits(0x0007, 0x00, 0x60);                       // clear flag
    } else {
        RICOH_DBG("no saved coulomb counter\n");    
        return -1;
    }
    return 0;
}

static int rn5t618_get_coulomber(struct aml_charger *charger)
{
    uint8_t val[4];
    int result;

    result = rn5t618_reads(0x00F3, val, 4);
    if (result) {
        RICOH_ERR("%s, failed: %d\n", __func__, __LINE__);
        return result;
    }

    result = val[3] | (val[2] << 8) | (val[1] << 16) | (val[0] << 24);
    result = result / (3600);                                           // to mAh
    if (rn5t618_coulomb_flag & POWER_OFF_FLAG) {
        /*
         * use saved coulomb registers
         */
        RICOH_DBG("saved   coulomb:%02x %02x %02x %02x, %dmah\n",
                 (rn5t618_save_coulomb >> 24) & 0xff,
                 (rn5t618_save_coulomb >> 16) & 0xff,
                 (rn5t618_save_coulomb >>  8) & 0xff,
                 (rn5t618_save_coulomb >>  0) & 0xff,
                  rn5t618_save_coulomb / 3600);    
        RICOH_DBG("current coulomb:%02x %02x %02x %02x, %dmah\n",
                  val[0], val[1], val[2], val[3],
                  result);
        rn5t618_save_coulomb = rn5t618_save_coulomb / 3600;
        charger->charge_cc   = rn5t618_save_coulomb + result;
        rn5t618_coulomb_flag = 0;
    } else {
        charger->charge_cc   = result;
    }
    charger->discharge_cc = 0;
    return 0;
}

static int rn5t618_clear_coulomber(struct aml_charger *charger)
{
    return rn5t618_set_bits(0x00EF, 0x08, 0x08);    
}

int rn5t618_get_battery_percent(void)
{
    CHECK_DRIVER();
    return g_rn5t618_supply->aml_charger.rest_vol;    
}
EXPORT_SYMBOL_GPL(rn5t618_get_battery_percent);

#ifdef CONFIG_RESET_TO_SYSTEM
void rn5t618_feed_watchdog(void)
{
    volatile uint8_t wdt;
    rn5t618_read(0x0b, &wdt);
    rn5t618_write(0x13, 0x00);
}
#endif

int rn5t618_first_init(struct rn5t618_supply *supply)
{
    /*
     * initialize charger from battery parameters
     */
    if (rn5t618_battery) {
        rn5t618_set_charge_current     (rn5t618_battery->pmu_init_chgcur);
        rn5t618_set_full_charge_voltage(rn5t618_battery->pmu_init_chgvol);
        rn5t618_set_charge_end_current (150000);
        rn5t618_set_trickle_time       (rn5t618_battery->pmu_init_chg_pretime);
        rn5t618_set_rapid_time         (rn5t618_battery->pmu_init_chg_csttime);
        rn5t618_set_charge_enable      (rn5t618_battery->pmu_init_chg_enabled);
        rn5t618_set_long_press_time    (rn5t618_battery->pmu_pekoff_time);
        rn5t618_set_recharge_voltage   (4100);

        if (rn5t618_battery->pmu_usbvol_limit) {
            rn5t618_set_usb_voltage_limit(rn5t618_battery->pmu_usbvol); 
        }
      //if (rn5t618_battery->pmu_usbcur_limit) {                            // should add it ?
      //    rn5t618_set_usb_current_limit(rn5t618_battery->pmu_usbcur);
      //}
    }

    return 0;
}

static enum power_supply_property rn5t618_battery_props[] = {
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

static enum power_supply_property rn5t618_ac_props[] = {
    POWER_SUPPLY_PROP_MODEL_NAME,
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_ONLINE,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW,
};

static enum power_supply_property rn5t618_usb_props[] = {
    POWER_SUPPLY_PROP_MODEL_NAME,
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_ONLINE,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW,
};

static void rn5t618_battery_check_status(struct rn5t618_supply       *supply,
                                         union  power_supply_propval *val)
{
    struct aml_charger *charger = &supply->aml_charger;

    if (!rn5t618_battery) {
        val->intval = POWER_SUPPLY_STATUS_UNKNOWN;                      // for no battery case
    } else {
        if (charger->bat_det) {
            if (charger->ext_valid) {
                if (charger->rest_vol == 100) {
                    val->intval = POWER_SUPPLY_STATUS_FULL;
                } else if (charger->rest_vol == 0 && 
                           charger->charge_status == CHARGER_DISCHARGING) {   // protect for over-discharging
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
}

static void rn5t618_battery_check_health(struct rn5t618_supply       *supply,
                                         union  power_supply_propval *val)
{
    int status = supply->aml_charger.fault & 0x1f;

    if ((status == RN5T618_DIE_ERROR) || 
        (status == RN5T618_DIE_SHUTDOWN)) {
        RICOH_ERR("%s, BATTERY DEAD, fault:0x%x\n", __func__, status);
        val->intval = POWER_SUPPLY_HEALTH_DEAD;
    } else if (status == RN5T618_BATTERY_TEMPERATURE_ERROR) {
        RICOH_ERR("%s, BATTERY OVERHEAT, fault:0x%x, temperature:%d\n", 
                  __func__, status, rn5t618_get_battery_temperature());
        val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
    } else if ((status == RN5T618_CHARGE_OVER_VOLTAGE) || 
               (status == RN5T618_BATTERY_OVER_VOLTAGE)) {
        RICOH_ERR("%s, BATTERY OVERVOLTAGE, fault:0x%x, voltage:%d\n",
                  __func__, status, rn5t618_get_battery_voltage());
        val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
    } else if ((status == RN5T618_BATTERY_ERROR) ||
               (status == RN5T618_NO_BATTERY   ) ||
               (status == RN5T618_NO_BATTERY2  )) {
        RICOH_ERR("%s, BATTERY UNSPEC FAILURE, fault:0x%x\n", __func__, status);
        val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;    
    } else {
        val->intval = POWER_SUPPLY_HEALTH_GOOD;
    }
}

static int rn5t618_battery_get_property(struct power_supply *psy,
                                        enum   power_supply_property psp,
                                        union  power_supply_propval *val)
{
    struct rn5t618_supply *supply;
    struct aml_charger    *charger;
    int ret = 0;
    int sign_bit = 1;
    supply  = container_of(psy, struct rn5t618_supply, batt);
    charger = &supply->aml_charger;
    
    switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:
        rn5t618_battery_check_status(supply, val);
        break;

    case POWER_SUPPLY_PROP_HEALTH:
        rn5t618_battery_check_health(supply, val);
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
        if (ABS(charger->ibat) > 20 && charger->charge_status != CHARGER_NONE) {
            if (charger->charge_status == CHARGER_CHARGING) {
                sign_bit = 1;    
            } else if (charger->charge_status == CHARGER_DISCHARGING) {
                sign_bit = -1;
            }
            val->intval = charger->ibat * 1000 * sign_bit; 
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
        if (rn5t618_battery) {
            val->intval = charger->rest_vol;
        } else {
            val->intval = 100;    
        }
        break;

    case POWER_SUPPLY_PROP_ONLINE:
        if (rn5t618_battery) {
            val->intval = charger->bat_det; 
        } else {
            val->intval = 0;    
        }
        break;

    case POWER_SUPPLY_PROP_PRESENT:
        if (rn5t618_battery) {
            val->intval = charger->bat_det; 
        } else {
            val->intval = 0;    
        }
        break;

    case POWER_SUPPLY_PROP_TEMP:
      //val->intval = rn5t618_get_battery_temperature();
        val->intval = 300; 
        break;
    default:
        ret = -EINVAL;
        break;
    }
    
    return ret;
}

static int rn5t618_ac_get_property(struct power_supply *psy,
                                   enum   power_supply_property psp,
                                   union  power_supply_propval *val)
{
    struct rn5t618_supply *supply;
    struct aml_charger    *charger;
    int ret = 0;
    supply  = container_of(psy, struct rn5t618_supply, ac);
    charger = &supply->aml_charger;

    switch(psp){
    case POWER_SUPPLY_PROP_MODEL_NAME:
        val->strval = supply->ac.name;
        break;

    case POWER_SUPPLY_PROP_PRESENT:
        if (power_protection) {   // protect for over-discharging
            val->intval = 0;
        } else {
            val->intval = charger->dcin_valid;
        }
        break;

    case POWER_SUPPLY_PROP_ONLINE:
        if (power_protection) {   // protect for over-discharging
            val->intval = 0;
        } else {
            val->intval = charger->dcin_valid;
        }
        break;

    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        val->intval = rn5t618_get_dcin_voltage() * 1000;
        break;

    case POWER_SUPPLY_PROP_CURRENT_NOW:
        val->intval = 1000 * 1000;
        break;

    default:
        ret = -EINVAL;
        break;
    }
    return ret;
}

static int rn5t618_usb_get_property(struct power_supply *psy,
           enum power_supply_property psp,
           union power_supply_propval *val)
{
    struct rn5t618_supply *supply;
    struct aml_charger    *charger;
    int ret = 0;
    supply  = container_of(psy, struct rn5t618_supply, usb);
    charger = &supply->aml_charger;
    
    switch(psp){
    case POWER_SUPPLY_PROP_MODEL_NAME:
        val->strval = supply->usb.name;
        break;

    case POWER_SUPPLY_PROP_PRESENT:
        if (power_protection) {                                         // over-disharging
            val->intval = 0;
        } else {
            val->intval = charger->usb_valid;
        }
        break;

    case POWER_SUPPLY_PROP_ONLINE:
        if (power_protection) {                                         // over-discharging
            val->intval = 0;
        } else {
            val->intval = charger->usb_valid;
        }
        break;

    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        val->intval = rn5t618_get_vbus_voltage() * 1000;
        break;

    case POWER_SUPPLY_PROP_CURRENT_NOW:
        val->intval = 1000 * 1000;      // charger->iusb * 1000;
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

static void rn5t618_battery_setup_psy(struct rn5t618_supply *supply)
{
    struct power_supply      *batt = &supply->batt;
    struct power_supply      *ac   = &supply->ac;
    struct power_supply      *usb  = &supply->usb;
    struct power_supply_info *info =  supply->battery_info;
    
    batt->name           = "battery";
    batt->use_for_apm    = info->use_for_apm;
    batt->type           = POWER_SUPPLY_TYPE_BATTERY;
    batt->get_property   = rn5t618_battery_get_property;
    batt->properties     = rn5t618_battery_props;
    batt->num_properties = ARRAY_SIZE(rn5t618_battery_props);
    
    ac->name             = "ac";
    ac->type             = POWER_SUPPLY_TYPE_MAINS;
    ac->get_property     = rn5t618_ac_get_property;
    ac->supplied_to      = supply_list;
    ac->num_supplicants  = ARRAY_SIZE(supply_list);
    ac->properties       = rn5t618_ac_props;
    ac->num_properties   = ARRAY_SIZE(rn5t618_ac_props);
    
    usb->name            = "usb";
    usb->type            = POWER_SUPPLY_TYPE_USB;
    usb->get_property    = rn5t618_usb_get_property;
    usb->supplied_to     = supply_list,
    usb->num_supplicants = ARRAY_SIZE(supply_list),
    usb->properties      = rn5t618_usb_props;
    usb->num_properties  = ARRAY_SIZE(rn5t618_usb_props);
}

#ifdef CONFIG_AMLOGIC_USB
int rn5t618_otg_change(struct notifier_block *nb, unsigned long value, void *pdata)
{
    uint8_t val;
    if (!g_rn5t618_supply) {
        RICOH_INFO("%s, driver is not ready, do it later\n", __func__);
        rn5t618_otg_job.flag  = 1;
        rn5t618_otg_job.value = value;
        return 0;
    }
    rn5t618_otg_value = value;
    RICOH_INFO("%s, value:%d, is_short:%d\n", __func__, rn5t618_otg_value, g_rn5t618_init->vbus_dcin_short_connect);
    if (rn5t618_otg_value) {
        rn5t618_read(0xB3, &val);
        if (g_rn5t618_init->vbus_dcin_short_connect) {
            otg_mask = 1;
            val |= 0x08; 
            rn5t618_set_charge_enable(0);
            rn5t618_set_dcin_current_limit(100);
        } else {
            val |= 0x10; 
        }
        RICOH_DBG("set boost en bit, val:%x\n", val);
        rn5t618_write(0xB3, val);
    } else {
        rn5t618_read(0xB3, &val);
        if (g_rn5t618_init->vbus_dcin_short_connect) {
            otg_mask = 0;
            val &= ~0x08; 
            rn5t618_set_charge_enable(1);
            rn5t618_set_dcin_current_limit(2500); 
        } else {
            val &= ~0x10; 
        }
        printk("[RN5T618] clear boost en bit, val:%x\n", val);
        rn5t618_write(0xB3, val);
    }
    rn5t618_read(0xB3, &val);
    printk("register 0xB3:%02x\n", val);
    rn5t618_update_state(&g_rn5t618_supply->aml_charger);
    power_supply_changed(&g_rn5t618_supply->batt);
    mdelay(100);
    return 0;
}

int rn5t618_usb_charger(struct notifier_block *nb, unsigned long value, void *pdata)
{
    if (!g_rn5t618_supply) {
        RICOH_INFO("%s, driver is not ready, do it later\n", __func__);
        rn5t618_charger_job.flag  = 1;
        rn5t618_charger_job.value = value;
        return 0;
    }
    switch (value) {
    case USB_BC_MODE_DISCONNECT:                                        // disconnect
    case USB_BC_MODE_SDP:                                               // pc
        if (rn5t618_battery && rn5t618_battery->pmu_usbcur_limit) {     // limit usb current
            rn5t618_set_usb_current_limit(rn5t618_battery->pmu_usbcur); 
        }
        break;

    case USB_BC_MODE_DCP:                                               // charger
    case USB_BC_MODE_CDP:                                               // PC + charger
        if (rn5t618_battery) {                                          // limit usb current
            rn5t618_set_usb_current_limit(-1);                          // not limit usb current
        }
        break;
        
    default:
        break;
    }
    return 0;
}
#endif

/*
 * add for debug 
 */
int printf_usage(void)
{

    printk(" \n"
           "usage:\n"
           "echo [r/w/] [addr] [value] > pmu_reg\n"
           "Example:\n"
           "   echo r 0x33 > pmu_reg        ---- read  register 0x33\n"
           "   echo w 0x33 0xa5 > pmu_reg   ---- write register 0x33 to 0xa5\n"
           " \n");
    return 0;
}

static ssize_t pmu_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return printf_usage(); 
}

static ssize_t pmu_reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret;
    int addr;
    uint8_t value;
    char *arg[3] = {}, *para, *buf_work, *p;
    int i;

    buf_work = kstrdup(buf, GFP_KERNEL);
    p = buf_work;
    for (i = 0; i < 3; i++) {
        para = strsep(&p, " ");
        if (para == NULL) {
            break;
        }
        arg[i] = para;
    }
    if (i < 2 || i > 3) {
        ret = 1;
        goto error;
    }
    switch (arg[0][0]) {
    case 'r':
        addr = simple_strtoul(arg[1], NULL, 16);
        ret = rn5t618_read(addr, &value);
        if (!ret) {
            printk("reg[0x%02x] = 0x%02x\n", addr, value);
        }
        break;

    case 'w':
        if (i != 3) {                       // parameter is not enough
            ret = 1;
            break;
        }
        addr  = simple_strtoul(arg[1], NULL, 16);
        value = simple_strtoul(arg[2], NULL, 16);
        ret = rn5t618_write(addr, value);
        if (!ret) {
            printk("set reg[0x%02x] to 0x%02x\n", addr, value);
        }
        break;

    default:
        ret = 1;
        break;
    }
error:
    kfree(buf_work);
    if (ret == 1) {
        printf_usage();
    }
    return count;
}

static ssize_t aml_pmu_vddao_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    uint8_t data;
    rn5t618_read(0x37, &data);

    return sprintf(buf, "Voltage of VDD_AO = %4dmV\n", (6000 + 125 * data) / 10);
}

static ssize_t aml_pmu_vddao_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
#if 0               // do not do this right now
    uint32_t data = simple_strtoul(buf, NULL, 10);

    if (data > 3500 || data < 600) {
        RICOH_ERR("Invalid input value = %d\n", data);
        return -1;
    }
    RICOH_DBG("Set VDD_AO to %4d mV\n", data);
    rn5t618_set_dcdc_voltage(2, data);
#endif
    return count; 
}

static ssize_t driver_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "RICOH PMU RN5T618 driver version is %s, build time:%s\n", 
                   RN5T618_DRIVER_VERSION, init_uts_ns.name.version);
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
    rn5t618_power_off();
    return count; 
}

static ssize_t charge_timeout_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    uint8_t val = 0;

    rn5t618_read(0x00B9, &val);
    val &=0x03;
    return sprintf(buf, "charge timeout is %d minutes\n", val * 60 + 120);   
}

static ssize_t charge_timeout_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{ 
    uint32_t data = simple_strtoul(buf, NULL, 10);
    if (data > 300 || data < 120) {
        RICOH_ERR("Invalid input value = %d\n", data);
        return -1;
    }
    RICOH_DBG("Set charge timeout to %4d minutes\n", data);
    rn5t618_set_rapid_time(data); 
    return count; 
}

int rn5t618_dump_all_register(char *buf)
{
    uint8_t val[16];
    int     i;
    int     size = 0;

    if (!buf) {
        printk(KERN_DEBUG "[RN5T618] DUMP ALL REGISTERS:\n");
        for (i = 0; i < 16; i++) {
            rn5t618_reads(i*16, val, 16);
            printk(KERN_DEBUG "0x%02x - %02x: ", i * 16, i * 16 + 15);
            printk("%02x %02x %02x %02x ",   val[0],  val[1],  val[2],  val[3]);
            printk("%02x %02x %02x %02x   ", val[4],  val[5],  val[6],  val[7]);
            printk("%02x %02x %02x %02x ",   val[8],  val[9],  val[10], val[11]);
            printk("%02x %02x %02x %02x\n",  val[12], val[13], val[14], val[15]);
        }
        return 0;
    }
    size += sprintf(buf + size, "%s", "[RN5T618] DUMP ALL REGISTERS:\n");
    for (i = 0; i < 16; i++) {
        rn5t618_reads(i*16, val, 16);
        size += sprintf(buf + size, "0x%02x - %02x: ", i * 16, i * 16 + 15);
        size += sprintf(buf + size, "%02x %02x %02x %02x ",   val[0],  val[1],  val[2],  val[3]);
        size += sprintf(buf + size, "%02x %02x %02x %02x   ", val[4],  val[5],  val[6],  val[7]);
        size += sprintf(buf + size, "%02x %02x %02x %02x ",   val[8],  val[9],  val[10], val[11]);
        size += sprintf(buf + size, "%02x %02x %02x %02x\n",  val[12], val[13], val[14], val[15]);
    }
    return size;
}

static ssize_t dump_pmu_regs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int size;
    size = rn5t618_dump_all_register(buf);
    size += sprintf(buf + size, "%s", "[RN5T618] DUMP ALL REGISTERS OVER!\n"); 
    return size;
}
static ssize_t dump_pmu_regs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return count;                                           /* nothing to do        */
}

static ssize_t dbg_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply   *battery = dev_get_drvdata(dev);
    struct rn5t618_supply *supply = container_of(battery, struct rn5t618_supply, batt); 
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
    struct rn5t618_supply *supply  = container_of(battery, struct rn5t618_supply, batt); 
    struct aml_charger    *charger = &supply->aml_charger;
    int i = 0; 
    int size;

    if (!rn5t618_battery) {
        return sprintf(buf, "No battery parameter find\n");
    }
    size = sprintf(buf, "\n i,      ocv,    charge,  discharge,\n");
    for (i = 0; i < 16; i++) {
        size += sprintf(buf + size, "%2d,     %4d,       %3d,        %3d,\n",
                        i, 
                        rn5t618_battery->pmu_bat_curve[i].ocv,
                        rn5t618_battery->pmu_bat_curve[i].charge_percent,
                        rn5t618_battery->pmu_bat_curve[i].discharge_percent);
    }
    size += sprintf(buf + size, "\nBattery capability:%4d@3700mAh, RDC:%3d mohm\n", 
                                rn5t618_battery->pmu_battery_cap, 
                                rn5t618_battery->pmu_battery_rdc);
    size += sprintf(buf + size, "Charging efficiency:%3d%%, capability now:%3d%%\n", 
                                rn5t618_battery->pmu_charge_efficiency,
                                charger->rest_vol);
    size += sprintf(buf + size, "ocv_empty:%4d, ocv_full:%4d\n\n",
                                charger->ocv_empty, 
                                charger->ocv_full);
    return size;
}

static ssize_t battery_para_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return count;                                           /* nothing to do        */    
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
        RICOH_ERR("input too large, failed to set report_delay\n");
        return count;
    }    
    if (api && api->pmu_set_report_delay) {
        api->pmu_set_report_delay(tmp);
    } else {
        RICOH_ERR("API not found\n");
    }    
    return count;
}

static struct device_attribute rn5t618_supply_attrs[] = {
    RICOH_ATTR(pmu_reg),
    RICOH_ATTR(aml_pmu_vddao),
    RICOH_ATTR(dbg_info),
    RICOH_ATTR(battery_para),
    RICOH_ATTR(report_delay),
    RICOH_ATTR(driver_version),
    RICOH_ATTR(clear_rtc_mem),
    RICOH_ATTR(charge_timeout),
    RICOH_ATTR(dump_pmu_regs),
};

int rn5t618_supply_create_attrs(struct power_supply *psy)
{
    int j,ret;
    for (j = 0; j < ARRAY_SIZE(rn5t618_supply_attrs); j++) {
        ret = device_create_file(psy->dev, &rn5t618_supply_attrs[j]);
        if (ret)
            goto sysfs_failed;
    }
    goto succeed;

sysfs_failed:
    while (j--) {
        device_remove_file(psy->dev, &rn5t618_supply_attrs[j]);
    }
succeed:
    return ret;
}

int rn5t618_cal_ocv(int ibat, int vbat, int dir)
{
    int result;

    if (dir == RN5T618_CHARGER_CHARGING && rn5t618_battery) {           // charging
        result = vbat - (ibat * rn5t618_battery->pmu_battery_rdc) / 1000;
    } else if (dir == RN5T618_CHARGER_DISCHARGING && rn5t618_battery) { // discharging
        result = vbat + (ibat * rn5t618_battery->pmu_battery_rdc) / 1000;    
    } else {
        result = vbat;    
    }
    return result;
}

static int rn5t618_update_state(struct aml_charger *charger)
{
    struct rn5t618_supply *supply = container_of(charger, struct rn5t618_supply, aml_charger);
    uint8_t buff[1] = {};
    int status;

    charger->vbat = rn5t618_get_battery_voltage();
    charger->ibat = rn5t618_get_battery_current();
    rn5t618_read(0x00BD, buff);
    status = buff[0] & 0x1f;
    charger->fault = buff[0];
    if (rn5t618_curr_dir < 0) {
        charger->fault |= 0x80000000;
    }
    if ((buff[0] & 0xc0) && (rn5t618_curr_dir == 1)) {
        if ((status == RN5T618_CHARGE_TRICKLE) || 
            (status == RN5T618_CHARGE_RAPID)) {
            charger->charge_status = CHARGER_CHARGING;
        } else {
            charger->charge_status = CHARGER_NONE;    
        }
    } else {
        charger->charge_status = CHARGER_DISCHARGING;
    }

    charger->ocv  = rn5t618_cal_ocv(charger->ibat, charger->vbat, charger->charge_status);
    if ((status != RN5T618_NO_BATTERY) && 
        (status != RN5T618_NO_BATTERY2)) {
        charger->bat_det = 1;
    } else {
        charger->bat_det = 0;    
    }
    if (status != RN5T618_BATTERY_TEMPERATURE_ERROR && 
        status != RN5T618_CHARGE_SUSPEND) {                             // charger is not suspended
        charger->dcin_valid = buff[0] & 0x40 ? 1 : 0;
        charger->usb_valid  = buff[0] & 0x80 ? 1 : 0;
        charger->ext_valid  = buff[0] & 0xc0;                           // to differ USB / AC status update 
    } else {
        charger->dcin_valid = 0;
        charger->usb_valid  = 0;
        charger->ext_valid  = 0;
        power_supply_changed(&supply->batt);
    }
#ifdef CONFIG_AMLOGIC_USB
    if (otg_mask) {
        charger->dcin_valid = 0;
        charger->usb_valid  = 0;
        charger->ext_valid  = 0;
    }
#endif

    rn5t618_read(0x00ef, buff);
    if (buff[0] & 0x01) {
        RICOH_DBG("cc is paused, reopen it\n");
        buff[0] &= ~0x01;
        rn5t618_write(0x00ef, buff[0]); 
    }
    rn5t618_read(0x00C5, buff);
    if (buff[0] & 0x20) {
        RICOH_INFO("charge time out, reset charger\n");
        rn5t618_set_bits(0x00C5, 0x00, 0x20);                           // clear flag
        rn5t618_set_bits(0x00B3, 0x00, 0x03);                           // disable charger
        msleep(100);
        rn5t618_set_bits(0x00B3, 0x03, 0x03);                           // eanble charger again
    }

    return 0;
}

#if 0
static void check_chip_temperature(void)
{
    int temp = rn5t618_get_battery_temperature();
    int stop, off;
    static uint8_t set_flag = 0;

    if (!g_rn5t618_init) {
        return ;    
    }
    stop = g_rn5t618_init->temp_to_stop_charger;
    off  = g_rn5t618_init->temp_to_power_off;
    if (stop && temp >= stop && !set_flag) {
        RICOH_DBG("temperature is %d, higher than %d, stop charger now\n", temp, stop);
        rn5t618_set_charge_enable(0);
        set_flag = 1;
    } else if (stop && temp < stop && set_flag) {
        RICOH_DBG("temperature is %d, lower than %d, re-start charger now\n", temp, stop);
        rn5t618_set_charge_enable(1);
        set_flag = 0;
    }

    if (off && temp >= off) {
        RICOH_DBG("temperature is %d, higher than %d, power off system\n", temp, off);
        rn5t618_power_off();    
    }
}

static void check_extern_power_voltage(struct aml_charger *charger)
{
    int voltage;
    int current_limit;
    uint8_t tmp;

    if (!charger->ext_valid) {
        return ;                                                        // no EXTERN power
    }
    if (charger->dcin_valid) {
        voltage = rn5t618_get_dcin_voltage();
        rn5t618_read(0x00B6, &tmp);
        current_limit = (tmp & 0x1f) * 100 + 100;
        if (voltage < EXT_POWER_LOW_THRESHOLD) {
            if (current_limit <= 500) {                                 // no need to limit too small
                return ;
            }
            current_limit -= 200;
            RICOH_DBG("DCIN voltage is %d, reduce current limit to %d\n", voltage, current_limit);
            rn5t618_set_dcin_current_limit(current_limit);
        } else if (voltage > EXT_POWER_HIGH_THRESHOLD) {
            if (current_limit >= 2500) {
                return;
            }
            current_limit += 200;
            RICOH_DBG("DCIN voltage is %d, increase current limit to %d\n", voltage, current_limit);
            rn5t618_set_dcin_current_limit(current_limit);
        }
    } else if (charger->usb_valid) {
        voltage = rn5t618_get_vbus_voltage();
        rn5t618_read(0x00B7, &tmp);
        current_limit = (tmp & 0x1f) * 100 + 100;
        if (voltage < EXT_POWER_LOW_THRESHOLD) {
            if (current_limit <= 500) {                                 // no need to limit too small
                return ;
            }
            current_limit -= 200;
            RICOH_DBG("VBUS voltage is %d, reduce current limit to %d\n", voltage, current_limit);
            rn5t618_set_usb_current_limit(current_limit);
        } else if (voltage > EXT_POWER_HIGH_THRESHOLD) {
            if (current_limit >= 1500) {
                return;
            }
            current_limit += 200;
            RICOH_DBG("VBUS voltage is %d, increase current limit to %d\n", voltage, current_limit);
            rn5t618_set_usb_current_limit(current_limit);
        }
    }
}
#endif

static void rn5t618_charging_monitor(struct work_struct *work)
{
    struct   rn5t618_supply *supply;
    struct   aml_charger    *charger;
    int32_t  pre_rest_cap;
    uint8_t  pre_chg_status;
    uint8_t  pre_pwr_status;
    struct aml_pmu_api *api = aml_pmu_get_api();
    static bool api_flag = false;

    supply  = container_of(work, struct rn5t618_supply, work.work);
    charger = &supply->aml_charger;
    pre_pwr_status = charger->ext_valid;
    pre_chg_status = charger->charge_status;
    pre_rest_cap   = charger->rest_vol;

    /*
     * 1. update status of PMU and all ADC value
     * 2. read ocv value and calculate ocv percent of battery
     * 3. read coulomb value and calculate movement of energy
     * 4. if battery capacity is larger than 429496 mAh, will cause over flow
     */
    if (rn5t618_battery) {
        if (!api) {
            schedule_delayed_work(&supply->work, supply->interval);
            return ;                                                // KO is not ready
        }
        if (api && !api_flag) {
            api_flag = true;
            if (api->pmu_probe_process) {
                api->pmu_probe_process(charger, rn5t618_battery);
            }
        }
        api->pmu_update_battery_capacity(charger, rn5t618_battery);
    } else {
        rn5t618_update_state(charger);
    }
//  check_chip_temperature();
//  check_extern_power_voltage(charger);
#ifdef CONFIG_RESET_TO_SYSTEM
    rn5t618_feed_watchdog();
#endif
    
    /*
     * protection for over-discharge with large loading usage
     */
    if (charger->rest_vol <= 0 && 
        charger->ext_valid     && 
        charger->charge_status == CHARGER_DISCHARGING) {
        over_discharge_cnt++;
        if (over_discharge_cnt >= 5) {
            RICOH_ERR("%s, battery is over-discharge now, force system power off\n", __func__);
            power_protection = 1;
        }
    } else {
        over_discharge_cnt = 0; 
        power_protection   = 0;
    }
    if ((charger->rest_vol - pre_rest_cap)         || 
        (pre_pwr_status != charger->ext_valid)     || 
        (pre_chg_status != charger->charge_status) ||
        charger->resume                            ||
        power_protection) {
        RICOH_INFO("battery vol change: %d->%d \n", pre_rest_cap, charger->rest_vol);
        if (unlikely(charger->resume)) {
            charger->resume = 0;
        }
        power_supply_changed(&supply->batt);
    #ifdef CONFIG_HAS_EARLYSUSPEND
        if (in_early_suspend && (pre_pwr_status != charger->ext_valid)) {
            wake_lock(&rn5t618_lock);
            RICOH_DBG("%s, usb power status changed in early suspend, wake up now\n", __func__);
            input_report_key(rn5t618_power_key, KEY_POWER, 1);              // assume power key pressed 
            input_sync(rn5t618_power_key);
        }
    #endif
    } 
    /* reschedule for the next time */
    schedule_delayed_work(&supply->work, supply->interval);
}

#if defined CONFIG_HAS_EARLYSUSPEND
static int early_power_status = 0;
static void rn5t618_earlysuspend(struct early_suspend *h)
{
    struct rn5t618_supply *supply = (struct rn5t618_supply *)h->param;
    if (rn5t618_battery) {
        rn5t618_set_charge_current(rn5t618_battery->pmu_suspend_chgcur);
        early_power_status = supply->aml_charger.ext_valid; 
    }
    in_early_suspend = 1;
}

static void rn5t618_lateresume(struct early_suspend *h)
{
    struct  rn5t618_supply *supply = (struct rn5t618_supply *)h->param;

    schedule_work(&supply->work.work);                                      // update for upper layer 
    if (rn5t618_battery) {
        rn5t618_set_charge_current(rn5t618_battery->pmu_resume_chgcur);
        early_power_status = supply->aml_charger.ext_valid; 
        input_report_key(rn5t618_power_key, KEY_POWER, 0);                  // cancel power key 
        input_sync(rn5t618_power_key);
    }
    in_early_suspend = 0;
    wake_unlock(&rn5t618_lock);
}
#endif

irqreturn_t rn5t618_irq_handler(int irq, void *dev_id)
{
    struct   rn5t618_supply *supply = (struct rn5t618_supply *)dev_id;

    disable_irq_nosync(supply->irq);
    schedule_work(&supply->irq_work);

    return IRQ_HANDLED;
}

static void rn5t618_irq_work_func(struct work_struct *work)
{
    struct rn5t618_supply *supply = container_of(work, struct rn5t618_supply, irq_work);

    //TODO: add code here
    enable_irq(supply->irq);
}

static struct notifier_block rn5t618_reboot_nb;
static int rn5t618_reboot_work(struct notifier_block *nb, unsigned long state, void *cmd)
{
    if (g_rn5t618_init->reset_to_system) {
        RICOH_DBG("%s, clear flags\n", __func__);
        rn5t618_set_bits(0x0007, 0x00, 0x01);
    }
    rn5t618_set_bits(0x0007, 0x20, 0x60);

    return NOTIFY_DONE;
}

struct aml_pmu_driver rn5t618_pmu_driver = {
    .name                           = "rn5t618",
    .pmu_get_coulomb                = rn5t618_get_coulomber, 
    .pmu_clear_coulomb              = rn5t618_clear_coulomber,
    .pmu_update_status              = rn5t618_update_state,
    .pmu_set_rdc                    = NULL,
    .pmu_set_gpio                   = rn5t618_set_gpio,
    .pmu_get_gpio                   = rn5t618_get_gpio,
    .pmu_reg_read                   = rn5t618_read,
    .pmu_reg_write                  = rn5t618_write,
    .pmu_reg_reads                  = rn5t618_reads,
    .pmu_reg_writes                 = rn5t618_writes,
    .pmu_set_bits                   = rn5t618_set_bits,
    .pmu_set_usb_current_limit      = rn5t618_set_usb_current_limit,
    .pmu_set_charge_current         = rn5t618_set_charge_current,
    .pmu_power_off                  = rn5t618_power_off,
};

static int rn5t618_battery_probe(struct platform_device *pdev)
{
    struct   rn5t618_supply *supply;
    struct   aml_charger    *charger;
    int      ret;
    uint32_t tmp2;

	RICOH_DBG("call %s in", __func__);
    g_rn5t618_init = pdev->dev.platform_data;
    if (g_rn5t618_init == NULL) {
        RICOH_ERR("%s, NO platform data\n", __func__);
        return -EINVAL;
    }
    rn5t618_power_key = input_allocate_device();
    if (!rn5t618_power_key) {
        kfree(rn5t618_power_key);
        return -ENODEV;
    }

    if (!(rn5t618_coulomb_flag & (POWER_OFF_FLAG | REBOOT_FLAG))) {
        RICOH_DBG("no special flag, clear RTC mem, force using ocv\n");
        aml_write_rtc_mem_reg(0, 0); 
    }
    rn5t618_power_key->name       = pdev->name;
    rn5t618_power_key->phys       = "m1kbd/input2";
    rn5t618_power_key->id.bustype = BUS_HOST;
    rn5t618_power_key->id.vendor  = 0x0001;
    rn5t618_power_key->id.product = 0x0001;
    rn5t618_power_key->id.version = 0x0100;
    rn5t618_power_key->open       = NULL;
    rn5t618_power_key->close      = NULL;
    rn5t618_power_key->dev.parent = &pdev->dev;

    set_bit(EV_KEY, rn5t618_power_key->evbit);
    set_bit(EV_REL, rn5t618_power_key->evbit);
    set_bit(KEY_POWER, rn5t618_power_key->keybit);

    ret = input_register_device(rn5t618_power_key);

#ifdef CONFIG_UBOOT_BATTERY_PARAMETERS 
    if (get_uboot_battery_para_status() == UBOOT_BATTERY_PARA_SUCCESS) {
        rn5t618_battery = get_uboot_battery_para();
        RICOH_DBG("use uboot passed battery parameters\n");
    } else {
        rn5t618_battery = g_rn5t618_init->board_battery; 
        RICOH_DBG("uboot battery parameter not get, use BSP configed battery parameters\n");
    }
#else
    rn5t618_battery = g_rn5t618_init->board_battery; 
    RICOH_DBG("use BSP configed battery parameters\n");
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

    g_rn5t618_supply = supply;
    charger = &supply->aml_charger;
    if (rn5t618_battery) {
        for (tmp2 = 1; tmp2 < 16; tmp2++) {
            if (!charger->ocv_empty && rn5t618_battery->pmu_bat_curve[tmp2].discharge_percent > 0) {
                charger->ocv_empty = rn5t618_battery->pmu_bat_curve[tmp2-1].ocv;
            }
            if (!charger->ocv_full && rn5t618_battery->pmu_bat_curve[tmp2].discharge_percent == 100) {
                charger->ocv_full = rn5t618_battery->pmu_bat_curve[tmp2].ocv;    
            }
        }

        supply->irq = rn5t618_battery->pmu_irq_id;
        supply->battery_info->technology         = rn5t618_battery->pmu_battery_technology;
        supply->battery_info->voltage_max_design = rn5t618_battery->pmu_init_chgvol;
        supply->battery_info->energy_full_design = rn5t618_battery->pmu_battery_cap;
        supply->battery_info->voltage_min_design = charger->ocv_empty * 1000;
        supply->battery_info->use_for_apm        = 1;
        supply->battery_info->name               = rn5t618_battery->pmu_battery_name;
    } else {
        RICOH_ERR(" NO BATTERY_PARAMETERS FOUND\n");
    }

    charger->soft_limit_to99     = g_rn5t618_init->soft_limit_to99;
    charger->coulomb_type        = COULOMB_SINGLE_CHG_INC; 
    supply->charge_timeout_retry = g_rn5t618_init->charge_timeout_retry;
#ifdef CONFIG_AMLOGIC_USB
    if (rn5t618_charger_job.flag) {     // do later job for usb charger detect
        rn5t618_usb_charger(NULL, rn5t618_charger_job.value, NULL);    
        rn5t618_charger_job.flag = 0;
    }
    if (rn5t618_otg_job.flag) {
        rn5t618_otg_change(NULL, rn5t618_otg_job.value, NULL);    
        rn5t618_otg_job.flag = 0;
    }
#endif
    rn5t618_reboot_nb.notifier_call = rn5t618_reboot_work;
    register_reboot_notifier(&rn5t618_reboot_nb);
    if (supply->irq == RN5T618_IRQ_NUM) {
        INIT_WORK(&supply->irq_work, rn5t618_irq_work_func); 
        ret = request_irq(supply->irq, 
                          rn5t618_irq_handler, 
                          IRQF_DISABLED | IRQF_SHARED,
                          RN5T618_IRQ_NAME,
                          supply); 
        if (ret) {
            RICOH_ERR("request irq failed, ret:%d, irq:%d\n", ret, supply->irq);    
        }
    }

    ret = rn5t618_first_init(supply);
    if (ret) {
        goto err_charger_init;
    }

    rn5t618_battery_setup_psy(supply);
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

    ret = rn5t618_supply_create_attrs(&supply->batt);
    if(ret){
        return ret;
    }

    platform_set_drvdata(pdev, supply);

    supply->interval = msecs_to_jiffies(RN5T618_WORK_CYCLE);
    INIT_DELAYED_WORK(&supply->work, rn5t618_charging_monitor);
    schedule_delayed_work(&supply->work, supply->interval);

#ifdef CONFIG_HAS_EARLYSUSPEND
    rn5t618_early_suspend.suspend = rn5t618_earlysuspend;
    rn5t618_early_suspend.resume  = rn5t618_lateresume;
    rn5t618_early_suspend.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 2;
    rn5t618_early_suspend.param   = supply;
    register_early_suspend(&rn5t618_early_suspend);
    wake_lock_init(&rn5t618_lock, WAKE_LOCK_SUSPEND, "rn5t618");
#endif
    if (rn5t618_battery) {
        power_supply_changed(&supply->batt);                    // update battery status
    }
    
#ifdef CONFIG_RESET_TO_SYSTEM
    rn5t618_set_bits(0x000b, 0x01, 0x0f);                       // disable watchdog 
    rn5t618_set_bits(0x0012, 0x00, 0x40);                       // disable watchdog
    rn5t618_set_bits(0x000b, 0x0d, 0x0f);                       // time out to 8s
    rn5t618_set_bits(0x0012, 0x40, 0x40);                       // enable watchdog
    rn5t618_feed_watchdog();
#endif
    //rn5t618_dump_all_register(NULL);
	RICOH_DBG("call %s exit, ret:%d", __func__, ret);
    return ret;

err_ps_register:
    free_irq(supply->irq, supply);
    cancel_delayed_work_sync(&supply->work);

err_charger_init:
    kfree(supply->battery_info);
    kfree(supply);
    input_unregister_device(rn5t618_power_key);
    kfree(rn5t618_power_key);
	RICOH_DBG("call %s exit, ret:%d", __func__, ret);
    return ret;
}

static int rn5t618_battery_remove(struct platform_device *dev)
{
    struct rn5t618_supply *supply= platform_get_drvdata(dev);

    cancel_work_sync(&supply->irq_work);
    cancel_delayed_work_sync(&supply->work);
    power_supply_unregister( &supply->usb);
    power_supply_unregister( &supply->ac);
    power_supply_unregister( &supply->batt);
    
    free_irq(supply->irq, supply);
    kfree(supply->battery_info);
    kfree(supply);
    input_unregister_device(rn5t618_power_key);
    kfree(rn5t618_power_key);
#ifdef CONFIG_HAS_EARLYSUSPEND
    wake_lock_destroy(&rn5t618_lock);
#endif

    return 0;
}


static int rn5t618_suspend(struct platform_device *dev, pm_message_t state)
{
    struct rn5t618_supply *supply = platform_get_drvdata(dev);
    struct aml_pmu_api    *api;

    cancel_delayed_work_sync(&supply->work);
    if (rn5t618_battery) {
        api = aml_pmu_get_api();
        if (api && api->pmu_suspend_process) {
            api->pmu_suspend_process(&supply->aml_charger);
        } 
    }
#ifdef CONFIG_HAS_EARLYSUSPEND
    if (early_power_status != supply->aml_charger.ext_valid) {
        RICOH_DBG("%s, power status changed, prev:%x, now:%x, exit suspend process\n", 
                  __func__, early_power_status, supply->aml_charger.ext_valid);
        input_report_key(rn5t618_power_key, KEY_POWER, 1);              // assume power key pressed 
        input_sync(rn5t618_power_key);
        return -1;
    }
    in_early_suspend = 0;
#endif

    return 0;
}

static int rn5t618_resume(struct platform_device *dev)
{
    struct rn5t618_supply *supply = platform_get_drvdata(dev);
    struct aml_pmu_api    *api;

    if (rn5t618_battery) {
        api = aml_pmu_get_api();
        if (api && api->pmu_resume_process) {
            api->pmu_resume_process(&supply->aml_charger, rn5t618_battery);
        }
    }
    schedule_work(&supply->work.work);

    return 0;
}

static void rn5t618_shutdown(struct platform_device *dev)
{
    // add code here
}

static struct platform_driver rn5t618_battery_driver = {
    .driver = {
        .name  = RN5T618_DRIVER_NAME, 
        .owner = THIS_MODULE,
    },
    .probe    = rn5t618_battery_probe,
    .remove   = rn5t618_battery_remove,
    .suspend  = rn5t618_suspend,
    .resume   = rn5t618_resume,
    .shutdown = rn5t618_shutdown,
};

static int rn5t618_battery_init(void)
{
    int ret;
    ret = platform_driver_register(&rn5t618_battery_driver);
	RICOH_DBG("call %s, ret = %d\n", __func__, ret);
	return ret;
}

static void rn5t618_battery_exit(void)
{
    platform_driver_unregister(&rn5t618_battery_driver);
}

module_init(rn5t618_battery_init);
module_exit(rn5t618_battery_exit);

MODULE_DESCRIPTION("RICOH PMU RN5T618 battery driver");
MODULE_AUTHOR("tao.zeng@amlogic.com, Amlogic, Inc");
MODULE_LICENSE("GPL");
