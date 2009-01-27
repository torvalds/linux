#ifndef _M68K_DELAY_H
#define _M68K_DELAY_H

#include <asm/param.h>

/*
 * Copyright (C) 1994 Hamish Macdonald
 *
 * Delay routines, using a pre-computed "loops_per_jiffy" value.
 */

static inline void __delay(unsigned long loops)
{
	__asm__ __volatile__ ("1: subql #1,%0; jcc 1b"
		: "=d" (loops) : "0" (loops));
}

extern void __bad_udelay(void);

/*
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 */
static inline void __const_udelay(unsigned long xloops)
{
	unsigned long tmp;

	__asm__ ("mulul %2,%0:%1"
		: "=d" (xloops), "=d" (tmp)
		: "d" (xloops), "1" (loops_per_jiffy));
	__delay(xloops * HZ);
}

static inline void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 4295);	/* 2**32 / 1000000 */
}

#define udelay(n) (__builtin_constant_p(n) ? \
	((n) > 20000 ? __bad_udelay() : __const_udelay((n) * 4295)) : \
	__udelay(n))

static inline unsigned long muldiv(unsigned long a, unsigned long b,
				   unsigned long c)
{
	unsigned long tmp;

	__asm__ ("mulul %2,%0:%1; divul %3,%0:%1"
		: "=d" (tmp), "=d" (a)
		: "d" (b), "d" (c), "1" (a));
	return a;
}

#endif /* defined(_M68K_DELAY_H) */
