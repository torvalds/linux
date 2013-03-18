/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */

#ifndef __ASM_ARM_PSCI_H
#define __ASM_ARM_PSCI_H

#define PSCI_POWER_STATE_TYPE_STANDBY		0
#define PSCI_POWER_STATE_TYPE_POWER_DOWN	1

struct psci_power_state {
	u16	id;
	u8	type;
	u8	affinity_level;
};

struct psci_operations {
	int (*cpu_suspend)(struct psci_power_state state,
			   unsigned long entry_point);
	int (*cpu_off)(struct psci_power_state state);
	int (*cpu_on)(unsigned long cpuid, unsigned long entry_point);
	int (*migrate)(unsigned long cpuid);
};

extern struct psci_operations psci_ops;

#endif /* __ASM_ARM_PSCI_H */
