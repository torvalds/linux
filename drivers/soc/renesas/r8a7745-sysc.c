/*
 * Renesas RZ/G1E System Controller
 *
 * Copyright (C) 2016 Cogent Embedded Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; of the License.
 */

#include <linux/bug.h>
#include <linux/kernel.h>

#include <dt-bindings/power/r8a7745-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a7745_areas[] __initconst = {
	{ "always-on",	    0, 0, R8A7745_PD_ALWAYS_ON,	-1, PD_ALWAYS_ON },
	{ "ca7-scu",	0x100, 0, R8A7745_PD_CA7_SCU,	R8A7745_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca7-cpu0",	0x1c0, 0, R8A7745_PD_CA7_CPU0,	R8A7745_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "ca7-cpu1",	0x1c0, 1, R8A7745_PD_CA7_CPU1,	R8A7745_PD_CA7_SCU,
	  PD_CPU_NOCR },
	{ "sgx",	 0xc0, 0, R8A7745_PD_SGX,	R8A7745_PD_ALWAYS_ON },
};

const struct rcar_sysc_info r8a7745_sysc_info __initconst = {
	.areas = r8a7745_areas,
	.num_areas = ARRAY_SIZE(r8a7745_areas),
};
