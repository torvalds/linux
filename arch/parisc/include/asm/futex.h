/* SPDX-License-Identifier: GPL-2.0 */
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
	int oldval, ret;
	u32 tmp;

	_futex_spin_lock_irqsave(uaddr, &flags);

	ret = -EFAULT;
	if (unlikely(get_user(oldval, uaddr) != 0))
		goto out_pagefault_enable;

	ret = 0;
	tmp = oldval;

	switch (op) {
	case FUTEX_OP_SET:
		tmp = oparg;
		break;
	case FUTEX_OP_ADD:
		tmp += oparg;
		break;
	case FUTEX_OP_OR:
		tmp |= oparg;
		break;
	case FUTEX_OP_ANDN:
		tmp &= ~oparg;
		break;
	case FUTEX_OP_XOR:
		tmp ^= oparg;
		break;
	default:
		ret = -ENOSYS;
	}

	if (ret == 0 && unlikely(put_user(tmp, uaddr) != 0))
		ret = -EFAULT;

out_pagefault_enable:
	_futex_spin_unlock_irqrestore(uaddr, &flags);

	if (!ret)
		*oval = oldval;

	return ret;
}

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	u32 val;
	unsigned long flags;

	/* futex.c wants to do a cmpxchg_inatomic on kernel NULL, which is
	 * our gateway page, and causes no end of trouble...
	 */
	if (uaccess_kernel() && !uaddr)
		return -EFAULT;

	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;

	/* HPPA has no cmpxchg in hardware and therefore the
	 * best we can do here is use an array of locks. The
	 * lock selected is based on a hash of the userspace
	 * address. This should scale to a couple of CPUs.
	 */

	_futex_spin_lock_irqsave(uaddr, &flags);
	if (unlikely(get_user(val, uaddr) != 0)) {
		_futex_spin_unlock_irqrestore(uaddr, &flags);
		return -EFAULT;
	}

	if (val == oldval && unlikely(put_user(newval, uaddr) != 0)) {
		_futex_spin_unlock_irqrestore(uaddr, &flags);
		return -EFAULT;
	}

	*uval = val;
	_futex_spin_unlock_irqrestore(uaddr, &flags);

	return 0;
}

#endif /*__KERNEL__*/
#endif /*_ASM_PARISC_FUTEX_H*/
