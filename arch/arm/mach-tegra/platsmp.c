/*
 *  linux/arch/arm/mach-tegra/platsmp.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 *  Copyright (C) 2009 Palm
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/clk/tegra.h>

#include <asm/cacheflush.h>
#include <asm/mach-types.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>

#include "fuse.h"
#include "flowctrl.h"
#include "reset.h"
#include "pmc.h"

#include "common.h"
#include "iomap.h"

static cpumask_t tegra_cpu_init_mask;

static void tegra_secondary_init(unsigned int cpu)
{
	cpumask_set_cpu(cpu, &tegra_cpu_init_mask);
}


static int tegra20_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	cpu = cpu_logical_map(cpu);

	/*
	 * Force the CPU into reset. The CPU must remain in reset when
	 * the flow controller state is cleared (which will cause the
	 * flow controller to stop driving reset if the CPU has been
	 * power-gated via the flow controller). This will have no
	 * effect on first boot of the CPU since it should already be
	 * in reset.
	 */
	tegra_put_cpu_in_reset(cpu);

	/*
	 * Unhalt the CPU. If the flow controller was used to
	 * power-gate the CPU this will cause the flow controller to
	 * stop driving reset. The CPU will remain in reset because the
	 * clock and reset block is now driving reset.
	 */
	flowctrl_write_cpu_halt(cpu, 0);

	tegra_enable_cpu_clock(cpu);
	flowctrl_write_cpu_csr(cpu, 0); /* Clear flow controller CSR. */
	tegra_cpu_out_of_reset(cpu);
	return 0;
}

static int tegra30_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int ret;
	unsigned long timeout;

	cpu = cpu_logical_map(cpu);
	tegra_put_cpu_in_reset(cpu);
	flowctrl_write_cpu_halt(cpu, 0);

	/*
	 * The power up sequence of cold boot CPU and warm boot CPU
	 * was different.
	 *
	 * For warm boot CPU that was resumed from CPU hotplug, the
	 * power will be resumed automatically after un-halting the
	 * flow controller of the warm boot CPU. We need to wait for
	 * the confirmaiton that the CPU is powered then removing
	 * the IO clamps.
	 * For cold boot CPU, do not wait. After the cold boot CPU be
	 * booted, it will run to tegra_secondary_init() and set
	 * tegra_cpu_init_mask which influences what tegra30_boot_secondary()
	 * next time around.
	 */
	if (cpumask_test_cpu(cpu, &tegra_cpu_init_mask)) {
		timeout = jiffies + msecs_to_jiffies(50);
		do {
			if (tegra_pmc_cpu_is_powered(cpu))
				goto remove_clamps;
			udelay(10);
		} while (time_before(jiffies, timeout));
	}

	/*
	 * The power status of the cold boot CPU is power gated as
	 * default. To power up the cold boot CPU, the power should
	 * be un-gated by un-toggling the power gate register
	 * manually.
	 */
	if (!tegra_pmc_cpu_is_powered(cpu)) {
		ret = tegra_pmc_cpu_power_on(cpu);
		if (ret)
			return ret;

		/* Wait for the power to come up. */
		timeout = jiffies + msecs_to_jiffies(100);
		while (tegra_pmc_cpu_is_powered(cpu)) {
			if (time_after(jiffies, timeout))
				return -ETIMEDOUT;
			udelay(10);
		}
	}

remove_clamps:
	/* CPU partition is powered. Enable the CPU clock. */
	tegra_enable_cpu_clock(cpu);
	udelay(10);

	/* Remove I/O clamps. */
	ret = tegra_pmc_cpu_remove_clamping(cpu);
	if (ret)
		return ret;

	udelay(10);

	flowctrl_write_cpu_csr(cpu, 0); /* Clear flow controller CSR. */
	tegra_cpu_out_of_reset(cpu);
	return 0;
}

static int tegra114_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int ret = 0;

	cpu = cpu_logical_map(cpu);

	if (cpumask_test_cpu(cpu, &tegra_cpu_init_mask)) {
		/*
		 * Warm boot flow
		 * The flow controller in charge of the power state and
		 * control for each CPU.
		 */
		/* set SCLK as event trigger for flow controller */
		flowctrl_write_cpu_csr(cpu, 1);
		flowctrl_write_cpu_halt(cpu,
				FLOW_CTRL_WAITEVENT | FLOW_CTRL_SCLK_RESUME);
	} else {
		/*
		 * Cold boot flow
		 * The CPU is powered up by toggling PMC directly. It will
		 * also initial power state in flow controller. After that,
		 * the CPU's power state is maintained by flow controller.
		 */
		ret = tegra_pmc_cpu_power_on(cpu);
	}

	return ret;
}

static int tegra_boot_secondary(unsigned int cpu,
					  struct task_struct *idle)
{
	if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC) && tegra_chip_id == TEGRA20)
		return tegra20_boot_secondary(cpu, idle);
	if (IS_ENABLED(CONFIG_ARCH_TEGRA_3x_SOC) && tegra_chip_id == TEGRA30)
		return tegra30_boot_secondary(cpu, idle);
	if (IS_ENABLED(CONFIG_ARCH_TEGRA_114_SOC) && tegra_chip_id == TEGRA114)
		return tegra114_boot_secondary(cpu, idle);

	return -EINVAL;
}

static void __init tegra_smp_prepare_cpus(unsigned int max_cpus)
{
	/* Always mark the boot CPU (CPU0) as initialized. */
	cpumask_set_cpu(0, &tegra_cpu_init_mask);

	if (scu_a9_has_base())
		scu_enable(IO_ADDRESS(scu_a9_get_base()));
}

struct smp_operations tegra_smp_ops __initdata = {
	.smp_prepare_cpus	= tegra_smp_prepare_cpus,
	.smp_secondary_init	= tegra_secondary_init,
	.smp_boot_secondary	= tegra_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= tegra_cpu_kill,
	.cpu_die		= tegra_cpu_die,
	.cpu_disable		= tegra_cpu_disable,
#endif
};
