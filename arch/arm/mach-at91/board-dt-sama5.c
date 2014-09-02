/*
 *  Setup code for SAMA5 Evaluation Kits with Device Tree support
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

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "at91_aic.h"
#include "generic.h"

static int ksz9021rn_phy_fixup(struct phy_device *phy)
{
	int value;

	/* Set delay values */
	value = MICREL_KSZ9021_RGMII_CLK_CTRL_PAD_SCEW | 0x8000;
	phy_write(phy, MICREL_KSZ9021_EXTREG_CTRL, value);
	value = 0xF2F4;
	phy_write(phy, MICREL_KSZ9021_EXTREG_DATA_WRITE, value);
	value = MICREL_KSZ9021_RGMII_RX_DATA_PAD_SCEW | 0x8000;
	phy_write(phy, MICREL_KSZ9021_EXTREG_CTRL, value);
	value = 0x2222;
	phy_write(phy, MICREL_KSZ9021_EXTREG_DATA_WRITE, value);

	return 0;
}

static void __init sama5_dt_device_init(void)
{
	if (of_machine_is_compatible("atmel,sama5d3xcm") &&
	    IS_ENABLED(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_KSZ9021, MICREL_PHY_ID_MASK,
			ksz9021rn_phy_fixup);

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *sama5_dt_board_compat[] __initdata = {
	"atmel,sama5",
	NULL
};

DT_MACHINE_START(sama5_dt, "Atmel SAMA5 (Device Tree)")
	/* Maintainer: Atmel */
	.map_io		= at91_map_io,
	.init_early	= at91_dt_initialize,
	.init_machine	= sama5_dt_device_init,
	.dt_compat	= sama5_dt_board_compat,
MACHINE_END
