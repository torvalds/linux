/*
 * x86 APERF/MPERF KHz calculation for
 * /sys/.../cpufreq/scaling_cur_freq
 *
 * Copyright (C) 2017 Intel Corp.
 * Author: Len Brown <len.brown@intel.com>
 *
 * This file is licensed under GPLv2.
 */

#include <linux/jiffies.h>
#include <linux/math64.h>
#include <linux/percpu.h>
#include <linux/smp.h>

struct aperfmperf_sample {
	unsigned int	khz;
	unsigned long	jiffies;
	u64	aperf;
	u64	mperf;
};

static DEFINE_PER_CPU(struct aperfmperf_sample, samples);

/*
 * aperfmperf_snapshot_khz()
 * On the current CPU, snapshot APERF, MPERF, and jiffies
 * unless we already did it within 10ms
 * calculate kHz, save snapshot
 */
static void aperfmperf_snapshot_khz(void *dummy)
{
	u64 aperf, aperf_delta;
	u64 mperf, mperf_delta;
	struct aperfmperf_sample *s = this_cpu_ptr(&samples);

	/* Don't bother re-computing within 10 ms */
	if (time_before(jiffies, s->jiffies + HZ/100))
		return;

	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);

	aperf_delta = aperf - s->aperf;
	mperf_delta = mperf - s->mperf;

	/*
	 * There is no architectural guarantee that MPERF
	 * increments faster than we can read it.
	 */
	if (mperf_delta == 0)
		return;

	/*
	 * if (cpu_khz * aperf_delta) fits into ULLONG_MAX, then
	 *	khz = (cpu_khz * aperf_delta) / mperf_delta
	 */
	if (div64_u64(ULLONG_MAX, cpu_khz) > aperf_delta)
		s->khz = div64_u64((cpu_khz * aperf_delta), mperf_delta);
	else	/* khz = aperf_delta / (mperf_delta / cpu_khz) */
		s->khz = div64_u64(aperf_delta,
			div64_u64(mperf_delta, cpu_khz));
	s->jiffies = jiffies;
	s->aperf = aperf;
	s->mperf = mperf;
}

unsigned int arch_freq_get_on_cpu(int cpu)
{
	if (!cpu_khz)
		return 0;

	if (!static_cpu_has(X86_FEATURE_APERFMPERF))
		return 0;

	smp_call_function_single(cpu, aperfmperf_snapshot_khz, NULL, 1);

	return per_cpu(samples.khz, cpu);
}
