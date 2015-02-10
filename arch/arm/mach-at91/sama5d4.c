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
static struct map_desc at91_io_desc[] __initdata = {
	{
	.virtual        = (unsigned long)AT91_ALT_IO_P2V(SAMA5D4_BASE_MPDDRC),
	.pfn            = __phys_to_pfn(SAMA5D4_BASE_MPDDRC),
	.length         = SZ_512,
	.type           = MT_DEVICE,
	},
	{
	.virtual        = (unsigned long)AT91_ALT_IO_P2V(SAMA5D4_BASE_PMC),
	.pfn            = __phys_to_pfn(SAMA5D4_BASE_PMC),
	.length         = SZ_512,
	.type           = MT_DEVICE,
	},
	{ /* On sama5d4, we use USART3 as serial console */
	.virtual        = (unsigned long)AT91_ALT_IO_P2V(SAMA5D4_BASE_USART3),
	.pfn            = __phys_to_pfn(SAMA5D4_BASE_USART3),
	.length         = SZ_256,
	.type           = MT_DEVICE,
	},
	{ /* A bunch of peripheral with fine grained IO space */
	.virtual        = (unsigned long)AT91_ALT_IO_P2V(SAMA5D4_BASE_SYS2),
	.pfn            = __phys_to_pfn(SAMA5D4_BASE_SYS2),
	.length         = SZ_2K,
	.type           = MT_DEVICE,
	},
};


static void __init sama5d4_map_io(void)
{
	iotable_init(at91_io_desc, ARRAY_SIZE(at91_io_desc));
	at91_init_sram(0, SAMA5D4_NS_SRAM_BASE, SAMA5D4_NS_SRAM_SIZE);
}

AT91_SOC_START(sama5d4)
	.map_io = sama5d4_map_io,
AT91_SOC_END
