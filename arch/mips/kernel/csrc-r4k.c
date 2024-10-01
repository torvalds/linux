/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 by Ralf Baechle
 */
#include <linux/clocksource.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/sched_clock.h>

#include <asm/time.h>

static u64 c0_hpt_read(struct clocksource *cs)
{
	return read_c0_count();
}

static struct clocksource clocksource_mips = {
	.name		= "MIPS",
	.read		= c0_hpt_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static u64 __maybe_unused notrace r4k_read_sched_clock(void)
{
	return read_c0_count();
}

static inline unsigned int rdhwr_count(void)
{
	unsigned int count;

	__asm__ __volatile__(
	"	.set push\n"
	"	.set mips32r2\n"
	"	rdhwr	%0, $2\n"
	"	.set pop\n"
	: "=r" (count));

	return count;
}

static bool rdhwr_count_usable(void)
{
	unsigned int prev, curr, i;

	/*
	 * Older QEMUs have a broken implementation of RDHWR for the CP0 count
	 * which always returns a constant value. Try to identify this and don't
	 * use it in the VDSO if it is broken. This workaround can be removed
	 * once the fix has been in QEMU stable for a reasonable amount of time.
	 */
	for (i = 0, prev = rdhwr_count(); i < 100; i++) {
		curr = rdhwr_count();

		if (curr != prev)
			return true;

		prev = curr;
	}

	pr_warn("Not using R4K clocksource in VDSO due to broken RDHWR\n");
	return false;
}

static inline __init bool count_can_be_sched_clock(void)
{
	if (IS_ENABLED(CONFIG_CPU_FREQ))
		return false;

	if (num_possible_cpus() > 1 &&
			!IS_ENABLED(CONFIG_HAVE_UNSTABLE_SCHED_CLOCK))
		return false;

	return true;
}

#ifdef CONFIG_CPU_FREQ

static bool __read_mostly r4k_clock_unstable;

static void r4k_clocksource_unstable(char *reason)
{
	if (r4k_clock_unstable)
		return;

	r4k_clock_unstable = true;

	pr_info("R4K timer is unstable due to %s\n", reason);

	clocksource_mark_unstable(&clocksource_mips);
}

static int r4k_cpufreq_callback(struct notifier_block *nb,
				unsigned long val, void *data)
{
	if (val == CPUFREQ_POSTCHANGE)
		r4k_clocksource_unstable("CPU frequency change");

	return 0;
}

static struct notifier_block r4k_cpufreq_notifier = {
	.notifier_call  = r4k_cpufreq_callback,
};

static int __init r4k_register_cpufreq_notifier(void)
{
	return cpufreq_register_notifier(&r4k_cpufreq_notifier,
					 CPUFREQ_TRANSITION_NOTIFIER);

}
core_initcall(r4k_register_cpufreq_notifier);

#endif /* !CONFIG_CPU_FREQ */

int __init init_r4k_clocksource(void)
{
	if (!cpu_has_counter || !mips_hpt_frequency)
		return -ENXIO;

	/* Calculate a somewhat reasonable rating value */
	clocksource_mips.rating = 200;
	clocksource_mips.rating += clamp(mips_hpt_frequency / 10000000, 0, 99);

	/*
	 * R2 onwards makes the count accessible to user mode so it can be used
	 * by the VDSO (HWREna is configured by configure_hwrena()).
	 */
	if (cpu_has_mips_r2_r6 && rdhwr_count_usable())
		clocksource_mips.vdso_clock_mode = VDSO_CLOCKMODE_R4K;

	clocksource_register_hz(&clocksource_mips, mips_hpt_frequency);

	if (count_can_be_sched_clock())
		sched_clock_register(r4k_read_sched_clock, 32, mips_hpt_frequency);

	return 0;
}
