/*
 * Supervisor Mode Access Prevention support
 *
 * Copyright (C) 2012 Intel Corporation
 * Author: H. Peter Anvin <hpa@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef _ASM_X86_SMAP_H
#define _ASM_X86_SMAP_H

#include <linux/stringify.h>
#include <asm/nops.h>
#include <asm/cpufeature.h>

/* "Raw" instruction opcodes */
#define __ASM_CLAC	.byte 0x0f,0x01,0xca
#define __ASM_STAC	.byte 0x0f,0x01,0xcb

#ifdef __ASSEMBLY__

#include <asm/alternative-asm.h>

#ifdef CONFIG_X86_SMAP

#define ASM_CLAC							\
	661: ASM_NOP3 ;							\
	.pushsection .altinstr_replacement, "ax" ;			\
	662: __ASM_CLAC ;						\
	.popsection ;							\
	.pushsection .altinstructions, "a" ;				\
	altinstruction_entry 661b, 662b, X86_FEATURE_SMAP, 3, 3 ;	\
	.popsection

#define ASM_STAC							\
	661: ASM_NOP3 ;							\
	.pushsection .altinstr_replacement, "ax" ;			\
	662: __ASM_STAC ;						\
	.popsection ;							\
	.pushsection .altinstructions, "a" ;				\
	altinstruction_entry 661b, 662b, X86_FEATURE_SMAP, 3, 3 ;	\
	.popsection

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
	alternative(ASM_NOP3, __stringify(__ASM_CLAC), X86_FEATURE_SMAP);
}

static __always_inline void stac(void)
{
	/* Note: a barrier is implicit in alternative() */
	alternative(ASM_NOP3, __stringify(__ASM_STAC), X86_FEATURE_SMAP);
}

/* These macros can be used in asm() statements */
#define ASM_CLAC \
	ALTERNATIVE(ASM_NOP3, __stringify(__ASM_CLAC), X86_FEATURE_SMAP)
#define ASM_STAC \
	ALTERNATIVE(ASM_NOP3, __stringify(__ASM_STAC), X86_FEATURE_SMAP)

#else /* CONFIG_X86_SMAP */

static inline void clac(void) { }
static inline void stac(void) { }

#define ASM_CLAC
#define ASM_STAC

#endif /* CONFIG_X86_SMAP */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_SMAP_H */
