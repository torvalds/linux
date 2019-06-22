// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-pxa/generic.c
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * Code common to all PXA machines.
 *
 * Since this file should be linked before any other machine specific file,
 * the __initcall() here will be executed first.  This serves as default
 * initialization stuff for PXA machines which can be overridden later if
 * need be.
 */
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <mach/hardware.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>

#include <mach/irqs.h>
#include <mach/reset.h>
#include <mach/smemc.h>
#include <mach/pxa3xx-regs.h>

#include "generic.h"
#include <clocksource/pxa.h>

void clear_reset_status(unsigned int mask)
{
	if (cpu_is_pxa2xx())
		pxa2xx_clear_reset_status(mask);
	else {
		/* RESET_STATUS_* has a 1:1 mapping with ARSR */
		ARSR = mask;
	}
}

/*
 * For non device-tree builds, keep legacy timer init
 */
void __init pxa_timer_init(void)
{
	if (cpu_is_pxa25x())
		pxa25x_clocks_init();
	if (cpu_is_pxa27x())
		pxa27x_clocks_init();
	if (cpu_is_pxa3xx())
		pxa3xx_clocks_init();
	pxa_timer_nodt_init(IRQ_OST0, io_p2v(0x40a00000));
}

/*
 * Get the clock frequency as reflected by CCCR and the turbo flag.
 * We assume these values have been applied via a fcs.
 * If info is not 0 we also display the current settings.
 */
unsigned int get_clk_frequency_khz(int info)
{
	if (cpu_is_pxa25x())
		return pxa25x_get_clk_frequency_khz(info);
	else if (cpu_is_pxa27x())
		return pxa27x_get_clk_frequency_khz(info);
	return 0;
}
EXPORT_SYMBOL(get_clk_frequency_khz);

/*
 * Intel PXA2xx internal register mapping.
 *
 * Note: virtual 0xfffe0000-0xffffffff is reserved for the vector table
 *       and cache flush area.
 */
static struct map_desc common_io_desc[] __initdata = {
  	{	/* Devs */
		.virtual	= (unsigned long)PERIPH_VIRT,
		.pfn		= __phys_to_pfn(PERIPH_PHYS),
		.length		= PERIPH_SIZE,
		.type		= MT_DEVICE
	}
};

void __init pxa_map_io(void)
{
	debug_ll_io_init();
	iotable_init(ARRAY_AND_SIZE(common_io_desc));
}
