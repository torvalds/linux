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
		.long 14472b - 14470b;				\
		.short line;
#else
#define _BUGVERBOSE_LOCATION(file, line)
#endif

#ifdef CONFIG_GENERIC_BUG

#define __BUG_ENTRY(flags) 				\
		.pushsection __bug_table,"aw";		\
		.align 2;				\
	14470:	.long 14471f - 14470b;			\
_BUGVERBOSE_LOCATION(__FILE__, __LINE__)		\
		.short flags; 				\
		.popsection;				\
	14471:
#else
#define __BUG_ENTRY(flags)
#endif

#define ASM_BUG_FLAGS(flags)				\
	__BUG_ENTRY(flags)				\
	brk	BUG_BRK_IMM

#define ASM_BUG()	ASM_BUG_FLAGS(0)

#endif /* __ASM_ASM_BUG_H */
