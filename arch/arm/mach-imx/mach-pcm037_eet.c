/*
 * Copyright (C) 2009
 * Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#include <mach/common.h>
#include <mach/iomux-mx3.h>

#include <asm/mach-types.h>

#include "pcm037.h"
#include "devices-imx31.h"

static unsigned int pcm037_eet_pins[] = {
	/* Reserve and hardwire GPIO 57 high - S6E63D6 chipselect */
	IOMUX_MODE(MX31_PIN_KEY_COL7, IOMUX_CONFIG_GPIO),
	/* GPIO keys */
	IOMUX_MODE(MX31_PIN_GPIO1_0,	IOMUX_CONFIG_GPIO), /* 0 */
	IOMUX_MODE(MX31_PIN_GPIO1_1,	IOMUX_CONFIG_GPIO), /* 1 */
	IOMUX_MODE(MX31_PIN_GPIO1_2,	IOMUX_CONFIG_GPIO), /* 2 */
	IOMUX_MODE(MX31_PIN_GPIO1_3,	IOMUX_CONFIG_GPIO), /* 3 */
	IOMUX_MODE(MX31_PIN_SVEN0,	IOMUX_CONFIG_GPIO), /* 32 */
	IOMUX_MODE(MX31_PIN_STX0,	IOMUX_CONFIG_GPIO), /* 33 */
	IOMUX_MODE(MX31_PIN_SRX0,	IOMUX_CONFIG_GPIO), /* 34 */
	IOMUX_MODE(MX31_PIN_SIMPD0,	IOMUX_CONFIG_GPIO), /* 35 */
	IOMUX_MODE(MX31_PIN_RTS1,	IOMUX_CONFIG_GPIO), /* 38 */
	IOMUX_MODE(MX31_PIN_CTS1,	IOMUX_CONFIG_GPIO), /* 39 */
	IOMUX_MODE(MX31_PIN_KEY_ROW4,	IOMUX_CONFIG_GPIO), /* 50 */
	IOMUX_MODE(MX31_PIN_KEY_ROW5,	IOMUX_CONFIG_GPIO), /* 51 */
	IOMUX_MODE(MX31_PIN_KEY_ROW6,	IOMUX_CONFIG_GPIO), /* 52 */
	IOMUX_MODE(MX31_PIN_KEY_ROW7,	IOMUX_CONFIG_GPIO), /* 53 */

	/* LEDs */
	IOMUX_MODE(MX31_PIN_DTR_DTE1,	IOMUX_CONFIG_GPIO), /* 44 */
	IOMUX_MODE(MX31_PIN_DSR_DTE1,	IOMUX_CONFIG_GPIO), /* 45 */
	IOMUX_MODE(MX31_PIN_KEY_COL5,	IOMUX_CONFIG_GPIO), /* 55 */
	IOMUX_MODE(MX31_PIN_KEY_COL6,	IOMUX_CONFIG_GPIO), /* 56 */
};

/* SPI */
static struct spi_board_info pcm037_spi_dev[] = {
	{
		.modalias	= "dac124s085",
		.max_speed_hz	= 400000,
		.bus_num	= 0,
		.chip_select	= 0,		/* Index in pcm037_spi1_cs[] */
		.mode		= SPI_CPHA,
	},
};

/* Platform Data for MXC CSPI */
static int pcm037_spi1_cs[] = {MXC_SPI_CS(1), IOMUX_TO_GPIO(MX31_PIN_KEY_COL7)};

static const struct spi_imx_master pcm037_spi1_pdata __initconst = {
	.chipselect = pcm037_spi1_cs,
	.num_chipselect = ARRAY_SIZE(pcm037_spi1_cs),
};

/* GPIO-keys input device */
static struct gpio_keys_button pcm037_gpio_keys[] = {
	{
		.type	= EV_KEY,
		.code	= KEY_L,
		.gpio	= 0,
		.desc	= "Wheel Manual",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= KEY_A,
		.gpio	= 1,
		.desc	= "Wheel AF",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= KEY_V,
		.gpio	= 2,
		.desc	= "Wheel View",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= KEY_M,
		.gpio	= 3,
		.desc	= "Wheel Menu",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= KEY_UP,
		.gpio	= 32,
		.desc	= "Nav Pad Up",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= KEY_RIGHT,
		.gpio	= 33,
		.desc	= "Nav Pad Right",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= KEY_DOWN,
		.gpio	= 34,
		.desc	= "Nav Pad Down",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= KEY_LEFT,
		.gpio	= 35,
		.desc	= "Nav Pad Left",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= KEY_ENTER,
		.gpio	= 38,
		.desc	= "Nav Pad Ok",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= KEY_O,
		.gpio	= 39,
		.desc	= "Wheel Off",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= BTN_FORWARD,
		.gpio	= 50,
		.desc	= "Focus Forward",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= BTN_BACK,
		.gpio	= 51,
		.desc	= "Focus Backward",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= BTN_MIDDLE,
		.gpio	= 52,
		.desc	= "Release Half",
		.wakeup	= 0,
	}, {
		.type	= EV_KEY,
		.code	= BTN_EXTRA,
		.gpio	= 53,
		.desc	= "Release Full",
		.wakeup	= 0,
	},
};

static const struct gpio_keys_platform_data
		pcm037_gpio_keys_platform_data __initconst = {
	.buttons	= pcm037_gpio_keys,
	.nbuttons	= ARRAY_SIZE(pcm037_gpio_keys),
	.rep		= 0, /* No auto-repeat */
};

int __init pcm037_eet_init_devices(void)
{
	if (pcm037_variant() != PCM037_EET)
		return 0;

	mxc_iomux_setup_multiple_pins(pcm037_eet_pins,
				ARRAY_SIZE(pcm037_eet_pins), "pcm037_eet");

	/* SPI */
	spi_register_board_info(pcm037_spi_dev, ARRAY_SIZE(pcm037_spi_dev));
	imx31_add_spi_imx0(&pcm037_spi1_pdata);

	imx_add_gpio_keys(&pcm037_gpio_keys_platform_data);

	return 0;
}
