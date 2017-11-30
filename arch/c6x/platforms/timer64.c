/*
 *  Copyright (C) 2010, 2011 Texas Instruments Incorporated
 *  Contributed by: Mark Salter (msalter@redhat.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <asm/soc.h>
#include <asm/dscr.h>
#include <asm/special_insns.h>
#include <asm/timer64.h>

struct timer_regs {
	u32	reserved0;
	u32	emumgt;
	u32	reserved1;
	u32	reserved2;
	u32	cntlo;
	u32	cnthi;
	u32	prdlo;
	u32	prdhi;
	u32	tcr;
	u32	tgcr;
	u32	wdtcr;
};

static struct timer_regs __iomem *timer;

#define TCR_TSTATLO	     0x001
#define TCR_INVOUTPLO	     0x002
#define TCR_INVINPLO	     0x004
#define TCR_CPLO	     0x008
#define TCR_ENAMODELO_ONCE   0x040
#define TCR_ENAMODELO_CONT   0x080
#define TCR_ENAMODELO_MASK   0x0c0
#define TCR_PWIDLO_MASK      0x030
#define TCR_CLKSRCLO	     0x100
#define TCR_TIENLO	     0x200
#define TCR_TSTATHI	     (0x001 << 16)
#define TCR_INVOUTPHI	     (0x002 << 16)
#define TCR_CPHI	     (0x008 << 16)
#define TCR_PWIDHI_MASK      (0x030 << 16)
#define TCR_ENAMODEHI_ONCE   (0x040 << 16)
#define TCR_ENAMODEHI_CONT   (0x080 << 16)
#define TCR_ENAMODEHI_MASK   (0x0c0 << 16)

#define TGCR_TIMLORS	     0x001
#define TGCR_TIMHIRS	     0x002
#define TGCR_TIMMODE_UD32    0x004
#define TGCR_TIMMODE_WDT64   0x008
#define TGCR_TIMMODE_CD32    0x00c
#define TGCR_TIMMODE_MASK    0x00c
#define TGCR_PSCHI_MASK      (0x00f << 8)
#define TGCR_TDDRHI_MASK     (0x00f << 12)

/*
 * Timer clocks are divided down from the CPU clock
 * The divisor is in the EMUMGTCLKSPD register
 */
#define TIMER_DIVISOR \
	((soc_readl(&timer->emumgt) & (0xf << 16)) >> 16)

#define TIMER64_RATE (c6x_core_freq / TIMER_DIVISOR)

#define TIMER64_MODE_DISABLED 0
#define TIMER64_MODE_ONE_SHOT TCR_ENAMODELO_ONCE
#define TIMER64_MODE_PERIODIC TCR_ENAMODELO_CONT

static int timer64_mode;
static int timer64_devstate_id = -1;

static void timer64_config(unsigned long period)
{
	u32 tcr = soc_readl(&timer->tcr) & ~TCR_ENAMODELO_MASK;

	soc_writel(tcr, &timer->tcr);
	soc_writel(period - 1, &timer->prdlo);
	soc_writel(0, &timer->cntlo);
	tcr |= timer64_mode;
	soc_writel(tcr, &timer->tcr);
}

static void timer64_enable(void)
{
	u32 val;

	if (timer64_devstate_id >= 0)
		dscr_set_devstate(timer64_devstate_id, DSCR_DEVSTATE_ENABLED);

	/* disable timer, reset count */
	soc_writel(soc_readl(&timer->tcr) & ~TCR_ENAMODELO_MASK, &timer->tcr);
	soc_writel(0, &timer->prdlo);

	/* use internal clock and 1 cycle pulse width */
	val = soc_readl(&timer->tcr);
	soc_writel(val & ~(TCR_CLKSRCLO | TCR_PWIDLO_MASK), &timer->tcr);

	/* dual 32-bit unchained mode */
	val = soc_readl(&timer->tgcr) & ~TGCR_TIMMODE_MASK;
	soc_writel(val, &timer->tgcr);
	soc_writel(val | (TGCR_TIMLORS | TGCR_TIMMODE_UD32), &timer->tgcr);
}

static void timer64_disable(void)
{
	/* disable timer, reset count */
	soc_writel(soc_readl(&timer->tcr) & ~TCR_ENAMODELO_MASK, &timer->tcr);
	soc_writel(0, &timer->prdlo);

	if (timer64_devstate_id >= 0)
		dscr_set_devstate(timer64_devstate_id, DSCR_DEVSTATE_DISABLED);
}

static int next_event(unsigned long delta,
		      struct clock_event_device *evt)
{
	timer64_config(delta);
	return 0;
}

static int set_periodic(struct clock_event_device *evt)
{
	timer64_enable();
	timer64_mode = TIMER64_MODE_PERIODIC;
	timer64_config(TIMER64_RATE / HZ);
	return 0;
}

static int set_oneshot(struct clock_event_device *evt)
{
	timer64_enable();
	timer64_mode = TIMER64_MODE_ONE_SHOT;
	return 0;
}

static int shutdown(struct clock_event_device *evt)
{
	timer64_mode = TIMER64_MODE_DISABLED;
	timer64_disable();
	return 0;
}

static struct clock_event_device t64_clockevent_device = {
	.name			= "TIMER64_EVT32_TIMER",
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_PERIODIC,
	.rating			= 200,
	.set_state_shutdown	= shutdown,
	.set_state_periodic	= set_periodic,
	.set_state_oneshot	= set_oneshot,
	.set_next_event		= next_event,
};

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *cd = &t64_clockevent_device;

	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static struct irqaction timer_iact = {
	.name		= "timer",
	.flags		= IRQF_TIMER,
	.handler	= timer_interrupt,
	.dev_id		= &t64_clockevent_device,
};

void __init timer64_init(void)
{
	struct clock_event_device *cd = &t64_clockevent_device;
	struct device_node *np, *first = NULL;
	u32 val;
	int err, found = 0;

	for_each_compatible_node(np, NULL, "ti,c64x+timer64") {
		err = of_property_read_u32(np, "ti,core-mask", &val);
		if (!err) {
			if (val & (1 << get_coreid())) {
				found = 1;
				break;
			}
		} else if (!first)
			first = np;
	}
	if (!found) {
		/* try first one with no core-mask */
		if (first)
			np = of_node_get(first);
		else {
			pr_debug("Cannot find ti,c64x+timer64 timer.\n");
			return;
		}
	}

	timer = of_iomap(np, 0);
	if (!timer) {
		pr_debug("%pOF: Cannot map timer registers.\n", np);
		goto out;
	}
	pr_debug("%pOF: Timer registers=%p.\n", np, timer);

	cd->irq	= irq_of_parse_and_map(np, 0);
	if (cd->irq == NO_IRQ) {
		pr_debug("%pOF: Cannot find interrupt.\n", np);
		iounmap(timer);
		goto out;
	}

	/* If there is a device state control, save the ID. */
	err = of_property_read_u32(np, "ti,dscr-dev-enable", &val);
	if (!err) {
		timer64_devstate_id = val;

		/*
		 * It is necessary to enable the timer block here because
		 * the TIMER_DIVISOR macro needs to read a timer register
		 * to get the divisor.
		 */
		dscr_set_devstate(timer64_devstate_id, DSCR_DEVSTATE_ENABLED);
	}

	pr_debug("%pOF: Timer irq=%d.\n", np, cd->irq);

	clockevents_calc_mult_shift(cd, c6x_core_freq / TIMER_DIVISOR, 5);

	cd->max_delta_ns	= clockevent_delta2ns(0x7fffffff, cd);
	cd->max_delta_ticks	= 0x7fffffff;
	cd->min_delta_ns	= clockevent_delta2ns(250, cd);
	cd->min_delta_ticks	= 250;

	cd->cpumask		= cpumask_of(smp_processor_id());

	clockevents_register_device(cd);
	setup_irq(cd->irq, &timer_iact);

out:
	of_node_put(np);
	return;
}
