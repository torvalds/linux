/*
 * Implement driver of AML1216 PMU
 * Author: chunjian.zheng@amlogic.com
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
#include <linux/amlogic/aml_pmu.h>
#include <mach/usbclock.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#include <linux/wakelock_android.h>
#endif

#ifdef CONFIG_UBOOT_BATTERY_PARAMETERS
#include <linux/amlogic/battery_parameter.h>
#endif

#define CHECK_DRIVER()      \
    if (!g_aml1216_supply) {        \
        AML1216_INFO("driver is not ready right now, wait...\n");   \
        dump_stack();       \
        return -ENODEV;     \
    }

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend aml1216_early_suspend;
static int    in_early_suspend = 0; 
static int    early_power_status = 0;
static struct wake_lock aml1216_lock;
#endif
struct aml1216_supply           *g_aml1216_supply  = NULL;
struct amlogic_pmu_init         *g_aml1216_init    = NULL;
struct battery_parameter        *aml1216_battery   = NULL;
struct input_dev                *aml1216_power_key = NULL;

static int power_protection   = 0;
static int over_discharge_cnt = 0;
static int adc_sign_bit       = 0; 
#define BATTERY_CHARGING      1
#define BATTERY_DISCHARGING   0

static int pmu_init_chgvol = 0;
static int ocv_voltage = 0;

#ifdef CONFIG_AMLOGIC_USB
struct work_struct          aml1216_otg_work;
extern int dwc_otg_power_register_notifier(struct notifier_block *nb);
extern int dwc_otg_power_unregister_notifier(struct notifier_block *nb);
extern int dwc_otg_charger_detect_register_notifier(struct notifier_block *nb);
extern int dwc_otg_charger_detect_unregister_notifier(struct notifier_block *nb);
struct later_job {
    int flag;
    int value;
};
static struct later_job aml1216_charger_job = {};
static struct later_job aml1216_otg_job = {};
#endif

static int aml1216_update_state(struct aml_charger *charger);

//static uint32_t charge_timeout = 0;
//static int      re_charge_cnt  = 0;
//static int      current_dir    = -1;
//static int      power_flag     = 0;
//static int      pmu_version    = 0;
//static int      chg_status_reg  = 0;
static int      usb_bc_mode    = 0;

int aml1216_get_battery_voltage(void)
{
    uint8_t val[2] = {};
    int result = 0;
    int tmp;
    
    aml1216_reads(0x00AF, val, 2);        
    tmp = (((val[1] & 0x1f) << 8) + val[0]);
    result = (tmp * 4800) / 4096;
    
    return result;
}
EXPORT_SYMBOL_GPL(aml1216_get_battery_voltage);

int aml1216_get_dcin_voltage(void)
{
    uint8_t val[2] = {};
    int     result;

    aml1216_write(0x00AA, 0xC1);                            // select DCIN channel
    aml1216_write(0x009A, 0x28);
    udelay(100);
    aml1216_reads(0x00B1, val, 2);
    result = ((val[1] & 0x1f) << 8) + val[0];
    if (result & 0x1000) {                                  // complement code
        result = 0;                                         // avoid ADC offset 
    } else {
        result = (result * 12800) / 4096;
    }

    return result;
}

int aml1216_get_vbus_voltage(void)
{
    uint8_t val[2] = {};
    int     result;

    aml1216_write(0x00AA, 0xC2);                            // select VBUS channel
    aml1216_write(0x009A, 0x28);
    udelay(100);
    aml1216_reads(0x00B1, val, 2);
    result = ((val[1] & 0x1f) << 8) + val[0];
    if (result & 0x1000) {                                  // complement code
        result = 0;                                         // avoid ADC offset 
    } else {
        result = result * 6400 / 4096;
    }

    return result;
}

int aml1216_get_battery_current(void)
{
    uint8_t  buf[2] = {};
    uint32_t tmp;
    int      sign_bit, result;

    aml1216_reads(0x00AB, buf, 2);
    tmp = ((buf[1] & 0x1f) << 8) + buf[0];
    sign_bit = tmp & 0x1000;
    adc_sign_bit = sign_bit;

    if (tmp & 0x1000) {                                              // complement code
        tmp = (tmp ^ 0x1fff) + 1;
    }
    result = (tmp * 5333) / 4096; 
    return result;
}
EXPORT_SYMBOL_GPL(aml1216_get_battery_current);

int aml1216_set_dcin(int enable)
{
    uint8_t val = 0;

    if (!enable) {
        val |= 0x01;
    }
    AML1216_INFO("%s:%s\n", __func__, enable ? "enable" : "disable");

    return aml1216_set_bits(0x002a, val, 0x01);
}
EXPORT_SYMBOL_GPL(aml1216_set_dcin);

int aml1216_set_gpio(int pin, int val)
{
#if 0
    uint32_t data;

    if (pin <= 0 || pin > 3 || val > 1 || val < 0) { 
        AML1216_DBG("ERROR, invalid input value, pin = %d, val= %d\n", pin, val);
        return -1;
    }    
    if (val < 2) { 
        data = ((val ? 1 : 0) << (pin));
    } else {
        AML1216_DBG("%s, not support value for 1216:%d\n", __func__, val);
        return -1;
    }    
    AML1216_DBG("%s, GPIO:%d, val:%d\n", __func__, pin, val);
    return aml1216_set_bits(0x0013, data, (1 << pin));
#else
    uint32_t data;

    if (pin <= 0 || pin > 4 || val > 1 || val < 0) {
        AML1216_ERR("ERROR, invalid input value, pin = %d, val= %d\n", pin, val);
        return -EINVAL;
    }
    data = (1 << (pin + 11));
    AML1216_DBG("%s, GPIO:%d, val:%d\n", __func__, pin, val);
    if (val) {
        return aml1216_write16(0x0084, data);
    } else {
        return aml1216_write16(0x0082, data);
    }
#endif
}
EXPORT_SYMBOL_GPL(aml1216_set_gpio);

int aml1216_get_gpio(int gpio, int *val)
{
    int ret;
    uint8_t data;

    if (gpio <= 0 || gpio> 4 || !val) { 
        AML1216_ERR("ERROR, invalid input value, gpio = %d, val= %p\n", gpio, val);
        return -EINVAL;
    }
    ret = aml1216_read(AML1216_GPIO_INPUT_STATUS, &data);
    if (ret) {                                                  // read failed
        return ret;    
    }
    if (data & (1 << (gpio - 1))) {
        *val = 1;    
    } else {
        *val = 0;    
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_get_gpio);

void aml1216_power_off()
{
    uint8_t buf = (1 << 5);                                     // software goto OFF state

    aml1216_write(0x0019, 0x10);
    aml1216_write16(0x0084, 0x0001);
    udelay(1000);
    aml1216_set_gpio(1, 1);
    aml1216_set_gpio(2, 1);
    aml1216_set_gpio(3, 1);
    AML1216_INFO("software goto OFF state\n");
    mdelay(10);
    aml1216_write(AML1216_GEN_CNTL1, buf);    
    udelay(1000);
    while (1) {
        msleep(1000);
        AML1216_ERR("%s, error\n", __func__);
    }
}
EXPORT_SYMBOL_GPL(aml1216_power_off);

int aml1216_set_usb_current_limit(int limit)
{
    int val;
    if ((limit < 100 || limit > 1600) && (limit != -1)) {
       AML1216_ERR("%s, wrong usb current limit:%d\n", __func__, limit); 
       return -1;
    }
    if (limit == -1) {                                       // -1 means not limit, so set limit to max
        limit = 1600;    
    }
    val = (limit-100)/ 100;
    val ^= 0x04;                                            // bit 2 is reverse bit
    
    AML1216_INFO("%s, set usb current limit to %d, bit:%02x\n", __func__, limit, val);
    return aml1216_set_bits(0x002D, val, 0x0f);
    
}
EXPORT_SYMBOL_GPL(aml1216_set_usb_current_limit);

int aml1216_set_usb_voltage_limit(int voltage)
{
    uint8_t val;

    if (voltage > 4600 || voltage < 4300) {
        AML1216_ERR("%s, Wrong usb voltage limit:%d\n", __func__, voltage);    
    }
    aml1216_read(AML1216_CHG_CTRL5, &val);
    val &= ~(0xc0);
    switch (voltage) {
    case 4300:
        val |= (0x01 << 5);
        break;

    case 4400:
        val |= (0x02 << 5);
        break;

    case 4500:
        val |= (0x00 << 5);
        break;

    case 4600:
        val |= (0x03 << 5);
        break;
    
    default:
        AML1216_ERR("%s, Wrong usb voltage limit:%d\n", __func__, voltage);
        return -1;
    }
    aml1216_write(AML1216_CHG_CTRL5, val);
    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_set_usb_voltage_limit);

int aml1216_get_vsys_voltage(void)
{
    uint8_t val[2] = {};
    int     result;
    aml1216_write(0x00AA, 0xC3);                            // select VBUS channel
    aml1216_write(0x009A, 0x28);
    udelay(100);
    aml1216_reads(0x00B1, val, 2);
    result = ((val[1] & 0x1f) << 8) + val[0];
    if (result & 0x1000) {                                  // complement code
        result = 0;                                         // avoid ADC offset 
    } else {
        result = result * 6400 / 4096;
    }
    return result;
}

int aml1216_get_otp_version(void)
{
    uint8_t val = 0; 
    int  otp_version = 0;
    aml1216_read(0x007e, &val);
    otp_version = (val & 0x60) >> 5;
    return otp_version;
}

int aml1216_set_full_charge_voltage(int voltage);
int aml1216_set_charge_enable(int enable)
{
    //uint8_t val = 0; 
    //uint8_t val_t = 0;
    int otp_version = 0;
    //int charge_status = 0;
    //int ocv = 0;
    otp_version = aml1216_get_otp_version();
    if (otp_version == 0)
    {   
        aml1216_set_full_charge_voltage(4050000);
        if (usb_bc_mode == USB_BC_MODE_SDP) {
            return aml1216_set_bits(0x0017, 0x00, 0x01);
        }
        if (ocv_voltage > 3950)
        {   
            printk("%s, otp_version:%d, ocv = %d, do not open charger.\n", __func__, otp_version, ocv_voltage);
            return aml1216_set_bits(0x0017, 0x00, 0x01);
        }
    }
    else if(otp_version >= 1)
    {
        aml1216_set_full_charge_voltage(pmu_init_chgvol);
    }
    return aml1216_set_bits(0x0017, ((enable & 0x01)), 0x01); 
}
EXPORT_SYMBOL_GPL(aml1216_set_charge_enable);

int aml1216_set_recharge_voltage(void)
{
    return aml1216_set_bits(0x012c, 0x04, 0x0c);
}
EXPORT_SYMBOL_GPL(aml1216_set_recharge_voltage);


int aml1216_set_charging_current(int curr)
{
    int idx_cur, idx_to, val = 0;
    int rem;

    if (curr > 2100 * 1000 || curr < 0) {
        AML1216_ERR("%s, wrong input of charge current:%d\n", __func__, curr);
        return -1;
    }
    if (curr > 100) {                        // input is uA
        curr = curr / 1000;
    } else {                                    // input is charge ratio
        curr = (curr * aml1216_battery->pmu_battery_cap) / 100 + 100; 
    } 
    
#if 0    
    if (curr < 750) {                       // limit current to 600mA for stable issue
        curr = 750;    
    }
#endif

    idx_to = (curr - 300) / 150;
    rem = curr % 150;                       // round up
    if (rem) {
        idx_to += 1;    
    }
    aml1216_read(0x012b, (unsigned char *)&val);
    AML1216_INFO("%s to %dmA, idx_to:%x, idx_cur:%x\n", __func__, idx_to * 150 + 300, idx_to, val);
    idx_cur = val & 0x0f;

    while (idx_cur != idx_to) {
        if (idx_cur < idx_to) {
            idx_cur++;    
        } else {
            idx_cur--;    
        }
        val &= ~0x0f;
        val |= (idx_cur & 0x0f);
        aml1216_write(0x012b, val);
        udelay(100);
    }

    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_set_charging_current);

int aml1216_set_trickle_time(int minutes)
{
    int bits;

    if (minutes < 30 && minutes > 80) {
        AML1216_ERR("%s, invalid trickle time:%d\n", __func__, minutes);
        return -EINVAL;
    }
    switch (minutes) {
    case 30:     bits = 0x04;     break;
    case 50:     bits = 0x08;     break;
    case 80:     bits = 0x0c;     break;
    default:
        AML1216_ERR("%s, unsupported trickle value:%d\n", __func__, minutes);
        return -EINVAL;
    }
    return aml1216_set_bits(0x012A, bits, 0x0c);
}

int aml1216_set_rapid_time(int minutes)
{
    int bits;

    if (minutes > 360 || minutes < 720) {
        AML1216_ERR("%s, invalid rapid time:%d\n", __func__, minutes);
        return -EINVAL;
    }
    switch (minutes) {
    case 360:     bits = 0x04;     break;
    case 540:     bits = 0x08;     break;
    case 720:     bits = 0x0c;     break;
    default:
        AML1216_ERR("%s, unsupported rapid value:%d\n", __func__, minutes);
        return -EINVAL;
    }
    return aml1216_set_bits(0x0129, bits, 0x0c);
}

int aml1216_set_full_charge_voltage(int voltage)
{
    uint8_t val;
    uint8_t tmp;
    
    if (voltage > 4400000 || voltage < 4050000) {
        AML1216_ERR("%s,Wrong charge voltage:%d\n", __func__, voltage);
        return -1;
    }
    tmp = ((voltage - 4050000) / 50000);
    aml1216_read(AML1216_CHG_CTRL0, &val);
    val &= ~(0x38);
    val |= (tmp << 3);
    aml1216_write(AML1216_CHG_CTRL0, val);

    return 0; 
}

int aml1216_set_charge_end_rate(int rate) 
{
    uint8_t val;

#if 1
    aml1216_read(AML1216_CHG_CTRL6, &val);
    switch (rate) {
    case 10:
        val &= ~(0x10);
        break;

    case 20:
        val |= (0x10);
        break;

    default:
        AML1216_ERR("%s, Wrong charge end rate:%d\n", __func__, rate);
        return -1;
    }
    aml1216_write(AML1216_CHG_CTRL6, val);
#endif
    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_set_charge_end_rate);

int aml1216_set_long_press_time(int ms)
{   
    uint16_t  val;
    uint16_t tmp;
     
    aml1216_read16(0x90, &val);
    tmp = ms/100 -1; 
    val |= tmp;                                        // set power key long press to 10s
    return aml1216_set_bits(0x0090, val, 0x7f);
}

static int aml1216_get_coulomber(struct aml_charger *charger)
{
    uint8_t buf[8]= {};
    int ret;
    int charge_result;
    int discharge_result;

    ret = aml1216_reads(0x0152, buf, 4);
    if (ret) {
        AML1216_ERR("%s, failed: %d\n", __func__, __LINE__);
        return ret;
    }

    /*
     * Convert to mAh:
     * 8ms per SAR ADC result accumulator, 125 add per second
     * LSB is 5333 / 4096, an hour is 3600 second
     * register value is 32MSB of 48bit register value.
     * coulomb = (65536 * [register vaule] * 5333 / 4096) / (3600 * 125)
     *         = 0.1896178 * [register vaule]
     *         = (97.084302 / 512) * [register vaule]
     * if [register vaule] > 2147483630(mAh) = [2^31 / 97]
     * this simple calculate method ill cause overflow bug due to 32bit 
     * register range, but fortunately, No battery has capacity large than
     * this value
     */
    charge_result  = (buf[0] <<  0) |
                     (buf[1] <<  8) |
                     (buf[2] << 16) |
                     (buf[3] << 24);
    charge_result *= -1;                                                // charge current is negative
    charge_result  = ((charge_result * 97) / 512);                      // convert to mAh

    ret = aml1216_reads(0x0158, buf, 4);
    if (ret) {
        AML1216_ERR("%s, failed: %d\n", __func__, __LINE__);
        return ret;
    }
    discharge_result = (buf[0] <<  0) |
                       (buf[1] <<  8) |
                       (buf[2] << 16) |
                       (buf[3] << 24);
    discharge_result = (discharge_result * 97) / 512;                   // convert to mAh
    charger->charge_cc    = charge_result;
    charger->discharge_cc = discharge_result;
    return 0;
}

static int aml1216_clear_coulomber(struct aml_charger *charger)
{
    return aml1216_set_bits(0x009A, 0x80, 0x80);    
}

int aml1216_get_battery_percent(void)
{
    CHECK_DRIVER();
    return g_aml1216_supply->aml_charger.rest_vol;    
}
EXPORT_SYMBOL_GPL(aml1216_get_battery_percent);

int aml1216_first_init(struct aml1216_supply *supply)
{
    int vbat, vsys;
    /*
     * initialize charger from battery parameters
     */
    if (aml1216_battery) {
        aml1216_set_charging_current   (aml1216_battery->pmu_init_chgcur);
        //aml1216_set_full_charge_voltage(aml1216_battery->pmu_init_chgvol);
        aml1216_set_charge_end_rate    (aml1216_battery->pmu_init_chgend_rate);
        aml1216_set_trickle_time       (aml1216_battery->pmu_init_chg_pretime);
        aml1216_set_rapid_time         (aml1216_battery->pmu_init_chg_csttime);
        aml1216_set_recharge_voltage   ();
        aml1216_set_long_press_time    (aml1216_battery->pmu_pekoff_time);
        pmu_init_chgvol = aml1216_battery->pmu_init_chgvol;
        vbat = aml1216_get_battery_voltage();
        vsys = aml1216_get_vsys_voltage();
        if ((vsys > vbat) && (vsys - vbat < 500)) {
            printk("%s, vsys is not large, vsys:%d, vbat:%d\n", __func__, vsys, vbat);
            aml1216_set_charge_enable  (0);
        } else {
            aml1216_set_charge_enable  (aml1216_battery->pmu_init_chg_enabled);
        }

        if (aml1216_battery->pmu_usbvol_limit) {
            aml1216_set_usb_voltage_limit(aml1216_battery->pmu_usbvol); 
        }
    }

    return 0;
}

static enum power_supply_property aml1216_battery_props[] = {
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

static enum power_supply_property aml1216_ac_props[] = {
    POWER_SUPPLY_PROP_MODEL_NAME,
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_ONLINE,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW,
};

static enum power_supply_property aml1216_usb_props[] = {
    POWER_SUPPLY_PROP_MODEL_NAME,
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_ONLINE,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW,
};

static void aml1216_battery_check_status(struct aml1216_supply       *supply,
                                         union  power_supply_propval *val)
{
    struct aml_charger *charger = &supply->aml_charger;

    if (!aml1216_battery) {
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

static void aml1216_battery_check_health(struct aml1216_supply       *supply,
                                         union  power_supply_propval *val)
{
    int status = 0; 

    if (status == 0x30) {
        // TODO: add other check method?
        AML1216_ERR("%s, battery error detect\n", __func__);
        val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
    } else {
        val->intval = POWER_SUPPLY_HEALTH_GOOD;
    }
}

static int aml1216_battery_get_property(struct power_supply *psy,
                                        enum   power_supply_property psp,
                                        union  power_supply_propval *val)
{
    struct aml1216_supply *supply;
    struct aml_charger    *charger;
    int ret = 0;
    int sign_bit = 1;
    supply  = container_of(psy, struct aml1216_supply, batt);
    charger = &supply->aml_charger;
    
    switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:
        aml1216_battery_check_status(supply, val);
        break;

    case POWER_SUPPLY_PROP_HEALTH:
        aml1216_battery_check_health(supply, val);
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
        if (aml1216_battery) {
            val->intval = charger->rest_vol;
        } else {
            val->intval = 100;    
        }
        break;

    case POWER_SUPPLY_PROP_ONLINE:
        if (aml1216_battery) {
            val->intval = charger->bat_det; 
        } else {
            val->intval = 0;    
        }
        break;

    case POWER_SUPPLY_PROP_PRESENT:
        if (aml1216_battery) {
            val->intval = charger->bat_det; 
        } else {
            val->intval = 0;    
        }
        break;

    case POWER_SUPPLY_PROP_TEMP:
        val->intval = 300; 
        break;
    default:
        ret = -EINVAL;
        break;
    }
    
    return ret;
}

static int aml1216_ac_get_property(struct power_supply *psy,
                                   enum   power_supply_property psp,
                                   union  power_supply_propval *val)
{
    struct aml1216_supply *supply;
    struct aml_charger    *charger;
    int ret = 0;
    supply  = container_of(psy, struct aml1216_supply, ac);
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
        val->intval = 5000 * 1000;
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

static int aml1216_usb_get_property(struct power_supply *psy,
           enum power_supply_property psp,
           union power_supply_propval *val)
{
    struct aml1216_supply *supply;
    struct aml_charger    *charger;
    int ret = 0;
    supply  = container_of(psy, struct aml1216_supply, usb);
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
        val->intval = 5000 * 1000;
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

static void aml1216_battery_setup_psy(struct aml1216_supply *supply)
{
    struct power_supply      *batt = &supply->batt;
    struct power_supply      *ac   = &supply->ac;
    struct power_supply      *usb  = &supply->usb;
    struct power_supply_info *info =  supply->battery_info;
    
    batt->name           = "battery";
    batt->use_for_apm    = info->use_for_apm;
    batt->type           = POWER_SUPPLY_TYPE_BATTERY;
    batt->get_property   = aml1216_battery_get_property;
    batt->properties     = aml1216_battery_props;
    batt->num_properties = ARRAY_SIZE(aml1216_battery_props);
    
    ac->name             = "ac";
    ac->type             = POWER_SUPPLY_TYPE_MAINS;
    ac->get_property     = aml1216_ac_get_property;
    ac->supplied_to      = supply_list;
    ac->num_supplicants  = ARRAY_SIZE(supply_list);
    ac->properties       = aml1216_ac_props;
    ac->num_properties   = ARRAY_SIZE(aml1216_ac_props);
    
    usb->name            = "usb";
    usb->type            = POWER_SUPPLY_TYPE_USB;
    usb->get_property    = aml1216_usb_get_property;
    usb->supplied_to     = supply_list,
    usb->num_supplicants = ARRAY_SIZE(supply_list),
    usb->properties      = aml1216_usb_props;
    usb->num_properties  = ARRAY_SIZE(aml1216_usb_props);
}

#ifdef CONFIG_AMLOGIC_USB

static int aml1216_otg_value = -1;
static void aml1216_otg_work_fun(struct work_struct *work)
{
    uint8_t val;
    if (aml1216_otg_value == -1) {
        return ;    
    }
    AML1216_INFO("%s, OTG value:%d, is_short:%d\n", __func__, aml1216_otg_value, g_aml1216_init->vbus_dcin_short_connect);
    if (aml1216_otg_value) {
        if (g_aml1216_init->vbus_dcin_short_connect) {
            aml1216_set_dcin(0);                            // cut off dcin for single usb port device
        }
        aml1216_write(0x0019, 0xD0); 
    } else {
        aml1216_write(0x0019, 0x10); 
        if (g_aml1216_init->vbus_dcin_short_connect) {
            aml1216_set_dcin(1);                            // cut off dcin for single usb port device
        }
    }
    msleep(10);
    aml1216_read(0x19, &val);
    printk("register 0x19:%02x\n", val);
    aml1216_otg_value = -1;
    aml1216_update_state(&g_aml1216_supply->aml_charger);
    power_supply_changed(&g_aml1216_supply->batt);
}

int aml1216_otg_change(struct notifier_block *nb, unsigned long value, void *pdata)
{
    if (!g_aml1216_supply) {
        AML1216_INFO("%s, driver is not ready, do it later\n", __func__);
        aml1216_otg_job.flag  = 1;
        aml1216_otg_job.value = value;
        return 0;
    }
    aml1216_otg_value = value;
    schedule_work(&aml1216_otg_work);
    return 0;
}

int aml1216_usb_charger(struct notifier_block *nb, unsigned long value, void *pdata)
{
    if (!g_aml1216_supply) {
        AML1216_INFO("%s, driver is not ready, do it later\n", __func__);
        aml1216_charger_job.flag  = 1;
        aml1216_charger_job.value = value;
        return 0;
    }
    usb_bc_mode = value;
    switch (value) {
    case USB_BC_MODE_SDP:                                               // pc
        if (aml1216_get_otp_version() == 0) {
            printk("disable charger for REVB chip when connect to PC\n");
            aml1216_set_charge_enable(0);
        }
        if (g_aml1216_init->vbus_dcin_short_connect) {
            aml1216_set_dcin(0);                            // cut off dcin for single usb port device
        }
        if (aml1216_battery && aml1216_battery->pmu_usbcur_limit) {     // limit usb current
            aml1216_set_usb_current_limit(aml1216_battery->pmu_usbcur); 
        }
        break;

    case USB_BC_MODE_DISCONNECT:                                        // disconnect
        if (g_aml1216_init->vbus_dcin_short_connect) {
            aml1216_set_dcin(1); 
        }
        if (aml1216_battery && aml1216_battery->pmu_usbcur_limit) {     // limit usb current
            aml1216_set_usb_current_limit(aml1216_battery->pmu_usbcur); 
        }
        if (aml1216_get_otp_version() == 0) {
            aml1216_set_charge_enable(1);
        }
        break;
    case USB_BC_MODE_DCP:                                               // charger
    case USB_BC_MODE_CDP:                                               // PC + charger
        if (aml1216_battery) {                                          // limit usb current
            aml1216_set_usb_current_limit(-1);                          // not limit usb current
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
static int printf_usage(void)
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
        ret = aml1216_read(addr, &value);
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
        ret   = aml1216_write(addr, value);
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

static ssize_t pmu_reg16_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return printf_usage(); 
}

static ssize_t pmu_reg16_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int ret;
    int addr;
    uint16_t value;
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
        ret = aml1216_read16(addr, &value);
        if (!ret) {
            printk("reg[0x%03x] = 0x%04x\n", addr, value);
        }
        break;

    case 'w':
        if (i != 3) {                       // parameter is not enough
            ret = 1;
            break;
        }
        addr  = simple_strtoul(arg[1], NULL, 16);
        value = simple_strtoul(arg[2], NULL, 16);
        ret   = aml1216_write16(addr, value);
        if (!ret) {
            printk("set reg[0x%03x] to 0x%04x\n", addr, value);
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

static ssize_t driver_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "AML PMU AML1216 driver version is %s, build time:%s\n", 
                   AML1216_DRIVER_VERSION, init_uts_ns.name.version);
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
    aml1216_power_off();
    return count; 
}

int aml1216_dump_all_register(char *buf)
{
    uint8_t val[16];
    int     i;
    int     size = 0;

    if (!buf) {
        printk("[AML1216] DUMP ALL REGISTERS:\n");
        for (i = 0; i < 24; i++) {
            aml1216_reads(i*16, val, 16);
            printk("0x%03x - %03x: ", i * 16, i * 16 + 15);
            printk("%02x %02x %02x %02x ",   val[0],  val[1],  val[2],  val[3]);
            printk("%02x %02x %02x %02x   ", val[4],  val[5],  val[6],  val[7]);
            printk("%02x %02x %02x %02x ",   val[8],  val[9],  val[10], val[11]);
            printk("%02x %02x %02x %02x\n",  val[12], val[13], val[14], val[15]);
        }
        return 0;
    }

    size += sprintf(buf + size, "%s", "[AML1216] DUMP ALL REGISTERS:\n");
    for (i = 0; i < 24; i++) {
        aml1216_reads(i*16, val, 16);
        size += sprintf(buf + size, "0x%03x - %03x: ", i * 16, i * 16 + 15);
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
    size = aml1216_dump_all_register(buf);
    size += sprintf(buf + size, "%s", "[AML1216] DUMP ALL REGISTERS OVER!\n"); 
    return size;
}
static ssize_t dump_pmu_regs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return count;                                           /* nothing to do        */
}

static ssize_t dbg_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct power_supply   *battery = dev_get_drvdata(dev);
    struct aml1216_supply *supply = container_of(battery, struct aml1216_supply, batt); 
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
    struct aml1216_supply *supply  = container_of(battery, struct aml1216_supply, batt); 
    struct aml_charger    *charger = &supply->aml_charger;
    int i = 0; 
    int size;

    if (!aml1216_battery) {
        return sprintf(buf, "No battery parameter find\n");
    }
    size = sprintf(buf, "\n i,      ocv,    charge,  discharge,\n");
    for (i = 0; i < 16; i++) {
        size += sprintf(buf + size, "%2d,     %4d,       %3d,        %3d,\n",
                        i, 
                        aml1216_battery->pmu_bat_curve[i].ocv,
                        aml1216_battery->pmu_bat_curve[i].charge_percent,
                        aml1216_battery->pmu_bat_curve[i].discharge_percent);
    }
    size += sprintf(buf + size, "\nBattery capability:%4d@3700mAh, RDC:%3d mohm\n", 
                                aml1216_battery->pmu_battery_cap, 
                                aml1216_battery->pmu_battery_rdc);
    size += sprintf(buf + size, "Charging efficiency:%3d%%, capability now:%3d%%\n", 
                                aml1216_battery->pmu_charge_efficiency,
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
        AML1216_ERR("input too large, failed to set report_delay\n");
        return count;
    }
    if (api && api->pmu_set_report_delay) {
        api->pmu_set_report_delay(tmp);
    } else {
        AML1216_ERR("API not found\n");
    }
    return count;
}

static struct device_attribute aml1216_supply_attrs[] = {
    AML_ATTR(pmu_reg),
    AML_ATTR(pmu_reg16),
    AML_ATTR(dbg_info),
    AML_ATTR(battery_para),
    AML_ATTR(report_delay),
    AML_ATTR(driver_version),
    AML_ATTR(clear_rtc_mem),
    AML_ATTR(dump_pmu_regs),
};

int aml1216_supply_create_attrs(struct power_supply *psy)
{
    int j,ret;
    for (j = 0; j < ARRAY_SIZE(aml1216_supply_attrs); j++) {
        ret = device_create_file(psy->dev, &aml1216_supply_attrs[j]);
        if (ret)
            goto sysfs_failed;
    }
    goto succeed;

sysfs_failed:
    while (j--) {
        device_remove_file(psy->dev, &aml1216_supply_attrs[j]);
    }
succeed:
    return ret;
}

int aml1216_cal_ocv(int ibat, int vbat, int dir)
{
    int result;

    if (dir == CHARGER_CHARGING && aml1216_battery) {           // charging
        result = vbat - (ibat * aml1216_battery->pmu_battery_rdc) / 1000;
    } else if (dir == CHARGER_DISCHARGING && aml1216_battery) { // discharging
        result = vbat + (ibat * aml1216_battery->pmu_battery_rdc) / 1000;    
    } else {
        result = vbat;    
    }
    return result;
}

static int aml1216_update_state(struct aml_charger *charger)
{
    uint8_t val;
    int  vsys_voltage;
    vsys_voltage = aml1216_get_vsys_voltage();

    aml1216_read(0x0172, &val);

    charger->ibat = aml1216_get_battery_current();
    if (val & 0x18) {
        if (charger->ibat >= 20) {
            charger->charge_status = CHARGER_CHARGING;                  // charging
        } else {
            charger->charge_status = CHARGER_NONE;                      // Not charging 
        } 
    } else {
        charger->charge_status = CHARGER_DISCHARGING; 
    }
    charger->bat_det    = 1;                                            // do not check register 0xdf, bug here
    charger->dcin_valid = (val & 0x10) ? 1 : 0; 
    charger->usb_valid  = (val & 0x08) ? 1 : 0; 
    charger->ext_valid  = charger->dcin_valid | (charger->usb_valid << 1); 
    charger->fault      = val;
    /*
     * limit duty cycle of DC3 according vsys voltage
     */
    //aml1216_set_bits(0x004f, (val & 0x01) << 3, 0x08);

    if (vsys_voltage >= 4350)
    {
        aml1216_set_bits(0x0035, 0x00, 0x07);
        aml1216_set_bits(0x003e, 0x00, 0x07);
        aml1216_set_bits(0x0047, 0x00, 0x07);
        aml1216_set_bits(0x004f, 0x08, 0x08);
    }
    else
    {
        aml1216_set_bits(0x004f, 0x00, 0x08);
        aml1216_set_bits(0x0035, 0x04, 0x07);
        aml1216_set_bits(0x003e, 0x04, 0x07);
        aml1216_set_bits(0x0047, 0x04, 0x07);
    } 
    charger->vbat = aml1216_get_battery_voltage();
    charger->ocv  = aml1216_cal_ocv(charger->ibat, charger->vbat, charger->charge_status);

    ocv_voltage = charger->ocv;
    if (val & 0x40) {
        AML1216_INFO("%s, charge timeout, val:0x%02x, reset charger now\n", __func__, val);
        aml1216_set_charge_enable(0);
        msleep(1000);
        aml1216_set_charge_enable(1);               
    }
    if (aml1216_get_otp_version() == 0) {
        if (((vsys_voltage > charger->vbat) && (vsys_voltage - charger->vbat < 500)) || (charger->vbat > 3950)) {
            printk("%s, vsys is not large, or vbat is too large, vsys:%d, vbat:%d\n", __func__, vsys_voltage, charger->vbat);
            aml1216_set_charge_enable(0);
        } else {
            aml1216_set_charge_enable(1);
        }
    }

    return 0;
}

static void aml1216_charging_monitor(struct work_struct *work)
{
    struct   aml1216_supply *supply;
    struct   aml_charger    *charger;
    int32_t  pre_rest_cap;
    uint8_t  pre_chg_status;
    uint8_t  pre_pwr_status;
    struct   aml_pmu_api *api = aml_pmu_get_api();
    static bool api_flag = false;

    supply  = container_of(work, struct aml1216_supply, work.work);
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
    if (aml1216_battery) {
        if (!api) {
            schedule_delayed_work(&supply->work, supply->interval);
            return ;                                                // KO is not ready
        }
        if (api && !api_flag) {
            api_flag = true;
            if (api->pmu_probe_process) {
                api->pmu_probe_process(charger, aml1216_battery);
            }
        }
        api->pmu_update_battery_capacity(charger, aml1216_battery); 
    } else {
        aml1216_update_state(charger);
    }

    /*
     * protection for over-discharge with large loading usage
     */
    if (charger->rest_vol <= 0 && 
        charger->ext_valid     && 
        charger->charge_status == CHARGER_DISCHARGING) {
        over_discharge_cnt++;
        if (over_discharge_cnt >= 5) {
            AML1216_ERR("%s, battery is over-discharge now, force system power off\n", __func__);
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
        AML1216_INFO("battery vol change: %d->%d vsys:%d\n", pre_rest_cap, charger->rest_vol, aml1216_get_vsys_voltage());
        if (unlikely(charger->resume)) {
            charger->resume = 0;                                        // MUST clear this flag
        }
        power_supply_changed(&supply->batt);
    #ifdef CONFIG_HAS_EARLYSUSPEND
        if (in_early_suspend && (pre_pwr_status != charger->ext_valid)) {
            wake_lock(&aml1216_lock);
            AML_PMU_INFO("%s, usb power status changed in early suspend, wake up now\n", __func__);
            input_report_key(aml1216_power_key, KEY_POWER, 1);          // assume power key pressed 
            input_sync(aml1216_power_key);
        }
    #endif
    } 
    /* reschedule for the next time */
    schedule_delayed_work(&supply->work, supply->interval);
}

#if defined CONFIG_HAS_EARLYSUSPEND
static void aml1216_earlysuspend(struct early_suspend *h)
{
    struct aml1216_supply *supply = (struct aml1216_supply *)h->param;
    if (aml1216_battery) {
        aml1216_set_charging_current(aml1216_battery->pmu_suspend_chgcur);
    }
    early_power_status = supply->aml_charger.ext_valid; 
    in_early_suspend = 1;
}

static void aml1216_lateresume(struct early_suspend *h)
{
    struct  aml1216_supply *supply = (struct aml1216_supply *)h->param;

    schedule_work(&supply->work.work);                                      // update for upper layer 
    if (aml1216_battery) {
        aml1216_set_charging_current(aml1216_battery->pmu_resume_chgcur);
        input_report_key(aml1216_power_key, KEY_POWER, 0);                  // cancel power key 
        input_sync(aml1216_power_key);
    }
    early_power_status = supply->aml_charger.ext_valid; 
    in_early_suspend = 0;
    wake_unlock(&aml1216_lock);
}
#endif

irqreturn_t aml1216_irq_handler(int irq, void *dev_id)
{
    struct   aml1216_supply *supply = (struct aml1216_supply *)dev_id;

    disable_irq_nosync(supply->irq);
    schedule_work(&supply->irq_work);

    return IRQ_HANDLED;
}

static void aml1216_irq_work_func(struct work_struct *work)
{
    struct aml1216_supply *supply = container_of(work, struct aml1216_supply, irq_work);

    //TODO: add code here
    enable_irq(supply->irq);
}

struct aml_pmu_driver aml1216_pmu_driver = {
    .name                      = "aml1216",
    .pmu_get_coulomb           = aml1216_get_coulomber, 
    .pmu_clear_coulomb         = aml1216_clear_coulomber,
    .pmu_update_status         = aml1216_update_state,
    .pmu_set_rdc               = NULL,
    .pmu_set_gpio              = aml1216_set_gpio,
    .pmu_get_gpio              = aml1216_get_gpio,
    .pmu_reg_read              = aml1216_read,
    .pmu_reg_write             = aml1216_write,
    .pmu_reg_reads             = aml1216_reads,
    .pmu_reg_writes            = aml1216_writes,
    .pmu_set_bits              = aml1216_set_bits,
    .pmu_set_usb_current_limit = aml1216_set_usb_current_limit,
    .pmu_set_charge_current    = aml1216_set_charging_current,
    .pmu_power_off             = aml1216_power_off,
};

static int aml1216_battery_probe(struct platform_device *pdev)
{
    struct   aml1216_supply *supply;
    struct   aml_charger    *charger;
    int      ret;
    uint32_t tmp2;

	AML1216_DBG("call %s in", __func__);

    g_aml1216_init = pdev->dev.platform_data;
    if (g_aml1216_init == NULL) {
        AML1216_ERR("%s, NO platform data\n", __func__);
        return -EINVAL;
    }
    aml1216_power_key = input_allocate_device();
    if (!aml1216_power_key) {
        kfree(aml1216_power_key);
        return -ENODEV;
    }

    aml1216_power_key->name       = pdev->name;
    aml1216_power_key->phys       = "m1kbd/input2";
    aml1216_power_key->id.bustype = BUS_HOST;
    aml1216_power_key->id.vendor  = 0x0001;
    aml1216_power_key->id.product = 0x0001;
    aml1216_power_key->id.version = 0x0100;
    aml1216_power_key->open       = NULL;
    aml1216_power_key->close      = NULL;
    aml1216_power_key->dev.parent = &pdev->dev;

    set_bit(EV_KEY, aml1216_power_key->evbit);
    set_bit(EV_REL, aml1216_power_key->evbit);
    set_bit(KEY_POWER, aml1216_power_key->keybit);

    ret = input_register_device(aml1216_power_key);

#ifdef CONFIG_UBOOT_BATTERY_PARAMETERS 
    if (get_uboot_battery_para_status() == UBOOT_BATTERY_PARA_SUCCESS) {
        aml1216_battery = get_uboot_battery_para();
        AML1216_DBG("use uboot passed battery parameters\n");
    } else {
        aml1216_battery = g_aml1216_init->board_battery; 
        AML1216_DBG("uboot battery parameter not get, use BSP configed battery parameters\n");
    }
#else
    aml1216_battery = g_aml1216_init->board_battery; 
    AML1216_DBG("use BSP configed battery parameters\n");
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

    g_aml1216_supply = supply;
    charger = &supply->aml_charger;
    if (aml1216_battery) {
        for (tmp2 = 1; tmp2 < 16; tmp2++) {
            if (!charger->ocv_empty && aml1216_battery->pmu_bat_curve[tmp2].discharge_percent > 0) {
                charger->ocv_empty = aml1216_battery->pmu_bat_curve[tmp2-1].ocv;
            }
            if (!charger->ocv_full && aml1216_battery->pmu_bat_curve[tmp2].discharge_percent == 100) {
                charger->ocv_full = aml1216_battery->pmu_bat_curve[tmp2].ocv;    
            }
        }

        supply->irq = aml1216_battery->pmu_irq_id;
        supply->battery_info->technology         = aml1216_battery->pmu_battery_technology;
        supply->battery_info->voltage_max_design = aml1216_battery->pmu_init_chgvol;
        supply->battery_info->energy_full_design = aml1216_battery->pmu_battery_cap;
        supply->battery_info->voltage_min_design = charger->ocv_empty * 1000;
        supply->battery_info->use_for_apm        = 1;
        supply->battery_info->name               = aml1216_battery->pmu_battery_name;
    } else {
        AML1216_ERR(" NO BATTERY_PARAMETERS FOUND\n");
    }

    charger->soft_limit_to99     = g_aml1216_init->soft_limit_to99;
    charger->coulomb_type        = COULOMB_BOTH; 
    supply->charge_timeout_retry = g_aml1216_init->charge_timeout_retry;
    aml1216_update_state(charger);
#ifdef CONFIG_AMLOGIC_USB
    INIT_WORK(&aml1216_otg_work, aml1216_otg_work_fun);
    if (aml1216_charger_job.flag) {     // do later job for usb charger detect
        aml1216_usb_charger(NULL, aml1216_charger_job.value, NULL);    
        aml1216_charger_job.flag = 0;
    }
    if (aml1216_otg_job.flag) {
        aml1216_otg_change(NULL, aml1216_otg_job.value, NULL);    
        aml1216_otg_job.flag = 0;
    }
#endif
    if (supply->irq == AML1216_IRQ_NUM) {
        INIT_WORK(&supply->irq_work, aml1216_irq_work_func); 
        ret = request_irq(supply->irq, 
                          aml1216_irq_handler, 
                          IRQF_DISABLED | IRQF_SHARED,
                          AML1216_IRQ_NAME,
                          supply); 
        if (ret) {
            AML1216_DBG("request irq failed, ret:%d, irq:%d\n", ret, supply->irq);    
        }
    }

    ret = aml1216_first_init(supply);
    if (ret) {
        goto err_charger_init;
    }

    aml1216_battery_setup_psy(supply);
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

    ret = aml1216_supply_create_attrs(&supply->batt);
    if(ret){
        return ret;
    }

    platform_set_drvdata(pdev, supply);

    supply->interval = msecs_to_jiffies(AML1216_WORK_CYCLE);
    INIT_DELAYED_WORK(&supply->work, aml1216_charging_monitor);
    schedule_delayed_work(&supply->work, supply->interval);

#ifdef CONFIG_HAS_EARLYSUSPEND
    aml1216_early_suspend.suspend = aml1216_earlysuspend;
    aml1216_early_suspend.resume  = aml1216_lateresume;
    aml1216_early_suspend.level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 2;
    aml1216_early_suspend.param   = supply;
    register_early_suspend(&aml1216_early_suspend);
    wake_lock_init(&aml1216_lock, WAKE_LOCK_SUSPEND, "aml1216");
#endif
    if (aml1216_battery) {
        power_supply_changed(&supply->batt);                    // update battery status
    }
    
    aml1216_dump_all_register(NULL);
	AML1216_DBG("call %s exit, ret:%d", __func__, ret);
    return ret;

err_ps_register:
    free_irq(supply->irq, supply);
    cancel_delayed_work_sync(&supply->work);

err_charger_init:
    kfree(supply->battery_info);
    kfree(supply);
    input_unregister_device(aml1216_power_key);
    kfree(aml1216_power_key);
	AML1216_DBG("call %s exit, ret:%d", __func__, ret);
    return ret;
}

static int aml1216_battery_remove(struct platform_device *dev)
{
    struct aml1216_supply *supply= platform_get_drvdata(dev);

    cancel_work_sync(&supply->irq_work);
    cancel_delayed_work_sync(&supply->work);
    power_supply_unregister( &supply->usb);
    power_supply_unregister( &supply->ac);
    power_supply_unregister( &supply->batt);
    
    free_irq(supply->irq, supply);
    kfree(supply->battery_info);
    kfree(supply);
    input_unregister_device(aml1216_power_key);
    kfree(aml1216_power_key);

    return 0;
}


static int aml1216_suspend(struct platform_device *dev, pm_message_t state)
{
    struct aml1216_supply *supply = platform_get_drvdata(dev);
    struct aml_pmu_api  *api;

    cancel_delayed_work_sync(&supply->work);
    if (aml1216_battery) {
        api = aml_pmu_get_api();
        if (api && api->pmu_suspend_process) {
            api->pmu_suspend_process(&supply->aml_charger);
        }
    }
#ifdef CONFIG_HAS_EARLYSUSPEND
    if (early_power_status != supply->aml_charger.ext_valid) {
        AML1216_DBG("%s, power status changed, prev:%x, now:%x, exit suspend process\n", 
                __func__, early_power_status, supply->aml_charger.ext_valid);
        input_report_key(aml1216_power_key, KEY_POWER, 1);              // assume power key pressed 
        input_sync(aml1216_power_key);
        return -1;
    }
    in_early_suspend = 0;
#endif

    return 0;
}

static int aml1216_resume(struct platform_device *dev)
{
    struct aml1216_supply *supply = platform_get_drvdata(dev);
    struct aml_pmu_api    *api;

    if (aml1216_battery) {
        api = aml_pmu_get_api();
        if (api && api->pmu_resume_process) {
            api->pmu_resume_process(&supply->aml_charger, aml1216_battery);
        }
    }
    schedule_work(&supply->work.work);

    return 0;
}

static void aml1216_shutdown(struct platform_device *dev)
{
    // add code here
#ifdef CONFIG_HAS_EARLYSUSPEND
    wake_lock_destroy(&aml1216_lock);
#endif
}

static struct platform_driver aml1216_battery_driver = {
    .driver = {
        .name  = AML1216_DRIVER_NAME, 
        .owner = THIS_MODULE,
    },
    .probe    = aml1216_battery_probe,
    .remove   = aml1216_battery_remove,
    .suspend  = aml1216_suspend,
    .resume   = aml1216_resume,
    .shutdown = aml1216_shutdown,
};

static int aml1216_battery_init(void)
{
    int ret;
    ret = platform_driver_register(&aml1216_battery_driver);
	AML1216_DBG("call %s, ret = %d\n", __func__, ret);
	return ret;
}

static void aml1216_battery_exit(void)
{
    platform_driver_unregister(&aml1216_battery_driver);
}

//subsys_initcall(aml1216_battery_init);
module_init(aml1216_battery_init);
module_exit(aml1216_battery_exit);

MODULE_DESCRIPTION("AML PMU AML1216 battery driver");
MODULE_AUTHOR("tao.zeng@amlogic.com, Amlogic, Inc");
MODULE_LICENSE("GPL");
