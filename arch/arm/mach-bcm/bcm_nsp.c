/*
 * Copyright (C) 2015 Broadcom Corporation
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
