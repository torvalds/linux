/*
 * Copyright (C) 2015 ARM Limited
 *
 * Author: Vladimir Murzin <vladimir.murzin@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <asm/mach/arch.h>

static const char *const mps2_compat[] __initconst = {
	"arm,mps2",
	NULL
};

DT_MACHINE_START(MPS2DT, "MPS2 (Device Tree Support)")
	.dt_compat = mps2_compat,
MACHINE_END
