/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __CPUIDLE_PSCI_H
#define __CPUIDLE_PSCI_H

struct device;
struct device_node;

void psci_set_domain_state(u32 state);
int psci_dt_parse_state_node(struct device_node *np, u32 *state);

#ifdef CONFIG_ARM_PSCI_CPUIDLE_DOMAIN
struct device *psci_dt_attach_cpu(int cpu);
void psci_dt_detach_cpu(struct device *dev);
#else
static inline struct device *psci_dt_attach_cpu(int cpu) { return NULL; }
static inline void psci_dt_detach_cpu(struct device *dev) { }
#endif

#endif /* __CPUIDLE_PSCI_H */
