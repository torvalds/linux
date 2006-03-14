/*
 *  linux/arch/i386/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * This file contains the PC-specific time handling details:
 * reading the RTC at bootup, etc..
 * 1994-07-02    Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1995-03-26    Markus Kuhn
 *      fixed 500 ms bug at call to set_rtc_mmss, fixed DS12887
 *      precision CMOS clock update
 * 1996-05-03    Ingo Molnar
 *      fixed time warps in do_[slow|fast]_gettimeoffset()
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 * 1998-09-05    (Various)
 *	More robust do_fast_gettimeoffset() algorithm implemented
 *	(works with APM, Cyrix 6x86MX and Centaur C6),
 *	monotonic gettimeofday() with fast_get_timeoffset(),
 *	drift-proof precision TSC calibration on boot
 *	(C. Scott Ananian <cananian@alumni.princeton.edu>, Andrew D.
 *	Balsa <andrebalsa@altern.org>, Philip Gladstone <philip@raptor.com>;
 *	ported from 2.0.35 Jumbo-9 by Michael Krause <m.krause@tu-harburg.de>).
 * 1998-12-16    Andrea Arcangeli
 *	Fixed Jumbo-9 code in 2.1.131: do_gettimeofday was missing 1 jiffy
 *	because was not accounting lost_ticks.
 * 1998-12-24 Copyright (C) 1998  Andrea Arcangeli
 *	Fixed a xtime SMP race (we need the xtime_lock rw spinlock to
 *	serialize accesses to xtime/lost_ticks).
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/bcd.h>
#include <linux/efi.h>
#include <linux/mca.h>

#include <asm/io.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/msr.h>
#include <asm/delay.h>
#include <asm/mpspec.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/timer.h>

#include "mach_time.h"

#include <linux/timex.h>
#include <linux/config.h>

#include <asm/hpet.h>

#include <asm/arch_hooks.h>

#include "io_ports.h"

#include <asm/i8259.h>

int pit_latch_buggy;              /* extern */

#include "do_timer.h"

unsigned int cpu_khz;	/* Detected as we calibrate the TSC */
EXPORT_SYMBOL(cpu_khz);

extern unsigned long wall_jiffies;

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

#include <asm/i8253.h>

DEFINE_SPINLOCK(i8253_lock);
EXPORT_SYMBOL(i8253_lock);

struct timer_opts *cur_timer __read_mostly = &timer_none;

/*
 * This is a special lock that is owned by the CPU and holds the index
 * register we are working with.  It is required for NMI access to the
 * CMOS/RTC registers.  See include/asm-i386/mc146818rtc.h for details.
 */
volatile unsigned long cmos_lock = 0;
EXPORT_SYMBOL(cmos_lock);

/* Routines for accessing the CMOS RAM/RTC. */
unsigned char rtc_cmos_read(unsigned char addr)
{
	unsigned char val;
	lock_cmos_prefix(addr);
	outb_p(addr, RTC_PORT(0));
	val = inb_p(RTC_PORT(1));
	lock_cmos_suffix(addr);
	return val;
}
EXPORT_SYMBOL(rtc_cmos_read);

void rtc_cmos_write(unsigned char val, unsigned char addr)
{
	lock_cmos_prefix(addr);
	outb_p(addr, RTC_PORT(0));
	outb_p(val, RTC_PORT(1));
	lock_cmos_suffix(addr);
}
EXPORT_SYMBOL(rtc_cmos_write);

/*
 * This version of gettimeofday has microsecond resolution
 * and better than microsecond precision on fast x86 machines with TSC.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;
	unsigned long max_ntp_tick;

	do {
		unsigned long lost;

		seq = read_seqbegin(&xtime_lock);

		usec = cur_timer->get_offset();
		lost = jiffies - wall_jiffies;

		/*
		 * If time_adjust is negative then NTP is slowing the clock
		 * so make sure not to go into next possible interval.
		 * Better to lose some accuracy than have time go backwards..
		 */
		if (unlikely(time_adjust < 0)) {
			max_ntp_tick = (USEC_PER_SEC / HZ) - tickadj;
			usec = min(usec, max_ntp_tick);

			if (lost)
				usec += lost * max_ntp_tick;
		}
		else if (unlikely(lost))
			usec += lost * (USEC_PER_SEC / HZ);

		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / 1000);
	} while (read_seqretry(&xtime_lock, seq));

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */
	nsec -= cur_timer->get_offset() * NSEC_PER_USEC;
	nsec -= (jiffies - wall_jiffies) * TICK_NSEC;

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	ntp_clear();
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

static int set_rtc_mmss(unsigned long nowtime)
{
	int retval;

	WARN_ON(irqs_disabled());

	/* gets recalled with irq locally disabled */
	spin_lock_irq(&rtc_lock);
	if (efi_enabled)
		retval = efi_set_rtc_mmss(nowtime);
	else
		retval = mach_set_rtc_mmss(nowtime);
	spin_unlock_irq(&rtc_lock);

	return retval;
}


int timer_ack;

/* monotonic_clock(): returns # of nanoseconds passed since time_init()
 *		Note: This function is required to return accurate
 *		time even in the absence of multiple timer ticks.
 */
unsigned long long monotonic_clock(void)
{
	return cur_timer->monotonic_clock();
}
EXPORT_SYMBOL(monotonic_clock);

#if defined(CONFIG_SMP) && defined(CONFIG_FRAME_POINTER)
unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (in_lock_functions(pc))
		return *(unsigned long *)(regs->ebp + 4);

	return pc;
}
EXPORT_SYMBOL(profile_pc);
#endif

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static inline void do_timer_interrupt(int irq, struct pt_regs *regs)
{
#ifdef CONFIG_X86_IO_APIC
	if (timer_ack) {
		/*
		 * Subtle, when I/O APICs are used we have to ack timer IRQ
		 * manually to reset the IRR bit for do_slow_gettimeoffset().
		 * This will also deassert NMI lines for the watchdog if run
		 * on an 82489DX-based system.
		 */
		spin_lock(&i8259A_lock);
		outb(0x0c, PIC_MASTER_OCW3);
		/* Ack the IRQ; AEOI will end it automatically. */
		inb(PIC_MASTER_POLL);
		spin_unlock(&i8259A_lock);
	}
#endif

	do_timer_interrupt_hook(regs);


	if (MCA_bus) {
		/* The PS/2 uses level-triggered interrupts.  You can't
		turn them off, nor would you want to (any attempt to
		enable edge-triggered interrupts usually gets intercepted by a
		special hardware circuit).  Hence we have to acknowledge
		the timer interrupt.  Through some incredibly stupid
		design idea, the reset for IRQ 0 is done by setting the
		high bit of the PPI port B (0x61).  Note that some PS/2s,
		notably the 55SX, work fine if this is removed.  */

		irq = inb_p( 0x61 );	/* read the current state */
		outb_p( irq|0x80, 0x61 );	/* reset the IRQ */
	}
}

/*
 * This is the same as the above, except we _also_ save the current
 * Time Stamp Counter value at the time of the timer interrupt, so that
 * we later on can estimate the time of day more exactly.
 */
irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);

	cur_timer->mark_offset();
 
	do_timer_interrupt(irq, regs);

	write_sequnlock(&xtime_lock);

#ifdef CONFIG_X86_LOCAL_APIC
	if (using_apic_timer)
		smp_send_timer_broadcast_ipi(regs);
#endif

	return IRQ_HANDLED;
}

/* not static: needed by APM */
unsigned long get_cmos_time(void)
{
	unsigned long retval;

	spin_lock(&rtc_lock);

	if (efi_enabled)
		retval = efi_get_time();
	else
		retval = mach_get_cmos_time();

	spin_unlock(&rtc_lock);

	return retval;
}
EXPORT_SYMBOL(get_cmos_time);

static void sync_cmos_clock(unsigned long dummy);

static DEFINE_TIMER(sync_cmos_timer, sync_cmos_clock, 0, 0);

static void sync_cmos_clock(unsigned long dummy)
{
	struct timeval now, next;
	int fail = 1;

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 * This code is run on a timer.  If the clock is set, that timer
	 * may not expire at the correct time.  Thus, we adjust...
	 */
	if (!ntp_synced())
		/*
		 * Not synced, exit, do not restart a timer (if one is
		 * running, let it run out).
		 */
		return;

	do_gettimeofday(&now);
	if (now.tv_usec >= USEC_AFTER - ((unsigned) TICK_SIZE) / 2 &&
	    now.tv_usec <= USEC_BEFORE + ((unsigned) TICK_SIZE) / 2)
		fail = set_rtc_mmss(now.tv_sec);

	next.tv_usec = USEC_AFTER - now.tv_usec;
	if (next.tv_usec <= 0)
		next.tv_usec += USEC_PER_SEC;

	if (!fail)
		next.tv_sec = 659;
	else
		next.tv_sec = 0;

	if (next.tv_usec >= USEC_PER_SEC) {
		next.tv_sec++;
		next.tv_usec -= USEC_PER_SEC;
	}
	mod_timer(&sync_cmos_timer, jiffies + timeval_to_jiffies(&next));
}

void notify_arch_cmos_timer(void)
{
	mod_timer(&sync_cmos_timer, jiffies + 1);
}

static long clock_cmos_diff, sleep_start;

static struct timer_opts *last_timer;
static int timer_suspend(struct sys_device *dev, pm_message_t state)
{
	/*
	 * Estimate time zone so that set_time can update the clock
	 */
	clock_cmos_diff = -get_cmos_time();
	clock_cmos_diff += get_seconds();
	sleep_start = get_cmos_time();
	last_timer = cur_timer;
	cur_timer = &timer_none;
	if (last_timer->suspend)
		last_timer->suspend(state);
	return 0;
}

static int timer_resume(struct sys_device *dev)
{
	unsigned long flags;
	unsigned long sec;
	unsigned long sleep_length;

#ifdef CONFIG_HPET_TIMER
	if (is_hpet_enabled())
		hpet_reenable();
#endif
	setup_pit_timer();
	sec = get_cmos_time() + clock_cmos_diff;
	sleep_length = (get_cmos_time() - sleep_start) * HZ;
	write_seqlock_irqsave(&xtime_lock, flags);
	xtime.tv_sec = sec;
	xtime.tv_nsec = 0;
	jiffies_64 += sleep_length;
	wall_jiffies += sleep_length;
	write_sequnlock_irqrestore(&xtime_lock, flags);
	if (last_timer->resume)
		last_timer->resume();
	cur_timer = last_timer;
	last_timer = NULL;
	touch_softlockup_watchdog();
	return 0;
}

static struct sysdev_class timer_sysclass = {
	.resume = timer_resume,
	.suspend = timer_suspend,
	set_kset_name("timer"),
};


/* XXX this driverfs stuff should probably go elsewhere later -john */
static struct sys_device device_timer = {
	.id	= 0,
	.cls	= &timer_sysclass,
};

static int time_init_device(void)
{
	int error = sysdev_class_register(&timer_sysclass);
	if (!error)
		error = sysdev_register(&device_timer);
	return error;
}

device_initcall(time_init_device);

#ifdef CONFIG_HPET_TIMER
extern void (*late_time_init)(void);
/* Duplicate of time_init() below, with hpet_enable part added */
static void __init hpet_time_init(void)
{
	xtime.tv_sec = get_cmos_time();
	xtime.tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
	set_normalized_timespec(&wall_to_monotonic,
		-xtime.tv_sec, -xtime.tv_nsec);

	if ((hpet_enable() >= 0) && hpet_use_timer) {
		printk("Using HPET for base-timer\n");
	}

	cur_timer = select_timer();
	printk(KERN_INFO "Using %s for high-res timesource\n",cur_timer->name);

	time_init_hook();
}
#endif

void __init time_init(void)
{
#ifdef CONFIG_HPET_TIMER
	if (is_hpet_capable()) {
		/*
		 * HPET initialization needs to do memory-mapped io. So, let
		 * us do a late initialization after mem_init().
		 */
		late_time_init = hpet_time_init;
		return;
	}
#endif
	xtime.tv_sec = get_cmos_time();
	xtime.tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
	set_normalized_timespec(&wall_to_monotonic,
		-xtime.tv_sec, -xtime.tv_nsec);

	cur_timer = select_timer();
	printk(KERN_INFO "Using %s for high-res timesource\n",cur_timer->name);

	time_init_hook();
}
