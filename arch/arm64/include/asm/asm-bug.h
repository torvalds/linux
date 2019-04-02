#ifndef __ASM_ASM__H
/*
 * Copyright (C) 2017  ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define __ASM_ASM__H

#include <asm/brk-imm.h>

#ifdef CONFIG_DE_VERBOSE
#define _VERBOSE_LOCATION(file, line) __VERBOSE_LOCATION(file, line)
#define __VERBOSE_LOCATION(file, line)			\
		.pushsection .rodata.str,"aMS",@progbits,1;	\
	14472:	.string file;					\
		.popsection;					\
								\
		.long 14472b - 14470b;				\
		.short line;
#else
#define _VERBOSE_LOCATION(file, line)
#endif

#ifdef CONFIG_GENERIC_

#define ___ENTRY(flags) 				\
		.pushsection ___table,"aw";		\
		.align 2;				\
	14470:	.long 14471f - 14470b;			\
_VERBOSE_LOCATION(__FILE__, __LINE__)		\
		.short flags; 				\
		.popsection;				\
	14471:
#else
#define ___ENTRY(flags)
#endif

#define ASM__FLAGS(flags)				\
	___ENTRY(flags)				\
	brk	_BRK_IMM

#define ASM_()	ASM__FLAGS(0)

#endif /* __ASM_ASM__H */
