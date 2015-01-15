/*
 *  Chip-specific setup code for the AT91SAM9x5 family
 *
 *  Copyright (C) 2010-2012 Atmel Corporation.
 *
 * Licensed under GPLv2 or later.
 */

#include <asm/system_misc.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"

/* --------------------------------------------------------------------
 *  AT91SAM9x5 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9x5_initialize(void)
{
	arm_pm_idle = at91sam9_idle;
}

AT91_SOC_START(at91sam9x5)
	.init = at91sam9x5_initialize,
AT91_SOC_END
