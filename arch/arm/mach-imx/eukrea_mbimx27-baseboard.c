/*
 * Copyright (C) 2009-2010 Eric Benard - eric@eukrea.com
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
#include <linux/backlight.h>
#include <video/platform_lcd.h>
#include <linux/input/matrix_keypad.h>

#include <asm/mach/arch.h>

#include <mach/common.h>
#include <mach/iomux-mx27.h>
#include <mach/imxfb.h>
#include <mach/hardware.h>
#include <mach/mmc.h>
#include <mach/spi.h>
#include <mach/audmux.h>

#include "devices-imx27.h"
#include "devices.h"

static const int eukrea_mbimx27_pins[] __initconst = {
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
#if !defined(MACH_EUKREA_CPUIMX27_USEUART4)
	PB26_AF_UART4_RTS,
	PB28_AF_UART4_TXD,
	PB29_AF_UART4_CTS,
	PB31_AF_UART4_RXD,
#endif
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
	PD29_PF_CSPI1_SCLK,
	PD30_PF_CSPI1_MISO,
	PD31_PF_CSPI1_MOSI,
	/* SSI4 */
#if defined(CONFIG_SND_SOC_EUKREA_TLV320) \
	|| defined(CONFIG_SND_SOC_EUKREA_TLV320_MODULE)
	PC16_PF_SSI4_FS,
	PC17_PF_SSI4_RXD | GPIO_PUEN,
	PC18_PF_SSI4_TXD | GPIO_PUEN,
	PC19_PF_SSI4_CLK,
#endif
};

static const uint32_t eukrea_mbimx27_keymap[] = {
	KEY(0, 0, KEY_UP),
	KEY(0, 1, KEY_DOWN),
	KEY(1, 0, KEY_RIGHT),
	KEY(1, 1, KEY_LEFT),
};

static struct matrix_keymap_data eukrea_mbimx27_keymap_data = {
	.keymap         = eukrea_mbimx27_keymap,
	.keymap_size    = ARRAY_SIZE(eukrea_mbimx27_keymap),
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
			.name		= "CMO-QVGA",
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
	}, {
		.mode = {
			.name		= "DVI-VGA",
			.refresh	= 60,
			.xres		= 640,
			.yres		= 480,
			.pixclock	= 32000,
			.hsync_len	= 1,
			.left_margin	= 35,
			.right_margin	= 0,
			.vsync_len	= 1,
			.upper_margin	= 7,
			.lower_margin	= 0,
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
			.hsync_len	= 1,
			.left_margin	= 35,
			.right_margin	= 0,
			.vsync_len	= 1,
			.upper_margin	= 7,
			.lower_margin	= 0,
		},
		.pcr		= 0xFA208B80,
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

static void eukrea_mbimx27_bl_set_intensity(int intensity)
{
	if (intensity)
		gpio_direction_output(GPIO_PORTE | 5, 1);
	else
		gpio_direction_output(GPIO_PORTE | 5, 0);
}

static struct generic_bl_info eukrea_mbimx27_bl_info = {
	.name			= "eukrea_mbimx27-bl",
	.max_intensity		= 0xff,
	.default_intensity	= 0xff,
	.set_bl_intensity	= eukrea_mbimx27_bl_set_intensity,
};

static struct platform_device eukrea_mbimx27_bl_dev = {
	.name			= "generic-bl",
	.id			= 1,
	.dev = {
		.platform_data	= &eukrea_mbimx27_bl_info,
	},
};

static void eukrea_mbimx27_lcd_power_set(struct plat_lcd_data *pd,
				   unsigned int power)
{
	if (power)
		gpio_direction_output(GPIO_PORTA | 25, 1);
	else
		gpio_direction_output(GPIO_PORTA | 25, 0);
}

static struct plat_lcd_data eukrea_mbimx27_lcd_power_data = {
	.set_power		= eukrea_mbimx27_lcd_power_set,
};

static struct platform_device eukrea_mbimx27_lcd_powerdev = {
	.name			= "platform-lcd",
	.dev.platform_data	= &eukrea_mbimx27_lcd_power_data,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

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

static const struct spi_imx_master eukrea_mbimx27_spi0_data __initconst = {
	.chipselect	= eukrea_mbimx27_spi_cs,
	.num_chipselect = ARRAY_SIZE(eukrea_mbimx27_spi_cs),
};

static struct i2c_board_info eukrea_mbimx27_i2c_devices[] = {
	{
		I2C_BOARD_INFO("tlv320aic23", 0x1a),
	},
};

static struct platform_device *platform_devices[] __initdata = {
	&leds_gpio,
};

static struct imxmmc_platform_data sdhc_pdata = {
	.dat3_card_detect = 1,
};

static const
struct imx_ssi_platform_data eukrea_mbimx27_ssi_pdata __initconst = {
	.flags = IMX_SSI_DMA | IMX_SSI_USE_I2S_SLAVE,
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

#if defined(CONFIG_SND_SOC_EUKREA_TLV320) \
	|| defined(CONFIG_SND_SOC_EUKREA_TLV320_MODULE)
	/* SSI unit master I2S codec connected to SSI_PINS_4*/
	mxc_audmux_v1_configure_port(MX27_AUDMUX_HPCR1_SSI0,
			MXC_AUDMUX_V1_PCR_SYN |
			MXC_AUDMUX_V1_PCR_TFSDIR |
			MXC_AUDMUX_V1_PCR_TCLKDIR |
			MXC_AUDMUX_V1_PCR_RFSDIR |
			MXC_AUDMUX_V1_PCR_RCLKDIR |
			MXC_AUDMUX_V1_PCR_TFCSEL(MX27_AUDMUX_HPCR3_SSI_PINS_4) |
			MXC_AUDMUX_V1_PCR_RFCSEL(MX27_AUDMUX_HPCR3_SSI_PINS_4) |
			MXC_AUDMUX_V1_PCR_RXDSEL(MX27_AUDMUX_HPCR3_SSI_PINS_4)
	);
	mxc_audmux_v1_configure_port(MX27_AUDMUX_HPCR3_SSI_PINS_4,
			MXC_AUDMUX_V1_PCR_SYN |
			MXC_AUDMUX_V1_PCR_RXDSEL(MX27_AUDMUX_HPCR1_SSI0)
	);
#endif

	imx27_add_imx_uart1(&uart_pdata);
	imx27_add_imx_uart2(&uart_pdata);
#if !defined(MACH_EUKREA_CPUIMX27_USEUART4)
	imx27_add_imx_uart3(&uart_pdata);
#endif

	mxc_register_device(&mxc_fb_device, &eukrea_mbimx27_fb_data);
	mxc_register_device(&mxc_sdhc_device0, &sdhc_pdata);

	i2c_register_board_info(0, eukrea_mbimx27_i2c_devices,
				ARRAY_SIZE(eukrea_mbimx27_i2c_devices));

	imx27_add_imx_ssi(0, &eukrea_mbimx27_ssi_pdata);

#if defined(CONFIG_TOUCHSCREEN_ADS7846) \
	|| defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
	/* ADS7846 Touchscreen controller init */
	mxc_gpio_mode(GPIO_PORTD | 25 | GPIO_GPIO | GPIO_IN);
	ads7846_dev_init();
#endif

#if defined(CONFIG_SPI_IMX) || defined(CONFIG_SPI_IMX_MODULE)
	/* SPI_CS0 init */
	mxc_gpio_mode(GPIO_PORTD | 28 | GPIO_GPIO | GPIO_OUT);
	imx27_add_spi_imx0(&eukrea_mbimx27_spi0_data);
	spi_register_board_info(eukrea_mbimx27_spi_board_info,
			ARRAY_SIZE(eukrea_mbimx27_spi_board_info));
#endif

	/* Leds configuration */
	mxc_gpio_mode(GPIO_PORTF | 16 | GPIO_GPIO | GPIO_OUT);
	mxc_gpio_mode(GPIO_PORTF | 19 | GPIO_GPIO | GPIO_OUT);
	/* Backlight */
	mxc_gpio_mode(GPIO_PORTE | 5 | GPIO_GPIO | GPIO_OUT);
	gpio_request(GPIO_PORTE | 5, "backlight");
	platform_device_register(&eukrea_mbimx27_bl_dev);
	/* LCD Reset */
	mxc_gpio_mode(GPIO_PORTA | 25 | GPIO_GPIO | GPIO_OUT);
	gpio_request(GPIO_PORTA | 25, "lcd_enable");
	platform_device_register(&eukrea_mbimx27_lcd_powerdev);

	mxc_register_device(&imx_kpp_device, &eukrea_mbimx27_keymap_data);

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
}
