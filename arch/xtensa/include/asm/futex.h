/*
 * Atomic futex routines
 *
 * Based on the PowerPC implementataion
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2013 TangoTec Ltd.
 *
 * Baruch Siach <baruch@tkos.co.il>
 */

#ifndef _ASM_XTENSA_FUTEX_H
#define _ASM_XTENSA_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <linux/errno.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg) \
	__asm__ __volatile(				\
	"1:	l32i	%0, %2, 0\n"			\
		insn "\n"				\
	"	wsr	%0, scompare1\n"		\
	"2:	s32c1i	%1, %2, 0\n"			\
	"	bne	%1, %0, 1b\n"			\
	"	movi	%1, 0\n"			\
	"3:\n"						\
	"	.section .fixup,\"ax\"\n"		\
	"	.align 4\n"				\
	"4:	.long	3b\n"				\
	"5:	l32r	%0, 4b\n"			\
	"	movi	%1, %3\n"			\
	"	jx	%0\n"				\
	"	.previous\n"				\
	"	.section __ex_table,\"a\"\n"		\
	"	.long 1b,5b,2b,5b\n"			\
	"	.previous\n"				\
	: "=&r" (oldval), "=&r" (ret)			\
	: "r" (uaddr), "I" (-EFAULT), "r" (oparg)	\
	: "memory")

static inline int arch_futex_atomic_op_inuser(int op, int oparg, int *oval,
		u32 __user *uaddr)
{
	int oldval = 0, ret;

#if !XCHAL_HAVE_S32C1I
	return -ENOSYS;
#endif

	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("mov %1, %4", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("add %1, %0, %4", ret, oldval, uaddr,
				oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or %1, %0, %4", ret, oldval, uaddr,
				oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("and %1, %0, %4", ret, oldval, uaddr,
				~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor %1, %0, %4", ret, oldval, uaddr,
				oparg);
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
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	int ret = 0;
	u32 prev;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

#if !XCHAL_HAVE_S32C1I
	return -ENOSYS;
#endif

	__asm__ __volatile__ (
	"	# futex_atomic_cmpxchg_inatomic\n"
	"1:	l32i	%1, %3, 0\n"
	"	mov	%0, %5\n"
	"	wsr	%1, scompare1\n"
	"2:	s32c1i	%0, %3, 0\n"
	"3:\n"
	"	.section .fixup,\"ax\"\n"
	"	.align 4\n"
	"4:	.long	3b\n"
	"5:	l32r	%1, 4b\n"
	"	movi	%0, %6\n"
	"	jx	%1\n"
	"	.previous\n"
	"	.section __ex_table,\"a\"\n"
	"	.long 1b,5b,2b,5b\n"
	"	.previous\n"
	: "+r" (ret), "=&r" (prev), "+m" (*uaddr)
	: "r" (uaddr), "r" (oldval), "r" (newval), "I" (-EFAULT)
	: "memory");

	*uval = prev;
	return ret;
}

#endif /* __KERNEL__ */
#endif /* _ASM_XTENSA_FUTEX_H */
