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
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <video/platform_lcd.h>
#include <linux/i2c.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx35.h>

#include "devices-imx35.h"

static const struct fb_videomode fb_modedb[] = {
	{
		.name		= "CMO-QVGA",
		.refresh	= 60,
		.xres		= 320,
		.yres		= 240,
		.pixclock	= KHZ2PICOS(6500),
		.left_margin	= 68,
		.right_margin	= 20,
		.upper_margin	= 15,
		.lower_margin	= 4,
		.hsync_len	= 30,
		.vsync_len	= 3,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	},
	{
		.name		= "DVI-VGA",
		.refresh	= 60,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 32000,
		.left_margin	= 100,
		.right_margin	= 100,
		.upper_margin	= 7,
		.lower_margin	= 100,
		.hsync_len	= 7,
		.vsync_len	= 7,
		.sync		= FB_SYNC_VERT_HIGH_ACT | FB_SYNC_HOR_HIGH_ACT |
				  FB_SYNC_OE_ACT_HIGH | FB_SYNC_CLK_INVERT,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	},
	{
		.name		= "DVI-SVGA",
		.refresh	= 60,
		.xres		= 800,
		.yres		= 600,
		.pixclock	= 25000,
		.left_margin	= 75,
		.right_margin	= 75,
		.upper_margin	= 7,
		.lower_margin	= 75,
		.hsync_len	= 7,
		.vsync_len	= 7,
		.sync		= FB_SYNC_VERT_HIGH_ACT | FB_SYNC_HOR_HIGH_ACT |
				  FB_SYNC_OE_ACT_HIGH | FB_SYNC_CLK_INVERT,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	},
};

static const struct ipu_platform_data mx3_ipu_data __initconst = {
	.irq_base = MXC_IPU_IRQ_START,
};

static struct mx3fb_platform_data mx3fb_pdata __initdata = {
	.name		= "CMO-QVGA",
	.mode		= fb_modedb,
	.num_modes	= ARRAY_SIZE(fb_modedb),
};

static iomux_v3_cfg_t eukrea_mbimxsd_pads[] = {
	/* LCD */
	MX35_PAD_LD0__IPU_DISPB_DAT_0,
	MX35_PAD_LD1__IPU_DISPB_DAT_1,
	MX35_PAD_LD2__IPU_DISPB_DAT_2,
	MX35_PAD_LD3__IPU_DISPB_DAT_3,
	MX35_PAD_LD4__IPU_DISPB_DAT_4,
	MX35_PAD_LD5__IPU_DISPB_DAT_5,
	MX35_PAD_LD6__IPU_DISPB_DAT_6,
	MX35_PAD_LD7__IPU_DISPB_DAT_7,
	MX35_PAD_LD8__IPU_DISPB_DAT_8,
	MX35_PAD_LD9__IPU_DISPB_DAT_9,
	MX35_PAD_LD10__IPU_DISPB_DAT_10,
	MX35_PAD_LD11__IPU_DISPB_DAT_11,
	MX35_PAD_LD12__IPU_DISPB_DAT_12,
	MX35_PAD_LD13__IPU_DISPB_DAT_13,
	MX35_PAD_LD14__IPU_DISPB_DAT_14,
	MX35_PAD_LD15__IPU_DISPB_DAT_15,
	MX35_PAD_LD16__IPU_DISPB_DAT_16,
	MX35_PAD_LD17__IPU_DISPB_DAT_17,
	MX35_PAD_D3_HSYNC__IPU_DISPB_D3_HSYNC,
	MX35_PAD_D3_FPSHIFT__IPU_DISPB_D3_CLK,
	MX35_PAD_D3_DRDY__IPU_DISPB_D3_DRDY,
	MX35_PAD_D3_VSYNC__IPU_DISPB_D3_VSYNC,
	/* Backlight */
	MX35_PAD_CONTRAST__IPU_DISPB_CONTR,
	/* LCD_PWR */
	MX35_PAD_D3_CLS__GPIO1_4,
	/* LED */
	MX35_PAD_LD23__GPIO3_29,
	/* SWITCH */
	MX35_PAD_LD19__GPIO3_25,
	/* UART2 */
	MX35_PAD_CTS2__UART2_CTS,
	MX35_PAD_RTS2__UART2_RTS,
	MX35_PAD_TXD2__UART2_TXD_MUX,
	MX35_PAD_RXD2__UART2_RXD_MUX,
	/* I2S */
	MX35_PAD_STXFS4__AUDMUX_AUD4_TXFS,
	MX35_PAD_STXD4__AUDMUX_AUD4_TXD,
	MX35_PAD_SRXD4__AUDMUX_AUD4_RXD,
	MX35_PAD_SCK4__AUDMUX_AUD4_TXC,
	/* CAN2 */
	MX35_PAD_TX5_RX0__CAN2_TXCAN,
	MX35_PAD_TX4_RX1__CAN2_RXCAN,
	/* SDCARD */
	MX35_PAD_SD1_CMD__ESDHC1_CMD,
	MX35_PAD_SD1_CLK__ESDHC1_CLK,
	MX35_PAD_SD1_DATA0__ESDHC1_DAT0,
	MX35_PAD_SD1_DATA1__ESDHC1_DAT1,
	MX35_PAD_SD1_DATA2__ESDHC1_DAT2,
	MX35_PAD_SD1_DATA3__ESDHC1_DAT3,
	/* SD1 CD */
	MX35_PAD_LD18__GPIO3_24,
};

#define GPIO_LED1	IMX_GPIO_NR(3, 29)
#define GPIO_SWITCH1	IMX_GPIO_NR(3, 25)
#define GPIO_LCDPWR	IMX_GPIO_NR(1, 4)
#define GPIO_SD1CD	IMX_GPIO_NR(3, 24)

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

static const struct gpio_led_platform_data
		eukrea_mbimxsd_led_info __initconst = {
	.leds		= eukrea_mbimxsd_leds,
	.num_leds	= ARRAY_SIZE(eukrea_mbimxsd_leds),
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
	.cd_type = ESDHC_CD_GPIO,
	.wp_type = ESDHC_WP_NONE,
};

/*
 * system init for baseboard usage. Will be called by cpuimx35 init.
 *
 * Add platform devices present on this baseboard and init
 * them from CPU side as far as required to use them later on
 */
void __init eukrea_mbimxsd35_baseboard_init(void)
{
	if (mxc_iomux_v3_setup_multiple_pads(eukrea_mbimxsd_pads,
			ARRAY_SIZE(eukrea_mbimxsd_pads)))
		printk(KERN_ERR "error setting mbimxsd pads !\n");

	imx35_add_imx_uart1(&uart_pdata);
	imx35_add_ipu_core(&mx3_ipu_data);
	imx35_add_mx3_sdc_fb(&mx3fb_pdata);

	imx35_add_imx_ssi(0, &eukrea_mbimxsd_ssi_pdata);

	imx35_add_flexcan1(NULL);
	imx35_add_sdhci_esdhc_imx(0, &sd1_pdata);

	gpio_request(GPIO_LED1, "LED1");
	gpio_direction_output(GPIO_LED1, 1);
	gpio_free(GPIO_LED1);

	gpio_request(GPIO_SWITCH1, "SWITCH1");
	gpio_direction_input(GPIO_SWITCH1);
	gpio_free(GPIO_SWITCH1);

	gpio_request(GPIO_LCDPWR, "LCDPWR");
	gpio_direction_output(GPIO_LCDPWR, 1);

	i2c_register_board_info(0, eukrea_mbimxsd_i2c_devices,
				ARRAY_SIZE(eukrea_mbimxsd_i2c_devices));

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
	gpio_led_register_device(-1, &eukrea_mbimxsd_led_info);
	imx_add_gpio_keys(&eukrea_mbimxsd_button_data);
}
