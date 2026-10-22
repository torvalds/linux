/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UM_FUTEX_H
#define _ASM_UM_FUTEX_H

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>


#ifdef CONFIG_MMU
int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval, u32 __user *uaddr);
int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval);
#else
#include <linux/atomic.h>
#include <asm-generic/futex.h>

#ifdef CONFIG_SMP
static inline int
arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval, u32 __user *uaddr)
{
	u32 oldval, newval;
	u32 *p = (u32 __force *)uaddr;

	do {
		oldval = READ_ONCE(*p);

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
			return -ENOSYS;
		}
	} while (!try_cmpxchg(p, &oldval, newval));

	*oval = oldval;
	return 0;
}

static inline int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval)
{
	u32 *p = (u32 __force *)uaddr;

	*uval = cmpxchg(p, oldval, newval);

	return 0;
}
#endif /* CONFIG_SMP */

#endif /* CONFIG_MMU */

#endif
