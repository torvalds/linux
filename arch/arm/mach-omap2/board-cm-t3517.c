/*
 * linux/arch/arm/mach-omap2/board-cm-t3517.c
 *
 * Support for the CompuLab CM-T3517 modules
 *
 * Copyright (C) 2010 CompuLab, Ltd.
 * Author: Igor Grinberg <grinberg@compulab.co.il>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/leds.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/control.h>

#include "mux.h"

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
static struct gpio_led cm_t3517_leds[] = {
	[0] = {
		.gpio			= 186,
		.name			= "cm-t3517:green",
		.default_trigger	= "heartbeat",
		.active_low		= 0,
	},
};

static struct gpio_led_platform_data cm_t3517_led_pdata = {
	.num_leds	= ARRAY_SIZE(cm_t3517_leds),
	.leds		= cm_t3517_leds,
};

static struct platform_device cm_t3517_led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &cm_t3517_led_pdata,
	},
};

static void __init cm_t3517_init_leds(void)
{
	platform_device_register(&cm_t3517_led_device);
}
#else
static inline void cm_t3517_init_leds(void) {}
#endif


static struct omap_board_config_kernel cm_t3517_config[] __initdata = {
};

static void __init cm_t3517_init_irq(void)
{
	omap_board_config = cm_t3517_config;
	omap_board_config_size = ARRAY_SIZE(cm_t3517_config);

	omap2_init_common_hw(NULL, NULL);
	omap_init_irq();
	omap_gpio_init();
}

static struct omap_board_mux board_mux[] __initdata = {
	/* GPIO186 - Green LED */
	OMAP3_MUX(SYS_CLKOUT2, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),

	{ .reg_offset = OMAP_MUX_TERMINATOR },
};

static void __init cm_t3517_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	omap_serial_init();
	cm_t3517_init_leds();
}

MACHINE_START(CM_T3517, "Compulab CM-T3517")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap3_map_io,
	.reserve        = omap_reserve,
	.init_irq	= cm_t3517_init_irq,
	.init_machine	= cm_t3517_init,
	.timer		= &omap_timer,
MACHINE_END
