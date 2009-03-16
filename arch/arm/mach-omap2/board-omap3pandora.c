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
#include <linux/i2c/twl4030.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/board.h>
#include <mach/common.h>
#include <mach/gpio.h>
#include <mach/hardware.h>
#include <mach/mcspi.h>
#include <mach/usb.h>

#include "mmc-twl4030.h"

#define OMAP3_PANDORA_TS_GPIO		94

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

static struct omap_uart_config omap3pandora_uart_config __initdata = {
	.enabled_uarts	= (1 << 2), /* UART3 */
};

static int omap3pandora_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	/* gpio + {0,1} is "mmc{0,1}_cd" (input/IRQ) */
	omap3pandora_mmc[0].gpio_cd = gpio + 0;
	omap3pandora_mmc[1].gpio_cd = gpio + 1;
	twl4030_mmc_init(omap3pandora_mmc);

	return 0;
}

static struct twl4030_gpio_platform_data omap3pandora_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.setup		= omap3pandora_twl_gpio_setup,
};

static struct twl4030_usb_data omap3pandora_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static struct twl4030_platform_data omap3pandora_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,
	.gpio		= &omap3pandora_gpio_data,
	.usb		= &omap3pandora_usb_data,
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

static void __init omap3pandora_init_irq(void)
{
	omap2_init_common_hw(NULL);
	omap_init_irq();
	omap_gpio_init();
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
	{ OMAP_TAG_UART,	&omap3pandora_uart_config },
	{ OMAP_TAG_LCD,		&omap3pandora_lcd_config },
};

static struct platform_device *omap3pandora_devices[] __initdata = {
	&omap3pandora_lcd_device,
};

static void __init omap3pandora_init(void)
{
	omap3pandora_i2c_init();
	platform_add_devices(omap3pandora_devices,
			ARRAY_SIZE(omap3pandora_devices));
	omap_board_config = omap3pandora_config;
	omap_board_config_size = ARRAY_SIZE(omap3pandora_config);
	omap_serial_init();
	spi_register_board_info(omap3pandora_spi_board_info,
			ARRAY_SIZE(omap3pandora_spi_board_info));
	omap3pandora_ads7846_init();
	usb_musb_init();
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
