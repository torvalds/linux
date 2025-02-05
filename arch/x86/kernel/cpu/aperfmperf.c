// SPDX-License-Identifier: GPL-2.0-only
/*
 * x86 APERF/MPERF KHz calculation for
 * /sys/.../cpufreq/scaling_cur_freq
 *
 * Copyright (C) 2017 Intel Corp.
 * Author: Len Brown <len.brown@intel.com>
 */
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/sched/isolation.h>
#include <linux/sched/topology.h>
#include <linux/smp.h>
#include <linux/syscore_ops.h>

#include <asm/cpu.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#include "cpu.h"

struct aperfmperf {
	seqcount_t	seq;
	unsigned long	last_update;
	u64		acnt;
	u64		mcnt;
	u64		aperf;
	u64		mperf;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct aperfmperf, cpu_samples) = {
	.seq = SEQCNT_ZERO(cpu_samples.seq)
};

static void init_counter_refs(void)
{
	u64 aperf, mperf;

	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);

	this_cpu_write(cpu_samples.aperf, aperf);
	this_cpu_write(cpu_samples.mperf, mperf);
}

#if defined(CONFIG_X86_64) && defined(CONFIG_SMP)
/*
 * APERF/MPERF frequency ratio computation.
 *
 * The scheduler wants to do frequency invariant accounting and needs a <1
 * ratio to account for the 'current' frequency, corresponding to
 * freq_curr / freq_max.
 *
 * Since the frequency freq_curr on x86 is controlled by micro-controller and
 * our P-state setting is little more than a request/hint, we need to observe
 * the effective frequency 'BusyMHz', i.e. the average frequency over a time
 * interval after discarding idle time. This is given by:
 *
 *   BusyMHz = delta_APERF / delta_MPERF * freq_base
 *
 * where freq_base is the max non-turbo P-state.
 *
 * The freq_max term has to be set to a somewhat arbitrary value, because we
 * can't know which turbo states will be available at a given point in time:
 * it all depends on the thermal headroom of the entire package. We set it to
 * the turbo level with 4 cores active.
 *
 * Benchmarks show that's a good compromise between the 1C turbo ratio
 * (freq_curr/freq_max would rarely reach 1) and something close to freq_base,
 * which would ignore the entire turbo range (a conspicuous part, making
 * freq_curr/freq_max always maxed out).
 *
 * An exception to the heuristic above is the Atom uarch, where we choose the
 * highest turbo level for freq_max since Atom's are generally oriented towards
 * power efficiency.
 *
 * Setting freq_max to anything less than the 1C turbo ratio makes the ratio
 * freq_curr / freq_max to eventually grow >1, in which case we clip it to 1.
 */

DEFINE_STATIC_KEY_FALSE(arch_scale_freq_key);

static u64 arch_turbo_freq_ratio = SCHED_CAPACITY_SCALE;
static u64 arch_max_freq_ratio = SCHED_CAPACITY_SCALE;

void arch_set_max_freq_ratio(bool turbo_disabled)
{
	arch_max_freq_ratio = turbo_disabled ? SCHED_CAPACITY_SCALE :
					arch_turbo_freq_ratio;
}
EXPORT_SYMBOL_GPL(arch_set_max_freq_ratio);

static bool __init turbo_disabled(void)
{
	u64 misc_en;
	int err;

	err = rdmsrl_safe(MSR_IA32_MISC_ENABLE, &misc_en);
	if (err)
		return false;

	return (misc_en & MSR_IA32_MISC_ENABLE_TURBO_DISABLE);
}

static bool __init slv_set_max_freq_ratio(u64 *base_freq, u64 *turbo_freq)
{
	int err;

	err = rdmsrl_safe(MSR_ATOM_CORE_RATIOS, base_freq);
	if (err)
		return false;

	err = rdmsrl_safe(MSR_ATOM_CORE_TURBO_RATIOS, turbo_freq);
	if (err)
		return false;

	*base_freq = (*base_freq >> 16) & 0x3F;     /* max P state */
	*turbo_freq = *turbo_freq & 0x3F;           /* 1C turbo    */

	return true;
}

#define X86_MATCH(vfm)						\
	X86_MATCH_VFM_FEATURE(vfm, X86_FEATURE_APERFMPERF, NULL)

static const struct x86_cpu_id has_knl_turbo_ratio_limits[] __initconst = {
	X86_MATCH(INTEL_XEON_PHI_KNL),
	X86_MATCH(INTEL_XEON_PHI_KNM),
	{}
};

static const struct x86_cpu_id has_skx_turbo_ratio_limits[] __initconst = {
	X86_MATCH(INTEL_SKYLAKE_X),
	{}
};

static const struct x86_cpu_id has_glm_turbo_ratio_limits[] __initconst = {
	X86_MATCH(INTEL_ATOM_GOLDMONT),
	X86_MATCH(INTEL_ATOM_GOLDMONT_D),
	X86_MATCH(INTEL_ATOM_GOLDMONT_PLUS),
	{}
};

static bool __init knl_set_max_freq_ratio(u64 *base_freq, u64 *turbo_freq,
					  int num_delta_fratio)
{
	int fratio, delta_fratio, found;
	int err, i;
	u64 msr;

	err = rdmsrl_safe(MSR_PLATFORM_INFO, base_freq);
	if (err)
		return false;

	*base_freq = (*base_freq >> 8) & 0xFF;	    /* max P state */

	err = rdmsrl_safe(MSR_TURBO_RATIO_LIMIT, &msr);
	if (err)
		return false;

	fratio = (msr >> 8) & 0xFF;
	i = 16;
	found = 0;
	do {
		if (found >= num_delta_fratio) {
			*turbo_freq = fratio;
			return true;
		}

		delta_fratio = (msr >> (i + 5)) & 0x7;

		if (delta_fratio) {
			found += 1;
			fratio -= delta_fratio;
		}

		i += 8;
	} while (i < 64);

	return true;
}

static bool __init skx_set_max_freq_ratio(u64 *base_freq, u64 *turbo_freq, int size)
{
	u64 ratios, counts;
	u32 group_size;
	int err, i;

	err = rdmsrl_safe(MSR_PLATFORM_INFO, base_freq);
	if (err)
		return false;

	*base_freq = (*base_freq >> 8) & 0xFF;      /* max P state */

	err = rdmsrl_safe(MSR_TURBO_RATIO_LIMIT, &ratios);
	if (err)
		return false;

	err = rdmsrl_safe(MSR_TURBO_RATIO_LIMIT1, &counts);
	if (err)
		return false;

	for (i = 0; i < 64; i += 8) {
		group_size = (counts >> i) & 0xFF;
		if (group_size >= size) {
			*turbo_freq = (ratios >> i) & 0xFF;
			return true;
		}
	}

	return false;
}

static bool __init core_set_max_freq_ratio(u64 *base_freq, u64 *turbo_freq)
{
	u64 msr;
	int err;

	err = rdmsrl_safe(MSR_PLATFORM_INFO, base_freq);
	if (err)
		return false;

	err = rdmsrl_safe(MSR_TURBO_RATIO_LIMIT, &msr);
	if (err)
		return false;

	*base_freq = (*base_freq >> 8) & 0xFF;    /* max P state */
	*turbo_freq = (msr >> 24) & 0xFF;         /* 4C turbo    */

	/* The CPU may have less than 4 cores */
	if (!*turbo_freq)
		*turbo_freq = msr & 0xFF;         /* 1C turbo    */

	return true;
}

static bool __init intel_set_max_freq_ratio(void)
{
	u64 base_freq, turbo_freq;
	u64 turbo_ratio;

	if (slv_set_max_freq_ratio(&base_freq, &turbo_freq))
		goto out;

	if (x86_match_cpu(has_glm_turbo_ratio_limits) &&
	    skx_set_max_freq_ratio(&base_freq, &turbo_freq, 1))
		goto out;

	if (x86_match_cpu(has_knl_turbo_ratio_limits) &&
	    knl_set_max_freq_ratio(&base_freq, &turbo_freq, 1))
		goto out;

	if (x86_match_cpu(has_skx_turbo_ratio_limits) &&
	    skx_set_max_freq_ratio(&base_freq, &turbo_freq, 4))
		goto out;

	if (core_set_max_freq_ratio(&base_freq, &turbo_freq))
		goto out;

	return false;

out:
	/*
	 * Some hypervisors advertise X86_FEATURE_APERFMPERF
	 * but then fill all MSR's with zeroes.
	 * Some CPUs have turbo boost but don't declare any turbo ratio
	 * in MSR_TURBO_RATIO_LIMIT.
	 */
	if (!base_freq || !turbo_freq) {
		pr_debug("Couldn't determine cpu base or turbo frequency, necessary for scale-invariant accounting.\n");
		return false;
	}

	turbo_ratio = div_u64(turbo_freq * SCHED_CAPACITY_SCALE, base_freq);
	if (!turbo_ratio) {
		pr_debug("Non-zero turbo and base frequencies led to a 0 ratio.\n");
		return false;
	}

	arch_turbo_freq_ratio = turbo_ratio;
	arch_set_max_freq_ratio(turbo_disabled());

	return true;
}

#ifdef CONFIG_PM_SLEEP
static struct syscore_ops freq_invariance_syscore_ops = {
	.resume = init_counter_refs,
};

static void register_freq_invariance_syscore_ops(void)
{
	register_syscore_ops(&freq_invariance_syscore_ops);
}
#else
static inline void register_freq_invariance_syscore_ops(void) {}
#endif

static void freq_invariance_enable(void)
{
	if (static_branch_unlikely(&arch_scale_freq_key)) {
		WARN_ON_ONCE(1);
		return;
	}
	static_branch_enable_cpuslocked(&arch_scale_freq_key);
	register_freq_invariance_syscore_ops();
	pr_info("Estimated ratio of average max frequency by base frequency (times 1024): %llu\n", arch_max_freq_ratio);
}

void freq_invariance_set_perf_ratio(u64 ratio, bool turbo_disabled)
{
	arch_turbo_freq_ratio = ratio;
	arch_set_max_freq_ratio(turbo_disabled);
	freq_invariance_enable();
}

static void __init bp_init_freq_invariance(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return;

	if (intel_set_max_freq_ratio()) {
		guard(cpus_read_lock)();
		freq_invariance_enable();
	}
}

static void disable_freq_invariance_workfn(struct work_struct *work)
{
	int cpu;

	static_branch_disable(&arch_scale_freq_key);

	/*
	 * Set arch_freq_scale to a default value on all cpus
	 * This negates the effect of scaling
	 */
	for_each_possible_cpu(cpu)
		per_cpu(arch_freq_scale, cpu) = SCHED_CAPACITY_SCALE;
}

static DECLARE_WORK(disable_freq_invariance_work,
		    disable_freq_invariance_workfn);

DEFINE_PER_CPU(unsigned long, arch_freq_scale) = SCHED_CAPACITY_SCALE;
EXPORT_PER_CPU_SYMBOL_GPL(arch_freq_scale);

static DEFINE_STATIC_KEY_FALSE(arch_hybrid_cap_scale_key);

struct arch_hybrid_cpu_scale {
	unsigned long capacity;
	unsigned long freq_ratio;
};

static struct arch_hybrid_cpu_scale __percpu *arch_cpu_scale;

/**
 * arch_enable_hybrid_capacity_scale() - Enable hybrid CPU capacity scaling
 *
 * Allocate memory for per-CPU data used by hybrid CPU capacity scaling,
 * initialize it and set the static key controlling its code paths.
 *
 * Must be called before arch_set_cpu_capacity().
 */
bool arch_enable_hybrid_capacity_scale(void)
{
	int cpu;

	if (static_branch_unlikely(&arch_hybrid_cap_scale_key)) {
		WARN_ONCE(1, "Hybrid CPU capacity scaling already enabled");
		return true;
	}

	arch_cpu_scale = alloc_percpu(struct arch_hybrid_cpu_scale);
	if (!arch_cpu_scale)
		return false;

	for_each_possible_cpu(cpu) {
		per_cpu_ptr(arch_cpu_scale, cpu)->capacity = SCHED_CAPACITY_SCALE;
		per_cpu_ptr(arch_cpu_scale, cpu)->freq_ratio = arch_max_freq_ratio;
	}

	static_branch_enable(&arch_hybrid_cap_scale_key);

	pr_info("Hybrid CPU capacity scaling enabled\n");

	return true;
}

/**
 * arch_set_cpu_capacity() - Set scale-invariance parameters for a CPU
 * @cpu: Target CPU.
 * @cap: Capacity of @cpu at its maximum frequency, relative to @max_cap.
 * @max_cap: System-wide maximum CPU capacity.
 * @cap_freq: Frequency of @cpu corresponding to @cap.
 * @base_freq: Frequency of @cpu at which MPERF counts.
 *
 * The units in which @cap and @max_cap are expressed do not matter, so long
 * as they are consistent, because the former is effectively divided by the
 * latter.  Analogously for @cap_freq and @base_freq.
 *
 * After calling this function for all CPUs, call arch_rebuild_sched_domains()
 * to let the scheduler know that capacity-aware scheduling can be used going
 * forward.
 */
void arch_set_cpu_capacity(int cpu, unsigned long cap, unsigned long max_cap,
			   unsigned long cap_freq, unsigned long base_freq)
{
	if (static_branch_likely(&arch_hybrid_cap_scale_key)) {
		WRITE_ONCE(per_cpu_ptr(arch_cpu_scale, cpu)->capacity,
			   div_u64(cap << SCHED_CAPACITY_SHIFT, max_cap));
		WRITE_ONCE(per_cpu_ptr(arch_cpu_scale, cpu)->freq_ratio,
			   div_u64(cap_freq << SCHED_CAPACITY_SHIFT, base_freq));
	} else {
		WARN_ONCE(1, "Hybrid CPU capacity scaling not enabled");
	}
}

unsigned long arch_scale_cpu_capacity(int cpu)
{
	if (static_branch_unlikely(&arch_hybrid_cap_scale_key))
		return READ_ONCE(per_cpu_ptr(arch_cpu_scale, cpu)->capacity);

	return SCHED_CAPACITY_SCALE;
}
EXPORT_SYMBOL_GPL(arch_scale_cpu_capacity);

static void scale_freq_tick(u64 acnt, u64 mcnt)
{
	u64 freq_scale, freq_ratio;

	if (!arch_scale_freq_invariant())
		return;

	if (check_shl_overflow(acnt, 2*SCHED_CAPACITY_SHIFT, &acnt))
		goto error;

	if (static_branch_unlikely(&arch_hybrid_cap_scale_key))
		freq_ratio = READ_ONCE(this_cpu_ptr(arch_cpu_scale)->freq_ratio);
	else
		freq_ratio = arch_max_freq_ratio;

	if (check_mul_overflow(mcnt, freq_ratio, &mcnt) || !mcnt)
		goto error;

	freq_scale = div64_u64(acnt, mcnt);
	if (!freq_scale)
		goto error;

	if (freq_scale > SCHED_CAPACITY_SCALE)
		freq_scale = SCHED_CAPACITY_SCALE;

	this_cpu_write(arch_freq_scale, freq_scale);
	return;

error:
	pr_warn("Scheduler frequency invariance went wobbly, disabling!\n");
	schedule_work(&disable_freq_invariance_work);
}
#else
static inline void bp_init_freq_invariance(void) { }
static inline void scale_freq_tick(u64 acnt, u64 mcnt) { }
#endif /* CONFIG_X86_64 && CONFIG_SMP */

void arch_scale_freq_tick(void)
{
	struct aperfmperf *s = this_cpu_ptr(&cpu_samples);
	u64 acnt, mcnt, aperf, mperf;

	if (!cpu_feature_enabled(X86_FEATURE_APERFMPERF))
		return;

	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);
	acnt = aperf - s->aperf;
	mcnt = mperf - s->mperf;

	s->aperf = aperf;
	s->mperf = mperf;

	raw_write_seqcount_begin(&s->seq);
	s->last_update = jiffies;
	s->acnt = acnt;
	s->mcnt = mcnt;
	raw_write_seqcount_end(&s->seq);

	scale_freq_tick(acnt, mcnt);
}

/*
 * Discard samples older than the define maximum sample age of 20ms. There
 * is no point in sending IPIs in such a case. If the scheduler tick was
 * not running then the CPU is either idle or isolated.
 */
#define MAX_SAMPLE_AGE	((unsigned long)HZ / 50)

unsigned int arch_freq_get_on_cpu(int cpu)
{
	struct aperfmperf *s = per_cpu_ptr(&cpu_samples, cpu);
	unsigned int seq, freq;
	unsigned long last;
	u64 acnt, mcnt;

	if (!cpu_feature_enabled(X86_FEATURE_APERFMPERF))
		goto fallback;

	do {
		seq = raw_read_seqcount_begin(&s->seq);
		last = s->last_update;
		acnt = s->acnt;
		mcnt = s->mcnt;
	} while (read_seqcount_retry(&s->seq, seq));

	/*
	 * Bail on invalid count and when the last update was too long ago,
	 * which covers idle and NOHZ full CPUs.
	 */
	if (!mcnt || (jiffies - last) > MAX_SAMPLE_AGE)
		goto fallback;

	return div64_u64((cpu_khz * acnt), mcnt);

fallback:
	freq = cpufreq_quick_get(cpu);
	return freq ? freq : cpu_khz;
}

static int __init bp_init_aperfmperf(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_APERFMPERF))
		return 0;

	init_counter_refs();
	bp_init_freq_invariance();
	return 0;
}
early_initcall(bp_init_aperfmperf);

void ap_init_aperfmperf(void)
{
	if (cpu_feature_enabled(X86_FEATURE_APERFMPERF))
		init_counter_refs();
}
