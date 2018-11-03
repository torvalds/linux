// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car M2-W/N System Controller
 *
 * Copyright (C) 2016 Glider bvba
 */

#include <linux/bug.h>
#include <linux/kernel.h>

#include <dt-bindings/power/r8a7791-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a7791_areas[] __initconst = {
	{ "always-on",	    0, 0, R8A7791_PD_ALWAYS_ON,	-1, PD_ALWAYS_ON },
	{ "ca15-scu",	0x180, 0, R8A7791_PD_CA15_SCU,	R8A7791_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca15-cpu0",	 0x40, 0, R8A7791_PD_CA15_CPU0,	R8A7791_PD_CA15_SCU,
	  PD_CPU_NOCR },
	{ "ca15-cpu1",	 0x40, 1, R8A7791_PD_CA15_CPU1,	R8A7791_PD_CA15_SCU,
	  PD_CPU_NOCR },
	{ "sh-4a",	 0x80, 0, R8A7791_PD_SH_4A,	R8A7791_PD_ALWAYS_ON },
	{ "sgx",	 0xc0, 0, R8A7791_PD_SGX,	R8A7791_PD_ALWAYS_ON },
};

const struct rcar_sysc_info r8a7791_sysc_info __initconst = {
	.areas = r8a7791_areas,
	.num_areas = ARRAY_SIZE(r8a7791_areas),
};
