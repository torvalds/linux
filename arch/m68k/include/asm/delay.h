/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_DELAY_H
#define _M68K_DELAY_H

#include <asm/param.h>

/*
 * Copyright (C) 1994 Hamish Macdonald
 * Copyright (C) 2004 Greg Ungerer <gerg@uclinux.com>
 *
 * Delay routines, using a pre-computed "loops_per_jiffy" value.
 */

#if defined(CONFIG_COLDFIRE)
/*
 * The ColdFire runs the delay loop at significantly different speeds
 * depending upon long word alignment or not.  We'll pad it to
 * long word alignment which is the faster version.
 * The 0x4a8e is of course a 'tstl %fp' instruction.  This is better
 * than using a NOP (0x4e71) instruction because it executes in one
 * cycle not three and doesn't allow for an arbitrary delay waiting
 * for bus cycles to finish.  Also fp/a6 isn't likely to cause a
 * stall waiting for the register to become valid if such is added
 * to the coldfire at some stage.
 */
#define	DELAY_ALIGN	".balignw 4, 0x4a8e\n\t"
#else
/*
 * No instruction alignment required for other m68k types.
 */
#define	DELAY_ALIGN
#endif

static inline void __delay(unsigned long loops)
{
	__asm__ __volatile__ (
		DELAY_ALIGN
		"1: subql #1,%0\n\t"
		"jcc 1b"
		: "=d" (loops)
		: "0" (loops));
}

extern void __bad_udelay(void);


#ifdef CONFIG_CPU_HAS_NO_MULDIV64
/*
 * The simpler m68k and ColdFire processors do not have a 32*32->64
 * multiply instruction. So we need to handle them a little differently.
 * We use a bit of shifting and a single 32*32->32 multiply to get close.
 */
#define	HZSCALE		(268435456 / (1000000 / HZ))

#define	__const_udelay(u) \
	__delay(((((u) * HZSCALE) >> 11) * (loops_per_jiffy >> 11)) >> 6)

#else

static inline void __xdelay(unsigned long xloops)
{
	unsigned long tmp;

	__asm__ ("mulul %2,%0:%1"
		: "=d" (xloops), "=d" (tmp)
		: "d" (xloops), "1" (loops_per_jiffy));
	__delay(xloops * HZ);
}

/*
 * The definition of __const_udelay is specifically made a macro so that
 * the const factor (4295 = 2**32 / 1000000) can be optimized out when
 * the delay is a const.
 */
#define	__const_udelay(n)	(__xdelay((n) * 4295))

#endif

static inline void __udelay(unsigned long usecs)
{
	__const_udelay(usecs);
}

/*
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 */
#define udelay(n) (__builtin_constant_p(n) ? \
	((n) > 20000 ? __bad_udelay() : __const_udelay(n)) : __udelay(n))

/*
 * nanosecond delay:
 *
 * ((((HZSCALE) >> 11) * (loops_per_jiffy >> 11)) >> 6) is the number of loops
 * per microsecond
 *
 * 1000 / ((((HZSCALE) >> 11) * (loops_per_jiffy >> 11)) >> 6) is the number of
 * nanoseconds per loop
 *
 * So n / ( 1000 / ((((HZSCALE) >> 11) * (loops_per_jiffy >> 11)) >> 6) ) would
 * be the number of loops for n nanoseconds
 */

/*
 * The simpler m68k and ColdFire processors do not have a 32*32->64
 * multiply instruction. So we need to handle them a little differently.
 * We use a bit of shifting and a single 32*32->32 multiply to get close.
 * This is a macro so that the const version can factor out the first
 * multiply and shift.
 */
#define	HZSCALE		(268435456 / (1000000 / HZ))

static inline void ndelay(unsigned long nsec)
{
	__delay(DIV_ROUND_UP(nsec *
			     ((((HZSCALE) >> 11) *
			       (loops_per_jiffy >> 11)) >> 6),
			     1000));
}
#define ndelay(n) ndelay(n)

#endif /* defined(_M68K_DELAY_H) */
