/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_NMI_H
#define __ASM_NMI_H

#include <linux/cpumask.h>

/*
 * Cross-CPU NMI provider hooks, consulted by the arm64 arch code before
 * its regular-IRQ / pseudo-NMI IPI paths. The SDEI provider in
 * drivers/firmware/arm_sdei_nmi.c implements them when active; a future
 * FEAT_NMI provider could slot in here too. The stubs let callers stay
 * unconditional when ARM_SDEI_NMI is off.
 */
#ifdef CONFIG_ARM_SDEI_NMI
bool sdei_nmi_trigger_cpumask_backtrace(const cpumask_t *mask, int exclude_cpu);
#else
static inline bool sdei_nmi_trigger_cpumask_backtrace(const cpumask_t *mask,
						      int exclude_cpu)
{
	return false;
}
#endif

#endif /* __ASM_NMI_H */
