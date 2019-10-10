// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car Generation 2 Power management support
 *
 * Copyright (C) 2013 - 2015  Renesas Electronics Corporation
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 */

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/smp.h>
#include <asm/io.h>
#include <asm/cputype.h>
#include "common.h"
#include "rcar-gen2.h"

/* RST */
#define RST		0xe6160000

#define CA15BAR		0x0020		/* CA15 Boot Address Register */
#define CA7BAR		0x0030		/* CA7 Boot Address Register */
#define CA15RESCNT	0x0040		/* CA15 Reset Control Register */
#define CA7RESCNT	0x0044		/* CA7 Reset Control Register */

/* SYS Boot Address Register */
#define SBAR_BAREN	BIT(4)		/* SBAR is valid */

/* Reset Control Registers */
#define CA15RESCNT_CODE	0xa5a50000
#define CA15RESCNT_CPUS	0xf		/* CPU0-3 */
#define CA7RESCNT_CODE	0x5a5a0000
#define CA7RESCNT_CPUS	0xf		/* CPU0-3 */

/* On-chip RAM */
#define ICRAM1		0xe63c0000	/* Inter Connect RAM1 (4 KiB) */

static inline u32 phys_to_sbar(phys_addr_t addr)
{
	return (addr >> 8) & 0xfffffc00;
}

void __init rcar_gen2_pm_init(void)
{
	void __iomem *p;
	u32 bar;
	static int once;
	struct device_node *np;
	bool has_a7 = false;
	bool has_a15 = false;
	struct resource res;
	int error;

	if (once++)
		return;

	for_each_of_cpu_node(np) {
		if (of_device_is_compatible(np, "arm,cortex-a15"))
			has_a15 = true;
		else if (of_device_is_compatible(np, "arm,cortex-a7"))
			has_a7 = true;
	}

	np = of_find_compatible_node(NULL, NULL, "renesas,smp-sram");
	if (!np) {
		/* No smp-sram in DT, fall back to hardcoded address */
		res = (struct resource)DEFINE_RES_MEM(ICRAM1,
						      shmobile_boot_size);
		goto map;
	}

	error = of_address_to_resource(np, 0, &res);
	of_node_put(np);
	if (error) {
		pr_err("Failed to get smp-sram address: %d\n", error);
		return;
	}

map:
	/* RAM for jump stub, because BAR requires 256KB aligned address */
	if (res.start & (256 * 1024 - 1) ||
	    resource_size(&res) < shmobile_boot_size) {
		pr_err("Invalid smp-sram region\n");
		return;
	}

	p = ioremap(res.start, resource_size(&res));
	if (!p)
		return;
	/*
	 * install the reset vector, use the largest version if we have enough
	 * memory available
	 */
	if (resource_size(&res) >= shmobile_boot_size_gen2) {
		shmobile_boot_cpu_gen2 = read_cpuid_mpidr();
		memcpy_toio(p, shmobile_boot_vector_gen2,
			    shmobile_boot_size_gen2);
	} else {
		memcpy_toio(p, shmobile_boot_vector, shmobile_boot_size);
	}
	iounmap(p);

	/* setup reset vectors */
	p = ioremap_nocache(RST, 0x63);
	bar = phys_to_sbar(res.start);
	if (has_a15) {
		writel_relaxed(bar, p + CA15BAR);
		writel_relaxed(bar | SBAR_BAREN, p + CA15BAR);

		/* de-assert reset for CA15 CPUs */
		writel_relaxed((readl_relaxed(p + CA15RESCNT) &
				~CA15RESCNT_CPUS) | CA15RESCNT_CODE,
			       p + CA15RESCNT);
	}
	if (has_a7) {
		writel_relaxed(bar, p + CA7BAR);
		writel_relaxed(bar | SBAR_BAREN, p + CA7BAR);

		/* de-assert reset for CA7 CPUs */
		writel_relaxed((readl_relaxed(p + CA7RESCNT) &
				~CA7RESCNT_CPUS) | CA7RESCNT_CODE,
			       p + CA7RESCNT);
	}
	iounmap(p);

	shmobile_smp_apmu_suspend_init();
}
