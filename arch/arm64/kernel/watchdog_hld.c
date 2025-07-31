// SPDX-License-Identifier: GPL-2.0
#include <linux/nmi.h>
#include <linux/cpufreq.h>
#include <linux/perf/arm_pmu.h>

/*
 * Safe maximum CPU frequency in case a particular platform doesn't implement
 * cpufreq driver. Although, architecture doesn't put any restrictions on
 * maximum frequency but 5 GHz seems to be safe maximum given the available
 * Arm CPUs in the market which are clocked much less than 5 GHz. On the other
 * hand, we can't make it much higher as it would lead to a large hard-lockup
 * detection timeout on parts which are running slower (eg. 1GHz on
 * Developerbox) and doesn't possess a cpufreq driver.
 */
#define SAFE_MAX_CPU_FREQ	5000000000UL // 5 GHz
u64 hw_nmi_get_sample_period(int watchdog_thresh)
{
	unsigned int cpu = smp_processor_id();
	unsigned long max_cpu_freq;

	max_cpu_freq = cpufreq_get_hw_max_freq(cpu) * 1000UL;
	if (!max_cpu_freq)
		max_cpu_freq = SAFE_MAX_CPU_FREQ;

	return (u64)max_cpu_freq * watchdog_thresh;
}

bool __init arch_perf_nmi_is_available(void)
{
	/*
	 * hardlockup_detector_perf_init() will success even if Pseudo-NMI turns off,
	 * however, the pmu interrupts will act like a normal interrupt instead of
	 * NMI and the hardlockup detector would be broken.
	 */
	return arm_pmu_irq_is_nmi();
}

static int watchdog_perf_update_period(void *data)
{
	int cpu = smp_processor_id();
	u64 max_cpu_freq, new_period;

	max_cpu_freq = cpufreq_get_hw_max_freq(cpu) * 1000UL;
	if (!max_cpu_freq)
		return 0;

	new_period = watchdog_thresh * max_cpu_freq;
	hardlockup_detector_perf_adjust_period(new_period);

	return 0;
}

static int watchdog_freq_notifier_callback(struct notifier_block *nb,
					   unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;
	int cpu;

	if (val != CPUFREQ_CREATE_POLICY)
		return NOTIFY_DONE;

	/*
	 * Let each online CPU related to the policy update the period by their
	 * own. This will serialize with the framework on start/stop the lockup
	 * detector (softlockup_{start,stop}_all) and avoid potential race
	 * condition. Otherwise we may have below theoretical race condition:
	 * (core 0/1 share the same policy)
	 * [core 0]                      [core 1]
	 *                               hardlockup_detector_event_create()
	 *                                 hw_nmi_get_sample_period()
	 * (cpufreq registered, notifier callback invoked)
	 * watchdog_freq_notifier_callback()
	 *   watchdog_perf_update_period()
	 *   (since core 1's event's not yet created,
	 *    the period is not set)
	 *                                 perf_event_create_kernel_counter()
	 *                                 (event's period is SAFE_MAX_CPU_FREQ)
	 */
	for_each_cpu(cpu, policy->cpus)
		smp_call_on_cpu(cpu, watchdog_perf_update_period, NULL, false);

	return NOTIFY_DONE;
}

static struct notifier_block watchdog_freq_notifier = {
	.notifier_call = watchdog_freq_notifier_callback,
};

static int __init init_watchdog_freq_notifier(void)
{
	return cpufreq_register_notifier(&watchdog_freq_notifier,
					 CPUFREQ_POLICY_NOTIFIER);
}
core_initcall(init_watchdog_freq_notifier);
