/*
 * arch/arm/common/mcpm_entry.c -- entry point for multi-cluster PM
 *
 * Created by:  Nicolas Pitre, March 2012
 * Copyright:   (C) 2012-2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/cpu_pm.h>

#include <asm/mcpm.h>
#include <asm/cacheflush.h>
#include <asm/idmap.h>
#include <asm/cputype.h>
#include <asm/suspend.h>

extern unsigned long mcpm_entry_vectors[MAX_NR_CLUSTERS][MAX_CPUS_PER_CLUSTER];

void mcpm_set_entry_vector(unsigned cpu, unsigned cluster, void *ptr)
{
	unsigned long val = ptr ? virt_to_phys(ptr) : 0;
	mcpm_entry_vectors[cluster][cpu] = val;
	sync_cache_w(&mcpm_entry_vectors[cluster][cpu]);
}

extern unsigned long mcpm_entry_early_pokes[MAX_NR_CLUSTERS][MAX_CPUS_PER_CLUSTER][2];

void mcpm_set_early_poke(unsigned cpu, unsigned cluster,
			 unsigned long poke_phys_addr, unsigned long poke_val)
{
	unsigned long *poke = &mcpm_entry_early_pokes[cluster][cpu][0];
	poke[0] = poke_phys_addr;
	poke[1] = poke_val;
	__sync_cache_range_w(poke, 2 * sizeof(*poke));
}

static const struct mcpm_platform_ops *platform_ops;

int __init mcpm_platform_register(const struct mcpm_platform_ops *ops)
{
	if (platform_ops)
		return -EBUSY;
	platform_ops = ops;
	return 0;
}

bool mcpm_is_available(void)
{
	return (platform_ops) ? true : false;
}

int mcpm_cpu_power_up(unsigned int cpu, unsigned int cluster)
{
	if (!platform_ops)
		return -EUNATCH; /* try not to shadow power_up errors */
	might_sleep();
	return platform_ops->power_up(cpu, cluster);
}

typedef void (*phys_reset_t)(unsigned long);

void mcpm_cpu_power_down(void)
{
	phys_reset_t phys_reset;

	if (WARN_ON_ONCE(!platform_ops || !platform_ops->power_down))
		return;
	BUG_ON(!irqs_disabled());

	/*
	 * Do this before calling into the power_down method,
	 * as it might not always be safe to do afterwards.
	 */
	setup_mm_for_reboot();

	platform_ops->power_down();

	/*
	 * It is possible for a power_up request to happen concurrently
	 * with a power_down request for the same CPU. In this case the
	 * power_down method might not be able to actually enter a
	 * powered down state with the WFI instruction if the power_up
	 * method has removed the required reset condition.  The
	 * power_down method is then allowed to return. We must perform
	 * a re-entry in the kernel as if the power_up method just had
	 * deasserted reset on the CPU.
	 *
	 * To simplify race issues, the platform specific implementation
	 * must accommodate for the possibility of unordered calls to
	 * power_down and power_up with a usage count. Therefore, if a
	 * call to power_up is issued for a CPU that is not down, then
	 * the next call to power_down must not attempt a full shutdown
	 * but only do the minimum (normally disabling L1 cache and CPU
	 * coherency) and return just as if a concurrent power_up request
	 * had happened as described above.
	 */

	phys_reset = (phys_reset_t)(unsigned long)virt_to_phys(cpu_reset);
	phys_reset(virt_to_phys(mcpm_entry_point));

	/* should never get here */
	BUG();
}

int mcpm_wait_for_cpu_powerdown(unsigned int cpu, unsigned int cluster)
{
	int ret;

	if (WARN_ON_ONCE(!platform_ops || !platform_ops->wait_for_powerdown))
		return -EUNATCH;

	ret = platform_ops->wait_for_powerdown(cpu, cluster);
	if (ret)
		pr_warn("%s: cpu %u, cluster %u failed to power down (%d)\n",
			__func__, cpu, cluster, ret);

	return ret;
}

void mcpm_cpu_suspend(u64 expected_residency)
{
	phys_reset_t phys_reset;

	if (WARN_ON_ONCE(!platform_ops || !platform_ops->suspend))
		return;
	BUG_ON(!irqs_disabled());

	/* Very similar to mcpm_cpu_power_down() */
	setup_mm_for_reboot();
	platform_ops->suspend(expected_residency);
	phys_reset = (phys_reset_t)(unsigned long)virt_to_phys(cpu_reset);
	phys_reset(virt_to_phys(mcpm_entry_point));
	BUG();
}

int mcpm_cpu_powered_up(void)
{
	if (!platform_ops)
		return -EUNATCH;
	if (platform_ops->powered_up)
		platform_ops->powered_up();
	return 0;
}

#ifdef CONFIG_ARM_CPU_SUSPEND

static int __init nocache_trampoline(unsigned long _arg)
{
	void (*cache_disable)(void) = (void *)_arg;
	unsigned int mpidr = read_cpuid_mpidr();
	unsigned int cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	unsigned int cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	phys_reset_t phys_reset;

	mcpm_set_entry_vector(cpu, cluster, cpu_resume);
	setup_mm_for_reboot();

	__mcpm_cpu_going_down(cpu, cluster);
	BUG_ON(!__mcpm_outbound_enter_critical(cpu, cluster));
	cache_disable();
	__mcpm_outbound_leave_critical(cluster, CLUSTER_DOWN);
	__mcpm_cpu_down(cpu, cluster);

	phys_reset = (phys_reset_t)(unsigned long)virt_to_phys(cpu_reset);
	phys_reset(virt_to_phys(mcpm_entry_point));
	BUG();
}

int __init mcpm_loopback(void (*cache_disable)(void))
{
	int ret;

	/*
	 * We're going to soft-restart the current CPU through the
	 * low-level MCPM code by leveraging the suspend/resume
	 * infrastructure. Let's play it safe by using cpu_pm_enter()
	 * in case the CPU init code path resets the VFP or similar.
	 */
	local_irq_disable();
	local_fiq_disable();
	ret = cpu_pm_enter();
	if (!ret) {
		ret = cpu_suspend((unsigned long)cache_disable, nocache_trampoline);
		cpu_pm_exit();
	}
	local_fiq_enable();
	local_irq_enable();
	if (ret)
		pr_err("%s returned %d\n", __func__, ret);
	return ret;
}

#endif

struct sync_struct mcpm_sync;

/*
 * __mcpm_cpu_going_down: Indicates that the cpu is being torn down.
 *    This must be called at the point of committing to teardown of a CPU.
 *    The CPU cache (SCTRL.C bit) is expected to still be active.
 */
void __mcpm_cpu_going_down(unsigned int cpu, unsigned int cluster)
{
	mcpm_sync.clusters[cluster].cpus[cpu].cpu = CPU_GOING_DOWN;
	sync_cache_w(&mcpm_sync.clusters[cluster].cpus[cpu].cpu);
}

/*
 * __mcpm_cpu_down: Indicates that cpu teardown is complete and that the
 *    cluster can be torn down without disrupting this CPU.
 *    To avoid deadlocks, this must be called before a CPU is powered down.
 *    The CPU cache (SCTRL.C bit) is expected to be off.
 *    However L2 cache might or might not be active.
 */
void __mcpm_cpu_down(unsigned int cpu, unsigned int cluster)
{
	dmb();
	mcpm_sync.clusters[cluster].cpus[cpu].cpu = CPU_DOWN;
	sync_cache_w(&mcpm_sync.clusters[cluster].cpus[cpu].cpu);
	sev();
}

/*
 * __mcpm_outbound_leave_critical: Leave the cluster teardown critical section.
 * @state: the final state of the cluster:
 *     CLUSTER_UP: no destructive teardown was done and the cluster has been
 *         restored to the previous state (CPU cache still active); or
 *     CLUSTER_DOWN: the cluster has been torn-down, ready for power-off
 *         (CPU cache disabled, L2 cache either enabled or disabled).
 */
void __mcpm_outbound_leave_critical(unsigned int cluster, int state)
{
	dmb();
	mcpm_sync.clusters[cluster].cluster = state;
	sync_cache_w(&mcpm_sync.clusters[cluster].cluster);
	sev();
}

/*
 * __mcpm_outbound_enter_critical: Enter the cluster teardown critical section.
 * This function should be called by the last man, after local CPU teardown
 * is complete.  CPU cache expected to be active.
 *
 * Returns:
 *     false: the critical section was not entered because an inbound CPU was
 *         observed, or the cluster is already being set up;
 *     true: the critical section was entered: it is now safe to tear down the
 *         cluster.
 */
bool __mcpm_outbound_enter_critical(unsigned int cpu, unsigned int cluster)
{
	unsigned int i;
	struct mcpm_sync_struct *c = &mcpm_sync.clusters[cluster];

	/* Warn inbound CPUs that the cluster is being torn down: */
	c->cluster = CLUSTER_GOING_DOWN;
	sync_cache_w(&c->cluster);

	/* Back out if the inbound cluster is already in the critical region: */
	sync_cache_r(&c->inbound);
	if (c->inbound == INBOUND_COMING_UP)
		goto abort;

	/*
	 * Wait for all CPUs to get out of the GOING_DOWN state, so that local
	 * teardown is complete on each CPU before tearing down the cluster.
	 *
	 * If any CPU has been woken up again from the DOWN state, then we
	 * shouldn't be taking the cluster down at all: abort in that case.
	 */
	sync_cache_r(&c->cpus);
	for (i = 0; i < MAX_CPUS_PER_CLUSTER; i++) {
		int cpustate;

		if (i == cpu)
			continue;

		while (1) {
			cpustate = c->cpus[i].cpu;
			if (cpustate != CPU_GOING_DOWN)
				break;

			wfe();
			sync_cache_r(&c->cpus[i].cpu);
		}

		switch (cpustate) {
		case CPU_DOWN:
			continue;

		default:
			goto abort;
		}
	}

	return true;

abort:
	__mcpm_outbound_leave_critical(cluster, CLUSTER_UP);
	return false;
}

int __mcpm_cluster_state(unsigned int cluster)
{
	sync_cache_r(&mcpm_sync.clusters[cluster].cluster);
	return mcpm_sync.clusters[cluster].cluster;
}

extern unsigned long mcpm_power_up_setup_phys;

int __init mcpm_sync_init(
	void (*power_up_setup)(unsigned int affinity_level))
{
	unsigned int i, j, mpidr, this_cluster;

	BUILD_BUG_ON(MCPM_SYNC_CLUSTER_SIZE * MAX_NR_CLUSTERS != sizeof mcpm_sync);
	BUG_ON((unsigned long)&mcpm_sync & (__CACHE_WRITEBACK_GRANULE - 1));

	/*
	 * Set initial CPU and cluster states.
	 * Only one cluster is assumed to be active at this point.
	 */
	for (i = 0; i < MAX_NR_CLUSTERS; i++) {
		mcpm_sync.clusters[i].cluster = CLUSTER_DOWN;
		mcpm_sync.clusters[i].inbound = INBOUND_NOT_COMING_UP;
		for (j = 0; j < MAX_CPUS_PER_CLUSTER; j++)
			mcpm_sync.clusters[i].cpus[j].cpu = CPU_DOWN;
	}
	mpidr = read_cpuid_mpidr();
	this_cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	for_each_online_cpu(i)
		mcpm_sync.clusters[this_cluster].cpus[i].cpu = CPU_UP;
	mcpm_sync.clusters[this_cluster].cluster = CLUSTER_UP;
	sync_cache_w(&mcpm_sync);

	if (power_up_setup) {
		mcpm_power_up_setup_phys = virt_to_phys(power_up_setup);
		sync_cache_w(&mcpm_power_up_setup_phys);
	}

	return 0;
}
