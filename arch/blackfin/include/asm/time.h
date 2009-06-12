/*
 * asm-blackfin/time.h:
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _ASM_BLACKFIN_TIME_H
#define _ASM_BLACKFIN_TIME_H

/*
 * The way that the Blackfin core timer works is:
 *  - CCLK is divided by a programmable 8-bit pre-scaler (TSCALE)
 *  - Every time TSCALE ticks, a 32bit is counted down (TCOUNT)
 *
 * If you take the fastest clock (1ns, or 1GHz to make the math work easier)
 *    10ms is 10,000,000 clock ticks, which fits easy into a 32-bit counter
 *    (32 bit counter is 4,294,967,296ns or 4.2 seconds) so, we don't need
 *    to use TSCALE, and program it to zero (which is pass CCLK through).
 *    If you feel like using it, try to keep HZ * TIMESCALE to some
 *    value that divides easy (like power of 2).
 */

#ifndef CONFIG_CPU_FREQ
#define TIME_SCALE 1
#define __bfin_cycles_off (0)
#define __bfin_cycles_mod (0)
#else
/*
 * Blackfin CPU frequency scaling supports max Core Clock 1, 1/2 and 1/4 .
 * Whenever we change the Core Clock frequency changes we immediately
 * adjust the Core Timer Presale Register. This way we don't lose time.
 */
#define TIME_SCALE 4
extern unsigned long long __bfin_cycles_off;
extern unsigned int __bfin_cycles_mod;
#endif

extern void __init setup_core_timer(void);
#endif
