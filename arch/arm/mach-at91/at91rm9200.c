/*
 *  Setup code for AT91RM9200
 *
 *  Copyright (C) 2011 Atmel,
 *                2011 Nicolas Ferre <nicolas.ferre@atmel.com>
 *                2012 Joachim Eastwood <manabian@gmail.com>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>

#include "generic.h"
#include "soc.h"

static const struct at91_soc rm9200_socs[] = {
	AT91_SOC(AT91RM9200_CIDR_MATCH, 0, "at91rm9200 BGA", "at91rm9200"),
	{ /* sentinel */ },
};

static void __init at91rm9200_dt_device_init(void)
{
	struct soc_device *soc;
	struct device *soc_dev = NULL;

	soc = at91_soc_init(rm9200_socs);
	if (soc != NULL)
		soc_dev = soc_device_to_device(soc);

	of_platform_populate(NULL, of_default_bus_match_table, NULL, soc_dev);

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
