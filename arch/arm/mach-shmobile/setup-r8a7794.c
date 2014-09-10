/*
 * r8a7794 processor support
 *
 * Copyright (C) 2014  Renesas Electronics Corporation
 * Copyright (C) 2014  Ulrich Hecht
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

#include <linux/of_platform.h>
#include "common.h"
#include "rcar-gen2.h"
#include <asm/mach/arch.h>

static const char * const r8a7794_boards_compat_dt[] __initconst = {
	"renesas,r8a7794",
	NULL,
};

DT_MACHINE_START(R8A7794_DT, "Generic R8A7794 (Flattened Device Tree)")
	.init_early	= shmobile_init_delay,
	.init_late	= shmobile_init_late,
	.init_time	= rcar_gen2_timer_init,
	.dt_compat	= r8a7794_boards_compat_dt,
MACHINE_END
