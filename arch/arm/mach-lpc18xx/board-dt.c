// SPDX-License-Identifier: GPL-2.0-only
/*
 * Device Tree board file for NXP LPC18xx/43xx
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 */

#include <asm/mach/arch.h>

static const char *const lpc18xx_43xx_compat[] __initconst = {
	"nxp,lpc1850",
	"nxp,lpc4350",
	"nxp,lpc4370",
	NULL
};

DT_MACHINE_START(LPC18XXDT, "NXP LPC18xx/43xx (Device Tree)")
	.dt_compat = lpc18xx_43xx_compat,
MACHINE_END
