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

static void __init at91sam9261_initialize(void)
{
	arm_pm_idle = at91sam9_idle;
}

AT91_SOC_START(at91sam9261)
	.init = at91sam9261_initialize,
AT91_SOC_END
