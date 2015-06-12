/*
 * Pistachio IRQ setup
 *
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/kernel.h>

#include <asm/cpu-features.h>
#include <asm/irq_cpu.h>

void __init arch_init_irq(void)
{
	pr_info("EIC is %s\n", cpu_has_veic ? "on" : "off");
	pr_info("VINT is %s\n", cpu_has_vint ? "on" : "off");

	if (!cpu_has_veic)
		mips_cpu_irq_init();

	irqchip_init();
}
