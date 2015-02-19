/*
 * include/asm-microblaze/delay.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Michal Simek
 * Copyright (C) 2007 John Williams
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_DELAY_H
#define _ASM_MICROBLAZE_DELAY_H

#include <linux/param.h>

static inline void __delay(unsigned long loops)
{
	asm volatile ("# __delay		\n\t"		\
			"1: addi	%0, %0, -1\t\n"		\
			"bneid	%0, 1b		\t\n"		\
			"nop			\t\n"
			: "=r" (loops)
			: "0" (loops));
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
 * -- paulus
 */
#define __MAX_UDELAY	(226050910UL/HZ)	/* maximum udelay argument */
#define __MAX_NDELAY	(4294967295UL/HZ)	/* maximum ndelay argument */

extern unsigned long loops_per_jiffy;

static inline void __udelay(unsigned int x)
{

	unsigned long long tmp =
		(unsigned long long)x * (unsigned long long)loops_per_jiffy \
			* 226LL;
	unsigned loops = tmp >> 32;

/*
	__asm__("mulxuu %0,%1,%2" : "=r" (loops) :
		"r" (x), "r" (loops_per_jiffy * 226));
*/
	__delay(loops);
}

extern void __bad_udelay(void);		/* deliberately undefined */
extern void __bad_ndelay(void);		/* deliberately undefined */

#define udelay(n)						\
	({							\
		if (__builtin_constant_p(n)) {			\
			if ((n) / __MAX_UDELAY >= 1)		\
				__bad_udelay();			\
			else					\
				__udelay((n) * (19 * HZ));	\
		} else {					\
			__udelay((n) * (19 * HZ));		\
		}						\
	})

#define ndelay(n)						\
	({							\
		if (__builtin_constant_p(n)) {			\
			if ((n) / __MAX_NDELAY >= 1)		\
				__bad_ndelay();			\
			else					\
				__udelay((n) * HZ);		\
		} else {					\
			__udelay((n) * HZ);			\
		}						\
	})

#define muldiv(a, b, c)		(((a)*(b))/(c))

#endif /* _ASM_MICROBLAZE_DELAY_H */
