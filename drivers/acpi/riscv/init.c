// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2024, Ventana Micro Systems Inc
 *	Author: Sunil V L <sunilvl@ventanamicro.com>
 */

#include <linux/acpi.h>
#include "init.h"

void __init acpi_arch_init(void)
{
	riscv_acpi_init_gsi_mapping();
}
