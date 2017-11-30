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

/*
 * The public API for this code is documented in arch/arm/include/asm/mcpm.h.
 * For a comprehensive description of the main algorithm used here, please
 * see Documentation/arm/cluster-pm-race-avoidance.txt.
 */

struct sync_struct mcpm_sync;

/*
 * __mcpm_cpu_going_down: Indicates that the cpu is being torn down.
 *    This must be called at the point of committing to teardown of a CPU.
 *    The CPU cache (SCTRL.C bit) is expected to still be active.
 */
static void __mcpm_cpu_going_down(unsigned int cpu, unsigned int cluster)
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
static void __mcpm_cpu_down(unsigned int cpu, unsigned int cluster)
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
static void __mcpm_outbound_leave_critical(unsigned int cluster, int state)
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
static bool __mcpm_outbound_enter_critical(unsigned int cpu, unsigned int cluster)
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

static int __mcpm_cluster_state(unsigned int cluster)
{
	sync_cache_r(&mcpm_sync.clusters[cluster].cluster);
	return mcpm_sync.clusters[cluster].cluster;
}

extern unsigned long mcpm_entry_vectors[MAX_NR_CLUSTERS][MAX_CPUS_PER_CLUSTER];

void mcpm_set_entry_vector(unsigned cpu, unsigned cluster, void *ptr)
{
	unsigned long val = ptr ? __pa_symbol(ptr) : 0;
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

/*
 * We can't use regular spinlocks. In the switcher case, it is possible
 * for an outbound CPU to call power_down() after its inbound counterpart
 * is already live using the same logical CPU number which trips lockdep
 * debugging.
 */
static arch_spinlock_t mcpm_lock = __ARCH_SPIN_LOCK_UNLOCKED;

static int mcpm_cpu_use_count[MAX_NR_CLUSTERS][MAX_CPUS_PER_CLUSTER];

static inline bool mcpm_cluster_unused(unsigned int cluster)
{
	int i, cnt;
	for (i = 0, cnt = 0; i < MAX_CPUS_PER_CLUSTER; i++)
		cnt |= mcpm_cpu_use_count[cluster][i];
	return !cnt;
}

int mcpm_cpu_power_up(unsigned int cpu, unsigned int cluster)
{
	bool cpu_is_down, cluster_is_down;
	int ret = 0;

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (!platform_ops)
		return -EUNATCH; /* try not to shadow power_up errors */
	might_sleep();

	/*
	 * Since this is called with IRQs enabled, and no arch_spin_lock_irq
	 * variant exists, we need to disable IRQs manually here.
	 */
	local_irq_disable();
	arch_spin_lock(&mcpm_lock);

	cpu_is_down = !mcpm_cpu_use_count[cluster][cpu];
	cluster_is_down = mcpm_cluster_unused(cluster);

	mcpm_cpu_use_count[cluster][cpu]++;
	/*
	 * The only possible values are:
	 * 0 = CPU down
	 * 1 = CPU (still) up
	 * 2 = CPU requested to be up before it had a chance
	 *     to actually make itself down.
	 * Any other value is a bug.
	 */
	BUG_ON(mcpm_cpu_use_count[cluster][cpu] != 1 &&
	       mcpm_cpu_use_count[cluster][cpu] != 2);

	if (cluster_is_down)
		ret = platform_ops->cluster_powerup(cluster);
	if (cpu_is_down && !ret)
		ret = platform_ops->cpu_powerup(cpu, cluster);

	arch_spin_unlock(&mcpm_lock);
	local_irq_enable();
	return ret;
}

typedef typeof(cpu_reset) phys_reset_t;

void mcpm_cpu_power_down(void)
{
	unsigned int mpidr, cpu, cluster;
	bool cpu_going_down, last_man;
	phys_reset_t phys_reset;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (WARN_ON_ONCE(!platform_ops))
	       return;
	BUG_ON(!irqs_disabled());

	setup_mm_for_reboot();

	__mcpm_cpu_going_down(cpu, cluster);
	arch_spin_lock(&mcpm_lock);
	BUG_ON(__mcpm_cluster_state(cluster) != CLUSTER_UP);

	mcpm_cpu_use_count[cluster][cpu]--;
	BUG_ON(mcpm_cpu_use_count[cluster][cpu] != 0 &&
	       mcpm_cpu_use_count[cluster][cpu] != 1);
	cpu_going_down = !mcpm_cpu_use_count[cluster][cpu];
	last_man = mcpm_cluster_unused(cluster);

	if (last_man && __mcpm_outbound_enter_critical(cpu, cluster)) {
		platform_ops->cpu_powerdown_prepare(cpu, cluster);
		platform_ops->cluster_powerdown_prepare(cluster);
		arch_spin_unlock(&mcpm_lock);
		platform_ops->cluster_cache_disable();
		__mcpm_outbound_leave_critical(cluster, CLUSTER_DOWN);
	} else {
		if (cpu_going_down)
			platform_ops->cpu_powerdown_prepare(cpu, cluster);
		arch_spin_unlock(&mcpm_lock);
		/*
		 * If cpu_going_down is false here, that means a power_up
		 * request raced ahead of us.  Even if we do not want to
		 * shut this CPU down, the caller still expects execution
		 * to return through the system resume entry path, like
		 * when the WFI is aborted due to a new IRQ or the like..
		 * So let's continue with cache cleaning in all cases.
		 */
		platform_ops->cpu_cache_disable();
	}

	__mcpm_cpu_down(cpu, cluster);

	/* Now we are prepared for power-down, do it: */
	if (cpu_going_down)
		wfi();

	/*
	 * It is possible for a power_up request to happen concurrently
	 * with a power_down request for the same CPU. In this case the
	 * CPU might not be able to actually enter a powered down state
	 * with the WFI instruction if the power_up request has removed
	 * the required reset condition.  We must perform a re-entry in
	 * the kernel as if the power_up method just had deasserted reset
	 * on the CPU.
	 */
	phys_reset = (phys_reset_t)(unsigned long)__pa_symbol(cpu_reset);
	phys_reset(__pa_symbol(mcpm_entry_point), false);

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

void mcpm_cpu_suspend(void)
{
	if (WARN_ON_ONCE(!platform_ops))
		return;

	/* Some platforms might have to enable special resume modes, etc. */
	if (platform_ops->cpu_suspend_prepare) {
		unsigned int mpidr = read_cpuid_mpidr();
		unsigned int cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
		unsigned int cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1); 
		arch_spin_lock(&mcpm_lock);
		platform_ops->cpu_suspend_prepare(cpu, cluster);
		arch_spin_unlock(&mcpm_lock);
	}
	mcpm_cpu_power_down();
}

int mcpm_cpu_powered_up(void)
{
	unsigned int mpidr, cpu, cluster;
	bool cpu_was_down, first_man;
	unsigned long flags;

	if (!platform_ops)
		return -EUNATCH;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	local_irq_save(flags);
	arch_spin_lock(&mcpm_lock);

	cpu_was_down = !mcpm_cpu_use_count[cluster][cpu];
	first_man = mcpm_cluster_unused(cluster);

	if (first_man && platform_ops->cluster_is_up)
		platform_ops->cluster_is_up(cluster);
	if (cpu_was_down)
		mcpm_cpu_use_count[cluster][cpu] = 1;
	if (platform_ops->cpu_is_up)
		platform_ops->cpu_is_up(cpu, cluster);

	arch_spin_unlock(&mcpm_lock);
	local_irq_restore(flags);

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

	phys_reset = (phys_reset_t)(unsigned long)__pa_symbol(cpu_reset);
	phys_reset(__pa_symbol(mcpm_entry_point), false);
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
	for_each_online_cpu(i) {
		mcpm_cpu_use_count[this_cluster][i] = 1;
		mcpm_sync.clusters[this_cluster].cpus[i].cpu = CPU_UP;
	}
	mcpm_sync.clusters[this_cluster].cluster = CLUSTER_UP;
	sync_cache_w(&mcpm_sync);

	if (power_up_setup) {
		mcpm_power_up_setup_phys = __pa_symbol(power_up_setup);
		sync_cache_w(&mcpm_power_up_setup_phys);
	}

	return 0;
}
