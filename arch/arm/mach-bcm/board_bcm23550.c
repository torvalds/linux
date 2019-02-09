/*
 * Copyright (C) 2016 Broadcom
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

static const char * const bcm23550_dt_compat[] = {
	"brcm,bcm23550",
	NULL,
};

DT_MACHINE_START(BCM23550_DT, "BCM23550 Broadcom Application Processor")
	.dt_compat = bcm23550_dt_compat,
MACHINE_END
