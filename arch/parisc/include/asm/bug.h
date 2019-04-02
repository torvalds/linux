/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC__H
#define _PARISC__H

#include <linux/kernel.h>	/* for FLAG_TAINT */

/*
 * Tell the user there is some problem.
 * The offending file and line are encoded in the ___table section.
 */

#ifdef CONFIG_
#define HAVE_ARCH_
#define HAVE_ARCH_WARN_ON

/* the break instruction is used as () marker.  */
#define	PARISC__BREAK_ASM	"break 0x1f, 0x1fff"
#define	PARISC__BREAK_INSN	0x03ffe01f  /* PARISC__BREAK_ASM */

#if defined(CONFIG_64BIT)
#define ASM_WORD_INSN		".dword\t"
#else
#define ASM_WORD_INSN		".word\t"
#endif

#ifdef CONFIG_DE_VERBOSE
#define ()								\
	do {								\
		asm volatile("\n"					\
			     "1:\t" PARISC__BREAK_ASM "\n"		\
			     "\t.pushsection ___table,\"aw\"\n"	\
			     "2:\t" ASM_WORD_INSN "1b, %c0\n"		\
			     "\t.short %c1, %c2\n"			\
			     "\t.org 2b+%c3\n"				\
			     "\t.popsection"				\
			     : : "i" (__FILE__), "i" (__LINE__),	\
			     "i" (0), "i" (sizeof(struct _entry)) ); \
		unreachable();						\
	} while(0)

#else
#define ()								\
	do {								\
		asm volatile(PARISC__BREAK_ASM : : );		\
		unreachable();						\
	} while(0)
#endif

#ifdef CONFIG_DE_VERBOSE
#define __WARN_FLAGS(flags)						\
	do {								\
		asm volatile("\n"					\
			     "1:\t" PARISC__BREAK_ASM "\n"		\
			     "\t.pushsection ___table,\"aw\"\n"	\
			     "2:\t" ASM_WORD_INSN "1b, %c0\n"		\
			     "\t.short %c1, %c2\n"			\
			     "\t.org 2b+%c3\n"				\
			     "\t.popsection"				\
			     : : "i" (__FILE__), "i" (__LINE__),	\
			     "i" (FLAG_WARNING|(flags)),		\
			     "i" (sizeof(struct _entry)) );		\
	} while(0)
#else
#define __WARN_FLAGS(flags)						\
	do {								\
		asm volatile("\n"					\
			     "1:\t" PARISC__BREAK_ASM "\n"		\
			     "\t.pushsection ___table,\"aw\"\n"	\
			     "2:\t" ASM_WORD_INSN "1b\n"		\
			     "\t.short %c0\n"				\
			     "\t.org 2b+%c1\n"				\
			     "\t.popsection"				\
			     : : "i" (FLAG_WARNING|(flags)),		\
			     "i" (sizeof(struct _entry)) );		\
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

#include <asm-generic/.h>
#endif

