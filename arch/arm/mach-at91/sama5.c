/*
 *  Setup code for SAMA5
 *
 *  Copyright (C) 2013 Atmel,
 *                2013 Ludovic Desroches <ludovic.desroches@atmel.com>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>

#include "generic.h"
#include "soc.h"

static const struct at91_soc sama5_socs[] = {
	AT91_SOC(SAMA5D2_CIDR_MATCH, SAMA5D21CU_EXID_MATCH,
		 "sama5d21", "sama5d2"),
	AT91_SOC(SAMA5D2_CIDR_MATCH, SAMA5D22CU_EXID_MATCH,
		 "sama5d22", "sama5d2"),
	AT91_SOC(SAMA5D2_CIDR_MATCH, SAMA5D23CU_EXID_MATCH,
		 "sama5d23", "sama5d2"),
	AT91_SOC(SAMA5D2_CIDR_MATCH, SAMA5D24CX_EXID_MATCH,
		 "sama5d24", "sama5d2"),
	AT91_SOC(SAMA5D2_CIDR_MATCH, SAMA5D24CU_EXID_MATCH,
		 "sama5d24", "sama5d2"),
	AT91_SOC(SAMA5D2_CIDR_MATCH, SAMA5D26CU_EXID_MATCH,
		 "sama5d26", "sama5d2"),
	AT91_SOC(SAMA5D2_CIDR_MATCH, SAMA5D27CU_EXID_MATCH,
		 "sama5d27", "sama5d2"),
	AT91_SOC(SAMA5D2_CIDR_MATCH, SAMA5D27CN_EXID_MATCH,
		 "sama5d27", "sama5d2"),
	AT91_SOC(SAMA5D2_CIDR_MATCH, SAMA5D28CU_EXID_MATCH,
		 "sama5d28", "sama5d2"),
	AT91_SOC(SAMA5D2_CIDR_MATCH, SAMA5D28CN_EXID_MATCH,
		 "sama5d28", "sama5d2"),
	AT91_SOC(SAMA5D3_CIDR_MATCH, SAMA5D31_EXID_MATCH,
		 "sama5d31", "sama5d3"),
	AT91_SOC(SAMA5D3_CIDR_MATCH, SAMA5D33_EXID_MATCH,
		 "sama5d33", "sama5d3"),
	AT91_SOC(SAMA5D3_CIDR_MATCH, SAMA5D34_EXID_MATCH,
		 "sama5d34", "sama5d3"),
	AT91_SOC(SAMA5D3_CIDR_MATCH, SAMA5D35_EXID_MATCH,
		 "sama5d35", "sama5d3"),
	AT91_SOC(SAMA5D3_CIDR_MATCH, SAMA5D36_EXID_MATCH,
		 "sama5d36", "sama5d3"),
	AT91_SOC(SAMA5D4_CIDR_MATCH, SAMA5D41_EXID_MATCH,
		 "sama5d41", "sama5d4"),
	AT91_SOC(SAMA5D4_CIDR_MATCH, SAMA5D42_EXID_MATCH,
		 "sama5d42", "sama5d4"),
	AT91_SOC(SAMA5D4_CIDR_MATCH, SAMA5D43_EXID_MATCH,
		 "sama5d43", "sama5d4"),
	AT91_SOC(SAMA5D4_CIDR_MATCH, SAMA5D44_EXID_MATCH,
		 "sama5d44", "sama5d4"),
	{ /* sentinel */ },
};

static void __init sama5_dt_device_init(void)
{
	struct soc_device *soc;
	struct device *soc_dev = NULL;

	soc = at91_soc_init(sama5_socs);
	if (soc != NULL)
		soc_dev = soc_device_to_device(soc);

	of_platform_populate(NULL, of_default_bus_match_table, NULL, soc_dev);
	sama5_pm_init();
}

static const char *const sama5_dt_board_compat[] __initconst = {
	"atmel,sama5",
	NULL
};

DT_MACHINE_START(sama5_dt, "Atmel SAMA5")
	/* Maintainer: Atmel */
	.init_machine	= sama5_dt_device_init,
	.dt_compat	= sama5_dt_board_compat,
MACHINE_END

static const char *const sama5_alt_dt_board_compat[] __initconst = {
	"atmel,sama5d2",
	"atmel,sama5d4",
	NULL
};

DT_MACHINE_START(sama5_alt_dt, "Atmel SAMA5")
	/* Maintainer: Atmel */
	.init_machine	= sama5_dt_device_init,
	.dt_compat	= sama5_alt_dt_board_compat,
	.l2c_aux_mask	= ~0UL,
MACHINE_END
