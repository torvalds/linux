/*
 *  Setup code for AT91RM9200
 *
 *  Copyright (C) 2011 Atmel,
 *                2011 Nicolas Ferre <nicolas.ferre@atmel.com>
 *                2012 Joachim Eastwood <manabian@gmail.com>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/system_misc.h>

#include <mach/at91_st.h>

#include "generic.h"

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
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);

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
	.map_io		= at91_map_io,
	.init_machine	= at91rm9200_dt_device_init,
	.dt_compat	= at91rm9200_dt_board_compat,
MACHINE_END
