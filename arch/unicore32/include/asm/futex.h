/*
 * linux/arch/unicore32/include/asm/futex.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UNICORE_FUTEX_H__
#define __UNICORE_FUTEX_H__

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/preempt.h>
#include <linux/uaccess.h>
#include <linux/errno.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg)	\
	__asm__ __volatile__(					\
	"1:	ldw.u	%1, [%2]\n"				\
	"	" insn "\n"					\
	"2:	stw.u	%0, [%2]\n"				\
	"	mov	%0, #0\n"				\
	"3:\n"							\
	"	.pushsection __ex_table,\"a\"\n"		\
	"	.align	3\n"					\
	"	.long	1b, 4f, 2b, 4f\n"			\
	"	.popsection\n"					\
	"	.pushsection .fixup,\"ax\"\n"			\
	"4:	mov	%0, %4\n"				\
	"	b	3b\n"					\
	"	.popsection"					\
	: "=&r" (ret), "=&r" (oldval)				\
	: "r" (uaddr), "r" (oparg), "Ir" (-EFAULT)		\
	: "cc", "memory")

static inline int
futex_atomic_op_inuser(int encoded_op, int __user *uaddr)
{
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int oldval = 0, ret;

	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(int)))
		return -EFAULT;

	pagefault_disable();	/* implies preempt_disable() */

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("mov	%0, %3", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("add	%0, %1, %3", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or	%0, %1, %3", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("and	%0, %1, %3",
				ret, oldval, uaddr, ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor	%0, %1, %3", ret, oldval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	pagefault_enable();	/* subsumes preempt_enable() */

	if (!ret) {
		switch (cmp) {
		case FUTEX_OP_CMP_EQ:
			ret = (oldval == cmparg);
			break;
		case FUTEX_OP_CMP_NE:
			ret = (oldval != cmparg);
			break;
		case FUTEX_OP_CMP_LT:
			ret = (oldval <  cmparg);
			break;
		case FUTEX_OP_CMP_GE:
			ret = (oldval >= cmparg);
			break;
		case FUTEX_OP_CMP_LE:
			ret = (oldval <= cmparg);
			break;
		case FUTEX_OP_CMP_GT:
			ret = (oldval >  cmparg);
			break;
		default:
			ret = -ENOSYS;
		}
	}
	return ret;
}

static inline int
futex_atomic_cmpxchg_inatomic(int __user *uaddr, int oldval, int newval)
{
	int val;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(int)))
		return -EFAULT;

	pagefault_disable();	/* implies preempt_disable() */

	__asm__ __volatile__("@futex_atomic_cmpxchg_inatomic\n"
	"1:	ldw.u	%0, [%3]\n"
	"	cmpxor.a	%0, %1\n"
	"	bne	3f\n"
	"2:	stw.u	%2, [%3]\n"
	"3:\n"
	"	.pushsection __ex_table,\"a\"\n"
	"	.align	3\n"
	"	.long	1b, 4f, 2b, 4f\n"
	"	.popsection\n"
	"	.pushsection .fixup,\"ax\"\n"
	"4:	mov	%0, %4\n"
	"	b	3b\n"
	"	.popsection"
	: "=&r" (val)
	: "r" (oldval), "r" (newval), "r" (uaddr), "Ir" (-EFAULT)
	: "cc", "memory");

	pagefault_enable();	/* subsumes preempt_enable() */

	return val;
}

#endif /* __KERNEL__ */
#endif /* __UNICORE_FUTEX_H__ */
