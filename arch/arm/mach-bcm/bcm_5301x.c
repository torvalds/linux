/*
 * Broadcom BCM470X / BCM5301X ARM platform code.
 *
 * Copyright 2013 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */
#include <linux/of_platform.h>
#include <asm/hardware/cache-l2x0.h>

#include <asm/mach/arch.h>

static const char *const bcm5301x_dt_compat[] __initconst = {
	"brcm,bcm4708",
	NULL,
};

DT_MACHINE_START(BCM5301X, "BCM5301X")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.dt_compat	= bcm5301x_dt_compat,
MACHINE_END
