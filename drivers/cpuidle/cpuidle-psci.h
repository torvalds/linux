/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __CPUIDLE_PSCI_H
#define __CPUIDLE_PSCI_H

#ifdef CONFIG_PM_GENERIC_DOMAINS_OF
struct device __init *psci_dt_attach_cpu(int cpu);
#else
static inline struct device __init *psci_dt_attach_cpu(int cpu) { return NULL; }
#endif

#endif /* __CPUIDLE_PSCI_H */
