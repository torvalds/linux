/*
 *  linux/arch/arm/mach-integrator/core.c
 *
 *  Copyright (C) 2000-2003 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/termios.h>
#include <linux/amba/bus.h>
#include <linux/amba/serial.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/io.h>

#include <asm/clkdev.h>
#include <mach/clkdev.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include <asm/irq.h>
#include <asm/hardware/arm_timer.h>
#include <mach/cm.h>
#include <asm/system.h>
#include <asm/leds.h>
#include <asm/mach/time.h>

#include "common.h"

static struct amba_pl010_data integrator_uart_data;

static struct amba_device rtc_device = {
	.dev		= {
		.init_name = "mb:15",
	},
	.res		= {
		.start	= INTEGRATOR_RTC_BASE,
		.end	= INTEGRATOR_RTC_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_RTCINT, NO_IRQ },
	.periphid	= 0x00041030,
};

static struct amba_device uart0_device = {
	.dev		= {
		.init_name = "mb:16",
		.platform_data = &integrator_uart_data,
	},
	.res		= {
		.start	= INTEGRATOR_UART0_BASE,
		.end	= INTEGRATOR_UART0_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_UARTINT0, NO_IRQ },
	.periphid	= 0x0041010,
};

static struct amba_device uart1_device = {
	.dev		= {
		.init_name = "mb:17",
		.platform_data = &integrator_uart_data,
	},
	.res		= {
		.start	= INTEGRATOR_UART1_BASE,
		.end	= INTEGRATOR_UART1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_UARTINT1, NO_IRQ },
	.periphid	= 0x0041010,
};

static struct amba_device kmi0_device = {
	.dev		= {
		.init_name = "mb:18",
	},
	.res		= {
		.start	= KMI0_BASE,
		.end	= KMI0_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_KMIINT0, NO_IRQ },
	.periphid	= 0x00041050,
};

static struct amba_device kmi1_device = {
	.dev		= {
		.init_name = "mb:19",
	},
	.res		= {
		.start	= KMI1_BASE,
		.end	= KMI1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_KMIINT1, NO_IRQ },
	.periphid	= 0x00041050,
};

static struct amba_device *amba_devs[] __initdata = {
	&rtc_device,
	&uart0_device,
	&uart1_device,
	&kmi0_device,
	&kmi1_device,
};

/*
 * These are fixed clocks.
 */
static struct clk clk24mhz = {
	.rate	= 24000000,
};

static struct clk uartclk = {
	.rate	= 14745600,
};

static struct clk_lookup lookups[] = {
	{	/* UART0 */
		.dev_id		= "mb:16",
		.clk		= &uartclk,
	}, {	/* UART1 */
		.dev_id		= "mb:17",
		.clk		= &uartclk,
	}, {	/* KMI0 */
		.dev_id		= "mb:18",
		.clk		= &clk24mhz,
	}, {	/* KMI1 */
		.dev_id		= "mb:19",
		.clk		= &clk24mhz,
	}, {	/* MMCI - IntegratorCP */
		.dev_id		= "mb:1c",
		.clk		= &uartclk,
	}
};

static int __init integrator_init(void)
{
	int i;

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

	return 0;
}

arch_initcall(integrator_init);

/*
 * On the Integrator platform, the port RTS and DTR are provided by
 * bits in the following SC_CTRLS register bits:
 *        RTS  DTR
 *  UART0  7    6
 *  UART1  5    4
 */
#define SC_CTRLC	(IO_ADDRESS(INTEGRATOR_SC_BASE) + INTEGRATOR_SC_CTRLC_OFFSET)
#define SC_CTRLS	(IO_ADDRESS(INTEGRATOR_SC_BASE) + INTEGRATOR_SC_CTRLS_OFFSET)

static void integrator_uart_set_mctrl(struct amba_device *dev, void __iomem *base, unsigned int mctrl)
{
	unsigned int ctrls = 0, ctrlc = 0, rts_mask, dtr_mask;

	if (dev == &uart0_device) {
		rts_mask = 1 << 4;
		dtr_mask = 1 << 5;
	} else {
		rts_mask = 1 << 6;
		dtr_mask = 1 << 7;
	}

	if (mctrl & TIOCM_RTS)
		ctrlc |= rts_mask;
	else
		ctrls |= rts_mask;

	if (mctrl & TIOCM_DTR)
		ctrlc |= dtr_mask;
	else
		ctrls |= dtr_mask;

	__raw_writel(ctrls, SC_CTRLS);
	__raw_writel(ctrlc, SC_CTRLC);
}

static struct amba_pl010_data integrator_uart_data = {
	.set_mctrl = integrator_uart_set_mctrl,
};

#define CM_CTRL	IO_ADDRESS(INTEGRATOR_HDR_BASE) + INTEGRATOR_HDR_CTRL_OFFSET

static DEFINE_SPINLOCK(cm_lock);

/**
 * cm_control - update the CM_CTRL register.
 * @mask: bits to change
 * @set: bits to set
 */
void cm_control(u32 mask, u32 set)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&cm_lock, flags);
	val = readl(CM_CTRL) & ~mask;
	writel(val | set, CM_CTRL);
	spin_unlock_irqrestore(&cm_lock, flags);
}

EXPORT_SYMBOL(cm_control);

/*
 * Where is the timer (VA)?
 */
#define TIMER0_VA_BASE (IO_ADDRESS(INTEGRATOR_CT_BASE)+0x00000000)
#define TIMER1_VA_BASE (IO_ADDRESS(INTEGRATOR_CT_BASE)+0x00000100)
#define TIMER2_VA_BASE (IO_ADDRESS(INTEGRATOR_CT_BASE)+0x00000200)

/*
 * How long is the timer interval?
 */
#define TIMER_INTERVAL	(TICKS_PER_uSEC * mSEC_10)
#if TIMER_INTERVAL >= 0x100000
#define TICKS2USECS(x)	(256 * (x) / TICKS_PER_uSEC)
#elif TIMER_INTERVAL >= 0x10000
#define TICKS2USECS(x)	(16 * (x) / TICKS_PER_uSEC)
#else
#define TICKS2USECS(x)	((x) / TICKS_PER_uSEC)
#endif

static unsigned long timer_reload;

static void __iomem * const clksrc_base = (void __iomem *)TIMER2_VA_BASE;

static cycle_t timersp_read(struct clocksource *cs)
{
	return ~(readl(clksrc_base + TIMER_VALUE) & 0xffff);
}

static struct clocksource clocksource_timersp = {
	.name		= "timer2",
	.rating		= 200,
	.read		= timersp_read,
	.mask		= CLOCKSOURCE_MASK(16),
	.shift		= 16,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void integrator_clocksource_init(u32 khz)
{
	struct clocksource *cs = &clocksource_timersp;
	void __iomem *base = clksrc_base;
	u32 ctrl = TIMER_CTRL_ENABLE;

	if (khz >= 1500) {
		khz /= 16;
		ctrl = TIMER_CTRL_DIV16;
	}

	writel(ctrl, base + TIMER_CTRL);
	writel(0xffff, base + TIMER_LOAD);

	cs->mult = clocksource_khz2mult(khz, cs->shift);
	clocksource_register(cs);
}

static void __iomem * const clkevt_base = (void __iomem *)TIMER1_VA_BASE;

/*
 * IRQ handler for the timer
 */
static irqreturn_t integrator_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	/* clear the interrupt */
	writel(1, clkevt_base + TIMER_INTCLR);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static void clkevt_set_mode(enum clock_event_mode mode, struct clock_event_device *evt)
{
	u32 ctrl = readl(clkevt_base + TIMER_CTRL) & ~TIMER_CTRL_ENABLE;

	BUG_ON(mode == CLOCK_EVT_MODE_ONESHOT);

	if (mode == CLOCK_EVT_MODE_PERIODIC) {
		writel(ctrl, clkevt_base + TIMER_CTRL);
		writel(timer_reload, clkevt_base + TIMER_LOAD);
		ctrl |= TIMER_CTRL_PERIODIC | TIMER_CTRL_ENABLE;
	}

	writel(ctrl, clkevt_base + TIMER_CTRL);
}

static int clkevt_set_next_event(unsigned long next, struct clock_event_device *evt)
{
	unsigned long ctrl = readl(clkevt_base + TIMER_CTRL);

	writel(ctrl & ~TIMER_CTRL_ENABLE, clkevt_base + TIMER_CTRL);
	writel(next, clkevt_base + TIMER_LOAD);
	writel(ctrl | TIMER_CTRL_ENABLE, clkevt_base + TIMER_CTRL);

	return 0;
}

static struct clock_event_device integrator_clockevent = {
	.name		= "timer1",
	.shift		= 34,
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.set_mode	= clkevt_set_mode,
	.set_next_event	= clkevt_set_next_event,
	.rating		= 300,
	.cpumask	= cpu_all_mask,
};

static struct irqaction integrator_timer_irq = {
	.name		= "timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= integrator_timer_interrupt,
	.dev_id		= &integrator_clockevent,
};

static void integrator_clockevent_init(u32 khz, unsigned int ctrl)
{
	struct clock_event_device *evt = &integrator_clockevent;

	if (khz * 1000 > 0x100000 * HZ) {
		khz /= 256;
		ctrl |= TIMER_CTRL_DIV256;
	} else if (khz * 1000 > 0x10000 * HZ) {
		khz /= 16;
		ctrl |= TIMER_CTRL_DIV16;
	}

	timer_reload = khz * 1000 / HZ;
	writel(ctrl, clkevt_base + TIMER_CTRL);

	evt->irq = IRQ_TIMERINT1;
	evt->mult = div_sc(khz, NSEC_PER_MSEC, evt->shift);
	evt->max_delta_ns = clockevent_delta2ns(0xffff, evt);
	evt->min_delta_ns = clockevent_delta2ns(0xf, evt);

	setup_irq(IRQ_TIMERINT1, &integrator_timer_irq);
	clockevents_register_device(evt);
}

/*
 * Set up timer(s).
 */
void __init integrator_time_init(u32 khz, unsigned int ctrl)
{
	writel(0, TIMER0_VA_BASE + TIMER_CTRL);
	writel(0, TIMER1_VA_BASE + TIMER_CTRL);
	writel(0, TIMER2_VA_BASE + TIMER_CTRL);

	integrator_clocksource_init(khz);
	integrator_clockevent_init(khz, ctrl);
}
