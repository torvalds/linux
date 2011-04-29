/*
 * board setup for STMP378x-Development-Board
 *
 * based on mx23evk board setup and information gained form the original
 * plat-stmp based board setup, now converted to mach-mxs.
 *
 * Copyright 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2011 Wolfram Sang, Pengutronix e.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/spi/spi.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/common.h>
#include <mach/iomux-mx23.h>

#include "devices-mx23.h"

#define STMP378X_DEVB_MMC0_WRITE_PROTECT	MXS_GPIO_NR(1, 30)
#define STMP378X_DEVB_MMC0_SLOT_POWER		MXS_GPIO_NR(1, 29)

#define STMP378X_DEVB_PAD_AUART (MXS_PAD_4MA | MXS_PAD_1V8 | MXS_PAD_NOPULL)

static const iomux_cfg_t stmp378x_dvb_pads[] __initconst = {
	/* duart (extended setup missing in old boardcode, too */
	MX23_PAD_PWM0__DUART_RX,
	MX23_PAD_PWM1__DUART_TX,

	/* auart */
	MX23_PAD_AUART1_RX__AUART1_RX | STMP378X_DEVB_PAD_AUART,
	MX23_PAD_AUART1_TX__AUART1_TX | STMP378X_DEVB_PAD_AUART,
	MX23_PAD_AUART1_CTS__AUART1_CTS | STMP378X_DEVB_PAD_AUART,
	MX23_PAD_AUART1_RTS__AUART1_RTS | STMP378X_DEVB_PAD_AUART,

	/* mmc */
	MX23_PAD_SSP1_DATA0__SSP1_DATA0 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX23_PAD_SSP1_DATA1__SSP1_DATA1 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX23_PAD_SSP1_DATA2__SSP1_DATA2 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX23_PAD_SSP1_DATA3__SSP1_DATA3 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX23_PAD_SSP1_CMD__SSP1_CMD |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX23_PAD_SSP1_DETECT__SSP1_DETECT |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	MX23_PAD_SSP1_SCK__SSP1_SCK |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	MX23_PAD_PWM4__GPIO_1_30 | MXS_PAD_CTRL, /* write protect */
	MX23_PAD_PWM3__GPIO_1_29 | MXS_PAD_CTRL, /* power enable */
};

static struct mxs_mmc_platform_data stmp378x_dvb_mmc_pdata __initdata = {
	.wp_gpio = STMP378X_DEVB_MMC0_WRITE_PROTECT,
};

static struct spi_board_info spi_board_info[] __initdata = {
#if defined(CONFIG_ENC28J60) || defined(CONFIG_ENC28J60_MODULE)
	{
		.modalias       = "enc28j60",
		.max_speed_hz   = 6 * 1000 * 1000,
		.bus_num	= 1,
		.chip_select    = 0,
		.platform_data  = NULL,
	},
#endif
};

static void __init stmp378x_dvb_init(void)
{
	int ret;

	mxs_iomux_setup_multiple_pads(stmp378x_dvb_pads,
			ARRAY_SIZE(stmp378x_dvb_pads));

	mx23_add_duart();
	mx23_add_auart0();

	/* power on mmc slot */
	ret = gpio_request_one(STMP378X_DEVB_MMC0_SLOT_POWER,
		GPIOF_OUT_INIT_LOW, "mmc0-slot-power");
	if (ret)
		pr_warn("could not power mmc (%d)\n", ret);

	mx23_add_mxs_mmc(0, &stmp378x_dvb_mmc_pdata);

	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
}

static void __init stmp378x_dvb_timer_init(void)
{
	mx23_clocks_init();
}

static struct sys_timer stmp378x_dvb_timer = {
	.init	= stmp378x_dvb_timer_init,
};

MACHINE_START(STMP378X, "STMP378X")
	.map_io		= mx23_map_io,
	.init_irq	= mx23_init_irq,
	.init_machine	= stmp378x_dvb_init,
	.timer		= &stmp378x_dvb_timer,
MACHINE_END
