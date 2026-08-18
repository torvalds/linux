// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Stefan Dösinger
 */

#include <asm/mach/arch.h>
#include <linux/init.h>

static const char *const zx297520v3_dt_compat[] __initconst = {
	"zte,zx297520v3",
	NULL,
};

DT_MACHINE_START(ZX, "ZTE zx297520v3 (Device Tree)")
	.dt_compat	= zx297520v3_dt_compat,
MACHINE_END
