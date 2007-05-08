/*
 *  linux/arch/arm/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Modifications for ARM (C) 1994-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains the ARM-specific time handling details:
 *  reading the RTC at bootup, etc...
 *
 *  1994-07-02  Alan Modra
 *              fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 *  1998-12-20  Updated NTP code according to technical memorandum Jan '96
 *              "A Kernel Model for Precision Timekeeping" by Dave Mills
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/timex.h>
#include <linux/errno.h>
#include <linux/profile.h>
#include <linux/sysdev.h>
#include <linux/timer.h>
#include <linux/irq.h>

#include <linux/mc146818rtc.h>

#include <asm/leds.h>
#include <asm/thread_info.h>
#include <asm/mach/time.h>

/*
 * Our system timer.
 */
struct sys_timer *system_timer;

#if defined(CONFIG_RTC_DRV_CMOS) || defined(CONFIG_RTC_DRV_CMOS_MODULE)
/* this needs a better home */
DEFINE_SPINLOCK(rtc_lock);

#ifdef CONFIG_RTC_DRV_CMOS_MODULE
EXPORT_SYMBOL(rtc_lock);
#endif
#endif	/* pc-style 'CMOS' RTC support */

/* change this if you have some constant time drift */
#define USECS_PER_JIFFY	(1000000/HZ)

#ifdef CONFIG_SMP
unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long fp, pc = instruction_pointer(regs);

	if (in_lock_functions(pc)) {
		fp = regs->ARM_fp;
		pc = pc_pointer(((unsigned long *)fp)[-1]);
	}

	return pc;
}
EXPORT_SYMBOL(profile_pc);
#endif

/*
 * hook for setting the RTC's idea of the current time.
 */
int (*set_rtc)(void);

#ifndef CONFIG_GENERIC_TIME
static unsigned long dummy_gettimeoffset(void)
{
	return 0;
}
#endif

/*
 * An implementation of printk_clock() independent from
 * sched_clock().  This avoids non-bootable kernels when
 * printk_clock is enabled.
 */
unsigned long long printk_clock(void)
{
	return (unsigned long long)(jiffies - INITIAL_JIFFIES) *
			(1000000000 / HZ);
}

static unsigned long next_rtc_update;

/*
 * If we have an externally synchronized linux clock, then update
 * CMOS clock accordingly every ~11 minutes.  set_rtc() has to be
 * called as close as possible to 500 ms before the new second
 * starts.
 */
static inline void do_set_rtc(void)
{
	if (!ntp_synced() || set_rtc == NULL)
		return;

	if (next_rtc_update &&
	    time_before((unsigned long)xtime.tv_sec, next_rtc_update))
		return;

	if (xtime.tv_nsec < 500000000 - ((unsigned) tick_nsec >> 1) &&
	    xtime.tv_nsec >= 500000000 + ((unsigned) tick_nsec >> 1))
		return;

	if (set_rtc())
		/*
		 * rtc update failed.  Try again in 60s
		 */
		next_rtc_update = xtime.tv_sec + 60;
	else
		next_rtc_update = xtime.tv_sec + 660;
}

#ifdef CONFIG_LEDS

static void dummy_leds_event(led_event_t evt)
{
}

void (*leds_event)(led_event_t) = dummy_leds_event;

struct leds_evt_name {
	const char	name[8];
	int		on;
	int		off;
};

static const struct leds_evt_name evt_names[] = {
	{ "amber", led_amber_on, led_amber_off },
	{ "blue",  led_blue_on,  led_blue_off  },
	{ "green", led_green_on, led_green_off },
	{ "red",   led_red_on,   led_red_off   },
};

static ssize_t leds_store(struct sys_device *dev, const char *buf, size_t size)
{
	int ret = -EINVAL, len = strcspn(buf, " ");

	if (len > 0 && buf[len] == '\0')
		len--;

	if (strncmp(buf, "claim", len) == 0) {
		leds_event(led_claim);
		ret = size;
	} else if (strncmp(buf, "release", len) == 0) {
		leds_event(led_release);
		ret = size;
	} else {
		int i;

		for (i = 0; i < ARRAY_SIZE(evt_names); i++) {
			if (strlen(evt_names[i].name) != len ||
			    strncmp(buf, evt_names[i].name, len) != 0)
				continue;
			if (strncmp(buf+len, " on", 3) == 0) {
				leds_event(evt_names[i].on);
				ret = size;
			} else if (strncmp(buf+len, " off", 4) == 0) {
				leds_event(evt_names[i].off);
				ret = size;
			}
			break;
		}
	}
	return ret;
}

static SYSDEV_ATTR(event, 0200, NULL, leds_store);

static int leds_suspend(struct sys_device *dev, pm_message_t state)
{
	leds_event(led_stop);
	return 0;
}

static int leds_resume(struct sys_device *dev)
{
	leds_event(led_start);
	return 0;
}

static int leds_shutdown(struct sys_device *dev)
{
	leds_event(led_halted);
	return 0;
}

static struct sysdev_class leds_sysclass = {
	set_kset_name("leds"),
	.shutdown	= leds_shutdown,
	.suspend	= leds_suspend,
	.resume		= leds_resume,
};

static struct sys_device leds_device = {
	.id		= 0,
	.cls		= &leds_sysclass,
};

static int __init leds_init(void)
{
	int ret;
	ret = sysdev_class_register(&leds_sysclass);
	if (ret == 0)
		ret = sysdev_register(&leds_device);
	if (ret == 0)
		ret = sysdev_create_file(&leds_device, &attr_event);
	return ret;
}

device_initcall(leds_init);

EXPORT_SYMBOL(leds_event);
#endif

#ifdef CONFIG_LEDS_TIMER
static inline void do_leds(void)
{
	static unsigned int count = HZ/2;

	if (--count == 0) {
		count = HZ/2;
		leds_event(led_timer);
	}
}
#else
#define	do_leds()
#endif

#ifndef CONFIG_GENERIC_TIME
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long seq;
	unsigned long usec, sec;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);
		usec = system_timer->offset();
		sec = xtime.tv_sec;
		usec += xtime.tv_nsec / 1000;
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	/* usec may have gone up a lot: be safe */
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
	 * done, and then undo it!
	 */
	nsec -= system_timer->offset() * NSEC_PER_USEC;

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

/**
 * save_time_delta - Save the offset between system time and RTC time
 * @delta: pointer to timespec to store delta
 * @rtc: pointer to timespec for current RTC time
 *
 * Return a delta between the system time and the RTC time, such
 * that system time can be restored later with restore_time_delta()
 */
void save_time_delta(struct timespec *delta, struct timespec *rtc)
{
	set_normalized_timespec(delta,
				xtime.tv_sec - rtc->tv_sec,
				xtime.tv_nsec - rtc->tv_nsec);
}
EXPORT_SYMBOL(save_time_delta);

/**
 * restore_time_delta - Restore the current system time
 * @delta: delta returned by save_time_delta()
 * @rtc: pointer to timespec for current RTC time
 */
void restore_time_delta(struct timespec *delta, struct timespec *rtc)
{
	struct timespec ts;

	set_normalized_timespec(&ts,
				delta->tv_sec + rtc->tv_sec,
				delta->tv_nsec + rtc->tv_nsec);

	do_settimeofday(&ts);
}
EXPORT_SYMBOL(restore_time_delta);

#ifndef CONFIG_GENERIC_CLOCKEVENTS
/*
 * Kernel system timer support.
 */
void timer_tick(void)
{
	profile_tick(CPU_PROFILING);
	do_leds();
	do_set_rtc();
	do_timer(1);
#ifndef CONFIG_SMP
	update_process_times(user_mode(get_irq_regs()));
#endif
}
#endif

#if defined(CONFIG_PM) && !defined(CONFIG_GENERIC_CLOCKEVENTS)
static int timer_suspend(struct sys_device *dev, pm_message_t state)
{
	struct sys_timer *timer = container_of(dev, struct sys_timer, dev);

	if (timer->suspend != NULL)
		timer->suspend();

	return 0;
}

static int timer_resume(struct sys_device *dev)
{
	struct sys_timer *timer = container_of(dev, struct sys_timer, dev);

	if (timer->resume != NULL)
		timer->resume();

	return 0;
}
#else
#define timer_suspend NULL
#define timer_resume NULL
#endif

static struct sysdev_class timer_sysclass = {
	set_kset_name("timer"),
	.suspend	= timer_suspend,
	.resume		= timer_resume,
};

#ifdef CONFIG_NO_IDLE_HZ
static int timer_dyn_tick_enable(void)
{
	struct dyn_tick_timer *dyn_tick = system_timer->dyn_tick;
	unsigned long flags;
	int ret = -ENODEV;

	if (dyn_tick) {
		spin_lock_irqsave(&dyn_tick->lock, flags);
		ret = 0;
		if (!(dyn_tick->state & DYN_TICK_ENABLED)) {
			ret = dyn_tick->enable();

			if (ret == 0)
				dyn_tick->state |= DYN_TICK_ENABLED;
		}
		spin_unlock_irqrestore(&dyn_tick->lock, flags);
	}

	return ret;
}

static int timer_dyn_tick_disable(void)
{
	struct dyn_tick_timer *dyn_tick = system_timer->dyn_tick;
	unsigned long flags;
	int ret = -ENODEV;

	if (dyn_tick) {
		spin_lock_irqsave(&dyn_tick->lock, flags);
		ret = 0;
		if (dyn_tick->state & DYN_TICK_ENABLED) {
			ret = dyn_tick->disable();

			if (ret == 0)
				dyn_tick->state &= ~DYN_TICK_ENABLED;
		}
		spin_unlock_irqrestore(&dyn_tick->lock, flags);
	}

	return ret;
}

/*
 * Reprogram the system timer for at least the calculated time interval.
 * This function should be called from the idle thread with IRQs disabled,
 * immediately before sleeping.
 */
void timer_dyn_reprogram(void)
{
	struct dyn_tick_timer *dyn_tick = system_timer->dyn_tick;
	unsigned long next, seq, flags;

	if (!dyn_tick)
		return;

	spin_lock_irqsave(&dyn_tick->lock, flags);
	if (dyn_tick->state & DYN_TICK_ENABLED) {
		next = next_timer_interrupt();
		do {
			seq = read_seqbegin(&xtime_lock);
			dyn_tick->reprogram(next - jiffies);
		} while (read_seqretry(&xtime_lock, seq));
	}
	spin_unlock_irqrestore(&dyn_tick->lock, flags);
}

static ssize_t timer_show_dyn_tick(struct sys_device *dev, char *buf)
{
	return sprintf(buf, "%i\n",
		       (system_timer->dyn_tick->state & DYN_TICK_ENABLED) >> 1);
}

static ssize_t timer_set_dyn_tick(struct sys_device *dev, const char *buf,
				  size_t count)
{
	unsigned int enable = simple_strtoul(buf, NULL, 2);

	if (enable)
		timer_dyn_tick_enable();
	else
		timer_dyn_tick_disable();

	return count;
}
static SYSDEV_ATTR(dyn_tick, 0644, timer_show_dyn_tick, timer_set_dyn_tick);

/*
 * dyntick=enable|disable
 */
static char dyntick_str[4] __initdata = "";

static int __init dyntick_setup(char *str)
{
	if (str)
		strlcpy(dyntick_str, str, sizeof(dyntick_str));
	return 1;
}

__setup("dyntick=", dyntick_setup);
#endif

static int __init timer_init_sysfs(void)
{
	int ret = sysdev_class_register(&timer_sysclass);
	if (ret == 0) {
		system_timer->dev.cls = &timer_sysclass;
		ret = sysdev_register(&system_timer->dev);
	}

#ifdef CONFIG_NO_IDLE_HZ
	if (ret == 0 && system_timer->dyn_tick) {
		ret = sysdev_create_file(&system_timer->dev, &attr_dyn_tick);

		/*
		 * Turn on dynamic tick after calibrate delay
		 * for correct bogomips
		 */
		if (ret == 0 && dyntick_str[0] == 'e')
			ret = timer_dyn_tick_enable();
	}
#endif

	return ret;
}

device_initcall(timer_init_sysfs);

void __init time_init(void)
{
#ifndef CONFIG_GENERIC_TIME
	if (system_timer->offset == NULL)
		system_timer->offset = dummy_gettimeoffset;
#endif
	system_timer->init();

#ifdef CONFIG_NO_IDLE_HZ
	if (system_timer->dyn_tick)
		system_timer->dyn_tick->lock = SPIN_LOCK_UNLOCKED;
#endif
}

