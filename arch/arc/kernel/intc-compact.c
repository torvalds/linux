// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011-12 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <asm/irq.h>

#define NR_CPU_IRQS	32	/* number of irq lines coming in */
#define TIMER0_IRQ	3	/* Fixed by ISA */

/*
 * Early Hardware specific Interrupt setup
 * -Platform independent, needed for each CPU (not foldable into init_IRQ)
 * -Called very early (start_kernel -> setup_arch -> setup_processor)
 *
 * what it does ?
 * -Optionally, setup the High priority Interrupts as Level 2 IRQs
 */
void arc_init_IRQ(void)
{
	unsigned int level_mask = 0, i;

       /* Is timer high priority Interrupt (Level2 in ARCompact jargon) */
	level_mask |= IS_ENABLED(CONFIG_ARC_COMPACT_IRQ_LEVELS) << TIMER0_IRQ;

	/*
	 * Write to register, even if no LV2 IRQs configured to reset it
	 * in case bootloader had mucked with it
	 */
	write_aux_reg(AUX_IRQ_LEV, level_mask);

	if (level_mask)
		pr_info("Level-2 interrupts bitset %x\n", level_mask);

	/*
	 * Disable all IRQ lines so faulty external hardware won't
	 * trigger interrupt that kernel is not ready to handle.
	 */
	for (i = TIMER0_IRQ; i < NR_CPU_IRQS; i++) {
		unsigned int ienb;

		ienb = read_aux_reg(AUX_IENABLE);
		ienb &= ~(1 << i);
		write_aux_reg(AUX_IENABLE, ienb);
	}
}

/*
 * ARC700 core includes a simple on-chip intc supporting
 * -per IRQ enable/disable
 * -2 levels of interrupts (high/low)
 * -all interrupts being level triggered
 *
 * To reduce platform code, we assume all IRQs directly hooked-up into intc.
 * Platforms with external intc, hence cascaded IRQs, are free to over-ride
 * below, per IRQ.
 */

static void arc_irq_mask(struct irq_data *data)
{
	unsigned int ienb;

	ienb = read_aux_reg(AUX_IENABLE);
	ienb &= ~(1 << data->hwirq);
	write_aux_reg(AUX_IENABLE, ienb);
}

static void arc_irq_unmask(struct irq_data *data)
{
	unsigned int ienb;

	ienb = read_aux_reg(AUX_IENABLE);
	ienb |= (1 << data->hwirq);
	write_aux_reg(AUX_IENABLE, ienb);
}

static struct irq_chip onchip_intc = {
	.name           = "ARC In-core Intc",
	.irq_mask	= arc_irq_mask,
	.irq_unmask	= arc_irq_unmask,
};

static int arc_intc_domain_map(struct irq_domain *d, unsigned int irq,
			       irq_hw_number_t hw)
{
	switch (hw) {
	case TIMER0_IRQ:
		irq_set_percpu_devid(irq);
		irq_set_chip_and_handler(irq, &onchip_intc, handle_percpu_irq);
		break;
	default:
		irq_set_chip_and_handler(irq, &onchip_intc, handle_level_irq);
	}
	return 0;
}

static const struct irq_domain_ops arc_intc_domain_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = arc_intc_domain_map,
};

static int __init
init_onchip_IRQ(struct device_node *intc, struct device_node *parent)
{
	struct irq_domain *root_domain;

	if (parent)
		panic("DeviceTree incore intc not a root irq controller\n");

	root_domain = irq_domain_add_linear(intc, NR_CPU_IRQS,
					    &arc_intc_domain_ops, NULL);
	if (!root_domain)
		panic("root irq domain not avail\n");

	/*
	 * Needed for primary domain lookup to succeed
	 * This is a primary irqchip, and can never have a parent
	 */
	irq_set_default_host(root_domain);

	return 0;
}

IRQCHIP_DECLARE(arc_intc, "snps,arc700-intc", init_onchip_IRQ);

/*
 * arch_local_irq_enable - Enable interrupts.
 *
 * 1. Explicitly called to re-enable interrupts
 * 2. Implicitly called from spin_unlock_irq, write_unlock_irq etc
 *    which maybe in hard ISR itself
 *
 * Semantics of this function change depending on where it is called from:
 *
 * -If called from hard-ISR, it must not invert interrupt priorities
 *  e.g. suppose TIMER is high priority (Level 2) IRQ
 *    Time hard-ISR, timer_interrupt( ) calls spin_unlock_irq several times.
 *    Here local_irq_enable( ) shd not re-enable lower priority interrupts
 * -If called from soft-ISR, it must re-enable all interrupts
 *    soft ISR are low prioity jobs which can be very slow, thus all IRQs
 *    must be enabled while they run.
 *    Now hardware context wise we may still be in L2 ISR (not done rtie)
 *    still we must re-enable both L1 and L2 IRQs
 *  Another twist is prev scenario with flow being
 *     L1 ISR ==> interrupted by L2 ISR  ==> L2 soft ISR
 *     here we must not re-enable Ll as prev Ll Interrupt's h/w context will get
 *     over-written (this is deficiency in ARC700 Interrupt mechanism)
 */

#ifdef CONFIG_ARC_COMPACT_IRQ_LEVELS	/* Complex version for 2 IRQ levels */

void arch_local_irq_enable(void)
{
	unsigned long flags = arch_local_save_flags();

	if (flags & STATUS_A2_MASK)
		flags |= STATUS_E2_MASK;
	else if (flags & STATUS_A1_MASK)
		flags |= STATUS_E1_MASK;

	arch_local_irq_restore(flags);
}

EXPORT_SYMBOL(arch_local_irq_enable);
#endif
