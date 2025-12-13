/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_RISCV_SWAB_H
#define _ASM_RISCV_SWAB_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/cpufeature-macros.h>
#include <asm/hwcap.h>
#include <asm-generic/swab.h>

#if defined(CONFIG_TOOLCHAIN_HAS_ZBB) && defined(CONFIG_RISCV_ISA_ZBB) && !defined(NO_ALTERNATIVE)

// Duplicated from include/uapi/linux/swab.h
#define ___constant_swab16(x) ((__u16)(				\
	(((__u16)(x) & (__u16)0x00ffU) << 8) |			\
	(((__u16)(x) & (__u16)0xff00U) >> 8)))

#define ___constant_swab32(x) ((__u32)(				\
	(((__u32)(x) & (__u32)0x000000ffUL) << 24) |		\
	(((__u32)(x) & (__u32)0x0000ff00UL) <<  8) |		\
	(((__u32)(x) & (__u32)0x00ff0000UL) >>  8) |		\
	(((__u32)(x) & (__u32)0xff000000UL) >> 24)))

#define ___constant_swab64(x) ((__u64)(				\
	(((__u64)(x) & (__u64)0x00000000000000ffULL) << 56) |	\
	(((__u64)(x) & (__u64)0x000000000000ff00ULL) << 40) |	\
	(((__u64)(x) & (__u64)0x0000000000ff0000ULL) << 24) |	\
	(((__u64)(x) & (__u64)0x00000000ff000000ULL) <<  8) |	\
	(((__u64)(x) & (__u64)0x000000ff00000000ULL) >>  8) |	\
	(((__u64)(x) & (__u64)0x0000ff0000000000ULL) >> 24) |	\
	(((__u64)(x) & (__u64)0x00ff000000000000ULL) >> 40) |	\
	(((__u64)(x) & (__u64)0xff00000000000000ULL) >> 56)))

#define ARCH_SWAB(size, value)						\
({									\
	unsigned long x = value;					\
									\
	if (riscv_has_extension_likely(RISCV_ISA_EXT_ZBB)) {            \
		asm volatile (".option push\n"				\
			      ".option arch,+zbb\n"			\
			      "rev8 %0, %1\n"				\
			      ".option pop\n"				\
			      : "=r" (x) : "r" (x));			\
		x = x >> (BITS_PER_LONG - size);			\
	} else {                                                        \
		x = ___constant_swab##size(value);                      \
	}								\
	x;								\
})

static __always_inline __u16 __arch_swab16(__u16 value)
{
	return ARCH_SWAB(16, value);
}

static __always_inline __u32 __arch_swab32(__u32 value)
{
	return ARCH_SWAB(32, value);
}

#ifdef CONFIG_64BIT
static __always_inline __u64 __arch_swab64(__u64 value)
{
	return ARCH_SWAB(64, value);
}
#else
static __always_inline __u64 __arch_swab64(__u64 value)
{
	__u32 h = value >> 32;
	__u32 l = value & ((1ULL << 32) - 1);

	return ((__u64)(__arch_swab32(l)) << 32) | ((__u64)(__arch_swab32(h)));
}
#endif

#define __arch_swab64 __arch_swab64
#define __arch_swab32 __arch_swab32
#define __arch_swab16 __arch_swab16

#undef ___constant_swab16
#undef ___constant_swab32
#undef ___constant_swab64

#undef ARCH_SWAB

#endif /* defined(CONFIG_TOOLCHAIN_HAS_ZBB) && defined(CONFIG_RISCV_ISA_ZBB) && !defined(NO_ALTERNATIVE) */
#endif /* _ASM_RISCV_SWAB_H */
