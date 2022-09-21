// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2015 Broadcom Corporation

#include <asm/mach/arch.h>

static const char *const bcm_nsp_dt_compat[] __initconst = {
	"brcm,nsp",
	NULL,
};

DT_MACHINE_START(NSP_DT, "Broadcom Northstar Plus SoC")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.dt_compat = bcm_nsp_dt_compat,
MACHINE_END
