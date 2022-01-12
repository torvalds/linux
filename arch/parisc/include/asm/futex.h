/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_FUTEX_H
#define _ASM_PARISC_FUTEX_H

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/atomic.h>
#include <asm/errno.h>

/* The following has to match the LWS code in syscall.S.  We have
 * 256 four-word locks. We use bits 20-27 of the futex virtual
 * address for the hash index.
 */

static inline unsigned long _futex_hash_index(unsigned long ua)
{
	return (ua >> 2) & 0x3fc;
}

static inline void
_futex_spin_lock_irqsave(arch_spinlock_t *s, unsigned long *flags)
{
	local_irq_save(*flags);
	arch_spin_lock(s);
}

static inline void
_futex_spin_unlock_irqrestore(arch_spinlock_t *s, unsigned long *flags)
{
	arch_spin_unlock(s);
	local_irq_restore(*flags);
}

static inline int
arch_futex_atomic_op_inuser(int op, int oparg, int *oval, u32 __user *uaddr)
{
	extern u32 lws_lock_start[];
	unsigned long ua = (unsigned long)uaddr;
	arch_spinlock_t *s;
	unsigned long flags;
	int oldval, ret;
	u32 tmp;

	s = (arch_spinlock_t *)&lws_lock_start[_futex_hash_index(ua)];
	_futex_spin_lock_irqsave(s, &flags);

	/* Return -EFAULT if we encounter a page fault or COW break */
	if (unlikely(get_user(oldval, uaddr) != 0)) {
		ret = -EFAULT;
		goto out_pagefault_enable;
	}

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
		goto out_pagefault_enable;
	}

	if (unlikely(put_user(tmp, uaddr) != 0))
		ret = -EFAULT;

out_pagefault_enable:
	_futex_spin_unlock_irqrestore(s, &flags);

	if (!ret)
		*oval = oldval;

	return ret;
}

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	extern u32 lws_lock_start[];
	unsigned long ua = (unsigned long)uaddr;
	arch_spinlock_t *s;
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
	 * lock selected is based on a hash of the virtual
	 * address of the futex. This should scale to a couple
	 * of CPUs.
	 */

	s = (arch_spinlock_t *)&lws_lock_start[_futex_hash_index(ua)];
	_futex_spin_lock_irqsave(s, &flags);
	if (unlikely(get_user(val, uaddr) != 0)) {
		_futex_spin_unlock_irqrestore(s, &flags);
		return -EFAULT;
	}

	if (val == oldval && unlikely(put_user(newval, uaddr) != 0)) {
		_futex_spin_unlock_irqrestore(s, &flags);
		return -EFAULT;
	}

	*uval = val;
	_futex_spin_unlock_irqrestore(s, &flags);

	return 0;
}

#endif /*_ASM_PARISC_FUTEX_H*/
