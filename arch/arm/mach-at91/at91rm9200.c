// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Setup code for AT91RM9200
 *
 *  Copyright (C) 2011 Atmel,
 *                2011 Nicolas Ferre <nicolas.ferre@atmel.com>
 *                2012 Joachim Eastwood <manabian@gmail.com>
 */

#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>

#include "generic.h"

static void __init at91rm9200_dt_device_init(void)
{
	of_platform_default_populate(NULL, NULL, NULL);

	at91rm9200_pm_init();
}

static const char *const at91rm9200_dt_board_compat[] __initconst = {
	"atmel,at91rm9200",
	NULL
};

DT_MACHINE_START(at91rm9200_dt, "Atmel AT91RM9200")
	.init_machine	= at91rm9200_dt_device_init,
	.dt_compat	= at91rm9200_dt_board_compat,
MACHINE_END
