#ifndef _ASM_ARM_FUTEX_H
#define _ASM_ARM_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

#define __futex_atomic_ex_table(err_reg)			\
	"3:\n"							\
	"	.pushsection __ex_table,\"a\"\n"		\
	"	.align	3\n"					\
	"	.long	1b, 4f, 2b, 4f\n"			\
	"	.popsection\n"					\
	"	.pushsection .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"4:	mov	%0, " err_reg "\n"			\
	"	b	3b\n"					\
	"	.popsection"

#ifdef CONFIG_SMP

#define __futex_atomic_op(insn, ret, oldval, tmp, uaddr, oparg)	\
	smp_mb();						\
	__asm__ __volatile__(					\
	"1:	ldrex	%1, [%3]\n"				\
	"	" insn "\n"					\
	"2:	strex	%2, %0, [%3]\n"				\
	"	teq	%2, #0\n"				\
	"	bne	1b\n"					\
	"	mov	%0, #0\n"				\
	__futex_atomic_ex_table("%5")				\
	: "=&r" (ret), "=&r" (oldval), "=&r" (tmp)		\
	: "r" (uaddr), "r" (oparg), "Ir" (-EFAULT)		\
	: "cc", "memory")

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	int ret;
	u32 val;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	smp_mb();
	__asm__ __volatile__("@futex_atomic_cmpxchg_inatomic\n"
	"1:	ldrex	%1, [%4]\n"
	"	teq	%1, %2\n"
	"	ite	eq	@ explicit IT needed for the 2b label\n"
	"2:	strexeq	%0, %3, [%4]\n"
	"	movne	%0, #0\n"
	"	teq	%0, #0\n"
	"	bne	1b\n"
	__futex_atomic_ex_table("%5")
	: "=&r" (ret), "=&r" (val)
	: "r" (oldval), "r" (newval), "r" (uaddr), "Ir" (-EFAULT)
	: "cc", "memory");
	smp_mb();

	*uval = val;
	return ret;
}

#else /* !SMP, we can work around lack of atomic ops by disabling preemption */

#include <linux/preempt.h>
#include <asm/domain.h>

#define __futex_atomic_op(insn, ret, oldval, tmp, uaddr, oparg)	\
	__asm__ __volatile__(					\
	"1:	" TUSER(ldr) "	%1, [%3]\n"			\
	"	" insn "\n"					\
	"2:	" TUSER(str) "	%0, [%3]\n"			\
	"	mov	%0, #0\n"				\
	__futex_atomic_ex_table("%5")				\
	: "=&r" (ret), "=&r" (oldval), "=&r" (tmp)		\
	: "r" (uaddr), "r" (oparg), "Ir" (-EFAULT)		\
	: "cc", "memory")

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	int ret = 0;
	u32 val;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	__asm__ __volatile__("@futex_atomic_cmpxchg_inatomic\n"
	"1:	" TUSER(ldr) "	%1, [%4]\n"
	"	teq	%1, %2\n"
	"	it	eq	@ explicit IT needed for the 2b label\n"
	"2:	" TUSER(streq) "	%3, [%4]\n"
	__futex_atomic_ex_table("%5")
	: "+r" (ret), "=&r" (val)
	: "r" (oldval), "r" (newval), "r" (uaddr), "Ir" (-EFAULT)
	: "cc", "memory");

	*uval = val;
	return ret;
}

#endif /* !SMP */

static inline int
futex_atomic_op_inuser (int encoded_op, u32 __user *uaddr)
{
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int oldval = 0, ret, tmp;

	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	pagefault_disable();	/* implies preempt_disable() */

	switch (op) {
	case FUTEX_OP_SET:
		__futex_atomic_op("mov	%0, %4", ret, oldval, tmp, uaddr, oparg);
		break;
	case FUTEX_OP_ADD:
		__futex_atomic_op("add	%0, %1, %4", ret, oldval, tmp, uaddr, oparg);
		break;
	case FUTEX_OP_OR:
		__futex_atomic_op("orr	%0, %1, %4", ret, oldval, tmp, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN:
		__futex_atomic_op("and	%0, %1, %4", ret, oldval, tmp, uaddr, ~oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("eor	%0, %1, %4", ret, oldval, tmp, uaddr, oparg);
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

#endif /* __KERNEL__ */
#endif /* _ASM_ARM_FUTEX_H */
