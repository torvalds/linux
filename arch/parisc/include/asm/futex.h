#ifndef _ASM_PARISC_FUTEX_H
#define _ASM_PARISC_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/atomic.h>
#include <asm/errno.h>

/* The following has to match the LWS code in syscall.S.  We have
   sixteen four-word locks. */

static inline void
_futex_spin_lock_irqsave(u32 __user *uaddr, unsigned long int *flags)
{
	extern u32 lws_lock_start[];
	long index = ((long)uaddr & 0xf0) >> 2;
	arch_spinlock_t *s = (arch_spinlock_t *)&lws_lock_start[index];
	local_irq_save(*flags);
	arch_spin_lock(s);
}

static inline void
_futex_spin_unlock_irqrestore(u32 __user *uaddr, unsigned long int *flags)
{
	extern u32 lws_lock_start[];
	long index = ((long)uaddr & 0xf0) >> 2;
	arch_spinlock_t *s = (arch_spinlock_t *)&lws_lock_start[index];
	arch_spin_unlock(s);
	local_irq_restore(*flags);
}

static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	unsigned long int flags;
	u32 val;
	int oldval = 0, ret;

	pagefault_disable();

	_futex_spin_lock_irqsave(uaddr, &flags);

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

	_futex_spin_unlock_irqrestore(uaddr, &flags);

	pagefault_enable();

	if (!ret)
		*oval = oldval;

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

	_futex_spin_lock_irqsave(uaddr, &flags);

	ret = get_user(val, uaddr);

	if (!ret && val == oldval)
		ret = put_user(newval, uaddr);

	*uval = val;

	_futex_spin_unlock_irqrestore(uaddr, &flags);

	return ret;
}

#endif /*__KERNEL__*/
#endif /*_ASM_PARISC_FUTEX_H*/
