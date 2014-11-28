/*
 *  Chip-specific setup code for the AT91SAM9G45 family
 *
 *  Copyright (C) 2009 Atmel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <asm/system_misc.h>
#include <asm/irq.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"

/* --------------------------------------------------------------------
 *  AT91SAM9G45 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9g45_map_io(void)
{
	at91_init_sram(0, AT91SAM9G45_SRAM_BASE, AT91SAM9G45_SRAM_SIZE);
}

static void __init at91sam9g45_initialize(void)
{
	arm_pm_idle = at91sam9_idle;

	at91_sysirq_mask_rtc(AT91SAM9G45_BASE_RTC);
	at91_sysirq_mask_rtt(AT91SAM9G45_BASE_RTT);
}

AT91_SOC_START(at91sam9g45)
	.map_io = at91sam9g45_map_io,
	.init = at91sam9g45_initialize,
AT91_SOC_END
