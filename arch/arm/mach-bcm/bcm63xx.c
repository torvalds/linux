/*
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

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
