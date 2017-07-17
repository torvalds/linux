/*
 *  Setup code for SAMA5
 *
 *  Copyright (C) 2013 Atmel,
 *                2013 Ludovic Desroches <ludovic.desroches@atmel.com>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>

#include "generic.h"

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
	.dt_compat	= sama5d2_compat,
	.l2c_aux_mask	= ~0UL,
MACHINE_END
