/*
 * Renesas R-Car H3 System Controller
 *
 * Copyright (C) 2016-2017 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/sys_soc.h>

#include <dt-bindings/power/r8a7795-sysc.h>

#include "rcar-sysc.h"

static struct rcar_sysc_area r8a7795_areas[] __initdata = {
	{ "always-on",	    0, 0, R8A7795_PD_ALWAYS_ON,	-1, PD_ALWAYS_ON },
	{ "ca57-scu",	0x1c0, 0, R8A7795_PD_CA57_SCU,	R8A7795_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca57-cpu0",	 0x80, 0, R8A7795_PD_CA57_CPU0,	R8A7795_PD_CA57_SCU,
	  PD_CPU_NOCR },
	{ "ca57-cpu1",	 0x80, 1, R8A7795_PD_CA57_CPU1,	R8A7795_PD_CA57_SCU,
	  PD_CPU_NOCR },
	{ "ca57-cpu2",	 0x80, 2, R8A7795_PD_CA57_CPU2,	R8A7795_PD_CA57_SCU,
	  PD_CPU_NOCR },
	{ "ca57-cpu3",	 0x80, 3, R8A7795_PD_CA57_CPU3,	R8A7795_PD_CA57_SCU,
	  PD_CPU_NOCR },
	{ "ca53-scu",	0x140, 0, R8A7795_PD_CA53_SCU,	R8A7795_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca53-cpu0",	0x200, 0, R8A7795_PD_CA53_CPU0,	R8A7795_PD_CA53_SCU,
	  PD_CPU_NOCR },
	{ "ca53-cpu1",	0x200, 1, R8A7795_PD_CA53_CPU1,	R8A7795_PD_CA53_SCU,
	  PD_CPU_NOCR },
	{ "ca53-cpu2",	0x200, 2, R8A7795_PD_CA53_CPU2,	R8A7795_PD_CA53_SCU,
	  PD_CPU_NOCR },
	{ "ca53-cpu3",	0x200, 3, R8A7795_PD_CA53_CPU3,	R8A7795_PD_CA53_SCU,
	  PD_CPU_NOCR },
	{ "a3vp",	0x340, 0, R8A7795_PD_A3VP,	R8A7795_PD_ALWAYS_ON },
	{ "cr7",	0x240, 0, R8A7795_PD_CR7,	R8A7795_PD_ALWAYS_ON },
	{ "a3vc",	0x380, 0, R8A7795_PD_A3VC,	R8A7795_PD_ALWAYS_ON },
	/* A2VC0 exists on ES1.x only */
	{ "a2vc0",	0x3c0, 0, R8A7795_PD_A2VC0,	R8A7795_PD_A3VC },
	{ "a2vc1",	0x3c0, 1, R8A7795_PD_A2VC1,	R8A7795_PD_A3VC },
	{ "3dg-a",	0x100, 0, R8A7795_PD_3DG_A,	R8A7795_PD_ALWAYS_ON },
	{ "3dg-b",	0x100, 1, R8A7795_PD_3DG_B,	R8A7795_PD_3DG_A },
	{ "3dg-c",	0x100, 2, R8A7795_PD_3DG_C,	R8A7795_PD_3DG_B },
	{ "3dg-d",	0x100, 3, R8A7795_PD_3DG_D,	R8A7795_PD_3DG_C },
	{ "3dg-e",	0x100, 4, R8A7795_PD_3DG_E,	R8A7795_PD_3DG_D },
	{ "a3ir",	0x180, 0, R8A7795_PD_A3IR,	R8A7795_PD_ALWAYS_ON },
};


	/*
	 * Fixups for R-Car H3 revisions after ES1.x
	 */

static const struct soc_device_attribute r8a7795es1[] __initconst = {
	{ .soc_id = "r8a7795", .revision = "ES1.*" },
	{ /* sentinel */ }
};

static int __init r8a7795_sysc_init(void)
{
	if (!soc_device_match(r8a7795es1))
		rcar_sysc_nullify(r8a7795_areas, ARRAY_SIZE(r8a7795_areas),
				  R8A7795_PD_A2VC0);

	return 0;
}

const struct rcar_sysc_info r8a7795_sysc_info __initconst = {
	.init = r8a7795_sysc_init,
	.areas = r8a7795_areas,
	.num_areas = ARRAY_SIZE(r8a7795_areas),
};
