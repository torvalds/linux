/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_FUTEX_H
#define __ASM_SH_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>

#if !defined(CONFIG_SMP)
#include <asm/futex-irq.h>
#elif defined(CONFIG_CPU_J2)
#include <asm/futex-cas.h>
#elif defined(CONFIG_CPU_SH4A)
#include <asm/futex-llsc.h>
#else
#error SMP not supported on this configuration.
#endif

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	if (!access_ok(VERIFY_WRITE, uaddr, sizeof(u32)))
		return -EFAULT;

	return atomic_futex_op_cmpxchg_inatomic(uval, uaddr, oldval, newval);
}

static inline int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval,
		u32 __user *uaddr)
{
	u32 oldval, newval, prev;
	int ret;

	pagefault_disable();

	do {
		ret = get_user(oldval, uaddr);

		if (ret) break;

		switch (op) {
		case FUTEX_OP_SET:
			newval = oparg;
			break;
		case FUTEX_OP_ADD:
			newval = oldval + oparg;
			break;
		case FUTEX_OP_OR:
			newval = oldval | oparg;
			break;
		case FUTEX_OP_ANDN:
			newval = oldval & ~oparg;
			break;
		case FUTEX_OP_XOR:
			newval = oldval ^ oparg;
			break;
		default:
			ret = -ENOSYS;
			break;
		}

		if (ret) break;

		ret = futex_atomic_cmpxchg_inatomic(&prev, uaddr, oldval, newval);
	} while (!ret && prev != oldval);

	pagefault_enable();

	if (!ret)
		*oval = oldval;

	return ret;
}

#endif /* __KERNEL__ */
#endif /* __ASM_SH_FUTEX_H */
