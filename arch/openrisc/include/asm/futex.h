#ifndef __ASM_OPENRISC_FUTEX_H
#define __ASM_OPENRISC_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg) \
({								\
	__asm__ __volatile__ (					\
		"1:	l.lwa	%0, %2			\n"	\
			insn				"\n"	\
		"2:	l.swa	%2, %1			\n"	\
		"	l.bnf	1b			\n"	\
		"	 l.ori	%1, r0, 0		\n"	\
		"3:					\n"	\
		".section .fixup,\"ax\"			\n"	\
		"4:	l.j	3b			\n"	\
		"	 l.addi	%1, r0, %3		\n"	\
		".previous				\n"	\
		".section __ex_table,\"a\"		\n"	\
		".word	1b,4b,2b,4b			\n"	\
		".previous				\n"	\
		: "=&r" (oldval), "=&r" (ret), "+m" (*uaddr)	\
		: "i" (-EFAULT), "r" (oparg)			\
		: "cc", "memory"				\
		);						\
})

static inline int
futex_atomic_op_inuser(int encoded_op, u32 __user *uaddr)
{
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int oldval = 0, ret;

	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	pagefault_disable();

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("l.or %1,%4,%4", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("l.add %1,%0,%4", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("l.or %1,%0,%4", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("l.and %1,%0,%4", ret, oldval, uaddr, ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("l.xor %1,%0,%4", ret, oldval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	pagefault_enable();

	if (!ret) {
		switch (cmp) {
		case FUTEX_OP_CMP_EQ:
			ret = (oldval == cmparg);
			break;
		case FUTEX_OP_CMP_NE:
			ret = (oldval != cmparg);
			break;
		case FUTEX_OP_CMP_LT:
			ret = (oldval < cmparg);
			break;
		case FUTEX_OP_CMP_GE:
			ret = (oldval >= cmparg);
			break;
		case FUTEX_OP_CMP_LE:
			ret = (oldval <= cmparg);
			break;
		case FUTEX_OP_CMP_GT:
			ret = (oldval > cmparg);
			break;
		default:
			ret = -ENOSYS;
		}
	}
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

	__asm__ __volatile__ (				\
		"1:	l.lwa	%1, %2		\n"	\
		"	l.sfeq	%1, %3		\n"	\
		"	l.bnf	3f		\n"	\
		"	 l.nop			\n"	\
		"2:	l.swa	%2, %4		\n"	\
		"	l.bnf	1b		\n"	\
		"	 l.nop			\n"	\
		"3:				\n"	\
		".section .fixup,\"ax\"		\n"	\
		"4:	l.j	3b		\n"	\
		"	 l.addi	%0, r0, %5	\n"	\
		".previous			\n"	\
		".section __ex_table,\"a\"	\n"	\
		".word	1b,4b,2b,4b		\n"	\
		".previous			\n"	\
		: "+r" (ret), "=&r" (prev), "+m" (*uaddr) \
		: "r" (oldval), "r" (newval), "i" (-EFAULT) \
		: "cc",	"memory"			\
		);

	*uval = prev;
	return ret;
}

#endif /* __KERNEL__ */

#endif /* __ASM_OPENRISC_FUTEX_H */
