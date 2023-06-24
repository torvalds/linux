// SPDX-License-Identifier: GPL-2.0
/*
 * Emma Mobile EV2 processor support
 *
 * Copyright (C) 2012  Magnus Damm
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "emev2.h"

static const char *const emev2_boards_compat_dt[] __initconst = {
	"renesas,emev2",
	NULL
};

DT_MACHINE_START(EMEV2_DT, "Generic Emma Mobile EV2 (Flattened Device Tree)")
	.smp		= smp_ops(emev2_smp_ops),
	.init_early	= shmobile_init_delay,
	.init_late	= shmobile_init_late,
	.dt_compat	= emev2_boards_compat_dt,
MACHINE_END
