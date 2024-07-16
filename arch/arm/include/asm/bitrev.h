/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_BITREV_H
#define __ASM_BITREV_H

static __always_inline __attribute_const__ u32 __arch_bitrev32(u32 x)
{
	__asm__ ("rbit %0, %1" : "=r" (x) : "r" (x));
	return x;
}

static __always_inline __attribute_const__ u16 __arch_bitrev16(u16 x)
{
	return __arch_bitrev32((u32)x) >> 16;
}

static __always_inline __attribute_const__ u8 __arch_bitrev8(u8 x)
{
	return __arch_bitrev32((u32)x) >> 24;
}

#endif
