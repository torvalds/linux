// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G1H System Controller
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 */

#include <linux/kernel.h>

#include <dt-bindings/power/r8a7742-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a7742_areas[] __initconst = {
	{ "always-on",	    0, 0, R8A7742_PD_ALWAYS_ON,	-1, PD_ALWAYS_ON },
	{ "ca15-scu",	0x180, 0, R8A7742_PD_CA15_SCU,	R8A7742_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca15-cpu0",	 0x40, 0, R8A7742_PD_CA15_CPU0,	R8A7742_PD_CA15_SCU,
	  PD_CPU_NOCR },
	{ "ca15-cpu1",	 0x40, 1, R8A7742_PD_CA15_CPU1,	R8A7742_PD_CA15_SCU,
	  PD_CPU_NOCR },
	{ "ca15-cpu2",	 0x40, 2, R8A7742_PD_CA15_CPU2,	R8A7742_PD_CA15_SCU,
	  PD_CPU_NOCR },
	{ "ca15-cpu3",	 0x40, 3, R8A7742_PD_CA15_CPU3,	R8A7742_PD_CA15_SCU,
	  PD_CPU_NOCR },
	{ "ca7-scu",	0x100, 0, R8A7742_PD_CA7_SCU,	R8A7742_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca7-cpu0",	0x1c0, 0, R8A7742_PD_CA7_CPU0,	R8A7742_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "ca7-cpu1",	0x1c0, 1, R8A7742_PD_CA7_CPU1,	R8A7742_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "ca7-cpu2",	0x1c0, 2, R8A7742_PD_CA7_CPU2,	R8A7742_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "ca7-cpu3",	0x1c0, 3, R8A7742_PD_CA7_CPU3,	R8A7742_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "rgx",	 0xc0, 0, R8A7742_PD_RGX,	R8A7742_PD_ALWAYS_ON },
};

const struct rcar_sysc_info r8a7742_sysc_info __initconst = {
	.areas = r8a7742_areas,
	.num_areas = ARRAY_SIZE(r8a7742_areas),
};
