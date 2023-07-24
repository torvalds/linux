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
