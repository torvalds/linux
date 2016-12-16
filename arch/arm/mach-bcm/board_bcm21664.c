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

#include <asm/mach/arch.h>

#include "kona_l2_cache.h"

static void __init bcm21664_init(void)
{
	kona_l2_cache_init();
}

static const char * const bcm21664_dt_compat[] = {
	"brcm,bcm21664",
	NULL,
};

DT_MACHINE_START(BCM21664_DT, "BCM21664 Broadcom Application Processor")
	.init_machine = bcm21664_init,
	.dt_compat = bcm21664_dt_compat,
MACHINE_END
