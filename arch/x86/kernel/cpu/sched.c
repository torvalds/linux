#include <linux/sched.h>
#include <linux/math64.h>
#include <linux/percpu.h>
#include <linux/irqflags.h>

#include <asm/cpufeature.h>
#include <asm/processor.h>

#ifdef CONFIG_SMP

static DEFINE_PER_CPU(struct aperfmperf, old_perf_sched);

static unsigned long scale_aperfmperf(void)
{
	struct aperfmperf val, *old = &__get_cpu_var(old_perf_sched);
	unsigned long ratio, flags;

	local_irq_save(flags);
	get_aperfmperf(&val);
	local_irq_restore(flags);

	ratio = calc_aperfmperf_ratio(old, &val);
	*old = val;

	return ratio;
}

unsigned long arch_scale_freq_power(struct sched_domain *sd, int cpu)
{
	/*
	 * do aperf/mperf on the cpu level because it includes things
	 * like turbo mode, which are relevant to full cores.
	 */
	if (boot_cpu_has(X86_FEATURE_APERFMPERF))
		return scale_aperfmperf();

	/*
	 * maybe have something cpufreq here
	 */

	return default_scale_freq_power(sd, cpu);
}

unsigned long arch_scale_smt_power(struct sched_domain *sd, int cpu)
{
	/*
	 * aperf/mperf already includes the smt gain
	 */
	if (boot_cpu_has(X86_FEATURE_APERFMPERF))
		return SCHED_LOAD_SCALE;

	return default_scale_smt_power(sd, cpu);
}

#endif
