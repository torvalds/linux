/*
 *  Atheros AP121 board support
 *
 *  Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include "machtypes.h"
#include "dev-gpio-buttons.h"
#include "dev-leds-gpio.h"
#include "dev-spi.h"
#include "dev-usb.h"
#include "dev-wmac.h"

#define AP121_GPIO_LED_WLAN		0
#define AP121_GPIO_LED_USB		1

#define AP121_GPIO_BTN_JUMPSTART	11
#define AP121_GPIO_BTN_RESET		12

#define AP121_KEYS_POLL_INTERVAL	20	/* msecs */
#define AP121_KEYS_DEBOUNCE_INTERVAL	(3 * AP121_KEYS_POLL_INTERVAL)

#define AP121_CAL_DATA_ADDR	0x1fff1000

static struct gpio_led ap121_leds_gpio[] __initdata = {
	{
		.name		= "ap121:green:usb",
		.gpio		= AP121_GPIO_LED_USB,
		.active_low	= 0,
	},
	{
		.name		= "ap121:green:wlan",
		.gpio		= AP121_GPIO_LED_WLAN,
		.active_low	= 0,
	},
};

static struct gpio_keys_button ap121_gpio_keys[] __initdata = {
	{
		.desc		= "jumpstart button",
		.type		= EV_KEY,
		.code		= KEY_WPS_BUTTON,
		.debounce_interval = AP121_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP121_GPIO_BTN_JUMPSTART,
		.active_low	= 1,
	},
	{
		.desc		= "reset button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = AP121_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP121_GPIO_BTN_RESET,
		.active_low	= 1,
	}
};

static struct spi_board_info ap121_spi_info[] = {
	{
		.bus_num	= 0,
		.chip_select	= 0,
		.max_speed_hz	= 25000000,
		.modalias	= "mx25l1606e",
	}
};

static struct ath79_spi_platform_data ap121_spi_data = {
	.bus_num	= 0,
	.num_chipselect	= 1,
};

static void __init ap121_setup(void)
{
	u8 *cal_data = (u8 *) KSEG1ADDR(AP121_CAL_DATA_ADDR);

	ath79_register_leds_gpio(-1, ARRAY_SIZE(ap121_leds_gpio),
				 ap121_leds_gpio);
	ath79_register_gpio_keys_polled(-1, AP121_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ap121_gpio_keys),
					ap121_gpio_keys);

	ath79_register_spi(&ap121_spi_data, ap121_spi_info,
			   ARRAY_SIZE(ap121_spi_info));
	ath79_register_usb();
	ath79_register_wmac(cal_data);
}

MIPS_MACHINE(ATH79_MACH_AP121, "AP121", "Atheros AP121 reference board",
	     ap121_setup);
