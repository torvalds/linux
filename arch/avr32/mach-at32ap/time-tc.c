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

#include <asm/arch/time.h>

/* how many counter cycles in a jiffy? */
static u32 cycles_per_jiffy;

/* the count value for the next timer interrupt */
static u32 expirelo;

/* the I/O registers of the TC module */
static void __iomem *ioregs;

cycle_t read_cycle_count(void)
{
	return (cycle_t)timer_read(ioregs, 0, CV);
}

struct clocksource clocksource_avr32 = {
	.name		= "avr32",
	.rating		= 342,
	.read		= read_cycle_count,
	.mask		= CLOCKSOURCE_MASK(16),
	.shift		= 16,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void avr32_timer_ack(void)
{
	u16 count = expirelo;

	/* Ack this timer interrupt and set the next one, use a u16
	 * variable so it will wrap around correctly */
	count += cycles_per_jiffy;
	expirelo = count;
	timer_write(ioregs, 0, RC, expirelo);

	/* Check to see if we have missed any timer interrupts */
	count = timer_read(ioregs, 0, CV);
	if ((count - expirelo) < 0x7fff) {
		expirelo = count + cycles_per_jiffy;
		timer_write(ioregs, 0, RC, expirelo);
	}
}

u32 avr32_hpt_read(void)
{
	return timer_read(ioregs, 0, CV);
}

static int avr32_timer_calc_div_and_set_jiffies(struct clk *pclk)
{
	unsigned int cycles_max = (clocksource_avr32.mask + 1) / 2;
	unsigned int divs[] = { 4, 8, 16, 32 };
	int divs_size = ARRAY_SIZE(divs);
	int i = 0;
	unsigned long count_hz;
	unsigned long shift;
	unsigned long mult;
	int clock_div = -1;
	u64 tmp;

	shift = clocksource_avr32.shift;

	do {
		count_hz = clk_get_rate(pclk) / divs[i];
		mult = clocksource_hz2mult(count_hz, shift);
		clocksource_avr32.mult = mult;

		tmp = TICK_NSEC;
		tmp <<= shift;
		tmp += mult / 2;
		do_div(tmp, mult);

		cycles_per_jiffy = tmp;
	} while (cycles_per_jiffy > cycles_max && ++i < divs_size);

	clock_div = i + 1;

	if (clock_div > divs_size) {
		pr_debug("timer: could not calculate clock divider\n");
		return -EFAULT;
	}

	/* Set the clock divider */
	timer_write(ioregs, 0, CMR, TIMER_BF(CMR_TCCLKS, clock_div));

	return 0;
}

int avr32_hpt_init(unsigned int count)
{
	struct resource *regs;
	struct clk *pclk;
	int irq = -1;
	int ret = 0;

	ret = -ENXIO;

	irq = platform_get_irq(&at32_systc0_device, 0);
	if (irq < 0) {
		pr_debug("timer: could not get irq\n");
		goto out_error;
	}

	pclk = clk_get(&at32_systc0_device.dev, "pclk");
	if (IS_ERR(pclk)) {
		pr_debug("timer: could not get clk: %ld\n", PTR_ERR(pclk));
		goto out_error;
	}
	clk_enable(pclk);

	regs = platform_get_resource(&at32_systc0_device, IORESOURCE_MEM, 0);
	if (!regs) {
		pr_debug("timer: could not get resource\n");
		goto out_error_clk;
	}

	ioregs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!ioregs) {
		pr_debug("timer: could not get ioregs\n");
		goto out_error_clk;
	}

	ret = avr32_timer_calc_div_and_set_jiffies(pclk);
	if (ret)
		goto out_error_io;

	ret = setup_irq(irq, &timer_irqaction);
	if (ret) {
		pr_debug("timer: could not request irq %d: %d\n",
				irq, ret);
		goto out_error_io;
	}

	expirelo = (timer_read(ioregs, 0, CV) / cycles_per_jiffy + 1)
		* cycles_per_jiffy;

	/* Enable clock and interrupts on RC compare */
	timer_write(ioregs, 0, CCR, TIMER_BIT(CCR_CLKEN));
	timer_write(ioregs, 0, IER, TIMER_BIT(IER_CPCS));
	/* Set cycles to first interrupt */
	timer_write(ioregs, 0,  RC, expirelo);

	printk(KERN_INFO "timer: AT32AP system timer/counter at 0x%p irq %d\n",
			ioregs, irq);

	return 0;

out_error_io:
	iounmap(ioregs);
out_error_clk:
	clk_put(pclk);
out_error:
	return ret;
}

int avr32_hpt_start(void)
{
	timer_write(ioregs, 0, CCR, TIMER_BIT(CCR_SWTRG));
	return 0;
}

irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	unsigned int sr = timer_read(ioregs, 0, SR);

	if (sr & TIMER_BIT(SR_CPCS)) {
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

	return IRQ_NONE;
}
