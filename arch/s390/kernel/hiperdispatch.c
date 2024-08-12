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
#include <linux/kernel_stat.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <asm/hiperdispatch.h>
#include <asm/smp.h>
#include <asm/topology.h>

#define HD_DELAY_FACTOR			(4)
#define HD_DELAY_INTERVAL		(HZ / 4)
#define HD_STEAL_THRESHOLD		30

static cpumask_t hd_vl_coremask;	/* Mask containing all vertical low COREs */
static cpumask_t hd_vmvl_cpumask;	/* Mask containing vertical medium and low CPUs */
static int hd_high_capacity_cores;	/* Current CORE count with high capacity */
static int hd_entitled_cores;		/* Total vertical high and medium CORE count */
static int hd_online_cores;		/* Current online CORE count */

static unsigned long hd_previous_steal;	/* Previous iteration's CPU steal timer total */

static void hd_capacity_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(hd_capacity_work, hd_capacity_work_fn);

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
	if (hd_entitled_cores == 0)
		return 0;
	if (hd_online_cores <= hd_entitled_cores)
		return 0;
	mod_delayed_work(system_wq, &hd_capacity_work, HD_DELAY_INTERVAL * HD_DELAY_FACTOR);
	hd_update_capacities();
	return 1;
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
	steal_percentage = hd_calculate_steal_percentage();
	if (steal_percentage < HD_STEAL_THRESHOLD)
		new_cores = hd_online_cores;
	else
		new_cores = hd_entitled_cores;
	if (hd_high_capacity_cores != new_cores) {
		hd_high_capacity_cores = new_cores;
		topology_schedule_update();
	}
	mutex_unlock(&smp_cpu_state_mutex);
	schedule_delayed_work(&hd_capacity_work, HD_DELAY_INTERVAL);
}
