// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car D3 System Controller
 *
 * Copyright (C) 2017 Glider bvba
 */

#include <linux/kernel.h>

#include <dt-bindings/power/r8a77995-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a77995_areas[] __initconst = {
	{ "always-on",     0, 0, R8A77995_PD_ALWAYS_ON, -1, PD_ALWAYS_ON },
	{ "ca53-scu",  0x140, 0, R8A77995_PD_CA53_SCU,  R8A77995_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca53-cpu0", 0x200, 0, R8A77995_PD_CA53_CPU0, R8A77995_PD_CA53_SCU,
	  PD_CPU_NOCR },
};


const struct rcar_sysc_info r8a77995_sysc_info __initconst = {
	.areas = r8a77995_areas,
	.num_areas = ARRAY_SIZE(r8a77995_areas),
};
