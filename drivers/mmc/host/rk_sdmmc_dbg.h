/*
 * Synopsys DesignWare Multimedia Card Interface driver
 *
 * Copyright (C) 2014 Fuzhou Rockchip Electronics Co.Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
 
#ifndef __RK_SDMMC_DBG_H
#define __RK_SDMMC_DBG_H

#include <linux/of_gpio.h>
#include <linux/of_i2c.h>
#include <linux/types.h>
#include <linux/bitops.h>


#define DRIVER_NAME "rk_sdmmc"
#define DRIVER_PREFIX DRIVER_NAME ": "
#define DRIVER_VER  "Dw-mci-rockchip"

enum MMC_DBG_MASK{
     MMC_DBG_NONE = 0,
     MMC_DBG_BOOT = BIT(0),    
     MMC_DBG_ERROR= BIT(1), 
     MMC_DBG_WARN = BIT(2),
     MMC_DBG_INFO = BIT(3),
     MMC_DBG_CMD  = BIT(4),
     MMC_DBG_DBG  = BIT(5),
     MMC_DBG_SW_VOL  = BIT(6),
     MMC_DBG_ALL  = 0xFF,
     
};

extern u32 mmc_debug_level;
extern char dbg_flag[];

#define MMC_DBG_FUNC_CONFIG 1
#if MMC_DBG_FUNC_CONFIG
#define MMC_DBG_BOOT_FUNC(mmc_host,fmt, arg...) \
    do { \
        if(mmc_debug_level & MMC_DBG_BOOT) { \
            if(NULL != strpbrk(dbg_flag,mmc_hostname(mmc_host))) { \
                printk(DRIVER_PREFIX "BOOT " fmt "\n", ##arg);\
            } \
        } \
    }while(0)
#define MMC_DBG_ERR_FUNC(mmc_host,fmt, arg...) \
    do{ \
        if(mmc_debug_level & MMC_DBG_ERROR) { \
            if(strstr(dbg_flag,mmc_hostname(mmc_host))) { \
                printk(DRIVER_PREFIX "ERROR " fmt "\n", ##arg);\
            } \
        } \
    }while(0)
#define MMC_DBG_WARN_FUNC(mmc_host,fmt, arg...) \
    do { \
        if(mmc_debug_level & MMC_DBG_WARN) { \
            if(strstr(dbg_flag,mmc_hostname(mmc_host))) { \
                printk(DRIVER_PREFIX "WARN " fmt "\n", ##arg);\
            } \
        } \
    }while(0)
#define MMC_DBG_INFO_FUNC(mmc_host,fmt, arg...) \
    do { \
        if(mmc_debug_level & MMC_DBG_INFO) { \
            if(strstr(dbg_flag,mmc_hostname(mmc_host))) { \
                printk(DRIVER_PREFIX "INFO " fmt "\n", ##arg);\
            } \
        } \
    }while(0)
#define MMC_DBG_CMD_FUNC(mmc_host,fmt, arg...) \
   do { \
        if(mmc_debug_level & MMC_DBG_CMD) { \
            if(strstr(dbg_flag,mmc_hostname(mmc_host))) { \
                printk(DRIVER_PREFIX "CMD " fmt "\n", ##arg);\
            } \
        } \
    }while(0)
#define MMC_DBG_SW_VOL_FUNC(mmc_host,fmt, arg...) \
   do { \
        if(mmc_debug_level & MMC_DBG_SW_VOL) { \
            if(strstr(dbg_flag,mmc_hostname(mmc_host))) { \
                printk(DRIVER_PREFIX "SW-VOL " fmt "\n", ##arg);\
            } \
        } \
    }while(0)
#else
#define MMC_DBG_BOOT_FUNC(mmc_host,fmt, arg...) {printk(DRIVER_PREFIX "BOOT " fmt "\n", ##arg);}
#define MMC_DBG_ERR_FUNC(mmc_host,fmt, arg...)
#define MMC_DBG_WARN_FUNC(mmc_host,fmt, arg...)
#define MMC_DBG_INFO_FUNC(mmc_host,fmt, arg...)
#define MMC_DBG_CMD_FUNC(mmc_host,fmt, arg...)
#define MMC_DBG_SW_VOL_FUNC(mmc_host,fmt, arg...)
#endif

#if defined(CONFIG_DYNAMIC_DEBUG)
    #define mmc_debug(level, fmt, arg...) \
           do { \
               if (unlikely(level & mmc_debug_level)) \
                   dynamic_pr_debug(DRIVER_PREFIX fmt "\n", ##arg); \
           } while (0)
#else
    #define mmc_debug(level, fmt, arg...) \
           do { \
               if (unlikely(level & mmc_debug_level)) \
                   printk(KERN_DEBUG pr_fmt(DRIVER_PREFIX fmt "\n"), \
                   ##arg); \
           } while (0)
#endif

#endif
