// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Nuvoton Technology corporation.
// Copyright 2018 Google, Inc.

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/hardware/cache-l2x0.h>

static const char *const npcm7xx_dt_match[] = {
	"nuvoton,npcm750",
	NULL
};

DT_MACHINE_START(NPCM7XX_DT, "NPCM7XX Chip family")
	.atag_offset	= 0x100,
	.dt_compat	= npcm7xx_dt_match,
MACHINE_END
