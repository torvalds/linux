/*
 * Lager board support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_device.h>
#include <mach/common.h>
#include <mach/r8a7790.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* LEDS */
static struct gpio_led lager_leds[] = {
	{
		.name		= "led8",
		.gpio		= RCAR_GP_PIN(5, 17),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led7",
		.gpio		= RCAR_GP_PIN(4, 23),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led6",
		.gpio		= RCAR_GP_PIN(4, 22),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
};

static __initdata struct gpio_led_platform_data lager_leds_pdata = {
	.leds		= lager_leds,
	.num_leds	= ARRAY_SIZE(lager_leds),
};

/* GPIO KEY */
#define GPIO_KEY(c, g, d, ...) \
	{ .code = c, .gpio = g, .desc = d, .active_low = 1 }

static __initdata struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_4,		RCAR_GP_PIN(1, 28),	"SW2-pin4"),
	GPIO_KEY(KEY_3,		RCAR_GP_PIN(1, 26),	"SW2-pin3"),
	GPIO_KEY(KEY_2,		RCAR_GP_PIN(1, 24),	"SW2-pin2"),
	GPIO_KEY(KEY_1,		RCAR_GP_PIN(1, 14),	"SW2-pin1"),
};

static __initdata struct gpio_keys_platform_data lager_keys_pdata = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

static const struct pinctrl_map lager_pinctrl_map[] = {
	/* SCIF0 (CN19: DEBUG SERIAL0) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.6", "pfc-r8a7790",
				  "scif0_data", "scif0"),
	/* SCIF1 (CN20: DEBUG SERIAL1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.7", "pfc-r8a7790",
				  "scif1_data", "scif1"),
};

static void __init lager_add_standard_devices(void)
{
	r8a7790_clock_init();

	pinctrl_register_mappings(lager_pinctrl_map,
				  ARRAY_SIZE(lager_pinctrl_map));
	r8a7790_pinmux_init();

	r8a7790_add_standard_devices();
	platform_device_register_data(&platform_bus, "leds-gpio", -1,
				      &lager_leds_pdata,
				      sizeof(lager_leds_pdata));
	platform_device_register_data(&platform_bus, "gpio-keys", -1,
				      &lager_keys_pdata,
				      sizeof(lager_keys_pdata));
}

static const char *lager_boards_compat_dt[] __initdata = {
	"renesas,lager",
	NULL,
};

DT_MACHINE_START(LAGER_DT, "lager")
	.init_irq	= irqchip_init,
	.init_time	= r8a7790_timer_init,
	.init_machine	= lager_add_standard_devices,
	.dt_compat	= lager_boards_compat_dt,
MACHINE_END
