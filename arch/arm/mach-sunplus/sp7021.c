// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * Copyright (C) Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */
#include <linux/kernel.h>
#include <asm/mach/arch.h>

static const char *sp7021_compat[] __initconst = {
	"sunplus,sp7021",
	NULL
};

DT_MACHINE_START(SP7021_DT, "SP7021")
	.dt_compat	= sp7021_compat,
MACHINE_END
