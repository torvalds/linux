/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : super_i.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-30 17:21
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __SUPER_I_H__
#define __SUPER_I_H__

#include "../../pm_config.h"
#include "../../pm_types.h"
#include "../../pm.h"
#include <linux/power/aw_pm.h>
#include <mach/platform.h>

#include "super_cfg.h"
#include "common.h"
#include "super_clock.h"
#include "super_power.h"
#include "super_twi.h"
#include "super_tmr.h"

extern struct aw_pm_info  pm_info;

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#endif  //__SUPER_I_H__

