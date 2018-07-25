/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LINUX_SSVCABRIO_PLATFORM_H
#define _LINUX_SSVCABRIO_PLATFORM_H 
#include <linux/mmc/host.h>
#include <hwif/sdio/sdio_def.h>
#define SSVCABRIO_PLAT_EEP_MAX_WORDS 2048
#define SSV_REG_WRITE(dev,reg,val) \
        (sh)->priv->ops->writereg((sh)->sc->dev, (reg), (val))
#define SSV_REG_READ(dev,reg,buf) \
        (sh)->priv->ops->readreg((sh)->sc->dev, (reg), (buf))
#if 0
#define SSV_REG_WRITE(sh,reg,val) \
        (sh)->priv->ops->writereg((sh)->sc->dev, (reg), (val))
#define SSV_REG_READ(sh,reg,buf) \
        (sh)->priv->ops->readreg((sh)->sc->dev, (reg), (buf))
#define SSV_REG_CONFIRM(sh,reg,val) \
{ \
    u32 regval; \
    SSV_REG_READ(sh, reg, &regval); \
    if (regval != (val)) { \
        printk("[0x%08x]: 0x%08x!=0x%08x\n",\
        (reg), (val), regval); \
        return -1; \
    } \
}
#define SSV_REG_SET_BITS(sh,reg,set,clr) \
{ \
    u32 reg_val; \
    SSV_REG_READ(sh, reg, &reg_val); \
    reg_val &= ~(clr); \
    reg_val |= (set); \
    SSV_REG_WRITE(sh, reg, reg_val); \
}
#endif
struct ssv6xxx_hwif_ops {
    int __must_check (*read)(struct device *child, void *buf,size_t *size);
    int __must_check (*write)(struct device *child, void *buf, size_t len,u8 queue_num);
    int __must_check (*readreg)(struct device *child, u32 addr, u32 *buf);
    int __must_check (*writereg)(struct device *child, u32 addr, u32 buf);
    int (*trigger_tx_rx)(struct device *child);
    int (*irq_getmask)(struct device *child, u32 *mask);
    void (*irq_setmask)(struct device *child,int mask);
    void (*irq_enable)(struct device *child);
    void (*irq_disable)(struct device *child,bool iswaitirq);
    int (*irq_getstatus)(struct device *child,int *status);
    void (*irq_request)(struct device *child,irq_handler_t irq_handler,void *irq_dev);
    void (*irq_trigger)(struct device *child);
    void (*pmu_wakeup)(struct device *child);
    int __must_check (*load_fw)(struct device *child, u8 *firmware_name, u8 openfile);
    int (*cmd52_read)(struct device *child, u32 addr, u32 *value);
    int (*cmd52_write)(struct device *child, u32 addr, u32 value);
    bool (*support_scatter)(struct device *child);
    int (*rw_scatter)(struct device *child, struct sdio_scatter_req *scat_req);
    bool (*is_ready)(struct device *child);
    int (*write_sram)(struct device *child, u32 addr, u8 *data, u32 size);
    void (*interface_reset)(struct device *child);
};
struct ssv6xxx_if_debug {
    struct device *dev;
    struct platform_device *pdev;
};
struct ssv6xxx_platform_data {
    atomic_t irq_handling;
    bool is_enabled;
    unsigned short vendor;
    unsigned short device;
    struct ssv6xxx_hwif_ops *ops;
};
#endif
