/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC_BUG_H
#define _PARISC_BUG_H

#include <linux/kernel.h>	/* for BUGFLAG_TAINT */

/*
 * Tell the user there is some problem.
 * The offending file and line are encoded in the __bug_table section.
 */

#ifdef CONFIG_BUG
#define HAVE_ARCH_BUG
#define HAVE_ARCH_WARN_ON

/* the break instruction is used as BUG() marker.  */
#define	PARISC_BUG_BREAK_ASM	"break 0x1f, 0x1fff"
#define	PARISC_BUG_BREAK_INSN	0x03ffe01f  /* PARISC_BUG_BREAK_ASM */

#ifdef CONFIG_GENERIC_BUG_RELATIVE_POINTERS
# define __BUG_REL(val) ".word " __stringify(val) " - ."
#else
# define __BUG_REL(val) ".word " __stringify(val)
#endif


#ifdef CONFIG_DEBUG_BUGVERBOSE
#define BUG()								\
	do {								\
		asm volatile("\n"					\
			     "1:\t" PARISC_BUG_BREAK_ASM "\n"		\
			     "\t.pushsection __bug_table,\"a\"\n"	\
			     "\t.align 4\n"				\
			     "2:\t" __BUG_REL(1b) "\n"			\
			     "\t" __BUG_REL(%c0)  "\n"			\
			     "\t.short %1, %2\n"			\
			     "\t.blockz %3-2*4-2*2\n"			\
			     "\t.popsection"				\
			     : : "i" (__FILE__), "i" (__LINE__),	\
			     "i" (0), "i" (sizeof(struct bug_entry)) );	\
		unreachable();						\
	} while(0)

#else
#define BUG()								\
	do {								\
		asm volatile(PARISC_BUG_BREAK_ASM : : );		\
		unreachable();						\
	} while(0)
#endif

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define __WARN_FLAGS(flags)						\
	do {								\
		asm volatile("\n"					\
			     "1:\t" PARISC_BUG_BREAK_ASM "\n"		\
			     "\t.pushsection __bug_table,\"a\"\n"	\
			     "\t.align 4\n"				\
			     "2:\t" __BUG_REL(1b) "\n"			\
			     "\t" __BUG_REL(%c0)  "\n"			\
			     "\t.short %1, %2\n"			\
			     "\t.blockz %3-2*4-2*2\n"			\
			     "\t.popsection"				\
			     : : "i" (__FILE__), "i" (__LINE__),	\
			     "i" (BUGFLAG_WARNING|(flags)),		\
			     "i" (sizeof(struct bug_entry)) );		\
	} while(0)
#else
#define __WARN_FLAGS(flags)						\
	do {								\
		asm volatile("\n"					\
			     "1:\t" PARISC_BUG_BREAK_ASM "\n"		\
			     "\t.pushsection __bug_table,\"a\"\n"	\
			     "\t.align %2\n"				\
			     "2:\t" __BUG_REL(1b) "\n"			\
			     "\t.short %0\n"				\
			     "\t.blockz %1-4-2\n"			\
			     "\t.popsection"				\
			     : : "i" (BUGFLAG_WARNING|(flags)),		\
			     "i" (sizeof(struct bug_entry)) );		\
	} while(0)
#endif


#define WARN_ON(x) ({						\
	int __ret_warn_on = !!(x);				\
	if (__builtin_constant_p(__ret_warn_on)) {		\
		if (__ret_warn_on)				\
			__WARN();				\
	} else {						\
		if (unlikely(__ret_warn_on))			\
			__WARN();				\
	}							\
	unlikely(__ret_warn_on);				\
})

#endif

#include <asm-generic/bug.h>
#endif

