/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/io.h>

#include <asm/irq_cpu.h>
#include <asm/setup.h>

#include <asm/mips-boards/sead3int.h>

#define SEAD_CONFIG_GIC_PRESENT_SHF	1
#define SEAD_CONFIG_GIC_PRESENT_MSK	(1 << SEAD_CONFIG_GIC_PRESENT_SHF)
#define SEAD_CONFIG_BASE		0x1b100110
#define SEAD_CONFIG_SIZE		4

static void __iomem *sead3_config_reg;

void __init arch_init_irq(void)
{
	if (!cpu_has_veic)
		mips_cpu_irq_init();

	sead3_config_reg = ioremap_nocache(SEAD_CONFIG_BASE, SEAD_CONFIG_SIZE);
	gic_present = (__raw_readl(sead3_config_reg) &
		       SEAD_CONFIG_GIC_PRESENT_MSK) >>
		SEAD_CONFIG_GIC_PRESENT_SHF;
	pr_info("GIC: %spresent\n", (gic_present) ? "" : "not ");
	pr_info("EIC: %s\n",
		(current_cpu_data.options & MIPS_CPU_VEIC) ?  "on" : "off");

	if (gic_present)
		gic_init(GIC_BASE_ADDR, GIC_ADDRSPACE_SZ, CPU_INT_GIC,
			 MIPS_GIC_IRQ_BASE);
}

