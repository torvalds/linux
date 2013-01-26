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

#include <asm/cacheflush.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/smp_scu.h>

#include <mach/powergate.h>

#include "fuse.h"
#include "flowctrl.h"
#include "reset.h"
#include "tegra_cpu_car.h"

#include "common.h"
#include "iomap.h"

extern void tegra_secondary_startup(void);

static void __iomem *scu_base = IO_ADDRESS(TEGRA_ARM_PERIF_BASE);

#define EVP_CPU_RESET_VECTOR \
	(IO_ADDRESS(TEGRA_EXCEPTION_VECTORS_BASE) + 0x100)

static void __cpuinit tegra_secondary_init(unsigned int cpu)
{
	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	gic_secondary_init(0);

}

static int tegra20_power_up_cpu(unsigned int cpu)
{
	/* Enable the CPU clock. */
	tegra_enable_cpu_clock(cpu);

	/* Clear flow controller CSR. */
	flowctrl_write_cpu_csr(cpu, 0);

	return 0;
}

static int tegra30_power_up_cpu(unsigned int cpu)
{
	int ret, pwrgateid;
	unsigned long timeout;

	pwrgateid = tegra_cpu_powergate_id(cpu);
	if (pwrgateid < 0)
		return pwrgateid;

	/* If this is the first boot, toggle powergates directly. */
	if (!tegra_powergate_is_powered(pwrgateid)) {
		ret = tegra_powergate_power_on(pwrgateid);
		if (ret)
			return ret;

		/* Wait for the power to come up. */
		timeout = jiffies + 10*HZ;
		while (tegra_powergate_is_powered(pwrgateid)) {
			if (time_after(jiffies, timeout))
				return -ETIMEDOUT;
			udelay(10);
		}
	}

	/* CPU partition is powered. Enable the CPU clock. */
	tegra_enable_cpu_clock(cpu);
	udelay(10);

	/* Remove I/O clamps. */
	ret = tegra_powergate_remove_clamping(pwrgateid);
	udelay(10);

	/* Clear flow controller CSR. */
	flowctrl_write_cpu_csr(cpu, 0);

	return 0;
}

static int __cpuinit tegra_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int status;

	/*
	 * Force the CPU into reset. The CPU must remain in reset when the
	 * flow controller state is cleared (which will cause the flow
	 * controller to stop driving reset if the CPU has been power-gated
	 * via the flow controller). This will have no effect on first boot
	 * of the CPU since it should already be in reset.
	 */
	tegra_put_cpu_in_reset(cpu);

	/*
	 * Unhalt the CPU. If the flow controller was used to power-gate the
	 * CPU this will cause the flow controller to stop driving reset.
	 * The CPU will remain in reset because the clock and reset block
	 * is now driving reset.
	 */
	flowctrl_write_cpu_halt(cpu, 0);

	switch (tegra_chip_id) {
	case TEGRA20:
		status = tegra20_power_up_cpu(cpu);
		break;
	case TEGRA30:
		status = tegra30_power_up_cpu(cpu);
		break;
	default:
		status = -EINVAL;
		break;
	}

	if (status)
		goto done;

	/* Take the CPU out of reset. */
	tegra_cpu_out_of_reset(cpu);
done:
	return status;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init tegra_smp_init_cpus(void)
{
	unsigned int i, ncores = scu_get_core_count(scu_base);

	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
			ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	set_smp_cross_call(gic_raise_softirq);
}

static void __init tegra_smp_prepare_cpus(unsigned int max_cpus)
{
	tegra_cpu_reset_handler_init();
	scu_enable(scu_base);
}

struct smp_operations tegra_smp_ops __initdata = {
	.smp_init_cpus		= tegra_smp_init_cpus,
	.smp_prepare_cpus	= tegra_smp_prepare_cpus,
	.smp_secondary_init	= tegra_secondary_init,
	.smp_boot_secondary	= tegra_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= tegra_cpu_die,
	.cpu_disable		= tegra_cpu_disable,
#endif
};
