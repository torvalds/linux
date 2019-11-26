/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DOUBLEFAULT_H
#define _ASM_X86_DOUBLEFAULT_H

#if defined(CONFIG_X86_32) && defined(CONFIG_DOUBLEFAULT)
extern void doublefault_init_cpu_tss(void);
#else
static inline void doublefault_init_cpu_tss(void)
{
}
#endif

#endif /* _ASM_X86_DOUBLEFAULT_H */
