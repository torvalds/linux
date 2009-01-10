/*
 * arch/blackfin/kernel/time.c
 *
 * This file contains the Blackfin-specific time handling details.
 * Most of the stuff is located in the machine specific files.
 *
 * Copyright 2004-2008 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/irq.h>
#include <linux/delay.h>

#include <asm/blackfin.h>
#include <asm/time.h>
#include <asm/gptimers.h>

/* This is an NTP setting */
#define	TICK_SIZE (tick_nsec / 1000)

static struct irqaction bfin_timer_irq = {
	.name = "Blackfin Timer Tick",
#ifdef CONFIG_IRQ_PER_CPU
	.flags = IRQF_DISABLED | IRQF_PERCPU,
#else
	.flags = IRQF_DISABLED
#endif
};

#if defined(CONFIG_TICK_SOURCE_SYSTMR0) || defined(CONFIG_IPIPE)
void __init setup_system_timer0(void)
{
	/* Power down the core timer, just to play safe. */
	bfin_write_TCNTL(0);

	disable_gptimers(TIMER0bit);
	set_gptimer_status(0, TIMER_STATUS_TRUN0);
	while (get_gptimer_status(0) & TIMER_STATUS_TRUN0)
		udelay(10);

	set_gptimer_config(0, 0x59); /* IRQ enable, periodic, PWM_OUT, SCLKed, OUT PAD disabled */
	set_gptimer_period(TIMER0_id, get_sclk() / HZ);
	set_gptimer_pwidth(TIMER0_id, 1);
	SSYNC();
	enable_gptimers(TIMER0bit);
}
#else
void __init setup_core_timer(void)
{
	u32 tcount;

	/* power up the timer, but don't enable it just yet */
	bfin_write_TCNTL(1);
	CSYNC();

	/* the TSCALE prescaler counter */
	bfin_write_TSCALE(TIME_SCALE - 1);

	tcount = ((get_cclk() / (HZ * TIME_SCALE)) - 1);
	bfin_write_TPERIOD(tcount);
	bfin_write_TCOUNT(tcount);

	/* now enable the timer */
	CSYNC();

	bfin_write_TCNTL(7);
}
#endif

static void __init
time_sched_init(irqreturn_t(*timer_routine) (int, void *))
{
#if defined(CONFIG_TICK_SOURCE_SYSTMR0) || defined(CONFIG_IPIPE)
	setup_system_timer0();
	bfin_timer_irq.handler = timer_routine;
	setup_irq(IRQ_TIMER0, &bfin_timer_irq);
#else
	setup_core_timer();
	bfin_timer_irq.handler = timer_routine;
	setup_irq(IRQ_CORETMR, &bfin_timer_irq);
#endif
}

/*
 * Should return useconds since last timer tick
 */
#ifndef CONFIG_GENERIC_TIME
static unsigned long gettimeoffset(void)
{
	unsigned long offset;
	unsigned long clocks_per_jiffy;

#if defined(CONFIG_TICK_SOURCE_SYSTMR0) || defined(CONFIG_IPIPE)
	clocks_per_jiffy = bfin_read_TIMER0_PERIOD();
	offset = bfin_read_TIMER0_COUNTER() / \
		(((clocks_per_jiffy + 1) * HZ) / USEC_PER_SEC);

	if ((get_gptimer_status(0) & TIMER_STATUS_TIMIL0) && offset < (100000 / HZ / 2))
		offset += (USEC_PER_SEC / HZ);
#else
	clocks_per_jiffy = bfin_read_TPERIOD();
	offset = (clocks_per_jiffy - bfin_read_TCOUNT()) / \
		(((clocks_per_jiffy + 1) * HZ) / USEC_PER_SEC);

	/* Check if we just wrapped the counters and maybe missed a tick */
	if ((bfin_read_ILAT() & (1 << IRQ_CORETMR))
		&& (offset < (100000 / HZ / 2)))
		offset += (USEC_PER_SEC / HZ);
#endif
	return offset;
}
#endif

static inline int set_rtc_mmss(unsigned long nowtime)
{
	return 0;
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
#ifdef CONFIG_CORE_TIMER_IRQ_L1
__attribute__((l1_text))
#endif
irqreturn_t timer_interrupt(int irq, void *dummy)
{
	/* last time the cmos clock got updated */
	static long last_rtc_update;

	write_seqlock(&xtime_lock);
#if defined(CONFIG_TICK_SOURCE_SYSTMR0) && !defined(CONFIG_IPIPE)
/* FIXME: Here TIMIL0 is not set when IPIPE enabled, why? */
	if (get_gptimer_status(0) & TIMER_STATUS_TIMIL0) {
#endif
		do_timer(1);

		/*
		 * If we have an externally synchronized Linux clock, then update
		 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
		 * called as close as possible to 500 ms before the new second starts.
		 */
		if (ntp_synced() &&
		    xtime.tv_sec > last_rtc_update + 660 &&
		    (xtime.tv_nsec / NSEC_PER_USEC) >=
		    500000 - ((unsigned)TICK_SIZE) / 2
		    && (xtime.tv_nsec / NSEC_PER_USEC) <=
		    500000 + ((unsigned)TICK_SIZE) / 2) {
			if (set_rtc_mmss(xtime.tv_sec) == 0)
				last_rtc_update = xtime.tv_sec;
			else
				/* Do it again in 60s. */
				last_rtc_update = xtime.tv_sec - 600;
		}
#if defined(CONFIG_TICK_SOURCE_SYSTMR0) && !defined(CONFIG_IPIPE)
		set_gptimer_status(0, TIMER_STATUS_TIMIL0);
	}
#endif
	write_sequnlock(&xtime_lock);

#ifdef CONFIG_IPIPE
	update_root_process_times(get_irq_regs());
#else
	update_process_times(user_mode(get_irq_regs()));
#endif
	profile_tick(CPU_PROFILING);

	return IRQ_HANDLED;
}

void __init time_init(void)
{
	time_t secs_since_1970 = (365 * 37 + 9) * 24 * 60 * 60;	/* 1 Jan 2007 */

#ifdef CONFIG_RTC_DRV_BFIN
	/* [#2663] hack to filter junk RTC values that would cause
	 * userspace to have to deal with time values greater than
	 * 2^31 seconds (which uClibc cannot cope with yet)
	 */
	if ((bfin_read_RTC_STAT() & 0xC0000000) == 0xC0000000) {
		printk(KERN_NOTICE "bfin-rtc: invalid date; resetting\n");
		bfin_write_RTC_STAT(0);
	}
#endif

	/* Initialize xtime. From now on, xtime is updated with timer interrupts */
	xtime.tv_sec = secs_since_1970;
	xtime.tv_nsec = 0;

	wall_to_monotonic.tv_sec = -xtime.tv_sec;

	time_sched_init(timer_interrupt);
}

#ifndef CONFIG_GENERIC_TIME
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long seq;
	unsigned long usec, sec;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);
		usec = gettimeoffset();
		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / NSEC_PER_USEC);
	}
	while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	while (usec >= USEC_PER_SEC) {
		usec -= USEC_PER_SEC;
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
	 * This is revolting. We need to set the xtime.tv_usec
	 * correctly. However, the value in this location is
	 * is value at the last tick.
	 * Discover what correction gettimeofday
	 * would have done, and then undo it!
	 */
	nsec -= (gettimeoffset() * NSEC_PER_USEC);

	wtm_sec = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	ntp_clear();

	write_sequnlock_irq(&xtime_lock);
	clock_was_set();

	return 0;
}
EXPORT_SYMBOL(do_settimeofday);
#endif /* !CONFIG_GENERIC_TIME */

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies *(NSEC_PER_SEC / HZ);
}
