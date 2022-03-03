// SPDX-License-Identifier: GPL-2.0
/*
 * r8a73a4 processor support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 */

#include <linux/init.h>

#include <asm/mach/arch.h>

#include "common.h"

static const char *const r8a73a4_boards_compat_dt[] __initconst = {
	"renesas,r8a73a4",
	NULL
};

DT_MACHINE_START(R8A73A4_DT, "Generic R8A73A4 (Flattened Device Tree)")
	.init_late	= shmobile_init_late,
	.dt_compat	= r8a73a4_boards_compat_dt,
MACHINE_END
