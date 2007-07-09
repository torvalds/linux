/*
 * System Timer Interrupt reconfigured to run in free-run mode.
 * Author: Vitaly Wool
 * Copyright 2004 MontaVista Software Inc.
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*!
 * @file time.c
 * @brief This file contains OS tick and wdog timer implementations.
 *
 * This file contains OS tick and wdog timer implementations.
 *
 * @ingroup Timers
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/hardware.h>
#include <asm/mach/time.h>
#include <asm/io.h>
#include <asm/arch/common.h>

/*!
 * This is the timer interrupt service routine to do required tasks.
 * It also services the WDOG timer at the frequency of twice per WDOG
 * timeout value. For example, if the WDOG's timeout value is 4 (2
 * seconds since the WDOG runs at 0.5Hz), it will be serviced once
 * every 2/2=1 second.
 *
 * @param  irq          GPT interrupt source number (not used)
 * @param  dev_id       this parameter is not used
 * @return always returns \b IRQ_HANDLED as defined in
 *         include/linux/interrupt.h.
 */
static irqreturn_t mxc_timer_interrupt(int irq, void *dev_id)
{
	unsigned int next_match;

	write_seqlock(&xtime_lock);

	if (__raw_readl(MXC_GPT_GPTSR) & GPTSR_OF1) {
		do {
			timer_tick();
			next_match = __raw_readl(MXC_GPT_GPTOCR1) + LATCH;
			__raw_writel(GPTSR_OF1, MXC_GPT_GPTSR);
			__raw_writel(next_match, MXC_GPT_GPTOCR1);
		} while ((signed long)(next_match -
				       __raw_readl(MXC_GPT_GPTCNT)) <= 0);
	}

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

/*!
 * This function is used to obtain the number of microseconds since the last
 * timer interrupt. Note that interrupts is disabled by do_gettimeofday().
 *
 * @return the number of microseconds since the last timer interrupt.
 */
static unsigned long mxc_gettimeoffset(void)
{
	unsigned long ticks_to_match, elapsed, usec, tick_usec, i;

	/* Get ticks before next timer match */
	ticks_to_match =
	    __raw_readl(MXC_GPT_GPTOCR1) - __raw_readl(MXC_GPT_GPTCNT);

	/* We need elapsed ticks since last match */
	elapsed = LATCH - ticks_to_match;

	/* Now convert them to usec */
	/* Insure no overflow when calculating the usec below */
	for (i = 1, tick_usec = tick_nsec / 1000;; i *= 2) {
		tick_usec /= i;
		if ((0xFFFFFFFF / tick_usec) > elapsed)
			break;
	}
	usec = (unsigned long)(elapsed * tick_usec) / (LATCH / i);

	return usec;
}

/*!
 * The OS tick timer interrupt structure.
 */
static struct irqaction timer_irq = {
	.name = "MXC Timer Tick",
	.flags = IRQF_DISABLED | IRQF_TIMER,
	.handler = mxc_timer_interrupt
};

/*!
 * This function is used to initialize the GPT to produce an interrupt
 * based on HZ.  It is called by start_kernel() during system startup.
 */
void __init mxc_init_time(void)
{
	u32 reg, v;
	reg = __raw_readl(MXC_GPT_GPTCR);
	reg &= ~GPTCR_ENABLE;
	__raw_writel(reg, MXC_GPT_GPTCR);
	reg |= GPTCR_SWR;
	__raw_writel(reg, MXC_GPT_GPTCR);

	while ((__raw_readl(MXC_GPT_GPTCR) & GPTCR_SWR) != 0)
		cpu_relax();

	reg = GPTCR_FRR | GPTCR_CLKSRC_HIGHFREQ;
	__raw_writel(reg, MXC_GPT_GPTCR);

	/* TODO: get timer rate from clk driver */
	v = 66500000;

	__raw_writel((v / CLOCK_TICK_RATE) - 1, MXC_GPT_GPTPR);

	if ((v % CLOCK_TICK_RATE) != 0) {
		pr_info("\nWARNING: Can't generate CLOCK_TICK_RATE at %d Hz\n",
			CLOCK_TICK_RATE);
	}
	pr_info("Actual CLOCK_TICK_RATE is %d Hz\n",
		v / ((__raw_readl(MXC_GPT_GPTPR) & 0xFFF) + 1));

	reg = __raw_readl(MXC_GPT_GPTCNT);
	reg += LATCH;
	__raw_writel(reg, MXC_GPT_GPTOCR1);

	setup_irq(MXC_INT_GPT, &timer_irq);

	reg = __raw_readl(MXC_GPT_GPTCR);
	reg =
	    GPTCR_FRR | GPTCR_CLKSRC_HIGHFREQ | GPTCR_STOPEN | GPTCR_DOZEN |
	    GPTCR_WAITEN | GPTCR_ENMOD | GPTCR_ENABLE;
	__raw_writel(reg, MXC_GPT_GPTCR);

	__raw_writel(GPTIR_OF1IE, MXC_GPT_GPTIR);
}

struct sys_timer mxc_timer = {
	.init = mxc_init_time,
	.offset = mxc_gettimeoffset,
};
