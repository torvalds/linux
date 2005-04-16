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
#include <linux/sched.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/hardware/amba.h>
#include <asm/arch/cm.h>
#include <asm/system.h>
#include <asm/leds.h>
#include <asm/mach/time.h>

#include "common.h"

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

/*
 * What does it look like?
 */
typedef struct TimerStruct {
	unsigned long TimerLoad;
	unsigned long TimerValue;
	unsigned long TimerControl;
	unsigned long TimerClear;
} TimerStruct_t;

static unsigned long timer_reload;

/*
 * Returns number of ms since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 */
unsigned long integrator_gettimeoffset(void)
{
	volatile TimerStruct_t *timer1 = (TimerStruct_t *)TIMER1_VA_BASE;
	unsigned long ticks1, ticks2, status;

	/*
	 * Get the current number of ticks.  Note that there is a race
	 * condition between us reading the timer and checking for
	 * an interrupt.  We get around this by ensuring that the
	 * counter has not reloaded between our two reads.
	 */
	ticks2 = timer1->TimerValue & 0xffff;
	do {
		ticks1 = ticks2;
		status = __raw_readl(VA_IC_BASE + IRQ_RAW_STATUS);
		ticks2 = timer1->TimerValue & 0xffff;
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
integrator_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	volatile TimerStruct_t *timer1 = (volatile TimerStruct_t *)TIMER1_VA_BASE;

	write_seqlock(&xtime_lock);

	// ...clear the interrupt
	timer1->TimerClear = 1;

	timer_tick(regs);

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction integrator_timer_irq = {
	.name		= "Integrator Timer Tick",
	.flags		= SA_INTERRUPT,
	.handler	= integrator_timer_interrupt
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
void __init integrator_time_init(unsigned long reload, unsigned int ctrl)
{
	volatile TimerStruct_t *timer0 = (volatile TimerStruct_t *)TIMER0_VA_BASE;
	volatile TimerStruct_t *timer1 = (volatile TimerStruct_t *)TIMER1_VA_BASE;
	volatile TimerStruct_t *timer2 = (volatile TimerStruct_t *)TIMER2_VA_BASE;
	unsigned int timer_ctrl = 0x80 | 0x40;	/* periodic */

	timer_reload = reload;
	timer_ctrl |= ctrl;

	if (timer_reload > 0x100000) {
		timer_reload >>= 8;
		timer_ctrl |= 0x08; /* /256 */
	} else if (timer_reload > 0x010000) {
		timer_reload >>= 4;
		timer_ctrl |= 0x04; /* /16 */
	}

	/*
	 * Initialise to a known state (all timers off)
	 */
	timer0->TimerControl = 0;
	timer1->TimerControl = 0;
	timer2->TimerControl = 0;

	timer1->TimerLoad    = timer_reload;
	timer1->TimerValue   = timer_reload;
	timer1->TimerControl = timer_ctrl;

	/* 
	 * Make irqs happen for the system timer
	 */
	setup_irq(IRQ_TIMERINT1, &integrator_timer_irq);
}
