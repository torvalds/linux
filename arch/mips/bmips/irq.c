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

unsigned int get_c0_compare_int(void)
{
	return CP0_LEGACY_COMPARE_IRQ;
}

void __init arch_init_irq(void)
{
	struct device_node *dn;

	/* Only the STB (bcm7038) controller supports SMP IRQ affinity */
	dn = of_find_compatible_node(NULL, NULL, "brcm,bcm7038-l1-intc");
	if (dn)
		of_node_put(dn);
	else
		bmips_tp1_irqs = 0;

	irqchip_init();
}

OF_DECLARE_2(irqchip, mips_cpu_intc, "mti,cpu-interrupt-controller",
	     mips_cpu_irq_of_init);
