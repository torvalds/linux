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

static void __init at91sam9260_initialize(void)
{
	arm_pm_idle = at91sam9_idle;
}

AT91_SOC_START(at91sam9260)
	.init = at91sam9260_initialize,
AT91_SOC_END
