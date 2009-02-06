/*
 * ATSTK1003 daughterboard-specific init code
 *
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>

#include <linux/spi/at73c213.h>
#include <linux/spi/spi.h>
#include <linux/atmel-mci.h>

#include <video/atmel_lcdc.h>

#include <asm/setup.h>

#include <mach/at32ap700x.h>
#include <mach/board.h>
#include <mach/init.h>
#include <mach/portmux.h>

#include "atstk1000.h"

/* Oscillator frequencies. These are board specific */
unsigned long at32_board_osc_rates[3] = {
	[0] = 32768,	/* 32.768 kHz on RTC osc */
	[1] = 20000000,	/* 20 MHz on osc0 */
	[2] = 12000000,	/* 12 MHz on osc1 */
};

#ifdef CONFIG_BOARD_ATSTK1000_EXTDAC
static struct at73c213_board_info at73c213_data = {
	.ssc_id		= 0,
	.shortname	= "AVR32 STK1000 external DAC",
};
#endif

#ifndef CONFIG_BOARD_ATSTK100X_SW1_CUSTOM
static struct spi_board_info spi0_board_info[] __initdata = {
#ifdef CONFIG_BOARD_ATSTK1000_EXTDAC
	{
		/* AT73C213 */
		.modalias	= "at73c213",
		.max_speed_hz	= 200000,
		.chip_select	= 0,
		.mode		= SPI_MODE_1,
		.platform_data	= &at73c213_data,
	},
#endif
	{
		/* QVGA display */
		.modalias	= "ltv350qv",
		.max_speed_hz	= 16000000,
		.chip_select	= 1,
		.mode		= SPI_MODE_3,
	},
};
#endif

#ifdef CONFIG_BOARD_ATSTK100X_SPI1
static struct spi_board_info spi1_board_info[] __initdata = { {
	/* patch in custom entries here */
} };
#endif

#ifndef CONFIG_BOARD_ATSTK100X_SW2_CUSTOM
static struct mci_platform_data __initdata mci0_data = {
	.slot[0] = {
		.bus_width	= 4,
		.detect_pin	= -ENODEV,
		.wp_pin		= -ENODEV,
	},
};
#endif

#ifdef CONFIG_BOARD_ATSTK1000_EXTDAC
static void __init atstk1004_setup_extdac(void)
{
	struct clk *gclk;
	struct clk *pll;

	gclk = clk_get(NULL, "gclk0");
	if (IS_ERR(gclk))
		goto err_gclk;
	pll = clk_get(NULL, "pll0");
	if (IS_ERR(pll))
		goto err_pll;

	if (clk_set_parent(gclk, pll)) {
		pr_debug("STK1000: failed to set pll0 as parent for DAC clock\n");
		goto err_set_clk;
	}

	at32_select_periph(GPIO_PIOA_BASE, (1 << 30), GPIO_PERIPH_A, 0);
	at73c213_data.dac_clk = gclk;

err_set_clk:
	clk_put(pll);
err_pll:
	clk_put(gclk);
err_gclk:
	return;
}
#else
static void __init atstk1004_setup_extdac(void)
{

}
#endif /* CONFIG_BOARD_ATSTK1000_EXTDAC */

void __init setup_board(void)
{
#ifdef	CONFIG_BOARD_ATSTK100X_SW2_CUSTOM
	at32_map_usart(0, 1);	/* USART 0/B: /dev/ttyS1, IRDA */
#else
	at32_map_usart(1, 0);	/* USART 1/A: /dev/ttyS0, DB9 */
#endif
	/* USART 2/unused: expansion connector */
	at32_map_usart(3, 2);	/* USART 3/C: /dev/ttyS2, DB9 */

	at32_setup_serial_console(0);
}

static int __init atstk1004_init(void)
{
#ifdef	CONFIG_BOARD_ATSTK100X_SW2_CUSTOM
	at32_add_device_usart(1);
#else
	at32_add_device_usart(0);
#endif
	at32_add_device_usart(2);

#ifndef CONFIG_BOARD_ATSTK100X_SW1_CUSTOM
	at32_add_device_spi(0, spi0_board_info, ARRAY_SIZE(spi0_board_info));
#endif
#ifdef CONFIG_BOARD_ATSTK100X_SPI1
	at32_add_device_spi(1, spi1_board_info, ARRAY_SIZE(spi1_board_info));
#endif
#ifndef CONFIG_BOARD_ATSTK100X_SW2_CUSTOM
	at32_add_device_mci(0, &mci0_data);
#endif
	at32_add_device_lcdc(0, &atstk1000_lcdc_data,
			     fbmem_start, fbmem_size,
			     ATMEL_LCDC_PRI_24BIT | ATMEL_LCDC_PRI_CONTROL);
	at32_add_device_usba(0, NULL);
#ifndef CONFIG_BOARD_ATSTK100X_SW3_CUSTOM
	at32_add_device_ssc(0, ATMEL_SSC_TX);
#endif

	atstk1000_setup_j2_leds();
	atstk1004_setup_extdac();

	return 0;
}
postcore_initcall(atstk1004_init);
