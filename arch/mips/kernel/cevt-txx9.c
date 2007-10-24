/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Based on linux/arch/mips/kernel/cevt-r4k.c,
 *          linux/arch/mips/jmr3927/rbhma3100/setup.c
 *
 * Copyright 2001 MontaVista Software Inc.
 * Copyright (C) 2000-2001 Toshiba Corporation
 * Copyright (C) 2007 MIPS Technologies, Inc.
 * Copyright (C) 2007 Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/time.h>
#include <asm/txx9tmr.h>

#define TCR_BASE (TXx9_TMTCR_CCDE | TXx9_TMTCR_CRE | TXx9_TMTCR_TMODE_ITVL)
#define TIMER_CCD	0	/* 1/2 */
#define TIMER_CLK(imclk)	((imclk) / (2 << TIMER_CCD))

static struct txx9_tmr_reg __iomem *txx9_cs_tmrptr;

static cycle_t txx9_cs_read(void)
{
	return __raw_readl(&txx9_cs_tmrptr->trr);
}

/* Use 1 bit smaller width to use full bits in that width */
#define TXX9_CLOCKSOURCE_BITS (TXX9_TIMER_BITS - 1)

static struct clocksource txx9_clocksource = {
	.name		= "TXx9",
	.rating		= 200,
	.read		= txx9_cs_read,
	.mask		= CLOCKSOURCE_MASK(TXX9_CLOCKSOURCE_BITS),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

void __init txx9_clocksource_init(unsigned long baseaddr,
				  unsigned int imbusclk)
{
	struct txx9_tmr_reg __iomem *tmrptr;

	clocksource_set_clock(&txx9_clocksource, TIMER_CLK(imbusclk));
	clocksource_register(&txx9_clocksource);

	tmrptr = ioremap(baseaddr, sizeof(struct txx9_tmr_reg));
	__raw_writel(TCR_BASE, &tmrptr->tcr);
	__raw_writel(0, &tmrptr->tisr);
	__raw_writel(TIMER_CCD, &tmrptr->ccdr);
	__raw_writel(TXx9_TMITMR_TZCE, &tmrptr->itmr);
	__raw_writel(1 << TXX9_CLOCKSOURCE_BITS, &tmrptr->cpra);
	__raw_writel(TCR_BASE | TXx9_TMTCR_TCE, &tmrptr->tcr);
	txx9_cs_tmrptr = tmrptr;
}

static struct txx9_tmr_reg __iomem *txx9_tmrptr;

static void txx9tmr_stop_and_clear(struct txx9_tmr_reg __iomem *tmrptr)
{
	/* stop and reset counter */
	__raw_writel(TCR_BASE, &tmrptr->tcr);
	/* clear pending interrupt */
	__raw_writel(0, &tmrptr->tisr);
}

static void txx9tmr_set_mode(enum clock_event_mode mode,
			     struct clock_event_device *evt)
{
	struct txx9_tmr_reg __iomem *tmrptr = txx9_tmrptr;

	txx9tmr_stop_and_clear(tmrptr);
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		__raw_writel(TXx9_TMITMR_TIIE | TXx9_TMITMR_TZCE,
			     &tmrptr->itmr);
		/* start timer */
		__raw_writel(((u64)(NSEC_PER_SEC / HZ) * evt->mult) >>
			     evt->shift,
			     &tmrptr->cpra);
		__raw_writel(TCR_BASE | TXx9_TMTCR_TCE, &tmrptr->tcr);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		__raw_writel(0, &tmrptr->itmr);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		__raw_writel(TXx9_TMITMR_TIIE, &tmrptr->itmr);
		break;
	case CLOCK_EVT_MODE_RESUME:
		__raw_writel(TIMER_CCD, &tmrptr->ccdr);
		__raw_writel(0, &tmrptr->itmr);
		break;
	}
}

static int txx9tmr_set_next_event(unsigned long delta,
				  struct clock_event_device *evt)
{
	struct txx9_tmr_reg __iomem *tmrptr = txx9_tmrptr;

	txx9tmr_stop_and_clear(tmrptr);
	/* start timer */
	__raw_writel(delta, &tmrptr->cpra);
	__raw_writel(TCR_BASE | TXx9_TMTCR_TCE, &tmrptr->tcr);
	return 0;
}

static struct clock_event_device txx9tmr_clock_event_device = {
	.name		= "TXx9",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 200,
	.cpumask	= CPU_MASK_CPU0,
	.set_mode	= txx9tmr_set_mode,
	.set_next_event	= txx9tmr_set_next_event,
};

static irqreturn_t txx9tmr_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *cd = &txx9tmr_clock_event_device;
	struct txx9_tmr_reg __iomem *tmrptr = txx9_tmrptr;

	__raw_writel(0, &tmrptr->tisr);	/* ack interrupt */
	cd->event_handler(cd);
	return IRQ_HANDLED;
}

static struct irqaction txx9tmr_irq = {
	.handler	= txx9tmr_interrupt,
	.flags		= IRQF_DISABLED | IRQF_PERCPU,
	.name		= "txx9tmr",
};

void __init txx9_clockevent_init(unsigned long baseaddr, int irq,
				 unsigned int imbusclk)
{
	struct clock_event_device *cd = &txx9tmr_clock_event_device;
	struct txx9_tmr_reg __iomem *tmrptr;

	tmrptr = ioremap(baseaddr, sizeof(struct txx9_tmr_reg));
	txx9tmr_stop_and_clear(tmrptr);
	__raw_writel(TIMER_CCD, &tmrptr->ccdr);
	__raw_writel(0, &tmrptr->itmr);
	txx9_tmrptr = tmrptr;

	clockevent_set_clock(cd, TIMER_CLK(imbusclk));
	cd->max_delta_ns =
		clockevent_delta2ns(0xffffffff >> (32 - TXX9_TIMER_BITS), cd);
	cd->min_delta_ns = clockevent_delta2ns(0xf, cd);
	cd->irq = irq;
	clockevents_register_device(cd);
	setup_irq(irq, &txx9tmr_irq);
	printk(KERN_INFO "TXx9: clockevent device at 0x%lx, irq %d\n",
	       baseaddr, irq);
}

void __init txx9_tmr_init(unsigned long baseaddr)
{
	struct txx9_tmr_reg __iomem *tmrptr;

	tmrptr = ioremap(baseaddr, sizeof(struct txx9_tmr_reg));
	__raw_writel(TXx9_TMTCR_CRE, &tmrptr->tcr);
	__raw_writel(0, &tmrptr->tisr);
	__raw_writel(0xffffffff, &tmrptr->cpra);
	__raw_writel(0, &tmrptr->itmr);
	__raw_writel(0, &tmrptr->ccdr);
	__raw_writel(0, &tmrptr->pgmr);
	iounmap(tmrptr);
}
