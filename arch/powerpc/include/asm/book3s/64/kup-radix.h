/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_KUP_RADIX_H
#define _ASM_POWERPC_BOOK3S_64_KUP_RADIX_H

DECLARE_STATIC_KEY_FALSE(uaccess_flush_key);

/* Prototype for function defined in exceptions-64s.S */
void do_uaccess_flush(void);

static __always_inline void allow_user_access(void __user *to, const void __user *from,
					      unsigned long size)
{
}

static inline void prevent_user_access(void __user *to, const void __user *from,
				       unsigned long size)
{
	if (static_branch_unlikely(&uaccess_flush_key))
		do_uaccess_flush();
}

#endif /* _ASM_POWERPC_BOOK3S_64_KUP_RADIX_H */
