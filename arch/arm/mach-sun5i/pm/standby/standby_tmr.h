/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
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


//define timer controller registers
typedef struct __STANDBY_TMR_REG
{
    // offset:0x00
    volatile __u32   IntCtl;
    volatile __u32   IntSta;
    volatile __u32   reserved0[2];
    // offset:0x10
    volatile __u32   Tmr0Ctl;
    volatile __u32   Tmr0IntVal;
    volatile __u32   Tmr0CntVal;
    volatile __u32   reserved1;
    // offset:0x20
    volatile __u32   Tmr1Ctl;
    volatile __u32   Tmr1IntVal;
    volatile __u32   Tmr1CntVal;
    volatile __u32   reserved2;
    // offset:0x30
    volatile __u32   Tmr2Ctl;
    volatile __u32   Tmr2IntVal;
    volatile __u32   Tmr2CntVal;
    volatile __u32   reserved3;
    // offset:0x40
    volatile __u32   Tmr3Ctl;
    volatile __u32   Tmr3IntVal;
    volatile __u32   reserved4[2];
    // offset:0x50
    volatile __u32   Tmr4Ctl;
    volatile __u32   Tmr4IntVal;
    volatile __u32   Tmr4CntVal;
    volatile __u32   reserved5;
    // offset:0x60
    volatile __u32   Tmr5Ctl;
    volatile __u32   Tmr5IntVal;
    volatile __u32   Tmr5CntVal;
    volatile __u32   reserved6[5];
    // offset:0x80
    volatile __u32   AvsCtl;
    volatile __u32   Avs0Cnt;
    volatile __u32   Avs1Cnt;
    volatile __u32   AvsDiv;
    // offset:0x90
    volatile __u32   DogCtl;
    volatile __u32   DogMode;
    volatile __u32   reserved7[2];
    // offset:0xa0
    volatile __u32   Cnt64Ctl;
    volatile __u32   Cnt64Lo;
    volatile __u32   Cnt64Hi;
    volatile __u32   reserved8[21];
    // offset:0x100
    volatile __u32   LoscCtl;
    volatile __u32   RtcYMD;
    volatile __u32   RtcHMS;
    volatile __u32   RtcDHMS;
    // offset:0x110
    volatile __u32   AlarmWHMS;
    volatile __u32   AlarmEn;
    volatile __u32   AlarmIrqEn;
    volatile __u32   AlarmIrqSta;
    // offset:0x120
    volatile __u32   TmrGpReg[4];

} __standby_tmr_reg_t;


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

