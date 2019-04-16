/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ALPHA_FUTEX_H
#define _ASM_ALPHA_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>
#include <asm/barrier.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg)	\
	__asm__ __volatile__(					\
		__ASM_SMP_MB					\
	"1:	ldl_l	%0,0(%2)\n"				\
		insn						\
	"2:	stl_c	%1,0(%2)\n"				\
	"	beq	%1,4f\n"				\
	"	mov	$31,%1\n"				\
	"3:	.subsection 2\n"				\
	"4:	br	1b\n"					\
	"	.previous\n"					\
	EXC(1b,3b,$31,%1)					\
	EXC(2b,3b,$31,%1)					\
	:	"=&r" (oldval), "=&r"(ret)			\
	:	"r" (uaddr), "r"(oparg)				\
	:	"memory")

static inline int arch_futex_atomic_op_inuser(int op, int oparg, int *oval,
		u32 __user *uaddr)
{
	int oldval = 0, ret;

	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("mov %3,%1\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("addl %0,%3,%1\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or %0,%3,%1\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("andnot %0,%3,%1\n", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor %0,%3,%1\n", ret, oldval, uaddr, oparg);
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
	int ret = 0, cmp;
	u32 prev;

	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;

	__asm__ __volatile__ (
		__ASM_SMP_MB
	"1:	ldl_l	%1,0(%3)\n"
	"	cmpeq	%1,%4,%2\n"
	"	beq	%2,3f\n"
	"	mov	%5,%2\n"
	"2:	stl_c	%2,0(%3)\n"
	"	beq	%2,4f\n"
	"3:	.subsection 2\n"
	"4:	br	1b\n"
	"	.previous\n"
	EXC(1b,3b,$31,%0)
	EXC(2b,3b,$31,%0)
	:	"+r"(ret), "=&r"(prev), "=&r"(cmp)
	:	"r"(uaddr), "r"((long)(int)oldval), "r"(newval)
	:	"memory");

	*uval = prev;
	return ret;
}

#endif /* __KERNEL__ */
#endif /* _ASM_ALPHA_FUTEX_H */
