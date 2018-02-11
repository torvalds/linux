/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FUTEX_H
#define _ASM_FUTEX_H

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

#define __futex_atomic_op1(insn, ret, oldval, uaddr, oparg) \
do {									\
	register unsigned long r8 __asm ("r8") = 0;			\
	__asm__ __volatile__(						\
		"	mf;;					\n"	\
		"[1:] "	insn ";;				\n"	\
		"	.xdata4 \"__ex_table\", 1b-., 2f-.	\n"	\
		"[2:]"							\
		: "+r" (r8), "=r" (oldval)				\
		: "r" (uaddr), "r" (oparg)				\
		: "memory");						\
	ret = r8;							\
} while (0)

#define __futex_atomic_op2(insn, ret, oldval, uaddr, oparg) \
do {									\
	register unsigned long r8 __asm ("r8") = 0;			\
	int val, newval;						\
	do {								\
		__asm__ __volatile__(					\
			"	mf;;				  \n"	\
			"[1:]	ld4 %3=[%4];;			  \n"	\
			"	mov %2=%3			  \n"	\
				insn	";;			  \n"	\
			"	mov ar.ccv=%2;;			  \n"	\
			"[2:]	cmpxchg4.acq %1=[%4],%3,ar.ccv;;  \n"	\
			"	.xdata4 \"__ex_table\", 1b-., 3f-.\n"	\
			"	.xdata4 \"__ex_table\", 2b-., 3f-.\n"	\
			"[3:]"						\
			: "+r" (r8), "=r" (val), "=&r" (oldval),	\
			   "=&r" (newval)				\
			: "r" (uaddr), "r" (oparg)			\
			: "memory");					\
		if (unlikely (r8))					\
			break;						\
	} while (unlikely (val != oldval));				\
	ret = r8;							\
} while (0)

static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	int oldval = 0, ret;

	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op1("xchg4 %1=[%2],%3", ret, oldval, uaddr,
				   oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op2("add %3=%3,%5", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op2("or %3=%3,%5", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op2("and %3=%3,%5", ret, oldval, uaddr,
				   ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op2("xor %3=%3,%5", ret, oldval, uaddr, oparg);
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
	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	{
		register unsigned long r8 __asm ("r8") = 0;
		unsigned long prev;
		__asm__ __volatile__(
			"	mf;;					\n"
			"	mov ar.ccv=%4;;				\n"
			"[1:]	cmpxchg4.acq %1=[%2],%3,ar.ccv		\n"
			"	.xdata4 \"__ex_table\", 1b-., 2f-.	\n"
			"[2:]"
			: "+r" (r8), "=&r" (prev)
			: "r" (uaddr), "r" (newval),
			  "rO" ((long) (unsigned) oldval)
			: "memory");
		*uval = prev;
		return r8;
	}
}

#endif /* _ASM_FUTEX_H */
