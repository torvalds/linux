
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

#define MAX_BUF         100
#define CHECK_DRIVER()      \
    if (!g_aml1218_client) {        \
        AML1218_DBG("driver is not ready right now, wait...\n");   \
        dump_stack();       \
        return -ENODEV;     \
    }

#define DEBUG_DVFS      0

int aml1218_write(int32_t add, uint8_t val)
{
    int ret;
    uint8_t buf[3] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1218_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1218_client;

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    buf[2] = val & 0xff;
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        AML1218_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1218_write);

int aml1218_write16(int32_t add, uint16_t val)
{
    int ret;
    uint8_t buf[4] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1218_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1218_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    buf[2] = val & 0xff;
    buf[3] = (val >> 8) & 0xff;
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        AML1218_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1218_write16);

int aml1218_writes(int32_t add, uint8_t *buff, int len)
{
    int ret;
    uint8_t buf[MAX_BUF] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1218_ADDR,
            .flags = 0,
            .len   = len + 2,
            .buf   = buf,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1218_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    memcpy(buf + 2, buff, len > MAX_BUF ? MAX_BUF : len);
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        AML1218_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1218_writes);

int aml1218_read(int add, uint8_t *val)
{
    int ret;
    uint8_t buf[2] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1218_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = AML1218_ADDR,
            .flags = I2C_M_RD,
            .len   = 1,
            .buf   = val,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1218_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        AML1218_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1218_read);

int aml1218_read16(int add, uint16_t *val)
{
    int ret;
    uint8_t buf[2] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1218_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = AML1218_ADDR,
            .flags = I2C_M_RD,
            .len   = 2, 
            .buf   = (uint8_t *)val,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1218_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        AML1218_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1218_read16);

int aml1218_reads(int add, uint8_t *buff, int len)
{
    int ret;
    uint8_t buf[2] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1218_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = AML1218_ADDR,
            .flags = I2C_M_RD,
            .len   = len,
            .buf   = buff,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1218_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        AML1218_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1218_reads);

int aml1218_set_bits(int addr, uint8_t bits, uint8_t mask)
{
    uint8_t val; 
    int ret; 
 
    ret = aml1218_read(addr, &val); 
    if (ret) { 
        return ret; 
    } 
    val &= ~(mask); 
    val |=  (bits & mask); 
    return aml1218_write(addr, val); 
} 
EXPORT_SYMBOL_GPL(aml1218_set_bits); 

static int find_idx(uint32_t start, uint32_t target, uint32_t step, int size)
{
    int i = 0; 

    if (start < target) {
        AML1218_DBG("%s, invalid input of voltage:%u\n", __func__, target);    
        return -1;
    }
    do { 
        if ((start - step) < target) {
            break;    
        }    
        start -= step;
        i++; 
    } while (i < size);
    if (i >= size) {
        AML1218_DBG("%s, input voltage %u outof range\n", __func__, target);    
        return -1;
    }

    return i;
}

static unsigned int VDDEE_voltage_table[] = {                   // voltage table of VDDEE
    1184, 1170, 1156, 1142, 1128, 1114, 1100, 1086, 
    1073, 1059, 1045, 1031, 1017, 1003, 989, 975, 
    961,  947,  934,  920,  906,  892,  878, 864, 
    850,  836,  822,  808,  794,  781,  767, 753  
};

int find_idx_by_vddEE_voltage(int voltage, unsigned int *table)
{
    int i;

    for (i = 0; i < 32; i++) {
        if (voltage >= table[i]) {
            break;    
        }    
    }    
    if (voltage == table[i]) {
        return i;    
    }    
    if (i == 0) {
        return 0;
    } else {
        return i - 1; 
    }
}

int aml1218_set_vddEE_voltage(int voltage)
{
    int addr = 0x005d;
    int idx_to, idx_cur;
    unsigned char val; 

    aml1218_read(addr, &val);
    idx_cur = ((val & 0x7c) >> 2);
    idx_to = find_idx_by_vddEE_voltage(voltage, VDDEE_voltage_table);

    val &= ~0x7c;
    val |= (idx_to << 2);

#ifdef DEBUG_DVFS
#if DEBUG_DVFS
    printk("%s, idx_to:%x, idx_cur:%x, val:%x, voltage:%d\n", __func__, idx_to, idx_cur, val, voltage);
#endif
#endif
    aml1218_write(addr, val);
    udelay(5 * 100);

    return 0;
}

int aml1218_set_dcdc_voltage(int dcdc, uint32_t voltage)
{
    int addr;
    int idx_to;
    int range    = 64; 
    int step     = 1875 * 10; 
    int start    = 1881 * 1000;
    int idx_cur;
    uint8_t val = 0;
    static uint8_t dcdc_val[3] = {};

    if (dcdc == 4) {
        aml1218_set_vddEE_voltage(voltage / 1000);            
        return 0;
    }
    if (dcdc > 3 || dcdc < 0) {
        return -1;    
    }   
    addr = 0x34+(dcdc-1)*9;
    if (dcdc == 3) {
        step     = 50 * 1000; 
        range    = 32; 
        start    = 3600 * 1000;
    }   
    if (dcdc_val[dcdc] == 0) {
        aml1218_read(addr, &val);                               // read first time
    } else {
        val = dcdc_val[dcdc];
    }
    idx_to   = find_idx(start, voltage, step, range);
    idx_cur  = (val & 0x7e) >> 1;

    step = idx_cur - idx_to;
    if (step < 0) {
        step = -step;
    }
    val &= ~0x7e;
    val |= (idx_to << 1);
    aml1218_write(addr, val);
    udelay(20 * step);
    dcdc_val[dcdc] = val;
    return 0;
}
EXPORT_SYMBOL_GPL(aml1218_set_dcdc_voltage);

int aml1218_get_dcdc_voltage(int dcdc, uint32_t *uV)
{
    int addr;
    uint8_t val;
    int ret;
    //int start;
	
    if (dcdc == 4) {
        addr = 0x5d;
        ret = aml1218_read(addr, &val);
        if (ret) {
            return ret;    
        }
        *uV = VDDEE_voltage_table[(val >> 2) & 0x1f] * 1000;
        return 0;
    }
    if (dcdc > 3 || dcdc < 0) {
        return -EINVAL;    
    }

    addr = 0x34+(dcdc-1)*9;
    
    ret = aml1218_read(addr, &val);
    if (ret) {
        return ret;    
    }
    val &= 0x7e;
    val >>= 1;
    if (dcdc == 3)
    {
        *uV = (3600000 - val * 50000); //step: 50 mv
    }
    else
    {
        *uV = (1881300 - val * 18750); //step: 20 mv
    }
    
    return 0;
}
EXPORT_SYMBOL_GPL(aml1218_get_dcdc_voltage);
