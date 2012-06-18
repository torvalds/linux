/*
 * arch/arm/mach-sun3i/core.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/amba/pl061.h>
#include <linux/amba/mmci.h>
#include <linux/amba/pl022.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/clockchips.h>

#include <asm/clkdev.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/icst.h>
#include <asm/hardware/vic.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/clkdev.h>
#include <mach/hardware.h>
#include <mach/platform.h>

#include "core.h"


/**
 * Global vars definitions
 *
 */
static void timer_set_mode(enum clock_event_mode mode, struct clock_event_device *clk)
{
	volatile u32 ctrl;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		printk("timer_set_mode: periodic\n");
		writel(20, SW_TIMER0_INTVAL_REG); /* interval (999+1) */
		ctrl = readl(SW_TIMER0_CTL_REG);
		ctrl &= ~(1<<2);  //periodic mode
                ctrl |= 1;  //enable
                break;

	case CLOCK_EVT_MODE_ONESHOT:
		printk("timer_set_mode: oneshot\n");
		ctrl = readl(SW_TIMER0_CTL_REG);
		ctrl |= (1<<2);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		ctrl = readl(SW_TIMER0_CTL_REG);
		ctrl &= ~(1<<0);
		break;
	}

	writel(ctrl, SW_TIMER0_CTL_REG);
}

static int timer_set_next_event(unsigned long evt, struct clock_event_device *unused)
{
	volatile u32 ctrl;

	/* clear any pending before continue */
	ctrl = readl(SW_TIMER0_CTL_REG);
	writel(evt, SW_TIMER0_CNTVAL_REG);
	writel(ctrl | 0x1, SW_TIMER_INT_STA_REG);

	return 0;
}

static struct clock_event_device timer0_clockevent = {
	.name = "timer0",
	.shift = 32,
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = timer_set_mode,
	.set_next_event = timer_set_next_event,
};


static irqreturn_t softwinner_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &timer0_clockevent;

	writel(0x1, SW_TIMER_INT_STA_REG);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction softwinner_timer_irq = {
	.name = "Softwinner Timer Tick",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = softwinner_timer_interrupt,
};

void softwinner_irq_ack(struct irq_data *d)
{
	unsigned int irq = d->irq;
	volatile u32 val;

	if (irq < 32) {
		val = readl(SW_INT_ENABLE_REG0) & (~(1<<irq));
		writel(val, SW_INT_ENABLE_REG0);
		val = readl(SW_INT_MASK_REG0) | (1<<irq);
		writel(val, SW_INT_MASK_REG0);
	} else {
		irq = irq - 32;
		val = readl(SW_INT_ENABLE_REG1) & (~(1<<irq));
		writel(val, SW_INT_ENABLE_REG1);
		val = readl(SW_INT_MASK_REG1) | (1<<irq);
		writel(val, SW_INT_MASK_REG1);
	}
}

/* Disable irq */
static void softwinner_irq_mask(struct irq_data *d)
{
	unsigned int irq = d->irq;
	volatile u32 val;

	if (irq < 32) {
		val = readl(SW_INT_ENABLE_REG0) & (~(1<<irq));
		writel(val, SW_INT_ENABLE_REG0);
		val = readl(SW_INT_MASK_REG0) | (1<<irq);
		writel(val, SW_INT_MASK_REG0);
	} else {
		irq = irq - 32;
		val = readl(SW_INT_ENABLE_REG1) & (~(1<<irq));
		writel(val, SW_INT_ENABLE_REG1);
		val = readl(SW_INT_MASK_REG1) | (1<<irq);
		writel(val, SW_INT_MASK_REG1);
	}
}

/* Enable irq */
static void softwinner_irq_unmask(struct irq_data *d)
{
	unsigned int irq = d->irq;
	volatile u32 val;

	if (irq < 32) {
		val = readl(SW_INT_ENABLE_REG0) | (1<<irq);
		writel(val, SW_INT_ENABLE_REG0);
		val = readl(SW_INT_MASK_REG0) & (~(1<<irq));
		writel(val, SW_INT_MASK_REG0);
	} else {
		irq = irq - 32;
		val = readl(SW_INT_ENABLE_REG1) | (1<<irq);
		writel(val, SW_INT_ENABLE_REG1);
		val = readl(SW_INT_MASK_REG1) & (~(1<<irq));
		writel(val, SW_INT_MASK_REG1);
	}
}

static struct irq_chip sw_f20_sic_chip = {
	.name	= "SW_F20_SIC",
	.irq_ack = softwinner_irq_ack,
	.irq_mask = softwinner_irq_mask,
	.irq_unmask = softwinner_irq_unmask,
};

void __init softwinner_init_irq(void)
{
	u32 i = 0;

	/* Disable & clear all interrupts */
	writel(0, SW_INT_ENABLE_REG0);
	writel(0, SW_INT_ENABLE_REG1);
	writel(0xffffffff, SW_INT_MASK_REG0);
	writel(0xffffffff, SW_INT_MASK_REG1);
	writel(0xffffffff, SW_INT_PENDING_REG0);
	writel(0xffffffff, SW_INT_PENDING_REG1);

	for (i = SW_INT_START; i < SW_INT_END; i++) {
		irq_set_chip_and_handler(i, &sw_f20_sic_chip, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}
}


static struct map_desc softwinner_io_desc[] __initdata = {
	{ SW_VA_SRAM_BASE,	__phys_to_pfn(SW_PA_SRAM_BASE),	SZ_16K,		MT_MEMORY_ITCM},
	{ SW_VA_IO_BASE, __phys_to_pfn(SW_PA_IO_BASE), SZ_4M, MT_DEVICE},
};

void __init softwinner_map_io(void)
{
	iotable_init(softwinner_io_desc, ARRAY_SIZE(softwinner_io_desc));
}

struct sysdev_class sw_sysclass = {
	.name = "sw-core",
};

static struct sys_device sw_sysdev = {
	.cls = &sw_sysclass,
};

static int __init sw_core_init(void)
{
        return sysdev_class_register(&sw_sysclass);
}
core_initcall(sw_core_init);


extern int sw_register_clocks(void);
void __init softwinner_init(void)
{
	sysdev_register(&sw_sysdev);
}

static void __init softwinner_timer_init(void)
{
        volatile u32  val = 0;

        writel(20, SW_TIMER0_INTVAL_REG);
        val = readl(SW_TIMER0_CTL_REG);
        val &= 0xfffffff2;
        writel(val, SW_TIMER0_CTL_REG);
        val = readl(SW_TIMER0_CTL_REG); /* 2KHz */
        val |= (15 << 8);
        writel(val, SW_TIMER0_CTL_REG);
        val = readl(SW_TIMER0_CTL_REG);
        val |= 2;
        writel(val, SW_TIMER0_CTL_REG);

        setup_irq(SW_INT_IRQNO_TIMER0, &softwinner_timer_irq);

	/* Enable time0 */
        writel(0x1, SW_TIMER_INT_CTL_REG);

        timer0_clockevent.mult = div_sc(2048, NSEC_PER_SEC, timer0_clockevent.shift);
        timer0_clockevent.max_delta_ns = clockevent_delta2ns(0xff, &timer0_clockevent);
        timer0_clockevent.min_delta_ns = clockevent_delta2ns(0x1, &timer0_clockevent);

        timer0_clockevent.cpumask = cpumask_of(0);
        clockevents_register_device(&timer0_clockevent);
}

struct sys_timer softwinner_timer = {
	.init		= softwinner_timer_init,
};

MACHINE_START(SUN3I, "sun3i")
        .map_io         = softwinner_map_io,
        .init_irq       = softwinner_init_irq,
        .timer          = &softwinner_timer,
        .init_machine   = softwinner_init,
        .boot_params = (unsigned long)(0x80000000),
MACHINE_END

extern void _eLIBs_CleanFlushDCacheRegion(void *addr, __u32 len);
EXPORT_SYMBOL(_eLIBs_CleanFlushDCacheRegion);
