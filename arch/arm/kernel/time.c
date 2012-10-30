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
 */
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/timex.h>
#include <linux/errno.h>
#include <linux/profile.h>
#include <linux/syscore_ops.h>
#include <linux/timer.h>
#include <linux/irq.h>

#include <asm/thread_info.h>
#include <asm/sched_clock.h>
#include <asm/stacktrace.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

/*
 * Our system timer.
 */
static struct sys_timer *system_timer;

#if defined(CONFIG_RTC_DRV_CMOS) || defined(CONFIG_RTC_DRV_CMOS_MODULE) || \
    defined(CONFIG_NVRAM) || defined(CONFIG_NVRAM_MODULE)
/* this needs a better home */
DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);
#endif	/* pc-style 'CMOS' RTC support */

/* change this if you have some constant time drift */
#define USECS_PER_JIFFY	(1000000/HZ)

#ifdef CONFIG_SMP
unsigned long profile_pc(struct pt_regs *regs)
{
	struct stackframe frame;

	if (!in_lock_functions(regs->ARM_pc))
		return regs->ARM_pc;

	frame.fp = regs->ARM_fp;
	frame.sp = regs->ARM_sp;
	frame.lr = regs->ARM_lr;
	frame.pc = regs->ARM_pc;
	do {
		int ret = unwind_frame(&frame);
		if (ret < 0)
			return 0;
	} while (in_lock_functions(frame.pc));

	return frame.pc;
}
EXPORT_SYMBOL(profile_pc);
#endif

#ifdef CONFIG_ARCH_USES_GETTIMEOFFSET
u32 arch_gettimeoffset(void)
{
	if (system_timer->offset != NULL)
		return system_timer->offset() * 1000;

	return 0;
}
#endif /* CONFIG_ARCH_USES_GETTIMEOFFSET */

#ifndef CONFIG_GENERIC_CLOCKEVENTS
/*
 * Kernel system timer support.
 */
void timer_tick(void)
{
	profile_tick(CPU_PROFILING);
	xtime_update(1);
#ifndef CONFIG_SMP
	update_process_times(user_mode(get_irq_regs()));
#endif
}
#endif

static void dummy_clock_access(struct timespec *ts)
{
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
}

static clock_access_fn __read_persistent_clock = dummy_clock_access;
static clock_access_fn __read_boot_clock = dummy_clock_access;;

void read_persistent_clock(struct timespec *ts)
{
	__read_persistent_clock(ts);
}

void read_boot_clock(struct timespec *ts)
{
	__read_boot_clock(ts);
}

int __init register_persistent_clock(clock_access_fn read_boot,
				     clock_access_fn read_persistent)
{
	/* Only allow the clockaccess functions to be registered once */
	if (__read_persistent_clock == dummy_clock_access &&
	    __read_boot_clock == dummy_clock_access) {
		if (read_boot)
			__read_boot_clock = read_boot;
		if (read_persistent)
			__read_persistent_clock = read_persistent;

		return 0;
	}

	return -EINVAL;
}

#if defined(CONFIG_PM) && !defined(CONFIG_GENERIC_CLOCKEVENTS)
static int timer_suspend(void)
{
	if (system_timer->suspend)
		system_timer->suspend();

	return 0;
}

static void timer_resume(void)
{
	if (system_timer->resume)
		system_timer->resume();
}
#else
#define timer_suspend NULL
#define timer_resume NULL
#endif

static struct syscore_ops timer_syscore_ops = {
	.suspend	= timer_suspend,
	.resume		= timer_resume,
};

static int __init timer_init_syscore_ops(void)
{
	register_syscore_ops(&timer_syscore_ops);

	return 0;
}

device_initcall(timer_init_syscore_ops);

void __init time_init(void)
{
	system_timer = machine_desc->timer;
	system_timer->init();
	sched_clock_postinit();
}

