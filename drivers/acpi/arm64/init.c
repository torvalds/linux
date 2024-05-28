// SPDX-License-Identifier: GPL-2.0-only
#include <linux/acpi.h>
#include "init.h"

void __init acpi_arm_init(void)
{
	if (IS_ENABLED(CONFIG_ACPI_AGDI))
		acpi_agdi_init();
	if (IS_ENABLED(CONFIG_ACPI_APMT))
		acpi_apmt_init();
	if (IS_ENABLED(CONFIG_ACPI_IORT))
		acpi_iort_init();
	if (IS_ENABLED(CONFIG_ARM_AMBA))
		acpi_amba_init();
}
