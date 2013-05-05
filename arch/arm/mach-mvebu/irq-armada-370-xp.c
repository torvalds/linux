/*
 * Marvell Armada 370 and Armada XP SoC IRQ handling
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 * Ben Dooks <ben.dooks@codethink.co.uk>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <asm/mach/arch.h>
#include <asm/exception.h>
#include <asm/smp_plat.h>
#include <asm/hardware/cache-l2x0.h>

/* Interrupt Controller Registers Map */
#define ARMADA_370_XP_INT_SET_MASK_OFFS		(0x48)
#define ARMADA_370_XP_INT_CLEAR_MASK_OFFS	(0x4C)

#define ARMADA_370_XP_INT_CONTROL		(0x00)
#define ARMADA_370_XP_INT_SET_ENABLE_OFFS	(0x30)
#define ARMADA_370_XP_INT_CLEAR_ENABLE_OFFS	(0x34)
#define ARMADA_370_XP_INT_SOURCE_CTL(irq)	(0x100 + irq*4)

#define ARMADA_370_XP_CPU_INTACK_OFFS		(0x44)

#define ARMADA_370_XP_SW_TRIG_INT_OFFS           (0x4)
#define ARMADA_370_XP_IN_DRBEL_MSK_OFFS          (0xc)
#define ARMADA_370_XP_IN_DRBEL_CAUSE_OFFS        (0x8)

#define ARMADA_370_XP_MAX_PER_CPU_IRQS		(28)

#define ARMADA_370_XP_TIMER0_PER_CPU_IRQ	(5)

#define ACTIVE_DOORBELLS			(8)

static DEFINE_RAW_SPINLOCK(irq_controller_lock);

static void __iomem *per_cpu_int_base;
static void __iomem *main_int_base;
static struct irq_domain *armada_370_xp_mpic_domain;

/*
 * In SMP mode:
 * For shared global interrupts, mask/unmask global enable bit
 * For CPU interrupts, mask/unmask the calling CPU's bit
 */
static void armada_370_xp_irq_mask(struct irq_data *d)
{
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	if (hwirq != ARMADA_370_XP_TIMER0_PER_CPU_IRQ)
		writel(hwirq, main_int_base +
				ARMADA_370_XP_INT_CLEAR_ENABLE_OFFS);
	else
		writel(hwirq, per_cpu_int_base +
				ARMADA_370_XP_INT_SET_MASK_OFFS);
}

static void armada_370_xp_irq_unmask(struct irq_data *d)
{
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	if (hwirq != ARMADA_370_XP_TIMER0_PER_CPU_IRQ)
		writel(hwirq, main_int_base +
				ARMADA_370_XP_INT_SET_ENABLE_OFFS);
	else
		writel(hwirq, per_cpu_int_base +
				ARMADA_370_XP_INT_CLEAR_MASK_OFFS);
}

#ifdef CONFIG_SMP
static int armada_xp_set_affinity(struct irq_data *d,
				  const struct cpumask *mask_val, bool force)
{
	unsigned long reg;
	unsigned long new_mask = 0;
	unsigned long online_mask = 0;
	unsigned long count = 0;
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	int cpu;

	for_each_cpu(cpu, mask_val) {
		new_mask |= 1 << cpu_logical_map(cpu);
		count++;
	}

	/*
	 * Forbid mutlicore interrupt affinity
	 * This is required since the MPIC HW doesn't limit
	 * several CPUs from acknowledging the same interrupt.
	 */
	if (count > 1)
		return -EINVAL;

	for_each_cpu(cpu, cpu_online_mask)
		online_mask |= 1 << cpu_logical_map(cpu);

	raw_spin_lock(&irq_controller_lock);

	reg = readl(main_int_base + ARMADA_370_XP_INT_SOURCE_CTL(hwirq));
	reg = (reg & (~online_mask)) | new_mask;
	writel(reg, main_int_base + ARMADA_370_XP_INT_SOURCE_CTL(hwirq));

	raw_spin_unlock(&irq_controller_lock);

	return 0;
}
#endif

static struct irq_chip armada_370_xp_irq_chip = {
	.name		= "armada_370_xp_irq",
	.irq_mask       = armada_370_xp_irq_mask,
	.irq_mask_ack   = armada_370_xp_irq_mask,
	.irq_unmask     = armada_370_xp_irq_unmask,
#ifdef CONFIG_SMP
	.irq_set_affinity = armada_xp_set_affinity,
#endif
};

static int armada_370_xp_mpic_irq_map(struct irq_domain *h,
				      unsigned int virq, irq_hw_number_t hw)
{
	armada_370_xp_irq_mask(irq_get_irq_data(virq));
	if (hw != ARMADA_370_XP_TIMER0_PER_CPU_IRQ)
		writel(hw, per_cpu_int_base +
			ARMADA_370_XP_INT_CLEAR_MASK_OFFS);
	else
		writel(hw, main_int_base + ARMADA_370_XP_INT_SET_ENABLE_OFFS);
	irq_set_status_flags(virq, IRQ_LEVEL);

	if (hw == ARMADA_370_XP_TIMER0_PER_CPU_IRQ) {
		irq_set_percpu_devid(virq);
		irq_set_chip_and_handler(virq, &armada_370_xp_irq_chip,
					handle_percpu_devid_irq);

	} else {
		irq_set_chip_and_handler(virq, &armada_370_xp_irq_chip,
					handle_level_irq);
	}
	set_irq_flags(virq, IRQF_VALID | IRQF_PROBE);

	return 0;
}

#ifdef CONFIG_SMP
void armada_mpic_send_doorbell(const struct cpumask *mask, unsigned int irq)
{
	int cpu;
	unsigned long map = 0;

	/* Convert our logical CPU mask into a physical one. */
	for_each_cpu(cpu, mask)
		map |= 1 << cpu_logical_map(cpu);

	/*
	 * Ensure that stores to Normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	dsb();

	/* submit softirq */
	writel((map << 8) | irq, main_int_base +
		ARMADA_370_XP_SW_TRIG_INT_OFFS);
}

void armada_xp_mpic_smp_cpu_init(void)
{
	/* Clear pending IPIs */
	writel(0, per_cpu_int_base + ARMADA_370_XP_IN_DRBEL_CAUSE_OFFS);

	/* Enable first 8 IPIs */
	writel((1 << ACTIVE_DOORBELLS) - 1, per_cpu_int_base +
		ARMADA_370_XP_IN_DRBEL_MSK_OFFS);

	/* Unmask IPI interrupt */
	writel(0, per_cpu_int_base + ARMADA_370_XP_INT_CLEAR_MASK_OFFS);
}
#endif /* CONFIG_SMP */

static struct irq_domain_ops armada_370_xp_mpic_irq_ops = {
	.map = armada_370_xp_mpic_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static int __init armada_370_xp_mpic_of_init(struct device_node *node,
					     struct device_node *parent)
{
	u32 control;

	main_int_base = of_iomap(node, 0);
	per_cpu_int_base = of_iomap(node, 1);

	BUG_ON(!main_int_base);
	BUG_ON(!per_cpu_int_base);

	control = readl(main_int_base + ARMADA_370_XP_INT_CONTROL);

	armada_370_xp_mpic_domain =
		irq_domain_add_linear(node, (control >> 2) & 0x3ff,
				&armada_370_xp_mpic_irq_ops, NULL);

	if (!armada_370_xp_mpic_domain)
		panic("Unable to add Armada_370_Xp MPIC irq domain (DT)\n");

	irq_set_default_host(armada_370_xp_mpic_domain);

#ifdef CONFIG_SMP
	armada_xp_mpic_smp_cpu_init();

	/*
	 * Set the default affinity from all CPUs to the boot cpu.
	 * This is required since the MPIC doesn't limit several CPUs
	 * from acknowledging the same interrupt.
	 */
	cpumask_clear(irq_default_affinity);
	cpumask_set_cpu(smp_processor_id(), irq_default_affinity);

#endif

	return 0;
}

asmlinkage void __exception_irq_entry armada_370_xp_handle_irq(struct pt_regs
							       *regs)
{
	u32 irqstat, irqnr;

	do {
		irqstat = readl_relaxed(per_cpu_int_base +
					ARMADA_370_XP_CPU_INTACK_OFFS);
		irqnr = irqstat & 0x3FF;

		if (irqnr > 1022)
			break;

		if (irqnr > 0) {
			irqnr =	irq_find_mapping(armada_370_xp_mpic_domain,
					irqnr);
			handle_IRQ(irqnr, regs);
			continue;
		}
#ifdef CONFIG_SMP
		/* IPI Handling */
		if (irqnr == 0) {
			u32 ipimask, ipinr;

			ipimask = readl_relaxed(per_cpu_int_base +
						ARMADA_370_XP_IN_DRBEL_CAUSE_OFFS)
				& 0xFF;

			writel(0x0, per_cpu_int_base +
				ARMADA_370_XP_IN_DRBEL_CAUSE_OFFS);

			/* Handle all pending doorbells */
			for (ipinr = 0; ipinr < ACTIVE_DOORBELLS; ipinr++) {
				if (ipimask & (0x1 << ipinr))
					handle_IPI(ipinr, regs);
			}
			continue;
		}
#endif

	} while (1);
}

static const struct of_device_id mpic_of_match[] __initconst = {
	{.compatible = "marvell,mpic", .data = armada_370_xp_mpic_of_init},
	{},
};

void __init armada_370_xp_init_irq(void)
{
	of_irq_init(mpic_of_match);
#ifdef CONFIG_CACHE_L2X0
	l2x0_of_init(0, ~0UL);
#endif
}
