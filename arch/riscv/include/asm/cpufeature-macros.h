/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022-2024 Rivos, Inc
 */

#ifndef _ASM_CPUFEATURE_MACROS_H
#define _ASM_CPUFEATURE_MACROS_H

#include <asm/hwcap.h>
#include <asm/alternative-macros.h>

#define STANDARD_EXT		0

bool __riscv_isa_extension_available(const unsigned long *isa_bitmap, unsigned int bit);
#define riscv_isa_extension_available(isa_bitmap, ext)	\
	__riscv_isa_extension_available(isa_bitmap, RISCV_ISA_EXT_##ext)

static __always_inline bool __riscv_has_extension_likely(const unsigned long vendor,
							 const unsigned long ext)
{
	asm goto(ALTERNATIVE("j	%l[l_no]", "nop", %[vendor], %[ext], 1)
	:
	: [vendor] "i" (vendor), [ext] "i" (ext)
	:
	: l_no);

	return true;
l_no:
	return false;
}

static __always_inline bool __riscv_has_extension_unlikely(const unsigned long vendor,
							   const unsigned long ext)
{
	asm goto(ALTERNATIVE("nop", "j	%l[l_yes]", %[vendor], %[ext], 1)
	:
	: [vendor] "i" (vendor), [ext] "i" (ext)
	:
	: l_yes);

	return false;
l_yes:
	return true;
}

static __always_inline bool riscv_has_extension_unlikely(const unsigned long ext)
{
	compiletime_assert(ext < RISCV_ISA_EXT_MAX, "ext must be < RISCV_ISA_EXT_MAX");

	if (IS_ENABLED(CONFIG_RISCV_ALTERNATIVE))
		return __riscv_has_extension_unlikely(STANDARD_EXT, ext);

	return __riscv_isa_extension_available(NULL, ext);
}

static __always_inline bool riscv_has_extension_likely(const unsigned long ext)
{
	compiletime_assert(ext < RISCV_ISA_EXT_MAX, "ext must be < RISCV_ISA_EXT_MAX");

	if (IS_ENABLED(CONFIG_RISCV_ALTERNATIVE))
		return __riscv_has_extension_likely(STANDARD_EXT, ext);

	return __riscv_isa_extension_available(NULL, ext);
}

#endif /* _ASM_CPUFEATURE_MACROS_H */
