/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : standby_i.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-30 17:21
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __STANDBY_I_H__
#define __STANDBY_I_H__

#include <linux/power/aw_pm.h>
#include <mach/platform.h>

#include "standby_cfg.h"
#include "common.h"
#include "standby_clock.h"
#include "standby_key.h"
#include "standby_power.h"
#include "standby_usb.h"
#include "standby_twi.h"
#include "standby_ir.h"
#include "standby_tmr.h"
#include "standby_int.h"

extern struct aw_pm_info  pm_info;


static inline __u32 raw_lib_udiv(__u32 dividend, __u32 divisior)
{
    __u32   tmpDiv = (__u32)divisior;
    __u32   tmpQuot = 0;
    __s32   shift = 0;

    if(!divisior)
    {
        /* divide 0 error abort */
        return 0;
    }

    while(!(tmpDiv & ((__u32)1<<31)))
    {
        tmpDiv <<= 1;
        shift ++;
    }

    do
    {
        if(dividend >= tmpDiv)
        {
            dividend -= tmpDiv;
            tmpQuot = (tmpQuot << 1) | 1;
        }
        else
        {
            tmpQuot = (tmpQuot << 1) | 0;
        }
        tmpDiv >>= 1;
        shift --;
    } while(shift >= 0);

    return tmpQuot;
}



#endif  //__STANDBY_I_H__

