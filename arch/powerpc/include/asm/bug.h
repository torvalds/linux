/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BUG_H
#define _ASM_POWERPC_BUG_H
#ifdef __KERNEL__

#include <asm/asm-compat.h>

#ifdef CONFIG_BUG

#ifdef __ASSEMBLY__
#include <asm/asm-offsets.h>
#ifdef CONFIG_DEBUG_BUGVERBOSE
.macro EMIT_BUG_ENTRY addr,file,line,flags
	 .section __bug_table,"aw"
5001:	 .4byte \addr - .
	 .4byte 5002f - .
	 .short \line, \flags
	 .org 5001b+BUG_ENTRY_SIZE
	 .previous
	 .section .rodata,"a"
5002:	 .asciz "\file"
	 .previous
.endm
#else
.macro EMIT_BUG_ENTRY addr,file,line,flags
	 .section __bug_table,"aw"
5001:	 .4byte \addr - .
	 .short \flags
	 .org 5001b+BUG_ENTRY_SIZE
	 .previous
.endm
#endif /* verbose */

#else /* !__ASSEMBLY__ */
/* _EMIT_BUG_ENTRY expects args %0,%1,%2,%3 to be FILE, LINE, flags and
   sizeof(struct bug_entry), respectively */
#ifdef CONFIG_DEBUG_BUGVERBOSE
#define _EMIT_BUG_ENTRY				\
	".section __bug_table,\"aw\"\n"		\
	"2:	.4byte 1b - .\n"		\
	"	.4byte %0 - .\n"		\
	"	.short %1, %2\n"		\
	".org 2b+%3\n"				\
	".previous\n"
#else
#define _EMIT_BUG_ENTRY				\
	".section __bug_table,\"aw\"\n"		\
	"2:	.4byte 1b - .\n"		\
	"	.short %2\n"			\
	".org 2b+%3\n"				\
	".previous\n"
#endif

#define BUG_ENTRY(insn, flags, ...)			\
	__asm__ __volatile__(				\
		"1:	" insn "\n"			\
		_EMIT_BUG_ENTRY				\
		: : "i" (__FILE__), "i" (__LINE__),	\
		  "i" (flags),				\
		  "i" (sizeof(struct bug_entry)),	\
		  ##__VA_ARGS__)

/*
 * BUG_ON() and WARN_ON() do their best to cooperate with compile-time
 * optimisations. However depending on the complexity of the condition
 * some compiler versions may not produce optimal results.
 */

#define BUG() do {						\
	BUG_ENTRY("twi 31, 0, 0", 0);				\
	unreachable();						\
} while (0)
#define HAVE_ARCH_BUG

#define __WARN_FLAGS(flags) BUG_ENTRY("twi 31, 0, 0", BUGFLAG_WARNING | (flags))

#ifdef CONFIG_PPC64
#define BUG_ON(x) do {						\
	if (__builtin_constant_p(x)) {				\
		if (x)						\
			BUG();					\
	} else {						\
		BUG_ENTRY(PPC_TLNEI " %4, 0", 0, "r" ((__force long)(x)));	\
	}							\
} while (0)

#define WARN_ON(x) ({						\
	int __ret_warn_on = !!(x);				\
	if (__builtin_constant_p(__ret_warn_on)) {		\
		if (__ret_warn_on)				\
			__WARN();				\
	} else {						\
		BUG_ENTRY(PPC_TLNEI " %4, 0",			\
			  BUGFLAG_WARNING | BUGFLAG_TAINT(TAINT_WARN),	\
			  "r" (__ret_warn_on));	\
	}							\
	unlikely(__ret_warn_on);				\
})

#define HAVE_ARCH_BUG_ON
#define HAVE_ARCH_WARN_ON
#endif

#endif /* __ASSEMBLY __ */
#else
#ifdef __ASSEMBLY__
.macro EMIT_BUG_ENTRY addr,file,line,flags
.endm
#else /* !__ASSEMBLY__ */
#define _EMIT_BUG_ENTRY
#endif
#endif /* CONFIG_BUG */

#define EMIT_WARN_ENTRY EMIT_BUG_ENTRY

#include <asm-generic/bug.h>

#ifndef __ASSEMBLY__

struct pt_regs;
void hash__do_page_fault(struct pt_regs *);
void bad_page_fault(struct pt_regs *, int);
void emulate_single_step(struct pt_regs *regs);
extern void _exception(int, struct pt_regs *, int, unsigned long);
extern void _exception_pkey(struct pt_regs *, unsigned long, int);
extern void die(const char *, struct pt_regs *, long);
void die_mce(const char *str, struct pt_regs *regs, long err);
extern bool die_will_crash(void);
extern void panic_flush_kmsg_start(void);
extern void panic_flush_kmsg_end(void);
#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_BUG_H */
