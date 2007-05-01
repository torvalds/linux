/*
 * Copyright (C) 2004-2007 Atmel Corporation
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
#include <linux/err.h>

#include <asm/div64.h>
#include <asm/sysreg.h>
#include <asm/io.h>
#include <asm/sections.h>

/* how many counter cycles in a jiffy? */
static u32 cycles_per_jiffy;

/* the count value for the next timer interrupt */
static u32 expirelo;

cycle_t __weak read_cycle_count(void)
{
	return (cycle_t)sysreg_read(COUNT);
}

struct clocksource __weak clocksource_avr32 = {
	.name		= "avr32",
	.rating		= 350,
	.read		= read_cycle_count,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 16,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

irqreturn_t __weak timer_interrupt(int irq, void *dev_id);

struct irqaction timer_irqaction = {
	.handler	= timer_interrupt,
	.flags		= IRQF_DISABLED,
	.name		= "timer",
};

/*
 * By default we provide the null RTC ops
 */
static unsigned long null_rtc_get_time(void)
{
	return mktime(2007, 1, 1, 0, 0, 0);
}

static int null_rtc_set_time(unsigned long sec)
{
	return 0;
}

static unsigned long (*rtc_get_time)(void) = null_rtc_get_time;
static int (*rtc_set_time)(unsigned long) = null_rtc_set_time;

static void avr32_timer_ack(void)
{
	u32 count;

	/* Ack this timer interrupt and set the next one */
	expirelo += cycles_per_jiffy;
	/* setting COMPARE to 0 stops the COUNT-COMPARE */
	if (expirelo == 0) {
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

int __weak avr32_hpt_init(void)
{
	int ret;
	unsigned long mult, shift, count_hz;

	count_hz = clk_get_rate(boot_cpu_data.clk);
	shift = clocksource_avr32.shift;
	mult = clocksource_hz2mult(count_hz, shift);
	clocksource_avr32.mult = mult;

	{
		u64 tmp;

		tmp = TICK_NSEC;
		tmp <<= shift;
		tmp += mult / 2;
		do_div(tmp, mult);

		cycles_per_jiffy = tmp;
	}

	ret = setup_irq(0, &timer_irqaction);
	if (ret) {
		pr_debug("timer: could not request IRQ 0: %d\n", ret);
		return -ENODEV;
	}

	printk(KERN_INFO "timer: AT32AP COUNT-COMPARE at irq 0, "
			"%lu.%03lu MHz\n",
			((count_hz + 500) / 1000) / 1000,
			((count_hz + 500) / 1000) % 1000);

	return 0;
}

/*
 * Taken from MIPS c0_hpt_timer_init().
 *
 * The reason COUNT is written twice is probably to make sure we don't get any
 * timer interrupts while we are messing with the counter.
 */
int __weak avr32_hpt_start(void)
{
	u32 count = sysreg_read(COUNT);
	expirelo = (count / cycles_per_jiffy + 1) * cycles_per_jiffy;
	sysreg_write(COUNT, expirelo - cycles_per_jiffy);
	sysreg_write(COMPARE, expirelo);
	sysreg_write(COUNT, count);

	return 0;
}

/*
 * local_timer_interrupt() does profiling and process accounting on a
 * per-CPU basis.
 *
 * In UP mode, it is invoked from the (global) timer_interrupt.
 */
void local_timer_interrupt(int irq, void *dev_id)
{
	if (current->pid)
		profile_tick(CPU_PROFILING);
	update_process_times(user_mode(get_irq_regs()));
}

irqreturn_t __weak timer_interrupt(int irq, void *dev_id)
{
	/* ack timer interrupt and try to set next interrupt */
	avr32_timer_ack();

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

void __init time_init(void)
{
	int ret;

	/*
	 * Make sure we don't get any COMPARE interrupts before we can
	 * handle them.
	 */
	sysreg_write(COMPARE, 0);

	xtime.tv_sec = rtc_get_time();
	xtime.tv_nsec = 0;

	set_normalized_timespec(&wall_to_monotonic,
				-xtime.tv_sec, -xtime.tv_nsec);

	ret = avr32_hpt_init();
	if (ret) {
		pr_debug("timer: failed setup: %d\n", ret);
		return;
	}

	ret = clocksource_register(&clocksource_avr32);
	if (ret)
		pr_debug("timer: could not register clocksource: %d\n", ret);

	ret = avr32_hpt_start();
	if (ret) {
		pr_debug("timer: failed starting: %d\n", ret);
		return;
	}
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
