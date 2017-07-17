#ifndef __ASM_SH_BUG_H
#define __ASM_SH_BUG_H

#include <linux/linkage.h>

#define TRAPA_BUG_OPCODE	0xc33e	/* trapa #0x3e */
#define BUGFLAG_UNWINDER	(1 << 1)

#ifdef CONFIG_GENERIC_BUG
#define HAVE_ARCH_BUG
#define HAVE_ARCH_WARN_ON

/**
 * _EMIT_BUG_ENTRY
 * %1 - __FILE__
 * %2 - __LINE__
 * %3 - trap type
 * %4 - sizeof(struct bug_entry)
 *
 * The trapa opcode itself sits in %0.
 * The %O notation is used to avoid # generation.
 *
 * The offending file and line are encoded in the __bug_table section.
 */
#ifdef CONFIG_DEBUG_BUGVERBOSE
#define _EMIT_BUG_ENTRY				\
	"\t.pushsection __bug_table,\"a\"\n"	\
	"2:\t.long 1b, %O1\n"			\
	"\t.short %O2, %O3\n"			\
	"\t.org 2b+%O4\n"			\
	"\t.popsection\n"
#else
#define _EMIT_BUG_ENTRY				\
	"\t.pushsection __bug_table,\"a\"\n"	\
	"2:\t.long 1b\n"			\
	"\t.short %O3\n"			\
	"\t.org 2b+%O4\n"			\
	"\t.popsection\n"
#endif

#define BUG()						\
do {							\
	__asm__ __volatile__ (				\
		"1:\t.short %O0\n"			\
		_EMIT_BUG_ENTRY				\
		 :					\
		 : "n" (TRAPA_BUG_OPCODE),		\
		   "i" (__FILE__),			\
		   "i" (__LINE__), "i" (0),		\
		   "i" (sizeof(struct bug_entry)));	\
	unreachable();					\
} while (0)

#define __WARN_FLAGS(flags)				\
do {							\
	__asm__ __volatile__ (				\
		"1:\t.short %O0\n"			\
		 _EMIT_BUG_ENTRY			\
		 :					\
		 : "n" (TRAPA_BUG_OPCODE),		\
		   "i" (__FILE__),			\
		   "i" (__LINE__),			\
		   "i" (BUGFLAG_WARNING|(flags)),	\
		   "i" (sizeof(struct bug_entry)));	\
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

#define UNWINDER_BUG()					\
do {							\
	__asm__ __volatile__ (				\
		"1:\t.short %O0\n"			\
		_EMIT_BUG_ENTRY				\
		 :					\
		 : "n" (TRAPA_BUG_OPCODE),		\
		   "i" (__FILE__),			\
		   "i" (__LINE__),			\
		   "i" (BUGFLAG_UNWINDER),		\
		   "i" (sizeof(struct bug_entry)));	\
} while (0)

#define UNWINDER_BUG_ON(x) ({					\
	int __ret_unwinder_on = !!(x);				\
	if (__builtin_constant_p(__ret_unwinder_on)) {		\
		if (__ret_unwinder_on)				\
			UNWINDER_BUG();				\
	} else {						\
		if (unlikely(__ret_unwinder_on))		\
			UNWINDER_BUG();				\
	}							\
	unlikely(__ret_unwinder_on);				\
})

#else

#define UNWINDER_BUG	BUG
#define UNWINDER_BUG_ON	BUG_ON

#endif /* CONFIG_GENERIC_BUG */

#include <asm-generic/bug.h>

struct pt_regs;

/* arch/sh/kernel/traps.c */
extern void die(const char *str, struct pt_regs *regs, long err) __attribute__ ((noreturn));
extern void die_if_kernel(const char *str, struct pt_regs *regs, long err);
extern void die_if_no_fixup(const char *str, struct pt_regs *regs, long err);

#endif /* __ASM_SH_BUG_H */
