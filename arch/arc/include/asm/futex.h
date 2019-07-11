/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vineetg: August 2010: From Android kernel work
 */

#ifndef _ASM_FUTEX_H
#define _ASM_FUTEX_H

#include <linux/futex.h>
#include <linux/preempt.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

#ifdef CONFIG_ARC_HAS_LLSC

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg)\
							\
	smp_mb();					\
	__asm__ __volatile__(				\
	"1:	llock	%1, [%2]		\n"	\
		insn				"\n"	\
	"2:	scond	%0, [%2]		\n"	\
	"	bnz	1b			\n"	\
	"	mov %0, 0			\n"	\
	"3:					\n"	\
	"	.section .fixup,\"ax\"		\n"	\
	"	.align  4			\n"	\
	"4:	mov %0, %4			\n"	\
	"	j   3b				\n"	\
	"	.previous			\n"	\
	"	.section __ex_table,\"a\"	\n"	\
	"	.align  4			\n"	\
	"	.word   1b, 4b			\n"	\
	"	.word   2b, 4b			\n"	\
	"	.previous			\n"	\
							\
	: "=&r" (ret), "=&r" (oldval)			\
	: "r" (uaddr), "r" (oparg), "ir" (-EFAULT)	\
	: "cc", "memory");				\
	smp_mb()					\

#else	/* !CONFIG_ARC_HAS_LLSC */

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg)\
							\
	smp_mb();					\
	__asm__ __volatile__(				\
	"1:	ld	%1, [%2]		\n"	\
		insn				"\n"	\
	"2:	st	%0, [%2]		\n"	\
	"	mov %0, 0			\n"	\
	"3:					\n"	\
	"	.section .fixup,\"ax\"		\n"	\
	"	.align  4			\n"	\
	"4:	mov %0, %4			\n"	\
	"	j   3b				\n"	\
	"	.previous			\n"	\
	"	.section __ex_table,\"a\"	\n"	\
	"	.align  4			\n"	\
	"	.word   1b, 4b			\n"	\
	"	.word   2b, 4b			\n"	\
	"	.previous			\n"	\
							\
	: "=&r" (ret), "=&r" (oldval)			\
	: "r" (uaddr), "r" (oparg), "ir" (-EFAULT)	\
	: "cc", "memory");				\
	smp_mb()					\

#endif

static inline int arch_futex_atomic_op_inuser(int op, int oparg, int *oval,
		u32 __user *uaddr)
{
	int oldval = 0, ret;

#ifndef CONFIG_ARC_HAS_LLSC
	preempt_disable();	/* to guarantee atomic r-m-w of futex op */
#endif
	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("mov %0, %3", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		/* oldval = *uaddr; *uaddr += oparg ; ret = *uaddr */
		__futex_atomic_op("add %0, %1, %3", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or  %0, %1, %3", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("bic %0, %1, %3", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor %0, %1, %3", ret, oldval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	pagefault_enable();
#ifndef CONFIG_ARC_HAS_LLSC
	preempt_enable();
#endif

	if (!ret)
		*oval = oldval;

	return ret;
}

/*
 * cmpxchg of futex (pagefaults disabled by caller)
 * Return 0 for success, -EFAULT otherwise
 */
static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr, u32 expval,
			      u32 newval)
{
	int ret = 0;
	u32 existval;

	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;

#ifndef CONFIG_ARC_HAS_LLSC
	preempt_disable();	/* to guarantee atomic r-m-w of futex op */
#endif
	smp_mb();

	__asm__ __volatile__(
#ifdef CONFIG_ARC_HAS_LLSC
	"1:	llock	%1, [%4]		\n"
	"	brne	%1, %2, 3f		\n"
	"2:	scond	%3, [%4]		\n"
	"	bnz	1b			\n"
#else
	"1:	ld	%1, [%4]		\n"
	"	brne	%1, %2, 3f		\n"
	"2:	st	%3, [%4]		\n"
#endif
	"3:	\n"
	"	.section .fixup,\"ax\"	\n"
	"4:	mov %0, %5	\n"
	"	j   3b	\n"
	"	.previous	\n"
	"	.section __ex_table,\"a\"	\n"
	"	.align  4	\n"
	"	.word   1b, 4b	\n"
	"	.word   2b, 4b	\n"
	"	.previous\n"
	: "+&r"(ret), "=&r"(existval)
	: "r"(expval), "r"(newval), "r"(uaddr), "ir"(-EFAULT)
	: "cc", "memory");

	smp_mb();

#ifndef CONFIG_ARC_HAS_LLSC
	preempt_enable();
#endif
	*uval = existval;
	return ret;
}

#endif
