// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Device Tree support for Airoha SoCs
 *
 * Copyright (c) 2022 Felix Fietkau <nbd@nbd.name>
 */
#include <asm/mach/arch.h>

static const char * const airoha_board_dt_compat[] = {
	"airoha,en7523",
	NULL,
};

DT_MACHINE_START(MEDIATEK_DT, "Airoha Cortex-A53 (Device Tree)")
	.dt_compat	= airoha_board_dt_compat,
MACHINE_END
