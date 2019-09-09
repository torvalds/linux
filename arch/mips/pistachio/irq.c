// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pistachio IRQ setup
 *
 * Copyright (C) 2014 Google, Inc.
 */

#include <linux/init.h>
#include <linux/irqchip.h>
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
