/*
 * r8a73a4 processor support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
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

static const char *r8a73a4_boards_compat_dt[] __initdata = {
	"renesas,r8a73a4",
	NULL,
};

DT_MACHINE_START(R8A73A4_DT, "Generic R8A73A4 (Flattened Device Tree)")
	.init_early	= shmobile_init_delay,
	.init_late	= shmobile_init_late,
	.dt_compat	= r8a73a4_boards_compat_dt,
MACHINE_END
