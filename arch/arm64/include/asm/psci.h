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
 * Copyright (C) 2013 ARM Limited
 */

#ifndef __ASM_PSCI_H
#define __ASM_PSCI_H

int __init psci_dt_init(void);

#ifdef CONFIG_ACPI
int __init psci_acpi_init(void);
bool __init acpi_psci_present(void);
bool __init acpi_psci_use_hvc(void);
#else
static inline int psci_acpi_init(void) { return 0; }
static inline bool acpi_psci_present(void) { return false; }
#endif

#endif /* __ASM_PSCI_H */
