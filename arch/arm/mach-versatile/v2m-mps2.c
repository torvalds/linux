// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 ARM Limited
 *
 * Author: Vladimir Murzin <vladimir.murzin@arm.com>
 */

#include <asm/mach/arch.h>

static const char *const mps2_compat[] __initconst = {
	"arm,mps2",
	NULL
};

DT_MACHINE_START(MPS2DT, "MPS2 (Device Tree Support)")
	.dt_compat = mps2_compat,
MACHINE_END
