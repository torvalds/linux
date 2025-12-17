/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Supervisor Mode Access Prevention support
 *
 * Copyright (C) 2012 Intel Corporation
 * Author: H. Peter Anvin <hpa@linux.intel.com>
 */

#ifndef _ASM_X86_SMAP_H
#define _ASM_X86_SMAP_H

#include <asm/nops.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>

#ifdef __ASSEMBLER__

#define ASM_CLAC \
	ALTERNATIVE "", "clac", X86_FEATURE_SMAP

#define ASM_STAC \
	ALTERNATIVE "", "stac", X86_FEATURE_SMAP

#else /* __ASSEMBLER__ */

/*
 * The CLAC/STAC instructions toggle the enforcement of
 * X86_FEATURE_SMAP along with X86_FEATURE_LASS.
 *
 * SMAP enforcement is based on the _PAGE_BIT_USER bit in the page
 * tables. The kernel is not allowed to touch pages with that bit set
 * unless the AC bit is set.
 *
 * Use stac()/clac() when accessing userspace (_PAGE_USER) mappings,
 * regardless of location.
 *
 * Note: a barrier is implicit in alternative().
 */

static __always_inline void clac(void)
{
	alternative("", "clac", X86_FEATURE_SMAP);
}

static __always_inline void stac(void)
{
	alternative("", "stac", X86_FEATURE_SMAP);
}

/*
 * LASS enforcement is based on bit 63 of the virtual address. The
 * kernel is not allowed to touch memory in the lower half of the
 * virtual address space.
 *
 * Use lass_stac()/lass_clac() to toggle the AC bit for kernel data
 * accesses (!_PAGE_USER) that are blocked by LASS, but not by SMAP.
 *
 * Even with the AC bit set, LASS will continue to block instruction
 * fetches from the user half of the address space. To allow those,
 * clear CR4.LASS to disable the LASS mechanism entirely.
 *
 * Note: a barrier is implicit in alternative().
 */

static __always_inline void lass_clac(void)
{
	alternative("", "clac", X86_FEATURE_LASS);
}

static __always_inline void lass_stac(void)
{
	alternative("", "stac", X86_FEATURE_LASS);
}

static __always_inline unsigned long smap_save(void)
{
	unsigned long flags;

	asm volatile ("# smap_save\n\t"
		      ALTERNATIVE(ANNOTATE_IGNORE_ALTERNATIVE "\n\t"
				  "", "pushf; pop %0; clac",
				  X86_FEATURE_SMAP)
		      : "=rm" (flags) : : "memory", "cc");

	return flags;
}

static __always_inline void smap_restore(unsigned long flags)
{
	asm volatile ("# smap_restore\n\t"
		      ALTERNATIVE(ANNOTATE_IGNORE_ALTERNATIVE "\n\t"
				  "", "push %0; popf",
				  X86_FEATURE_SMAP)
		      : : "g" (flags) : "memory", "cc");
}

/* These macros can be used in asm() statements */
#define ASM_CLAC \
	ALTERNATIVE("", "clac", X86_FEATURE_SMAP)
#define ASM_STAC \
	ALTERNATIVE("", "stac", X86_FEATURE_SMAP)

#define ASM_CLAC_UNSAFE \
	ALTERNATIVE("", ANNOTATE_IGNORE_ALTERNATIVE "\n\t" "clac", X86_FEATURE_SMAP)
#define ASM_STAC_UNSAFE \
	ALTERNATIVE("", ANNOTATE_IGNORE_ALTERNATIVE "\n\t" "stac", X86_FEATURE_SMAP)

#endif /* __ASSEMBLER__ */

#endif /* _ASM_X86_SMAP_H */
