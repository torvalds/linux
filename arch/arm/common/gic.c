/*
 *  linux/arch/arm/common/gic.c
 *
 *  Copyright (C) 2002 ARM Limited, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Interrupt architecture for the GIC:
 *
 * o There is one Interrupt Distributor, which receives interrupts
 *   from system devices and sends them to the Interrupt Controllers.
 *
 * o There is one CPU Interface per CPU, which sends interrupts sent
 *   by the Distributor, and interrupts generated locally, to the
 *   associated CPU.
 *
 * Note that IRQs 0-31 are special - they are local to each CPU.
 * As such, the enable set/clear, pending set/clear and active bit
 * registers are banked per-cpu for these sources.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/smp.h>
#include <linux/cpumask.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach/irq.h>
#include <asm/hardware/gic.h>

static void __iomem *gic_dist_base;
static void __iomem *gic_cpu_base;
static DEFINE_SPINLOCK(irq_controller_lock);

/*
 * Routines to acknowledge, disable and enable interrupts
 *
 * Linux assumes that when we're done with an interrupt we need to
 * unmask it, in the same way we need to unmask an interrupt when
 * we first enable it.
 *
 * The GIC has a seperate notion of "end of interrupt" to re-enable
 * an interrupt after handling, in order to support hardware
 * prioritisation.
 *
 * We can make the GIC behave in the way that Linux expects by making
 * our "acknowledge" routine disable the interrupt, then mark it as
 * complete.
 */
static void gic_ack_irq(unsigned int irq)
{
	u32 mask = 1 << (irq % 32);

	spin_lock(&irq_controller_lock);
	writel(mask, gic_dist_base + GIC_DIST_ENABLE_CLEAR + (irq / 32) * 4);
	writel(irq, gic_cpu_base + GIC_CPU_EOI);
	spin_unlock(&irq_controller_lock);
}

static void gic_mask_irq(unsigned int irq)
{
	u32 mask = 1 << (irq % 32);

	spin_lock(&irq_controller_lock);
	writel(mask, gic_dist_base + GIC_DIST_ENABLE_CLEAR + (irq / 32) * 4);
	spin_unlock(&irq_controller_lock);
}

static void gic_unmask_irq(unsigned int irq)
{
	u32 mask = 1 << (irq % 32);

	spin_lock(&irq_controller_lock);
	writel(mask, gic_dist_base + GIC_DIST_ENABLE_SET + (irq / 32) * 4);
	spin_unlock(&irq_controller_lock);
}

#ifdef CONFIG_SMP
static void gic_set_cpu(unsigned int irq, cpumask_t mask_val)
{
	void __iomem *reg = gic_dist_base + GIC_DIST_TARGET + (irq & ~3);
	unsigned int shift = (irq % 4) * 8;
	unsigned int cpu = first_cpu(mask_val);
	u32 val;

	spin_lock(&irq_controller_lock);
	irq_desc[irq].cpu = cpu;
	val = readl(reg) & ~(0xff << shift);
	val |= 1 << (cpu + shift);
	writel(val, reg);
	spin_unlock(&irq_controller_lock);
}
#endif

static struct irq_chip gic_chip = {
	.name		= "GIC",
	.ack		= gic_ack_irq,
	.mask		= gic_mask_irq,
	.unmask		= gic_unmask_irq,
#ifdef CONFIG_SMP
	.set_affinity	= gic_set_cpu,
#endif
};

void __init gic_dist_init(void __iomem *base)
{
	unsigned int max_irq, i;
	u32 cpumask = 1 << smp_processor_id();

	cpumask |= cpumask << 8;
	cpumask |= cpumask << 16;

	gic_dist_base = base;

	writel(0, base + GIC_DIST_CTRL);

	/*
	 * Find out how many interrupts are supported.
	 */
	max_irq = readl(base + GIC_DIST_CTR) & 0x1f;
	max_irq = (max_irq + 1) * 32;

	/*
	 * The GIC only supports up to 1020 interrupt sources.
	 * Limit this to either the architected maximum, or the
	 * platform maximum.
	 */
	if (max_irq > max(1020, NR_IRQS))
		max_irq = max(1020, NR_IRQS);

	/*
	 * Set all global interrupts to be level triggered, active low.
	 */
	for (i = 32; i < max_irq; i += 16)
		writel(0, base + GIC_DIST_CONFIG + i * 4 / 16);

	/*
	 * Set all global interrupts to this CPU only.
	 */
	for (i = 32; i < max_irq; i += 4)
		writel(cpumask, base + GIC_DIST_TARGET + i * 4 / 4);

	/*
	 * Set priority on all interrupts.
	 */
	for (i = 0; i < max_irq; i += 4)
		writel(0xa0a0a0a0, base + GIC_DIST_PRI + i * 4 / 4);

	/*
	 * Disable all interrupts.
	 */
	for (i = 0; i < max_irq; i += 32)
		writel(0xffffffff, base + GIC_DIST_ENABLE_CLEAR + i * 4 / 32);

	/*
	 * Setup the Linux IRQ subsystem.
	 */
	for (i = 29; i < max_irq; i++) {
		set_irq_chip(i, &gic_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	writel(1, base + GIC_DIST_CTRL);
}

void __cpuinit gic_cpu_init(void __iomem *base)
{
	gic_cpu_base = base;
	writel(0xf0, base + GIC_CPU_PRIMASK);
	writel(1, base + GIC_CPU_CTRL);
}

#ifdef CONFIG_SMP
void gic_raise_softirq(cpumask_t cpumask, unsigned int irq)
{
	unsigned long map = *cpus_addr(cpumask);

	writel(map << 16 | irq, gic_dist_base + GIC_DIST_SOFTINT);
}
#endif
