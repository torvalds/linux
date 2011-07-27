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

#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <video/platform_lcd.h>

#include <mach/hardware.h>
#include <mach/iomux-mx25.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/mx25.h>
#include <mach/audmux.h>

#include "devices-imx25.h"

static iomux_v3_cfg_t eukrea_mbimxsd_pads[] = {
	/* LCD */
	MX25_PAD_LD0__LD0,
	MX25_PAD_LD1__LD1,
	MX25_PAD_LD2__LD2,
	MX25_PAD_LD3__LD3,
	MX25_PAD_LD4__LD4,
	MX25_PAD_LD5__LD5,
	MX25_PAD_LD6__LD6,
	MX25_PAD_LD7__LD7,
	MX25_PAD_LD8__LD8,
	MX25_PAD_LD9__LD9,
	MX25_PAD_LD10__LD10,
	MX25_PAD_LD11__LD11,
	MX25_PAD_LD12__LD12,
	MX25_PAD_LD13__LD13,
	MX25_PAD_LD14__LD14,
	MX25_PAD_LD15__LD15,
	MX25_PAD_GPIO_E__LD16,
	MX25_PAD_GPIO_F__LD17,
	MX25_PAD_HSYNC__HSYNC,
	MX25_PAD_VSYNC__VSYNC,
	MX25_PAD_LSCLK__LSCLK,
	MX25_PAD_OE_ACD__OE_ACD,
	MX25_PAD_CONTRAST__CONTRAST,
	/* LCD_PWR */
	MX25_PAD_PWM__GPIO_1_26,
	/* LED */
	MX25_PAD_POWER_FAIL__GPIO_3_19,
	/* SWITCH */
	MX25_PAD_VSTBY_ACK__GPIO_3_18,
	/* UART2 */
	MX25_PAD_UART2_RTS__UART2_RTS,
	MX25_PAD_UART2_CTS__UART2_CTS,
	MX25_PAD_UART2_TXD__UART2_TXD,
	MX25_PAD_UART2_RXD__UART2_RXD,
	/* SD1 */
	MX25_PAD_SD1_CMD__SD1_CMD,
	MX25_PAD_SD1_CLK__SD1_CLK,
	MX25_PAD_SD1_DATA0__SD1_DATA0,
	MX25_PAD_SD1_DATA1__SD1_DATA1,
	MX25_PAD_SD1_DATA2__SD1_DATA2,
	MX25_PAD_SD1_DATA3__SD1_DATA3,
	/* SD1 CD */
	MX25_PAD_DE_B__GPIO_2_20,
	/* I2S */
	MX25_PAD_KPP_COL3__AUD5_TXFS,
	MX25_PAD_KPP_COL2__AUD5_TXC,
	MX25_PAD_KPP_COL1__AUD5_RXD,
	MX25_PAD_KPP_COL0__AUD5_TXD,
	/* CAN */
	MX25_PAD_GPIO_D__CAN2_RX,
	MX25_PAD_GPIO_C__CAN2_TX,
};

#define GPIO_LED1	83
#define GPIO_SWITCH1	82
#define GPIO_SD1CD	52
#define GPIO_LCDPWR	26

static struct imx_fb_videomode eukrea_mximxsd_modes[] = {
	{
		.mode	= {
			.name		= "CMO-QVGA",
			.refresh	= 60,
			.xres		= 320,
			.yres		= 240,
			.pixclock	= KHZ2PICOS(6500),
			.left_margin	= 30,
			.right_margin	= 38,
			.upper_margin	= 20,
			.lower_margin	= 3,
			.hsync_len	= 15,
			.vsync_len	= 4,
		},
		.bpp	= 16,
		.pcr	= 0xCAD08B80,
	}, {
		.mode = {
			.name		= "DVI-VGA",
			.refresh	= 60,
			.xres		= 640,
			.yres		= 480,
			.pixclock	= 32000,
			.hsync_len	= 7,
			.left_margin	= 100,
			.right_margin	= 100,
			.vsync_len	= 7,
			.upper_margin	= 7,
			.lower_margin	= 100,
		},
		.pcr		= 0xFA208B80,
		.bpp		= 16,
	}, {
		.mode = {
			.name		= "DVI-SVGA",
			.refresh	= 60,
			.xres		= 800,
			.yres		= 600,
			.pixclock	= 25000,
			.hsync_len	= 7,
			.left_margin	= 75,
			.right_margin	= 75,
			.vsync_len	= 7,
			.upper_margin	= 7,
			.lower_margin	= 75,
		},
		.pcr		= 0xFA208B80,
		.bpp		= 16,
	},
};

static const struct imx_fb_platform_data eukrea_mximxsd_fb_pdata __initconst = {
	.mode		= eukrea_mximxsd_modes,
	.num_modes	= ARRAY_SIZE(eukrea_mximxsd_modes),
	.pwmr		= 0x00A903FF,
	.lscr1		= 0x00120300,
	.dmacr		= 0x00040060,
};

static void eukrea_mbimxsd_lcd_power_set(struct plat_lcd_data *pd,
				   unsigned int power)
{
	if (power)
		gpio_direction_output(GPIO_LCDPWR, 1);
	else
		gpio_direction_output(GPIO_LCDPWR, 0);
}

static struct plat_lcd_data eukrea_mbimxsd_lcd_power_data = {
	.set_power		= eukrea_mbimxsd_lcd_power_set,
};

static struct platform_device eukrea_mbimxsd_lcd_powerdev = {
	.name			= "platform-lcd",
	.dev.platform_data	= &eukrea_mbimxsd_lcd_power_data,
};

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

static const struct gpio_keys_platform_data
		eukrea_mbimxsd_button_data __initconst = {
	.buttons	= eukrea_mbimxsd_gpio_buttons,
	.nbuttons	= ARRAY_SIZE(eukrea_mbimxsd_gpio_buttons),
};

static struct platform_device *platform_devices[] __initdata = {
	&eukrea_mbimxsd_leds_gpio,
	&eukrea_mbimxsd_lcd_powerdev,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct i2c_board_info eukrea_mbimxsd_i2c_devices[] = {
	{
		I2C_BOARD_INFO("tlv320aic23", 0x1a),
	},
};

static const
struct imx_ssi_platform_data eukrea_mbimxsd_ssi_pdata __initconst = {
	.flags = IMX_SSI_SYN | IMX_SSI_NET | IMX_SSI_USE_I2S_SLAVE,
};

static struct esdhc_platform_data sd1_pdata = {
	.cd_gpio = GPIO_SD1CD,
	.wp_gpio = -EINVAL,
};

/*
 * system init for baseboard usage. Will be called by cpuimx25 init.
 *
 * Add platform devices present on this baseboard and init
 * them from CPU side as far as required to use them later on
 */
void __init eukrea_mbimxsd25_baseboard_init(void)
{
	if (mxc_iomux_v3_setup_multiple_pads(eukrea_mbimxsd_pads,
			ARRAY_SIZE(eukrea_mbimxsd_pads)))
		printk(KERN_ERR "error setting mbimxsd pads !\n");

#if defined(CONFIG_SND_SOC_EUKREA_TLV320)
	/* SSI unit master I2S codec connected to SSI_AUD5*/
	mxc_audmux_v2_configure_port(0,
			MXC_AUDMUX_V2_PTCR_SYN |
			MXC_AUDMUX_V2_PTCR_TFSDIR |
			MXC_AUDMUX_V2_PTCR_TFSEL(4) |
			MXC_AUDMUX_V2_PTCR_TCLKDIR |
			MXC_AUDMUX_V2_PTCR_TCSEL(4),
			MXC_AUDMUX_V2_PDCR_RXDSEL(4)
	);
	mxc_audmux_v2_configure_port(4,
			MXC_AUDMUX_V2_PTCR_SYN,
			MXC_AUDMUX_V2_PDCR_RXDSEL(0)
	);
#endif

	imx25_add_imx_uart1(&uart_pdata);
	imx25_add_imx_fb(&eukrea_mximxsd_fb_pdata);
	imx25_add_imx_ssi(0, &eukrea_mbimxsd_ssi_pdata);

	imx25_add_flexcan1(NULL);
	imx25_add_sdhci_esdhc_imx(0, &sd1_pdata);

	gpio_request(GPIO_LED1, "LED1");
	gpio_direction_output(GPIO_LED1, 1);
	gpio_free(GPIO_LED1);

	gpio_request(GPIO_SWITCH1, "SWITCH1");
	gpio_direction_input(GPIO_SWITCH1);
	gpio_free(GPIO_SWITCH1);

	gpio_request(GPIO_LCDPWR, "LCDPWR");
	gpio_direction_output(GPIO_LCDPWR, 1);
	gpio_free(GPIO_SWITCH1);

	i2c_register_board_info(0, eukrea_mbimxsd_i2c_devices,
				ARRAY_SIZE(eukrea_mbimxsd_i2c_devices));

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
	imx_add_gpio_keys(&eukrea_mbimxsd_button_data);
}
