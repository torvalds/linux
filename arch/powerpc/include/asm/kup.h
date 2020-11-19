/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_KUP_H_
#define _ASM_POWERPC_KUP_H_

#ifndef __ASSEMBLY__

#include <asm/pgtable.h>

static inline void allow_user_access(void __user *to, const void __user *from,
				     unsigned long size) { }
static inline void prevent_user_access(void __user *to, const void __user *from,
				       unsigned long size) { }

static inline void allow_read_from_user(const void __user *from, unsigned long size)
{
	allow_user_access(NULL, from, size);
}

static inline void allow_write_to_user(void __user *to, unsigned long size)
{
	allow_user_access(to, NULL, size);
}

static inline void prevent_read_from_user(const void __user *from, unsigned long size)
{
	prevent_user_access(NULL, from, size);
}

static inline void prevent_write_to_user(void __user *to, unsigned long size)
{
	prevent_user_access(to, NULL, size);
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_KUP_H_ */
