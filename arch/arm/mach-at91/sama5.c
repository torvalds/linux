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

#include <mach/hardware.h>

#include "generic.h"
#include "soc.h"

static const struct at91_soc sama5_socs[] = {
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
	at91sam9x5_pm_init();
}

static const char *sama5_dt_board_compat[] __initconst = {
	"atmel,sama5",
	NULL
};

DT_MACHINE_START(sama5_dt, "Atmel SAMA5")
	/* Maintainer: Atmel */
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
