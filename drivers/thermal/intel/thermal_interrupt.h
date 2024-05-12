/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_THERMAL_INTERRUPT_H
#define _INTEL_THERMAL_INTERRUPT_H

#define CORE_LEVEL	0
#define PACKAGE_LEVEL	1

/* Interrupt Handler for package thermal thresholds */
extern int (*platform_thermal_package_notify)(__u64 msr_val);

/* Interrupt Handler for core thermal thresholds */
extern int (*platform_thermal_notify)(__u64 msr_val);

/* Callback support of rate control, return true, if
 * callback has rate control */
extern bool (*platform_thermal_package_rate_control)(void);

/* Handle HWP interrupt */
extern void notify_hwp_interrupt(void);

/* Common function to clear Package thermal status register */
extern void thermal_clear_package_intr_status(int level, u64 bit_mask);

#endif /* _INTEL_THERMAL_INTERRUPT_H */
