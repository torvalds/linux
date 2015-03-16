/*
 *  Setup code for AT91RM9200
 *
 *  Copyright (C) 2011 Atmel,
 *                2011 Nicolas Ferre <nicolas.ferre@atmel.com>
 *                2012 Joachim Eastwood <manabian@gmail.com>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/system_misc.h>

#include <mach/at91_st.h>

#include "generic.h"
#include "soc.h"

static const struct at91_soc rm9200_socs[] = {
	AT91_SOC(AT91RM9200_CIDR_MATCH, 0, "at91rm9200 BGA", "at91rm9200"),
	{ /* sentinel */ },
};

static void at91rm9200_restart(enum reboot_mode reboot_mode, const char *cmd)
{
	/*
	 * Perform a hardware reset with the use of the Watchdog timer.
	 */
	at91_st_write(AT91_ST_WDMR, AT91_ST_RSTEN | AT91_ST_EXTEN | 1);
	at91_st_write(AT91_ST_CR, AT91_ST_WDRST);
}

static void __init at91rm9200_dt_timer_init(void)
{
	of_clk_init(NULL);
	at91rm9200_timer_init();
}

static void __init at91rm9200_dt_device_init(void)
{
	struct soc_device *soc;
	struct device *soc_dev = NULL;

	soc = at91_soc_init(rm9200_socs);
	if (soc != NULL)
		soc_dev = soc_device_to_device(soc);

	of_platform_populate(NULL, of_default_bus_match_table, NULL, soc_dev);

	arm_pm_idle = at91rm9200_idle;
	arm_pm_restart = at91rm9200_restart;
	at91rm9200_pm_init();
}

static const char *at91rm9200_dt_board_compat[] __initconst = {
	"atmel,at91rm9200",
	NULL
};

DT_MACHINE_START(at91rm9200_dt, "Atmel AT91RM9200")
	.init_time      = at91rm9200_dt_timer_init,
	.init_machine	= at91rm9200_dt_device_init,
	.dt_compat	= at91rm9200_dt_board_compat,
MACHINE_END
