/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH__H
#define __ASM_SH__H

#include <linux/linkage.h>

#define TRAPA__OPCODE	0xc33e	/* trapa #0x3e */
#define FLAG_UNWINDER	(1 << 1)

#ifdef CONFIG_GENERIC_
#define HAVE_ARCH_
#define HAVE_ARCH_WARN_ON

/**
 * _EMIT__ENTRY
 * %1 - __FILE__
 * %2 - __LINE__
 * %3 - trap type
 * %4 - sizeof(struct _entry)
 *
 * The trapa opcode itself sits in %0.
 * The %O notation is used to avoid # generation.
 *
 * The offending file and line are encoded in the ___table section.
 */
#ifdef CONFIG_DE_VERBOSE
#define _EMIT__ENTRY				\
	"\t.pushsection ___table,\"aw\"\n"	\
	"2:\t.long 1b, %O1\n"			\
	"\t.short %O2, %O3\n"			\
	"\t.org 2b+%O4\n"			\
	"\t.popsection\n"
#else
#define _EMIT__ENTRY				\
	"\t.pushsection ___table,\"aw\"\n"	\
	"2:\t.long 1b\n"			\
	"\t.short %O3\n"			\
	"\t.org 2b+%O4\n"			\
	"\t.popsection\n"
#endif

#define ()						\
do {							\
	__asm__ __volatile__ (				\
		"1:\t.short %O0\n"			\
		_EMIT__ENTRY				\
		 :					\
		 : "n" (TRAPA__OPCODE),		\
		   "i" (__FILE__),			\
		   "i" (__LINE__), "i" (0),		\
		   "i" (sizeof(struct _entry)));	\
	unreachable();					\
} while (0)

#define __WARN_FLAGS(flags)				\
do {							\
	__asm__ __volatile__ (				\
		"1:\t.short %O0\n"			\
		 _EMIT__ENTRY			\
		 :					\
		 : "n" (TRAPA__OPCODE),		\
		   "i" (__FILE__),			\
		   "i" (__LINE__),			\
		   "i" (FLAG_WARNING|(flags)),	\
		   "i" (sizeof(struct _entry)));	\
} while (0)

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

#define UNWINDER_()					\
do {							\
	__asm__ __volatile__ (				\
		"1:\t.short %O0\n"			\
		_EMIT__ENTRY				\
		 :					\
		 : "n" (TRAPA__OPCODE),		\
		   "i" (__FILE__),			\
		   "i" (__LINE__),			\
		   "i" (FLAG_UNWINDER),		\
		   "i" (sizeof(struct _entry)));	\
} while (0)

#define UNWINDER__ON(x) ({					\
	int __ret_unwinder_on = !!(x);				\
	if (__builtin_constant_p(__ret_unwinder_on)) {		\
		if (__ret_unwinder_on)				\
			UNWINDER_();				\
	} else {						\
		if (unlikely(__ret_unwinder_on))		\
			UNWINDER_();				\
	}							\
	unlikely(__ret_unwinder_on);				\
})

#else

#define UNWINDER_	
#define UNWINDER__ON	_ON

#endif /* CONFIG_GENERIC_ */

#include <asm-generic/.h>

struct pt_regs;

/* arch/sh/kernel/traps.c */
extern void die(const char *str, struct pt_regs *regs, long err) __attribute__ ((noreturn));
extern void die_if_kernel(const char *str, struct pt_regs *regs, long err);
extern void die_if_no_fixup(const char *str, struct pt_regs *regs, long err);

#endif /* __ASM_SH__H */
