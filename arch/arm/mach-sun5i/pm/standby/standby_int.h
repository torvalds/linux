/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : standby_int.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-30 19:50
* Descript: intterupt bsp for platform standby.
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __STANDBY_INT_H__
#define __STANDBY_INT_H__

#include "standby_cfg.h"


/* define interrupt source */
enum interrupt_source_e{

    INT_SOURCE_EXTNMI   = 0,
    INT_SOURCE_IR0      = 5,
    INT_SOURCE_IR1      = 6,
    INT_SOURCE_KEYPAD   = 21,
    INT_SOURCE_TIMER0   = 22,
    INT_SOURCE_TIMER1   = 23,
    INT_SOURCE_ALARM    = 24,
    INT_SOURCE_TOUCHPNL = 29,
    INT_SOURCE_LRADC    = 31,
    INT_SOURCE_USB0     = 38,
    INT_SOURCE_USB1     = 39,
    INT_SOURCE_USB2     = 40,
    INT_SOURCE_USB3     = 64,
    INT_SOURCE_USB4     = 65,
};


/* define register for interrupt controller */
struct standby_int_reg_t{

    volatile __u32   Vector;
    volatile __u32   BaseAddr;
    volatile __u32   reserved0;
    volatile __u32   NmiCtrl;

    volatile __u32   IrqPend[3];
    volatile __u32   reserved1;

    volatile __u32   FiqPend[3];
    volatile __u32   reserved2;

    volatile __u32   TypeSel[3];
    volatile __u32   reserved3;

    volatile __u32   IrqEn[3];
    volatile __u32   reserved4;

    volatile __u32   IrqMask[3];
    volatile __u32   reserved5;

    volatile __u32   IrqResp[3];
    volatile __u32   reserved6;

    volatile __u32   IrqForce[3];
    volatile __u32   reserved7;

    volatile __u32   IrqPrio[5];
};


extern __s32 standby_int_init(void);
extern __s32 standby_int_exit(void);
extern __s32 standby_enable_int(enum interrupt_source_e src);
extern __s32 standby_query_int(enum interrupt_source_e src);


#endif  //__STANDBY_INT_H__

