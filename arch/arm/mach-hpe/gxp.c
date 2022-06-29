// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Hewlett-Packard Enterprise Development Company, L.P. */

#include <linux/of_platform.h>
#include <asm/mach/arch.h>

static const char * const gxp_board_dt_compat[] = {
	"hpe,gxp",
	NULL,
};

DT_MACHINE_START(GXP_DT, "HPE GXP")
	.dt_compat	= gxp_board_dt_compat,
	.l2c_aux_val = 0,
	.l2c_aux_mask = ~0,
MACHINE_END
