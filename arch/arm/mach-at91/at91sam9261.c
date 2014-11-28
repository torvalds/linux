/*
 * arch/arm/mach-at91/at91sam9261.c
 *
 *  Copyright (C) 2005 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <asm/system_misc.h>
#include <mach/cpu.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"

/* --------------------------------------------------------------------
 *  AT91SAM9261 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9261_map_io(void)
{
	if (cpu_is_at91sam9g10())
		at91_init_sram(0, AT91SAM9G10_SRAM_BASE, AT91SAM9G10_SRAM_SIZE);
	else
		at91_init_sram(0, AT91SAM9261_SRAM_BASE, AT91SAM9261_SRAM_SIZE);
}

static void __init at91sam9261_initialize(void)
{
	arm_pm_idle = at91sam9_idle;

	at91_sysirq_mask_rtt(AT91SAM9261_BASE_RTT);
}

AT91_SOC_START(at91sam9261)
	.map_io = at91sam9261_map_io,
	.init = at91sam9261_initialize,
AT91_SOC_END
