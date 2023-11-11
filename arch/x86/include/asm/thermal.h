/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_THERMAL_H
#define _ASM_X86_THERMAL_H

#ifdef CONFIG_X86_THERMAL_VECTOR
void therm_lvt_init(void);
void intel_init_thermal(struct cpuinfo_x86 *c);
bool x86_thermal_enabled(void);
void intel_thermal_interrupt(void);
#else
static inline void therm_lvt_init(void)				{ }
static inline void intel_init_thermal(struct cpuinfo_x86 *c)	{ }
#endif

#endif /* _ASM_X86_THERMAL_H */
