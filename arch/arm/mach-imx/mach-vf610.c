// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
 */

#include <linux/of_platform.h>
#include <linux/irqchip.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>

static const char * const vf610_dt_compat[] __initconst = {
	"fsl,vf500",
	"fsl,vf510",
	"fsl,vf600",
	"fsl,vf610",
	"fsl,vf610m4",
	NULL,
};

DT_MACHINE_START(VYBRID_VF610, "Freescale Vybrid VF5xx/VF6xx (Device Tree)")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.dt_compat	= vf610_dt_compat,
MACHINE_END
