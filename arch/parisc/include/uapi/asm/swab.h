/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _PARISC_SWAB_H
#define _PARISC_SWAB_H

#include <asm/bitsperlong.h>
#include <linux/types.h>
#include <linux/compiler.h>

#define __SWAB_64_THRU_32__

static inline __attribute_const__ __u16 __arch_swab16(__u16 x)
{
	__asm__("dep %0, 15, 8, %0\n\t"		/* deposit 00ab -> 0bab */
		"shd %%r0, %0, 8, %0"		/* shift 000000ab -> 00ba */
		: "=r" (x)
		: "0" (x));
	return x;
}
#define __arch_swab16 __arch_swab16

static inline __attribute_const__ __u32 __arch_swab24(__u32 x)
{
	__asm__("shd %0, %0, 8, %0\n\t"		/* shift xabcxabc -> cxab */
		"dep %0, 15, 8, %0\n\t"		/* deposit cxab -> cbab */
		"shd %%r0, %0, 8, %0"		/* shift 0000cbab -> 0cba */
		: "=r" (x)
		: "0" (x));
	return x;
}

static inline __attribute_const__ __u32 __arch_swab32(__u32 x)
{
	unsigned int temp;
	__asm__("shd %0, %0, 16, %1\n\t"	/* shift abcdabcd -> cdab */
		"dep %1, 15, 8, %1\n\t"		/* deposit cdab -> cbab */
		"shd %0, %1, 8, %0"		/* shift abcdcbab -> dcba */
		: "=r" (x), "=&r" (temp)
		: "0" (x));
	return x;
}
#define __arch_swab32 __arch_swab32

#if __BITS_PER_LONG > 32
/*
** From "PA-RISC 2.0 Architecture", HP Professional Books.
** See Appendix I page 8 , "Endian Byte Swapping".
**
** Pretty cool algorithm: (* == zero'd bits)
**      PERMH   01234567 -> 67452301 into %0
**      HSHL    67452301 -> 7*5*3*1* into %1
**      HSHR    67452301 -> *6*4*2*0 into %0
**      OR      %0 | %1  -> 76543210 into %0 (all done!)
*/
static inline __attribute_const__ __u64 __arch_swab64(__u64 x)
{
	__u64 temp;
	__asm__("permh,3210 %0, %0\n\t"
		"hshl %0, 8, %1\n\t"
		"hshr,u %0, 8, %0\n\t"
		"or %1, %0, %0"
		: "=r" (x), "=&r" (temp)
		: "0" (x));
	return x;
}
#define __arch_swab64 __arch_swab64
#endif /* __BITS_PER_LONG > 32 */

#endif /* _PARISC_SWAB_H */
