// SPDX-License-Identifier: GPL-2.0-only
/*
 * Abilis Systems TB10x platform initialisation
 *
 * Copyright (C) Abilis Systems 2012
 *
 * Author: Christian Ruppert <christian.ruppert@abilis.com>
 */

#include <linux/init.h>
#include <asm/mach_desc.h>

static const char *tb10x_compat[] __initdata = {
	"abilis,arc-tb10x",
	NULL,
};

MACHINE_START(TB10x, "tb10x")
	.dt_compat	= tb10x_compat,
MACHINE_END
