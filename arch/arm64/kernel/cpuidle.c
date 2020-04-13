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

#include <asm/cpuidle.h>
#include <asm/cpu_ops.h>

int arm_cpuidle_init(unsigned int cpu)
{
	const struct cpu_operations *ops = get_cpu_ops(cpu);
	int ret = -EOPNOTSUPP;

	if (ops && ops->cpu_suspend && ops->cpu_init_idle)
		ret = ops->cpu_init_idle(cpu);

	return ret;
}

/**
 * arm_cpuidle_suspend() - function to enter a low-power idle state
 * @arg: argument to pass to CPU suspend operations
 *
 * Return: 0 on success, -EOPNOTSUPP if CPU suspend hook not initialized, CPU
 * operations back-end error code otherwise.
 */
int arm_cpuidle_suspend(int index)
{
	int cpu = smp_processor_id();
	const struct cpu_operations *ops = get_cpu_ops(cpu);

	return ops->cpu_suspend(index);
}

#ifdef CONFIG_ACPI

#include <acpi/processor.h>

#define ARM64_LPI_IS_RETENTION_STATE(arch_flags) (!(arch_flags))

static int psci_acpi_cpu_init_idle(unsigned int cpu)
{
	int i, count;
	struct acpi_lpi_state *lpi;
	struct acpi_processor *pr = per_cpu(processors, cpu);

	/*
	 * If the PSCI cpu_suspend function hook has not been initialized
	 * idle states must not be enabled, so bail out
	 */
	if (!psci_ops.cpu_suspend)
		return -EOPNOTSUPP;

	if (unlikely(!pr || !pr->flags.has_lpi))
		return -EINVAL;

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

int acpi_processor_ffh_lpi_enter(struct acpi_lpi_state *lpi)
{
	u32 state = lpi->address;

	if (ARM64_LPI_IS_RETENTION_STATE(lpi->arch_flags))
		return CPU_PM_CPU_IDLE_ENTER_RETENTION_PARAM(psci_cpu_suspend_enter,
						lpi->index, state);
	else
		return CPU_PM_CPU_IDLE_ENTER_PARAM(psci_cpu_suspend_enter,
					     lpi->index, state);
}
#endif
