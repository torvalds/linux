/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_NMI_H
#define _ASM_NMI_H

#ifdef CONFIG_PPC_WATCHDOG
extern void arch_touch_nmi_watchdog(void);
long soft_nmi_interrupt(struct pt_regs *regs);
void watchdog_nmi_set_timeout_pct(u64 pct);
#else
static inline void arch_touch_nmi_watchdog(void) {}
static inline void watchdog_nmi_set_timeout_pct(u64 pct) {}
#endif

#ifdef CONFIG_NMI_IPI
extern void arch_trigger_cpumask_backtrace(const cpumask_t *mask,
					   bool exclude_self);
#define arch_trigger_cpumask_backtrace arch_trigger_cpumask_backtrace
#endif

extern void hv_nmi_check_nonrecoverable(struct pt_regs *regs);

#endif /* _ASM_NMI_H */
