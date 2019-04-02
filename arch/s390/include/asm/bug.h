/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390__H
#define _ASM_S390__H

#include <linux/kernel.h>

#ifdef CONFIG_

#ifdef CONFIG_DE_VERBOSE

#define __EMIT_(x) do {					\
	asm volatile(						\
		"0:	j	0b+2\n"				\
		"1:\n"						\
		".section .rodata.str,\"aMS\",@progbits,1\n"	\
		"2:	.asciz	\""__FILE__"\"\n"		\
		".previous\n"					\
		".section ___table,\"aw\"\n"			\
		"3:	.long	1b-3b,2b-3b\n"			\
		"	.short	%0,%1\n"			\
		"	.org	3b+%2\n"			\
		".previous\n"					\
		: : "i" (__LINE__),				\
		    "i" (x),					\
		    "i" (sizeof(struct _entry)));		\
} while (0)

#else /* CONFIG_DE_VERBOSE */

#define __EMIT_(x) do {				\
	asm volatile(					\
		"0:	j	0b+2\n"			\
		"1:\n"					\
		".section ___table,\"aw\"\n"		\
		"2:	.long	1b-2b\n"		\
		"	.short	%0\n"			\
		"	.org	2b+%1\n"		\
		".previous\n"				\
		: : "i" (x),				\
		    "i" (sizeof(struct _entry)));	\
} while (0)

#endif /* CONFIG_DE_VERBOSE */

#define () do {					\
	__EMIT_(0);					\
	unreachable();					\
} while (0)

#define __WARN_FLAGS(flags) do {			\
	__EMIT_(FLAG_WARNING|(flags));		\
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

#define HAVE_ARCH_
#define HAVE_ARCH_WARN_ON
#endif /* CONFIG_ */

#include <asm-generic/.h>

#endif /* _ASM_S390__H */
