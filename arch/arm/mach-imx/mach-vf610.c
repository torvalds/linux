/*
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/of_platform.h>
#include <linux/irqchip.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>

#include "common.h"

static void __init vf610_init_machine(void)
{
	mxc_arch_reset_init_dt();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *vf610_dt_compat[] __initconst = {
	"fsl,vf610",
	NULL,
};

DT_MACHINE_START(VYBRID_VF610, "Freescale Vybrid VF610 (Device Tree)")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.init_machine   = vf610_init_machine,
	.dt_compat	= vf610_dt_compat,
	.restart	= mxc_restart,
MACHINE_END
