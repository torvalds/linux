/*
 * Copyright (C) 1996 Paul Mackerras.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#define GETREG(reg)		\
    static inline unsigned long get_ ## reg (void)	\
	{ unsigned long ret; asm volatile ("mf" #reg " %0" : "=r" (ret) :); return ret; }

#define SETREG(reg)		\
    static inline void set_ ## reg (unsigned long val)	\
	{ asm volatile ("mt" #reg " %0" : : "r" (val)); }

GETREG(msr)
SETREG(msrd)
GETREG(cr)

#define GSETSPR(n, name)	\
    static inline long get_ ## name (void) \
	{ long ret; asm volatile ("mfspr %0," #n : "=r" (ret) : ); return ret; } \
    static inline void set_ ## name (long val) \
	{ asm volatile ("mtspr " #n ",%0" : : "r" (val)); }

GSETSPR(0, mq)
GSETSPR(1, xer)
GSETSPR(4, rtcu)
GSETSPR(5, rtcl)
GSETSPR(8, lr)
GSETSPR(9, ctr)
GSETSPR(18, dsisr)
GSETSPR(19, dar)
GSETSPR(22, dec)
GSETSPR(25, sdr1)
GSETSPR(26, srr0)
GSETSPR(27, srr1)
GSETSPR(272, sprg0)
GSETSPR(273, sprg1)
GSETSPR(274, sprg2)
GSETSPR(275, sprg3)
GSETSPR(282, ear)
GSETSPR(287, pvr)
GSETSPR(1008, hid0)
GSETSPR(1009, hid1)
GSETSPR(1010, iabr)
GSETSPR(1023, pir)

static inline void store_inst(void *p)
{
	asm volatile ("dcbst 0,%0; sync; icbi 0,%0; isync" : : "r" (p));
}

static inline void cflush(void *p)
{
	asm volatile ("dcbf 0,%0; icbi 0,%0" : : "r" (p));
}

static inline void cinval(void *p)
{
	asm volatile ("dcbi 0,%0; icbi 0,%0" : : "r" (p));
}
