/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>

#include <asm/cpu.h>
#include <asm/setup.h>
#include <asm/time.h>
#include <asm/irq.h>
#include <asm/mips-boards/generic.h>

unsigned long cpu_khz;

static int mips_cpu_timer_irq;
static int mips_cpu_perf_irq;

static void mips_timer_dispatch(void)
{
	do_IRQ(mips_cpu_timer_irq);
}

static void mips_perf_dispatch(void)
{
	do_IRQ(mips_cpu_perf_irq);
}

static void __iomem *status_reg = (void __iomem *)0xbf000410;

/*
 * Estimate CPU frequency.  Sets mips_hpt_frequency as a side-effect.
 */
static unsigned int __init estimate_cpu_frequency(void)
{
	unsigned int prid = read_c0_prid() & (PRID_COMP_MASK | PRID_IMP_MASK);
	unsigned int tick = 0;
	unsigned int freq;
	unsigned int orig;
	unsigned long flags;

	local_irq_save(flags);

	orig = readl(status_reg) & 0x2;		      /* get original sample */
	/* wait for transition */
	while ((readl(status_reg) & 0x2) == orig)
		;
	orig = orig ^ 0x2;			      /* flip the bit */

	write_c0_count(0);

	/* wait 1 second (the sampling clock transitions every 10ms) */
	while (tick < 100) {
		/* wait for transition */
		while ((readl(status_reg) & 0x2) == orig)
			;
		orig = orig ^ 0x2;			      /* flip the bit */
		tick++;
	}

	freq = read_c0_count();

	local_irq_restore(flags);

	mips_hpt_frequency = freq;

	/* Adjust for processor */
	if ((prid != (PRID_COMP_MIPS | PRID_IMP_20KC)) &&
		(prid != (PRID_COMP_MIPS | PRID_IMP_25KF)))
		freq *= 2;

	freq += 5000;	     /* rounding */
	freq -= freq%10000;

	return freq ;
}

void read_persistent_clock(struct timespec *ts)
{
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
}

static void __init plat_perf_setup(void)
{
	if (cp0_perfcount_irq >= 0) {
		if (cpu_has_vint)
			set_vi_handler(cp0_perfcount_irq, mips_perf_dispatch);
		mips_cpu_perf_irq = MIPS_CPU_IRQ_BASE + cp0_perfcount_irq;
	}
}

unsigned int get_c0_compare_int(void)
{
	if (cpu_has_vint)
		set_vi_handler(cp0_compare_irq, mips_timer_dispatch);
	mips_cpu_timer_irq = MIPS_CPU_IRQ_BASE + cp0_compare_irq;
	return mips_cpu_timer_irq;
}

void __init plat_time_init(void)
{
	unsigned int est_freq;

	est_freq = estimate_cpu_frequency();

	pr_debug("CPU frequency %d.%02d MHz\n", (est_freq / 1000000),
		(est_freq % 1000000) * 100 / 1000000);

	cpu_khz = est_freq / 1000;

	mips_scroll_message();

	plat_perf_setup();
}
