/*
 *  Atheros PB44 reference board support
 *
 *  Copyright (C) 2009-2010 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/i2c/pcf857x.h>

#include "machtypes.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-spi.h"
#include "dev-usb.h"
#include "pci.h"

#define PB44_GPIO_I2C_SCL	0
#define PB44_GPIO_I2C_SDA	1

#define PB44_GPIO_EXP_BASE	16
#define PB44_GPIO_SW_RESET	(PB44_GPIO_EXP_BASE + 6)
#define PB44_GPIO_SW_JUMP	(PB44_GPIO_EXP_BASE + 8)
#define PB44_GPIO_LED_JUMP1	(PB44_GPIO_EXP_BASE + 9)
#define PB44_GPIO_LED_JUMP2	(PB44_GPIO_EXP_BASE + 10)

#define PB44_KEYS_POLL_INTERVAL		20	/* msecs */
#define PB44_KEYS_DEBOUNCE_INTERVAL	(3 * PB44_KEYS_POLL_INTERVAL)

static struct i2c_gpio_platform_data pb44_i2c_gpio_data = {
	.sda_pin        = PB44_GPIO_I2C_SDA,
	.scl_pin        = PB44_GPIO_I2C_SCL,
};

static struct platform_device pb44_i2c_gpio_device = {
	.name		= "i2c-gpio",
	.id		= 0,
	.dev = {
		.platform_data	= &pb44_i2c_gpio_data,
	}
};

static struct pcf857x_platform_data pb44_pcf857x_data = {
	.gpio_base	= PB44_GPIO_EXP_BASE,
};

static struct i2c_board_info pb44_i2c_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("pcf8575", 0x20),
		.platform_data  = &pb44_pcf857x_data,
	},
};

static struct gpio_led pb44_leds_gpio[] __initdata = {
	{
		.name		= "pb44:amber:jump1",
		.gpio		= PB44_GPIO_LED_JUMP1,
		.active_low	= 1,
	}, {
		.name		= "pb44:green:jump2",
		.gpio		= PB44_GPIO_LED_JUMP2,
		.active_low	= 1,
	},
};

static struct gpio_keys_button pb44_gpio_keys[] __initdata = {
	{
		.desc		= "soft_reset",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = PB44_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= PB44_GPIO_SW_RESET,
		.active_low	= 1,
	} , {
		.desc		= "jumpstart",
		.type		= EV_KEY,
		.code		= KEY_WPS_BUTTON,
		.debounce_interval = PB44_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= PB44_GPIO_SW_JUMP,
		.active_low	= 1,
	}
};

static struct spi_board_info pb44_spi_info[] = {
	{
		.bus_num	= 0,
		.chip_select	= 0,
		.max_speed_hz	= 25000000,
		.modalias	= "m25p64",
	},
};

static struct ath79_spi_platform_data pb44_spi_data = {
	.bus_num		= 0,
	.num_chipselect		= 1,
};

static void __init pb44_init(void)
{
	i2c_register_board_info(0, pb44_i2c_board_info,
				ARRAY_SIZE(pb44_i2c_board_info));
	platform_device_register(&pb44_i2c_gpio_device);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(pb44_leds_gpio),
				 pb44_leds_gpio);
	ath79_register_gpio_keys_polled(-1, PB44_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(pb44_gpio_keys),
					pb44_gpio_keys);
	ath79_register_spi(&pb44_spi_data, pb44_spi_info,
			   ARRAY_SIZE(pb44_spi_info));
	ath79_register_usb();
	ath79_register_pci();
}

MIPS_MACHINE(ATH79_MACH_PB44, "PB44", "Atheros PB44 reference board",
	     pb44_init);
