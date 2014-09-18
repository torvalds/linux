/*
 *  linux/arch/alpha/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995, 1999, 2000  Linus Torvalds
 *
 * This file contains the clocksource time handling.
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 * 1997-01-09    Adrian Sun
 *      use interval timer if CONFIG_RTC=y
 * 1997-10-29    John Bowman (bowman@math.ualberta.ca)
 *      fixed tick loss calculation in timer_interrupt
 *      (round system clock to nearest tick instead of truncating)
 *      fixed algorithm in time_init for getting time from CMOS clock
 * 1999-04-16	Thorsten Kranzkowski (dl8bcu@gmx.net)
 *	fixed algorithm in do_gettimeofday() for calculating the precise time
 *	from processor cycle counter (now taking lost_ticks into account)
 * 2003-06-03	R. Scott Bailey <scott.bailey@eds.com>
 *	Tighten sanity in time_init from 1% (10,000 PPM) to 250 PPM
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/bcd.h>
#include <linux/profile.h>
#include <linux/irq_work.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/hwrpb.h>

#include <linux/mc146818rtc.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>

#include "proto.h"
#include "irq_impl.h"

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

unsigned long est_cycle_freq;

#ifdef CONFIG_IRQ_WORK

DEFINE_PER_CPU(u8, irq_work_pending);

#define set_irq_work_pending_flag()  __get_cpu_var(irq_work_pending) = 1
#define test_irq_work_pending()      __get_cpu_var(irq_work_pending)
#define clear_irq_work_pending()     __get_cpu_var(irq_work_pending) = 0

void arch_irq_work_raise(void)
{
	set_irq_work_pending_flag();
}

#else  /* CONFIG_IRQ_WORK */

#define test_irq_work_pending()      0
#define clear_irq_work_pending()

#endif /* CONFIG_IRQ_WORK */


static inline __u32 rpcc(void)
{
	return __builtin_alpha_rpcc();
}



/*
 * The RTC as a clock_event_device primitive.
 */

static DEFINE_PER_CPU(struct clock_event_device, cpu_ce);

irqreturn_t
rtc_timer_interrupt(int irq, void *dev)
{
	int cpu = smp_processor_id();
	struct clock_event_device *ce = &per_cpu(cpu_ce, cpu);

	/* Don't run the hook for UNUSED or SHUTDOWN.  */
	if (likely(ce->mode == CLOCK_EVT_MODE_PERIODIC))
		ce->event_handler(ce);

	if (test_irq_work_pending()) {
		clear_irq_work_pending();
		irq_work_run();
	}

	return IRQ_HANDLED;
}

static void
rtc_ce_set_mode(enum clock_event_mode mode, struct clock_event_device *ce)
{
	/* The mode member of CE is updated in generic code.
	   Since we only support periodic events, nothing to do.  */
}

static int
rtc_ce_set_next_event(unsigned long evt, struct clock_event_device *ce)
{
	/* This hook is for oneshot mode, which we don't support.  */
	return -EINVAL;
}

static void __init
init_rtc_clockevent(void)
{
	int cpu = smp_processor_id();
	struct clock_event_device *ce = &per_cpu(cpu_ce, cpu);

	*ce = (struct clock_event_device){
		.name = "rtc",
		.features = CLOCK_EVT_FEAT_PERIODIC,
		.rating = 100,
		.cpumask = cpumask_of(cpu),
		.set_mode = rtc_ce_set_mode,
		.set_next_event = rtc_ce_set_next_event,
	};

	clockevents_config_and_register(ce, CONFIG_HZ, 0, 0);
}


/*
 * The QEMU clock as a clocksource primitive.
 */

static cycle_t
qemu_cs_read(struct clocksource *cs)
{
	return qemu_get_vmtime();
}

static struct clocksource qemu_cs = {
	.name                   = "qemu",
	.rating                 = 400,
	.read                   = qemu_cs_read,
	.mask                   = CLOCKSOURCE_MASK(64),
	.flags                  = CLOCK_SOURCE_IS_CONTINUOUS,
	.max_idle_ns		= LONG_MAX
};


/*
 * The QEMU alarm as a clock_event_device primitive.
 */

static void
qemu_ce_set_mode(enum clock_event_mode mode, struct clock_event_device *ce)
{
	/* The mode member of CE is updated for us in generic code.
	   Just make sure that the event is disabled.  */
	qemu_set_alarm_abs(0);
}

static int
qemu_ce_set_next_event(unsigned long evt, struct clock_event_device *ce)
{
	qemu_set_alarm_rel(evt);
	return 0;
}

static irqreturn_t
qemu_timer_interrupt(int irq, void *dev)
{
	int cpu = smp_processor_id();
	struct clock_event_device *ce = &per_cpu(cpu_ce, cpu);

	ce->event_handler(ce);
	return IRQ_HANDLED;
}

static void __init
init_qemu_clockevent(void)
{
	int cpu = smp_processor_id();
	struct clock_event_device *ce = &per_cpu(cpu_ce, cpu);

	*ce = (struct clock_event_device){
		.name = "qemu",
		.features = CLOCK_EVT_FEAT_ONESHOT,
		.rating = 400,
		.cpumask = cpumask_of(cpu),
		.set_mode = qemu_ce_set_mode,
		.set_next_event = qemu_ce_set_next_event,
	};

	clockevents_config_and_register(ce, NSEC_PER_SEC, 1000, LONG_MAX);
}


void __init
common_init_rtc(void)
{
	unsigned char x, sel = 0;

	/* Reset periodic interrupt frequency.  */
#if CONFIG_HZ == 1024 || CONFIG_HZ == 1200
 	x = CMOS_READ(RTC_FREQ_SELECT) & 0x3f;
	/* Test includes known working values on various platforms
	   where 0x26 is wrong; we refuse to change those. */
 	if (x != 0x26 && x != 0x25 && x != 0x19 && x != 0x06) {
		sel = RTC_REF_CLCK_32KHZ + 6;
	}
#elif CONFIG_HZ == 256 || CONFIG_HZ == 128 || CONFIG_HZ == 64 || CONFIG_HZ == 32
	sel = RTC_REF_CLCK_32KHZ + __builtin_ffs(32768 / CONFIG_HZ);
#else
# error "Unknown HZ from arch/alpha/Kconfig"
#endif
	if (sel) {
		printk(KERN_INFO "Setting RTC_FREQ to %d Hz (%x)\n",
		       CONFIG_HZ, sel);
		CMOS_WRITE(sel, RTC_FREQ_SELECT);
 	}

	/* Turn on periodic interrupts.  */
	x = CMOS_READ(RTC_CONTROL);
	if (!(x & RTC_PIE)) {
		printk("Turning on RTC interrupts.\n");
		x |= RTC_PIE;
		x &= ~(RTC_AIE | RTC_UIE);
		CMOS_WRITE(x, RTC_CONTROL);
	}
	(void) CMOS_READ(RTC_INTR_FLAGS);

	outb(0x36, 0x43);	/* pit counter 0: system timer */
	outb(0x00, 0x40);
	outb(0x00, 0x40);

	outb(0xb6, 0x43);	/* pit counter 2: speaker */
	outb(0x31, 0x42);
	outb(0x13, 0x42);

	init_rtc_irq();
}


#ifndef CONFIG_ALPHA_WTINT
/*
 * The RPCC as a clocksource primitive.
 *
 * While we have free-running timecounters running on all CPUs, and we make
 * a half-hearted attempt in init_rtc_rpcc_info to sync the timecounter
 * with the wall clock, that initialization isn't kept up-to-date across
 * different time counters in SMP mode.  Therefore we can only use this
 * method when there's only one CPU enabled.
 *
 * When using the WTINT PALcall, the RPCC may shift to a lower frequency,
 * or stop altogether, while waiting for the interrupt.  Therefore we cannot
 * use this method when WTINT is in use.
 */

static cycle_t read_rpcc(struct clocksource *cs)
{
	return rpcc();
}

static struct clocksource clocksource_rpcc = {
	.name                   = "rpcc",
	.rating                 = 300,
	.read                   = read_rpcc,
	.mask                   = CLOCKSOURCE_MASK(32),
	.flags                  = CLOCK_SOURCE_IS_CONTINUOUS
};
#endif /* ALPHA_WTINT */


/* Validate a computed cycle counter result against the known bounds for
   the given processor core.  There's too much brokenness in the way of
   timing hardware for any one method to work everywhere.  :-(

   Return 0 if the result cannot be trusted, otherwise return the argument.  */

static unsigned long __init
validate_cc_value(unsigned long cc)
{
	static struct bounds {
		unsigned int min, max;
	} cpu_hz[] __initdata = {
		[EV3_CPU]    = {   50000000,  200000000 },	/* guess */
		[EV4_CPU]    = {  100000000,  300000000 },
		[LCA4_CPU]   = {  100000000,  300000000 },	/* guess */
		[EV45_CPU]   = {  200000000,  300000000 },
		[EV5_CPU]    = {  250000000,  433000000 },
		[EV56_CPU]   = {  333000000,  667000000 },
		[PCA56_CPU]  = {  400000000,  600000000 },	/* guess */
		[PCA57_CPU]  = {  500000000,  600000000 },	/* guess */
		[EV6_CPU]    = {  466000000,  600000000 },
		[EV67_CPU]   = {  600000000,  750000000 },
		[EV68AL_CPU] = {  750000000,  940000000 },
		[EV68CB_CPU] = { 1000000000, 1333333333 },
		/* None of the following are shipping as of 2001-11-01.  */
		[EV68CX_CPU] = { 1000000000, 1700000000 },	/* guess */
		[EV69_CPU]   = { 1000000000, 1700000000 },	/* guess */
		[EV7_CPU]    = {  800000000, 1400000000 },	/* guess */
		[EV79_CPU]   = { 1000000000, 2000000000 },	/* guess */
	};

	/* Allow for some drift in the crystal.  10MHz is more than enough.  */
	const unsigned int deviation = 10000000;

	struct percpu_struct *cpu;
	unsigned int index;

	cpu = (struct percpu_struct *)((char*)hwrpb + hwrpb->processor_offset);
	index = cpu->type & 0xffffffff;

	/* If index out of bounds, no way to validate.  */
	if (index >= ARRAY_SIZE(cpu_hz))
		return cc;

	/* If index contains no data, no way to validate.  */
	if (cpu_hz[index].max == 0)
		return cc;

	if (cc < cpu_hz[index].min - deviation
	    || cc > cpu_hz[index].max + deviation)
		return 0;

	return cc;
}


/*
 * Calibrate CPU clock using legacy 8254 timer/counter. Stolen from
 * arch/i386/time.c.
 */

#define CALIBRATE_LATCH	0xffff
#define TIMEOUT_COUNT	0x100000

static unsigned long __init
calibrate_cc_with_pit(void)
{
	int cc, count = 0;

	/* Set the Gate high, disable speaker */
	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	/*
	 * Now let's take care of CTC channel 2
	 *
	 * Set the Gate high, program CTC channel 2 for mode 0,
	 * (interrupt on terminal count mode), binary count,
	 * load 5 * LATCH count, (LSB and MSB) to begin countdown.
	 */
	outb(0xb0, 0x43);		/* binary, mode 0, LSB/MSB, Ch 2 */
	outb(CALIBRATE_LATCH & 0xff, 0x42);	/* LSB of count */
	outb(CALIBRATE_LATCH >> 8, 0x42);	/* MSB of count */

	cc = rpcc();
	do {
		count++;
	} while ((inb(0x61) & 0x20) == 0 && count < TIMEOUT_COUNT);
	cc = rpcc() - cc;

	/* Error: ECTCNEVERSET or ECPUTOOFAST.  */
	if (count <= 1 || count == TIMEOUT_COUNT)
		return 0;

	return ((long)cc * PIT_TICK_RATE) / (CALIBRATE_LATCH + 1);
}

/* The Linux interpretation of the CMOS clock register contents:
   When the Update-In-Progress (UIP) flag goes from 1 to 0, the
   RTC registers show the second which has precisely just started.
   Let's hope other operating systems interpret the RTC the same way.  */

static unsigned long __init
rpcc_after_update_in_progress(void)
{
	do { } while (!(CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP));
	do { } while (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP);

	return rpcc();
}

void __init
time_init(void)
{
	unsigned int cc1, cc2;
	unsigned long cycle_freq, tolerance;
	long diff;

	if (alpha_using_qemu) {
		clocksource_register_hz(&qemu_cs, NSEC_PER_SEC);
		init_qemu_clockevent();

		timer_irqaction.handler = qemu_timer_interrupt;
		init_rtc_irq();
		return;
	}

	/* Calibrate CPU clock -- attempt #1.  */
	if (!est_cycle_freq)
		est_cycle_freq = validate_cc_value(calibrate_cc_with_pit());

	cc1 = rpcc();

	/* Calibrate CPU clock -- attempt #2.  */
	if (!est_cycle_freq) {
		cc1 = rpcc_after_update_in_progress();
		cc2 = rpcc_after_update_in_progress();
		est_cycle_freq = validate_cc_value(cc2 - cc1);
		cc1 = cc2;
	}

	cycle_freq = hwrpb->cycle_freq;
	if (est_cycle_freq) {
		/* If the given value is within 250 PPM of what we calculated,
		   accept it.  Otherwise, use what we found.  */
		tolerance = cycle_freq / 4000;
		diff = cycle_freq - est_cycle_freq;
		if (diff < 0)
			diff = -diff;
		if ((unsigned long)diff > tolerance) {
			cycle_freq = est_cycle_freq;
			printk("HWRPB cycle frequency bogus.  "
			       "Estimated %lu Hz\n", cycle_freq);
		} else {
			est_cycle_freq = 0;
		}
	} else if (! validate_cc_value (cycle_freq)) {
		printk("HWRPB cycle frequency bogus, "
		       "and unable to estimate a proper value!\n");
	}

	/* See above for restrictions on using clocksource_rpcc.  */
#ifndef CONFIG_ALPHA_WTINT
	if (hwrpb->nr_processors == 1)
		clocksource_register_hz(&clocksource_rpcc, cycle_freq);
#endif

	/* Startup the timer source. */
	alpha_mv.init_rtc();
	init_rtc_clockevent();
}

/* Initialize the clock_event_device for secondary cpus.  */
#ifdef CONFIG_SMP
void __init
init_clockevent(void)
{
	if (alpha_using_qemu)
		init_qemu_clockevent();
	else
		init_rtc_clockevent();
}
#endif
