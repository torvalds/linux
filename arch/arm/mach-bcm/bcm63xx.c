// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2014 Broadcom Corporation

#include <linux/of_platform.h>

#include <asm/mach/arch.h>

static const char * const bcm63xx_dt_compat[] = {
	"brcm,bcm63138",
	NULL
};

DT_MACHINE_START(BCM63XXX_DT, "BCM63xx DSL SoC")
	.dt_compat	= bcm63xx_dt_compat,
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
MACHINE_END
