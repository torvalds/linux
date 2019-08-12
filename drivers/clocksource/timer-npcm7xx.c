// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014-2018 Nuvoton Technologies tomer.maimon@nuvoton.com
 * All rights reserved.
 *
 * Copyright 2017 Google, Inc.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/clockchips.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include "timer-of.h"

/* Timers registers */
#define NPCM7XX_REG_TCSR0	0x0 /* Timer 0 Control and Status Register */
#define NPCM7XX_REG_TICR0	0x8 /* Timer 0 Initial Count Register */
#define NPCM7XX_REG_TCSR1	0x4 /* Timer 1 Control and Status Register */
#define NPCM7XX_REG_TICR1	0xc /* Timer 1 Initial Count Register */
#define NPCM7XX_REG_TDR1	0x14 /* Timer 1 Data Register */
#define NPCM7XX_REG_TISR	0x18 /* Timer Interrupt Status Register */

/* Timers control */
#define NPCM7XX_Tx_RESETINT		0x1f
#define NPCM7XX_Tx_PERIOD		BIT(27)
#define NPCM7XX_Tx_INTEN		BIT(29)
#define NPCM7XX_Tx_COUNTEN		BIT(30)
#define NPCM7XX_Tx_ONESHOT		0x0
#define NPCM7XX_Tx_OPER			GENMASK(27, 3)
#define NPCM7XX_Tx_MIN_PRESCALE		0x1
#define NPCM7XX_Tx_TDR_MASK_BITS	24
#define NPCM7XX_Tx_MAX_CNT		0xFFFFFF
#define NPCM7XX_T0_CLR_INT		0x1
#define NPCM7XX_Tx_CLR_CSR		0x0

/* Timers operating mode */
#define NPCM7XX_START_PERIODIC_Tx (NPCM7XX_Tx_PERIOD | NPCM7XX_Tx_COUNTEN | \
					NPCM7XX_Tx_INTEN | \
					NPCM7XX_Tx_MIN_PRESCALE)

#define NPCM7XX_START_ONESHOT_Tx (NPCM7XX_Tx_ONESHOT | NPCM7XX_Tx_COUNTEN | \
					NPCM7XX_Tx_INTEN | \
					NPCM7XX_Tx_MIN_PRESCALE)

#define NPCM7XX_START_Tx (NPCM7XX_Tx_COUNTEN | NPCM7XX_Tx_PERIOD | \
				NPCM7XX_Tx_MIN_PRESCALE)

#define NPCM7XX_DEFAULT_CSR (NPCM7XX_Tx_CLR_CSR | NPCM7XX_Tx_MIN_PRESCALE)

static int npcm7xx_timer_resume(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);
	u32 val;

	val = readl(timer_of_base(to) + NPCM7XX_REG_TCSR0);
	val |= NPCM7XX_Tx_COUNTEN;
	writel(val, timer_of_base(to) + NPCM7XX_REG_TCSR0);

	return 0;
}

static int npcm7xx_timer_shutdown(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);
	u32 val;

	val = readl(timer_of_base(to) + NPCM7XX_REG_TCSR0);
	val &= ~NPCM7XX_Tx_COUNTEN;
	writel(val, timer_of_base(to) + NPCM7XX_REG_TCSR0);

	return 0;
}

static int npcm7xx_timer_oneshot(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);
	u32 val;

	val = readl(timer_of_base(to) + NPCM7XX_REG_TCSR0);
	val &= ~NPCM7XX_Tx_OPER;

	val = readl(timer_of_base(to) + NPCM7XX_REG_TCSR0);
	val |= NPCM7XX_START_ONESHOT_Tx;
	writel(val, timer_of_base(to) + NPCM7XX_REG_TCSR0);

	return 0;
}

static int npcm7xx_timer_periodic(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);
	u32 val;

	val = readl(timer_of_base(to) + NPCM7XX_REG_TCSR0);
	val &= ~NPCM7XX_Tx_OPER;

	writel(timer_of_period(to), timer_of_base(to) + NPCM7XX_REG_TICR0);
	val |= NPCM7XX_START_PERIODIC_Tx;

	writel(val, timer_of_base(to) + NPCM7XX_REG_TCSR0);

	return 0;
}

static int npcm7xx_clockevent_set_next_event(unsigned long evt,
		struct clock_event_device *clk)
{
	struct timer_of *to = to_timer_of(clk);
	u32 val;

	writel(evt, timer_of_base(to) + NPCM7XX_REG_TICR0);
	val = readl(timer_of_base(to) + NPCM7XX_REG_TCSR0);
	val |= NPCM7XX_START_Tx;
	writel(val, timer_of_base(to) + NPCM7XX_REG_TCSR0);

	return 0;
}

static irqreturn_t npcm7xx_timer0_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;
	struct timer_of *to = to_timer_of(evt);

	writel(NPCM7XX_T0_CLR_INT, timer_of_base(to) + NPCM7XX_REG_TISR);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct timer_of npcm7xx_to = {
	.flags = TIMER_OF_IRQ | TIMER_OF_BASE | TIMER_OF_CLOCK,

	.clkevt = {
		.name		    = "npcm7xx-timer0",
		.features	    = CLOCK_EVT_FEAT_PERIODIC |
				      CLOCK_EVT_FEAT_ONESHOT,
		.set_next_event	    = npcm7xx_clockevent_set_next_event,
		.set_state_shutdown = npcm7xx_timer_shutdown,
		.set_state_periodic = npcm7xx_timer_periodic,
		.set_state_oneshot  = npcm7xx_timer_oneshot,
		.tick_resume	    = npcm7xx_timer_resume,
		.rating		    = 300,
	},

	.of_irq = {
		.handler = npcm7xx_timer0_interrupt,
		.flags = IRQF_TIMER | IRQF_IRQPOLL,
	},
};

static void __init npcm7xx_clockevents_init(void)
{
	writel(NPCM7XX_DEFAULT_CSR,
		timer_of_base(&npcm7xx_to) + NPCM7XX_REG_TCSR0);

	writel(NPCM7XX_Tx_RESETINT,
		timer_of_base(&npcm7xx_to) + NPCM7XX_REG_TISR);

	npcm7xx_to.clkevt.cpumask = cpumask_of(0);
	clockevents_config_and_register(&npcm7xx_to.clkevt,
					timer_of_rate(&npcm7xx_to),
					0x1, NPCM7XX_Tx_MAX_CNT);
}

static void __init npcm7xx_clocksource_init(void)
{
	u32 val;

	writel(NPCM7XX_DEFAULT_CSR,
		timer_of_base(&npcm7xx_to) + NPCM7XX_REG_TCSR1);
	writel(NPCM7XX_Tx_MAX_CNT,
		timer_of_base(&npcm7xx_to) + NPCM7XX_REG_TICR1);

	val = readl(timer_of_base(&npcm7xx_to) + NPCM7XX_REG_TCSR1);
	val |= NPCM7XX_START_Tx;
	writel(val, timer_of_base(&npcm7xx_to) + NPCM7XX_REG_TCSR1);

	clocksource_mmio_init(timer_of_base(&npcm7xx_to) +
				NPCM7XX_REG_TDR1,
				"npcm7xx-timer1", timer_of_rate(&npcm7xx_to),
				200, (unsigned int)NPCM7XX_Tx_TDR_MASK_BITS,
				clocksource_mmio_readl_down);
}

static int __init npcm7xx_timer_init(struct device_node *np)
{
	int ret;

	ret = timer_of_init(np, &npcm7xx_to);
	if (ret)
		return ret;

	/* Clock input is divided by PRESCALE + 1 before it is fed */
	/* to the counter */
	npcm7xx_to.of_clk.rate = npcm7xx_to.of_clk.rate /
		(NPCM7XX_Tx_MIN_PRESCALE + 1);

	npcm7xx_clocksource_init();
	npcm7xx_clockevents_init();

	pr_info("Enabling NPCM7xx clocksource timer base: %px, IRQ: %d ",
		timer_of_base(&npcm7xx_to), timer_of_irq(&npcm7xx_to));

	return 0;
}

TIMER_OF_DECLARE(npcm7xx, "nuvoton,npcm750-timer", npcm7xx_timer_init);

