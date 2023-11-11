/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ESPFIX_H
#define _ASM_X86_ESPFIX_H

#ifdef CONFIG_X86_ESPFIX64

#include <asm/percpu.h>

DECLARE_PER_CPU_READ_MOSTLY(unsigned long, espfix_stack);
DECLARE_PER_CPU_READ_MOSTLY(unsigned long, espfix_waddr);

extern void init_espfix_bsp(void);
extern void init_espfix_ap(int cpu);
#else
static inline void init_espfix_ap(int cpu) { }
#endif

#endif /* _ASM_X86_ESPFIX_H */
