/*
 * Static memory mapping for DEBUG_LL
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <mach/map.h>
#include <mach/uart.h>

void __init sirfsoc_map_lluart(void)
{
	struct map_desc sirfsoc_lluart_map = {
		.virtual        = SIRFSOC_UART1_VA_BASE,
		.pfn            = __phys_to_pfn(SIRFSOC_UART1_PA_BASE),
		.length         = SIRFSOC_UART1_SIZE,
		.type           = MT_DEVICE,
	};

	iotable_init(&sirfsoc_lluart_map, 1);
}
