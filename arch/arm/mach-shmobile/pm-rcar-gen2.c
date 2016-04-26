/*
 * R-Car Generation 2 Power management support
 *
 * Copyright (C) 2013 - 2015  Renesas Electronics Corporation
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/soc/renesas/rcar-sysc.h>
#include <asm/io.h>
#include "common.h"
#include "rcar-gen2.h"

/* RST */
#define RST		0xe6160000
#define CA15BAR		0x0020
#define CA7BAR		0x0030
#define CA15RESCNT	0x0040
#define CA7RESCNT	0x0044

/* On-chip RAM */
#define MERAM		0xe8080000
#define RAM		0xe6300000

/* SYSC */
#define SYSCIER 0x0c
#define SYSCIMR 0x10

#if defined(CONFIG_SMP)

static void __init rcar_gen2_sysc_init(u32 syscier)
{
	void __iomem *base = rcar_sysc_init(0xe6180000);

	/* enable all interrupt sources, but do not use interrupt handler */
	iowrite32(syscier, base + SYSCIER);
	iowrite32(0, base + SYSCIMR);
}

#else /* CONFIG_SMP */

static inline void rcar_gen2_sysc_init(u32 syscier) {}

#endif /* CONFIG_SMP */

void __init rcar_gen2_pm_init(void)
{
	void __iomem *p;
	u32 bar;
	static int once;
	struct device_node *np, *cpus;
	bool has_a7 = false;
	bool has_a15 = false;
	phys_addr_t boot_vector_addr = 0;
	u32 syscier = 0;

	if (once++)
		return;

	cpus = of_find_node_by_path("/cpus");
	if (!cpus)
		return;

	for_each_child_of_node(cpus, np) {
		if (of_device_is_compatible(np, "arm,cortex-a15"))
			has_a15 = true;
		else if (of_device_is_compatible(np, "arm,cortex-a7"))
			has_a7 = true;
	}

	if (of_machine_is_compatible("renesas,r8a7790")) {
		boot_vector_addr = MERAM;
		syscier = 0x013111ef;

	} else if (of_machine_is_compatible("renesas,r8a7791")) {
		boot_vector_addr = RAM;
		syscier = 0x00111003;
	}

	/* RAM for jump stub, because BAR requires 256KB aligned address */
	p = ioremap_nocache(boot_vector_addr, shmobile_boot_size);
	memcpy_toio(p, shmobile_boot_vector, shmobile_boot_size);
	iounmap(p);

	/* setup reset vectors */
	p = ioremap_nocache(RST, 0x63);
	bar = (boot_vector_addr >> 8) & 0xfffffc00;
	if (has_a15) {
		writel_relaxed(bar, p + CA15BAR);
		writel_relaxed(bar | 0x10, p + CA15BAR);

		/* de-assert reset for CA15 CPUs */
		writel_relaxed((readl_relaxed(p + CA15RESCNT) & ~0x0f) |
				0xa5a50000, p + CA15RESCNT);
	}
	if (has_a7) {
		writel_relaxed(bar, p + CA7BAR);
		writel_relaxed(bar | 0x10, p + CA7BAR);

		/* de-assert reset for CA7 CPUs */
		writel_relaxed((readl_relaxed(p + CA7RESCNT) & ~0x0f) |
				0x5a5a0000, p + CA7RESCNT);
	}
	iounmap(p);

	rcar_gen2_sysc_init(syscier);
	shmobile_smp_apmu_suspend_init();
}
