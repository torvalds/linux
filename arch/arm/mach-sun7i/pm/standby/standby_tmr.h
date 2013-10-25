/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : standby_tmr.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 15:23
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __STANDBY_TMR_H__
#define __STANDBY_TMR_H__

#include "standby_cfg.h"

enum tmr_event_type_e{
    TMR_EVENT_POWEROFF,
    TMR_EVENT_ALARM,
};


__s32 standby_tmr_init(void);
__s32 standby_tmr_exit(void);
__s32 standby_tmr_query(enum tmr_event_type_e type);
void standby_tmr_mdlay(int ms);
void standby_tmr_enable_watchdog(void);
void standby_tmr_disable_watchdog(void);

#endif  //__STANDBY_TMR_H__

