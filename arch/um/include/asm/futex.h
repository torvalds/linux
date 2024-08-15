/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UM_FUTEX_H
#define _ASM_UM_FUTEX_H

#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/errno.h>


int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval, u32 __user *uaddr);
int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
			      u32 oldval, u32 newval);

#endif
