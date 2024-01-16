/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MIPS_ASM_BITREV_H__
#define __MIPS_ASM_BITREV_H__

#include <linux/swab.h>

static __always_inline __attribute_const__ u32 __arch_bitrev32(u32 x)
{
	u32 ret;

	asm("bitswap	%0, %1" : "=r"(ret) : "r"(__swab32(x)));
	return ret;
}

static __always_inline __attribute_const__ u16 __arch_bitrev16(u16 x)
{
	u16 ret;

	asm("bitswap	%0, %1" : "=r"(ret) : "r"(__swab16(x)));
	return ret;
}

static __always_inline __attribute_const__ u8 __arch_bitrev8(u8 x)
{
	u8 ret;

	asm("bitswap	%0, %1" : "=r"(ret) : "r"(x));
	return ret;
}

#endif /* __MIPS_ASM_BITREV_H__ */
