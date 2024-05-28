// SPDX-License-Identifier: GPL-2.0-only
#include <linux/acpi.h>
#include <linux/export.h>

#include "../internal.h"

#define SMCCC_SOC_ID_T241      0x036b0241

int acpi_arch_thermal_cpufreq_pctg(void)
{
	s32 soc_id = arm_smccc_get_soc_id_version();

	/*
	 * Check JEP106 code for NVIDIA Tegra241 chip (036b:0241) and
	 * reduce the CPUFREQ Thermal reduction percentage to 5%.
	 */
	if (soc_id == SMCCC_SOC_ID_T241)
		return 5;

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_arch_thermal_cpufreq_pctg);
