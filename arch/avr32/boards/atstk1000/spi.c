/*
 * ATSTK1000 SPI devices
 *
 * Copyright (C) 2005 Atmel Norway
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/device.h>
#include <linux/spi/spi.h>

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias	= "ltv350qv",
		.max_speed_hz	= 16000000,
		.bus_num	= 0,
		.chip_select	= 1,
	},
};

static int board_init_spi(void)
{
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
	return 0;
}
arch_initcall(board_init_spi);
