/*
 *  Setup code for AT91SAM Evaluation Kits with Device Tree support
 *
 *  Covers: * AT91SAM9G45-EKES  board
 *          * AT91SAM9M10-EKES  board
 *          * AT91SAM9M10G45-EK board
 *
 *  Copyright (C) 2011 Atmel,
 *                2011 Nicolas Ferre <nicolas.ferre@atmel.com>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/system_rev.h>
#include <mach/at91sam9_smc.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "sam9_smc.h"
#include "generic.h"


static void __init ek_init_early(void)
{
	/* Initialize processor: 12.000 MHz crystal */
	at91_initialize(12000000);
}

/* det_pin is not connected */
static struct atmel_nand_data __initdata ek_nand_data = {
	.ale		= 21,
	.cle		= 22,
	.det_pin	= -EINVAL,
	.rdy_pin	= AT91_PIN_PC8,
	.enable_pin	= AT91_PIN_PC14,
};

static struct sam9_smc_config __initdata ek_nand_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 2,
	.ncs_write_setup	= 0,
	.nwe_setup		= 2,

	.ncs_read_pulse		= 4,
	.nrd_pulse		= 4,
	.ncs_write_pulse	= 4,
	.nwe_pulse		= 4,

	.read_cycle		= 7,
	.write_cycle		= 7,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE,
	.tdf_cycles		= 3,
};

static void __init ek_add_device_nand(void)
{
	ek_nand_data.bus_width_16 = board_have_nand_16bit();
	/* setup bus-width (8 or 16) */
	if (ek_nand_data.bus_width_16)
		ek_nand_smc_config.mode |= AT91_SMC_DBW_16;
	else
		ek_nand_smc_config.mode |= AT91_SMC_DBW_8;

	/* configure chip-select 3 (NAND) */
	sam9_smc_configure(0, 3, &ek_nand_smc_config);

	at91_add_device_nand(&ek_nand_data);
}

static const struct of_device_id irq_of_match[] __initconst = {

	{ .compatible = "atmel,at91rm9200-aic", .data = at91_aic_of_init },
	{ .compatible = "atmel,at91rm9200-gpio", .data = at91_gpio_of_irq_setup },
	{ .compatible = "atmel,at91sam9x5-gpio", .data = at91_gpio_of_irq_setup },
	{ /*sentinel*/ }
};

static void __init at91_dt_init_irq(void)
{
	of_irq_init(irq_of_match);
}

static void __init at91_dt_device_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);

	/* NAND */
	ek_add_device_nand();
}

static const char *at91_dt_board_compat[] __initdata = {
	"atmel,at91sam9m10g45ek",
	"atmel,at91sam9x5ek",
	"calao,usb-a9g20",
	NULL
};

DT_MACHINE_START(at91sam_dt, "Atmel AT91SAM (Device Tree)")
	/* Maintainer: Atmel */
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_early	= ek_init_early,
	.init_irq	= at91_dt_init_irq,
	.init_machine	= at91_dt_device_init,
	.dt_compat	= at91_dt_board_compat,
MACHINE_END
