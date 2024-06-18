// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2014 Broadcom Corporation

#include <asm/mach/arch.h>

static const char * const bcm_cygnus_dt_compat[] __initconst = {
	"brcm,cygnus",
	NULL,
};

DT_MACHINE_START(BCM_CYGNUS_DT, "Broadcom Cygnus SoC")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.dt_compat = bcm_cygnus_dt_compat,
MACHINE_END
