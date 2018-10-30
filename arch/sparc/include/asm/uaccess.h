/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ___ASM_SPARC_UACCESS_H
#define ___ASM_SPARC_UACCESS_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/uaccess_64.h>
#else
#include <asm/uaccess_32.h>
#endif

#define user_addr_max() \
	(uaccess_kernel() ? ~0UL : TASK_SIZE)

long strncpy_from_user(char *dest, const char __user *src, long count);

#endif
