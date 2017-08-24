#ifndef _ASM_MICROBLAZE_FUTEX_H
#define _ASM_MICROBLAZE_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg) \
({									\
	__asm__ __volatile__ (						\
			"1:	lwx	%0, %2, r0; "			\
				insn					\
			"2:	swx	%1, %2, r0;			\
				addic	%1, r0, 0;			\
				bnei	%1, 1b;				\
			3:						\
			.section .fixup,\"ax\";				\
			4:	brid	3b;				\
				addik	%1, r0, %3;			\
			.previous;					\
			.section __ex_table,\"a\";			\
			.word	1b,4b,2b,4b;				\
			.previous;"					\
	: "=&r" (oldval), "=&r" (ret)					\
	: "r" (uaddr), "i" (-EFAULT), "r" (oparg)			\
	);								\
})

static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	int oldval = 0, ret;

	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("or %1,%4,%4;", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("add %1,%0,%4;", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("or %1,%0,%4;", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("andn %1,%0,%4;", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xor %1,%0,%4;", ret, oldval, uaddr, oparg);
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

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	__asm__ __volatile__ ("1:	lwx	%1, %3, r0;		\
					cmp	%2, %1, %4;		\
					bnei	%2, 3f;			\
				2:	swx	%5, %3, r0;		\
					addic	%2, r0, 0;		\
					bnei	%2, 1b;			\
				3:					\
				.section .fixup,\"ax\";			\
				4:	brid	3b;			\
					addik	%0, r0, %6;		\
				.previous;				\
				.section __ex_table,\"a\";		\
				.word	1b,4b,2b,4b;			\
				.previous;"				\
		: "+r" (ret), "=&r" (prev), "=&r"(cmp)	\
		: "r" (uaddr), "r" (oldval), "r" (newval), "i" (-EFAULT));

	*uval = prev;
	return ret;
}

#endif /* __KERNEL__ */

#endif
