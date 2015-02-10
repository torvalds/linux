/*
 * SoC specific setup code for the AT91SAM9N12
 *
 * Copyright (C) 2012 Atmel Corporation.
 *
 * Licensed under GPLv2 or later.
 */

#include <asm/system_misc.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"

/* --------------------------------------------------------------------
 *  AT91SAM9N12 processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9n12_map_io(void)
{
	at91_init_sram(0, AT91SAM9N12_SRAM_BASE, AT91SAM9N12_SRAM_SIZE);
}

static void __init at91sam9n12_initialize(void)
{
	at91_sysirq_mask_rtc(AT91SAM9N12_BASE_RTC);
}

AT91_SOC_START(at91sam9n12)
	.map_io = at91sam9n12_map_io,
	.init = at91sam9n12_initialize,
AT91_SOC_END
