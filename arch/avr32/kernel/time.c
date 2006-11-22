/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * Based on MIPS implementation arch/mips/kernel/time.c
 *   Copyright 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/profile.h>
#include <linux/sysdev.h>

#include <asm/div64.h>
#include <asm/sysreg.h>
#include <asm/io.h>
#include <asm/sections.h>

static cycle_t read_cycle_count(void)
{
	return (cycle_t)sysreg_read(COUNT);
}

static struct clocksource clocksource_avr32 = {
	.name		= "avr32",
	.rating		= 350,
	.read		= read_cycle_count,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 16,
	.is_continuous	= 1,
};

/*
 * By default we provide the null RTC ops
 */
static unsigned long null_rtc_get_time(void)
{
	return mktime(2004, 1, 1, 0, 0, 0);
}

static int null_rtc_set_time(unsigned long sec)
{
	return 0;
}

static unsigned long (*rtc_get_time)(void) = null_rtc_get_time;
static int (*rtc_set_time)(unsigned long) = null_rtc_set_time;

/* how many counter cycles in a jiffy? */
static unsigned long cycles_per_jiffy;

/* cycle counter value at the previous timer interrupt */
static unsigned int timerhi, timerlo;

/* the count value for the next timer interrupt */
static unsigned int expirelo;

static void avr32_timer_ack(void)
{
	unsigned int count;

	/* Ack this timer interrupt and set the next one */
	expirelo += cycles_per_jiffy;
	if (expirelo == 0) {
		printk(KERN_DEBUG "expirelo == 0\n");
		sysreg_write(COMPARE, expirelo + 1);
	} else {
		sysreg_write(COMPARE, expirelo);
	}

	/* Check to see if we have missed any timer interrupts */
	count = sysreg_read(COUNT);
	if ((count - expirelo) < 0x7fffffff) {
		expirelo = count + cycles_per_jiffy;
		sysreg_write(COMPARE, expirelo);
	}
}

static unsigned int avr32_hpt_read(void)
{
	return sysreg_read(COUNT);
}

/*
 * Taken from MIPS c0_hpt_timer_init().
 *
 * Why is it so complicated, and what is "count"?  My assumption is
 * that `count' specifies the "reference cycle", i.e. the cycle since
 * reset that should mean "zero". The reason COUNT is written twice is
 * probably to make sure we don't get any timer interrupts while we
 * are messing with the counter.
 */
static void avr32_hpt_init(unsigned int count)
{
	count = sysreg_read(COUNT) - count;
	expirelo = (count / cycles_per_jiffy + 1) * cycles_per_jiffy;
	sysreg_write(COUNT, expirelo - cycles_per_jiffy);
	sysreg_write(COMPARE, expirelo);
	sysreg_write(COUNT, count);
}

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	/* There must be better ways...? */
	return (unsigned long long)jiffies * (1000000000 / HZ);
}

/*
 * local_timer_interrupt() does profiling and process accounting on a
 * per-CPU basis.
 *
 * In UP mode, it is invoked from the (global) timer_interrupt.
 */
static void local_timer_interrupt(int irq, void *dev_id)
{
	if (current->pid)
		profile_tick(CPU_PROFILING);
	update_process_times(user_mode(get_irq_regs()));
}

static irqreturn_t
timer_interrupt(int irq, void *dev_id)
{
	unsigned int count;

	/* ack timer interrupt and try to set next interrupt */
	count = avr32_hpt_read();
	avr32_timer_ack();

	/* Update timerhi/timerlo for intra-jiffy calibration */
	timerhi += count < timerlo;	/* Wrap around */
	timerlo = count;

	/*
	 * Call the generic timer interrupt handler
	 */
	write_seqlock(&xtime_lock);
	do_timer(1);
	write_sequnlock(&xtime_lock);

	/*
	 * In UP mode, we call local_timer_interrupt() to do profiling
	 * and process accounting.
	 *
	 * SMP is not supported yet.
	 */
	local_timer_interrupt(irq, dev_id);

	return IRQ_HANDLED;
}

static struct irqaction timer_irqaction = {
	.handler	= timer_interrupt,
	.flags		= IRQF_DISABLED,
	.name		= "timer",
};

void __init time_init(void)
{
	unsigned long mult, shift, count_hz;
	int ret;

	xtime.tv_sec = rtc_get_time();
	xtime.tv_nsec = 0;

	set_normalized_timespec(&wall_to_monotonic,
				-xtime.tv_sec, -xtime.tv_nsec);

	printk("Before time_init: count=%08lx, compare=%08lx\n",
	       (unsigned long)sysreg_read(COUNT),
	       (unsigned long)sysreg_read(COMPARE));

	count_hz = clk_get_rate(boot_cpu_data.clk);
	shift = clocksource_avr32.shift;
	mult = clocksource_hz2mult(count_hz, shift);
	clocksource_avr32.mult = mult;

	printk("Cycle counter: mult=%lu, shift=%lu\n", mult, shift);

	{
		u64 tmp;

		tmp = TICK_NSEC;
		tmp <<= shift;
		tmp += mult / 2;
		do_div(tmp, mult);

		cycles_per_jiffy = tmp;
	}

	/* This sets up the high precision timer for the first interrupt. */
	avr32_hpt_init(avr32_hpt_read());

	printk("After time_init: count=%08lx, compare=%08lx\n",
	       (unsigned long)sysreg_read(COUNT),
	       (unsigned long)sysreg_read(COMPARE));

	ret = clocksource_register(&clocksource_avr32);
	if (ret)
		printk(KERN_ERR
		       "timer: could not register clocksource: %d\n", ret);

	ret = setup_irq(0, &timer_irqaction);
	if (ret)
		printk("timer: could not request IRQ 0: %d\n", ret);
}

static struct sysdev_class timer_class = {
	set_kset_name("timer"),
};

static struct sys_device timer_device = {
	.id	= 0,
	.cls	= &timer_class,
};

static int __init init_timer_sysfs(void)
{
	int err = sysdev_class_register(&timer_class);
	if (!err)
		err = sysdev_register(&timer_device);
	return err;
}

device_initcall(init_timer_sysfs);
