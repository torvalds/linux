/*
 * Copyright (C) 2011-12 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/irqdomain.h>
#include <asm/sections.h>
#include <asm/irq.h>

/*
 * Early Hardware specific Interrupt setup
 * -Called very early (start_kernel -> setup_arch -> setup_processor)
 * -Platform Independent (must for any ARC700)
 * -Needed for each CPU (hence not foldable into init_IRQ)
 *
 * what it does ?
 * -setup Vector Table Base Reg - in case Linux not linked at 0x8000_0000
 * -Disable all IRQs (on CPU side)
 * -Optionally, setup the High priority Interrupts as Level 2 IRQs
 */
void __init arc_init_IRQ(void)
{
	int level_mask = 0;

	write_aux_reg(AUX_INTR_VEC_BASE, _int_vec_base_lds);

	/* Disable all IRQs: enable them as devices request */
	write_aux_reg(AUX_IENABLE, 0);

       /* setup any high priority Interrupts (Level2 in ARCompact jargon) */
#ifdef CONFIG_ARC_IRQ3_LV2
	level_mask |= (1 << 3);
#endif
#ifdef CONFIG_ARC_IRQ5_LV2
	level_mask |= (1 << 5);
#endif
#ifdef CONFIG_ARC_IRQ6_LV2
	level_mask |= (1 << 6);
#endif

	if (level_mask) {
		pr_info("Level-2 interrupts bitset %x\n", level_mask);
		write_aux_reg(AUX_IRQ_LEV, level_mask);
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

static void arc_mask_irq(struct irq_data *data)
{
	arch_mask_irq(data->irq);
}

static void arc_unmask_irq(struct irq_data *data)
{
	arch_unmask_irq(data->irq);
}

static struct irq_chip onchip_intc = {
	.name           = "ARC In-core Intc",
	.irq_mask	= arc_mask_irq,
	.irq_unmask	= arc_unmask_irq,
};

static int arc_intc_domain_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hw)
{
	if (irq == TIMER0_IRQ)
		irq_set_chip_and_handler(irq, &onchip_intc, handle_percpu_irq);
	else
		irq_set_chip_and_handler(irq, &onchip_intc, handle_level_irq);

	return 0;
}

static const struct irq_domain_ops arc_intc_domain_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = arc_intc_domain_map,
};

static struct irq_domain *root_domain;

void __init init_onchip_IRQ(void)
{
	struct device_node *intc = NULL;

	intc = of_find_compatible_node(NULL, NULL, "snps,arc700-intc");
	if(!intc)
		panic("DeviceTree Missing incore intc\n");

	root_domain = irq_domain_add_legacy(intc, NR_IRQS, 0, 0,
					    &arc_intc_domain_ops, NULL);

	if (!root_domain)
		panic("root irq domain not avail\n");

	/* with this we don't need to export root_domain */
	irq_set_default_host(root_domain);
}

/*
 * Late Interrupt system init called from start_kernel for Boot CPU only
 *
 * Since slab must already be initialized, platforms can start doing any
 * needed request_irq( )s
 */
void __init init_IRQ(void)
{
	init_onchip_IRQ();
	plat_init_IRQ();

#ifdef CONFIG_SMP
	/* Master CPU can initialize it's side of IPI */
	arc_platform_smp_init_cpu();
#endif
}

/*
 * "C" Entry point for any ARC ISR, called from low level vector handler
 * @irq is the vector number read from ICAUSE reg of on-chip intc
 */
void arch_do_IRQ(unsigned int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();
	generic_handle_irq(irq);
	irq_exit();
	set_irq_regs(old_regs);
}

int __init get_hw_config_num_irq(void)
{
	uint32_t val = read_aux_reg(ARC_REG_VECBASE_BCR);

	switch (val & 0x03) {
	case 0:
		return 16;
	case 1:
		return 32;
	case 2:
		return 8;
	default:
		return 0;
	}

	return 0;
}

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

	unsigned long flags;
	flags = arch_local_save_flags();

	/* Allow both L1 and L2 at the onset */
	flags |= (STATUS_E1_MASK | STATUS_E2_MASK);

	/* Called from hard ISR (between irq_enter and irq_exit) */
	if (in_irq()) {

		/* If in L2 ISR, don't re-enable any further IRQs as this can
		 * cause IRQ priorities to get upside down. e.g. it could allow
		 * L1 be taken while in L2 hard ISR which is wrong not only in
		 * theory, it can also cause the dreaded L1-L2-L1 scenario
		 */
		if (flags & STATUS_A2_MASK)
			flags &= ~(STATUS_E1_MASK | STATUS_E2_MASK);

		/* Even if in L1 ISR, allowe Higher prio L2 IRQs */
		else if (flags & STATUS_A1_MASK)
			flags &= ~(STATUS_E1_MASK);
	}

	/* called from soft IRQ, ideally we want to re-enable all levels */

	else if (in_softirq()) {

		/* However if this is case of L1 interrupted by L2,
		 * re-enabling both may cause whaco L1-L2-L1 scenario
		 * because ARC700 allows level 1 to interrupt an active L2 ISR
		 * Thus we disable both
		 * However some code, executing in soft ISR wants some IRQs
		 * to be enabled so we re-enable L2 only
		 *
		 * How do we determine L1 intr by L2
		 *  -A2 is set (means in L2 ISR)
		 *  -E1 is set in this ISR's pt_regs->status32 which is
		 *      saved copy of status32_l2 when l2 ISR happened
		 */
		struct pt_regs *pt = get_irq_regs();
		if ((flags & STATUS_A2_MASK) && pt &&
		    (pt->status32 & STATUS_A1_MASK)) {
			/*flags &= ~(STATUS_E1_MASK | STATUS_E2_MASK); */
			flags &= ~(STATUS_E1_MASK);
		}
	}

	arch_local_irq_restore(flags);
}

#else /* ! CONFIG_ARC_COMPACT_IRQ_LEVELS */

/*
 * Simpler version for only 1 level of interrupt
 * Here we only Worry about Level 1 Bits
 */
void arch_local_irq_enable(void)
{
	unsigned long flags;

	/*
	 * ARC IDE Drivers tries to re-enable interrupts from hard-isr
	 * context which is simply wrong
	 */
	if (in_irq()) {
		WARN_ONCE(1, "IRQ enabled from hard-isr");
		return;
	}

	flags = arch_local_save_flags();
	flags |= (STATUS_E1_MASK | STATUS_E2_MASK);
	arch_local_irq_restore(flags);
}
#endif
EXPORT_SYMBOL(arch_local_irq_enable);
