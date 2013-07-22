/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
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
#ifndef __MEM_INT_H__
#define __MEM_INT_H__

#include "pm_config.h"
#include <mach/irqs.h>
#define GIC_400_ENABLE_LEN (0x40) //unit is byte. so in 32bit unit, the reg offset is 0-0x3c

enum interrupt_source_e{
    INT_SOURCE_EXTNMI   = SW_INT_IRQNO_NMI,
    INT_SOURCE_IR0      = SW_INT_IRQNO_IR0,
    INT_SOURCE_IR1      = SW_INT_IRQNO_IR1,
    INT_SOURCE_KEYPAD   = SW_INT_IRQNO_KEYPAD,
    INT_SOURCE_TIMER0   = SW_INT_IRQNO_TIMER0,
    INT_SOURCE_TIMER1   = SW_INT_IRQNO_TIMER1,
    INT_SOURCE_ALARM    = SW_INT_IRQNO_TIMER2,
    INT_SOURCE_TOUCHPNL = SW_INT_IRQNO_TOUCH_PANEL,
    INT_SOURCE_LRADC    = SW_INT_IRQNO_LRADC,
    INT_SOURCE_USB0     = SW_INT_IRQNO_USB0,
    INT_SOURCE_USB1     = SW_INT_IRQNO_USB1,
    INT_SOURCE_USB2     = SW_INT_IRQNO_USB2,
    INT_SOURCE_USB3     = SW_INT_IRQNO_USB3,
    INT_SOURCE_USB4     = SW_INT_IRQNO_USB4,
    INT_SOURCE_GPIO     = SW_INT_IRQNO_PIO,
};

#define GIC_CPU_CTRL			0x00
#define GIC_CPU_PRIMASK			0x04
#define GIC_CPU_BINPOINT		0x08
#define GIC_CPU_INTACK			0x0c
#define GIC_CPU_EOI			0x10
#define GIC_CPU_RUNNINGPRI		0x14
#define GIC_CPU_HIGHPRI			0x18

#define GIC_DIST_CTRL			0x000
#define GIC_DIST_CTR			0x004
#define GIC_DIST_ENABLE_SET		0x100
#define GIC_DIST_ENABLE_CLEAR		0x180
#define GIC_DIST_PENDING_SET		0x200
#define GIC_DIST_PENDING_CLEAR		0x280
#define GIC_DIST_ACTIVE_BIT		0x300
#define GIC_DIST_PRI			0x400
#define GIC_DIST_TARGET			0x800
#define GIC_DIST_CONFIG			0xc00
#define GIC_DIST_SOFTINT		0xf00
/*
*/

extern __s32 mem_int_init(void);
extern __s32 mem_int_exit(void);
extern __s32 mem_enable_int(enum interrupt_source_e src);
extern __s32 mem_query_int(enum interrupt_source_e src);


#endif  //__MEM_INT_H__

