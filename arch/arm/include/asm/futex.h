#ifndef _ASM_ARM_FUTEX_H
#define _ASM_ARM_FUTEX_H

#ifdef __KERNEL__

#ifdef CONFIG_SMP

#include <asm-generic/futex.h>

#else /* !SMP, we can work around lack of atomic ops by disabling preemption */

#include <linux/futex.h>
#include <linux/preempt.h>
#include <linux/uaccess.h>
#include <asm/errno.h>
#include <asm/domain.h>

#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg)	\
	__asm__ __volatile__(					\
	"1:	" T(ldr) "	%1, [%2]\n"			\
	"	" insn "\n"					\
	"2:	" T(str) "	%0, [%2]\n"			\
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
futex_atomic_op_inuser (int encoded_op, int __user *uaddr)
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
		__futex_atomic_op("orr	%0, %1, %3", ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("and	%0, %1, %3", ret, oldval, uaddr, ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("eor	%0, %1, %3", ret, oldval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	pagefault_enable();	/* subsumes preempt_enable() */

	if (!ret) {
		switch (cmp) {
		case FUTEX_OP_CMP_EQ: ret = (oldval == cmparg); break;
		case FUTEX_OP_CMP_NE: ret = (oldval != cmparg); break;
		case FUTEX_OP_CMP_LT: ret = (oldval < cmparg); break;
		case FUTEX_OP_CMP_GE: ret = (oldval >= cmparg); break;
		case FUTEX_OP_CMP_LE: ret = (oldval <= cmparg); break;
		case FUTEX_OP_CMP_GT: ret = (oldval > cmparg); break;
		default: ret = -ENOSYS;
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
	"1:	" T(ldr) "	%0, [%3]\n"
	"	teq	%0, %1\n"
	"	it	eq	@ explicit IT needed for the 2b label\n"
	"2:	" T(streq) "	%2, [%3]\n"
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

#endif /* !SMP */

#endif /* __KERNEL__ */
#endif /* _ASM_ARM_FUTEX_H */
