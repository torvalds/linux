/*
 *  LogicPD i.MX31 SOM-LV development board support
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
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx3.h>
#include <mach/board-mx31lite.h>

#include "devices-imx31.h"

/*
 * This file contains board-specific initialization routines for the
 * LogicPD i.MX31 SOM-LV development board, aka 'LiteKit'.
 * If you design an own baseboard for the module, use this file as base
 * for support code.
 */

static unsigned int litekit_db_board_pins[] __initdata = {
	/* UART1 */
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
	/* SPI 0 */
	MX31_PIN_CSPI1_SCLK__SCLK,
	MX31_PIN_CSPI1_MOSI__MOSI,
	MX31_PIN_CSPI1_MISO__MISO,
	MX31_PIN_CSPI1_SPI_RDY__SPI_RDY,
	MX31_PIN_CSPI1_SS0__SS0,
	MX31_PIN_CSPI1_SS1__SS1,
	MX31_PIN_CSPI1_SS2__SS2,
	/* SDHC1 */
	MX31_PIN_SD1_DATA0__SD1_DATA0,
	MX31_PIN_SD1_DATA1__SD1_DATA1,
	MX31_PIN_SD1_DATA2__SD1_DATA2,
	MX31_PIN_SD1_DATA3__SD1_DATA3,
	MX31_PIN_SD1_CLK__SD1_CLK,
	MX31_PIN_SD1_CMD__SD1_CMD,
};

/* UART */
static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

/* MMC */

static int gpio_det, gpio_wp;

#define MMC_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_HYS_CMOS | \
		     PAD_CTL_ODE_CMOS)

static int mxc_mmc1_get_ro(struct device *dev)
{
	return gpio_get_value(IOMUX_TO_GPIO(MX31_PIN_GPIO1_6));
}

static int mxc_mmc1_init(struct device *dev,
			 irq_handler_t detect_irq, void *data)
{
	int ret;

	gpio_det = IOMUX_TO_GPIO(MX31_PIN_DCD_DCE1);
	gpio_wp = IOMUX_TO_GPIO(MX31_PIN_GPIO1_6);

	mxc_iomux_set_pad(MX31_PIN_SD1_DATA0,
			  MMC_PAD_CFG | PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_SD1_DATA1,
			  MMC_PAD_CFG | PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_SD1_DATA2,
			  MMC_PAD_CFG | PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_SD1_DATA3,
			  MMC_PAD_CFG | PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_SD1_CMD,
			  MMC_PAD_CFG | PAD_CTL_PUE_PUD | PAD_CTL_100K_PU);
	mxc_iomux_set_pad(MX31_PIN_SD1_CLK, MMC_PAD_CFG);

	ret = gpio_request(gpio_det, "MMC detect");
	if (ret)
		return ret;

	ret = gpio_request(gpio_wp, "MMC w/p");
	if (ret)
		goto exit_free_det;

	gpio_direction_input(gpio_det);
	gpio_direction_input(gpio_wp);

	ret = request_irq(IOMUX_TO_IRQ(MX31_PIN_DCD_DCE1), detect_irq,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
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
	free_irq(IOMUX_TO_IRQ(MX31_PIN_DCD_DCE1), data);
}

static const struct imxmmc_platform_data mmc_pdata __initconst = {
	.get_ro	 = mxc_mmc1_get_ro,
	.init	   = mxc_mmc1_init,
	.exit	   = mxc_mmc1_exit,
};

/* SPI */

static int spi_internal_chipselect[] = {
	MXC_SPI_CS(0),
	MXC_SPI_CS(1),
	MXC_SPI_CS(2),
};

static const struct spi_imx_master spi0_pdata __initconst = {
	.chipselect	= spi_internal_chipselect,
	.num_chipselect	= ARRAY_SIZE(spi_internal_chipselect),
};

/* GPIO LEDs */

static const struct gpio_led litekit_leds[] __initconst = {
	{
		.name           = "GPIO0",
		.gpio           = IOMUX_TO_GPIO(MX31_PIN_COMPARE),
		.active_low     = 1,
		.default_state  = LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name           = "GPIO1",
		.gpio           = IOMUX_TO_GPIO(MX31_PIN_CAPTURE),
		.active_low     = 1,
		.default_state  = LEDS_GPIO_DEFSTATE_OFF,
	}
};

static const struct gpio_led_platform_data
		litekit_led_platform_data __initconst = {
	.leds           = litekit_leds,
	.num_leds       = ARRAY_SIZE(litekit_leds),
};

void __init mx31lite_db_init(void)
{
	mxc_iomux_setup_multiple_pins(litekit_db_board_pins,
					ARRAY_SIZE(litekit_db_board_pins),
					"development board pins");
	imx31_add_imx_uart0(&uart_pdata);
	imx31_add_mxc_mmc(0, &mmc_pdata);
	imx31_add_spi_imx0(&spi0_pdata);
	gpio_led_register_device(-1, &litekit_led_platform_data);
	imx31_add_imx2_wdt(NULL);
	imx31_add_mxc_rtc(NULL);
}
