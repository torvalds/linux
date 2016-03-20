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

extern const struct smp_operations psci_smp_ops;

#if defined(CONFIG_SMP) && defined(CONFIG_ARM_PSCI)
bool psci_smp_available(void);
#else
static inline bool psci_smp_available(void) { return false; }
#endif

#endif /* __ASM_ARM_PSCI_H */
