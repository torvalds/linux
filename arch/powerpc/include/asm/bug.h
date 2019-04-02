/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC__H
#define _ASM_POWERPC__H
#ifdef __KERNEL__

#include <asm/asm-compat.h>

/*
 * Define an illegal instr to trap on the .
 * We don't use 0 because that marks the end of a function
 * in the ELF ABI.  That's "Boo Boo" in case you wonder...
 */
#define _OPCODE .long 0x00b00b00  /* For asm */
#define _ILLEGAL_INSTR "0x00b00b00" /* For  macro */

#ifdef CONFIG_

#ifdef __ASSEMBLY__
#include <asm/asm-offsets.h>
#ifdef CONFIG_DE_VERBOSE
.macro EMIT__ENTRY addr,file,line,flags
	 .section ___table,"aw"
5001:	 PPC_LONG \addr, 5002f
	 .short \line, \flags
	 .org 5001b+_ENTRY_SIZE
	 .previous
	 .section .rodata,"a"
5002:	 .asciz "\file"
	 .previous
.endm
#else
.macro EMIT__ENTRY addr,file,line,flags
	 .section ___table,"aw"
5001:	 PPC_LONG \addr
	 .short \flags
	 .org 5001b+_ENTRY_SIZE
	 .previous
.endm
#endif /* verbose */

#else /* !__ASSEMBLY__ */
/* _EMIT__ENTRY expects args %0,%1,%2,%3 to be FILE, LINE, flags and
   sizeof(struct _entry), respectively */
#ifdef CONFIG_DE_VERBOSE
#define _EMIT__ENTRY				\
	".section ___table,\"aw\"\n"		\
	"2:\t" PPC_LONG "1b, %0\n"		\
	"\t.short %1, %2\n"			\
	".org 2b+%3\n"				\
	".previous\n"
#else
#define _EMIT__ENTRY				\
	".section ___table,\"aw\"\n"		\
	"2:\t" PPC_LONG "1b\n"			\
	"\t.short %2\n"				\
	".org 2b+%3\n"				\
	".previous\n"
#endif

/*
 * _ON() and WARN_ON() do their best to cooperate with compile-time
 * optimisations. However depending on the complexity of the condition
 * some compiler versions may not produce optimal results.
 */

#define () do {						\
	__asm__ __volatile__(					\
		"1:	twi 31,0,0\n"				\
		_EMIT__ENTRY					\
		: : "i" (__FILE__), "i" (__LINE__),		\
		    "i" (0), "i"  (sizeof(struct _entry)));	\
	unreachable();						\
} while (0)

#define _ON(x) do {						\
	if (__builtin_constant_p(x)) {				\
		if (x)						\
			();					\
	} else {						\
		__asm__ __volatile__(				\
		"1:	"PPC_TLNEI"	%4,0\n"			\
		_EMIT__ENTRY					\
		: : "i" (__FILE__), "i" (__LINE__), "i" (0),	\
		  "i" (sizeof(struct _entry)),		\
		  "r" ((__force long)(x)));			\
	}							\
} while (0)

#define __WARN_FLAGS(flags) do {				\
	__asm__ __volatile__(					\
		"1:	twi 31,0,0\n"				\
		_EMIT__ENTRY					\
		: : "i" (__FILE__), "i" (__LINE__),		\
		  "i" (FLAG_WARNING|(flags)),		\
		  "i" (sizeof(struct _entry)));		\
} while (0)

#define WARN_ON(x) ({						\
	int __ret_warn_on = !!(x);				\
	if (__builtin_constant_p(__ret_warn_on)) {		\
		if (__ret_warn_on)				\
			__WARN();				\
	} else {						\
		__asm__ __volatile__(				\
		"1:	"PPC_TLNEI"	%4,0\n"			\
		_EMIT__ENTRY					\
		: : "i" (__FILE__), "i" (__LINE__),		\
		  "i" (FLAG_WARNING|FLAG_TAINT(TAINT_WARN)),\
		  "i" (sizeof(struct _entry)),		\
		  "r" (__ret_warn_on));				\
	}							\
	unlikely(__ret_warn_on);				\
})

#define HAVE_ARCH_
#define HAVE_ARCH__ON
#define HAVE_ARCH_WARN_ON
#endif /* __ASSEMBLY __ */
#else
#ifdef __ASSEMBLY__
.macro EMIT__ENTRY addr,file,line,flags
.endm
#else /* !__ASSEMBLY__ */
#define _EMIT__ENTRY
#endif
#endif /* CONFIG_ */

#include <asm-generic/.h>

#ifndef __ASSEMBLY__

struct pt_regs;
extern int do_page_fault(struct pt_regs *, unsigned long, unsigned long);
extern void bad_page_fault(struct pt_regs *, unsigned long, int);
extern void _exception(int, struct pt_regs *, int, unsigned long);
extern void _exception_pkey(struct pt_regs *, unsigned long, int);
extern void die(const char *, struct pt_regs *, long);
extern bool die_will_crash(void);
extern void panic_flush_kmsg_start(void);
extern void panic_flush_kmsg_end(void);
#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC__H */
