/*
 * Copyright (C) 2009 Eric Benard - eric@eukrea.com
 *
 * Based on pcm970-baseboard.c which is :
 * Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>

#include <asm/mach/arch.h>

#include <mach/common.h>
#include <mach/iomux-mx27.h>
#include <mach/imxfb.h>
#include <mach/hardware.h>
#include <mach/mmc.h>
#include <mach/imx-uart.h>

#include "devices.h"

static int eukrea_mbimx27_pins[] = {
	/* UART2 */
	PE3_PF_UART2_CTS,
	PE4_PF_UART2_RTS,
	PE6_PF_UART2_TXD,
	PE7_PF_UART2_RXD,
	/* UART3 */
	PE8_PF_UART3_TXD,
	PE9_PF_UART3_RXD,
	PE10_PF_UART3_CTS,
	PE11_PF_UART3_RTS,
	/* UART4 */
	PB26_AF_UART4_RTS,
	PB28_AF_UART4_TXD,
	PB29_AF_UART4_CTS,
	PB31_AF_UART4_RXD,
	/* SDHC1*/
	PE18_PF_SD1_D0,
	PE19_PF_SD1_D1,
	PE20_PF_SD1_D2,
	PE21_PF_SD1_D3,
	PE22_PF_SD1_CMD,
	PE23_PF_SD1_CLK,
	/* display */
	PA5_PF_LSCLK,
	PA6_PF_LD0,
	PA7_PF_LD1,
	PA8_PF_LD2,
	PA9_PF_LD3,
	PA10_PF_LD4,
	PA11_PF_LD5,
	PA12_PF_LD6,
	PA13_PF_LD7,
	PA14_PF_LD8,
	PA15_PF_LD9,
	PA16_PF_LD10,
	PA17_PF_LD11,
	PA18_PF_LD12,
	PA19_PF_LD13,
	PA20_PF_LD14,
	PA21_PF_LD15,
	PA22_PF_LD16,
	PA23_PF_LD17,
	PA28_PF_HSYNC,
	PA29_PF_VSYNC,
	PA30_PF_CONTRAST,
	PA31_PF_OE_ACD,
	/* SPI1 */
	PD28_PF_CSPI1_SS0,
	PD29_PF_CSPI1_SCLK,
	PD30_PF_CSPI1_MISO,
	PD31_PF_CSPI1_MOSI,
};

static struct gpio_led gpio_leds[] = {
	{
		.name			= "led1",
		.default_trigger	= "heartbeat",
		.active_low		= 1,
		.gpio			= GPIO_PORTF | 16,
	},
	{
		.name			= "led2",
		.default_trigger	= "none",
		.active_low		= 1,
		.gpio			= GPIO_PORTF | 19,
	},
	{
		.name			= "backlight",
		.default_trigger	= "backlight",
		.active_low		= 0,
		.gpio			= GPIO_PORTE | 5,
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

static struct imx_fb_videomode eukrea_mbimx27_modes[] = {
	{
		.mode = {
			.name		= "CMO-QGVA",
			.refresh	= 60,
			.xres		= 320,
			.yres		= 240,
			.pixclock	= 156000,
			.hsync_len	= 30,
			.left_margin	= 38,
			.right_margin	= 20,
			.vsync_len	= 3,
			.upper_margin	= 15,
			.lower_margin	= 4,
		},
		.pcr		= 0xFAD08B80,
		.bpp		= 16,
	},
};

static struct imx_fb_platform_data eukrea_mbimx27_fb_data = {
	.mode = eukrea_mbimx27_modes,
	.num_modes = ARRAY_SIZE(eukrea_mbimx27_modes),

	.pwmr		= 0x00A903FF,
	.lscr1		= 0x00120300,
	.dmacr		= 0x00040060,
};

static struct imxuart_platform_data uart_pdata[] = {
	{
		.flags = IMXUART_HAVE_RTSCTS,
	},
	{
		.flags = IMXUART_HAVE_RTSCTS,
	},
};

#if defined(CONFIG_TOUCHSCREEN_ADS7846)
	|| defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)

#define ADS7846_PENDOWN (GPIO_PORTD | 25)

static void ads7846_dev_init(void)
{
	if (gpio_request(ADS7846_PENDOWN, "ADS7846 pendown") < 0) {
		printk(KERN_ERR "can't get ads746 pen down GPIO\n");
		return;
	}

	gpio_direction_input(ADS7846_PENDOWN);
}

static int ads7846_get_pendown_state(void)
{
	return !gpio_get_value(ADS7846_PENDOWN);
}

static struct ads7846_platform_data ads7846_config __initdata = {
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
};

static struct spi_board_info eukrea_mbimx27_spi_board_info[] __initdata = {
	[0] = {
		.modalias	= "ads7846",
		.bus_num	= 0,
		.chip_select	= 0,
		.max_speed_hz	= 1500000,
		.irq		= IRQ_GPIOD(25),
		.platform_data	= &ads7846_config,
		.mode           = SPI_MODE_2,
	},
};

static int eukrea_mbimx27_spi_cs[] = {GPIO_PORTD | 28};

static struct spi_imx_master eukrea_mbimx27_spi_0_data = {
	.chipselect	= eukrea_mbimx27_spi_cs,
	.num_chipselect = ARRAY_SIZE(eukrea_mbimx27_spi_cs),
};
#endif

static struct platform_device *platform_devices[] __initdata = {
	&leds_gpio,
};

/*
 * system init for baseboard usage. Will be called by cpuimx27 init.
 *
 * Add platform devices present on this baseboard and init
 * them from CPU side as far as required to use them later on
 */
void __init eukrea_mbimx27_baseboard_init(void)
{
	mxc_gpio_setup_multiple_pins(eukrea_mbimx27_pins,
		ARRAY_SIZE(eukrea_mbimx27_pins), "MBIMX27");

	mxc_register_device(&mxc_uart_device1, &uart_pdata[0]);
	mxc_register_device(&mxc_uart_device2, &uart_pdata[1]);

	mxc_register_device(&mxc_fb_device, &eukrea_mbimx27_fb_data);
	mxc_register_device(&mxc_sdhc_device0, NULL);

#if defined(CONFIG_TOUCHSCREEN_ADS7846)
	|| defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
	/* SPI and ADS7846 Touchscreen controler init */
	mxc_gpio_mode(GPIO_PORTD | 28 | GPIO_GPIO | GPIO_OUT);
	mxc_gpio_mode(GPIO_PORTD | 25 | GPIO_GPIO | GPIO_IN);
	mxc_register_device(&mxc_spi_device0, &eukrea_mbimx27_spi_0_data);
	spi_register_board_info(eukrea_mbimx27_spi_board_info,
			ARRAY_SIZE(eukrea_mbimx27_spi_board_info));
	ads7846_dev_init();
#endif

	/* Leds configuration */
	mxc_gpio_mode(GPIO_PORTF | 16 | GPIO_GPIO | GPIO_OUT);
	mxc_gpio_mode(GPIO_PORTF | 19 | GPIO_GPIO | GPIO_OUT);
	/* Backlight */
	mxc_gpio_mode(GPIO_PORTE | 5 | GPIO_GPIO | GPIO_OUT);

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
}
