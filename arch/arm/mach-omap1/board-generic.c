/*
 * linux/arch/arm/mach-omap1/board-generic.c
 *
 * Modified from board-innovator1510.c
 *
 * Code for generic OMAP board. Should work on many OMAP systems where
 * the device drivers take care of all the necessary hardware initialization.
 * Do not put any board specific code to this file; create a new machine
 * type if you need custom low-level initializations.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>
#include <asm/arch/usb.h>
#include <asm/arch/board.h>
#include <asm/arch/common.h>

static void __init omap_generic_init_irq(void)
{
	omap1_init_common_hw();
	omap_init_irq();
}

/* assume no Mini-AB port */

#ifdef CONFIG_ARCH_OMAP15XX
static struct omap_usb_config generic1510_usb_config __initdata = {
	.register_host	= 1,
	.register_dev	= 1,
	.hmc_mode	= 16,
	.pins[0]	= 3,
};
#endif

#if defined(CONFIG_ARCH_OMAP16XX)
static struct omap_usb_config generic1610_usb_config __initdata = {
#ifdef CONFIG_USB_OTG
	.otg		= 1,
#endif
	.register_host	= 1,
	.register_dev	= 1,
	.hmc_mode	= 16,
	.pins[0]	= 6,
};

static struct omap_mmc_config generic_mmc_config __initdata = {
	.mmc [0] = {
		.enabled 	= 0,
		.wire4		= 0,
		.wp_pin		= -1,
		.power_pin	= -1,
		.switch_pin	= -1,
	},
	.mmc [1] = {
		.enabled 	= 0,
		.wire4		= 0,
		.wp_pin		= -1,
		.power_pin	= -1,
		.switch_pin	= -1,
	},
};

#endif

static struct omap_uart_config generic_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel generic_config[] = {
	{ OMAP_TAG_USB,           NULL },
	{ OMAP_TAG_MMC,           &generic_mmc_config },
	{ OMAP_TAG_UART,	&generic_uart_config },
};

static void __init omap_generic_init(void)
{
#ifdef CONFIG_ARCH_OMAP15XX
	if (cpu_is_omap1510()) {
		generic_config[0].data = &generic1510_usb_config;
	}
#endif
#if defined(CONFIG_ARCH_OMAP16XX)
	if (!cpu_is_omap1510()) {
		generic_config[0].data = &generic1610_usb_config;
	}
#endif

	omap_board_config = generic_config;
	omap_board_config_size = ARRAY_SIZE(generic_config);
	omap_serial_init();
}

static void __init omap_generic_map_io(void)
{
	omap1_map_common_io();
}

MACHINE_START(OMAP_GENERIC, "Generic OMAP1510/1610/1710")
	/* Maintainer: Tony Lindgren <tony@atomide.com> */
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= omap_generic_map_io,
	.init_irq	= omap_generic_init_irq,
	.init_machine	= omap_generic_init,
	.timer		= &omap_timer,
MACHINE_END
