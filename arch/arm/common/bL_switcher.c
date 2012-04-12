/*
 * arch/arm/common/bL_switcher.c -- big.LITTLE cluster switcher core driver
 *
 * Created by:	Nicolas Pitre, March 2012
 * Copyright:	(C) 2012  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/cpu_pm.h>
#include <linux/workqueue.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/smp_plat.h>
#include <asm/cacheflush.h>
#include <asm/suspend.h>
#include <asm/mcpm.h>
#include <asm/bL_switcher.h>


/*
 * Use our own MPIDR accessors as the generic ones in asm/cputype.h have
 * __attribute_const__ and we don't want the compiler to assume any
 * constness here as the value _does_ change along some code paths.
 */

static int read_mpidr(void)
{
	unsigned int id;
	asm volatile ("mrc\tp15, 0, %0, c0, c0, 5" : "=r" (id));
	return id & MPIDR_HWID_BITMASK;
}

/*
 * bL switcher core code.
 */

static void bL_do_switch(void *_unused)
{
	unsigned mpidr, cpuid, clusterid, ob_cluster, ib_cluster;

	/*
	 * We now have a piece of stack borrowed from the init task's.
	 * Let's also switch to init_mm right away to match it.
	 */
	cpu_switch_mm(init_mm.pgd, &init_mm);

	pr_debug("%s\n", __func__);

	mpidr = read_mpidr();
	cpuid = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	clusterid = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	ob_cluster = clusterid;
	ib_cluster = clusterid ^ 1;

	/*
	 * Our state has been saved at this point.  Let's release our
	 * inbound CPU.
	 */
	mcpm_set_entry_vector(cpuid, ib_cluster, cpu_resume);
	sev();

	/*
	 * From this point, we must assume that our counterpart CPU might
	 * have taken over in its parallel world already, as if execution
	 * just returned from cpu_suspend().  It is therefore important to
	 * be very careful not to make any change the other guy is not
	 * expecting.  This is why we need stack isolation.
	 *
	 * Fancy under cover tasks could be performed here.  For now
	 * we have none.
	 */

	/* Let's put ourself down. */
	mcpm_cpu_power_down();

	/* should never get here */
	BUG();
}

/*
 * Stack isolation.  To ensure 'current' remains valid, we just borrow
 * a slice of the init/idle task which should be fairly lightly used.
 * The borrowed area starts just above the thread_info structure located
 * at the very bottom of the stack, aligned to a cache line.
 */
#define STACK_SIZE 256
extern void call_with_stack(void (*fn)(void *), void *arg, void *sp);
static int bL_switchpoint(unsigned long _arg)
{
	unsigned int mpidr = read_mpidr();
	unsigned int cpuid = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	unsigned int clusterid = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	unsigned int cpu_index = cpuid + clusterid * MAX_CPUS_PER_CLUSTER;
	void *stack = &init_thread_info + 1;
	stack = PTR_ALIGN(stack, L1_CACHE_BYTES);
	stack += cpu_index * STACK_SIZE + STACK_SIZE;
	call_with_stack(bL_do_switch, (void *)_arg, stack);
	BUG();
}

/*
 * Generic switcher interface
 */

/*
 * bL_switch_to - Switch to a specific cluster for the current CPU
 * @new_cluster_id: the ID of the cluster to switch to.
 *
 * This function must be called on the CPU to be switched.
 * Returns 0 on success, else a negative status code.
 */
static int bL_switch_to(unsigned int new_cluster_id)
{
	unsigned int mpidr, cpuid, clusterid, ob_cluster, ib_cluster, this_cpu;
	int ret;

	mpidr = read_mpidr();
	cpuid = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	clusterid = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	ob_cluster = clusterid;
	ib_cluster = clusterid ^ 1;

	if (new_cluster_id == clusterid)
		return 0;

	pr_debug("before switch: CPU %d in cluster %d\n", cpuid, clusterid);

	/* Close the gate for our entry vectors */
	mcpm_set_entry_vector(cpuid, ob_cluster, NULL);
	mcpm_set_entry_vector(cpuid, ib_cluster, NULL);

	/*
	 * Let's wake up the inbound CPU now in case it requires some delay
	 * to come online, but leave it gated in our entry vector code.
	 */
	ret = mcpm_cpu_power_up(cpuid, ib_cluster);
	if (ret) {
		pr_err("%s: mcpm_cpu_power_up() returned %d\n", __func__, ret);
		return ret;
	}

	/*
	 * From this point we are entering the switch critical zone
	 * and can't sleep/schedule anymore.
	 */
	local_irq_disable();
	local_fiq_disable();

	this_cpu = smp_processor_id();

	/* redirect GIC's SGIs to our counterpart */
	gic_migrate_target(cpuid + ib_cluster*4);

	/*
	 * Raise a SGI on the inbound CPU to make sure it doesn't stall
	 * in a possible WFI, such as in mcpm_power_down().
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(this_cpu));

	ret = cpu_pm_enter();

	/* we can not tolerate errors at this point */
	if (ret)
		panic("%s: cpu_pm_enter() returned %d\n", __func__, ret);

	/*
	 * Flip the cluster in the CPU logical map for this CPU.
	 * This must be flushed to RAM as the resume code
	 * needs to access it while the caches are still disabled.
	 */
	cpu_logical_map(this_cpu) ^= (1 << 8);
	__cpuc_flush_dcache_area(&cpu_logical_map(this_cpu),
				 sizeof(cpu_logical_map(this_cpu)));

	/* Let's do the actual CPU switch. */
	ret = cpu_suspend(0, bL_switchpoint);
	if (ret > 0)
		panic("%s: cpu_suspend() returned %d\n", __func__, ret);

	/* We are executing on the inbound CPU at this point */
	mpidr = read_mpidr();
	cpuid = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	clusterid = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	pr_debug("after switch: CPU %d in cluster %d\n", cpuid, clusterid);
	BUG_ON(clusterid != ib_cluster);

	mcpm_cpu_powered_up();

	ret = cpu_pm_exit();

	local_fiq_enable();
	local_irq_enable();

	if (ret)
		pr_err("%s exiting with error %d\n", __func__, ret);
	return ret;
}

struct switch_args {
	unsigned int cluster;
	struct work_struct work;
};

static void __bL_switch_to(struct work_struct *work)
{
	struct switch_args *args = container_of(work, struct switch_args, work);
	bL_switch_to(args->cluster);
}

/*
 * bL_switch_request - Switch to a specific cluster for the given CPU
 *
 * @cpu: the CPU to switch
 * @new_cluster_id: the ID of the cluster to switch to.
 *
 * This function causes a cluster switch on the given CPU.  If the given
 * CPU is the same as the calling CPU then the switch happens right away.
 * Otherwise the request is put on a work queue to be scheduled on the
 * remote CPU.
 */
void bL_switch_request(unsigned int cpu, unsigned int new_cluster_id)
{
	unsigned int this_cpu = get_cpu();
	struct switch_args args;

	if (cpu == this_cpu) {
		bL_switch_to(new_cluster_id);
		put_cpu();
		return;
	}
	put_cpu();

	args.cluster = new_cluster_id;
	INIT_WORK_ONSTACK(&args.work, __bL_switch_to);
	schedule_work_on(cpu, &args.work);
	flush_work(&args.work);
}

EXPORT_SYMBOL_GPL(bL_switch_request);
