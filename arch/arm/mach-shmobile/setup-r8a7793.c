/*
 * r8a7793 processor support
 *
 * Copyright (C) 2015  Ulrich Hecht
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <asm/mach/arch.h>

#include "common.h"
#include "rcar-gen2.h"

static const char *r8a7793_boards_compat_dt[] __initconst = {
	"renesas,r8a7793",
	NULL,
};

DT_MACHINE_START(R8A7793_DT, "Generic R8A7793 (Flattened Device Tree)")
	.init_early	= shmobile_init_delay,
	.init_time	= rcar_gen2_timer_init,
	.init_late	= shmobile_init_late,
	.reserve	= rcar_gen2_reserve,
	.dt_compat	= r8a7793_boards_compat_dt,
MACHINE_END
