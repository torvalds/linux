/*
 * Copyright (C) 2015  ARM Limited
 * Author: Dave Martin <Dave.Martin@arm.com>
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

#ifndef _ARCH_ARM64_ASM_BUG_H
#define _ARCH_ARM64_ASM_BUG_H

#include <asm/brk-imm.h>

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define _BUGVERBOSE_LOCATION(file, line) __BUGVERBOSE_LOCATION(file, line)
#define __BUGVERBOSE_LOCATION(file, line)				\
		".pushsection .rodata.str,\"aMS\",@progbits,1\n"	\
	"2:	.string \"" file "\"\n\t"				\
		".popsection\n\t"					\
									\
		".long 2b - 0b\n\t"					\
		".short " #line "\n\t"
#else
#define _BUGVERBOSE_LOCATION(file, line)
#endif

#ifdef CONFIG_GENERIC_BUG

#define __BUG_ENTRY(flags) 				\
		".pushsection __bug_table,\"a\"\n\t"	\
		".align 2\n\t"				\
	"0:	.long 1f - 0b\n\t"			\
_BUGVERBOSE_LOCATION(__FILE__, __LINE__)		\
		".short " #flags "\n\t"			\
		".popsection\n"				\
	"1:	"
#else
#define __BUG_ENTRY(flags) ""
#endif

#define __BUG_FLAGS(flags)				\
	asm volatile (					\
		__BUG_ENTRY(flags)			\
		"brk %[imm]" :: [imm] "i" (BUG_BRK_IMM)	\
	);


#define BUG() do {					\
	__BUG_FLAGS(0);					\
	unreachable();					\
} while (0)

#define __WARN_FLAGS(flags) __BUG_FLAGS(BUGFLAG_WARNING|(flags))

#define HAVE_ARCH_BUG

#include <asm-generic/bug.h>

#endif /* ! _ARCH_ARM64_ASM_BUG_H */
