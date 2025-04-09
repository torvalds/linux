/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Based on arch/x86/include/asm/arch_hweight.h
 */

#ifndef _ASM_RISCV_HWEIGHT_H
#define _ASM_RISCV_HWEIGHT_H

#include <asm/alternative-macros.h>
#include <asm/hwcap.h>

#if (BITS_PER_LONG == 64)
#define CPOPW	"cpopw "
#elif (BITS_PER_LONG == 32)
#define CPOPW	"cpop "
#else
#error "Unexpected BITS_PER_LONG"
#endif

static __always_inline unsigned int __arch_hweight32(unsigned int w)
{
#if defined(CONFIG_RISCV_ISA_ZBB) && defined(CONFIG_TOOLCHAIN_HAS_ZBB)
	asm goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
				      RISCV_ISA_EXT_ZBB, 1)
			  : : : : legacy);

	asm (".option push\n"
	     ".option arch,+zbb\n"
	     CPOPW "%0, %1\n"
	     ".option pop\n"
	     : "=r" (w) : "r" (w) :);

	return w;

legacy:
#endif
	return __sw_hweight32(w);
}

static inline unsigned int __arch_hweight16(unsigned int w)
{
	return __arch_hweight32(w & 0xffff);
}

static inline unsigned int __arch_hweight8(unsigned int w)
{
	return __arch_hweight32(w & 0xff);
}

#if BITS_PER_LONG == 64
static __always_inline unsigned long __arch_hweight64(__u64 w)
{
#if defined(CONFIG_RISCV_ISA_ZBB) && defined(CONFIG_TOOLCHAIN_HAS_ZBB)
	asm goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
				      RISCV_ISA_EXT_ZBB, 1)
			  : : : : legacy);

	asm (".option push\n"
	     ".option arch,+zbb\n"
	     "cpop %0, %1\n"
	     ".option pop\n"
	     : "=r" (w) : "r" (w) :);

	return w;

legacy:
#endif
	return __sw_hweight64(w);
}
#else /* BITS_PER_LONG == 64 */
static inline unsigned long __arch_hweight64(__u64 w)
{
	return  __arch_hweight32((u32)w) +
		__arch_hweight32((u32)(w >> 32));
}
#endif /* !(BITS_PER_LONG == 64) */

#endif /* _ASM_RISCV_HWEIGHT_H */
