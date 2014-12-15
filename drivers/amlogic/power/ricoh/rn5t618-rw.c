
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

#define MAX_BUF         100
#define CHECK_DRIVER()      \
    if (!g_rn5t618_client) {        \
        RICOH_DBG("driver is not ready right now, wait...\n");   \
        dump_stack();       \
        return -ENODEV;     \
    }

int rn5t618_write(int add, uint8_t val)
{
    int ret;
    uint8_t buf[2] = {};
    struct i2c_client *pdev = g_rn5t618_client;
    struct i2c_msg msg[] = {
        {
            .addr  = RN5T618_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        }
    };

    CHECK_DRIVER();

    buf[0] = add & 0xff;
    buf[1] = val & 0xff;
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        RICOH_DBG("%s: i2c transfer failed, ret:%d\n", __func__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(rn5t618_write);

int rn5t618_writes(int add, uint8_t *buff, int len)
{
    int ret;
    uint8_t buf[MAX_BUF] = {};
    struct i2c_client *pdev = g_rn5t618_client;
    struct i2c_msg msg[] = {
        {
            .addr  = RN5T618_ADDR,
            .flags = 0,
            .len   = len + 1,
            .buf   = buf,
        }
    };

    CHECK_DRIVER();

    buf[0] = add & 0xff;
    memcpy(buf + 1, buff, len > MAX_BUF ? MAX_BUF : len);
    ret = i2c_transfer(pdev->adapter, msg, 1);
    if (ret < 0) {
        RICOH_DBG("%s: i2c transfer failed, ret:%d\n", __func__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(rn5t618_writes);

int rn5t618_read(int add, uint8_t *val)
{
    int ret;
    uint8_t buf[1] = {};
    struct i2c_client *pdev = g_rn5t618_client;
    struct i2c_msg msg[] = {
        {
            .addr  = RN5T618_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = RN5T618_ADDR,
            .flags = I2C_M_RD,
            .len   = 1,
            .buf   = val,
        }
    };

    CHECK_DRIVER();

    buf[0] = add & 0xff;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        RICOH_DBG("%s: i2c transfer failed, ret:%d\n", __func__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(rn5t618_read);

int rn5t618_reads(int add, uint8_t *buff, int len)
{
    int ret;
    uint8_t buf[1] = {};
    struct i2c_client *pdev = g_rn5t618_client;
    struct i2c_msg msg[] = {
        {
            .addr  = RN5T618_ADDR,
            .flags = 0,
            .len   = sizeof(buf),
            .buf   = buf,
        },
        {
            .addr  = RN5T618_ADDR,
            .flags = I2C_M_RD,
            .len   = len,
            .buf   = buff,
        }
    };

    CHECK_DRIVER();

    buf[0] = add & 0xff;
    ret = i2c_transfer(pdev->adapter, msg, 2);
    if (ret < 0) {
        RICOH_DBG("%s: i2c transfer failed, ret:%d\n", __func__, ret);
        return ret;
    }
    return 0;
}
EXPORT_SYMBOL_GPL(rn5t618_reads);

int rn5t618_set_bits(int addr, uint8_t bits, uint8_t mask)
{
    uint8_t val; 
    int ret; 
 
    ret = rn5t618_read(addr, &val); 
    if (ret) { 
        return ret; 
    } 
    val &= ~(mask); 
    val |=  (bits & mask); 
    return rn5t618_write(addr, val); 
} 
EXPORT_SYMBOL_GPL(rn5t618_set_bits); 

static int find_idx(uint32_t start, uint32_t target, uint32_t step, int size)
{
    int i = 0; 

    if (start >= target) {
        RICOH_DBG("%s, invalid input of voltage:%u\n", __func__, target);    
        return -1;
    }
    do { 
        if (start >= target) {
            break;    
        }    
        start += step;
        i++; 
    } while (i < size);
    if (i >= size) {
        RICOH_DBG("%s, input voltage %u outof range\n", __func__, target);    
        return -1;
    }

    return i;
}

int rn5t618_set_dcdc_voltage(int dcdc, uint32_t voltage)
{
    int addr;
    int idx_to;
    int ret;

    addr = 0x35 + dcdc;
    idx_to = find_idx(600 * 1000, voltage, 12500, 256);             // step is 12.5mV
    if (idx_to >= 0) {
        ret = rn5t618_write(addr, (uint8_t)idx_to);
        udelay(100);                                                // wait a moment
        return ret;
    } else {
        return idx_to;
    }
}
EXPORT_SYMBOL_GPL(rn5t618_set_dcdc_voltage);

int rn5t618_get_dcdc_voltage(int dcdc, uint32_t *uV)
{
    int addr = 0x35 + dcdc;
    uint8_t val;
    int ret;

    ret = rn5t618_read(addr, &val);
    if (ret) {
        return ret;    
    }
    *uV = (600 * 1000 + val * 12500);
    return 0;
}
EXPORT_SYMBOL_GPL(rn5t618_get_dcdc_voltage);
