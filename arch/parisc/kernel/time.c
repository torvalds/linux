// SPDX-License-Identifier: GPL-2.0
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
#include <linux/sched/clock.h>
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

#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/param.h>
#include <asm/pdc.h>
#include <asm/led.h>

#include <linux/timex.h>

static unsigned long clocktick __ro_after_init;	/* timer cycles per tick */

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
	unsigned long now;
	unsigned long next_tick;
	unsigned long ticks_elapsed = 0;
	unsigned int cpu = smp_processor_id();
	struct cpuinfo_parisc *cpuinfo = &per_cpu(cpu_data, cpu);

	/* gcc can optimize for "read-only" case with a local clocktick */
	unsigned long cpt = clocktick;

	/* Initialize next_tick to the old expected tick time. */
	next_tick = cpuinfo->it_value;

	/* Calculate how many ticks have elapsed. */
	now = mfctl(16);
	do {
		++ticks_elapsed;
		next_tick += cpt;
	} while (next_tick - now > cpt);

	/* Store (in CR16 cycles) up to when we are accounting right now. */
	cpuinfo->it_value = next_tick;

	/* Go do system house keeping. */
	if (cpu != 0)
		ticks_elapsed = 0;
	legacy_timer_tick(ticks_elapsed);

	/* Skip clockticks on purpose if we know we would miss those.
	 * The new CR16 must be "later" than current CR16 otherwise
	 * itimer would not fire until CR16 wrapped - e.g 4 seconds
	 * later on a 1Ghz processor. We'll account for the missed
	 * ticks on the next timer interrupt.
	 * We want IT to fire modulo clocktick even if we miss/skip some.
	 * But those interrupts don't in fact get delivered that regularly.
	 *
	 * "next_tick - now" will always give the difference regardless
	 * if one or the other wrapped. If "now" is "bigger" we'll end up
	 * with a very large unsigned number.
	 */
	now = mfctl(16);
	while (next_tick - now > cpt)
		next_tick += cpt;

	/* Program the IT when to deliver the next interrupt.
	 * Only bottom 32-bits of next_tick are writable in CR16!
	 * Timer interrupt will be delivered at least a few hundred cycles
	 * after the IT fires, so if we are too close (<= 8000 cycles) to the
	 * next cycle, simply skip it.
	 */
	if (next_tick - now <= 8000)
		next_tick += cpt;
	mtctl(next_tick, 16);

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

static u64 notrace read_cr16(struct clocksource *cs)
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
	return 0;
}

static int rtc_generic_set_time(struct device *dev, struct rtc_time *tm)
{
	time64_t secs = rtc_tm_to_time64(tm);
	int ret;

	/* hppa has Y2K38 problem: pdc_tod_set() takes an u32 value! */
	ret = pdc_tod_set(secs, 0);
	if (ret != 0) {
		pr_warn("pdc_tod_set(%lld) returned error %d\n", secs, ret);
		if (ret == PDC_INVALID_ARG)
			return -EINVAL;
		return -EOPNOTSUPP;
	}

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

void read_persistent_clock64(struct timespec64 *ts)
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

	/* register as sched_clock source */
	sched_clock_register(read_cr16_sched_clock, BITS_PER_LONG, cr16_hz);
}

static int __init init_cr16_clocksource(void)
{
	/*
	 * The cr16 interval timers are not syncronized across CPUs on
	 * different sockets, so mark them unstable and lower rating on
	 * multi-socket SMP systems.
	 */
	if (num_online_cpus() > 1 && !running_on_qemu) {
		int cpu;
		unsigned long cpu0_loc;
		cpu0_loc = per_cpu(cpu_data, 0).cpu_loc;

		for_each_online_cpu(cpu) {
			if (cpu == 0)
				continue;
			if ((cpu0_loc != 0) &&
			    (cpu0_loc == per_cpu(cpu_data, cpu).cpu_loc))
				continue;

			/* mark sched_clock unstable */
			clear_sched_clock_stable();

			clocksource_cr16.name = "cr16_unstable";
			clocksource_cr16.flags = CLOCK_SOURCE_UNSTABLE;
			clocksource_cr16.rating = 0;
			break;
		}
	}

	/* register at clocksource framework */
	clocksource_register_hz(&clocksource_cr16,
		100 * PAGE0->mem_10msec);

	return 0;
}

device_initcall(init_cr16_clocksource);
