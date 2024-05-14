/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_BUGS_H
#define _ASM_X86_BUGS_H

#include <asm/processor.h>

#if defined(CONFIG_CPU_SUP_INTEL) && defined(CONFIG_X86_32)
int ppro_with_ram_bug(void);
#else
static inline int ppro_with_ram_bug(void) { return 0; }
#endif

extern void cpu_bugs_smt_update(void);

#endif /* _ASM_X86_BUGS_H */
