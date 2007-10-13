/*
 * Copyright 2001, 2002, 2003 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 *
 * Common time service routines for MIPS machines. See
 * Documents/MIPS/README.txt.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/time.h>
#include <asm/hardirq.h>
#include <asm/div64.h>
#include <asm/debug.h>

#include <int.h>
#include <cm.h>

static unsigned long cpj;

static cycle_t hpt_read(void)
{
	return read_c0_count2();
}

static void timer_ack(void)
{
	write_c0_compare(cpj);
}

/*
 * plat_time_init() - it does the following things:
 *
 * 1) plat_time_init() -
 * 	a) (optional) set up RTC routines,
 *      b) (optional) calibrate and set the mips_hpt_frequency
 *	    (only needed if you intended to use cpu counter as timer interrupt
 *	     source)
 */

__init void plat_time_init(void)
{
	unsigned int             n;
	unsigned int             m;
	unsigned int             p;
	unsigned int             pow2p;

        /* PLL0 sets MIPS clock (PLL1 <=> TM1, PLL6 <=> TM2, PLL5 <=> mem) */
        /* (but only if CLK_MIPS_CTL select value [bits 3:1] is 1:  FIXME) */

        n = (PNX8550_CM_PLL0_CTL & PNX8550_CM_PLL_N_MASK) >> 16;
        m = (PNX8550_CM_PLL0_CTL & PNX8550_CM_PLL_M_MASK) >> 8;
        p = (PNX8550_CM_PLL0_CTL & PNX8550_CM_PLL_P_MASK) >> 2;
	pow2p = (1 << p);

	db_assert(m != 0 && pow2p != 0);

        /*
	 * Compute the frequency as in the PNX8550 User Manual 1.0, p.186
	 * (a.k.a. 8-10).  Divide by HZ for a timer offset that results in
	 * HZ timer interrupts per second.
	 */
	mips_hpt_frequency = 27UL * ((1000000UL * n)/(m * pow2p));
	cpj = (mips_hpt_frequency + HZ / 2) / HZ;
	write_c0_count(0);
	timer_ack();

	/* Setup Timer 2 */
	write_c0_count2(0);
	write_c0_compare2(0xffffffff);

	clocksource_mips.read = hpt_read;
	mips_timer_ack = timer_ack;
}

static irqreturn_t monotonic_interrupt(int irq, void *dev_id)
{
	/* Timer 2 clear interrupt */
	write_c0_compare2(-1);
	return IRQ_HANDLED;
}

static struct irqaction monotonic_irqaction = {
	.handler = monotonic_interrupt,
	.flags = IRQF_DISABLED,
	.name = "Monotonic timer",
};

void __init plat_timer_setup(struct irqaction *irq)
{
	int configPR;

	setup_irq(PNX8550_INT_TIMER1, irq);
	setup_irq(PNX8550_INT_TIMER2, &monotonic_irqaction);

	/* Timer 1 start */
	configPR = read_c0_config7();
	configPR &= ~0x00000008;
	write_c0_config7(configPR);

	/* Timer 2 start */
	configPR = read_c0_config7();
	configPR &= ~0x00000010;
	write_c0_config7(configPR);

	/* Timer 3 stop */
	configPR = read_c0_config7();
	configPR |= 0x00000020;
	write_c0_config7(configPR);
}
