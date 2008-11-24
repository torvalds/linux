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
#include <linux/io.h>

#include <mach/hardware.h>
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
		.bus_id	= "mb:15",
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
		.bus_id	= "mb:16",
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
		.bus_id	= "mb:17",
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
		.bus_id	= "mb:18",
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
		.bus_id	= "mb:19",
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

static int __init integrator_init(void)
{
	int i;

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
#define VA_IC_BASE     IO_ADDRESS(INTEGRATOR_IC_BASE) 

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

/*
 * Returns number of ms since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 */
unsigned long integrator_gettimeoffset(void)
{
	unsigned long ticks1, ticks2, status;

	/*
	 * Get the current number of ticks.  Note that there is a race
	 * condition between us reading the timer and checking for
	 * an interrupt.  We get around this by ensuring that the
	 * counter has not reloaded between our two reads.
	 */
	ticks2 = readl(TIMER1_VA_BASE + TIMER_VALUE) & 0xffff;
	do {
		ticks1 = ticks2;
		status = __raw_readl(VA_IC_BASE + IRQ_RAW_STATUS);
		ticks2 = readl(TIMER1_VA_BASE + TIMER_VALUE) & 0xffff;
	} while (ticks2 > ticks1);

	/*
	 * Number of ticks since last interrupt.
	 */
	ticks1 = timer_reload - ticks2;

	/*
	 * Interrupt pending?  If so, we've reloaded once already.
	 */
	if (status & (1 << IRQ_TIMERINT1))
		ticks1 += timer_reload;

	/*
	 * Convert the ticks to usecs
	 */
	return TICKS2USECS(ticks1);
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t
integrator_timer_interrupt(int irq, void *dev_id)
{
	/*
	 * clear the interrupt
	 */
	writel(1, TIMER1_VA_BASE + TIMER_INTCLR);

	timer_tick();

	return IRQ_HANDLED;
}

static struct irqaction integrator_timer_irq = {
	.name		= "Integrator Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= integrator_timer_interrupt,
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
void __init integrator_time_init(unsigned long reload, unsigned int ctrl)
{
	unsigned int timer_ctrl = TIMER_CTRL_ENABLE | TIMER_CTRL_PERIODIC;

	timer_reload = reload;
	timer_ctrl |= ctrl;

	if (timer_reload > 0x100000) {
		timer_reload >>= 8;
		timer_ctrl |= TIMER_CTRL_DIV256;
	} else if (timer_reload > 0x010000) {
		timer_reload >>= 4;
		timer_ctrl |= TIMER_CTRL_DIV16;
	}

	/*
	 * Initialise to a known state (all timers off)
	 */
	writel(0, TIMER0_VA_BASE + TIMER_CTRL);
	writel(0, TIMER1_VA_BASE + TIMER_CTRL);
	writel(0, TIMER2_VA_BASE + TIMER_CTRL);

	writel(timer_reload, TIMER1_VA_BASE + TIMER_LOAD);
	writel(timer_reload, TIMER1_VA_BASE + TIMER_VALUE);
	writel(timer_ctrl, TIMER1_VA_BASE + TIMER_CTRL);

	/*
	 * Make irqs happen for the system timer
	 */
	setup_irq(IRQ_TIMERINT1, &integrator_timer_irq);
}
