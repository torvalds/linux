/*
 * Platform glue data for ST-Ericsson CW1200 driver
 *
 * Copyright (c) 2013, Sagrad, Inc
 * Author: Solomon Peachy <speachy@sagrad.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_data/cw1200_platform.h>

MODULE_AUTHOR("Solomon Peachy <speachy@sagrad.com>");
MODULE_DESCRIPTION("ST-Ericsson CW1200 Platform glue driver");
MODULE_LICENSE("GPL");

/* Define just one of these.  Feel free to customize as needed */
#define SAGRAD_1091_1098_EVK_SDIO
/* #define SAGRAD_1091_1098_EVK_SPI */

#ifdef SAGRAD_1091_1098_EVK_SDIO
static int cw1200_power_ctrl(const struct cw1200_platform_data_sdio *pdata,
			     bool enable)
{
	/* Control 3v3 and 1v8 to hardware as appropriate */
	/* Note this is not needed if it's controlled elsewhere or always on */

	/* May require delay for power to stabilize */
	return 0;
}

static int cw1200_clk_ctrl(const struct cw1200_platform_data_sdio *pdata,
			   bool enable)
{
	/* Turn CLK_32K off and on as appropriate. */
	/* Note this is not needed if it's always on */

	/* May require delay for clock to stabilize */
	return 0;
}

static struct cw1200_platform_data_sdio cw1200_platform_data = {
	.ref_clk = 38400,
	.have_5ghz = false,
#if 0
	.reset = GPIO_RF_RESET, /* Replace as appropriate */
	.powerup = GPIO_RF_POWERUP, /* Replace as appropriate */
	.irq = GPIO_TO_IRQ(GPIO_RF_IRQ), /* Replace as appropriate */
#endif
	.power_ctrl = cw1200_power_ctrl,
	.clk_ctrl = cw1200_clk_ctrl,
/*	.macaddr = ??? */
	.sdd_file = "sdd_sagrad_1091_1098.bin",
};
#endif

#ifdef SAGRAD_1091_1098_EVK_SPI
static int cw1200_power_ctrl(const struct cw1200_platform_data_spi *pdata,
			     bool enable)
{
	/* Control 3v3 and 1v8 to hardware as appropriate */
	/* Note this is not needed if it's controlled elsewhere or always on */

	/* May require delay for power to stabilize */
	return 0;
}
static int cw1200_clk_ctrl(const struct cw1200_platform_data_spi *pdata,
			   bool enable)
{
	/* Turn CLK_32K off and on as appropriate. */
	/* Note this is not needed if it's always on */

	/* May require delay for clock to stabilize */
	return 0;
}

static struct cw1200_platform_data_spi cw1200_platform_data = {
	.ref_clk = 38400,
	.spi_bits_per_word = 16,
	.reset = GPIO_RF_RESET, /* Replace as appropriate */
	.powerup = GPIO_RF_POWERUP, /* Replace as appropriate */
	.power_ctrl = cw1200_power_ctrl,
	.clk_ctrl = cw1200_clk_ctrl,
/*	.macaddr = ??? */
	.sdd_file = "sdd_sagrad_1091_1098.bin",
};
static struct spi_board_info myboard_spi_devices[] __initdata = {
	{
		.modalias = "cw1200_wlan_spi",
		.max_speed_hz = 10000000, /* 52MHz Max */
		.bus_num = 0,
		.irq = WIFI_IRQ,
		.platform_data = &cw1200_platform_data,
		.chip_select = 0,
	},
};
#endif


const void *cw1200_get_platform_data(void)
{
	return &cw1200_platform_data;
}
EXPORT_SYMBOL_GPL(cw1200_get_platform_data);
