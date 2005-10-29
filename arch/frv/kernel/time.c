/* time.c: FRV arch-specific time handling
 *
 * Copyright (C) 2003-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/m68k/kernel/time.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h> /* CONFIG_HEARTBEAT */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/profile.h>
#include <linux/irq.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/timer-regs.h>
#include <asm/mb-regs.h>
#include <asm/mb86943a.h>
#include <asm/irq-routing.h>

#include <linux/timex.h>

#define TICK_SIZE (tick_nsec / 1000)

extern unsigned long wall_jiffies;

u64 jiffies_64 = INITIAL_JIFFIES;
EXPORT_SYMBOL(jiffies_64);

unsigned long __nongprelbss __clkin_clock_speed_HZ;
unsigned long __nongprelbss __ext_bus_clock_speed_HZ;
unsigned long __nongprelbss __res_bus_clock_speed_HZ;
unsigned long __nongprelbss __sdram_clock_speed_HZ;
unsigned long __nongprelbss __core_bus_clock_speed_HZ;
unsigned long __nongprelbss __core_clock_speed_HZ;
unsigned long __nongprelbss __dsu_clock_speed_HZ;
unsigned long __nongprelbss __serial_clock_speed_HZ;
unsigned long __delay_loops_MHz;

static irqreturn_t timer_interrupt(int irq, void *dummy, struct pt_regs *regs);

static struct irqaction timer_irq  = {
	timer_interrupt, SA_INTERRUPT, CPU_MASK_NONE, "timer", NULL, NULL
};

static inline int set_rtc_mmss(unsigned long nowtime)
{
	return -1;
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static irqreturn_t timer_interrupt(int irq, void *dummy, struct pt_regs * regs)
{
	/* last time the cmos clock got updated */
	static long last_rtc_update = 0;

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);

	do_timer(regs);
	update_process_times(user_mode(regs));
	profile_tick(CPU_PROFILING, regs);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (ntp_synced() &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2
	    ) {
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}

#ifdef CONFIG_HEARTBEAT
	static unsigned short n;
	n++;
	__set_LEDS(n);
#endif /* CONFIG_HEARTBEAT */

	write_sequnlock(&xtime_lock);
	return IRQ_HANDLED;
}

void time_divisor_init(void)
{
	unsigned short base, pre, prediv;

	/* set the scheduling timer going */
	pre = 1;
	prediv = 4;
	base = __res_bus_clock_speed_HZ / pre / HZ / (1 << prediv);

	__set_TPRV(pre);
	__set_TxCKSL_DATA(0, prediv);
	__set_TCTR(TCTR_SC_CTR0 | TCTR_RL_RW_LH8 | TCTR_MODE_2);
	__set_TCSR_DATA(0, base & 0xff);
	__set_TCSR_DATA(0, base >> 8);
}

void time_init(void)
{
	unsigned int year, mon, day, hour, min, sec;

	extern void arch_gettod(int *year, int *mon, int *day, int *hour, int *min, int *sec);

	/* FIX by dqg : Set to zero for platforms that don't have tod */
	/* without this time is undefined and can overflow time_t, causing  */
	/* very stange errors */
	year = 1980;
	mon = day = 1;
	hour = min = sec = 0;
	arch_gettod (&year, &mon, &day, &hour, &min, &sec);

	if ((year += 1900) < 1970)
		year += 100;
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_nsec = 0;

	/* install scheduling interrupt handler */
	setup_irq(IRQ_CPU_TIMER0, &timer_irq);

	time_divisor_init();
}

/*
 * This version of gettimeofday has near microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;
	unsigned long max_ntp_tick;

	do {
		unsigned long lost;

		seq = read_seqbegin(&xtime_lock);

		usec = 0;
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
	nsec -= 0 * NSEC_PER_USEC;
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

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	return jiffies_64 * (1000000000 / HZ);
}
