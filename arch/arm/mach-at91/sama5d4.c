/*
 *  Chip-specific setup code for the SAMA5D4 family
 *
 *  Copyright (C) 2013 Atmel Corporation,
 *                     Nicolas Ferre <nicolas.ferre@atmel.com>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/clk/at91_pmc.h>

#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/sama5d4.h>
#include <mach/cpu.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"
#include "sam9_smc.h"

/* --------------------------------------------------------------------
 *  Processor initialization
 * -------------------------------------------------------------------- */

static void __init sama5d4_map_io(void)
{
	at91_init_sram(0, SAMA5D4_NS_SRAM_BASE, SAMA5D4_NS_SRAM_SIZE);
}

AT91_SOC_START(sama5d4)
	.map_io = sama5d4_map_io,
AT91_SOC_END
