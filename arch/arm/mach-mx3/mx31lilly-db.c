/*
 *  LILLY-1131 development board support
 *
 *    Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 *  based on code for other MX31 boards,
 *
 *    Copyright 2005-2007 Freescale Semiconductor
 *    Copyright (c) 2009 Alberto Panizzo <maramaopercheseimorto@gmail.com>
 *    Copyright (C) 2009 Valentin Longchamp, EPFL Mobots group
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

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include <mach/board-mx31lilly.h>
#include <mach/mmc.h>

#include "devices.h"

/*
 * This file contains board-specific initialization routines for the
 * LILLY-1131 development board. If you design an own baseboard for the
 * module, use this file as base for support code.
 */

static unsigned int lilly_db_board_pins[] __initdata = {
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
	MX31_PIN_SD1_DATA3__SD1_DATA3,
	MX31_PIN_SD1_DATA2__SD1_DATA2,
	MX31_PIN_SD1_DATA1__SD1_DATA1,
	MX31_PIN_SD1_DATA0__SD1_DATA0,
	MX31_PIN_SD1_CLK__SD1_CLK,
	MX31_PIN_SD1_CMD__SD1_CMD,
};

/* UART */
static struct imxuart_platform_data uart_pdata __initdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

/* MMC support */

static int mxc_mmc1_get_ro(struct device *dev)
{
	return gpio_get_value(IOMUX_TO_GPIO(MX31_PIN_LCS0));
}

static int gpio_det, gpio_wp;

static int mxc_mmc1_init(struct device *dev,
			 irq_handler_t detect_irq, void *data)
{
	int ret;

	gpio_det = IOMUX_TO_GPIO(MX31_PIN_GPIO1_1);
	gpio_wp = IOMUX_TO_GPIO(MX31_PIN_LCS0);

	ret = gpio_request(gpio_det, "MMC detect");
	if (ret)
		return ret;

	ret = gpio_request(gpio_wp, "MMC w/p");
	if (ret)
		goto exit_free_det;

	gpio_direction_input(gpio_det);
	gpio_direction_input(gpio_wp);

	ret = request_irq(IOMUX_TO_IRQ(MX31_PIN_GPIO1_1), detect_irq,
			  IRQF_DISABLED | IRQF_TRIGGER_FALLING,
			  "MMC detect", data);
	if (ret)
		goto exit_free_wp;

	return 0;

exit_free_wp:
	gpio_free(gpio_wp);

exit_free_det:
	gpio_free(gpio_det);

	return ret;
}

static void mxc_mmc1_exit(struct device *dev, void *data)
{
	gpio_free(gpio_det);
	gpio_free(gpio_wp);
	free_irq(IOMUX_TO_IRQ(MX31_PIN_GPIO1_1), data);
}

static struct imxmmc_platform_data mmc_pdata = {
	.get_ro	= mxc_mmc1_get_ro,
	.init	= mxc_mmc1_init,
	.exit	= mxc_mmc1_exit,
};

void __init mx31lilly_db_init(void)
{
	mxc_iomux_setup_multiple_pins(lilly_db_board_pins,
					ARRAY_SIZE(lilly_db_board_pins),
					"development board pins");
	mxc_register_device(&mxc_uart_device0, &uart_pdata);
	mxc_register_device(&mxcsdhc_device0, &mmc_pdata);
}

