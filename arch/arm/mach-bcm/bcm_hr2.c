// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2017 Broadcom

#include <asm/mach/arch.h>

static const char * const bcm_hr2_dt_compat[] __initconst = {
	"brcm,hr2",
	NULL,
};

DT_MACHINE_START(BCM_HR2_DT, "Broadcom Hurricane 2 SoC")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.dt_compat = bcm_hr2_dt_compat,
MACHINE_END
