/*
 * arch/arm/mach-at91/at91sam9260.c
 *
 *  Copyright (C) 2006 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <asm/system_misc.h>
#include <mach/cpu.h>
#include <mach/at91_dbgu.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"

/* --------------------------------------------------------------------
 *  AT91SAM9260 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9xe_map_io(void)
{
	unsigned long sram_size;

	switch (at91_soc_initdata.cidr & AT91_CIDR_SRAMSIZ) {
		case AT91_CIDR_SRAMSIZ_32K:
			sram_size = 2 * SZ_16K;
			break;
		case AT91_CIDR_SRAMSIZ_16K:
		default:
			sram_size = SZ_16K;
	}

	at91_init_sram(0, AT91SAM9XE_SRAM_BASE, sram_size);
}

static void __init at91sam9260_map_io(void)
{
	if (cpu_is_at91sam9xe())
		at91sam9xe_map_io();
	else if (cpu_is_at91sam9g20())
		at91_init_sram(0, AT91SAM9G20_SRAM_BASE, AT91SAM9G20_SRAM_SIZE);
	else
		at91_init_sram(0, AT91SAM9260_SRAM_BASE, AT91SAM9260_SRAM_SIZE);
}

static void __init at91sam9260_initialize(void)
{
	arm_pm_idle = at91sam9_idle;

	at91_sysirq_mask_rtt(AT91SAM9260_BASE_RTT);
}

AT91_SOC_START(at91sam9260)
	.map_io = at91sam9260_map_io,
	.init = at91sam9260_initialize,
AT91_SOC_END
