/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_ASM_BUG_H
/*
 * Copyright (C) 2017  ARM Limited
 */
#define __ASM_ASM_BUG_H

#include <asm/brk-imm.h>

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define _BUGVERBOSE_LOCATION(file, line) __BUGVERBOSE_LOCATION(file, line)
#define __BUGVERBOSE_LOCATION(file, line)			\
		.pushsection .rodata.str,"aMS",@progbits,1;	\
	14472:	.string file;					\
		.popsection;					\
								\
		.long 14472b - .;				\
		.short line;
#else
#define _BUGVERBOSE_LOCATION(file, line)
#endif

#ifdef CONFIG_GENERIC_BUG
#define __BUG_ENTRY_START				\
		.pushsection __bug_table,"aw";		\
		.align 2;				\
	14470:	.long 14471f - .;			\

#define __BUG_ENTRY_END					\
		.align 2;				\
		.popsection;				\
	14471:

#define __BUG_ENTRY(flags)				\
		__BUG_ENTRY_START			\
_BUGVERBOSE_LOCATION(__FILE__, __LINE__)		\
		.short flags;				\
		__BUG_ENTRY_END
#else
#define __BUG_ENTRY(flags)
#endif

#define ASM_BUG_FLAGS(flags)				\
	__BUG_ENTRY(flags)				\
	brk	BUG_BRK_IMM

#define ASM_BUG()	ASM_BUG_FLAGS(0)

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define __BUG_LOCATION_STRING(file, line)		\
		".long " file "- .;"			\
		".short " line ";"
#else
#define __BUG_LOCATION_STRING(file, line)
#endif

#define __BUG_ENTRY_STRING(file, line, flags)		\
		__stringify(__BUG_ENTRY_START)		\
		__BUG_LOCATION_STRING(file, line)	\
		".short " flags ";"			\
		__stringify(__BUG_ENTRY_END)

#define ARCH_WARN_ASM(file, line, flags, size)		\
	__BUG_ENTRY_STRING(file, line, flags)		\
	__stringify(brk BUG_BRK_IMM)

#define ARCH_WARN_REACHABLE

#endif /* __ASM_ASM_BUG_H */
