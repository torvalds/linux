// SPDX-License-Identifier: GPL-2.0-only
/*
 * x86 APERF/MPERF KHz calculation for
 * /sys/.../cpufreq/scaling_cur_freq
 *
 * Copyright (C) 2017 Intel Corp.
 * Author: Len Brown <len.brown@intel.com>
 */

#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/percpu.h>
#include <linux/cpufreq.h>
#include <linux/smp.h>
#include <linux/sched/isolation.h>
#include <linux/rcupdate.h>

#include "cpu.h"

struct aperfmperf_sample {
	unsigned int	khz;
	atomic_t	scfpending;
	ktime_t	time;
	u64	aperf;
	u64	mperf;
};

static DEFINE_PER_CPU(struct aperfmperf_sample, samples);

#define APERFMPERF_CACHE_THRESHOLD_MS	10
#define APERFMPERF_REFRESH_DELAY_MS	10
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
	unsigned long flags;

	local_irq_save(flags);
	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);
	local_irq_restore(flags);

	aperf_delta = aperf - s->aperf;
	mperf_delta = mperf - s->mperf;

	/*
	 * There is no architectural guarantee that MPERF
	 * increments faster than we can read it.
	 */
	if (mperf_delta == 0)
		return;

	s->time = ktime_get();
	s->aperf = aperf;
	s->mperf = mperf;
	s->khz = div64_u64((cpu_khz * aperf_delta), mperf_delta);
	atomic_set_release(&s->scfpending, 0);
}

static bool aperfmperf_snapshot_cpu(int cpu, ktime_t now, bool wait)
{
	s64 time_delta = ktime_ms_delta(now, per_cpu(samples.time, cpu));
	struct aperfmperf_sample *s = per_cpu_ptr(&samples, cpu);

	/* Don't bother re-computing within the cache threshold time. */
	if (time_delta < APERFMPERF_CACHE_THRESHOLD_MS)
		return true;

	if (!atomic_xchg(&s->scfpending, 1) || wait)
		smp_call_function_single(cpu, aperfmperf_snapshot_khz, NULL, wait);

	/* Return false if the previous iteration was too long ago. */
	return time_delta <= APERFMPERF_STALE_THRESHOLD_MS;
}

unsigned int aperfmperf_get_khz(int cpu)
{
	if (!cpu_khz)
		return 0;

	if (!boot_cpu_has(X86_FEATURE_APERFMPERF))
		return 0;

	if (!housekeeping_cpu(cpu, HK_TYPE_MISC))
		return 0;

	if (rcu_is_idle_cpu(cpu))
		return 0; /* Idle CPUs are completely uninteresting. */

	aperfmperf_snapshot_cpu(cpu, ktime_get(), true);
	return per_cpu(samples.khz, cpu);
}

void arch_freq_prepare_all(void)
{
	ktime_t now = ktime_get();
	bool wait = false;
	int cpu;

	if (!cpu_khz)
		return;

	if (!boot_cpu_has(X86_FEATURE_APERFMPERF))
		return;

	for_each_online_cpu(cpu) {
		if (!housekeeping_cpu(cpu, HK_TYPE_MISC))
			continue;
		if (rcu_is_idle_cpu(cpu))
			continue; /* Idle CPUs are completely uninteresting. */
		if (!aperfmperf_snapshot_cpu(cpu, now, false))
			wait = true;
	}

	if (wait)
		msleep(APERFMPERF_REFRESH_DELAY_MS);
}

unsigned int arch_freq_get_on_cpu(int cpu)
{
	struct aperfmperf_sample *s = per_cpu_ptr(&samples, cpu);

	if (!cpu_khz)
		return 0;

	if (!boot_cpu_has(X86_FEATURE_APERFMPERF))
		return 0;

	if (!housekeeping_cpu(cpu, HK_TYPE_MISC))
		return 0;

	if (aperfmperf_snapshot_cpu(cpu, ktime_get(), true))
		return per_cpu(samples.khz, cpu);

	msleep(APERFMPERF_REFRESH_DELAY_MS);
	atomic_set(&s->scfpending, 1);
	smp_mb(); /* ->scfpending before smp_call_function_single(). */
	smp_call_function_single(cpu, aperfmperf_snapshot_khz, NULL, 1);

	return per_cpu(samples.khz, cpu);
}
