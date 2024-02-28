/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __CPUIDLE_PSCI_H
#define __CPUIDLE_PSCI_H

struct device_node;

void psci_set_domain_state(u32 state);
int psci_dt_parse_state_node(struct device_node *np, u32 *state);

#endif /* __CPUIDLE_PSCI_H */
