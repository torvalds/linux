/*
 * linux/arch/arm/mach-omap2/board-rx51.c
 *
 * Copyright (C) 2007, 2008 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/leds.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/mcspi.h>
#include <plat/board.h>
#include <plat/common.h>
#include <plat/dma.h>
#include <plat/gpmc.h>
#include <plat/usb.h>

#include "mux.h"
#include "pm.h"
#include "sdram-nokia.h"

#define RX51_GPIO_SLEEP_IND 162

extern void rx51_video_mem_init(void);

static struct gpio_led gpio_leds[] = {
	{
		.name	= "sleep_ind",
		.gpio	= RX51_GPIO_SLEEP_IND,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	},
};

/*
 * cpuidle C-states definition override from the default values.
 * The 'exit_latency' field is the sum of sleep and wake-up latencies.
 */
static struct cpuidle_params rx51_cpuidle_params[] = {
	/* C1 */
	{110 + 162, 5 , 1},
	/* C2 */
	{106 + 180, 309, 1},
	/* C3 */
	{107 + 410, 46057, 0},
	/* C4 */
	{121 + 3374, 46057, 0},
	/* C5 */
	{855 + 1146, 46057, 1},
	/* C6 */
	{7580 + 4134, 484329, 0},
	/* C7 */
	{7505 + 15274, 484329, 1},
};

static struct omap_lcd_config rx51_lcd_config = {
	.ctrl_name	= "internal",
};

static struct omap_fbmem_config rx51_fbmem0_config = {
	.size = 752 * 1024,
};

static struct omap_fbmem_config rx51_fbmem1_config = {
	.size = 752 * 1024,
};

static struct omap_fbmem_config rx51_fbmem2_config = {
	.size = 752 * 1024,
};

static struct omap_board_config_kernel rx51_config[] = {
	{ OMAP_TAG_FBMEM,	&rx51_fbmem0_config },
	{ OMAP_TAG_FBMEM,	&rx51_fbmem1_config },
	{ OMAP_TAG_FBMEM,	&rx51_fbmem2_config },
	{ OMAP_TAG_LCD,		&rx51_lcd_config },
};

static void __init rx51_init_early(void)
{
	struct omap_sdrc_params *sdrc_params;

	omap2_init_common_infrastructure();
	sdrc_params = nokia_get_sdram_timings();
	omap2_init_common_devices(sdrc_params, sdrc_params);
}

extern void __init rx51_peripherals_init(void);

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_PERIPHERAL,
	.power			= 0,
};

static void __init rx51_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	omap_board_config = rx51_config;
	omap_board_config_size = ARRAY_SIZE(rx51_config);
	omap3_pm_init_cpuidle(rx51_cpuidle_params);
	omap_serial_init();
	usb_musb_init(&musb_board_data);
	rx51_peripherals_init();

	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_mux_init_signal("sdrc_cke0", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("sdrc_cke1", OMAP_PIN_OUTPUT);

	platform_device_register(&leds_gpio);
}

static void __init rx51_map_io(void)
{
	omap2_set_globals_3xxx();
	omap34xx_map_common_io();
}

static void __init rx51_reserve(void)
{
	rx51_video_mem_init();
	omap_reserve();
}

MACHINE_START(NOKIA_RX51, "Nokia RX-51 board")
	/* Maintainer: Lauri Leukkunen <lauri.leukkunen@nokia.com> */
	.boot_params	= 0x80000100,
	.reserve	= rx51_reserve,
	.map_io		= rx51_map_io,
	.init_early	= rx51_init_early,
	.init_irq	= omap3_init_irq,
	.init_machine	= rx51_init,
	.timer		= &omap3_timer,
MACHINE_END
