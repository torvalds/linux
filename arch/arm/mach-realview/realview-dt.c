/*
 * Copyright (C) 2014 Linaro Ltd.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>
#include "core.h"

static const char *realview_dt_platform_compat[] __initconst = {
	"arm,realview-eb",
	"arm,realview-pb1176",
	"arm,realview-pb11mp",
	"arm,realview-pba8",
	"arm,realview-pbx",
	NULL,
};

DT_MACHINE_START(REALVIEW_DT, "ARM RealView Machine (Device Tree Support)")
#ifdef CONFIG_ZONE_DMA
	.dma_zone_size	= SZ_256M,
#endif
	.dt_compat	= realview_dt_platform_compat,
	.l2c_aux_val = 0x0,
	.l2c_aux_mask = ~0x0,
MACHINE_END
