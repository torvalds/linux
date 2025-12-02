/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_BUG_H
#define _ASM_S390_BUG_H

#include <linux/stringify.h>

#ifndef CONFIG_DEBUG_BUGVERBOSE
#define _BUGVERBOSE_LOCATION(file, line)
#else
#define __BUGVERBOSE_LOCATION(file, line)			\
		.pushsection .rodata.str, "aMS", @progbits, 1;	\
	10002:	.ascii file "\0";				\
		.popsection;					\
								\
		.long 10002b - .;				\
		.short line;
#define _BUGVERBOSE_LOCATION(file, line) __BUGVERBOSE_LOCATION(file, line)
#endif

#ifndef CONFIG_GENERIC_BUG
#define __BUG_ENTRY(cond_str, flags)
#else
#define __BUG_ENTRY(cond_str, flags)				\
		.pushsection __bug_table, "aw";			\
		.align 4;					\
	10000:	.long 10001f - .;				\
		_BUGVERBOSE_LOCATION(WARN_CONDITION_STR(cond_str) __FILE__, __LINE__) \
		.short flags;					\
		.popsection;					\
	10001:
#endif

#define ASM_BUG_FLAGS(cond_str, flags)				\
	__BUG_ENTRY(cond_str, flags)				\
	mc		0,0

#define ASM_BUG()	ASM_BUG_FLAGS("", 0)

#define __BUG_FLAGS(cond_str, flags)				\
	asm_inline volatile(__stringify(ASM_BUG_FLAGS(cond_str, flags)));

#define __WARN_FLAGS(cond_str, flags)				\
do {								\
	__BUG_FLAGS(cond_str, BUGFLAG_WARNING|(flags));		\
} while (0)

#define BUG()							\
do {								\
	__BUG_FLAGS("", 0);					\
	unreachable();						\
} while (0)

#define HAVE_ARCH_BUG

#include <asm-generic/bug.h>

#endif /* _ASM_S390_BUG_H */
