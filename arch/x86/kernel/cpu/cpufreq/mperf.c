#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>

#include "mperf.h"

static DEFINE_PER_CPU(struct aperfmperf, acfreq_old_perf);

/* Called via smp_call_function_single(), on the target CPU */
static void read_measured_perf_ctrs(void *_cur)
{
	struct aperfmperf *am = _cur;

	get_aperfmperf(am);
}

/*
 * Return the measured active (C0) frequency on this CPU since last call
 * to this function.
 * Input: cpu number
 * Return: Average CPU frequency in terms of max frequency (zero on error)
 *
 * We use IA32_MPERF and IA32_APERF MSRs to get the measured performance
 * over a period of time, while CPU is in C0 state.
 * IA32_MPERF counts at the rate of max advertised frequency
 * IA32_APERF counts at the rate of actual CPU frequency
 * Only IA32_APERF/IA32_MPERF ratio is architecturally defined and
 * no meaning should be associated with absolute values of these MSRs.
 */
unsigned int cpufreq_get_measured_perf(struct cpufreq_policy *policy,
					unsigned int cpu)
{
	struct aperfmperf perf;
	unsigned long ratio;
	unsigned int retval;

	if (smp_call_function_single(cpu, read_measured_perf_ctrs, &perf, 1))
		return 0;

	ratio = calc_aperfmperf_ratio(&per_cpu(acfreq_old_perf, cpu), &perf);
	per_cpu(acfreq_old_perf, cpu) = perf;

	retval = (policy->cpuinfo.max_freq * ratio) >> APERFMPERF_SHIFT;

	return retval;
}
EXPORT_SYMBOL_GPL(cpufreq_get_measured_perf);
MODULE_LICENSE("GPL");
