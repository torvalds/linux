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
	ALTERNATIVE __stringify(ANNOTATE_IGNORE_ALTERNATIVE), "clac", X86_FEATURE_SMAP

#define ASM_STAC \
	ALTERNATIVE __stringify(ANNOTATE_IGNORE_ALTERNATIVE), "stac", X86_FEATURE_SMAP

#else /* __ASSEMBLER__ */

static __always_inline void clac(void)
{
	/* Note: a barrier is implicit in alternative() */
	alternative(ANNOTATE_IGNORE_ALTERNATIVE "", "clac", X86_FEATURE_SMAP);
}

static __always_inline void stac(void)
{
	/* Note: a barrier is implicit in alternative() */
	alternative(ANNOTATE_IGNORE_ALTERNATIVE "", "stac", X86_FEATURE_SMAP);
}

static __always_inline unsigned long smap_save(void)
{
	unsigned long flags;

	asm volatile ("# smap_save\n\t"
		      ALTERNATIVE(ANNOTATE_IGNORE_ALTERNATIVE
				  "", "pushf; pop %0; clac",
				  X86_FEATURE_SMAP)
		      : "=rm" (flags) : : "memory", "cc");

	return flags;
}

static __always_inline void smap_restore(unsigned long flags)
{
	asm volatile ("# smap_restore\n\t"
		      ALTERNATIVE(ANNOTATE_IGNORE_ALTERNATIVE
				  "", "push %0; popf",
				  X86_FEATURE_SMAP)
		      : : "g" (flags) : "memory", "cc");
}

/* These macros can be used in asm() statements */
#define ASM_CLAC \
	ALTERNATIVE(ANNOTATE_IGNORE_ALTERNATIVE "", "clac", X86_FEATURE_SMAP)
#define ASM_STAC \
	ALTERNATIVE(ANNOTATE_IGNORE_ALTERNATIVE "", "stac", X86_FEATURE_SMAP)

#define ASM_CLAC_UNSAFE \
	ALTERNATIVE("", ANNOTATE_IGNORE_ALTERNATIVE "clac", X86_FEATURE_SMAP)
#define ASM_STAC_UNSAFE \
	ALTERNATIVE("", ANNOTATE_IGNORE_ALTERNATIVE "stac", X86_FEATURE_SMAP)

#endif /* __ASSEMBLER__ */

#endif /* _ASM_X86_SMAP_H */
