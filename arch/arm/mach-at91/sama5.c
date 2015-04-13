/*
 *  Setup code for SAMA5
 *
 *  Copyright (C) 2013 Atmel,
 *                2013 Ludovic Desroches <ludovic.desroches@atmel.com>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/micrel_phy.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/clk-provider.h>
#include <linux/phy.h>

#include <mach/hardware.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "generic.h"

static int ksz8081_phy_fixup(struct phy_device *phy)
{
	int value;

	value = phy_read(phy, 0x16);
	value &= ~0x20;
	phy_write(phy, 0x16, value);

	return 0;
}

static void __init sama5_dt_device_init(void)
{
	if (of_machine_is_compatible("atmel,sama5d4ek") &&
	   IS_ENABLED(CONFIG_PHYLIB)) {
		phy_register_fixup_for_id("fc028000.etherne:00",
						ksz8081_phy_fixup);
	}

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	at91sam9x5_pm_init();
}

static const char *sama5_dt_board_compat[] __initconst = {
	"atmel,sama5",
	NULL
};

DT_MACHINE_START(sama5_dt, "Atmel SAMA5")
	/* Maintainer: Atmel */
	.map_io		= at91_map_io,
	.init_machine	= sama5_dt_device_init,
	.dt_compat	= sama5_dt_board_compat,
MACHINE_END

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

static void __init sama5_alt_map_io(void)
{
	at91_alt_map_io();
	iotable_init(at91_io_desc, ARRAY_SIZE(at91_io_desc));
}

static const char *sama5_alt_dt_board_compat[] __initconst = {
	"atmel,sama5d4",
	NULL
};

DT_MACHINE_START(sama5_alt_dt, "Atmel SAMA5")
	/* Maintainer: Atmel */
	.map_io		= sama5_alt_map_io,
	.init_machine	= sama5_dt_device_init,
	.dt_compat	= sama5_alt_dt_board_compat,
	.l2c_aux_mask	= ~0UL,
MACHINE_END
