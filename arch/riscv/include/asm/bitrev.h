/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_BITREV_H
#define __ASM_BITREV_H

#include <linux/types.h>
#include <asm/cpufeature-macros.h>
#include <asm/hwcap.h>
#include <asm-generic/bitops/__bitrev.h>

static __always_inline __attribute_const__ u32 __arch_bitrev32(u32 x)
{
	unsigned long result;

	if (!riscv_has_extension_likely(RISCV_ISA_EXT_ZBKB))
		return generic___bitrev32(x);

	asm volatile(
		".option push\n"
		".option arch,+zbkb\n"
		"rev8 %0, %1\n"
		"brev8 %0, %0\n"
		".option pop"
		: "=r" (result) : "r" ((long)x)
	);

	return result >> (__riscv_xlen - 32);
}

static __always_inline __attribute_const__ u16 __arch_bitrev16(u16 x)
{
	return __arch_bitrev32(x) >> 16;
}

static __always_inline __attribute_const__ u8 __arch_bitrev8(u8 x)
{
	unsigned long result;

	if (!riscv_has_extension_likely(RISCV_ISA_EXT_ZBKB))
		return generic___bitrev8(x);

	asm volatile(
		".option push\n"
		".option arch,+zbkb\n"
		"brev8 %0, %1\n"
		".option pop"
		: "=r" (result) : "r" ((long)x)
	);

	return result;
}
#endif
