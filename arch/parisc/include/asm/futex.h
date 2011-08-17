#ifndef _ASM_PARISC_FUTEX_H
#define _ASM_PARISC_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/atomic.h>
#include <asm/errno.h>

static inline int
futex_atomic_op_inuser (int encoded_op, u32 __user *uaddr)
{
	unsigned long int flags;
	u32 val;
	int op = (encoded_op >> 28) & 7;
	int cmp = (encoded_op >> 24) & 15;
	int oparg = (encoded_op << 8) >> 20;
	int cmparg = (encoded_op << 20) >> 20;
	int oldval = 0, ret;
	if (encoded_op & (FUTEX_OP_OPARG_SHIFT << 28))
		oparg = 1 << oparg;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(*uaddr)))
		return -EFAULT;

	pagefault_disable();

	_atomic_spin_lock_irqsave(uaddr, flags);

	switch (op) {
	case FUTEX_OP_SET:
		/* *(int *)UADDR2 = OPARG; */
		ret = get_user(oldval, uaddr);
		if (!ret)
			ret = put_user(oparg, uaddr);
		break;
	case FUTEX_OP_ADD:
		/* *(int *)UADDR2 += OPARG; */
		ret = get_user(oldval, uaddr);
		if (!ret) {
			val = oldval + oparg;
			ret = put_user(val, uaddr);
		}
		break;
	case FUTEX_OP_OR:
		/* *(int *)UADDR2 |= OPARG; */
		ret = get_user(oldval, uaddr);
		if (!ret) {
			val = oldval | oparg;
			ret = put_user(val, uaddr);
		}
		break;
	case FUTEX_OP_ANDN:
		/* *(int *)UADDR2 &= ~OPARG; */
		ret = get_user(oldval, uaddr);
		if (!ret) {
			val = oldval & ~oparg;
			ret = put_user(val, uaddr);
		}
		break;
	case FUTEX_OP_XOR:
		/* *(int *)UADDR2 ^= OPARG; */
		ret = get_user(oldval, uaddr);
		if (!ret) {
			val = oldval ^ oparg;
			ret = put_user(val, uaddr);
		}
		break;
	default:
		ret = -ENOSYS;
	}

	_atomic_spin_unlock_irqrestore(uaddr, flags);

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

/* Non-atomic version */
static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	int ret;
	u32 val;
	unsigned long flags;

	/* futex.c wants to do a cmpxchg_inatomic on kernel NULL, which is
	 * our gateway page, and causes no end of trouble...
	 */
	if (segment_eq(KERNEL_DS, get_fs()) && !uaddr)
		return -EFAULT;

	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	/* HPPA has no cmpxchg in hardware and therefore the
	 * best we can do here is use an array of locks. The
	 * lock selected is based on a hash of the userspace
	 * address. This should scale to a couple of CPUs.
	 */

	_atomic_spin_lock_irqsave(uaddr, flags);

	ret = get_user(val, uaddr);

	if (!ret && val == oldval)
		ret = put_user(newval, uaddr);

	*uval = val;

	_atomic_spin_unlock_irqrestore(uaddr, flags);

	return ret;
}

#endif /*__KERNEL__*/
#endif /*_ASM_PARISC_FUTEX_H*/
