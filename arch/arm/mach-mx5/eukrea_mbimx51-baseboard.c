/*
 *
 * Copyright (C) 2010 Eric Bénard <eric@eukrea.com>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c/tsc2007.h>
#include <linux/leds.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx51.h>

#include <asm/mach/arch.h>

#include "devices-imx51.h"
#include "devices.h"

#define MBIMX51_TSC2007_GPIO	IMX_GPIO_NR(3, 30)
#define MBIMX51_TSC2007_IRQ	(MXC_INTERNAL_IRQS + MBIMX51_TSC2007_GPIO)
#define MBIMX51_LED0		IMX_GPIO_NR(3, 5)
#define MBIMX51_LED1		IMX_GPIO_NR(3, 6)
#define MBIMX51_LED2		IMX_GPIO_NR(3, 7)
#define MBIMX51_LED3		IMX_GPIO_NR(3, 8)

static struct gpio_led mbimx51_leds[] = {
	{
		.name			= "led0",
		.default_trigger	= "heartbeat",
		.active_low		= 1,
		.gpio			= MBIMX51_LED0,
	},
	{
		.name			= "led1",
		.default_trigger	= "nand-disk",
		.active_low		= 1,
		.gpio			= MBIMX51_LED1,
	},
	{
		.name			= "led2",
		.default_trigger	= "mmc0",
		.active_low		= 1,
		.gpio			= MBIMX51_LED2,
	},
	{
		.name			= "led3",
		.default_trigger	= "default-on",
		.active_low		= 1,
		.gpio			= MBIMX51_LED3,
	},
};

static struct gpio_led_platform_data mbimx51_leds_info = {
	.leds		= mbimx51_leds,
	.num_leds	= ARRAY_SIZE(mbimx51_leds),
};

static struct platform_device mbimx51_leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &mbimx51_leds_info,
	},
};

static struct platform_device *devices[] __initdata = {
	&mbimx51_leds_gpio,
};

static iomux_v3_cfg_t mbimx51_pads[] = {
	/* UART2 */
	MX51_PAD_UART2_RXD__UART2_RXD,
	MX51_PAD_UART2_TXD__UART2_TXD,

	/* UART3 */
	MX51_PAD_UART3_RXD__UART3_RXD,
	MX51_PAD_UART3_TXD__UART3_TXD,
	MX51_PAD_KEY_COL4__UART3_RTS,
	MX51_PAD_KEY_COL5__UART3_CTS,

	/* TSC2007 IRQ */
	MX51_PAD_NANDF_D10__GPIO3_30,

	/* LEDS */
	MX51_PAD_DISPB2_SER_DIN__GPIO3_5,
	MX51_PAD_DISPB2_SER_DIO__GPIO3_6,
	MX51_PAD_DISPB2_SER_CLK__GPIO3_7,
	MX51_PAD_DISPB2_SER_RS__GPIO3_8,

	/* KPP */
	MX51_PAD_KEY_ROW0__KEY_ROW0,
	MX51_PAD_KEY_ROW1__KEY_ROW1,
	MX51_PAD_KEY_ROW2__KEY_ROW2,
	MX51_PAD_KEY_ROW3__KEY_ROW3,
	MX51_PAD_KEY_COL0__KEY_COL0,
	MX51_PAD_KEY_COL1__KEY_COL1,
	MX51_PAD_KEY_COL2__KEY_COL2,
	MX51_PAD_KEY_COL3__KEY_COL3,

	/* SD 1 */
	MX51_PAD_SD1_CMD__SD1_CMD,
	MX51_PAD_SD1_CLK__SD1_CLK,
	MX51_PAD_SD1_DATA0__SD1_DATA0,
	MX51_PAD_SD1_DATA1__SD1_DATA1,
	MX51_PAD_SD1_DATA2__SD1_DATA2,
	MX51_PAD_SD1_DATA3__SD1_DATA3,

	/* SD 2 */
	MX51_PAD_SD2_CMD__SD2_CMD,
	MX51_PAD_SD2_CLK__SD2_CLK,
	MX51_PAD_SD2_DATA0__SD2_DATA0,
	MX51_PAD_SD2_DATA1__SD2_DATA1,
	MX51_PAD_SD2_DATA2__SD2_DATA2,
	MX51_PAD_SD2_DATA3__SD2_DATA3,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static int mbimx51_keymap[] = {
	KEY(0, 0, KEY_1),
	KEY(0, 1, KEY_2),
	KEY(0, 2, KEY_3),
	KEY(0, 3, KEY_UP),

	KEY(1, 0, KEY_4),
	KEY(1, 1, KEY_5),
	KEY(1, 2, KEY_6),
	KEY(1, 3, KEY_LEFT),

	KEY(2, 0, KEY_7),
	KEY(2, 1, KEY_8),
	KEY(2, 2, KEY_9),
	KEY(2, 3, KEY_RIGHT),

	KEY(3, 0, KEY_0),
	KEY(3, 1, KEY_DOWN),
	KEY(3, 2, KEY_ESC),
	KEY(3, 3, KEY_ENTER),
};

static const struct matrix_keymap_data mbimx51_map_data __initconst = {
	.keymap		= mbimx51_keymap,
	.keymap_size	= ARRAY_SIZE(mbimx51_keymap),
};

static int tsc2007_get_pendown_state(void)
{
	return !gpio_get_value(MBIMX51_TSC2007_GPIO);
}

struct tsc2007_platform_data tsc2007_data = {
	.model = 2007,
	.x_plate_ohms = 180,
	.get_pendown_state = tsc2007_get_pendown_state,
};

static struct i2c_board_info mbimx51_i2c_devices[] = {
	{
		I2C_BOARD_INFO("tsc2007", 0x49),
		.irq  = MBIMX51_TSC2007_IRQ,
		.platform_data = &tsc2007_data,
	}, {
		I2C_BOARD_INFO("tlv320aic23", 0x1a),
	},
};

/*
 * baseboard initialization.
 */
void __init eukrea_mbimx51_baseboard_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mbimx51_pads,
					ARRAY_SIZE(mbimx51_pads));

	imx51_add_imx_uart(1, NULL);
	imx51_add_imx_uart(2, &uart_pdata);

	gpio_request(MBIMX51_LED0, "LED0");
	gpio_direction_output(MBIMX51_LED0, 1);
	gpio_free(MBIMX51_LED0);
	gpio_request(MBIMX51_LED1, "LED1");
	gpio_direction_output(MBIMX51_LED1, 1);
	gpio_free(MBIMX51_LED1);
	gpio_request(MBIMX51_LED2, "LED2");
	gpio_direction_output(MBIMX51_LED2, 1);
	gpio_free(MBIMX51_LED2);
	gpio_request(MBIMX51_LED3, "LED3");
	gpio_direction_output(MBIMX51_LED3, 1);
	gpio_free(MBIMX51_LED3);

	platform_add_devices(devices, ARRAY_SIZE(devices));

	imx51_add_imx_keypad(&mbimx51_map_data);

	gpio_request(MBIMX51_TSC2007_GPIO, "tsc2007_irq");
	gpio_direction_input(MBIMX51_TSC2007_GPIO);
	irq_set_irq_type(MBIMX51_TSC2007_IRQ, IRQF_TRIGGER_FALLING);
	i2c_register_board_info(1, mbimx51_i2c_devices,
				ARRAY_SIZE(mbimx51_i2c_devices));

	imx51_add_sdhci_esdhc_imx(0, NULL);
	imx51_add_sdhci_esdhc_imx(1, NULL);
}
