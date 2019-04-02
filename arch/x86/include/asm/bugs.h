/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_S_H
#define _ASM_X86_S_H

#include <asm/processor.h>

extern void check_s(void);

#if defined(CONFIG_CPU_SUP_INTEL)
void check_mpx_erratum(struct cpuinfo_x86 *c);
#else
static inline void check_mpx_erratum(struct cpuinfo_x86 *c) {}
#endif

#if defined(CONFIG_CPU_SUP_INTEL) && defined(CONFIG_X86_32)
int ppro_with_ram_(void);
#else
static inline int ppro_with_ram_(void) { return 0; }
#endif

#endif /* _ASM_X86_S_H */
