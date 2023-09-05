/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DOUBLEFAULT_H
#define _ASM_X86_DOUBLEFAULT_H

#include <linux/linkage.h>

#ifdef CONFIG_X86_32
extern void doublefault_init_cpu_tss(void);
#else
static inline void doublefault_init_cpu_tss(void)
{
}
#endif

asmlinkage void __noreturn doublefault_shim(void);

#endif /* _ASM_X86_DOUBLEFAULT_H */
