// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2024
 */

#define KMSG_COMPONENT "hd"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

/*
 * Hiperdispatch:
 * Dynamically calculates the optimum number of high capacity COREs
 * by considering the state the system is in. When hiperdispatch decides
 * that a capacity update is necessary, it schedules a topology update.
 * During topology updates the CPU capacities are always re-adjusted.
 *
 * There is two places where CPU capacities are being accessed within
 * hiperdispatch.
 * -> hiperdispatch's reoccuring work function reads CPU capacities to
 *    determine high capacity CPU count.
 * -> during a topology update hiperdispatch's adjustment function
 *    updates CPU capacities.
 * These two can run on different CPUs in parallel which can cause
 * hiperdispatch to make wrong decisions. This can potentially cause
 * some overhead by leading to extra rebuild_sched_domains() calls
 * for correction. Access to capacities within hiperdispatch has to be
 * serialized to prevent the overhead.
 *
 * Hiperdispatch decision making revolves around steal time.
 * HD_STEAL_THRESHOLD value is taken as reference. Whenever steal time
 * crosses the threshold value hiperdispatch falls back to giving high
 * capacities to entitled CPUs. When steal time drops below the
 * threshold boundary, hiperdispatch utilizes all CPUs by giving all
 * of them high capacity.
 *
 * The theory behind HD_STEAL_THRESHOLD is related to the SMP thread
 * performance. Comparing the throughput of;
 * - single CORE, with N threads, running N tasks
 * - N separate COREs running N tasks,
 * using individual COREs for individual tasks yield better
 * performance. This performance difference is roughly ~30% (can change
 * between machine generations)
 *
 * Hiperdispatch tries to hint scheduler to use individual COREs for
 * each task, as long as steal time on those COREs are less than 30%,
 * therefore delaying the throughput loss caused by using SMP threads.
 */

#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel_stat.h>
#include <linux/kstrtox.h>
#include <linux/ktime.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <asm/hiperdispatch.h>
#include <asm/setup.h>
#include <asm/smp.h>
#include <asm/topology.h>

#define CREATE_TRACE_POINTS
#include <asm/trace/hiperdispatch.h>

#define HD_DELAY_FACTOR			(4)
#define HD_DELAY_INTERVAL		(HZ / 4)
#define HD_STEAL_THRESHOLD		30
#define HD_STEAL_AVG_WEIGHT		16

static cpumask_t hd_vl_coremask;	/* Mask containing all vertical low COREs */
static cpumask_t hd_vmvl_cpumask;	/* Mask containing vertical medium and low CPUs */
static int hd_high_capacity_cores;	/* Current CORE count with high capacity */
static int hd_entitled_cores;		/* Total vertical high and medium CORE count */
static int hd_online_cores;		/* Current online CORE count */

static unsigned long hd_previous_steal;	/* Previous iteration's CPU steal timer total */
static unsigned long hd_high_time;	/* Total time spent while all cpus have high capacity */
static unsigned long hd_low_time;	/* Total time spent while vl cpus have low capacity */
static atomic64_t hd_adjustments;	/* Total occurrence count of hiperdispatch adjustments */

static unsigned int hd_steal_threshold = HD_STEAL_THRESHOLD;
static unsigned int hd_delay_factor = HD_DELAY_FACTOR;
static int hd_enabled;

static void hd_capacity_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(hd_capacity_work, hd_capacity_work_fn);

static int hd_set_hiperdispatch_mode(int enable)
{
	if (!MACHINE_HAS_TOPOLOGY)
		enable = 0;
	if (hd_enabled == enable)
		return 0;
	hd_enabled = enable;
	return 1;
}

void hd_reset_state(void)
{
	cpumask_clear(&hd_vl_coremask);
	cpumask_clear(&hd_vmvl_cpumask);
	hd_entitled_cores = 0;
	hd_online_cores = 0;
}

void hd_add_core(int cpu)
{
	const struct cpumask *siblings;
	int polarization;

	hd_online_cores++;
	polarization = smp_cpu_get_polarization(cpu);
	siblings = topology_sibling_cpumask(cpu);
	switch (polarization) {
	case POLARIZATION_VH:
		hd_entitled_cores++;
		break;
	case POLARIZATION_VM:
		hd_entitled_cores++;
		cpumask_or(&hd_vmvl_cpumask, &hd_vmvl_cpumask, siblings);
		break;
	case POLARIZATION_VL:
		cpumask_set_cpu(cpu, &hd_vl_coremask);
		cpumask_or(&hd_vmvl_cpumask, &hd_vmvl_cpumask, siblings);
		break;
	}
}

/* Serialize update and read operations of debug counters. */
static DEFINE_MUTEX(hd_counter_mutex);

static void hd_update_times(void)
{
	static ktime_t prev;
	ktime_t now;

	/*
	 * Check if hiperdispatch is active, if not set the prev to 0.
	 * This way it is possible to differentiate the first update iteration after
	 * enabling hiperdispatch.
	 */
	if (hd_entitled_cores == 0 || hd_enabled == 0) {
		prev = ktime_set(0, 0);
		return;
	}
	now = ktime_get();
	if (ktime_after(prev, 0)) {
		if (hd_high_capacity_cores == hd_online_cores)
			hd_high_time += ktime_ms_delta(now, prev);
		else
			hd_low_time += ktime_ms_delta(now, prev);
	}
	prev = now;
}

static void hd_update_capacities(void)
{
	int cpu, upscaling_cores;
	unsigned long capacity;

	upscaling_cores = hd_high_capacity_cores - hd_entitled_cores;
	capacity = upscaling_cores > 0 ? CPU_CAPACITY_HIGH : CPU_CAPACITY_LOW;
	hd_high_capacity_cores = hd_entitled_cores;
	for_each_cpu(cpu, &hd_vl_coremask) {
		smp_set_core_capacity(cpu, capacity);
		if (capacity != CPU_CAPACITY_HIGH)
			continue;
		hd_high_capacity_cores++;
		upscaling_cores--;
		if (upscaling_cores == 0)
			capacity = CPU_CAPACITY_LOW;
	}
}

void hd_disable_hiperdispatch(void)
{
	cancel_delayed_work_sync(&hd_capacity_work);
	hd_high_capacity_cores = hd_online_cores;
	hd_previous_steal = 0;
}

int hd_enable_hiperdispatch(void)
{
	mutex_lock(&hd_counter_mutex);
	hd_update_times();
	mutex_unlock(&hd_counter_mutex);
	if (hd_enabled == 0)
		return 0;
	if (hd_entitled_cores == 0)
		return 0;
	if (hd_online_cores <= hd_entitled_cores)
		return 0;
	mod_delayed_work(system_wq, &hd_capacity_work, HD_DELAY_INTERVAL * hd_delay_factor);
	hd_update_capacities();
	return 1;
}

static unsigned long hd_steal_avg(unsigned long new)
{
	static unsigned long steal;

	steal = (steal * (HD_STEAL_AVG_WEIGHT - 1) + new) / HD_STEAL_AVG_WEIGHT;
	return steal;
}

static unsigned long hd_calculate_steal_percentage(void)
{
	unsigned long time_delta, steal_delta, steal, percentage;
	static ktime_t prev;
	int cpus, cpu;
	ktime_t now;

	cpus = 0;
	steal = 0;
	percentage = 0;
	for_each_cpu(cpu, &hd_vmvl_cpumask) {
		steal += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
		cpus++;
	}
	/*
	 * If there is no vertical medium and low CPUs steal time
	 * is 0 as vertical high CPUs shouldn't experience steal time.
	 */
	if (cpus == 0)
		return percentage;
	now = ktime_get();
	time_delta = ktime_to_ns(ktime_sub(now, prev));
	if (steal > hd_previous_steal && hd_previous_steal != 0) {
		steal_delta = (steal - hd_previous_steal) * 100 / time_delta;
		percentage = steal_delta / cpus;
	}
	hd_previous_steal = steal;
	prev = now;
	return percentage;
}

static void hd_capacity_work_fn(struct work_struct *work)
{
	unsigned long steal_percentage, new_cores;

	mutex_lock(&smp_cpu_state_mutex);
	/*
	 * If online cores are less or equal to entitled cores hiperdispatch
	 * does not need to make any adjustments, call a topology update to
	 * disable hiperdispatch.
	 * Normally this check is handled on topology update, but during cpu
	 * unhotplug, topology and cpu mask updates are done in reverse
	 * order, causing hd_enable_hiperdispatch() to get stale data.
	 */
	if (hd_online_cores <= hd_entitled_cores) {
		topology_schedule_update();
		mutex_unlock(&smp_cpu_state_mutex);
		return;
	}
	steal_percentage = hd_steal_avg(hd_calculate_steal_percentage());
	if (steal_percentage < hd_steal_threshold)
		new_cores = hd_online_cores;
	else
		new_cores = hd_entitled_cores;
	if (hd_high_capacity_cores != new_cores) {
		trace_s390_hd_rebuild_domains(hd_high_capacity_cores, new_cores);
		hd_high_capacity_cores = new_cores;
		atomic64_inc(&hd_adjustments);
		topology_schedule_update();
	}
	trace_s390_hd_work_fn(steal_percentage, hd_entitled_cores, hd_high_capacity_cores);
	mutex_unlock(&smp_cpu_state_mutex);
	schedule_delayed_work(&hd_capacity_work, HD_DELAY_INTERVAL);
}

static int hiperdispatch_ctl_handler(const struct ctl_table *ctl, int write,
				     void *buffer, size_t *lenp, loff_t *ppos)
{
	int hiperdispatch;
	int rc;
	struct ctl_table ctl_entry = {
		.procname	= ctl->procname,
		.data		= &hiperdispatch,
		.maxlen		= sizeof(int),
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	};

	hiperdispatch = hd_enabled;
	rc = proc_douintvec_minmax(&ctl_entry, write, buffer, lenp, ppos);
	if (rc < 0 || !write)
		return rc;
	mutex_lock(&smp_cpu_state_mutex);
	if (hd_set_hiperdispatch_mode(hiperdispatch))
		topology_schedule_update();
	mutex_unlock(&smp_cpu_state_mutex);
	return 0;
}

static const struct ctl_table hiperdispatch_ctl_table[] = {
	{
		.procname	= "hiperdispatch",
		.mode		= 0644,
		.proc_handler	= hiperdispatch_ctl_handler,
	},
};

static ssize_t hd_steal_threshold_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	return sysfs_emit(buf, "%u\n", hd_steal_threshold);
}

static ssize_t hd_steal_threshold_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t count)
{
	unsigned int val;
	int rc;

	rc = kstrtouint(buf, 0, &val);
	if (rc)
		return rc;
	if (val > 100)
		return -ERANGE;
	hd_steal_threshold = val;
	return count;
}

static DEVICE_ATTR_RW(hd_steal_threshold);

static ssize_t hd_delay_factor_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	return sysfs_emit(buf, "%u\n", hd_delay_factor);
}

static ssize_t hd_delay_factor_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t count)
{
	unsigned int val;
	int rc;

	rc = kstrtouint(buf, 0, &val);
	if (rc)
		return rc;
	if (!val)
		return -ERANGE;
	hd_delay_factor = val;
	return count;
}

static DEVICE_ATTR_RW(hd_delay_factor);

static struct attribute *hd_attrs[] = {
	&dev_attr_hd_steal_threshold.attr,
	&dev_attr_hd_delay_factor.attr,
	NULL,
};

static const struct attribute_group hd_attr_group = {
	.name  = "hiperdispatch",
	.attrs = hd_attrs,
};

static int hd_greedy_time_get(void *unused, u64 *val)
{
	mutex_lock(&hd_counter_mutex);
	hd_update_times();
	*val = hd_high_time;
	mutex_unlock(&hd_counter_mutex);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(hd_greedy_time_fops, hd_greedy_time_get, NULL, "%llu\n");

static int hd_conservative_time_get(void *unused, u64 *val)
{
	mutex_lock(&hd_counter_mutex);
	hd_update_times();
	*val = hd_low_time;
	mutex_unlock(&hd_counter_mutex);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(hd_conservative_time_fops, hd_conservative_time_get, NULL, "%llu\n");

static int hd_adjustment_count_get(void *unused, u64 *val)
{
	*val = atomic64_read(&hd_adjustments);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(hd_adjustments_fops, hd_adjustment_count_get, NULL, "%llu\n");

static void __init hd_create_debugfs_counters(void)
{
	struct dentry *dir;

	dir = debugfs_create_dir("hiperdispatch", arch_debugfs_dir);
	debugfs_create_file("conservative_time_ms", 0400, dir, NULL, &hd_conservative_time_fops);
	debugfs_create_file("greedy_time_ms", 0400, dir, NULL, &hd_greedy_time_fops);
	debugfs_create_file("adjustment_count", 0400, dir, NULL, &hd_adjustments_fops);
}

static void __init hd_create_attributes(void)
{
	struct device *dev;

	dev = bus_get_dev_root(&cpu_subsys);
	if (!dev)
		return;
	if (sysfs_create_group(&dev->kobj, &hd_attr_group))
		pr_warn("Unable to create hiperdispatch attribute group\n");
	put_device(dev);
}

static int __init hd_init(void)
{
	if (IS_ENABLED(CONFIG_HIPERDISPATCH_ON)) {
		hd_set_hiperdispatch_mode(1);
		topology_schedule_update();
	}
	if (!register_sysctl("s390", hiperdispatch_ctl_table))
		pr_warn("Failed to register s390.hiperdispatch sysctl attribute\n");
	hd_create_debugfs_counters();
	hd_create_attributes();
	return 0;
}
late_initcall(hd_init);
