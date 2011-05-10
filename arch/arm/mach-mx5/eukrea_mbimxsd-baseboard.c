/*
 * Copyright (C) 2010 Eric Benard - eric@eukrea.com
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

#include <linux/types.h>
#include <linux/init.h>

#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx51.h>
#include <mach/audmux.h>

#include "devices-imx51.h"
#include "devices.h"

static iomux_v3_cfg_t eukrea_mbimxsd_pads[] = {
	/* LED */
	MX51_PAD_NANDF_D10__GPIO3_30,
	/* SWITCH */
	_MX51_PAD_NANDF_D9__GPIO3_31 | MUX_PAD_CTRL(PAD_CTL_PUS_22K_UP |
			PAD_CTL_PKE | PAD_CTL_SRE_FAST |
			PAD_CTL_DSE_HIGH | PAD_CTL_PUE | PAD_CTL_HYS),
	/* UART2 */
	MX51_PAD_UART2_RXD__UART2_RXD,
	MX51_PAD_UART2_TXD__UART2_TXD,
	/* UART 3 */
	MX51_PAD_UART3_RXD__UART3_RXD,
	MX51_PAD_UART3_TXD__UART3_TXD,
	MX51_PAD_KEY_COL4__UART3_RTS,
	MX51_PAD_KEY_COL5__UART3_CTS,
	/* SD */
	MX51_PAD_SD1_CMD__SD1_CMD,
	MX51_PAD_SD1_CLK__SD1_CLK,
	MX51_PAD_SD1_DATA0__SD1_DATA0,
	MX51_PAD_SD1_DATA1__SD1_DATA1,
	MX51_PAD_SD1_DATA2__SD1_DATA2,
	MX51_PAD_SD1_DATA3__SD1_DATA3,
	/* SD1 CD */
	_MX51_PAD_GPIO1_0__SD1_CD | MUX_PAD_CTRL(PAD_CTL_PUS_22K_UP |
			PAD_CTL_PKE | PAD_CTL_SRE_FAST |
			PAD_CTL_DSE_HIGH | PAD_CTL_PUE | PAD_CTL_HYS),
};

#define GPIO_LED1	IMX_GPIO_NR(3, 30)
#define GPIO_SWITCH1	IMX_GPIO_NR(3, 31)

static struct gpio_led eukrea_mbimxsd_leds[] = {
	{
		.name			= "led1",
		.default_trigger	= "heartbeat",
		.active_low		= 1,
		.gpio			= GPIO_LED1,
	},
};

static struct gpio_led_platform_data eukrea_mbimxsd_led_info = {
	.leds		= eukrea_mbimxsd_leds,
	.num_leds	= ARRAY_SIZE(eukrea_mbimxsd_leds),
};

static struct platform_device eukrea_mbimxsd_leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &eukrea_mbimxsd_led_info,
	},
};

static struct gpio_keys_button eukrea_mbimxsd_gpio_buttons[] = {
	{
		.gpio		= GPIO_SWITCH1,
		.code		= BTN_0,
		.desc		= "BP1",
		.active_low	= 1,
		.wakeup		= 1,
	},
};

static struct gpio_keys_platform_data eukrea_mbimxsd_button_data = {
	.buttons	= eukrea_mbimxsd_gpio_buttons,
	.nbuttons	= ARRAY_SIZE(eukrea_mbimxsd_gpio_buttons),
};

static struct platform_device eukrea_mbimxsd_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &eukrea_mbimxsd_button_data,
	}
};

static struct platform_device *platform_devices[] __initdata = {
	&eukrea_mbimxsd_leds_gpio,
	&eukrea_mbimxsd_button_device,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct i2c_board_info eukrea_mbimxsd_i2c_devices[] = {
	{
		I2C_BOARD_INFO("tlv320aic23", 0x1a),
	},
};

/*
 * system init for baseboard usage. Will be called by cpuimx51sd init.
 *
 * Add platform devices present on this baseboard and init
 * them from CPU side as far as required to use them later on
 */
void __init eukrea_mbimxsd51_baseboard_init(void)
{
	if (mxc_iomux_v3_setup_multiple_pads(eukrea_mbimxsd_pads,
			ARRAY_SIZE(eukrea_mbimxsd_pads)))
		printk(KERN_ERR "error setting mbimxsd pads !\n");

	imx51_add_imx_uart(1, NULL);
	imx51_add_imx_uart(2, &uart_pdata);

	imx51_add_sdhci_esdhc_imx(0, NULL);

	gpio_request(GPIO_LED1, "LED1");
	gpio_direction_output(GPIO_LED1, 1);
	gpio_free(GPIO_LED1);

	gpio_request(GPIO_SWITCH1, "SWITCH1");
	gpio_direction_input(GPIO_SWITCH1);
	gpio_free(GPIO_SWITCH1);

	i2c_register_board_info(0, eukrea_mbimxsd_i2c_devices,
				ARRAY_SIZE(eukrea_mbimxsd_i2c_devices));

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
}
