/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_BUG_H
#define _ASM_S390_BUG_H

#include <linux/compiler.h>

#if defined(CONFIG_BUG) && defined(CONFIG_CC_HAS_ASM_IMMEDIATE_STRINGS)

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define __BUG_ENTRY_VERBOSE(format, file, line)				\
	"	.long	" format " - .	# bug_entry::format\n"		\
	"	.long	" file " - .	# bug_entry::file\n"		\
	"	.short	" line "	# bug_entry::line\n"
#else
#define __BUG_ENTRY_VERBOSE(format, file, line)
#endif

#ifdef CONFIG_DEBUG_BUGVERBOSE_DETAILED
#define WARN_CONDITION_STR(cond_str) cond_str
#else
#define WARN_CONDITION_STR(cond_str) ""
#endif

#define __BUG_ASM(cond_str, flags)					\
do {									\
	asm_inline volatile("\n"					\
		"0:	mc	0,0\n"					\
		"	.section __bug_table,\"aw\"\n"			\
		"1:	.long	0b - .	# bug_entry::bug_addr\n"	\
		__BUG_ENTRY_VERBOSE("%[frmt]", "%[file]", "%[line]")	\
		"	.short	%[flgs]	# bug_entry::flags\n"		\
		"	.org	1b+%[size]\n"				\
		"	.previous"					\
		:							\
		: [frmt] "i" (WARN_CONDITION_STR(cond_str)),		\
		  [file] "i" (__FILE__),				\
		  [line] "i" (__LINE__),				\
		  [flgs] "i" (flags),					\
		  [size] "i" (sizeof(struct bug_entry)));		\
} while (0)

#define BUG()								\
do {									\
	__BUG_ASM("", 0);						\
	unreachable();							\
} while (0)

#define __WARN_FLAGS(cond_str, flags)					\
do {									\
	__BUG_ASM(cond_str, BUGFLAG_WARNING | (flags));			\
} while (0)

#define HAVE_ARCH_BUG
#define HAVE_ARCH_BUG_FORMAT

#endif /* CONFIG_BUG && CONFIG_CC_HAS_ASM_IMMEDIATE_STRINGS */

#include <asm-generic/bug.h>

#endif /* _ASM_S390_BUG_H */
