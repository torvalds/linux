/*
 * arch/arm/mach-at91/at91sam9263.c
 *
 *  Copyright (C) 2007 Atmel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <asm/system_misc.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"

/* --------------------------------------------------------------------
 *  AT91SAM9263 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9263_map_io(void)
{
	at91_init_sram(0, AT91SAM9263_SRAM0_BASE, AT91SAM9263_SRAM0_SIZE);
	at91_init_sram(1, AT91SAM9263_SRAM1_BASE, AT91SAM9263_SRAM1_SIZE);
}

static void __init at91sam9263_initialize(void)
{
	arm_pm_idle = at91sam9_idle;
}

AT91_SOC_START(at91sam9263)
	.map_io = at91sam9263_map_io,
	.init = at91sam9263_initialize,
AT91_SOC_END
