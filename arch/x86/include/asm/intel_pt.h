/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_INTEL_PT_H
#define _ASM_X86_INTEL_PT_H

#if defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_INTEL)
void cpu_emergency_stop_pt(void);
#else
static inline void cpu_emergency_stop_pt(void) {}
#endif

#endif /* _ASM_X86_INTEL_PT_H */
