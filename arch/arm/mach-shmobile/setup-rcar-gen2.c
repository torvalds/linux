/*
 * R-Car Generation 2 support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 * Copyright (C) 2014  Ulrich Hecht
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

#include <linux/clk/renesas.h>
#include <linux/clocksource.h>
#include <linux/device.h>
#include <linux/dma-contiguous.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <asm/mach/arch.h>
#include "common.h"
#include "rcar-gen2.h"

#define MODEMR 0xe6160060

u32 rcar_gen2_read_mode_pins(void)
{
	static u32 mode;
	static bool mode_valid;

	if (!mode_valid) {
		void __iomem *modemr = ioremap_nocache(MODEMR, 4);
		BUG_ON(!modemr);
		mode = ioread32(modemr);
		iounmap(modemr);
		mode_valid = true;
	}

	return mode;
}

static unsigned int __init get_extal_freq(void)
{
	struct device_node *cpg, *extal;
	u32 freq = 20000000;

	cpg = of_find_compatible_node(NULL, NULL,
				      "renesas,rcar-gen2-cpg-clocks");
	if (!cpg)
		return freq;

	extal = of_parse_phandle(cpg, "clocks", 0);
	of_node_put(cpg);
	if (!extal)
		return freq;

	of_property_read_u32(extal, "clock-frequency", &freq);
	of_node_put(extal);
	return freq;
}

#define CNTCR 0
#define CNTFID0 0x20

void __init rcar_gen2_timer_init(void)
{
	u32 mode = rcar_gen2_read_mode_pins();
#ifdef CONFIG_ARM_ARCH_TIMER
	void __iomem *base;
	u32 freq;

	if (of_machine_is_compatible("renesas,r8a7792") ||
	    of_machine_is_compatible("renesas,r8a7794")) {
		freq = 260000000 / 8;	/* ZS / 8 */
		/* CNTVOFF has to be initialized either from non-secure
		 * Hypervisor mode or secure Monitor mode with SCR.NS==1.
		 * If TrustZone is enabled then it should be handled by the
		 * secure code.
		 */
		asm volatile(
		"	cps	0x16\n"
		"	mrc	p15, 0, r1, c1, c1, 0\n"
		"	orr	r0, r1, #1\n"
		"	mcr	p15, 0, r0, c1, c1, 0\n"
		"	isb\n"
		"	mov	r0, #0\n"
		"	mcrr	p15, 4, r0, r0, c14\n"
		"	isb\n"
		"	mcr	p15, 0, r1, c1, c1, 0\n"
		"	isb\n"
		"	cps	0x13\n"
			: : : "r0", "r1");
	} else {
		/* At Linux boot time the r8a7790 arch timer comes up
		 * with the counter disabled. Moreover, it may also report
		 * a potentially incorrect fixed 13 MHz frequency. To be
		 * correct these registers need to be updated to use the
		 * frequency EXTAL / 2.
		 */
		freq = get_extal_freq() / 2;
	}

	/* Remap "armgcnt address map" space */
	base = ioremap(0xe6080000, PAGE_SIZE);

	/*
	 * Update the timer if it is either not running, or is not at the
	 * right frequency. The timer is only configurable in secure mode
	 * so this avoids an abort if the loader started the timer and
	 * entered the kernel in non-secure mode.
	 */

	if ((ioread32(base + CNTCR) & 1) == 0 ||
	    ioread32(base + CNTFID0) != freq) {
		/* Update registers with correct frequency */
		iowrite32(freq, base + CNTFID0);
		asm volatile("mcr p15, 0, %0, c14, c0, 0" : : "r" (freq));

		/* make sure arch timer is started by setting bit 0 of CNTCR */
		iowrite32(1, base + CNTCR);
	}

	iounmap(base);
#endif /* CONFIG_ARM_ARCH_TIMER */

	rcar_gen2_clocks_init(mode);
	clocksource_probe();
}

struct memory_reserve_config {
	u64 reserved;
	u64 base, size;
};

static int __init rcar_gen2_scan_mem(unsigned long node, const char *uname,
				     int depth, void *data)
{
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const __be32 *reg, *endp;
	int l;
	struct memory_reserve_config *mrc = data;
	u64 lpae_start = 1ULL << 32;

	/* We are scanning "memory" nodes only */
	if (type == NULL || strcmp(type, "memory"))
		return 0;

	reg = of_get_flat_dt_prop(node, "linux,usable-memory", &l);
	if (reg == NULL)
		reg = of_get_flat_dt_prop(node, "reg", &l);
	if (reg == NULL)
		return 0;

	endp = reg + (l / sizeof(__be32));
	while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
		u64 base, size;

		base = dt_mem_next_cell(dt_root_addr_cells, &reg);
		size = dt_mem_next_cell(dt_root_size_cells, &reg);

		if (base >= lpae_start)
			continue;

		if ((base + size) >= lpae_start)
			size = lpae_start - base;

		if (size < mrc->reserved)
			continue;

		if (base < mrc->base)
			continue;

		/* keep the area at top near the 32-bit legacy limit */
		mrc->base = base + size - mrc->reserved;
		mrc->size = mrc->reserved;
	}

	return 0;
}

void __init rcar_gen2_reserve(void)
{
	struct memory_reserve_config mrc;

	/* reserve 256 MiB at the top of the physical legacy 32-bit space */
	memset(&mrc, 0, sizeof(mrc));
	mrc.reserved = SZ_256M;

	of_scan_flat_dt(rcar_gen2_scan_mem, &mrc);
#ifdef CONFIG_DMA_CMA
	if (mrc.size && memblock_is_region_memory(mrc.base, mrc.size)) {
		static struct cma *rcar_gen2_dma_contiguous;

		dma_contiguous_reserve_area(mrc.size, mrc.base, 0,
					    &rcar_gen2_dma_contiguous, true);
	}
#endif
}
