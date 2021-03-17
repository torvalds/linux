// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Machine declaration for Alpine platforms.
 *
 * Copyright (C) 2015 Annapurna Labs Ltd.
 */

#include <linux/of_platform.h>

#include <asm/mach/arch.h>

static const char * const al_match[] __initconst = {
	"al,alpine",
	NULL,
};

DT_MACHINE_START(AL_DT, "Annapurna Labs Alpine")
	.dt_compat	= al_match,
MACHINE_END
