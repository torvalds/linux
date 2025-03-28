// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car Generation 2 support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 * Copyright (C) 2014  Ulrich Hecht
 */

#include <linux/clocksource.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/psci.h>
#include <asm/mach/arch.h>
#include <asm/secure_cntvoff.h>
#include "common.h"

static const struct of_device_id cpg_matches[] __initconst = {
	{ .compatible = "renesas,r8a7742-cpg-mssr", .data = "extal" },
	{ .compatible = "renesas,r8a7743-cpg-mssr", .data = "extal" },
	{ .compatible = "renesas,r8a7744-cpg-mssr", .data = "extal" },
	{ .compatible = "renesas,r8a7790-cpg-mssr", .data = "extal" },
	{ .compatible = "renesas,r8a7791-cpg-mssr", .data = "extal" },
	{ .compatible = "renesas,r8a7793-cpg-mssr", .data = "extal" },
	{ /* sentinel */ }
};

static unsigned int __init get_extal_freq(void)
{
	const struct of_device_id *match;
	struct device_node *cpg, *extal;
	u32 freq = 20000000;
	int idx = 0;

	cpg = of_find_matching_node_and_match(NULL, cpg_matches, &match);
	if (!cpg)
		return freq;

	if (match->data)
		idx = of_property_match_string(cpg, "clock-names", match->data);
	extal = of_parse_phandle(cpg, "clocks", idx);
	of_node_put(cpg);
	if (!extal)
		return freq;

	of_property_read_u32(extal, "clock-frequency", &freq);
	of_node_put(extal);
	return freq;
}

#define CNTCR 0
#define CNTFID0 0x20

static void __init rcar_gen2_timer_init(void)
{
	bool need_update = true;
	void __iomem *base;
	u32 freq;

	/*
	 * If PSCI is available then most likely we are running on PSCI-enabled
	 * U-Boot which, we assume, has already taken care of resetting CNTVOFF
	 * and updating counter module before switching to non-secure mode
	 * and we don't need to.
	 */
#ifdef CONFIG_ARM_PSCI_FW
	if (psci_ops.cpu_on)
		need_update = false;
#endif

	if (need_update == false)
		goto skip_update;

	secure_cntvoff_init();

	if (of_machine_is_compatible("renesas,r8a7745") ||
	    of_machine_is_compatible("renesas,r8a77470") ||
	    of_machine_is_compatible("renesas,r8a7792") ||
	    of_machine_is_compatible("renesas,r8a7794")) {
		freq = 260000000 / 8;	/* ZS / 8 */
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

skip_update:
	of_clk_init(NULL);
	timer_probe();
}

static const char * const rcar_gen2_boards_compat_dt[] __initconst = {
	"renesas,r8a7790",
	"renesas,r8a7791",
	"renesas,r8a7792",
	"renesas,r8a7793",
	"renesas,r8a7794",
	NULL
};

DT_MACHINE_START(RCAR_GEN2_DT, "Generic R-Car Gen2 (Flattened Device Tree)")
	.init_late	= shmobile_init_late,
	.init_time	= rcar_gen2_timer_init,
	.dt_compat	= rcar_gen2_boards_compat_dt,
MACHINE_END

static const char * const rz_g1_boards_compat_dt[] __initconst = {
	"renesas,r8a7742",
	"renesas,r8a7743",
	"renesas,r8a7744",
	"renesas,r8a7745",
	"renesas,r8a77470",
	NULL
};

DT_MACHINE_START(RZ_G1_DT, "Generic RZ/G1 (Flattened Device Tree)")
	.init_late	= shmobile_init_late,
	.init_time	= rcar_gen2_timer_init,
	.dt_compat	= rz_g1_boards_compat_dt,
MACHINE_END
