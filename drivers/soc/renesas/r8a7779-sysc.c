// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car H1 System Controller
 *
 * Copyright (C) 2016 Glider bvba
 */

#include <linux/kernel.h>

#include <dt-bindings/power/r8a7779-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a7779_areas[] __initconst = {
	{ "always-on",	    0, 0, R8A7779_PD_ALWAYS_ON,	-1, PD_ALWAYS_ON },
	{ "arm1",	 0x40, 1, R8A7779_PD_ARM1,	R8A7779_PD_ALWAYS_ON,
	  PD_CPU_CR },
	{ "arm2",	 0x40, 2, R8A7779_PD_ARM2,	R8A7779_PD_ALWAYS_ON,
	  PD_CPU_CR },
	{ "arm3",	 0x40, 3, R8A7779_PD_ARM3,	R8A7779_PD_ALWAYS_ON,
	  PD_CPU_CR },
	{ "sgx",	 0xc0, 0, R8A7779_PD_SGX,	R8A7779_PD_ALWAYS_ON },
	{ "vdp",	0x100, 0, R8A7779_PD_VDP,	R8A7779_PD_ALWAYS_ON },
	{ "imp",	0x140, 0, R8A7779_PD_IMP,	R8A7779_PD_ALWAYS_ON },
};

const struct rcar_sysc_info r8a7779_sysc_info __initconst = {
	.areas = r8a7779_areas,
	.num_areas = ARRAY_SIZE(r8a7779_areas),
};
