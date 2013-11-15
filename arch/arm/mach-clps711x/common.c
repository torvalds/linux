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

#include <asm/exception.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/sched_clock.h>
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
		.length		= SZ_64K,
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

static void int1_eoi(struct irq_data *d)
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
	.name		= "Interrupt Vector 1",
	.irq_eoi	= int1_eoi,
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

static void int2_eoi(struct irq_data *d)
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
	.name		= "Interrupt Vector 2",
	.irq_eoi	= int2_eoi,
	.irq_mask	= int2_mask,
	.irq_unmask	= int2_unmask,
};

static void int3_mask(struct irq_data *d)
{
	u32 intmr3;

	intmr3 = clps_readl(INTMR3);
	intmr3 &= ~(1 << (d->irq - 32));
	clps_writel(intmr3, INTMR3);
}

static void int3_unmask(struct irq_data *d)
{
	u32 intmr3;

	intmr3 = clps_readl(INTMR3);
	intmr3 |= 1 << (d->irq - 32);
	clps_writel(intmr3, INTMR3);
}

static struct irq_chip int3_chip = {
	.name		= "Interrupt Vector 3",
	.irq_mask	= int3_mask,
	.irq_unmask	= int3_unmask,
};

static struct {
	int			nr;
	struct irq_chip		*chip;
	irq_flow_handler_t	handle;
} clps711x_irqdescs[] __initdata = {
	{ IRQ_CSINT,	&int1_chip,	handle_fasteoi_irq,	},
	{ IRQ_EINT1,	&int1_chip,	handle_level_irq,	},
	{ IRQ_EINT2,	&int1_chip,	handle_level_irq,	},
	{ IRQ_EINT3,	&int1_chip,	handle_level_irq,	},
	{ IRQ_TC1OI,	&int1_chip,	handle_fasteoi_irq,	},
	{ IRQ_TC2OI,	&int1_chip,	handle_fasteoi_irq,	},
	{ IRQ_RTCMI,	&int1_chip,	handle_fasteoi_irq,	},
	{ IRQ_TINT,	&int1_chip,	handle_fasteoi_irq,	},
	{ IRQ_UTXINT1,	&int1_chip,	handle_level_irq,	},
	{ IRQ_URXINT1,	&int1_chip,	handle_level_irq,	},
	{ IRQ_UMSINT,	&int1_chip,	handle_fasteoi_irq,	},
	{ IRQ_SSEOTI,	&int1_chip,	handle_level_irq,	},
	{ IRQ_KBDINT,	&int2_chip,	handle_fasteoi_irq,	},
	{ IRQ_SS2RX,	&int2_chip,	handle_level_irq,	},
	{ IRQ_SS2TX,	&int2_chip,	handle_level_irq,	},
	{ IRQ_UTXINT2,	&int2_chip,	handle_level_irq,	},
	{ IRQ_URXINT2,	&int2_chip,	handle_level_irq,	},
};

void __init clps711x_init_irq(void)
{
	unsigned int i;

	/* Disable interrupts */
	clps_writel(0, INTMR1);
	clps_writel(0, INTMR2);
	clps_writel(0, INTMR3);

	/* Clear down any pending interrupts */
	clps_writel(0, BLEOI);
	clps_writel(0, MCEOI);
	clps_writel(0, COEOI);
	clps_writel(0, TC1EOI);
	clps_writel(0, TC2EOI);
	clps_writel(0, RTCEOI);
	clps_writel(0, TEOI);
	clps_writel(0, UMSEOI);
	clps_writel(0, KBDEOI);
	clps_writel(0, SRXEOF);
	clps_writel(0xffffffff, DAISR);

	for (i = 0; i < ARRAY_SIZE(clps711x_irqdescs); i++) {
		irq_set_chip_and_handler(clps711x_irqdescs[i].nr,
					 clps711x_irqdescs[i].chip,
					 clps711x_irqdescs[i].handle);
		set_irq_flags(clps711x_irqdescs[i].nr,
			      IRQF_VALID | IRQF_PROBE);
	}

	if (IS_ENABLED(CONFIG_FIQ)) {
		init_FIQ(0);
		irq_set_chip_and_handler(IRQ_DAIINT, &int3_chip,
					 handle_bad_irq);
		set_irq_flags(IRQ_DAIINT,
			      IRQF_VALID | IRQF_PROBE | IRQF_NOAUTOEN);
	}
}

static inline u32 fls16(u32 x)
{
	u32 r = 15;

	if (!(x & 0xff00)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf000)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc000)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x8000))
		r--;

	return r;
}

asmlinkage void __exception_irq_entry clps711x_handle_irq(struct pt_regs *regs)
{
	do {
		u32 irqstat;
		void __iomem *base = CLPS711X_VIRT_BASE;

		irqstat = readw_relaxed(base + INTSR1) &
			  readw_relaxed(base + INTMR1);
		if (irqstat)
			handle_IRQ(fls16(irqstat), regs);

		irqstat = readw_relaxed(base + INTSR2) &
			  readw_relaxed(base + INTMR2);
		if (irqstat) {
			handle_IRQ(fls16(irqstat) + 16, regs);
			continue;
		}

		break;
	} while (1);
}

static u32 notrace clps711x_sched_clock_read(void)
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

	setup_sched_clock(clps711x_sched_clock_read, 16, timl);

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
