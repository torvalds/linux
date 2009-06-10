/* MN10300 Kernel bug reporting
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_BUG_H
#define _ASM_BUG_H

#ifdef CONFIG_BUG

/*
 * Tell the user there is some problem.
 */
#define BUG()							\
do {								\
	asm volatile(						\
		"	syscall 15			\n"	\
		"0:					\n"	\
		"	.section __bug_table,\"a\"	\n"	\
		"	.long 0b,%0,%1			\n"	\
		"	.previous			\n"	\
		:						\
		: "i"(__FILE__), "i"(__LINE__)			\
		);						\
} while (1)

#define HAVE_ARCH_BUG
#endif /* CONFIG_BUG */

#include <asm-generic/bug.h>

#endif /* _ASM_BUG_H */
