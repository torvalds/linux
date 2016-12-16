/*
 * Renesas R-Car H2 System Controller
 *
 * Copyright (C) 2016 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/bug.h>
#include <linux/kernel.h>

#include <dt-bindings/power/r8a7790-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a7790_areas[] __initconst = {
	{ "always-on",	    0, 0, R8A7790_PD_ALWAYS_ON,	-1, PD_ALWAYS_ON },
	{ "ca15-scu",	0x180, 0, R8A7790_PD_CA15_SCU,	R8A7790_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca15-cpu0",	 0x40, 0, R8A7790_PD_CA15_CPU0,	R8A7790_PD_CA15_SCU,
	  PD_CPU_NOCR },
	{ "ca15-cpu1",	 0x40, 1, R8A7790_PD_CA15_CPU1,	R8A7790_PD_CA15_SCU,
	  PD_CPU_NOCR },
	{ "ca15-cpu2",	 0x40, 2, R8A7790_PD_CA15_CPU2,	R8A7790_PD_CA15_SCU,
	  PD_CPU_NOCR },
	{ "ca15-cpu3",	 0x40, 3, R8A7790_PD_CA15_CPU3,	R8A7790_PD_CA15_SCU,
	  PD_CPU_NOCR },
	{ "ca7-scu",	0x100, 0, R8A7790_PD_CA7_SCU,	R8A7790_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca7-cpu0",	0x1c0, 0, R8A7790_PD_CA7_CPU0,	R8A7790_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "ca7-cpu1",	0x1c0, 1, R8A7790_PD_CA7_CPU1,	R8A7790_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "ca7-cpu2",	0x1c0, 2, R8A7790_PD_CA7_CPU2,	R8A7790_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "ca7-cpu3",	0x1c0, 3, R8A7790_PD_CA7_CPU3,	R8A7790_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "sh-4a",	 0x80, 0, R8A7790_PD_SH_4A,	R8A7790_PD_ALWAYS_ON },
	{ "rgx",	 0xc0, 0, R8A7790_PD_RGX,	R8A7790_PD_ALWAYS_ON },
	{ "imp",	0x140, 0, R8A7790_PD_IMP,	R8A7790_PD_ALWAYS_ON },
};

const struct rcar_sysc_info r8a7790_sysc_info __initconst = {
	.areas = r8a7790_areas,
	.num_areas = ARRAY_SIZE(r8a7790_areas),
};
