/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : super_tmr.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 15:23
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __SUPER_TMR_H__
#define __SUPER_TMR_H__

#include "super_cfg.h"


//define timer controller registers




enum tmr_event_type_e{
    TMR_EVENT_POWEROFF,
    TMR_EVENT_ALARM,
};

void mem_tmr_init(void);
void mem_tmr_disable_watchdog(void);

#endif  //__SUPER_TMR_H__

