/*
 * board-omap3pandora.c (Pandora Handheld Console)
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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/regulator/machine.h>
#include <linux/i2c/twl4030.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/gpio_keys.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/board.h>
#include <mach/common.h>
#include <mach/gpio.h>
#include <mach/hardware.h>
#include <mach/mcspi.h>
#include <mach/usb.h>
#include <mach/mux.h>

#include "sdram-micron-mt46h32m32lf-6.h"
#include "mmc-twl4030.h"

#define OMAP3_PANDORA_TS_GPIO		94

/* hardware debounce: (value + 1) * 31us */
#define GPIO_DEBOUNCE_TIME		127

static struct gpio_led pandora_gpio_leds[] = {
	{
		.name			= "pandora::sd1",
		.default_trigger	= "mmc0",
		.gpio			= 128,
	}, {
		.name			= "pandora::sd2",
		.default_trigger	= "mmc1",
		.gpio			= 129,
	}, {
		.name			= "pandora::bluetooth",
		.gpio			= 158,
	}, {
		.name			= "pandora::wifi",
		.gpio			= 159,
	},
};

static struct gpio_led_platform_data pandora_gpio_led_data = {
	.leds		= pandora_gpio_leds,
	.num_leds	= ARRAY_SIZE(pandora_gpio_leds),
};

static struct platform_device pandora_leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &pandora_gpio_led_data,
	},
};

#define GPIO_BUTTON(gpio_num, ev_type, ev_code, act_low, descr)	\
{								\
	.gpio		= gpio_num,				\
	.type		= ev_type,				\
	.code		= ev_code,				\
	.active_low	= act_low,				\
	.desc		= "btn " descr,				\
}

#define GPIO_BUTTON_LOW(gpio_num, event_code, description)	\
	GPIO_BUTTON(gpio_num, EV_KEY, event_code, 1, description)

static struct gpio_keys_button pandora_gpio_keys[] = {
	GPIO_BUTTON_LOW(110,	KEY_UP,		"up"),
	GPIO_BUTTON_LOW(103,	KEY_DOWN,	"down"),
	GPIO_BUTTON_LOW(96,	KEY_LEFT,	"left"),
	GPIO_BUTTON_LOW(98,	KEY_RIGHT,	"right"),
	GPIO_BUTTON_LOW(111,	BTN_A,		"a"),
	GPIO_BUTTON_LOW(106,	BTN_B,		"b"),
	GPIO_BUTTON_LOW(109,	BTN_X,		"x"),
	GPIO_BUTTON_LOW(101,	BTN_Y,		"y"),
	GPIO_BUTTON_LOW(102,	BTN_TL,		"l"),
	GPIO_BUTTON_LOW(97,	BTN_TL2,	"l2"),
	GPIO_BUTTON_LOW(105,	BTN_TR,		"r"),
	GPIO_BUTTON_LOW(107,	BTN_TR2,	"r2"),
	GPIO_BUTTON_LOW(104,	KEY_LEFTCTRL,	"ctrl"),
	GPIO_BUTTON_LOW(99,	KEY_MENU,	"menu"),
	GPIO_BUTTON_LOW(176,	KEY_COFFEE,	"hold"),
	GPIO_BUTTON(100, EV_KEY, KEY_LEFTALT, 0, "alt"),
	GPIO_BUTTON(108, EV_SW, SW_LID, 1, "lid"),
};

static struct gpio_keys_platform_data pandora_gpio_key_info = {
	.buttons	= pandora_gpio_keys,
	.nbuttons	= ARRAY_SIZE(pandora_gpio_keys),
};

static struct platform_device pandora_keys_gpio = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &pandora_gpio_key_info,
	},
};

static void __init pandora_keys_gpio_init(void)
{
	/* set debounce time for GPIO banks 4 and 6 */
	omap_set_gpio_debounce_time(32 * 3, GPIO_DEBOUNCE_TIME);
	omap_set_gpio_debounce_time(32 * 5, GPIO_DEBOUNCE_TIME);
}

static int board_keymap[] = {
	/* row, col, code */
	KEY(0, 0, KEY_9),
	KEY(0, 1, KEY_8),
	KEY(0, 2, KEY_I),
	KEY(0, 3, KEY_J),
	KEY(0, 4, KEY_N),
	KEY(0, 5, KEY_M),
	KEY(1, 0, KEY_0),
	KEY(1, 1, KEY_7),
	KEY(1, 2, KEY_U),
	KEY(1, 3, KEY_H),
	KEY(1, 4, KEY_B),
	KEY(1, 5, KEY_SPACE),
	KEY(2, 0, KEY_BACKSPACE),
	KEY(2, 1, KEY_6),
	KEY(2, 2, KEY_Y),
	KEY(2, 3, KEY_G),
	KEY(2, 4, KEY_V),
	KEY(2, 5, KEY_FN),
	KEY(3, 0, KEY_O),
	KEY(3, 1, KEY_5),
	KEY(3, 2, KEY_T),
	KEY(3, 3, KEY_F),
	KEY(3, 4, KEY_C),
	KEY(4, 0, KEY_P),
	KEY(4, 1, KEY_4),
	KEY(4, 2, KEY_R),
	KEY(4, 3, KEY_D),
	KEY(4, 4, KEY_X),
	KEY(5, 0, KEY_K),
	KEY(5, 1, KEY_3),
	KEY(5, 2, KEY_E),
	KEY(5, 3, KEY_S),
	KEY(5, 4, KEY_Z),
	KEY(6, 0, KEY_L),
	KEY(6, 1, KEY_2),
	KEY(6, 2, KEY_W),
	KEY(6, 3, KEY_A),
	KEY(6, 4, KEY_DOT),
	KEY(7, 0, KEY_ENTER),
	KEY(7, 1, KEY_1),
	KEY(7, 2, KEY_Q),
	KEY(7, 3, KEY_LEFTSHIFT),
	KEY(7, 4, KEY_COMMA),
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data pandora_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 8,
	.cols		= 6,
	.rep		= 1,
};

static struct twl4030_hsmmc_info omap3pandora_mmc[] = {
	{
		.mmc		= 1,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= 126,
		.ext_clock	= 0,
	},
	{
		.mmc		= 2,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= 127,
		.ext_clock	= 1,
		.transceiver	= true,
	},
	{
		.mmc		= 3,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
	{}	/* Terminator */
};

static struct regulator_consumer_supply pandora_vmmc1_supply = {
	.supply			= "vmmc",
};

static struct regulator_consumer_supply pandora_vmmc2_supply = {
	.supply			= "vmmc",
};

static int omap3pandora_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	/* gpio + {0,1} is "mmc{0,1}_cd" (input/IRQ) */
	omap3pandora_mmc[0].gpio_cd = gpio + 0;
	omap3pandora_mmc[1].gpio_cd = gpio + 1;
	twl4030_mmc_init(omap3pandora_mmc);

	/* link regulators to MMC adapters */
	pandora_vmmc1_supply.dev = omap3pandora_mmc[0].dev;
	pandora_vmmc2_supply.dev = omap3pandora_mmc[1].dev;

	return 0;
}

static struct twl4030_gpio_platform_data omap3pandora_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.setup		= omap3pandora_twl_gpio_setup,
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data pandora_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &pandora_vmmc1_supply,
};

/* VMMC2 for MMC2 pins CMD, CLK, DAT0..DAT3 (max 100 mA) */
static struct regulator_init_data pandora_vmmc2 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &pandora_vmmc2_supply,
};

static struct twl4030_usb_data omap3pandora_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static struct twl4030_platform_data omap3pandora_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,
	.gpio		= &omap3pandora_gpio_data,
	.usb		= &omap3pandora_usb_data,
	.vmmc1		= &pandora_vmmc1,
	.vmmc2		= &pandora_vmmc2,
	.keypad		= &pandora_kp_data,
};

static struct i2c_board_info __initdata omap3pandora_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("tps65950", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &omap3pandora_twldata,
	},
};

static int __init omap3pandora_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, omap3pandora_i2c_boardinfo,
			ARRAY_SIZE(omap3pandora_i2c_boardinfo));
	/* i2c2 pins are not connected */
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static void __init omap3pandora_ads7846_init(void)
{
	int gpio = OMAP3_PANDORA_TS_GPIO;
	int ret;

	ret = gpio_request(gpio, "ads7846_pen_down");
	if (ret < 0) {
		printk(KERN_ERR "Failed to request GPIO %d for "
				"ads7846 pen down IRQ\n", gpio);
		return;
	}

	gpio_direction_input(gpio);
}

static int ads7846_get_pendown_state(void)
{
	return !gpio_get_value(OMAP3_PANDORA_TS_GPIO);
}

static struct ads7846_platform_data ads7846_config = {
	.x_max			= 0x0fff,
	.y_max			= 0x0fff,
	.x_plate_ohms		= 180,
	.pressure_max		= 255,
	.debounce_max		= 10,
	.debounce_tol		= 3,
	.debounce_rep		= 1,
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
};

static struct omap2_mcspi_device_config ads7846_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,	/* 0: slave, 1: master */
};

static struct spi_board_info omap3pandora_spi_board_info[] __initdata = {
	{
		.modalias		= "ads7846",
		.bus_num		= 1,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &ads7846_mcspi_config,
		.irq			= OMAP_GPIO_IRQ(OMAP3_PANDORA_TS_GPIO),
		.platform_data		= &ads7846_config,
	}
};

static struct platform_device omap3pandora_lcd_device = {
	.name		= "pandora_lcd",
	.id		= -1,
};

static struct omap_lcd_config omap3pandora_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel omap3pandora_config[] __initdata = {
	{ OMAP_TAG_LCD,		&omap3pandora_lcd_config },
};

static void __init omap3pandora_init_irq(void)
{
	omap_board_config = omap3pandora_config;
	omap_board_config_size = ARRAY_SIZE(omap3pandora_config);
	omap2_init_common_hw(mt46h32m32lf6_sdrc_params,
			     mt46h32m32lf6_sdrc_params);
	omap_init_irq();
	omap_gpio_init();
}

static struct platform_device *omap3pandora_devices[] __initdata = {
	&omap3pandora_lcd_device,
	&pandora_leds_gpio,
	&pandora_keys_gpio,
};

static void __init omap3pandora_init(void)
{
	omap3pandora_i2c_init();
	platform_add_devices(omap3pandora_devices,
			ARRAY_SIZE(omap3pandora_devices));
	omap_serial_init();
	spi_register_board_info(omap3pandora_spi_board_info,
			ARRAY_SIZE(omap3pandora_spi_board_info));
	omap3pandora_ads7846_init();
	pandora_keys_gpio_init();
	usb_musb_init();

	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_cfg_reg(H16_34XX_SDRC_CKE0);
	omap_cfg_reg(H17_34XX_SDRC_CKE1);
}

static void __init omap3pandora_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(OMAP3_PANDORA, "Pandora Handheld Console")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap3pandora_map_io,
	.init_irq	= omap3pandora_init_irq,
	.init_machine	= omap3pandora_init,
	.timer		= &omap_timer,
MACHINE_END
