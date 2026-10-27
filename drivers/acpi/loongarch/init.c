// SPDX-License-Identifier: GPL-2.0-only

#include <linux/acpi.h>
#include "init.h"

void __init acpi_arch_init(void)
{
	if (IS_ENABLED(CONFIG_ACPI_IOVT))
		acpi_iovt_init();
}

void __init acpi_arch_late_init(void)
{
	if (IS_ENABLED(CONFIG_ACPI_IOVT))
		acpi_iovt_late_init();
}
