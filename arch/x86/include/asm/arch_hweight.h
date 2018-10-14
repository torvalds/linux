/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HWEIGHT_H
#define _ASM_X86_HWEIGHT_H

#include <asm/cpufeatures.h>

#ifdef CONFIG_64BIT
#define REG_IN "D"
#define REG_OUT "a"
#else
#define REG_IN "a"
#define REG_OUT "a"
#endif

#define __HAVE_ARCH_SW_HWEIGHT

static __always_inline unsigned int __arch_hweight32(unsigned int w)
{
	unsigned int res;

	asm (ALTERNATIVE("call __sw_hweight32", "popcntl %1, %0", X86_FEATURE_POPCNT)
			 : "="REG_OUT (res)
			 : REG_IN (w));

	return res;
}

static inline unsigned int __arch_hweight16(unsigned int w)
{
	return __arch_hweight32(w & 0xffff);
}

static inline unsigned int __arch_hweight8(unsigned int w)
{
	return __arch_hweight32(w & 0xff);
}

#ifdef CONFIG_X86_32
static inline unsigned long __arch_hweight64(__u64 w)
{
	return  __arch_hweight32((u32)w) +
		__arch_hweight32((u32)(w >> 32));
}
#else
static __always_inline unsigned long __arch_hweight64(__u64 w)
{
	unsigned long res;

	asm (ALTERNATIVE("call __sw_hweight64", "popcntq %1, %0", X86_FEATURE_POPCNT)
			 : "="REG_OUT (res)
			 : REG_IN (w));

	return res;
}
#endif /* CONFIG_X86_32 */

#endif
