/*
 * arch/arm/mach-vexpress/tc2_pm.c - TC2 power management support
 *
 * Created by:	Nicolas Pitre, October 2012
 * Copyright:	(C) 2012  Linaro Limited
 *
 * Some portions of this file were originally written by Achin Gupta
 * Copyright:   (C) 2012  ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/mcpm.h>
#include <asm/proc-fns.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/cp15.h>
#include <asm/psci.h>

#include <mach/motherboard.h>
#include <mach/tc2.h>

#include <linux/vexpress.h>
#include <linux/arm-cci.h>

/*
 * We can't use regular spinlocks. In the switcher case, it is possible
 * for an outbound CPU to call power_down() after its inbound counterpart
 * is already live using the same logical CPU number which trips lockdep
 * debugging.
 */
static arch_spinlock_t tc2_pm_lock = __ARCH_SPIN_LOCK_UNLOCKED;

static int tc2_pm_use_count[TC2_MAX_CPUS][TC2_MAX_CLUSTERS];

static int tc2_pm_power_up(unsigned int cpu, unsigned int cluster)
{
	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (cluster >= TC2_MAX_CLUSTERS ||
	    cpu >= vexpress_spc_get_nb_cpus(cluster))
		return -EINVAL;

	/*
	 * Since this is called with IRQs enabled, and no arch_spin_lock_irq
	 * variant exists, we need to disable IRQs manually here.
	 */
	local_irq_disable();
	arch_spin_lock(&tc2_pm_lock);

	if (!tc2_pm_use_count[0][cluster] &&
	    !tc2_pm_use_count[1][cluster] &&
	    !tc2_pm_use_count[2][cluster])
		vexpress_spc_powerdown_enable(cluster, 0);

	tc2_pm_use_count[cpu][cluster]++;
	if (tc2_pm_use_count[cpu][cluster] == 1) {
		vexpress_spc_write_resume_reg(cluster, cpu,
					      virt_to_phys(mcpm_entry_point));
		vexpress_spc_set_cpu_wakeup_irq(cpu, cluster, 1);
	} else if (tc2_pm_use_count[cpu][cluster] != 2) {
		/*
		 * The only possible values are:
		 * 0 = CPU down
		 * 1 = CPU (still) up
		 * 2 = CPU requested to be up before it had a chance
		 *     to actually make itself down.
		 * Any other value is a bug.
		 */
		BUG();
	}

	arch_spin_unlock(&tc2_pm_lock);
	local_irq_enable();

	return 0;
}

static void tc2_pm_down(u64 residency)
{
	unsigned int mpidr, cpu, cluster;
	bool last_man = false, skip_wfi = false;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= TC2_MAX_CLUSTERS ||
	       cpu >= vexpress_spc_get_nb_cpus(cluster));

	__mcpm_cpu_going_down(cpu, cluster);

	arch_spin_lock(&tc2_pm_lock);
	BUG_ON(__mcpm_cluster_state(cluster) != CLUSTER_UP);
	tc2_pm_use_count[cpu][cluster]--;
	if (tc2_pm_use_count[cpu][cluster] == 0) {
		vexpress_spc_set_cpu_wakeup_irq(cpu, cluster, 1);
		if (!tc2_pm_use_count[0][cluster] &&
		    !tc2_pm_use_count[1][cluster] &&
		    !tc2_pm_use_count[2][cluster] &&
		    (!residency || residency > 5000)) {
			vexpress_spc_powerdown_enable(cluster, 1);
			vexpress_spc_set_global_wakeup_intr(1);
			last_man = true;
		}
	} else if (tc2_pm_use_count[cpu][cluster] == 1) {
		/*
		 * A power_up request went ahead of us.
		 * Even if we do not want to shut this CPU down,
		 * the caller expects a certain state as if the WFI
		 * was aborted.  So let's continue with cache cleaning.
		 */
		skip_wfi = true;
	} else
		BUG();

	gic_cpu_if_down();

	if (last_man && __mcpm_outbound_enter_critical(cpu, cluster)) {
		arch_spin_unlock(&tc2_pm_lock);

		set_cr(get_cr() & ~CR_C);
		flush_cache_all();
		asm volatile ("clrex");
		set_auxcr(get_auxcr() & ~(1 << 6));

		cci_disable_port_by_cpu(mpidr);

		/*
		 * Ensure that both C & I bits are disabled in the SCTLR
		 * before disabling ACE snoops. This ensures that no
		 * coherency traffic will originate from this cpu after
		 * ACE snoops are turned off.
		 */
		cpu_proc_fin();

		__mcpm_outbound_leave_critical(cluster, CLUSTER_DOWN);
	} else {
		/*
		 * If last man then undo any setup done previously.
		 */
		if (last_man) {
			vexpress_spc_powerdown_enable(cluster, 0);
			vexpress_spc_set_global_wakeup_intr(0);
		}

		arch_spin_unlock(&tc2_pm_lock);

		set_cr(get_cr() & ~CR_C);
		flush_cache_louis();
		asm volatile ("clrex");
		set_auxcr(get_auxcr() & ~(1 << 6));
	}

	__mcpm_cpu_down(cpu, cluster);

	/* Now we are prepared for power-down, do it: */
	if (!skip_wfi)
		wfi();

	/* Not dead at this point?  Let our caller cope. */
}

static void tc2_pm_power_down(void)
{
	tc2_pm_down(0);
}

static void tc2_pm_suspend(u64 residency)
{
	extern void tc2_resume(void);
	unsigned int mpidr, cpu, cluster;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	vexpress_spc_write_resume_reg(cluster, cpu,
				      virt_to_phys(tc2_resume));

	tc2_pm_down(residency);
}

static void tc2_pm_powered_up(void)
{
	unsigned int mpidr, cpu, cluster;
	unsigned long flags;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= TC2_MAX_CLUSTERS ||
	       cpu >= vexpress_spc_get_nb_cpus(cluster));

	local_irq_save(flags);
	arch_spin_lock(&tc2_pm_lock);

	if (!tc2_pm_use_count[0][cluster] &&
	    !tc2_pm_use_count[1][cluster] &&
	    !tc2_pm_use_count[2][cluster]) {
		vexpress_spc_powerdown_enable(cluster, 0);
		vexpress_spc_set_global_wakeup_intr(0);
	}

	if (!tc2_pm_use_count[cpu][cluster])
		tc2_pm_use_count[cpu][cluster] = 1;

	vexpress_spc_set_cpu_wakeup_irq(cpu, cluster, 0);
	vexpress_spc_write_resume_reg(cluster, cpu, 0);

	arch_spin_unlock(&tc2_pm_lock);
	local_irq_restore(flags);
}

static const struct mcpm_platform_ops tc2_pm_power_ops = {
	.power_up	= tc2_pm_power_up,
	.power_down	= tc2_pm_power_down,
	.suspend	= tc2_pm_suspend,
	.powered_up	= tc2_pm_powered_up,
};

static void __init tc2_pm_usage_count_init(void)
{
	unsigned int mpidr, cpu, cluster;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= TC2_MAX_CLUSTERS ||
	       cpu >= vexpress_spc_get_nb_cpus(cluster));

	tc2_pm_use_count[cpu][cluster] = 1;
}

extern void tc2_pm_power_up_setup(unsigned int affinity_level);

static int __init tc2_pm_init(void)
{
	int ret;

	ret = psci_probe();
	if (!ret) {
		pr_debug("psci found. Aborting native init\n");
		return -ENODEV;
	}

	if (!vexpress_spc_check_loaded())
		return -ENODEV;

	tc2_pm_usage_count_init();

	ret = mcpm_platform_register(&tc2_pm_power_ops);
	if (!ret)
		ret = mcpm_sync_init(tc2_pm_power_up_setup);
	if (!ret)
		pr_info("TC2 power management initialized\n");
	return ret;
}

early_initcall(tc2_pm_init);
