/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_FUTEX_H
#define _ASM_FUTEX_H

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/barrier.h>
#include <asm/errno.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg)		\
{									\
	__asm__ __volatile__(						\
	"1:	ll.w	%1, %4 # __futex_atomic_op\n"		\
	"	" insn	"				\n"	\
	"2:	sc.w	$t0, %2				\n"	\
	"	beqz	$t0, 1b				\n"	\
	"3:						\n"	\
	"	.section .fixup,\"ax\"			\n"	\
	"4:	li.w	%0, %6				\n"	\
	"	b	3b				\n"	\
	"	.previous				\n"	\
	"	.section __ex_table,\"a\"		\n"	\
	"	"__UA_ADDR "\t1b, 4b			\n"	\
	"	"__UA_ADDR "\t2b, 4b			\n"	\
	"	.previous				\n"	\
	: "=r" (ret), "=&r" (oldval),				\
	  "=ZC" (*uaddr)					\
	: "0" (0), "ZC" (*uaddr), "Jr" (oparg),			\
	  "i" (-EFAULT)						\
	: "memory", "t0");					\
}

static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	int oldval = 0, ret = 0;

	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("move $t0, %z5", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("add.w $t0, %1, %z5", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or	$t0, %1, %z5", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("and	$t0, %1, %z5", ret, oldval, uaddr, ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor	$t0, %1, %z5", ret, oldval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	pagefault_enable();

	if (!ret)
		*oval = oldval;

	return ret;
}

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr, u32 oldval, u32 newval)
{
	int ret = 0;
	u32 val = 0;

	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;

	__asm__ __volatile__(
	"# futex_atomic_cmpxchg_inatomic			\n"
	"1:	ll.w	%1, %3					\n"
	"	bne	%1, %z4, 3f				\n"
	"	move	$t0, %z5				\n"
	"2:	sc.w	$t0, %2					\n"
	"	beqz	$t0, 1b					\n"
	"3:							\n"
	__WEAK_LLSC_MB
	"	.section .fixup,\"ax\"				\n"
	"4:	li.d	%0, %6					\n"
	"	b	3b					\n"
	"	.previous					\n"
	"	.section __ex_table,\"a\"			\n"
	"	"__UA_ADDR "\t1b, 4b				\n"
	"	"__UA_ADDR "\t2b, 4b				\n"
	"	.previous					\n"
	: "+r" (ret), "=&r" (val), "=ZC" (*uaddr)
	: "ZC" (*uaddr), "Jr" (oldval), "Jr" (newval),
	  "i" (-EFAULT)
	: "memory", "t0");

	*uval = val;

	return ret;
}

#endif /* _ASM_FUTEX_H */
