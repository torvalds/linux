// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car E3 System Controller
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 */

#include <linux/bug.h>
#include <linux/kernel.h>

#include <dt-bindings/power/r8a77990-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a77990_areas[] __initconst = {
	{ "always-on",	    0, 0, R8A77990_PD_ALWAYS_ON, -1, PD_ALWAYS_ON },
	{ "ca53-scu",	0x140, 0, R8A77990_PD_CA53_SCU,  R8A77990_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca53-cpu0",	0x200, 0, R8A77990_PD_CA53_CPU0, R8A77990_PD_CA53_SCU,
	  PD_CPU_NOCR },
	{ "ca53-cpu1",	0x200, 1, R8A77990_PD_CA53_CPU1, R8A77990_PD_CA53_SCU,
	  PD_CPU_NOCR },
	{ "cr7",	0x240, 0, R8A77990_PD_CR7,	R8A77990_PD_ALWAYS_ON },
	{ "a3vc",	0x380, 0, R8A77990_PD_A3VC,	R8A77990_PD_ALWAYS_ON },
	{ "a2vc1",	0x3c0, 1, R8A77990_PD_A2VC1,	R8A77990_PD_A3VC },
	{ "3dg-a",	0x100, 0, R8A77990_PD_3DG_A,	R8A77990_PD_ALWAYS_ON },
	{ "3dg-b",	0x100, 1, R8A77990_PD_3DG_B,	R8A77990_PD_3DG_A },
};

const struct rcar_sysc_info r8a77990_sysc_info __initconst = {
	.areas = r8a77990_areas,
	.num_areas = ARRAY_SIZE(r8a77990_areas),
};
