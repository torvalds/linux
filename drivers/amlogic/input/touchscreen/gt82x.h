/*
 * 
 * Copyright (C) 2011 Goodix, Inc.
 * 
 * Author: Scott
 * Date: 2012.01.05
 */

#ifndef _LINUX_GOODIX_TOUCH_H
#define _LINUX_GOODIX_TOUCH_H

#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <mach/gpio.h>
#include <mach/gpio_data.h>
#include <linux/ctp.h>

#define fail    0
#define success 1

#define false   0
#define true    1


#if 0
#define DEBUG(fmt, arg...) printk("<--GT-DEBUG-->"fmt, ##arg)
#else
#define DEBUG(fmt, arg...)
#endif

#if 0
#define NOTICE(fmt, arg...) printk("<--GT-NOTICE-->"fmt, ##arg)
#else
#define NOTICE(fmt, arg...)
#endif

#if 0
#define WARNING(fmt, arg...) printk("<--GT-WARNING-->"fmt, ##arg)
#else
#define WARNING(fmt, arg...)
#endif

#if 1
#define DEBUG_MSG(fmt, arg...) printk("<--GT msg-->"fmt, ##arg)
#else
#define DEBUG_MSG(fmt, arg...)
#endif

#if 0
#define DEBUG_UPDATE(fmt, arg...) printk("<--GT update-->"fmt, ##arg)
#else
#define DEBUG_UPDATE(fmt, arg...)
#endif 

#if 0
#define DEBUG_COOR(fmt, arg...) printk(fmt, ##arg)
#define DEBUG_COORD
#else
#define DEBUG_COOR(fmt, arg...)
#endif

#if 0
#define DEBUG_ARRAY(array, num)   do{\
                                   int i;\
                                   u8* a = array;\
                                   for (i = 0; i < (num); i++)\
                                   {\
                                       printk("%02x   ", (a)[i]);\
                                       if ((i + 1 ) %10 == 0)\
                                       {\
                                           printk("\n");\
                                       }\
                                   }\
                                   printk("\n");\
                                  }while(0)
#else
#define DEBUG_ARRAY(array, num) 
#endif 

#if 0
#define DEBUG_REPORT(fmt, arg...) printk(fmt, ##arg)
#else
#define DEBUG_REPORT(fmt, arg...)
#endif

#define ADDR_MAX_LENGTH     2
#define ADDR_LENGTH         ADDR_MAX_LENGTH

#define CREATE_WR_NODE
//#define AUTO_UPDATE_GUITAR             //如果定义了则上电会自动判断是否需要升级

      

#define GOODIX_MULTI_TOUCH
#ifdef GOODIX_MULTI_TOUCH
    #define MAX_FINGER_NUM    5
#else
    #define MAX_FINGER_NUM    1
#endif


struct goodix_ts_data {
    u8 irq_is_disable;
    u16 addr;
    u32 version;
    struct i2c_client *client;
    struct input_dev *input_dev;
    struct work_struct work;
    struct early_suspend early_suspend;
    s8 phys[32];
    struct ctp_platform_data *pdata;
    u8 last_key;
};

static inline void guitar_reset(struct goodix_ts_data *ts, s32 ms)
{
    if (ts->pdata->gpio_reset) {
        gpio_set_status(ts->pdata->gpio_reset, gpio_status_out);
        gpio_out(ts->pdata->gpio_reset, 0);
        msleep(ms);
        gpio_out(ts->pdata->gpio_reset, 1);
        msleep(50);
        printk("goodix reset\n");
    }
}

static inline void goodix_ts_power(struct goodix_ts_data * ts, s32 on)
{
    if (ts->pdata->gpio_power) {
        gpio_set_status(ts->pdata->gpio_power, gpio_status_out);
        gpio_out(ts->pdata->gpio_power, !!on);
        printk("goodix power %s\n", on ? "on":"off");
    }
}

static inline void guitar_enter_update_mode(struct goodix_ts_data * ts)
{
    if (ts->pdata->gpio_interrupt) {
        gpio_set_status(ts->pdata->gpio_interrupt, gpio_status_out);
        gpio_out(ts->pdata->gpio_interrupt, 0);
    }
}

static inline void guitar_leave_update_mode(struct goodix_ts_data * ts)
{
    if (ts->pdata->gpio_interrupt) {
        gpio_set_status(ts->pdata->gpio_interrupt, gpio_status_in);
    }
}

//*************************TouchScreen Work Part End****************************

#endif /* _LINUX_GOODIX_TOUCH_H */
