/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_THERMAL_INTERRUPT_H
#define _INTEL_THERMAL_INTERRUPT_H

/* Interrupt Handler for package thermal thresholds */
extern int (*platform_thermal_package_notify)(__u64 msr_val);

/* Interrupt Handler for core thermal thresholds */
extern int (*platform_thermal_notify)(__u64 msr_val);

/* Callback support of rate control, return true, if
 * callback has rate control */
extern bool (*platform_thermal_package_rate_control)(void);

#endif /* _INTEL_THERMAL_INTERRUPT_H */
