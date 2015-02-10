/*
 * arch/arm/mach-at91/at91rm9200.c
 *
 *  Copyright (C) 2005 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/clk/at91_pmc.h>

#include <asm/mach/map.h>
#include <asm/system_misc.h>
#include <mach/at91_st.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"

static void at91rm9200_idle(void)
{
	/*
	 * Disable the processor clock.  The processor will be automatically
	 * re-enabled by an interrupt or by a reset.
	 */
	at91_pmc_write(AT91_PMC_SCDR, AT91_PMC_PCK);
}

static void at91rm9200_restart(enum reboot_mode reboot_mode, const char *cmd)
{
	/*
	 * Perform a hardware reset with the use of the Watchdog timer.
	 */
	at91_st_write(AT91_ST_WDMR, AT91_ST_RSTEN | AT91_ST_EXTEN | 1);
	at91_st_write(AT91_ST_CR, AT91_ST_WDRST);
}

/* --------------------------------------------------------------------
 *  AT91RM9200 processor initialization
 * -------------------------------------------------------------------- */
static void __init at91rm9200_map_io(void)
{
	/* Map peripherals */
	at91_init_sram(0, AT91RM9200_SRAM_BASE, AT91RM9200_SRAM_SIZE);
}

static void __init at91rm9200_initialize(void)
{
	arm_pm_idle = at91rm9200_idle;
	arm_pm_restart = at91rm9200_restart;
}


AT91_SOC_START(at91rm9200)
	.map_io = at91rm9200_map_io,
	.init = at91rm9200_initialize,
AT91_SOC_END
