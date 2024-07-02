// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024, Ventana Micro Systems Inc
 *	Author: Sunil V L <sunilvl@ventanamicro.com>
 *
 */

#include <linux/acpi.h>
#include <acpi/processor.h>
#include <linux/cpu_pm.h>
#include <linux/cpuidle.h>
#include <linux/suspend.h>
#include <asm/cpuidle.h>
#include <asm/sbi.h>
#include <asm/suspend.h>

#define RISCV_FFH_LPI_TYPE_MASK	GENMASK_ULL(63, 60)
#define RISCV_FFH_LPI_RSVD_MASK	GENMASK_ULL(59, 32)

#define RISCV_FFH_LPI_TYPE_SBI	BIT_ULL(60)

static int acpi_cpu_init_idle(unsigned int cpu)
{
	int i;
	struct acpi_lpi_state *lpi;
	struct acpi_processor *pr = per_cpu(processors, cpu);

	if (unlikely(!pr || !pr->flags.has_lpi))
		return -EINVAL;

	if (!riscv_sbi_hsm_is_supported())
		return -ENODEV;

	if (pr->power.count <= 1)
		return -ENODEV;

	for (i = 1; i < pr->power.count; i++) {
		u32 state;

		lpi = &pr->power.lpi_states[i];

		/*
		 * Validate Entry Method as per FFH spec.
		 * bits[63:60] should be 0x1
		 * bits[59:32] should be 0x0
		 * bits[31:0] represent a SBI power_state
		 */
		if (((lpi->address & RISCV_FFH_LPI_TYPE_MASK) != RISCV_FFH_LPI_TYPE_SBI) ||
		    (lpi->address & RISCV_FFH_LPI_RSVD_MASK)) {
			pr_warn("Invalid LPI entry method %#llx\n", lpi->address);
			return -EINVAL;
		}

		state = lpi->address;
		if (!riscv_sbi_suspend_state_is_valid(state)) {
			pr_warn("Invalid SBI power state %#x\n", state);
			return -EINVAL;
		}
	}

	return 0;
}

int acpi_processor_ffh_lpi_probe(unsigned int cpu)
{
	return acpi_cpu_init_idle(cpu);
}

int acpi_processor_ffh_lpi_enter(struct acpi_lpi_state *lpi)
{
	u32 state = lpi->address;

	if (state & SBI_HSM_SUSP_NON_RET_BIT)
		return CPU_PM_CPU_IDLE_ENTER_PARAM(riscv_sbi_hart_suspend,
						   lpi->index,
						   state);
	else
		return CPU_PM_CPU_IDLE_ENTER_RETENTION_PARAM(riscv_sbi_hart_suspend,
							     lpi->index,
							     state);
}
