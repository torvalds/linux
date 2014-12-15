/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2013/1/31   18:20
 *
 *******************************************************************/
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/pinctrl/consumer.h>
#include <mach/am_regs.h>
#include <linux/delay.h>
#include <linux/amlogic/battery_parameter.h>
#include <linux/amlogic/aml_pmu_common.h>

#define AML_I2C_BUS_AO     0
#define AML_I2C_BUS_A       1
#define AML_I2C_BUS_B       2

#define DEBUG_TREE      0
#define DEBUG_PARSE     0
#define DBG(format, args...) printk("%s, "format, __func__, ##args)

/*
 * must make sure value is 32 bit when use this macro
 * otherwise you should use another variable to get result value
 */
#define PARSE_UINT32_PROPERTY(node, prop_name, value, exception)        \
    if (of_property_read_u32(node, prop_name, (u32*)(&value))) {        \
        DBG("failed to get property: %s\n", prop_name);                 \
        goto exception;                                                 \
    }                                                                   \
    if (DEBUG_PARSE) {                                                  \
        DBG("get property:%25s, value:0x%08x, dec:%8d\n",               \
            prop_name, value, value);                                   \
    }

#define PARSE_STRING_PROPERTY(node, prop_name, value, exception)                \
    if (of_property_read_string(node, prop_name, (const char **)&value)) {      \
        DBG("failed to get property: %s\n", prop_name);                 \
        goto exception;                                                 \
    }                                                                   \
    if (DEBUG_PARSE) {                                                  \
        DBG("get property:%25s, value:%s\n",                            \
            prop_name, value);                                          \
    }

/*
 * common API for callback and drivers
 */
static void    *pmu_callback_mutex     = NULL;
static struct aml_pmu_driver *g_driver = NULL;
static struct aml_pmu_api *g_aml_pmu_api = NULL;

struct pmu_callback_group {
    char                        name[20];
    void                       *private;
    pmu_callback                callback;
    struct pmu_callback_group *next;
};

static struct pmu_callback_group *g_callbacks = NULL;

int aml_pmu_register_callback(pmu_callback callback, void *pdata, char *name)
{
    struct pmu_callback_group *cg = NULL, *cn;

    if (!pmu_callback_mutex) {
        pmu_callback_mutex = pmu_alloc_mutex();
        if (!pmu_callback_mutex) {
            printk("%s, allocate mutex failed\n", __func__);
            return -ENOMEM;
        }
    }
    pmu_mutex_lock(pmu_callback_mutex);
    cg = kzalloc(sizeof(*cg), GFP_KERNEL);
    if (!cg) {
        printk("%s, allocate callback failed\n", __func__);
        pmu_mutex_unlock(pmu_callback_mutex);
        return -ENOMEM; 
    }
    cg->callback = callback;
    cg->private  = pdata;
    strcpy(cg->name, name);
    if (!g_callbacks) {                                                 // add first callback
        g_callbacks = cg;
        printk("callback %s registed, cg:%p\n", cg->name, cg);
        pmu_mutex_unlock(pmu_callback_mutex);
        return 0;
    }
    for (cn = g_callbacks; cn->next; cn = cn->next) {
        if (name && !strcmp(cn->name, name)) {
            printk("%s, callback %s is already exist\n", __func__, name);
            pmu_mutex_unlock(pmu_callback_mutex);
            return -EINVAL;
        }
    }
    cn->next = cg;
    printk("callback %s registed, cg:%p\n", cg->name, cg);
    pmu_mutex_unlock(pmu_callback_mutex);
    return 0;
}
EXPORT_SYMBOL(aml_pmu_register_callback);

int aml_pmu_unregister_callback(char *name)
{
    struct pmu_callback_group *cn, *tmp = g_callbacks;
    int find = 0;
    pmu_mutex_lock(pmu_callback_mutex);
    if (name && !strcmp(tmp->name, name)) {                             // first node is target
        g_callbacks = g_callbacks->next;    
        kfree(tmp);
        printk("%s, callback %s unregisted\n", __func__, name);
        find = 1;
    }
    if (g_callbacks) {
        for (cn = g_callbacks->next; cn; cn = cn->next) {
            if (name && !strcmp(cn->name, name)) {
                tmp->next = cn->next;
                kfree(cn);
                printk("%s, callback %s unregisted\n", __func__, name);
                find = 1;
                break;
            }
            tmp = tmp->next;
        }
    }
    pmu_mutex_unlock(pmu_callback_mutex);
    if (!find) {
        printk("%s, callback %s not find\n", __func__, name);
        return -1;
    }
    return 0;
}
EXPORT_SYMBOL(aml_pmu_unregister_callback);

void aml_pmu_do_callbacks(struct aml_charger *charger)
{
    struct pmu_callback_group *cn;

    if (g_callbacks) {
        pmu_mutex_lock(pmu_callback_mutex);
        for (cn = g_callbacks; cn; cn = cn->next) {
            if (cn->callback) {
                cn->callback(charger, cn->private);    
            }
        }
        pmu_mutex_unlock(pmu_callback_mutex);
    }
}
EXPORT_SYMBOL(aml_pmu_do_callbacks);

long aml_pmu_get_ts(void)
{
    struct timespec ts; 
    ktime_get_ts(&ts);
    return ts.tv_sec;
}
EXPORT_SYMBOL(aml_pmu_get_ts);

int aml_pmu_register_api(struct aml_pmu_api *api)
{
    if (!api || g_aml_pmu_api) {
        printk("%s, invalid input, api:%p\n", __func__, g_aml_pmu_api);    
        return -EINVAL;
    }
    g_aml_pmu_api = api;
    return 0;
}
EXPORT_SYMBOL(aml_pmu_register_api);

void aml_pmu_clear_api(void)
{
    g_aml_pmu_api = NULL;    
}
EXPORT_SYMBOL(aml_pmu_clear_api);

struct aml_pmu_api *aml_pmu_get_api(void)
{
    return g_aml_pmu_api;    
}
EXPORT_SYMBOL(aml_pmu_get_api);

/*
 * register PMU operater for this lib
 * @driver: amlogic PMU driver to be registed
 */
int aml_pmu_register_driver(struct aml_pmu_driver *driver)
{
    if (driver == NULL) {
        printk("%s, ERROR:NULL driver\n", __func__);    
        return -1;
    }
    if (g_driver != NULL) {
        printk("%s, ERROR:driver %s has alread registed\n", __func__, driver->name);
        return -1;
    }
    if (!driver->pmu_get_coulomb || !driver->pmu_update_status) {
        printk("%s, lost important functions\n", __func__);
        return -1;
    }
    g_driver = driver;

    return 0;
}
EXPORT_SYMBOL(aml_pmu_register_driver);

/*
 * clear pmu drivers which already registered;
 */
void aml_pmu_clear_driver(void) 
{
    g_driver = NULL;
}
EXPORT_SYMBOL(aml_pmu_clear_driver);

/*
 * return registed pmu driver
 */
struct aml_pmu_driver* aml_pmu_get_driver(void)
{
    return g_driver;
}
EXPORT_SYMBOL(aml_pmu_get_driver);


/*
 * common API of parse battery parameters for each PMU
 */
int parse_battery_parameters(struct device_node *node, struct battery_parameter *battery)
{
    unsigned int *curve;
    char *bat_name = NULL;

    PARSE_UINT32_PROPERTY(node, "pmu_twi_id",             battery->pmu_twi_id,              parse_failed); 
    PARSE_UINT32_PROPERTY(node, "pmu_irq_id",             battery->pmu_irq_id,              parse_failed); 
    PARSE_UINT32_PROPERTY(node, "pmu_twi_addr",           battery->pmu_twi_addr,            parse_failed); 
    PARSE_UINT32_PROPERTY(node, "pmu_battery_rdc",        battery->pmu_battery_rdc,         parse_failed); 
    PARSE_UINT32_PROPERTY(node, "pmu_battery_cap",        battery->pmu_battery_cap,         parse_failed); 
    PARSE_UINT32_PROPERTY(node, "pmu_battery_technology", battery->pmu_battery_technology,  parse_failed); 
    PARSE_STRING_PROPERTY(node, "pmu_battery_name",       bat_name,                         parse_failed);
    /*
     * of_property_read_string only change output pointer address,
     * so need copy string from pointed address to array.
     */
    if (bat_name) {
        strcpy(battery->pmu_battery_name, bat_name);    
    }
    PARSE_UINT32_PROPERTY(node, "pmu_init_chgvol",        battery->pmu_init_chgvol,         parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_init_chgend_rate",   battery->pmu_init_chgend_rate,    parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_init_chg_enabled",   battery->pmu_init_chg_enabled,    parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_init_adc_freq",      battery->pmu_init_adc_freq,       parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_init_adc_freqc",     battery->pmu_init_adc_freqc,      parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_init_chg_pretime",   battery->pmu_init_chg_pretime,    parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_init_chg_csttime",   battery->pmu_init_chg_csttime,    parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_init_chgcur",        battery->pmu_init_chgcur,         parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_suspend_chgcur",     battery->pmu_suspend_chgcur,      parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_resume_chgcur",      battery->pmu_resume_chgcur,       parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_shutdown_chgcur",    battery->pmu_shutdown_chgcur,     parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_usbcur_limit",       battery->pmu_usbcur_limit,        parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_usbcur",             battery->pmu_usbcur,              parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_usbvol_limit",       battery->pmu_usbvol_limit,        parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_usbvol",             battery->pmu_usbvol,              parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_pwroff_vol",         battery->pmu_pwroff_vol,          parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_pwron_vol",          battery->pmu_pwron_vol,           parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_pekoff_time",        battery->pmu_pekoff_time,         parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_pekoff_en",          battery->pmu_pekoff_en,           parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_peklong_time",       battery->pmu_peklong_time,        parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_pwrok_time",         battery->pmu_pwrok_time,          parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_pwrnoe_time",        battery->pmu_pwrnoe_time,         parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_intotp_en",          battery->pmu_intotp_en,           parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_pekon_time",         battery->pmu_pekon_time,          parse_failed);
    PARSE_UINT32_PROPERTY(node, "pmu_charge_efficiency",  battery->pmu_charge_efficiency,   parse_failed);
    /*
     * These 4 members are not "MUST HAVE", so if parse failed, just keep going
     */
    PARSE_UINT32_PROPERTY(node, "pmu_ntc_enable",         battery->pmu_ntc_enable,          parse_next1);
parse_next1:
    PARSE_UINT32_PROPERTY(node, "pmu_ntc_ts_current",     battery->pmu_ntc_ts_current,      parse_next2);
parse_next2:
    PARSE_UINT32_PROPERTY(node, "pmu_ntc_lowtempvol",     battery->pmu_ntc_lowtempvol,      parse_next3);
parse_next3:
    PARSE_UINT32_PROPERTY(node, "pmu_ntc_hightempvol",    battery->pmu_ntc_hightempvol,     parse_next4);
parse_next4:
    curve = (unsigned int *)battery->pmu_bat_curve;
    if (of_property_read_u32_array(node, 
                                   "pmu_bat_curve", 
                                   curve, 
                                   sizeof(battery->pmu_bat_curve) / sizeof(int))) {
        DBG("failed to read battery curve\n");
        goto parse_failed;
    }
    return 0;

parse_failed:
    return -EINVAL;
}
EXPORT_SYMBOL(parse_battery_parameters);

static int aml_pmus_probe(struct platform_device *pdev)
{
    struct device_node      *pmu_node = pdev->dev.of_node;
    struct device_node      *child;
    struct i2c_board_info   board_info;
    struct i2c_adapter      *adapter;
    struct i2c_client       *client;
    //struct property         *prop;
    //int    val;
    int    err;
    int    addr;
    int    bus_type = -1;
    const  char *str;

    for_each_child_of_node(pmu_node, child) {
        /* register exist pmu */
        printk("%s, child name:%s\n", __func__, child->name);
        err = of_property_read_string(child, "i2c_bus", &str);
        if (err) {
            printk("%s, get 'i2c_bus' failed, ret:%d\n", __func__, err);
            continue;
        }
        printk("%s, i2c_bus:%s\n", __func__, str);
        if (!strncmp(str, "i2c_bus_ao", 10)) { 
            bus_type = AML_I2C_BUS_AO;
        } else if (!strncmp(str, "i2c_bus_b", 9)) {
            bus_type = AML_I2C_BUS_B;
        } else if (!strncmp(str, "i2c_bus_a", 9)) {
            bus_type = AML_I2C_BUS_A;
        } else {
            bus_type = AML_I2C_BUS_AO; 
        }
        err = of_property_read_string(child, "status", &str);
        if (err) {
            printk("%s, get 'status' failed, ret:%d\n", __func__, err);
            continue;
        }
        if (strcmp(str, "okay") && strcmp(str, "ok")) {              // status is not OK, do not probe it
            printk("%s, device %s status is %s, stop probe it\n", __func__, child->name, str); 
            continue;
        }
        err = of_property_read_u32(child, "reg", &addr);
        if (err) {
            printk("%s, get 'reg' failed, ret:%d\n", __func__, err);
            continue;
        }
        memset(&board_info, 0, sizeof(board_info));
        adapter = i2c_get_adapter(bus_type);
        if (!adapter) {
            printk("%s, wrong i2c adapter:%d\n", __func__, bus_type);
        }
        err = of_property_read_string(child, "compatible", &str);
        if (err) {
            printk("%s, get 'compatible' failed, ret:%d\n", __func__, err);
            continue;
        }
        strncpy(board_info.type, str, I2C_NAME_SIZE);
        board_info.addr = addr;
        board_info.of_node = child;                                     // for device driver
        client = i2c_new_device(adapter, &board_info);
        if (!client) {
            printk("%s, allocate i2c_client failed\n", __func__);    
            continue;
        }
        printk("Allocate new i2c device: adapter:%d, addr:0x%x, node name:%s, type:%s\n", 
               bus_type, addr, child->name, str);
    }
    return 0;
}    

static int aml_pmus_remove(struct platform_device *pdev)
{
    /* nothing to do */ 
    return 0;
}

static const struct of_device_id aml_pmu_dt_match[] = {
    {
        .compatible = "amlogic, aml_pmu_prober",
    },
    {}
};

static  struct platform_driver aml_pmu_prober = { 
    .probe      = aml_pmus_probe,
    .remove     = aml_pmus_remove,
    .driver     = { 
        .name   = "aml_pmu_prober",
        .owner  = THIS_MODULE,
        .of_match_table = aml_pmu_dt_match,
    },  
};

static int __init aml_pmu_probe_init(void)
{
    int ret;
    printk("call %s in\n", __func__);
    ret = platform_driver_register(&aml_pmu_prober);
    return ret;
}

static void __exit aml_pmu_probe_exit(void)
{
    platform_driver_unregister(&aml_pmu_prober);
}

subsys_initcall(aml_pmu_probe_init);
module_exit(aml_pmu_probe_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Amlogic pmu common driver");
