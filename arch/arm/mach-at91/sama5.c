// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Setup code for SAMA5
 *
 *  Copyright (C) 2013 Atmel,
 *                2013 Ludovic Desroches <ludovic.desroches@atmel.com>
 */

#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/outercache.h>
#include <asm/system_misc.h>

#include "generic.h"
#include "sam_secure.h"

static void sama5_l2c310_write_sec(unsigned long val, unsigned reg)
{
	/* OP-TEE configures the L2 cache and does not allow modifying it yet */
}

static void __init sama5_secure_cache_init(void)
{
	sam_secure_init();
	if (sam_linux_is_optee_available())
		outer_cache.write_sec = sama5_l2c310_write_sec;
}

static void __init sama5_dt_device_init(void)
{
	of_platform_default_populate(NULL, NULL, NULL);
	sama5_pm_init();
}

static const char *const sama5_dt_board_compat[] __initconst = {
	"atmel,sama5",
	NULL
};

DT_MACHINE_START(sama5_dt, "Atmel SAMA5")
	/* Maintainer: Atmel */
	.init_machine	= sama5_dt_device_init,
	.dt_compat	= sama5_dt_board_compat,
MACHINE_END

static const char *const sama5_alt_dt_board_compat[] __initconst = {
	"atmel,sama5d4",
	NULL
};

DT_MACHINE_START(sama5_alt_dt, "Atmel SAMA5")
	/* Maintainer: Atmel */
	.init_machine	= sama5_dt_device_init,
	.dt_compat	= sama5_alt_dt_board_compat,
	.l2c_aux_mask	= ~0UL,
MACHINE_END

static void __init sama5d2_init(void)
{
	of_platform_default_populate(NULL, NULL, NULL);
	sama5d2_pm_init();
}

static const char *const sama5d2_compat[] __initconst = {
	"atmel,sama5d2",
	NULL
};

DT_MACHINE_START(sama5d2, "Atmel SAMA5")
	/* Maintainer: Atmel */
	.init_machine	= sama5d2_init,
	.init_early	= sama5_secure_cache_init,
	.dt_compat	= sama5d2_compat,
	.l2c_aux_mask	= ~0UL,
MACHINE_END
