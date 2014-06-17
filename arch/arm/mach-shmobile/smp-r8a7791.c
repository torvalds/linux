/*
 * SMP support for r8a7791
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <asm/smp_plat.h>
#include <mach/r8a7791.h>
#include <mach/rcar-gen2.h>
#include "common.h"

#define RST		0xe6160000
#define CA15BAR		0x0020
#define CA15RESCNT	0x0040
#define RAM		0xe6300000

static void __init r8a7791_smp_prepare_cpus(unsigned int max_cpus)
{
	void __iomem *p;
	u32 bar;

	/* let APMU code install data related to shmobile_boot_vector */
	shmobile_smp_apmu_prepare_cpus(max_cpus);

	/* RAM for jump stub, because BAR requires 256KB aligned address */
	p = ioremap_nocache(RAM, shmobile_boot_size);
	memcpy_toio(p, shmobile_boot_vector, shmobile_boot_size);
	iounmap(p);

	/* setup reset vectors */
	p = ioremap_nocache(RST, 0x63);
	bar = (RAM >> 8) & 0xfffffc00;
	writel_relaxed(bar, p + CA15BAR);
	writel_relaxed(bar | 0x10, p + CA15BAR);

	/* enable clocks to all CPUs */
	writel_relaxed((readl_relaxed(p + CA15RESCNT) & ~0x0f) | 0xa5a50000,
		       p + CA15RESCNT);
	iounmap(p);
}

static int r8a7791_smp_boot_secondary(unsigned int cpu,
				      struct task_struct *idle)
{
	/* Error out when hardware debug mode is enabled */
	if (rcar_gen2_read_mode_pins() & BIT(21)) {
		pr_warn("Unable to boot CPU%u when MD21 is set\n", cpu);
		return -ENOTSUPP;
	}

	return shmobile_smp_apmu_boot_secondary(cpu, idle);
}

struct smp_operations r8a7791_smp_ops __initdata = {
	.smp_prepare_cpus	= r8a7791_smp_prepare_cpus,
	.smp_boot_secondary	= r8a7791_smp_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= shmobile_smp_cpu_disable,
	.cpu_die		= shmobile_smp_apmu_cpu_die,
	.cpu_kill		= shmobile_smp_apmu_cpu_kill,
#endif
};
