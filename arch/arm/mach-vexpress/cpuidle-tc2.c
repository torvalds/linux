/*
 * TC2 CPU idle driver.
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/arm-cci.h>
#include <linux/bitmap.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/clockchips.h>
#include <linux/debugfs.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tick.h>
#include <linux/vexpress.h>
#include <asm/cpuidle.h>
#include <asm/cputype.h>
#include <asm/idmap.h>
#include <asm/proc-fns.h>
#include <asm/suspend.h>

#include <mach/motherboard.h>

static int tc2_cpuidle_simple_enter(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)
{
	ktime_t time_start, time_end;
	s64 diff;

	time_start = ktime_get();

	cpu_do_idle();

	time_end = ktime_get();

	local_irq_enable();

	diff = ktime_to_us(ktime_sub(time_end, time_start));
	if (diff > INT_MAX)
		diff = INT_MAX;

	dev->last_residency = (int) diff;

	return index;
}

static int tc2_enter_coupled(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int idx);

static struct cpuidle_state tc2_cpuidle_set[] __initdata = {
	[0] = {
		.enter                  = tc2_cpuidle_simple_enter,
		.exit_latency           = 1,
		.target_residency       = 1,
		.power_usage		= UINT_MAX,
		.flags                  = CPUIDLE_FLAG_TIME_VALID,
		.name                   = "WFI",
		.desc                   = "ARM WFI",
	},
	[1] = {
		.enter			= tc2_enter_coupled,
		.exit_latency		= 300,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
							CPUIDLE_FLAG_COUPLED,
		.name			= "C1",
		.desc			= "ARM power down",
	},
};

struct cpuidle_driver tc2_idle_driver = {
	.name = "tc2_idle",
	.owner = THIS_MODULE,
	.safe_state_index = 0
};

static DEFINE_PER_CPU(struct cpuidle_device, tc2_idle_dev);

#define NR_CLUSTERS 2
static cpumask_t cluster_mask = CPU_MASK_NONE;

extern void disable_clean_inv_dcache(int);
static atomic_t abort_barrier[NR_CLUSTERS];

extern void tc2_cpu_resume(void);
extern void disable_snoops(void);

static int notrace tc2_coupled_finisher(unsigned long arg)
{
	unsigned int mpidr = read_cpuid_mpidr();
	unsigned int cpu = smp_processor_id();
	unsigned int cluster = (mpidr >> 8) & 0xf;
	unsigned int weight = cpumask_weight(topology_core_cpumask(cpu));
	u8 wfi_weight = 0;

	cpuidle_coupled_parallel_barrier((struct cpuidle_device *)arg,
					&abort_barrier[cluster]);
	if (mpidr & 0xf) {
		disable_clean_inv_dcache(0);
		wfi();
		/* not reached */
	}

	while (wfi_weight != (weight - 1)) {
		wfi_weight = vexpress_spc_wfi_cpustat(cluster);
		wfi_weight = hweight8(wfi_weight);
	}

	vexpress_spc_powerdown_enable(cluster, 1);
	disable_clean_inv_dcache(1);
	disable_cci(cluster);
	disable_snoops();
	return 1;
}

/*
 * tc2_enter_coupled - Programs CPU to enter the specified state
 * @dev: cpuidle device
 * @drv: The target state to be programmed
 * @idx: state index
 *
 * Called from the CPUidle framework to program the device to the
 * specified target state selected by the governor.
 */
static int tc2_enter_coupled(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int idx)
{
	struct timespec ts_preidle, ts_postidle, ts_idle;
	int ret;
	int cluster = (read_cpuid_mpidr() >> 8) & 0xf;
	/* Used to keep track of the total time in idle */
	getnstimeofday(&ts_preidle);

	if (!cpu_isset(cluster, cluster_mask)) {
			cpuidle_coupled_parallel_barrier(dev,
					&abort_barrier[cluster]);
			goto shallow_out;
	}

	BUG_ON(!irqs_disabled());

	cpu_pm_enter();

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &dev->cpu);

	ret = cpu_suspend((unsigned long) dev, tc2_coupled_finisher);

	if (ret)
		BUG();

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &dev->cpu);

	cpu_pm_exit();

shallow_out:
	getnstimeofday(&ts_postidle);
	ts_idle = timespec_sub(ts_postidle, ts_preidle);

	dev->last_residency = ts_idle.tv_nsec / NSEC_PER_USEC +
					ts_idle.tv_sec * USEC_PER_SEC;
	return idx;
}

static int idle_mask_show(struct seq_file *f, void *p)
{
	char buf[256];
	bitmap_scnlistprintf(buf, 256, cpumask_bits(&cluster_mask),
							NR_CLUSTERS);

	seq_printf(f, "%s\n", buf);

	return 0;
}

static int idle_mask_open(struct inode *inode, struct file *file)
{
	return single_open(file, idle_mask_show, inode->i_private);
}

static const struct file_operations cpuidle_fops = {
	.open		= idle_mask_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int idle_debug_set(void *data, u64 val)
{
	if (val >= (unsigned)NR_CLUSTERS && val != 0xff) {
		pr_warning("Wrong parameter passed\n");
		return -EINVAL;
	}
	cpuidle_pause_and_lock();
	if (val == 0xff)
		cpumask_clear(&cluster_mask);
	else
		cpumask_set_cpu(val, &cluster_mask);

	cpuidle_resume_and_unlock();
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(idle_debug_fops, NULL, idle_debug_set, "%llu\n");

/*
 * tc2_idle_init
 *
 * Registers the TC2 specific cpuidle driver with the cpuidle
 * framework with the valid set of states.
 */
int __init tc2_idle_init(void)
{
	struct cpuidle_device *dev;
	int i, cpu_id;
	struct dentry *idle_debug, *file_debug;
	struct cpuidle_driver *drv = &tc2_idle_driver;

	if (!vexpress_spc_check_loaded()) {
		pr_info("TC2 CPUidle not registered because no SPC found\n");
		return -ENODEV;
	}

	drv->state_count = (sizeof(tc2_cpuidle_set) /
				       sizeof(struct cpuidle_state));

	for (i = 0; i < drv->state_count; i++) {
		memcpy(&drv->states[i], &tc2_cpuidle_set[i],
				sizeof(struct cpuidle_state));
	}

	cpuidle_register_driver(drv);

	for_each_cpu(cpu_id, cpu_online_mask) {
		pr_err("CPUidle for CPU%d registered\n", cpu_id);
		dev = &per_cpu(tc2_idle_dev, cpu_id);
		dev->cpu = cpu_id;
		dev->safe_state_index = 0;

		cpumask_copy(&dev->coupled_cpus,
				topology_core_cpumask(cpu_id));
		dev->state_count = drv->state_count;

		if (cpuidle_register_device(dev)) {
			printk(KERN_ERR "%s: Cpuidle register device failed\n",
			       __func__);
			return -EIO;
		}
	}

	idle_debug = debugfs_create_dir("idle_debug", NULL);

	if (IS_ERR_OR_NULL(idle_debug)) {
		printk(KERN_INFO "Error in creating idle debugfs directory\n");
		return 0;
	}

	file_debug = debugfs_create_file("enable_idle", S_IRUGO | S_IWGRP,
				   idle_debug, NULL, &idle_debug_fops);

	if (IS_ERR_OR_NULL(file_debug)) {
		printk(KERN_INFO "Error in creating enable_idle file\n");
		return 0;
	}

	file_debug = debugfs_create_file("enable_mask", S_IRUGO | S_IWGRP,
					idle_debug, NULL, &cpuidle_fops);

	if (IS_ERR_OR_NULL(file_debug))
		printk(KERN_INFO "Error in creating enable_mask file\n");

	/* enable all wake-up IRQs by default */
	vexpress_spc_set_wake_intr(0x7ff);
	vexpress_flags_set(virt_to_phys(tc2_cpu_resume));

	/*
	 * Enable idle by default for all possible clusters.
	 * This must be done after all other setup to prevent the 
	 * possibility of clusters being powered down before they
	 * are fully configured.
	 */
	for (i = 0; i < NR_CLUSTERS; i++)
		cpumask_set_cpu(i, &cluster_mask);

	return 0;
}

late_initcall(tc2_idle_init);
