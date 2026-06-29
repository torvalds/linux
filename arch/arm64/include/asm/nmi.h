/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_NMI_H
#define __ASM_NMI_H

#include <linux/cpumask.h>

struct pt_regs;

/*
 * Cross-CPU NMI provider hooks, consulted by the arm64 arch code before
 * its regular-IRQ / pseudo-NMI IPI paths. The SDEI provider in
 * drivers/firmware/arm_sdei_nmi.c implements them when active; a future
 * FEAT_NMI provider could slot in here too. The stubs let callers stay
 * unconditional when ARM_SDEI_NMI is off.
 *
 * sdei_nmi_active() lets a caller test for the service before committing
 * to (and waiting on) the SDEI stop rung; sdei_nmi_stop_cpus() then signals
 * the targets, which ack by going offline.
 */
#ifdef CONFIG_ARM_SDEI_NMI
bool sdei_nmi_trigger_cpumask_backtrace(const cpumask_t *mask, int exclude_cpu);
bool sdei_nmi_active(void);
void sdei_nmi_stop_cpus(const cpumask_t *mask);
#else
static inline bool sdei_nmi_trigger_cpumask_backtrace(const cpumask_t *mask,
						      int exclude_cpu)
{
	return false;
}

static inline bool sdei_nmi_active(void)
{
	return false;
}

static inline void sdei_nmi_stop_cpus(const cpumask_t *mask) { }
#endif

/*
 * The common "stop this CPU" entry every arm64 stop path funnels through:
 * the regular/pseudo-NMI stop IPI handlers, panic_smp_self_stop(), and the
 * SDEI cross-CPU NMI handler. @die_on_crash powers the CPU off on the kdump
 * crash path (IPI handlers) instead of parking it (SDEI / self-stop).
 * Defined in arch/arm64/kernel/smp.c.
 */
void __noreturn arm64_nmi_cpu_stop(struct pt_regs *regs, bool die_on_crash);

#endif /* __ASM_NMI_H */
