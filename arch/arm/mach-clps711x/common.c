/*
 *  linux/arch/arm/mach-clps711x/core.c
 *
 *  Core support for the CLPS711x-based machines.
 *
 *  Copyright (C) 2001,2011 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/io.h>
#include <linux/init.h>
#include <linux/sizes.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/clk-provider.h>
#include <linux/sched_clock.h>

#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/system_misc.h>

#include <mach/hardware.h>

#include "common.h"

static struct clk *clk_pll, *clk_bus, *clk_uart, *clk_timerl, *clk_timerh,
		  *clk_tint, *clk_spi;

/*
 * This maps the generic CLPS711x registers
 */
static struct map_desc clps711x_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)CLPS711X_VIRT_BASE,
		.pfn		= __phys_to_pfn(CLPS711X_PHYS_BASE),
		.length		= SZ_64K,
		.type		= MT_DEVICE
	}
};

void __init clps711x_map_io(void)
{
	iotable_init(clps711x_io_desc, ARRAY_SIZE(clps711x_io_desc));
}

void __init clps711x_init_irq(void)
{
	clps711x_intc_init(CLPS711X_PHYS_BASE, SZ_16K);
}

static u64 notrace clps711x_sched_clock_read(void)
{
	return ~readw_relaxed(CLPS711X_VIRT_BASE + TC1D);
}

static void clps711x_clockevent_set_mode(enum clock_event_mode mode,
					 struct clock_event_device *evt)
{
	disable_irq(IRQ_TC2OI);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		enable_irq(IRQ_TC2OI);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* Not supported */
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_RESUME:
		/* Left event sources disabled, no more interrupts appear */
		break;
	}
}

static struct clock_event_device clockevent_clps711x = {
	.name		= "clps711x-clockevent",
	.rating		= 300,
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.set_mode	= clps711x_clockevent_set_mode,
};

static irqreturn_t clps711x_timer_interrupt(int irq, void *dev_id)
{
	clockevent_clps711x.event_handler(&clockevent_clps711x);

	return IRQ_HANDLED;
}

static struct irqaction clps711x_timer_irq = {
	.name		= "clps711x-timer",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= clps711x_timer_interrupt,
};

static void add_fixed_clk(struct clk *clk, const char *name, int rate)
{
	clk = clk_register_fixed_rate(NULL, name, NULL, CLK_IS_ROOT, rate);
	clk_register_clkdev(clk, name, NULL);
}

void __init clps711x_timer_init(void)
{
	int osc, ext, pll, cpu, bus, timl, timh, uart, spi;
	u32 tmp;

	osc = 3686400;
	ext = 13000000;

	tmp = clps_readl(PLLR) >> 24;
	if (tmp)
		pll = (osc * tmp) / 2;
	else
		pll = 73728000; /* Default value */

	tmp = clps_readl(SYSFLG2);
	if (tmp & SYSFLG2_CKMODE) {
		cpu = ext;
		bus = cpu;
		spi = 135400;
		pll = 0;
	} else {
		cpu = pll;
		if (cpu >= 36864000)
			bus = cpu / 2;
		else
			bus = 36864000 / 2;
		spi = cpu / 576;
	}

	uart = bus / 10;

	if (tmp & SYSFLG2_CKMODE) {
		tmp = clps_readl(SYSCON2);
		if (tmp & SYSCON2_OSTB)
			timh = ext / 26;
		else
			timh = 541440;
	} else
		timh = DIV_ROUND_CLOSEST(cpu, 144);

	timl = DIV_ROUND_CLOSEST(timh, 256);

	/* All clocks are fixed */
	add_fixed_clk(clk_pll, "pll", pll);
	add_fixed_clk(clk_bus, "bus", bus);
	add_fixed_clk(clk_uart, "uart", uart);
	add_fixed_clk(clk_timerl, "timer_lf", timl);
	add_fixed_clk(clk_timerh, "timer_hf", timh);
	add_fixed_clk(clk_tint, "tint", 64);
	add_fixed_clk(clk_spi, "spi", spi);

	pr_info("CPU frequency set at %i Hz.\n", cpu);

	/* Start Timer1 in free running mode (Low frequency) */
	tmp = clps_readl(SYSCON1) & ~(SYSCON1_TC1S | SYSCON1_TC1M);
	clps_writel(tmp, SYSCON1);

	sched_clock_register(clps711x_sched_clock_read, 16, timl);

	clocksource_mmio_init(CLPS711X_VIRT_BASE + TC1D,
			      "clps711x_clocksource", timl, 300, 16,
			      clocksource_mmio_readw_down);

	/* Set Timer2 prescaler */
	clps_writew(DIV_ROUND_CLOSEST(timh, HZ), TC2D);

	/* Start Timer2 in prescale mode (High frequency)*/
	tmp = clps_readl(SYSCON1) | SYSCON1_TC2M | SYSCON1_TC2S;
	clps_writel(tmp, SYSCON1);

	clockevents_config_and_register(&clockevent_clps711x, timh, 0, 0);

	setup_irq(IRQ_TC2OI, &clps711x_timer_irq);
}

void clps711x_restart(enum reboot_mode mode, const char *cmd)
{
	soft_restart(0);
}

static void clps711x_idle(void)
{
	clps_writel(1, HALT);
	asm("mov r0, r0");
	asm("mov r0, r0");
}

void __init clps711x_init_early(void)
{
	arm_pm_idle = clps711x_idle;
}
