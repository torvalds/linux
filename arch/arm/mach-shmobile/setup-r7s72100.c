// SPDX-License-Identifier: GPL-2.0
/*
 * r7s72100 processor support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 */

#include <linux/kernel.h>

#include <asm/mach/arch.h>

#include "common.h"

static const char *const r7s72100_boards_compat_dt[] __initconst = {
	"renesas,r7s72100",
	NULL,
};

DT_MACHINE_START(R7S72100_DT, "Generic R7S72100 (Flattened Device Tree)")
	.l2c_aux_val    = 0,
	.l2c_aux_mask   = ~0,
	.init_early	= shmobile_init_delay,
	.init_late	= shmobile_init_late,
	.dt_compat	= r7s72100_boards_compat_dt,
MACHINE_END
