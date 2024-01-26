// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Linaro Ltd.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#include <asm/mach/arch.h>

static const char *const realview_dt_platform_compat[] __initconst = {
	"arm,realview-eb",
	"arm,realview-pb1176",
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
