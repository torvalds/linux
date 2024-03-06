// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2H System Controller
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 * Based on Renesas R-Car H3 System Controller
 * Copyright (C) 2016-2017 Glider bvba
 */

#include <linux/kernel.h>

#include <dt-bindings/power/r8a774e1-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a774e1_areas[] __initconst = {
	{ "always-on",	    0, 0, R8A774E1_PD_ALWAYS_ON, -1, PD_ALWAYS_ON },
	{ "ca57-scu",	0x1c0, 0, R8A774E1_PD_CA57_SCU, R8A774E1_PD_ALWAYS_ON, PD_SCU },
	{ "ca57-cpu0",	 0x80, 0, R8A774E1_PD_CA57_CPU0, R8A774E1_PD_CA57_SCU, PD_CPU_NOCR },
	{ "ca57-cpu1",	 0x80, 1, R8A774E1_PD_CA57_CPU1, R8A774E1_PD_CA57_SCU, PD_CPU_NOCR },
	{ "ca57-cpu2",	 0x80, 2, R8A774E1_PD_CA57_CPU2, R8A774E1_PD_CA57_SCU, PD_CPU_NOCR },
	{ "ca57-cpu3",	 0x80, 3, R8A774E1_PD_CA57_CPU3, R8A774E1_PD_CA57_SCU, PD_CPU_NOCR },
	{ "ca53-scu",	0x140, 0, R8A774E1_PD_CA53_SCU, R8A774E1_PD_ALWAYS_ON, PD_SCU },
	{ "ca53-cpu0",	0x200, 0, R8A774E1_PD_CA53_CPU0, R8A774E1_PD_CA53_SCU, PD_CPU_NOCR },
	{ "ca53-cpu1",	0x200, 1, R8A774E1_PD_CA53_CPU1, R8A774E1_PD_CA53_SCU, PD_CPU_NOCR },
	{ "ca53-cpu2",	0x200, 2, R8A774E1_PD_CA53_CPU2, R8A774E1_PD_CA53_SCU, PD_CPU_NOCR },
	{ "ca53-cpu3",	0x200, 3, R8A774E1_PD_CA53_CPU3, R8A774E1_PD_CA53_SCU, PD_CPU_NOCR },
	{ "a3vp",	0x340, 0, R8A774E1_PD_A3VP, R8A774E1_PD_ALWAYS_ON },
	{ "a3vc",	0x380, 0, R8A774E1_PD_A3VC, R8A774E1_PD_ALWAYS_ON },
	{ "a2vc1",	0x3c0, 1, R8A774E1_PD_A2VC1, R8A774E1_PD_A3VC },
	{ "3dg-a",	0x100, 0, R8A774E1_PD_3DG_A, R8A774E1_PD_ALWAYS_ON },
	{ "3dg-b",	0x100, 1, R8A774E1_PD_3DG_B, R8A774E1_PD_3DG_A },
	{ "3dg-c",	0x100, 2, R8A774E1_PD_3DG_C, R8A774E1_PD_3DG_B },
	{ "3dg-d",	0x100, 3, R8A774E1_PD_3DG_D, R8A774E1_PD_3DG_C },
	{ "3dg-e",	0x100, 4, R8A774E1_PD_3DG_E, R8A774E1_PD_3DG_D },
};

const struct rcar_sysc_info r8a774e1_sysc_info __initconst = {
	.areas = r8a774e1_areas,
	.num_areas = ARRAY_SIZE(r8a774e1_areas),
	.extmask_offs = 0x2f8,
	.extmask_val = BIT(0),
};
