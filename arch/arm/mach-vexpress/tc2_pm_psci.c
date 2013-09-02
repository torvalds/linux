/*
 * arch/arm/mach-vexpress/tc2_pm_psci.c - TC2 PSCI support
 *
 * Created by: Achin Gupta, December 2012
 * Copyright:  (C) 2012  ARM Limited
 *
 * Some portions of this file were originally written by Nicolas Pitre
 * Copyright:   (C) 2012  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/errno.h>

#include <asm/mcpm.h>
#include <asm/proc-fns.h>
#include <asm/cacheflush.h>
#include <asm/psci.h>
#include <asm/atomic.h>
#include <asm/cputype.h>
#include <asm/cp15.h>

#include <mach/motherboard.h>

#include <linux/vexpress.h>

/*
 * Platform specific state id understood by the firmware and used to
 * program the power controller
 */
#define PSCI_POWER_STATE_ID           0

#define TC2_CLUSTERS			2
#define TC2_MAX_CPUS_PER_CLUSTER	3

static atomic_t tc2_pm_use_count[TC2_MAX_CPUS_PER_CLUSTER][TC2_CLUSTERS];

static int tc2_pm_psci_power_up(unsigned int cpu, unsigned int cluster)
{
	unsigned int mpidr = (cluster << 8) | cpu;
	int ret = 0;

	BUG_ON(!psci_ops.cpu_on);

	switch (atomic_inc_return(&tc2_pm_use_count[cpu][cluster])) {
	case 1:
		/*
		 * This is a request to power up a cpu that linux thinks has
		 * been powered down. Retries are needed if the firmware has
		 * seen the power down request as yet.
		 */
		do
			ret = psci_ops.cpu_on(mpidr,
					      virt_to_phys(mcpm_entry_point));
		while (ret == -EAGAIN);

		return ret;
	case 2:
		/* This power up request has overtaken a power down request */
		return ret;
	default:
		/* Any other value is a bug */
		BUG();
	}
}

static void tc2_pm_psci_power_down(void)
{
	struct psci_power_state power_state;
	unsigned int mpidr, cpu, cluster;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	BUG_ON(!psci_ops.cpu_off);

	switch (atomic_dec_return(&tc2_pm_use_count[cpu][cluster])) {
	case 1:
		/*
		 * Overtaken by a power up. Flush caches, exit coherency,
		 * return & fake a reset
		 */
		set_cr(get_cr() & ~CR_C);

		flush_cache_louis();

		asm volatile ("clrex");
		set_auxcr(get_auxcr() & ~(1 << 6));

		return;
	case 0:
		/* A normal request to possibly power down the cluster */
		power_state.id = PSCI_POWER_STATE_ID;
		power_state.type = PSCI_POWER_STATE_TYPE_POWER_DOWN;
		power_state.affinity_level = PSCI_POWER_STATE_AFFINITY_LEVEL1;

		psci_ops.cpu_off(power_state);

		/* On success this function never returns */
	default:
		/* Any other value is a bug */
		BUG();
	}
}

static void tc2_pm_psci_suspend(u64 unused)
{
	struct psci_power_state power_state;

	BUG_ON(!psci_ops.cpu_suspend);

	/* On TC2 always attempt to power down the cluster */
	power_state.id = PSCI_POWER_STATE_ID;
	power_state.type = PSCI_POWER_STATE_TYPE_POWER_DOWN;
	power_state.affinity_level = PSCI_POWER_STATE_AFFINITY_LEVEL1;

	psci_ops.cpu_suspend(power_state, virt_to_phys(mcpm_entry_point));

	/* On success this function never returns */
	BUG();
}

static const struct mcpm_platform_ops tc2_pm_power_ops = {
	.power_up      = tc2_pm_psci_power_up,
	.power_down    = tc2_pm_psci_power_down,
	.suspend       = tc2_pm_psci_suspend,
};

static void __init tc2_pm_usage_count_init(void)
{
	unsigned int mpidr, cpu, cluster;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= TC2_CLUSTERS || cpu >= TC2_MAX_CPUS_PER_CLUSTER);

	atomic_set(&tc2_pm_use_count[cpu][cluster], 1);
}

static int __init tc2_pm_psci_init(void)
{
	int ret;

	ret = psci_probe();
	if (ret) {
		pr_debug("psci not found. Aborting psci init\n");
		return -ENODEV;
	}

	if (!of_machine_is_compatible("arm,vexpress,v2p-ca15_a7"))
		return -ENODEV;

	tc2_pm_usage_count_init();

	ret = mcpm_platform_register(&tc2_pm_power_ops);
	if (!ret)
		ret = mcpm_sync_init(NULL);
	if (!ret)
		pr_info("TC2 power management using PSCI initialized\n");
	return ret;
}

early_initcall(tc2_pm_psci_init);
