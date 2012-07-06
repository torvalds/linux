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
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/clockchips.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
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
 * Stack isolation.  To ensure 'current' remains valid, we just use another
 * piece of our thread's stack space which should be fairly lightly used.
 * The selected area starts just above the thread_info structure located
 * at the very bottom of the stack, aligned to a cache line, and indexed
 * with the cluster number.
 */
#define STACK_SIZE 512
extern void call_with_stack(void (*fn)(void *), void *arg, void *sp);
static int bL_switchpoint(unsigned long _arg)
{
	unsigned int mpidr = read_mpidr();
	unsigned int clusterid = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	void *stack = current_thread_info() + 1;
	stack = PTR_ALIGN(stack, L1_CACHE_BYTES);
	stack += clusterid * STACK_SIZE + STACK_SIZE;
	call_with_stack(bL_do_switch, (void *)_arg, stack);
	BUG();
}

/*
 * Generic switcher interface
 */

static unsigned int bL_gic_id[MAX_CPUS_PER_CLUSTER][MAX_NR_CLUSTERS];

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
	struct tick_device *tdev;
	enum clock_event_mode tdev_mode;
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
	gic_migrate_target(bL_gic_id[cpuid][ib_cluster]);

	/*
	 * Raise a SGI on the inbound CPU to make sure it doesn't stall
	 * in a possible WFI, such as in mcpm_power_down().
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(this_cpu));

	tdev = tick_get_device(this_cpu);
	if (tdev && !cpumask_equal(tdev->evtdev->cpumask, cpumask_of(this_cpu)))
		tdev = NULL;
	if (tdev) {
		tdev_mode = tdev->evtdev->mode;
		clockevents_set_mode(tdev->evtdev, CLOCK_EVT_MODE_SHUTDOWN);
	}

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

	if (tdev) {
		clockevents_set_mode(tdev->evtdev, tdev_mode);
		clockevents_program_event(tdev->evtdev,
					  tdev->evtdev->next_event, 1);
	}

	local_fiq_enable();
	local_irq_enable();

	if (ret)
		pr_err("%s exiting with error %d\n", __func__, ret);
	return ret;
}

struct bL_thread {
	struct task_struct *task;
	wait_queue_head_t wq;
	int wanted_cluster;
};

static struct bL_thread bL_threads[MAX_CPUS_PER_CLUSTER];

static int bL_switcher_thread(void *arg)
{
	struct bL_thread *t = arg;
	struct sched_param param = { .sched_priority = 1 };
	int cluster;

	sched_setscheduler_nocheck(current, SCHED_FIFO, &param);

	do {
		if (signal_pending(current))
			flush_signals(current);
		wait_event_interruptible(t->wq,
				t->wanted_cluster != -1 ||
				kthread_should_stop());
		cluster = xchg(&t->wanted_cluster, -1);
		if (cluster != -1)
			bL_switch_to(cluster);
	} while (!kthread_should_stop());

	return 0;
}

static struct task_struct * __init bL_switcher_thread_create(int cpu, void *arg)
{
	struct task_struct *task;

	task = kthread_create_on_node(bL_switcher_thread, arg,
				      cpu_to_node(cpu), "kswitcher_%d", cpu);
	if (!IS_ERR(task)) {
		kthread_bind(task, cpu);
		wake_up_process(task);
	} else
		pr_err("%s failed for CPU %d\n", __func__, cpu);
	return task;
}

/*
 * bL_switch_request - Switch to a specific cluster for the given CPU
 *
 * @cpu: the CPU to switch
 * @new_cluster_id: the ID of the cluster to switch to.
 *
 * This function causes a cluster switch on the given CPU by waking up
 * the appropriate switcher thread.  This function may or may not return
 * before the switch has occurred.
 */
int bL_switch_request(unsigned int cpu, unsigned int new_cluster_id)
{
	struct bL_thread *t;

	if (cpu >= MAX_CPUS_PER_CLUSTER) {
		pr_err("%s: cpu %d out of bounds\n", __func__, cpu);
		return -EINVAL;
	}

	t = &bL_threads[cpu];
	if (IS_ERR(t->task))
		return PTR_ERR(t->task);
	if (!t->task)
		return -ESRCH;

	t->wanted_cluster = new_cluster_id;
	wake_up(&t->wq);
	return 0;
}

EXPORT_SYMBOL_GPL(bL_switch_request);

/*
 * Activation and configuration code.
 */

static cpumask_t bL_switcher_removed_logical_cpus;

static void __init bL_switcher_restore_cpus(void)
{
	int i;

	for_each_cpu(i, &bL_switcher_removed_logical_cpus)
		cpu_up(i);
}

static int __init bL_switcher_halve_cpus(void)
{
	int cpu, cluster, i, ret;
	cpumask_t cluster_mask[2], common_mask;

	cpumask_clear(&bL_switcher_removed_logical_cpus);
	cpumask_clear(&cluster_mask[0]);
	cpumask_clear(&cluster_mask[1]);

	for_each_online_cpu(i) {
		cpu = cpu_logical_map(i) & 0xff;
		cluster = (cpu_logical_map(i) >> 8) & 0xff;
		if (cluster >= 2) {
			pr_err("%s: only dual cluster systems are supported\n", __func__);
			return -EINVAL;
		}
		cpumask_set_cpu(cpu, &cluster_mask[cluster]);
	}

	if (!cpumask_and(&common_mask, &cluster_mask[0], &cluster_mask[1])) {
		pr_err("%s: no common set of CPUs\n", __func__);
		return -EINVAL;
	}

	for_each_online_cpu(i) {
		cpu = cpu_logical_map(i) & 0xff;
		cluster = (cpu_logical_map(i) >> 8) & 0xff;

		if (cpumask_test_cpu(cpu, &common_mask)) {
			/* Let's take note of the GIC ID for this CPU */
			int gic_id = gic_get_cpu_id(i);
			if (gic_id < 0) {
				pr_err("%s: bad GIC ID for CPU %d\n", __func__, i);
				return -EINVAL;
			}
			bL_gic_id[cpu][cluster] = gic_id;
			pr_info("GIC ID for CPU %u cluster %u is %u\n",
				cpu, cluster, gic_id);

			/*
			 * We keep only those logical CPUs which number
			 * is equal to their physical CPU number. This is
			 * not perfect but good enough in most cases.
			 */
			if (cpu == i)
				continue;
		}

		ret = cpu_down(i);
		if (ret) {
			bL_switcher_restore_cpus();
			return ret;
		}
		cpumask_set_cpu(i, &bL_switcher_removed_logical_cpus);
	}

	return 0;
}

static int __init bL_switcher_init(void)
{
	int cpu, ret;

	pr_info("big.LITTLE switcher initializing\n");

	if (MAX_NR_CLUSTERS != 2) {
		pr_err("%s: only dual cluster systems are supported\n", __func__);
		return -EINVAL;
	}

	cpu_hotplug_driver_lock();
	ret = bL_switcher_halve_cpus();
	if (ret) {
		cpu_hotplug_driver_unlock();
		return ret;
	}

	for_each_online_cpu(cpu) {
		struct bL_thread *t = &bL_threads[cpu];
		init_waitqueue_head(&t->wq);
		t->wanted_cluster = -1;
		t->task = bL_switcher_thread_create(cpu, t);
	}
	cpu_hotplug_driver_unlock();

	pr_info("big.LITTLE switcher initialized\n");
	return 0;
}

late_initcall(bL_switcher_init);
