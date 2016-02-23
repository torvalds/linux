/* timer-regs.h: hardware timer register definitions
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_TIMER_REGS_H
#define _ASM_TIMER_REGS_H

#include <asm/sections.h>

extern unsigned long __nongprelbss __clkin_clock_speed_HZ;
extern unsigned long __nongprelbss __ext_bus_clock_speed_HZ;
extern unsigned long __nongprelbss __res_bus_clock_speed_HZ;
extern unsigned long __nongprelbss __sdram_clock_speed_HZ;
extern unsigned long __nongprelbss __core_bus_clock_speed_HZ;
extern unsigned long __nongprelbss __core_clock_speed_HZ;
extern unsigned long __nongprelbss __dsu_clock_speed_HZ;
extern unsigned long __nongprelbss __serial_clock_speed_HZ;

#define __get_CLKC()	({ *(volatile unsigned long *)(0xfeff9a00); })

static inline void __set_CLKC(unsigned long v)
{
	int tmp;

	asm volatile("	st%I0.p	%2,%M0		\n"
		     "	setlos	%3,%1		\n"
		     "	membar			\n"
		     "0:			\n"
		     "	subicc	%1,#1,%1,icc0	\n"
		     "	bnc	icc0,#1,0b	\n"
		     : "=m"(*(volatile unsigned long *) 0xfeff9a00), "=r"(tmp)
		     : "r"(v), "i"(256)
		     : "icc0");
}

#define __get_TCTR()	({ *(volatile unsigned long *)(0xfeff9418); })
#define __get_TPRV()	({ *(volatile unsigned long *)(0xfeff9420); })
#define __get_TPRCKSL()	({ *(volatile unsigned long *)(0xfeff9428); })
#define __get_TCSR(T)	({ *(volatile unsigned long *)(0xfeff9400 + 8 * (T)); })
#define __get_TxCKSL(T)	({ *(volatile unsigned long *)(0xfeff9430 + 8 * (T)); })

#define __get_TCSR_DATA(T) ({ __get_TCSR(T) >> 24; })

#define __set_TCTR(V)	do { *(volatile unsigned long *)(0xfeff9418) = (V); mb(); } while(0)
#define __set_TPRV(V)	do { *(volatile unsigned long *)(0xfeff9420) = (V) << 24; mb(); } while(0)
#define __set_TPRCKSL(V) do { *(volatile unsigned long *)(0xfeff9428) = (V); mb(); } while(0)
#define __set_TCSR(T,V)	\
do { *(volatile unsigned long *)(0xfeff9400 + 8 * (T)) = (V); mb(); } while(0)

#define __set_TxCKSL(T,V) \
do { *(volatile unsigned long *)(0xfeff9430 + 8 * (T)) = (V); mb(); } while(0)

#define __set_TCSR_DATA(T,V) __set_TCSR(T, (V) << 24)
#define __set_TxCKSL_DATA(T,V) __set_TxCKSL(T, TxCKSL_EIGHT | __TxCKSL_SELECT((V)))

/* clock control register */
#define CLKC_CMODE		0x0f000000
#define CLKC_SLPL		0x000f0000
#define CLKC_P0			0x00000100
#define CLKC_CM			0x00000003

#define CLKC_CMODE_s		24

/* timer control register - non-readback mode */
#define TCTR_MODE_0		0x00000000
#define TCTR_MODE_2		0x04000000
#define TCTR_MODE_4		0x08000000
#define TCTR_MODE_5		0x0a000000
#define TCTR_RL_LATCH		0x00000000
#define TCTR_RL_RW_LOW8		0x10000000
#define TCTR_RL_RW_HIGH8	0x20000000
#define TCTR_RL_RW_LH8		0x30000000
#define TCTR_SC_CTR0		0x00000000
#define TCTR_SC_CTR1		0x40000000
#define TCTR_SC_CTR2		0x80000000

/* timer control register - readback mode */
#define TCTR_CNT0		0x02000000
#define TCTR_CNT1		0x04000000
#define TCTR_CNT2		0x08000000
#define TCTR_NSTATUS		0x10000000
#define TCTR_NCOUNT		0x20000000
#define TCTR_SC_READBACK	0xc0000000

/* timer control status registers - non-readback mode */
#define TCSRx_DATA		0xff000000

/* timer control status registers - readback mode */
#define TCSRx_OUTPUT		0x80000000
#define TCSRx_NULLCOUNT		0x40000000
#define TCSRx_RL		0x30000000
#define TCSRx_MODE		0x07000000

/* timer clock select registers */
#define TxCKSL_SELECT		0x0f000000
#define __TxCKSL_SELECT(X)	((X) << 24)
#define TxCKSL_EIGHT		0xf0000000

#endif /* _ASM_TIMER_REGS_H */
