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

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "at91_aic.h"
#include "generic.h"


static const struct of_device_id irq_of_match[] __initconst = {

	{ .compatible = "atmel,sama5d3-aic", .data = at91_aic5_of_init },
	{ /*sentinel*/ }
};

static void __init at91_dt_init_irq(void)
{
	of_irq_init(irq_of_match);
}

static int ksz9021rn_phy_fixup(struct phy_device *phy)
{
	int value;

#define GMII_RCCPSR	260
#define GMII_RRDPSR	261
#define GMII_ERCR	11
#define GMII_ERDWR	12

	/* Set delay values */
	value = GMII_RCCPSR | 0x8000;
	phy_write(phy, GMII_ERCR, value);
	value = 0xF2F4;
	phy_write(phy, GMII_ERDWR, value);
	value = GMII_RRDPSR | 0x8000;
	phy_write(phy, GMII_ERCR, value);
	value = 0x2222;
	phy_write(phy, GMII_ERDWR, value);

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
	.init_time	= at91sam926x_pit_init,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic5_handle_irq,
	.init_early	= at91_dt_initialize,
	.init_irq	= at91_dt_init_irq,
	.init_machine	= sama5_dt_device_init,
	.dt_compat	= sama5_dt_board_compat,
MACHINE_END
