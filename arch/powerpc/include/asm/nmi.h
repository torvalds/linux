#ifndef _ASM_NMI_H
#define _ASM_NMI_H

#ifdef CONFIG_HARDLOCKUP_DETECTOR
extern void arch_touch_nmi_watchdog(void);

extern void arch_trigger_cpumask_backtrace(const cpumask_t *mask,
					   bool exclude_self);
#define arch_trigger_cpumask_backtrace arch_trigger_cpumask_backtrace

#else
static inline void arch_touch_nmi_watchdog(void) {}
#endif

#endif /* _ASM_NMI_H */
