#ifndef _ASM_S390_BUG_H
#define _ASM_S390_BUG_H

#include <linux/kernel.h>

#ifdef CONFIG_BUG

#ifdef CONFIG_DEBUG_BUGVERBOSE

#define __EMIT_BUG(x) do {					\
	asm volatile(						\
		"0:	j	0b+2\n"				\
		"1:\n"						\
		".section .rodata.str,\"aMS\",@progbits,1\n"	\
		"2:	.asciz	\""__FILE__"\"\n"		\
		".previous\n"					\
		".section __bug_table,\"a\"\n"			\
		"3:	.long	1b-3b,2b-3b\n"			\
		"	.short	%0,%1\n"			\
		"	.org	3b+%2\n"			\
		".previous\n"					\
		: : "i" (__LINE__),				\
		    "i" (x),					\
		    "i" (sizeof(struct bug_entry)));		\
} while (0)

#else /* CONFIG_DEBUG_BUGVERBOSE */

#define __EMIT_BUG(x) do {				\
	asm volatile(					\
		"0:	j	0b+2\n"			\
		"1:\n"					\
		".section __bug_table,\"a\"\n"		\
		"2:	.long	1b-2b\n"		\
		"	.short	%0\n"			\
		"	.org	2b+%1\n"		\
		".previous\n"				\
		: : "i" (x),				\
		    "i" (sizeof(struct bug_entry)));	\
} while (0)

#endif /* CONFIG_DEBUG_BUGVERBOSE */

#define BUG() do {					\
	__EMIT_BUG(0);					\
	unreachable();					\
} while (0)

#define __WARN_FLAGS(flags) do {			\
	__EMIT_BUG(BUGFLAG_WARNING|(flags));		\
} while (0)

#define WARN_ON(x) ({					\
	int __ret_warn_on = !!(x);			\
	if (__builtin_constant_p(__ret_warn_on)) {	\
		if (__ret_warn_on)			\
			__WARN();			\
	} else {					\
		if (unlikely(__ret_warn_on))		\
			__WARN();			\
	}						\
	unlikely(__ret_warn_on);			\
})

#define HAVE_ARCH_BUG
#define HAVE_ARCH_WARN_ON
#endif /* CONFIG_BUG */

#include <asm-generic/bug.h>

#endif /* _ASM_S390_BUG_H */
