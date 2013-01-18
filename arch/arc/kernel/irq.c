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
 */
void __init arc_init_IRQ(void)
{
	int level_mask = level_mask;

	write_aux_reg(AUX_INTR_VEC_BASE, _int_vec_base_lds);

	/* Disable all IRQs: enable them as devices request */
	write_aux_reg(AUX_IENABLE, 0);
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
EXPORT_SYMBOL(arch_local_irq_enable);
