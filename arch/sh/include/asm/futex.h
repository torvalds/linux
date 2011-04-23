#ifndef __ASM_SH_FUTEX_H
#define __ASM_SH_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

/* XXX: UP variants, fix for SH-4A and SMP.. */
#include <asm/futex-irq.h>

static inline int futex_atomic_op_inuser(int encoded_op, u32 __user *uaddr)
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
		ret = atomic_futex_op_xchg_set(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_ADD:
		ret = atomic_futex_op_xchg_add(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_OR:
		ret = atomic_futex_op_xchg_or(oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_ANDN:
		ret = atomic_futex_op_xchg_and(~oparg, uaddr, &oldval);
		break;
	case FUTEX_OP_XOR:
		ret = atomic_futex_op_xchg_xor(oparg, uaddr, &oldval);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	pagefault_enable();

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
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	return atomic_futex_op_cmpxchg_inatomic(uval, uaddr, oldval, newval);
}

#endif /* __KERNEL__ */
#endif /* __ASM_SH_FUTEX_H */
