/*
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_BUG_H
#define __ASM_AVR32_BUG_H

#ifdef CONFIG_BUG

/*
 * According to our Chief Architect, this compact opcode is very
 * unlikely to ever be implemented.
 */
#define AVR32_BUG_OPCODE	0x5df0

#ifdef CONFIG_DEBUG_BUGVERBOSE

#define _BUG_OR_WARN(flags)						\
	asm volatile(							\
		"1:	.hword	%0\n"					\
		"	.section __bug_table,\"a\",@progbits\n"		\
		"2:	.long	1b\n"					\
		"	.long	%1\n"					\
		"	.short	%2\n"					\
		"	.short	%3\n"					\
		"	.org	2b + %4\n"				\
		"	.previous"					\
		:							\
		: "i"(AVR32_BUG_OPCODE), "i"(__FILE__),			\
		  "i"(__LINE__), "i"(flags),				\
		  "i"(sizeof(struct bug_entry)))

#else

#define _BUG_OR_WARN(flags)						\
	asm volatile(							\
		"1:	.hword	%0\n"					\
		"	.section __bug_table,\"a\",@progbits\n"		\
		"2:	.long	1b\n"					\
		"	.short	%1\n"					\
		"	.org	2b + %2\n"				\
		"	.previous"					\
		:							\
		: "i"(AVR32_BUG_OPCODE), "i"(flags),			\
		  "i"(sizeof(struct bug_entry)))

#endif /* CONFIG_DEBUG_BUGVERBOSE */

#define BUG()								\
	do {								\
		_BUG_OR_WARN(0);					\
		for (;;);						\
	} while (0)

#define WARN_ON(condition)							\
	({								\
		int __ret_warn_on = !!(condition);			\
		if (unlikely(__ret_warn_on))				\
			_BUG_OR_WARN(BUGFLAG_WARNING);			\
		unlikely(__ret_warn_on);				\
	})

#define HAVE_ARCH_BUG
#define HAVE_ARCH_WARN_ON

#endif /* CONFIG_BUG */

#include <asm-generic/bug.h>

#endif /* __ASM_AVR32_BUG_H */
