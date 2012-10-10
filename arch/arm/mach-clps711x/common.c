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
#include <linux/clk-provider.h>

#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/system_misc.h>

#include <mach/hardware.h>

static struct clk *clk_pll, *clk_bus, *clk_uart, *clk_timerl, *clk_timerh,
		  *clk_tint, *clk_spi;

/*
 * This maps the generic CLPS711x registers
 */
static struct map_desc clps711x_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)CLPS711X_VIRT_BASE,
		.pfn		= __phys_to_pfn(CLPS711X_PHYS_BASE),
		.length		= SZ_1M,
		.type		= MT_DEVICE
	}
};

void __init clps711x_map_io(void)
{
	iotable_init(clps711x_io_desc, ARRAY_SIZE(clps711x_io_desc));
}

static void int1_mask(struct irq_data *d)
{
	u32 intmr1;

	intmr1 = clps_readl(INTMR1);
	intmr1 &= ~(1 << d->irq);
	clps_writel(intmr1, INTMR1);
}

static void int1_ack(struct irq_data *d)
{
	switch (d->irq) {
	case IRQ_CSINT:  clps_writel(0, COEOI);  break;
	case IRQ_TC1OI:  clps_writel(0, TC1EOI); break;
	case IRQ_TC2OI:  clps_writel(0, TC2EOI); break;
	case IRQ_RTCMI:  clps_writel(0, RTCEOI); break;
	case IRQ_TINT:   clps_writel(0, TEOI);   break;
	case IRQ_UMSINT: clps_writel(0, UMSEOI); break;
	}
}

static void int1_unmask(struct irq_data *d)
{
	u32 intmr1;

	intmr1 = clps_readl(INTMR1);
	intmr1 |= 1 << d->irq;
	clps_writel(intmr1, INTMR1);
}

static struct irq_chip int1_chip = {
	.irq_ack	= int1_ack,
	.irq_mask	= int1_mask,
	.irq_unmask	= int1_unmask,
};

static void int2_mask(struct irq_data *d)
{
	u32 intmr2;

	intmr2 = clps_readl(INTMR2);
	intmr2 &= ~(1 << (d->irq - 16));
	clps_writel(intmr2, INTMR2);
}

static void int2_ack(struct irq_data *d)
{
	switch (d->irq) {
	case IRQ_KBDINT: clps_writel(0, KBDEOI); break;
	}
}

static void int2_unmask(struct irq_data *d)
{
	u32 intmr2;

	intmr2 = clps_readl(INTMR2);
	intmr2 |= 1 << (d->irq - 16);
	clps_writel(intmr2, INTMR2);
}

static struct irq_chip int2_chip = {
	.irq_ack	= int2_ack,
	.irq_mask	= int2_mask,
	.irq_unmask	= int2_unmask,
};

void __init clps711x_init_irq(void)
{
	unsigned int i;

	for (i = 0; i < NR_IRQS; i++) {
	        if (INT1_IRQS & (1 << i)) {
			irq_set_chip_and_handler(i, &int1_chip,
						 handle_level_irq);
			set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
		}
		if (INT2_IRQS & (1 << i)) {
			irq_set_chip_and_handler(i, &int2_chip,
						 handle_level_irq);
			set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
		}
	}

	/*
	 * Disable interrupts
	 */
	clps_writel(0, INTMR1);
	clps_writel(0, INTMR2);

	/*
	 * Clear down any pending interrupts
	 */
	clps_writel(0, COEOI);
	clps_writel(0, TC1EOI);
	clps_writel(0, TC2EOI);
	clps_writel(0, RTCEOI);
	clps_writel(0, TEOI);
	clps_writel(0, UMSEOI);
	clps_writel(0, SYNCIO);
	clps_writel(0, KBDEOI);
}

static void clps711x_clockevent_set_mode(enum clock_event_mode mode,
					 struct clock_event_device *evt)
{
}

static struct clock_event_device clockevent_clps711x = {
	.name		= "CLPS711x Clockevents",
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
	.name		= "CLPS711x Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= clps711x_timer_interrupt,
};

static void add_fixed_clk(struct clk *clk, const char *name, int rate)
{
	clk = clk_register_fixed_rate(NULL, name, NULL, CLK_IS_ROOT, rate);
	clk_register_clkdev(clk, name, NULL);
}

static void __init clps711x_timer_init(void)
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
		timh = cpu / 144;

	timl = timh / 256;

	/* All clocks are fixed */
	add_fixed_clk(clk_pll, "pll", pll);
	add_fixed_clk(clk_bus, "bus", bus);
	add_fixed_clk(clk_uart, "uart", uart);
	add_fixed_clk(clk_timerl, "timer_lf", timl);
	add_fixed_clk(clk_timerh, "timer_hf", timh);
	add_fixed_clk(clk_tint, "tint", 64);
	add_fixed_clk(clk_spi, "spi", spi);

	pr_info("CPU frequency set at %i Hz.\n", cpu);

	clps_writew(DIV_ROUND_CLOSEST(timh, HZ), TC2D);

	tmp = clps_readl(SYSCON1);
	tmp |= SYSCON1_TC2S | SYSCON1_TC2M;
	clps_writel(tmp, SYSCON1);

	clockevents_config_and_register(&clockevent_clps711x, timh, 1, 0xffff);

	setup_irq(IRQ_TC2OI, &clps711x_timer_irq);
}

struct sys_timer clps711x_timer = {
	.init		= clps711x_timer_init,
};

void clps711x_restart(char mode, const char *cmd)
{
	soft_restart(0);
}

static void clps711x_idle(void)
{
	clps_writel(1, HALT);
	__asm__ __volatile__(
	"mov    r0, r0\n\
	mov     r0, r0");
}

static int __init clps711x_idle_init(void)
{
	arm_pm_idle = clps711x_idle;
	return 0;
}

arch_initcall(clps711x_idle_init);
