// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARM64 CPU idle arch support
 *
 * Copyright (C) 2014 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 */

#include <linux/acpi.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/psci.h>

#ifdef CONFIG_ACPI_PROCESSOR_IDLE

#include <acpi/processor.h>

#define ARM64_LPI_IS_RETENTION_STATE(arch_flags) (!(arch_flags))

static int psci_acpi_cpu_init_idle(unsigned int cpu)
{
	int i, count;
	struct acpi_lpi_state *lpi;
	struct acpi_processor *pr = per_cpu(processors, cpu);

	if (unlikely(!pr || !pr->flags.has_lpi))
		return -EINVAL;

	/*
	 * If the PSCI cpu_suspend function hook has not been initialized
	 * idle states must not be enabled, so bail out
	 */
	if (!psci_ops.cpu_suspend)
		return -EOPNOTSUPP;

	count = pr->power.count - 1;
	if (count <= 0)
		return -ENODEV;

	for (i = 0; i < count; i++) {
		u32 state;

		lpi = &pr->power.lpi_states[i + 1];
		/*
		 * Only bits[31:0] represent a PSCI power_state while
		 * bits[63:32] must be 0x0 as per ARM ACPI FFH Specification
		 */
		state = lpi->address;
		if (!psci_power_state_is_valid(state)) {
			pr_warn("Invalid PSCI power state %#x\n", state);
			return -EINVAL;
		}
	}

	return 0;
}

int acpi_processor_ffh_lpi_probe(unsigned int cpu)
{
	return psci_acpi_cpu_init_idle(cpu);
}

__cpuidle int acpi_processor_ffh_lpi_enter(struct acpi_lpi_state *lpi)
{
	u32 state = lpi->address;

	if (ARM64_LPI_IS_RETENTION_STATE(lpi->arch_flags))
		return CPU_PM_CPU_IDLE_ENTER_RETENTION_PARAM_RCU(psci_cpu_suspend_enter,
						lpi->index, state);
	else
		return CPU_PM_CPU_IDLE_ENTER_PARAM_RCU(psci_cpu_suspend_enter,
					     lpi->index, state);
}
#endif
