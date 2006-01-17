/*
 *  arch/sh/kernel/time.c
 *
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 2002, 2003, 2004, 2005  Paul Mundt
 *  Copyright (C) 2002  M. R. Brown  <mrbrown@linux-sh.org>
 *
 *  Some code taken from i386 version.
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/profile.h>
#include <asm/clock.h>
#include <asm/rtc.h>
#include <asm/timer.h>
#include <asm/kgdb.h>

extern unsigned long wall_jiffies;
struct sys_timer *sys_timer;

/* Move this somewhere more sensible.. */
DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

/* XXX: Can we initialize this in a routine somewhere?  Dreamcast doesn't want
 * these routines anywhere... */
#ifdef CONFIG_SH_RTC
void (*rtc_get_time)(struct timespec *) = sh_rtc_gettimeofday;
int (*rtc_set_time)(const time_t) = sh_rtc_settimeofday;
#else
void (*rtc_get_time)(struct timespec *);
int (*rtc_set_time)(const time_t);
#endif

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long __attribute__ ((weak)) sched_clock(void)
{
	return (unsigned long long)jiffies * (1000000000 / HZ);
}

void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;
	unsigned long lost;

	do {
		seq = read_seqbegin(&xtime_lock);
		usec = get_timer_offset();

		lost = jiffies - wall_jiffies;
		if (lost)
			usec += lost * (1000000 / HZ);

		sec = xtime.tv_sec;
		usec += xtime.tv_nsec / 1000;
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
	nsec -= 1000 * (get_timer_offset() +
				(jiffies - wall_jiffies) * (1000000 / HZ));

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

/* last time the RTC clock got updated */
static long last_rtc_update;

/*
 * handle_timer_tick() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
void handle_timer_tick(struct pt_regs *regs)
{
	do_timer(regs);
#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
#endif
	profile_tick(CPU_PROFILING, regs);

#ifdef CONFIG_HEARTBEAT
	if (sh_mv.mv_heartbeat != NULL)
		sh_mv.mv_heartbeat();
#endif

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * RTC clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (ntp_synced() &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2) {
		if (rtc_set_time(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			/* do it again in 60s */
			last_rtc_update = xtime.tv_sec - 600;
	}
}

static struct sysdev_class timer_sysclass = {
	set_kset_name("timer"),
};

static int __init timer_init_sysfs(void)
{
	int ret = sysdev_class_register(&timer_sysclass);
	if (ret != 0)
		return ret;

	sys_timer->dev.cls = &timer_sysclass;
	return sysdev_register(&sys_timer->dev);
}

device_initcall(timer_init_sysfs);

void (*board_time_init)(void);

void __init time_init(void)
{
	if (board_time_init)
		board_time_init();

	clk_init();

	if (rtc_get_time) {
		rtc_get_time(&xtime);
	} else {
		xtime.tv_sec = mktime(2000, 1, 1, 0, 0, 0);
		xtime.tv_nsec = 0;
	}

        set_normalized_timespec(&wall_to_monotonic,
                                -xtime.tv_sec, -xtime.tv_nsec);

	/*
	 * Find the timer to use as the system timer, it will be
	 * initialized for us.
	 */
	sys_timer = get_sys_timer();
	printk(KERN_INFO "Using %s for system timer\n", sys_timer->name);

#if defined(CONFIG_SH_KGDB)
	/*
	 * Set up kgdb as requested. We do it here because the serial
	 * init uses the timer vars we just set up for figuring baud.
	 */
	kgdb_init();
#endif
}
