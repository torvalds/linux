/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Setting up the clock on the MIPS boards.
 */
#include <linux/types.h>
#include <linux/i8253.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/timex.h>
#include <linux/mc146818rtc.h>

#include <asm/cpu.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/hardirq.h>
#include <asm/irq.h>
#include <asm/div64.h>
#include <asm/setup.h>
#include <asm/time.h>
#include <asm/mc146818-time.h>
#include <asm/msc01_ic.h>

#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/maltaint.h>

static int mips_cpu_timer_irq;
static int mips_cpu_perf_irq;
extern int cp0_perfcount_irq;

static unsigned int gic_frequency;

static void mips_timer_dispatch(void)
{
	do_IRQ(mips_cpu_timer_irq);
}

static void mips_perf_dispatch(void)
{
	do_IRQ(mips_cpu_perf_irq);
}

static unsigned int freqround(unsigned int freq, unsigned int amount)
{
	freq += amount;
	freq -= freq % (amount*2);
	return freq;
}

/*
 * Estimate CPU and GIC frequencies.
 */
static void __init estimate_frequencies(void)
{
	unsigned long flags;
	unsigned int count, start;
	cycle_t giccount = 0, gicstart = 0;

#if defined(CONFIG_KVM_GUEST) && CONFIG_KVM_GUEST_TIMER_FREQ
	mips_hpt_frequency = CONFIG_KVM_GUEST_TIMER_FREQ * 1000000;
	return;
#endif

	local_irq_save(flags);

	/* Start counter exactly on falling edge of update flag. */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));

	/* Initialize counters. */
	start = read_c0_count();
	if (gic_present) {
		gic_start_count();
		gicstart = gic_read_count();
	}

	/* Read counter exactly on falling edge of update flag. */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));

	count = read_c0_count();
	if (gic_present)
		giccount = gic_read_count();

	local_irq_restore(flags);

	count -= start;
	mips_hpt_frequency = count;

	if (gic_present) {
		giccount -= gicstart;
		gic_frequency = giccount;
	}
}

void read_persistent_clock(struct timespec *ts)
{
	ts->tv_sec = mc146818_get_cmos_time();
	ts->tv_nsec = 0;
}

int get_c0_fdc_int(void)
{
	int mips_cpu_fdc_irq;

	if (cpu_has_veic)
		mips_cpu_fdc_irq = -1;
	else if (gic_present)
		mips_cpu_fdc_irq = gic_get_c0_fdc_int();
	else if (cp0_fdc_irq >= 0)
		mips_cpu_fdc_irq = MIPS_CPU_IRQ_BASE + cp0_fdc_irq;
	else
		mips_cpu_fdc_irq = -1;

	return mips_cpu_fdc_irq;
}

int get_c0_perfcount_int(void)
{
	if (cpu_has_veic) {
		set_vi_handler(MSC01E_INT_PERFCTR, mips_perf_dispatch);
		mips_cpu_perf_irq = MSC01E_INT_BASE + MSC01E_INT_PERFCTR;
	} else if (gic_present) {
		mips_cpu_perf_irq = gic_get_c0_perfcount_int();
	} else if (cp0_perfcount_irq >= 0) {
		mips_cpu_perf_irq = MIPS_CPU_IRQ_BASE + cp0_perfcount_irq;
	} else {
		mips_cpu_perf_irq = -1;
	}

	return mips_cpu_perf_irq;
}
EXPORT_SYMBOL_GPL(get_c0_perfcount_int);

unsigned int get_c0_compare_int(void)
{
	if (cpu_has_veic) {
		set_vi_handler(MSC01E_INT_CPUCTR, mips_timer_dispatch);
		mips_cpu_timer_irq = MSC01E_INT_BASE + MSC01E_INT_CPUCTR;
	} else if (gic_present) {
		mips_cpu_timer_irq = gic_get_c0_compare_int();
	} else {
		mips_cpu_timer_irq = MIPS_CPU_IRQ_BASE + cp0_compare_irq;
	}

	return mips_cpu_timer_irq;
}

static void __init init_rtc(void)
{
	unsigned char freq, ctrl;

	/* Set 32KHz time base if not already set */
	freq = CMOS_READ(RTC_FREQ_SELECT);
	if ((freq & RTC_DIV_CTL) != RTC_REF_CLCK_32KHZ)
		CMOS_WRITE(RTC_REF_CLCK_32KHZ, RTC_FREQ_SELECT);

	/* Ensure SET bit is clear so RTC can run */
	ctrl = CMOS_READ(RTC_CONTROL);
	if (ctrl & RTC_SET)
		CMOS_WRITE(ctrl & ~RTC_SET, RTC_CONTROL);
}

void __init plat_time_init(void)
{
	unsigned int prid = read_c0_prid() & (PRID_COMP_MASK | PRID_IMP_MASK);
	unsigned int freq;

	init_rtc();
	estimate_frequencies();

	freq = mips_hpt_frequency;
	if ((prid != (PRID_COMP_MIPS | PRID_IMP_20KC)) &&
	    (prid != (PRID_COMP_MIPS | PRID_IMP_25KF)))
		freq *= 2;
	freq = freqround(freq, 5000);
	printk("CPU frequency %d.%02d MHz\n", freq/1000000,
	       (freq%1000000)*100/1000000);

	mips_scroll_message();

#ifdef CONFIG_I8253
	/* Only Malta has a PIT. */
	setup_pit_timer();
#endif

#ifdef CONFIG_MIPS_GIC
	if (gic_present) {
		freq = freqround(gic_frequency, 5000);
		printk("GIC frequency %d.%02d MHz\n", freq/1000000,
		       (freq%1000000)*100/1000000);
#ifdef CONFIG_CLKSRC_MIPS_GIC
		gic_clocksource_init(gic_frequency);
#endif
	}
#endif
}
