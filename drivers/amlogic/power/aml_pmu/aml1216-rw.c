
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
    if (!g_aml1216_client) {        \
        AML1216_DBG("driver is not ready right now, wait...\n");   \
        dump_stack();       \
        return -ENODEV;     \
    }

int aml1216_write(int32_t add, uint8_t val)
{
    int ret;
    uint8_t buf[3] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1216_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1216_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    buf[2] = val & 0xff;
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        AML1216_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_write);

int aml1216_write16(int32_t add, uint16_t val)
{
    int ret;
    uint8_t buf[4] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1216_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1216_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    buf[2] = val & 0xff;
    buf[3] = (val >> 8) & 0xff;
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        AML1216_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_write16);

int aml1216_writes(int32_t add, uint8_t *buff, int len)
{
    int ret;
    uint8_t buf[MAX_BUF] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1216_ADDR,
            .flags = 0,
            .len   = len + 2,
            .buf   = buf,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1216_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    memcpy(buf + 2, buff, len > MAX_BUF ? MAX_BUF : len);
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        AML1216_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_writes);

int aml1216_read(int add, uint8_t *val)
{
    int ret;
    uint8_t buf[2] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1216_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = AML1216_ADDR,
            .flags = I2C_M_RD,
            .len   = 1,
            .buf   = val,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1216_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        AML1216_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_read);

int aml1216_read16(int add, uint16_t *val)
{
    int ret;
    uint8_t buf[2] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1216_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = AML1216_ADDR,
            .flags = I2C_M_RD,
            .len   = 2, 
            .buf   = (uint8_t *)val,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1216_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        AML1216_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_read16);

int aml1216_reads(int add, uint8_t *buff, int len)
{
    int ret;
    uint8_t buf[2] = {};
    struct i2c_client *pdev;
    struct i2c_msg msg[] = {
        {
            .addr  = AML1216_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = AML1216_ADDR,
            .flags = I2C_M_RD,
            .len   = len,
            .buf   = buff,
        }
    };

    CHECK_DRIVER();
    pdev = g_aml1216_client; 

    buf[0] = add & 0xff;
    buf[1] = (add >> 8) & 0x0f;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        AML1216_DBG("%s: i2c transfer failed, ret:%d\n", __FUNCTION__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_reads);

int aml1216_set_bits(int addr, uint8_t bits, uint8_t mask)
{
    uint8_t val; 
    int ret; 
 
    ret = aml1216_read(addr, &val); 
    if (ret) { 
        return ret; 
    } 
    val &= ~(mask); 
    val |=  (bits & mask); 
    return aml1216_write(addr, val); 
} 
EXPORT_SYMBOL_GPL(aml1216_set_bits); 

static int find_idx(uint32_t start, uint32_t target, uint32_t step, int size)
{
    int i = 0; 

    if (start < target) {
        AML1216_DBG("%s, invalid input of voltage:%u\n", __func__, target);    
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
        AML1216_DBG("%s, input voltage %u outof range\n", __func__, target);    
        return -1;
    }

    return i;
}

int aml1216_set_dcdc_voltage(int dcdc, uint32_t voltage)
{
    int addr;
    int idx_to;
    int range    = 64; 
    int step     = 1875 * 10; 
    int start    = 1881 * 1000;
    int bit_mask = 0x3f;
    int idx_cur;
    uint8_t val = 0;
    static uint8_t dcdc_val[3] = {};

    if (dcdc > 3 || dcdc < 0) {
        return -1;    
    }   
    addr = 0x34+(dcdc-1)*9;
    if (dcdc == 3) {
        step     = 50 * 1000; 
        range    = 32; 
        start    = 3600 * 1000;
        bit_mask = 0x1f;
    }   
    if (dcdc_val[dcdc] == 0) {
        aml1216_read(addr, &val);                               // read first time
    } else {
        val = dcdc_val[dcdc];
    }
    idx_to   = find_idx(start, voltage, step, range);
    idx_cur  = val >> 2;
    while (idx_cur != idx_to) {
        if (idx_cur < idx_to) {                                 // adjust to target voltage step by step
            idx_cur++;    
        } else {
            idx_cur--;
        }
        val &= ~0xfc;
        val |= (idx_cur << 2);
        aml1216_write(addr, val);
        udelay(50);                                             // atleast delay 100uS
    }
    dcdc_val[dcdc] = val;
    return 0;
}
EXPORT_SYMBOL_GPL(aml1216_set_dcdc_voltage);

int aml1216_get_dcdc_voltage(int dcdc, uint32_t *uV)
{
    int addr;
    uint8_t val;
    int ret;
//    int start;

    if (dcdc > 3 || dcdc < 0) {
        return -EINVAL;    
    }

    addr = 0x34+(dcdc-1)*9;
    
    ret = aml1216_read(addr, &val);
    if (ret) {
        return ret;    
    }
    val &= 0xfc;
    val >>= 2;
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
EXPORT_SYMBOL_GPL(aml1216_get_dcdc_voltage);
