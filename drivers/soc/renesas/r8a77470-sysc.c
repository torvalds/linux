// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G1C System Controller
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 */

#include <linux/kernel.h>

#include <dt-bindings/power/r8a77470-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a77470_areas[] __initconst = {
	{ "always-on",	    0, 0, R8A77470_PD_ALWAYS_ON, -1, PD_ALWAYS_ON },
	{ "ca7-scu",	0x100, 0, R8A77470_PD_CA7_SCU,	R8A77470_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca7-cpu0",	0x1c0, 0, R8A77470_PD_CA7_CPU0,	R8A77470_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "ca7-cpu1",	0x1c0, 1, R8A77470_PD_CA7_CPU1,	R8A77470_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "sgx",	 0xc0, 0, R8A77470_PD_SGX, R8A77470_PD_ALWAYS_ON },
};

const struct rcar_sysc_info r8a77470_sysc_info __initconst = {
	.areas = r8a77470_areas,
	.num_areas = ARRAY_SIZE(r8a77470_areas),
};
