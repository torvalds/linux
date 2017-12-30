/*
 * Renesas R-Car D3 System Controller
 *
 * Copyright (C) 2017 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/sys_soc.h>

#include <dt-bindings/power/r8a77995-sysc.h>

#include "rcar-sysc.h"

static struct rcar_sysc_area r8a77995_areas[] __initdata = {
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
