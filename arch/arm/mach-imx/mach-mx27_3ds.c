/*
 * Copyright 2009 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * Author: Fabio Estevam <fabio.estevam@freescale.com>
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
 */

/*
 * This machine is known as:
 *  - i.MX27 3-Stack Development System
 *  - i.MX27 Platform Development Kit (i.MX27 PDK)
 */

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx27.h>

#include "devices-imx27.h"

#define SD1_EN_GPIO (GPIO_PORTB + 25)

static const int mx27pdk_pins[] __initconst = {
	/* UART1 */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS,
	/* FEC */
	PD0_AIN_FEC_TXD0,
	PD1_AIN_FEC_TXD1,
	PD2_AIN_FEC_TXD2,
	PD3_AIN_FEC_TXD3,
	PD4_AOUT_FEC_RX_ER,
	PD5_AOUT_FEC_RXD1,
	PD6_AOUT_FEC_RXD2,
	PD7_AOUT_FEC_RXD3,
	PD8_AF_FEC_MDIO,
	PD9_AIN_FEC_MDC,
	PD10_AOUT_FEC_CRS,
	PD11_AOUT_FEC_TX_CLK,
	PD12_AOUT_FEC_RXD0,
	PD13_AOUT_FEC_RX_DV,
	PD14_AOUT_FEC_RX_CLK,
	PD15_AOUT_FEC_COL,
	PD16_AIN_FEC_TX_ER,
	PF23_AIN_FEC_TX_EN,
	/* SDHC1 */
	PE18_PF_SD1_D0,
	PE19_PF_SD1_D1,
	PE20_PF_SD1_D2,
	PE21_PF_SD1_D3,
	PE22_PF_SD1_CMD,
	PE23_PF_SD1_CLK,
	SD1_EN_GPIO | GPIO_GPIO | GPIO_OUT,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

/*
 * Matrix keyboard
 */

static const uint32_t mx27_3ds_keymap[] = {
	KEY(0, 0, KEY_UP),
	KEY(0, 1, KEY_DOWN),
	KEY(1, 0, KEY_RIGHT),
	KEY(1, 1, KEY_LEFT),
	KEY(1, 2, KEY_ENTER),
	KEY(2, 0, KEY_F6),
	KEY(2, 1, KEY_F8),
	KEY(2, 2, KEY_F9),
	KEY(2, 3, KEY_F10),
};

static const struct matrix_keymap_data mx27_3ds_keymap_data __initconst = {
	.keymap		= mx27_3ds_keymap,
	.keymap_size	= ARRAY_SIZE(mx27_3ds_keymap),
};

static int mx27_3ds_sdhc1_init(struct device *dev, irq_handler_t detect_irq,
				void *data)
{
	return request_irq(IRQ_GPIOB(26), detect_irq, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING, "sdhc1-card-detect", data);
}

static void mx27_3ds_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(IRQ_GPIOB(26), data);
}

static const struct imxmmc_platform_data sdhc1_pdata __initconst = {
	.init = mx27_3ds_sdhc1_init,
	.exit = mx27_3ds_sdhc1_exit,
};

static void mx27_3ds_sdhc1_enable_level_translator(void)
{
	/* Turn on TXB0108 OE pin */
	gpio_request(SD1_EN_GPIO, "sd1_enable");
	gpio_direction_output(SD1_EN_GPIO, 1);
}

static void __init mx27pdk_init(void)
{
	mxc_gpio_setup_multiple_pins(mx27pdk_pins, ARRAY_SIZE(mx27pdk_pins),
		"mx27pdk");
	mx27_3ds_sdhc1_enable_level_translator();
	imx27_add_imx_uart0(&uart_pdata);
	imx27_add_fec(NULL);
	imx27_add_imx_keypad(&mx27_3ds_keymap_data);
	imx27_add_mxc_mmc(0, &sdhc1_pdata);
	imx27_add_imx2_wdt(NULL);
}

static void __init mx27pdk_timer_init(void)
{
	mx27_clocks_init(26000000);
}

static struct sys_timer mx27pdk_timer = {
	.init	= mx27pdk_timer_init,
};

MACHINE_START(MX27_3DS, "Freescale MX27PDK")
	/* maintainer: Freescale Semiconductor, Inc. */
	.boot_params    = MX27_PHYS_OFFSET + 0x100,
	.map_io         = mx27_map_io,
	.init_irq       = mx27_init_irq,
	.init_machine   = mx27pdk_init,
	.timer          = &mx27pdk_timer,
MACHINE_END
