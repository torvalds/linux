/*
 *  arch/arm/mach-pxa/colibri-pxa300.c
 *
 *  Support for Toradex PXA300 based Colibri module
 *  Daniel Mack <daniel@caiaq.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <net/ax88796.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>

#include <mach/pxa300.h>
#include <mach/colibri.h>

#include "generic.h"
#include "devices.h"

/*
 * GPIO configuration
 */
static mfp_cfg_t colibri_pxa300_pin_config[] __initdata = {
	GPIO1_nCS2,			/* AX88796 chip select */
	GPIO26_GPIO | MFP_PULL_HIGH,	/* AX88796 IRQ */
};

#if defined(CONFIG_AX88796)
/*
 * Asix AX88796 Ethernet
 */
static struct ax_plat_data colibri_asix_platdata = {
	.flags		= AXFLG_MAC_FROMDEV,
	.wordlength	= 2,
	.dcr_val	= 0x01,
	.rcr_val	= 0x0e,
	.gpoc_val	= 0x19
};

static struct resource colibri_asix_resource[] = {
	[0] = {
		.start = PXA3xx_CS2_PHYS,
		.end   = PXA3xx_CS2_PHYS + (0x18 * 0x2) - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = PXA3xx_CS2_PHYS + (1 << 11),
		.end   = PXA3xx_CS2_PHYS + (1 << 11) + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = COLIBRI_PXA300_ETH_IRQ,
		.end   = COLIBRI_PXA300_ETH_IRQ,
		.flags = IORESOURCE_IRQ
	}
};

static struct platform_device asix_device = {
	.name		= "ax88796",
	.id		= 0,
	.num_resources 	= ARRAY_SIZE(colibri_asix_resource),
	.resource	= colibri_asix_resource,
	.dev		= {
		.platform_data = &colibri_asix_platdata
	}
};
#endif /* CONFIG_AX88796 */

static struct platform_device *colibri_pxa300_devices[] __initdata = {
#if defined(CONFIG_AX88796)
	&asix_device
#endif
};

static void __init colibri_pxa300_init(void)
{
	set_irq_type(COLIBRI_PXA300_ETH_IRQ, IRQ_TYPE_EDGE_FALLING);
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa300_pin_config));
	platform_add_devices(ARRAY_AND_SIZE(colibri_pxa300_devices));
}

MACHINE_START(COLIBRI300, "Toradex Colibri PXA300")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= COLIBRI_SDRAM_BASE + 0x100,
	.init_machine	= colibri_pxa300_init,
	.map_io		= pxa_map_io,
	.init_irq	= pxa3xx_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

