/*
 * arch/arm/plat-sunxi/pm/standby/standby_tmr.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
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

