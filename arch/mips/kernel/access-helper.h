/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/uaccess.h>

static inline int __get_addr(unsigned long *a, unsigned long *p, bool user)
{
	return user ? get_user(*a, (unsigned long __user *)p) :
		      get_kernel_nofault(*a, p);
}

static inline int __get_inst16(u16 *i, u16 *p, bool user)
{
	return user ? get_user(*i, (u16 __user *)p) : get_kernel_nofault(*i, p);
}

static inline int __get_inst32(u32 *i, u32 *p, bool user)
{
	return user ? get_user(*i, (u32 __user *)p) : get_kernel_nofault(*i, p);
}
