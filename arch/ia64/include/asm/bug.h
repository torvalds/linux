/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64__H
#define _ASM_IA64__H

#ifdef CONFIG_
#define ia64_abort()	__builtin_trap()
#define () do {						\
	printk("kernel  at %s:%d!\n", __FILE__, __LINE__);	\
	barrier_before_unreachable();				\
	ia64_abort();						\
} while (0)

/* should this  be made generic? */
#define HAVE_ARCH_
#endif

#include <asm-generic/.h>

#endif
