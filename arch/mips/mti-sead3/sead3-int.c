/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/irqchip/mips-gic.h>

#include <asm/cpu-info.h>
#include <asm/irq.h>

void __init arch_init_irq(void)
{
	irqchip_init();

	pr_info("GIC: %spresent\n", (gic_present) ? "" : "not ");
	pr_info("EIC: %s\n",
		(current_cpu_data.options & MIPS_CPU_VEIC) ?  "on" : "off");
}

