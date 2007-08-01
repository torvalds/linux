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
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/mc146818rtc.h>

#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/hardirq.h>
#include <asm/irq.h>
#include <asm/div64.h>
#include <asm/cpu.h>
#include <asm/time.h>
#include <asm/mc146818-time.h>
#include <asm/msc01_ic.h>

#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/prom.h>

#ifdef CONFIG_MIPS_ATLAS
#include <asm/mips-boards/atlasint.h>
#endif
#ifdef CONFIG_MIPS_MALTA
#include <asm/mips-boards/maltaint.h>
#endif
#ifdef CONFIG_MIPS_SEAD
#include <asm/mips-boards/seadint.h>
#endif

unsigned long cpu_khz;

static int mips_cpu_timer_irq;
extern int cp0_perfcount_irq;
extern void smtc_timer_broadcast(void);

static void mips_timer_dispatch(void)
{
	do_IRQ(mips_cpu_timer_irq);
}

static void mips_perf_dispatch(void)
{
	do_IRQ(cp0_perfcount_irq);
}

/*
 * Redeclare until I get around mopping the timer code insanity on MIPS.
 */
extern int null_perf_irq(void);

extern int (*perf_irq)(void);

/*
 * Possibly handle a performance counter interrupt.
 * Return true if the timer interrupt should not be checked
 */
static inline int handle_perf_irq (int r2)
{
	/*
	 * The performance counter overflow interrupt may be shared with the
	 * timer interrupt (cp0_perfcount_irq < 0). If it is and a
	 * performance counter has overflowed (perf_irq() == IRQ_HANDLED)
	 * and we can't reliably determine if a counter interrupt has also
	 * happened (!r2) then don't check for a timer interrupt.
	 */
	return (cp0_perfcount_irq < 0) &&
		perf_irq() == IRQ_HANDLED &&
		!r2;
}

irqreturn_t mips_timer_interrupt(int irq, void *dev_id)
{
	int cpu = smp_processor_id();

#ifdef CONFIG_MIPS_MT_SMTC
	/*
	 *  In an SMTC system, one Count/Compare set exists per VPE.
	 *  Which TC within a VPE gets the interrupt is essentially
	 *  random - we only know that it shouldn't be one with
	 *  IXMT set. Whichever TC gets the interrupt needs to
	 *  send special interprocessor interrupts to the other
	 *  TCs to make sure that they schedule, etc.
	 *
	 *  That code is specific to the SMTC kernel, not to
	 *  the a particular platform, so it's invoked from
	 *  the general MIPS timer_interrupt routine.
	 */

	/*
	 * We could be here due to timer interrupt,
	 * perf counter overflow, or both.
	 */
	(void) handle_perf_irq(1);

	if (read_c0_cause() & (1 << 30)) {
		/*
		 * There are things we only want to do once per tick
		 * in an "MP" system.   One TC of each VPE will take
		 * the actual timer interrupt.  The others will get
		 * timer broadcast IPIs. We use whoever it is that takes
		 * the tick on VPE 0 to run the full timer_interrupt().
		 */
		if (cpu_data[cpu].vpe_id == 0) {
			timer_interrupt(irq, NULL);
		} else {
			write_c0_compare(read_c0_count() +
			                 (mips_hpt_frequency/HZ));
			local_timer_interrupt(irq, dev_id);
		}
		smtc_timer_broadcast();
	}
#else /* CONFIG_MIPS_MT_SMTC */
	int r2 = cpu_has_mips_r2;

	if (handle_perf_irq(r2))
		goto out;

	if (r2 && ((read_c0_cause() & (1 << 30)) == 0))
		goto out;

	if (cpu == 0) {
		/*
		 * CPU 0 handles the global timer interrupt job and process
		 * accounting resets count/compare registers to trigger next
		 * timer int.
		 */
		timer_interrupt(irq, NULL);
	} else {
		/* Everyone else needs to reset the timer int here as
		   ll_local_timer_interrupt doesn't */
		/*
		 * FIXME: need to cope with counter underflow.
		 * More support needs to be added to kernel/time for
		 * counter/timer interrupts on multiple CPU's
		 */
		write_c0_compare(read_c0_count() + (mips_hpt_frequency/HZ));

		/*
		 * Other CPUs should do profiling and process accounting
		 */
		local_timer_interrupt(irq, dev_id);
	}
out:
#endif /* CONFIG_MIPS_MT_SMTC */
	return IRQ_HANDLED;
}

/*
 * Estimate CPU frequency.  Sets mips_hpt_frequency as a side-effect
 */
static unsigned int __init estimate_cpu_frequency(void)
{
	unsigned int prid = read_c0_prid() & 0xffff00;
	unsigned int count;

#if defined(CONFIG_MIPS_SEAD) || defined(CONFIG_MIPS_SIM)
	/*
	 * The SEAD board doesn't have a real time clock, so we can't
	 * really calculate the timer frequency
	 * For now we hardwire the SEAD board frequency to 12MHz.
	 */

	if ((prid == (PRID_COMP_MIPS | PRID_IMP_20KC)) ||
	    (prid == (PRID_COMP_MIPS | PRID_IMP_25KF)))
		count = 12000000;
	else
		count = 6000000;
#endif
#if defined(CONFIG_MIPS_ATLAS) || defined(CONFIG_MIPS_MALTA)
	unsigned long flags;
	unsigned int start;

	local_irq_save(flags);

	/* Start counter exactly on falling edge of update flag */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));

	/* Start r4k counter. */
	start = read_c0_count();

	/* Read counter exactly on falling edge of update flag */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));

	count = read_c0_count() - start;

	/* restore interrupts */
	local_irq_restore(flags);
#endif

	mips_hpt_frequency = count;
	if ((prid != (PRID_COMP_MIPS | PRID_IMP_20KC)) &&
	    (prid != (PRID_COMP_MIPS | PRID_IMP_25KF)))
		count *= 2;

	count += 5000;    /* round */
	count -= count%10000;

	return count;
}

unsigned long __init mips_rtc_get_time(void)
{
	return mc146818_get_cmos_time();
}

void __init mips_time_init(void)
{
	unsigned int est_freq;

        /* Set Data mode - binary. */
        CMOS_WRITE(CMOS_READ(RTC_CONTROL) | RTC_DM_BINARY, RTC_CONTROL);

	est_freq = estimate_cpu_frequency ();

	printk("CPU frequency %d.%02d MHz\n", est_freq/1000000,
	       (est_freq%1000000)*100/1000000);

        cpu_khz = est_freq / 1000;

	mips_scroll_message();
}

irqreturn_t mips_perf_interrupt(int irq, void *dev_id)
{
	return perf_irq();
}

static struct irqaction perf_irqaction = {
	.handler = mips_perf_interrupt,
	.flags = IRQF_DISABLED | IRQF_PERCPU,
	.name = "performance",
};

void __init plat_perf_setup(struct irqaction *irq)
{
	cp0_perfcount_irq = -1;

#ifdef MSC01E_INT_BASE
	if (cpu_has_veic) {
		set_vi_handler (MSC01E_INT_PERFCTR, mips_perf_dispatch);
		cp0_perfcount_irq = MSC01E_INT_BASE + MSC01E_INT_PERFCTR;
	} else
#endif
	if (cp0_perfcount_irq >= 0) {
		if (cpu_has_vint)
			set_vi_handler(cp0_perfcount_irq, mips_perf_dispatch);
#ifdef CONFIG_MIPS_MT_SMTC
		setup_irq_smtc(cp0_perfcount_irq, irq,
		               0x100 << cp0_perfcount_irq);
#else
		setup_irq(cp0_perfcount_irq, irq);
#endif /* CONFIG_MIPS_MT_SMTC */
#ifdef CONFIG_SMP
		set_irq_handler(cp0_perfcount_irq, handle_percpu_irq);
#endif
	}
}

void __init plat_timer_setup(struct irqaction *irq)
{
#ifdef MSC01E_INT_BASE
	if (cpu_has_veic) {
		set_vi_handler (MSC01E_INT_CPUCTR, mips_timer_dispatch);
		mips_cpu_timer_irq = MSC01E_INT_BASE + MSC01E_INT_CPUCTR;
	}
	else
#endif
	{
		if (cpu_has_vint)
			set_vi_handler(cp0_compare_irq, mips_timer_dispatch);
		mips_cpu_timer_irq = MIPS_CPU_IRQ_BASE + cp0_compare_irq;
	}

	/* we are using the cpu counter for timer interrupts */
	irq->handler = mips_timer_interrupt;	/* we use our own handler */
#ifdef CONFIG_MIPS_MT_SMTC
	setup_irq_smtc(mips_cpu_timer_irq, irq, 0x100 << cp0_compare_irq);
#else
	setup_irq(mips_cpu_timer_irq, irq);
#endif /* CONFIG_MIPS_MT_SMTC */
#ifdef CONFIG_SMP
	set_irq_handler(mips_cpu_timer_irq, handle_percpu_irq);
#endif

	plat_perf_setup(&perf_irqaction);
}
