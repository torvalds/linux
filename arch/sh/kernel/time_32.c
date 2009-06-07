/*
 *  arch/sh/kernel/time_32.c
 *
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 2002 - 2008  Paul Mundt
 *  Copyright (C) 2002  M. R. Brown  <mrbrown@linux-sh.org>
 *
 *  Some code taken from i386 version.
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/profile.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/clockchips.h>
#include <linux/mc146818rtc.h>	/* for rtc_lock */
#include <linux/smp.h>
#include <asm/clock.h>
#include <asm/rtc.h>
#include <asm/timer.h>
#include <asm/kgdb.h>

struct sys_timer *sys_timer;

/* Move this somewhere more sensible.. */
DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

/* Dummy RTC ops */
static void null_rtc_get_time(struct timespec *tv)
{
	tv->tv_sec = mktime(2000, 1, 1, 0, 0, 0);
	tv->tv_nsec = 0;
}

static int null_rtc_set_time(const time_t secs)
{
	return 0;
}

void (*rtc_sh_get_time)(struct timespec *) = null_rtc_get_time;
int (*rtc_sh_set_time)(const time_t) = null_rtc_set_time;

#ifndef CONFIG_GENERIC_TIME
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long seq;
	unsigned long usec, sec;

	do {
		/*
		 * Turn off IRQs when grabbing xtime_lock, so that
		 * the sys_timer get_offset code doesn't have to handle it.
		 */
		seq = read_seqbegin_irqsave(&xtime_lock, flags);
		usec = get_timer_offset();
		sec = xtime.tv_sec;
		usec += xtime.tv_nsec / NSEC_PER_USEC;
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

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
	nsec -= get_timer_offset() * NSEC_PER_USEC;

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
#endif /* !CONFIG_GENERIC_TIME */

/* last time the RTC clock got updated */
static long last_rtc_update;

/*
 * handle_timer_tick() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
void handle_timer_tick(void)
{
	if (current->pid)
		profile_tick(CPU_PROFILING);

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);
	do_timer(1);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * RTC clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (ntp_synced() &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2) {
		if (rtc_sh_set_time(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			/* do it again in 60s */
			last_rtc_update = xtime.tv_sec - 600;
	}
	write_sequnlock(&xtime_lock);

#ifndef CONFIG_SMP
	update_process_times(user_mode(get_irq_regs()));
#endif
}

#ifdef CONFIG_PM
int timer_suspend(struct sys_device *dev, pm_message_t state)
{
	struct sys_timer *sys_timer = container_of(dev, struct sys_timer, dev);

	sys_timer->ops->stop();

	return 0;
}

int timer_resume(struct sys_device *dev)
{
	struct sys_timer *sys_timer = container_of(dev, struct sys_timer, dev);

	sys_timer->ops->start();

	return 0;
}
#else
#define timer_suspend NULL
#define timer_resume NULL
#endif

static struct sysdev_class timer_sysclass = {
	.name	 = "timer",
	.suspend = timer_suspend,
	.resume	 = timer_resume,
};

static int __init timer_init_sysfs(void)
{
	int ret;

	if (!sys_timer)
		return 0;

	ret = sysdev_class_register(&timer_sysclass);
	if (ret != 0)
		return ret;

	sys_timer->dev.cls = &timer_sysclass;
	return sysdev_register(&sys_timer->dev);
}
device_initcall(timer_init_sysfs);

void (*board_time_init)(void);

struct clocksource clocksource_sh = {
	.name		= "SuperH",
};

#ifdef CONFIG_GENERIC_TIME
unsigned long long sched_clock(void)
{
	unsigned long long cycles;

	/* jiffies based sched_clock if no clocksource is installed */
	if (!clocksource_sh.rating)
		return (unsigned long long)jiffies * (NSEC_PER_SEC / HZ);

	cycles = clocksource_sh.read(&clocksource_sh);
	return cyc2ns(&clocksource_sh, cycles);
}
#endif

void __init time_init(void)
{
	if (board_time_init)
		board_time_init();

	clk_init();

	rtc_sh_get_time(&xtime);
	set_normalized_timespec(&wall_to_monotonic,
				-xtime.tv_sec, -xtime.tv_nsec);

#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
	local_timer_setup(smp_processor_id());
#endif

	/*
	 * Find the timer to use as the system timer, it will be
	 * initialized for us.
	 */
	sys_timer = get_sys_timer();
	if (unlikely(!sys_timer))
		panic("System timer missing.\n");

	printk(KERN_INFO "Using %s for system timer\n", sys_timer->name);
}
