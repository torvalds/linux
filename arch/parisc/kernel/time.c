/*
 *  linux/arch/parisc/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Modifications for ARM (C) 1994, 1995, 1996,1997 Russell King
 *  Copyright (C) 1999 SuSE GmbH, (Philipp Rumpf, prumpf@tux.org)
 *
 * 1994-07-02  Alan Modra
 *             fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1998-12-20  Updated NTP code according to technical memorandum Jan '96
 *             "A Kernel Model for Precision Timekeeping" by Dave Mills
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/sched.h>
#include <linux/sched_clock.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/profile.h>
#include <linux/clocksource.h>
#include <linux/platform_device.h>
#include <linux/ftrace.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/param.h>
#include <asm/pdc.h>
#include <asm/led.h>

#include <linux/timex.h>

static unsigned long clocktick __read_mostly;	/* timer cycles per tick */

/*
 * We keep time on PA-RISC Linux by using the Interval Timer which is
 * a pair of registers; one is read-only and one is write-only; both
 * accessed through CR16.  The read-only register is 32 or 64 bits wide,
 * and increments by 1 every CPU clock tick.  The architecture only
 * guarantees us a rate between 0.5 and 2, but all implementations use a
 * rate of 1.  The write-only register is 32-bits wide.  When the lowest
 * 32 bits of the read-only register compare equal to the write-only
 * register, it raises a maskable external interrupt.  Each processor has
 * an Interval Timer of its own and they are not synchronised.  
 *
 * We want to generate an interrupt every 1/HZ seconds.  So we program
 * CR16 to interrupt every @clocktick cycles.  The it_value in cpu_data
 * is programmed with the intended time of the next tick.  We can be
 * held off for an arbitrarily long period of time by interrupts being
 * disabled, so we may miss one or more ticks.
 */
irqreturn_t __irq_entry timer_interrupt(int irq, void *dev_id)
{
	unsigned long now, now2;
	unsigned long next_tick;
	unsigned long cycles_elapsed, ticks_elapsed = 1;
	unsigned long cycles_remainder;
	unsigned int cpu = smp_processor_id();
	struct cpuinfo_parisc *cpuinfo = &per_cpu(cpu_data, cpu);

	/* gcc can optimize for "read-only" case with a local clocktick */
	unsigned long cpt = clocktick;

	profile_tick(CPU_PROFILING);

	/* Initialize next_tick to the expected tick time. */
	next_tick = cpuinfo->it_value;

	/* Get current cycle counter (Control Register 16). */
	now = mfctl(16);

	cycles_elapsed = now - next_tick;

	if ((cycles_elapsed >> 6) < cpt) {
		/* use "cheap" math (add/subtract) instead
		 * of the more expensive div/mul method
		 */
		cycles_remainder = cycles_elapsed;
		while (cycles_remainder > cpt) {
			cycles_remainder -= cpt;
			ticks_elapsed++;
		}
	} else {
		/* TODO: Reduce this to one fdiv op */
		cycles_remainder = cycles_elapsed % cpt;
		ticks_elapsed += cycles_elapsed / cpt;
	}

	/* convert from "division remainder" to "remainder of clock tick" */
	cycles_remainder = cpt - cycles_remainder;

	/* Determine when (in CR16 cycles) next IT interrupt will fire.
	 * We want IT to fire modulo clocktick even if we miss/skip some.
	 * But those interrupts don't in fact get delivered that regularly.
	 */
	next_tick = now + cycles_remainder;

	cpuinfo->it_value = next_tick;

	/* Program the IT when to deliver the next interrupt.
	 * Only bottom 32-bits of next_tick are writable in CR16!
	 */
	mtctl(next_tick, 16);

	/* Skip one clocktick on purpose if we missed next_tick.
	 * The new CR16 must be "later" than current CR16 otherwise
	 * itimer would not fire until CR16 wrapped - e.g 4 seconds
	 * later on a 1Ghz processor. We'll account for the missed
	 * tick on the next timer interrupt.
	 *
	 * "next_tick - now" will always give the difference regardless
	 * if one or the other wrapped. If "now" is "bigger" we'll end up
	 * with a very large unsigned number.
	 */
	now2 = mfctl(16);
	if (next_tick - now2 > cpt)
		mtctl(next_tick+cpt, 16);

#if 1
/*
 * GGG: DEBUG code for how many cycles programming CR16 used.
 */
	if (unlikely(now2 - now > 0x3000)) 	/* 12K cycles */
		printk (KERN_CRIT "timer_interrupt(CPU %d): SLOW! 0x%lx cycles!"
			" cyc %lX rem %lX "
			" next/now %lX/%lX\n",
			cpu, now2 - now, cycles_elapsed, cycles_remainder,
			next_tick, now );
#endif

	/* Can we differentiate between "early CR16" (aka Scenario 1) and
	 * "long delay" (aka Scenario 3)? I don't think so.
	 *
	 * Timer_interrupt will be delivered at least a few hundred cycles
	 * after the IT fires. But it's arbitrary how much time passes
	 * before we call it "late". I've picked one second.
	 *
	 * It's important NO printk's are between reading CR16 and
	 * setting up the next value. May introduce huge variance.
	 */
	if (unlikely(ticks_elapsed > HZ)) {
		/* Scenario 3: very long delay?  bad in any case */
		printk (KERN_CRIT "timer_interrupt(CPU %d): delayed!"
			" cycles %lX rem %lX "
			" next/now %lX/%lX\n",
			cpu,
			cycles_elapsed, cycles_remainder,
			next_tick, now );
	}

	/* Done mucking with unreliable delivery of interrupts.
	 * Go do system house keeping.
	 */

	if (!--cpuinfo->prof_counter) {
		cpuinfo->prof_counter = cpuinfo->prof_multiplier;
		update_process_times(user_mode(get_irq_regs()));
	}

	if (cpu == 0)
		xtime_update(ticks_elapsed);

	return IRQ_HANDLED;
}


unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (regs->gr[0] & PSW_N)
		pc -= 4;

#ifdef CONFIG_SMP
	if (in_lock_functions(pc))
		pc = regs->gr[2];
#endif

	return pc;
}
EXPORT_SYMBOL(profile_pc);


/* clock source code */

static cycle_t notrace read_cr16(struct clocksource *cs)
{
	return get_cycles();
}

static struct clocksource clocksource_cr16 = {
	.name			= "cr16",
	.rating			= 300,
	.read			= read_cr16,
	.mask			= CLOCKSOURCE_MASK(BITS_PER_LONG),
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS,
};

void __init start_cpu_itimer(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned long next_tick = mfctl(16) + clocktick;

	mtctl(next_tick, 16);		/* kick off Interval Timer (CR16) */

	per_cpu(cpu_data, cpu).it_value = next_tick;
}

#if IS_ENABLED(CONFIG_RTC_DRV_GENERIC)
static int rtc_generic_get_time(struct device *dev, struct rtc_time *tm)
{
	struct pdc_tod tod_data;

	memset(tm, 0, sizeof(*tm));
	if (pdc_tod_read(&tod_data) < 0)
		return -EOPNOTSUPP;

	/* we treat tod_sec as unsigned, so this can work until year 2106 */
	rtc_time64_to_tm(tod_data.tod_sec, tm);
	return rtc_valid_tm(tm);
}

static int rtc_generic_set_time(struct device *dev, struct rtc_time *tm)
{
	time64_t secs = rtc_tm_to_time64(tm);

	if (pdc_tod_set(secs, 0) < 0)
		return -EOPNOTSUPP;

	return 0;
}

static const struct rtc_class_ops rtc_generic_ops = {
	.read_time = rtc_generic_get_time,
	.set_time = rtc_generic_set_time,
};

static int __init rtc_init(void)
{
	struct platform_device *pdev;

	pdev = platform_device_register_data(NULL, "rtc-generic", -1,
					     &rtc_generic_ops,
					     sizeof(rtc_generic_ops));

	return PTR_ERR_OR_ZERO(pdev);
}
device_initcall(rtc_init);
#endif

void read_persistent_clock(struct timespec *ts)
{
	static struct pdc_tod tod_data;
	if (pdc_tod_read(&tod_data) == 0) {
		ts->tv_sec = tod_data.tod_sec;
		ts->tv_nsec = tod_data.tod_usec * 1000;
	} else {
		printk(KERN_ERR "Error reading tod clock\n");
	        ts->tv_sec = 0;
		ts->tv_nsec = 0;
	}
}


static u64 notrace read_cr16_sched_clock(void)
{
	return get_cycles();
}


/*
 * timer interrupt and sched_clock() initialization
 */

void __init time_init(void)
{
	unsigned long cr16_hz;

	clocktick = (100 * PAGE0->mem_10msec) / HZ;
	start_cpu_itimer();	/* get CPU 0 started */

	cr16_hz = 100 * PAGE0->mem_10msec;  /* Hz */

	/* register at clocksource framework */
	clocksource_register_hz(&clocksource_cr16, cr16_hz);

	/* register as sched_clock source */
	sched_clock_register(read_cr16_sched_clock, BITS_PER_LONG, cr16_hz);
}
