// SPDX-License-Identifier: GPL-2.0
// Copyright 2021 Jonathan Neuschäfer

#include <asm/mach/arch.h>

static const char *const wpcm450_dt_match[] = {
	"nuvoton,wpcm450",
	NULL
};

DT_MACHINE_START(WPCM450_DT, "WPCM450 chip")
	.dt_compat	= wpcm450_dt_match,
MACHINE_END
