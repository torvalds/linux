/*
 * Copyright 1999 - 2003 ARM Limited
 * Copyright 2000 Deep Blue Solutions Ltd
 * Copyright 2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/io.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>
#include <asm/hardware/gic.h>
#include <mach/cns3xxx.h>
#include "core.h"

static struct map_desc cns3xxx_io_desc[] __initdata = {
	{
		.virtual	= CNS3XXX_TC11MP_TWD_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_TC11MP_TWD_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_TC11MP_GIC_CPU_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_TC11MP_GIC_CPU_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_TC11MP_GIC_DIST_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_TC11MP_GIC_DIST_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_TIMER1_2_3_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_TIMER1_2_3_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_GPIOA_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_GPIOA_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_GPIOB_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_GPIOB_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_MISC_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_MISC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= CNS3XXX_PM_BASE_VIRT,
		.pfn		= __phys_to_pfn(CNS3XXX_PM_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

void __init cns3xxx_map_io(void)
{
	iotable_init(cns3xxx_io_desc, ARRAY_SIZE(cns3xxx_io_desc));
}

/* used by entry-macro.S */
void __iomem *gic_cpu_base_addr;

void __init cns3xxx_init_irq(void)
{
	gic_cpu_base_addr = __io(CNS3XXX_TC11MP_GIC_CPU_BASE_VIRT);
	gic_dist_init(0, __io(CNS3XXX_TC11MP_GIC_DIST_BASE_VIRT), 29);
	gic_cpu_init(0, gic_cpu_base_addr);
}

void cns3xxx_power_off(void)
{
	u32 __iomem *pm_base = __io(CNS3XXX_PM_BASE_VIRT);
	u32 clkctrl;

	printk(KERN_INFO "powering system down...\n");

	clkctrl = readl(pm_base + PM_SYS_CLK_CTRL_OFFSET);
	clkctrl &= 0xfffff1ff;
	clkctrl |= (0x5 << 9);		/* Hibernate */
	writel(clkctrl, pm_base + PM_SYS_CLK_CTRL_OFFSET);

}

/*
 * Timer
 */
static void __iomem *cns3xxx_tmr1;

static void cns3xxx_timer_set_mode(enum clock_event_mode mode,
				   struct clock_event_device *clk)
{
	unsigned long ctrl = readl(cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	int pclk = cns3xxx_cpu_clock() / 8;
	int reload;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		reload = pclk * 20 / (3 * HZ) * 0x25000;
		writel(reload, cns3xxx_tmr1 + TIMER1_AUTO_RELOAD_OFFSET);
		ctrl |= (1 << 0) | (1 << 2) | (1 << 9);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* period set, and timer enabled in 'next_event' hook */
		ctrl |= (1 << 2) | (1 << 9);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		ctrl = 0;
	}

	writel(ctrl, cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
}

static int cns3xxx_timer_set_next_event(unsigned long evt,
					struct clock_event_device *unused)
{
	unsigned long ctrl = readl(cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);

	writel(evt, cns3xxx_tmr1 + TIMER1_AUTO_RELOAD_OFFSET);
	writel(ctrl | (1 << 0), cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);

	return 0;
}

static struct clock_event_device cns3xxx_tmr1_clockevent = {
	.name		= "cns3xxx timer1",
	.shift		= 8,
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= cns3xxx_timer_set_mode,
	.set_next_event	= cns3xxx_timer_set_next_event,
	.rating		= 350,
	.cpumask	= cpu_all_mask,
};

static void __init cns3xxx_clockevents_init(unsigned int timer_irq)
{
	cns3xxx_tmr1_clockevent.irq = timer_irq;
	cns3xxx_tmr1_clockevent.mult =
		div_sc((cns3xxx_cpu_clock() >> 3) * 1000000, NSEC_PER_SEC,
		       cns3xxx_tmr1_clockevent.shift);
	cns3xxx_tmr1_clockevent.max_delta_ns =
		clockevent_delta2ns(0xffffffff, &cns3xxx_tmr1_clockevent);
	cns3xxx_tmr1_clockevent.min_delta_ns =
		clockevent_delta2ns(0xf, &cns3xxx_tmr1_clockevent);

	clockevents_register_device(&cns3xxx_tmr1_clockevent);
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t cns3xxx_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &cns3xxx_tmr1_clockevent;
	u32 __iomem *stat = cns3xxx_tmr1 + TIMER1_2_INTERRUPT_STATUS_OFFSET;
	u32 val;

	/* Clear the interrupt */
	val = readl(stat);
	writel(val & ~(1 << 2), stat);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction cns3xxx_timer_irq = {
	.name		= "timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= cns3xxx_timer_interrupt,
};

/*
 * Set up the clock source and clock events devices
 */
static void __init __cns3xxx_timer_init(unsigned int timer_irq)
{
	u32 val;
	u32 irq_mask;

	/*
	 * Initialise to a known state (all timers off)
	 */

	/* disable timer1 and timer2 */
	writel(0, cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	/* stop free running timer3 */
	writel(0, cns3xxx_tmr1 + TIMER_FREERUN_CONTROL_OFFSET);

	/* timer1 */
	writel(0x5C800, cns3xxx_tmr1 + TIMER1_COUNTER_OFFSET);
	writel(0x5C800, cns3xxx_tmr1 + TIMER1_AUTO_RELOAD_OFFSET);

	writel(0, cns3xxx_tmr1 + TIMER1_MATCH_V1_OFFSET);
	writel(0, cns3xxx_tmr1 + TIMER1_MATCH_V2_OFFSET);

	/* mask irq, non-mask timer1 overflow */
	irq_mask = readl(cns3xxx_tmr1 + TIMER1_2_INTERRUPT_MASK_OFFSET);
	irq_mask &= ~(1 << 2);
	irq_mask |= 0x03;
	writel(irq_mask, cns3xxx_tmr1 + TIMER1_2_INTERRUPT_MASK_OFFSET);

	/* down counter */
	val = readl(cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	val |= (1 << 9);
	writel(val, cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);

	/* timer2 */
	writel(0, cns3xxx_tmr1 + TIMER2_MATCH_V1_OFFSET);
	writel(0, cns3xxx_tmr1 + TIMER2_MATCH_V2_OFFSET);

	/* mask irq */
	irq_mask = readl(cns3xxx_tmr1 + TIMER1_2_INTERRUPT_MASK_OFFSET);
	irq_mask |= ((1 << 3) | (1 << 4) | (1 << 5));
	writel(irq_mask, cns3xxx_tmr1 + TIMER1_2_INTERRUPT_MASK_OFFSET);

	/* down counter */
	val = readl(cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);
	val |= (1 << 10);
	writel(val, cns3xxx_tmr1 + TIMER1_2_CONTROL_OFFSET);

	/* Make irqs happen for the system timer */
	setup_irq(timer_irq, &cns3xxx_timer_irq);

	cns3xxx_clockevents_init(timer_irq);
}

static void __init cns3xxx_timer_init(void)
{
	cns3xxx_tmr1 = __io(CNS3XXX_TIMER1_2_3_BASE_VIRT);

	__cns3xxx_timer_init(IRQ_CNS3XXX_TIMER0);
}

struct sys_timer cns3xxx_timer = {
	.init = cns3xxx_timer_init,
};
