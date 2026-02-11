// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Setup code for SAMA7
 *
 * Copyright (C) 2021 Microchip Technology, Inc. and its subsidiaries
 *
 */

#include <asm/mach/arch.h>
#include <asm/system_misc.h>

#include "generic.h"

static const char *const sama7_dt_board_compat[] __initconst = {
	"microchip,sama7",
	NULL
};

DT_MACHINE_START(sama7_dt, "Microchip SAMA7")
	/* Maintainer: Microchip */
	.init_late	= sama7_pm_init,
	.dt_compat	= sama7_dt_board_compat,
MACHINE_END

