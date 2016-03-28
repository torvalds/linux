/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2014 Broadcom Corporation
 * Author: Kevin Cernekee <cernekee@gmail.com>
 */

#include <linux/of.h>
#include <linux/irqchip.h>

#include <asm/bmips.h>
#include <asm/irq.h>
#include <asm/irq_cpu.h>
#include <asm/time.h>

static const struct of_device_id smp_intc_dt_match[] = {
	{ .compatible = "brcm,bcm7038-l1-intc" },
	{ .compatible = "brcm,bcm6345-l1-intc" },
	{}
};

unsigned int get_c0_compare_int(void)
{
	return CP0_LEGACY_COMPARE_IRQ;
}

void __init arch_init_irq(void)
{
	struct device_node *dn;

	/* Only these controllers support SMP IRQ affinity */
	dn = of_find_matching_node(NULL, smp_intc_dt_match);
	if (dn)
		of_node_put(dn);
	else
		bmips_tp1_irqs = 0;

	irqchip_init();
}

IRQCHIP_DECLARE(mips_cpu_intc, "mti,cpu-interrupt-controller",
	     mips_cpu_irq_of_init);
