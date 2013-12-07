/*
 * Koelsch board support
 *
 * Copyright (C) 2013  Renesas Electronics Corporation
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

#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/rcar-du.h>
#include <linux/platform_device.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7791.h>
#include <mach/rcar-gen2.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* DU */
static struct rcar_du_encoder_data koelsch_du_encoders[] = {
	{
		.type = RCAR_DU_ENCODER_NONE,
		.output = RCAR_DU_OUTPUT_LVDS0,
		.connector.lvds.panel = {
			.width_mm = 210,
			.height_mm = 158,
			.mode = {
				.clock = 65000,
				.hdisplay = 1024,
				.hsync_start = 1048,
				.hsync_end = 1184,
				.htotal = 1344,
				.vdisplay = 768,
				.vsync_start = 771,
				.vsync_end = 777,
				.vtotal = 806,
				.flags = 0,
			},
		},
	},
};

static const struct rcar_du_platform_data koelsch_du_pdata __initconst = {
	.encoders = koelsch_du_encoders,
	.num_encoders = ARRAY_SIZE(koelsch_du_encoders),
};

static const struct resource du_resources[] __initconst = {
	DEFINE_RES_MEM(0xfeb00000, 0x40000),
	DEFINE_RES_MEM_NAMED(0xfeb90000, 0x1c, "lvds.0"),
	DEFINE_RES_IRQ(gic_spi(256)),
	DEFINE_RES_IRQ(gic_spi(268)),
};

static void __init koelsch_add_du_device(void)
{
	struct platform_device_info info = {
		.name = "rcar-du-r8a7791",
		.id = -1,
		.res = du_resources,
		.num_res = ARRAY_SIZE(du_resources),
		.data = &koelsch_du_pdata,
		.size_data = sizeof(koelsch_du_pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	platform_device_register_full(&info);
}

/* LEDS */
static struct gpio_led koelsch_leds[] = {
	{
		.name		= "led8",
		.gpio		= RCAR_GP_PIN(2, 21),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led7",
		.gpio		= RCAR_GP_PIN(2, 20),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led6",
		.gpio		= RCAR_GP_PIN(2, 19),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
};

static const struct gpio_led_platform_data koelsch_leds_pdata __initconst = {
	.leds		= koelsch_leds,
	.num_leds	= ARRAY_SIZE(koelsch_leds),
};

/* GPIO KEY */
#define GPIO_KEY(c, g, d, ...) \
	{ .code = c, .gpio = g, .desc = d, .active_low = 1, \
	  .wakeup = 1, .debounce_interval = 20 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_4,		RCAR_GP_PIN(5, 3),	"SW2-pin4"),
	GPIO_KEY(KEY_3,		RCAR_GP_PIN(5, 2),	"SW2-pin3"),
	GPIO_KEY(KEY_2,		RCAR_GP_PIN(5, 1),	"SW2-pin2"),
	GPIO_KEY(KEY_1,		RCAR_GP_PIN(5, 0),	"SW2-pin1"),
	GPIO_KEY(KEY_G,		RCAR_GP_PIN(7, 6),	"SW36"),
	GPIO_KEY(KEY_F,		RCAR_GP_PIN(7, 5),	"SW35"),
	GPIO_KEY(KEY_E,		RCAR_GP_PIN(7, 4),	"SW34"),
	GPIO_KEY(KEY_D,		RCAR_GP_PIN(7, 3),	"SW33"),
	GPIO_KEY(KEY_C,		RCAR_GP_PIN(7, 2),	"SW32"),
	GPIO_KEY(KEY_B,		RCAR_GP_PIN(7, 1),	"SW31"),
	GPIO_KEY(KEY_A,		RCAR_GP_PIN(7, 0),	"SW30"),
};

static const struct gpio_keys_platform_data koelsch_keys_pdata __initconst = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

static const struct pinctrl_map koelsch_pinctrl_map[] = {
	/* DU */
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_rgb666", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_sync", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_clk_out_0", "du"),
	/* SCIF0 (CN19: DEBUG SERIAL0) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.6", "pfc-r8a7791",
				  "scif0_data_d", "scif0"),
	/* SCIF1 (CN20: DEBUG SERIAL1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.7", "pfc-r8a7791",
				  "scif1_data_d", "scif1"),
};

static void __init koelsch_add_standard_devices(void)
{
	r8a7791_clock_init();
	pinctrl_register_mappings(koelsch_pinctrl_map,
				  ARRAY_SIZE(koelsch_pinctrl_map));
	r8a7791_pinmux_init();
	r8a7791_add_standard_devices();
	platform_device_register_data(&platform_bus, "leds-gpio", -1,
				      &koelsch_leds_pdata,
				      sizeof(koelsch_leds_pdata));
	platform_device_register_data(&platform_bus, "gpio-keys", -1,
				      &koelsch_keys_pdata,
				      sizeof(koelsch_keys_pdata));

	koelsch_add_du_device();
}

static const char * const koelsch_boards_compat_dt[] __initconst = {
	"renesas,koelsch",
	NULL,
};

DT_MACHINE_START(KOELSCH_DT, "koelsch")
	.smp		= smp_ops(r8a7791_smp_ops),
	.init_early	= r8a7791_init_early,
	.init_time	= rcar_gen2_timer_init,
	.init_machine	= koelsch_add_standard_devices,
	.init_late	= shmobile_init_late,
	.dt_compat	= koelsch_boards_compat_dt,
MACHINE_END
