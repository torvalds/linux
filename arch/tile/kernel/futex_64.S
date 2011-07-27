/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Atomically access user memory, but use MMU to avoid propagating
 * kernel exceptions.
 */

#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/futex.h>
#include <asm/page.h>
#include <asm/processor.h>

/*
 * Provide a set of atomic memory operations supporting <asm/futex.h>.
 *
 * r0: user address to manipulate
 * r1: new value to write, or for cmpxchg, old value to compare against
 * r2: (cmpxchg only) new value to write
 *
 * Return __get_user struct, r0 with value, r1 with error.
 */
#define FUTEX_OP(name, ...) \
STD_ENTRY(futex_##name)			\
	__VA_ARGS__;			\
	{				\
	 move   r1, zero;		\
	 jrp    lr			\
	};				\
	STD_ENDPROC(futex_##name);	\
	.pushsection __ex_table,"a";	\
	.quad 1b, get_user_fault;	\
	.popsection

	.pushsection .fixup,"ax"
get_user_fault:
	{ movei r1, -EFAULT; jrp lr }
	ENDPROC(get_user_fault)
	.popsection

FUTEX_OP(cmpxchg, mtspr CMPEXCH_VALUE, r1; 1: cmpexch4 r0, r0, r2)
FUTEX_OP(set, 1: exch4 r0, r0, r1)
FUTEX_OP(add, 1: fetchadd4 r0, r0, r1)
FUTEX_OP(or, 1: fetchor4 r0, r0, r1)
FUTEX_OP(andn, nor r1, r1, zero; 1: fetchand4 r0, r0, r1)
