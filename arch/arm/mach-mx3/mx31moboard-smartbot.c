/*
 * Copyright (C) 2009 Valentin Longchamp, EPFL Mobots group
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>

#include <media/soc_camera.h>

#include "devices.h"

static unsigned int smartbot_pins[] = {
	/* UART1 */
	MX31_PIN_CTS2__CTS2, MX31_PIN_RTS2__RTS2,
	MX31_PIN_TXD2__TXD2, MX31_PIN_RXD2__RXD2,
	/* CSI */
	MX31_PIN_CSI_D4__CSI_D4, MX31_PIN_CSI_D5__CSI_D5,
	MX31_PIN_CSI_D6__CSI_D6, MX31_PIN_CSI_D7__CSI_D7,
	MX31_PIN_CSI_D8__CSI_D8, MX31_PIN_CSI_D9__CSI_D9,
	MX31_PIN_CSI_D10__CSI_D10, MX31_PIN_CSI_D11__CSI_D11,
	MX31_PIN_CSI_D12__CSI_D12, MX31_PIN_CSI_D13__CSI_D13,
	MX31_PIN_CSI_D14__CSI_D14, MX31_PIN_CSI_D15__CSI_D15,
	MX31_PIN_CSI_HSYNC__CSI_HSYNC, MX31_PIN_CSI_MCLK__CSI_MCLK,
	MX31_PIN_CSI_PIXCLK__CSI_PIXCLK, MX31_PIN_CSI_VSYNC__CSI_VSYNC,
	MX31_PIN_GPIO3_0__GPIO3_0, MX31_PIN_GPIO3_1__GPIO3_1,
	/* ENABLES */
	MX31_PIN_DTR_DCE1__GPIO2_8, MX31_PIN_DSR_DCE1__GPIO2_9,
	MX31_PIN_RI_DCE1__GPIO2_10, MX31_PIN_DCD_DCE1__GPIO2_11,
};

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

#define CAM_POWER	IOMUX_TO_GPIO(MX31_PIN_GPIO3_1)
#define CAM_RST_B	IOMUX_TO_GPIO(MX31_PIN_GPIO3_0)

static int smartbot_cam_power(struct device *dev, int on)
{
	gpio_set_value(CAM_POWER, !on);
	return 0;
}

static int smartbot_cam_reset(struct device *dev)
{
	gpio_set_value(CAM_RST_B, 0);
	udelay(100);
	gpio_set_value(CAM_RST_B, 1);
	return 0;
}

static struct i2c_board_info smartbot_i2c_devices[] = {
	{
		I2C_BOARD_INFO("mt9t031", 0x5d),
	},
};

static struct soc_camera_link base_iclink = {
	.bus_id		= 0,		/* Must match with the camera ID */
	.power		= smartbot_cam_power,
	.reset		= smartbot_cam_reset,
	.board_info	= &smartbot_i2c_devices[0],
	.i2c_adapter_id	= 0,
	.module_name	= "mt9t031",
};

static struct platform_device smartbot_camera[] = {
	{
		.name	= "soc-camera-pdrv",
		.id	= 0,
		.dev	= {
			.platform_data = &base_iclink,
		},
	},
};

static struct platform_device *smartbot_cameras[] __initdata = {
	&smartbot_camera[0],
};

static int __init smartbot_cam_init(void)
{
	int ret = gpio_request(CAM_RST_B, "cam-reset");
	if (ret)
		return ret;
	gpio_direction_output(CAM_RST_B, 1);
	ret = gpio_request(CAM_POWER, "cam-standby");
	if (ret)
		return ret;
	gpio_direction_output(CAM_POWER, 0);

	return 0;
}

#define POWER_EN IOMUX_TO_GPIO(MX31_PIN_DTR_DCE1)
#define DSPIC_RST_B IOMUX_TO_GPIO(MX31_PIN_DSR_DCE1)
#define TRSLAT_RST_B IOMUX_TO_GPIO(MX31_PIN_RI_DCE1)
#define SEL3 IOMUX_TO_GPIO(MX31_PIN_DCD_DCE1)

static void smartbot_resets_init(void)
{
	if (!gpio_request(POWER_EN, "power-enable")) {
		gpio_direction_output(POWER_EN, 0);
		gpio_export(POWER_EN, false);
	}

	if (!gpio_request(DSPIC_RST_B, "dspic-rst")) {
		gpio_direction_output(DSPIC_RST_B, 0);
		gpio_export(DSPIC_RST_B, false);
	}

	if (!gpio_request(TRSLAT_RST_B, "translator-rst")) {
		gpio_direction_output(TRSLAT_RST_B, 0);
		gpio_export(TRSLAT_RST_B, false);
	}

	if (!gpio_request(SEL3, "sel3")) {
		gpio_direction_input(SEL3);
		gpio_export(SEL3, true);
	}
}
/*
 * system init for baseboard usage. Will be called by mx31moboard init.
 */
void __init mx31moboard_smartbot_init(void)
{
	printk(KERN_INFO "Initializing mx31smartbot peripherals\n");

	mxc_iomux_setup_multiple_pins(smartbot_pins, ARRAY_SIZE(smartbot_pins),
		"smartbot");

	mxc_register_device(&mxc_uart_device1, &uart_pdata);

	smartbot_resets_init();

	smartbot_cam_init();
	platform_add_devices(smartbot_cameras, ARRAY_SIZE(smartbot_cameras));
}
