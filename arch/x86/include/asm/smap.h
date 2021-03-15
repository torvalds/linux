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

/* "Raw" instruction opcodes */
#define __ASM_CLAC	".byte 0x0f,0x01,0xca"
#define __ASM_STAC	".byte 0x0f,0x01,0xcb"

#ifdef __ASSEMBLY__

#include <asm/alternative-asm.h>

#ifdef CONFIG_X86_SMAP

#define ASM_CLAC \
	ALTERNATIVE "", __ASM_CLAC, X86_FEATURE_SMAP

#define ASM_STAC \
	ALTERNATIVE "", __ASM_STAC, X86_FEATURE_SMAP

#else /* CONFIG_X86_SMAP */

#define ASM_CLAC
#define ASM_STAC

#endif /* CONFIG_X86_SMAP */

#else /* __ASSEMBLY__ */

#include <asm/alternative.h>

#ifdef CONFIG_X86_SMAP

static __always_inline void clac(void)
{
	/* Note: a barrier is implicit in alternative() */
	alternative("", __ASM_CLAC, X86_FEATURE_SMAP);
}

static __always_inline void stac(void)
{
	/* Note: a barrier is implicit in alternative() */
	alternative("", __ASM_STAC, X86_FEATURE_SMAP);
}

static __always_inline unsigned long smap_save(void)
{
	unsigned long flags;

	asm volatile ("# smap_save\n\t"
		      ALTERNATIVE("", "pushf; pop %0; " __ASM_CLAC "\n\t",
				  X86_FEATURE_SMAP)
		      : "=rm" (flags) : : "memory", "cc");

	return flags;
}

static __always_inline void smap_restore(unsigned long flags)
{
	asm volatile ("# smap_restore\n\t"
		      ALTERNATIVE("", "push %0; popf\n\t",
				  X86_FEATURE_SMAP)
		      : : "g" (flags) : "memory", "cc");
}

/* These macros can be used in asm() statements */
#define ASM_CLAC \
	ALTERNATIVE("", __ASM_CLAC, X86_FEATURE_SMAP)
#define ASM_STAC \
	ALTERNATIVE("", __ASM_STAC, X86_FEATURE_SMAP)

#else /* CONFIG_X86_SMAP */

static inline void clac(void) { }
static inline void stac(void) { }

static inline unsigned long smap_save(void) { return 0; }
static inline void smap_restore(unsigned long flags) { }

#define ASM_CLAC
#define ASM_STAC

#endif /* CONFIG_X86_SMAP */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_SMAP_H */
