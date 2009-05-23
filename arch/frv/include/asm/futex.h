#ifndef _ASM_FUTEX_H
#define _ASM_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <asm/errno.h>
#include <asm/uaccess.h>

extern int futex_atomic_op_inuser(int encoded_op, int __user *uaddr);

static inline int
futex_atomic_cmpxchg_inatomic(int __user *uaddr, int oldval, int newval)
{
	return -ENOSYS;
}

#endif
#endif
