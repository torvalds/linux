/*
 * x86 APERF/MPERF KHz calculation for
 * /sys/.../cpufreq/scaling_cur_freq
 *
 * Copyright (C) 2017 Intel Corp.
 * Author: Len Brown <len.brown@intel.com>
 *
 * This file is licensed under GPLv2.
 */

#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/percpu.h>
#include <linux/smp.h>

struct aperfmperf_sample {
	unsigned int	khz;
	ktime_t	time;
	u64	aperf;
	u64	mperf;
};

static DEFINE_PER_CPU(struct aperfmperf_sample, samples);

#define APERFMPERF_CACHE_THRESHOLD_MS	10
#define APERFMPERF_REFRESH_DELAY_MS	20
#define APERFMPERF_STALE_THRESHOLD_MS	1000

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
	ktime_t now = ktime_get();
	s64 time_delta = ktime_ms_delta(now, s->time);

	/* Don't bother re-computing within the cache threshold time. */
	if (time_delta < APERFMPERF_CACHE_THRESHOLD_MS)
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

	s->time = now;
	s->aperf = aperf;
	s->mperf = mperf;

	/* If the previous iteration was too long ago, discard it. */
	if (time_delta > APERFMPERF_STALE_THRESHOLD_MS)
		s->khz = 0;
	else
		s->khz = div64_u64((cpu_khz * aperf_delta), mperf_delta);
}

unsigned int arch_freq_get_on_cpu(int cpu)
{
	unsigned int khz;

	if (!cpu_khz)
		return 0;

	if (!static_cpu_has(X86_FEATURE_APERFMPERF))
		return 0;

	smp_call_function_single(cpu, aperfmperf_snapshot_khz, NULL, 1);
	khz = per_cpu(samples.khz, cpu);
	if (khz)
		return khz;

	msleep(APERFMPERF_REFRESH_DELAY_MS);
	smp_call_function_single(cpu, aperfmperf_snapshot_khz, NULL, 1);

	return per_cpu(samples.khz, cpu);
}
