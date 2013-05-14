/*
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_DELAY_H
#define _ASM_NIOS2_DELAY_H

#include <asm/param.h>

static inline void __delay(unsigned long loops)
{
	int dummy;

	__asm__ __volatile__(
	"1:\n\t"
	"    beq    %0,zero,2f\n\t"
	"    addi   %0, %0, -1\n\t"
	"    br     1b\n\t"
	"2:\n\t"
	:  "=r" (dummy)		/* Need output for optimizer */
	:  "0" (loops));	/* %0  Input */
}

/*
 * Note that 19 * 226 == 4294 ==~ 2^32 / 10^6, so
 * loops = (4294 * usecs * loops_per_jiffy * HZ) / 2^32.
 *
 * The mul instruction gives us loops = (a * b) / 2^32.
 * We choose a = usecs * 19 * HZ and b = loops_per_jiffy * 226
 * because this lets us support a wide range of HZ and
 * loops_per_jiffy values without either a or b overflowing 2^32.
 * Thus we need usecs * HZ <= (2^32 - 1) / 19 = 226050910 and
 * loops_per_jiffy <= (2^32 - 1) / 226 = 19004280
 * (which corresponds to ~3800 bogomips at HZ = 100).
 *  -- paulus
 */
#define __MAX_UDELAY	(226050910UL/HZ)	/* maximum udelay argument */
#define __MAX_NDELAY	(4294967295UL/HZ)	/* maximum ndelay argument */

extern unsigned long loops_per_jiffy;

static inline void __udelay(unsigned int x)
{
	unsigned int loops;

	/*
	 * Note, if this is compiled with -mhw-mulx it will produce a "mulxuu"
	 * (at least in toolchain 145) so there is no need for inline
	 * assembly here anymore, which might in turn be emulated if unsupported
	 * by the design.
	 */
	loops = (unsigned int)((((unsigned long long)(x) *
			(unsigned long long)(loops_per_jiffy * 226))) >> 32);

/*
	__asm__("mulxuu %0,%1,%2" : "=r" (loops) :
		"r" (x), "r" (loops_per_jiffy * 226));
*/
	__delay(loops);
}

static inline void __ndelay(unsigned int x)
{
	unsigned int loops;

	/* see comment in __udelay */
	loops = (unsigned int)((((unsigned long long)(x) *
			(unsigned long long)(loops_per_jiffy * 5))) >> 32);

/*
	__asm__("mulxuu %0,%1,%2" : "=r" (loops) :
		"r" (x), "r" (loops_per_jiffy * 5));
*/
	__delay(loops);
}

extern void __bad_udelay(void);		/* deliberately undefined */
extern void __bad_ndelay(void);		/* deliberately undefined */

#define udelay(n) (__builtin_constant_p(n) ? \
	((n) > __MAX_UDELAY ? __bad_udelay() : __udelay((n) * (19 * HZ))) : \
	__udelay((n) * (19 * HZ)))

#define ndelay(n) (__builtin_constant_p(n) ? \
	((n) > __MAX_NDELAY ? __bad_ndelay() : __ndelay((n) * HZ)) : \
	__ndelay((n) * HZ))

#endif /* _ASM_NIOS2_DELAY_H */
