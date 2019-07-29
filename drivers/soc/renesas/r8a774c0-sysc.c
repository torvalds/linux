// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2E System Controller
 * Copyright (C) 2018 Renesas Electronics Corp.
 *
 * Based on Renesas R-Car E3 System Controller
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/sys_soc.h>

#include <dt-bindings/power/r8a774c0-sysc.h>

#include "rcar-sysc.h"

static struct rcar_sysc_area r8a774c0_areas[] __initdata = {
	{ "always-on",	    0, 0, R8A774C0_PD_ALWAYS_ON, -1, PD_ALWAYS_ON },
	{ "ca53-scu",	0x140, 0, R8A774C0_PD_CA53_SCU,  R8A774C0_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca53-cpu0",	0x200, 0, R8A774C0_PD_CA53_CPU0, R8A774C0_PD_CA53_SCU,
	  PD_CPU_NOCR },
	{ "ca53-cpu1",	0x200, 1, R8A774C0_PD_CA53_CPU1, R8A774C0_PD_CA53_SCU,
	  PD_CPU_NOCR },
	{ "a3vc",	0x380, 0, R8A774C0_PD_A3VC,	R8A774C0_PD_ALWAYS_ON },
	{ "a2vc1",	0x3c0, 1, R8A774C0_PD_A2VC1,	R8A774C0_PD_A3VC },
	{ "3dg-a",	0x100, 0, R8A774C0_PD_3DG_A,	R8A774C0_PD_ALWAYS_ON },
	{ "3dg-b",	0x100, 1, R8A774C0_PD_3DG_B,	R8A774C0_PD_3DG_A },
};

static void __init rcar_sysc_fix_parent(struct rcar_sysc_area *areas,
					unsigned int num_areas, u8 id,
					int new_parent)
{
	unsigned int i;

	for (i = 0; i < num_areas; i++)
		if (areas[i].isr_bit == id) {
			areas[i].parent = new_parent;
			return;
		}
}

/* Fixups for RZ/G2E ES1.0 revision */
static const struct soc_device_attribute r8a774c0[] __initconst = {
	{ .soc_id = "r8a774c0", .revision = "ES1.0" },
	{ /* sentinel */ }
};

static int __init r8a774c0_sysc_init(void)
{
	if (soc_device_match(r8a774c0)) {
		rcar_sysc_fix_parent(r8a774c0_areas,
				     ARRAY_SIZE(r8a774c0_areas),
				     R8A774C0_PD_3DG_A, R8A774C0_PD_3DG_B);
		rcar_sysc_fix_parent(r8a774c0_areas,
				     ARRAY_SIZE(r8a774c0_areas),
				     R8A774C0_PD_3DG_B, R8A774C0_PD_ALWAYS_ON);
	}

	return 0;
}

const struct rcar_sysc_info r8a774c0_sysc_info __initconst = {
	.init = r8a774c0_sysc_init,
	.areas = r8a774c0_areas,
	.num_areas = ARRAY_SIZE(r8a774c0_areas),
};
