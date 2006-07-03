/*
 * Copyright (C) 1996 Paul Mackerras.
 */

#define GETREG(reg)		\
    static inline int get_ ## reg (void)	\
	{ int ret; asm volatile ("mf" #reg " %0" : "=r" (ret) :); return ret; }

#define SETREG(reg)		\
    static inline void set_ ## reg (int val)	\
	{ asm volatile ("mt" #reg " %0" : : "r" (val)); }

GETREG(msr)
SETREG(msr)
GETREG(cr)

#define GSETSPR(n, name)	\
    static inline int get_ ## name (void) \
	{ int ret; asm volatile ("mfspr %0," #n : "=r" (ret) : ); return ret; } \
    static inline void set_ ## name (int val) \
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
#ifndef CONFIG_8xx
GSETSPR(528, bat0u)
GSETSPR(529, bat0l)
GSETSPR(530, bat1u)
GSETSPR(531, bat1l)
GSETSPR(532, bat2u)
GSETSPR(533, bat2l)
GSETSPR(534, bat3u)
GSETSPR(535, bat3l)
GSETSPR(1008, hid0)
GSETSPR(1009, hid1)
GSETSPR(1010, iabr)
GSETSPR(1013, dabr)
GSETSPR(1023, pir)
#else
GSETSPR(144, cmpa)
GSETSPR(145, cmpb)
GSETSPR(146, cmpc)
GSETSPR(147, cmpd)
GSETSPR(158, ictrl)
#endif

static inline int get_sr(int n)
{
    int ret;

    asm (" mfsrin %0,%1" : "=r" (ret) : "r" (n << 28));
    return ret;
}

static inline void set_sr(int n, int val)
{
    asm ("mtsrin %0,%1" : : "r" (val), "r" (n << 28));
}

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

