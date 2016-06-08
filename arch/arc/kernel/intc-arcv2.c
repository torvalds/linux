/*
 * Copyright (C) 2014 Synopsys, Inc. (www.synopsys.com)
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
#include <linux/irqchip.h>
#include <asm/irq.h>

static int irq_prio;

/*
 * Early Hardware specific Interrupt setup
 * -Called very early (start_kernel -> setup_arch -> setup_processor)
 * -Platform Independent (must for any ARC Core)
 * -Needed for each CPU (hence not foldable into init_IRQ)
 */
void arc_init_IRQ(void)
{
	unsigned int tmp;

	struct irq_build {
#ifdef CONFIG_CPU_BIG_ENDIAN
		unsigned int pad:3, firq:1, prio:4, exts:8, irqs:8, ver:8;
#else
		unsigned int ver:8, irqs:8, exts:8, prio:4, firq:1, pad:3;
#endif
	} irq_bcr;

	struct aux_irq_ctrl {
#ifdef CONFIG_CPU_BIG_ENDIAN
		unsigned int res3:18, save_idx_regs:1, res2:1,
			     save_u_to_u:1, save_lp_regs:1, save_blink:1,
			     res:4, save_nr_gpr_pairs:5;
#else
		unsigned int save_nr_gpr_pairs:5, res:4,
			     save_blink:1, save_lp_regs:1, save_u_to_u:1,
			     res2:1, save_idx_regs:1, res3:18;
#endif
	} ictrl;

	*(unsigned int *)&ictrl = 0;

	ictrl.save_nr_gpr_pairs = 6;	/* r0 to r11 (r12 saved manually) */
	ictrl.save_blink = 1;
	ictrl.save_lp_regs = 1;		/* LP_COUNT, LP_START, LP_END */
	ictrl.save_u_to_u = 0;		/* user ctxt saved on kernel stack */
	ictrl.save_idx_regs = 1;	/* JLI, LDI, EI */

	WRITE_AUX(AUX_IRQ_CTRL, ictrl);

	/*
	 * ARCv2 core intc provides multiple interrupt priorities (upto 16).
	 * Typical builds though have only two levels (0-high, 1-low)
	 * Linux by default uses lower prio 1 for most irqs, reserving 0 for
	 * NMI style interrupts in future (say perf)
	 */

	READ_BCR(ARC_REG_IRQ_BCR, irq_bcr);

	irq_prio = irq_bcr.prio;	/* Encoded as N-1 for N levels */
	pr_info("archs-intc\t: %d priority levels (default %d)%s\n",
		irq_prio + 1, irq_prio,
		irq_bcr.firq ? " FIRQ (not used)":"");

	/* setup status32, don't enable intr yet as kernel doesn't want */
	tmp = read_aux_reg(0xa);
	tmp |= STATUS_AD_MASK | (irq_prio << 1);
	tmp &= ~STATUS_IE_MASK;
	asm volatile("flag %0	\n"::"r"(tmp));
}

static void arcv2_irq_mask(struct irq_data *data)
{
	write_aux_reg(AUX_IRQ_SELECT, data->irq);
	write_aux_reg(AUX_IRQ_ENABLE, 0);
}

static void arcv2_irq_unmask(struct irq_data *data)
{
	write_aux_reg(AUX_IRQ_SELECT, data->irq);
	write_aux_reg(AUX_IRQ_ENABLE, 1);
}

void arcv2_irq_enable(struct irq_data *data)
{
	/* set default priority */
	write_aux_reg(AUX_IRQ_SELECT, data->irq);
	write_aux_reg(AUX_IRQ_PRIORITY, irq_prio);

	/*
	 * hw auto enables (linux unmask) all by default
	 * So no need to do IRQ_ENABLE here
	 * XXX: However OSCI LAN need it
	 */
	write_aux_reg(AUX_IRQ_ENABLE, 1);
}

static struct irq_chip arcv2_irq_chip = {
	.name           = "ARCv2 core Intc",
	.irq_mask	= arcv2_irq_mask,
	.irq_unmask	= arcv2_irq_unmask,
	.irq_enable	= arcv2_irq_enable
};

static int arcv2_irq_map(struct irq_domain *d, unsigned int irq,
			 irq_hw_number_t hw)
{
	/*
	 * core intc IRQs [16, 23]:
	 * Statically assigned always private-per-core (Timers, WDT, IPI, PCT)
	 */
	if (hw < 24) {
		/*
		 * A subsequent request_percpu_irq() fails if percpu_devid is
		 * not set. That in turns sets NOAUTOEN, meaning each core needs
		 * to call enable_percpu_irq()
		 */
		irq_set_percpu_devid(irq);
		irq_set_chip_and_handler(irq, &arcv2_irq_chip, handle_percpu_irq);
	} else {
		irq_set_chip_and_handler(irq, &arcv2_irq_chip, handle_level_irq);
	}

	return 0;
}

static const struct irq_domain_ops arcv2_irq_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = arcv2_irq_map,
};


static int __init
init_onchip_IRQ(struct device_node *intc, struct device_node *parent)
{
	struct irq_domain *root_domain;

	if (parent)
		panic("DeviceTree incore intc not a root irq controller\n");

	root_domain = irq_domain_add_linear(intc, NR_CPU_IRQS, &arcv2_irq_ops, NULL);
	if (!root_domain)
		panic("root irq domain not avail\n");

	/*
	 * Needed for primary domain lookup to succeed
	 * This is a primary irqchip, and can never have a parent
	 */
	irq_set_default_host(root_domain);

#ifdef CONFIG_SMP
	irq_create_mapping(root_domain, IPI_IRQ);
#endif
	irq_create_mapping(root_domain, SOFTIRQ_IRQ);

	return 0;
}

IRQCHIP_DECLARE(arc_intc, "snps,archs-intc", init_onchip_IRQ);
