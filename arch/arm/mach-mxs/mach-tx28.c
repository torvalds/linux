/*
 * Copyright (C) 2010 <LW@KARO-electronics.de>
 *
 * based on: mach-mx28_evk.c
 * Copyright 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation
 */
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/i2c.h>

#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/common.h>
#include <mach/iomux-mx28.h>

#include "devices-mx28.h"
#include "module-tx28.h"

#define TX28_STK5_GPIO_LED		MXS_GPIO_NR(4, 10)

static const iomux_cfg_t tx28_stk5v3_pads[] __initconst = {
	/* LED */
	MX28_PAD_ENET0_RXD3__GPIO_4_10 |
		MXS_PAD_3V3 | MXS_PAD_4MA | MXS_PAD_NOPULL,

	/* framebuffer */
#define LCD_MODE (MXS_PAD_3V3 | MXS_PAD_4MA)
	MX28_PAD_LCD_D00__LCD_D0 | LCD_MODE,
	MX28_PAD_LCD_D01__LCD_D1 | LCD_MODE,
	MX28_PAD_LCD_D02__LCD_D2 | LCD_MODE,
	MX28_PAD_LCD_D03__LCD_D3 | LCD_MODE,
	MX28_PAD_LCD_D04__LCD_D4 | LCD_MODE,
	MX28_PAD_LCD_D05__LCD_D5 | LCD_MODE,
	MX28_PAD_LCD_D06__LCD_D6 | LCD_MODE,
	MX28_PAD_LCD_D07__LCD_D7 | LCD_MODE,
	MX28_PAD_LCD_D08__LCD_D8 | LCD_MODE,
	MX28_PAD_LCD_D09__LCD_D9 | LCD_MODE,
	MX28_PAD_LCD_D10__LCD_D10 | LCD_MODE,
	MX28_PAD_LCD_D11__LCD_D11 | LCD_MODE,
	MX28_PAD_LCD_D12__LCD_D12 | LCD_MODE,
	MX28_PAD_LCD_D13__LCD_D13 | LCD_MODE,
	MX28_PAD_LCD_D14__LCD_D14 | LCD_MODE,
	MX28_PAD_LCD_D15__LCD_D15 | LCD_MODE,
	MX28_PAD_LCD_D16__LCD_D16 | LCD_MODE,
	MX28_PAD_LCD_D17__LCD_D17 | LCD_MODE,
	MX28_PAD_LCD_D18__LCD_D18 | LCD_MODE,
	MX28_PAD_LCD_D19__LCD_D19 | LCD_MODE,
	MX28_PAD_LCD_D20__LCD_D20 | LCD_MODE,
	MX28_PAD_LCD_D21__LCD_D21 | LCD_MODE,
	MX28_PAD_LCD_D22__LCD_D22 | LCD_MODE,
	MX28_PAD_LCD_D23__LCD_D23 | LCD_MODE,
	MX28_PAD_LCD_RD_E__LCD_VSYNC | LCD_MODE,
	MX28_PAD_LCD_WR_RWN__LCD_HSYNC | LCD_MODE,
	MX28_PAD_LCD_RS__LCD_DOTCLK | LCD_MODE,
	MX28_PAD_LCD_CS__LCD_CS | LCD_MODE,
	MX28_PAD_LCD_VSYNC__LCD_VSYNC | LCD_MODE,
	MX28_PAD_LCD_HSYNC__LCD_HSYNC | LCD_MODE,
	MX28_PAD_LCD_DOTCLK__LCD_DOTCLK | LCD_MODE,
	MX28_PAD_LCD_ENABLE__GPIO_1_31 | LCD_MODE,
	MX28_PAD_LCD_RESET__GPIO_3_30 | LCD_MODE,
	MX28_PAD_PWM0__PWM_0 | LCD_MODE,

	/* UART1 */
	MX28_PAD_AUART0_CTS__DUART_RX,
	MX28_PAD_AUART0_RTS__DUART_TX,
	MX28_PAD_AUART0_TX__DUART_RTS,
	MX28_PAD_AUART0_RX__DUART_CTS,

	/* UART2 */
	MX28_PAD_AUART1_RX__AUART1_RX,
	MX28_PAD_AUART1_TX__AUART1_TX,
	MX28_PAD_AUART1_RTS__AUART1_RTS,
	MX28_PAD_AUART1_CTS__AUART1_CTS,

	/* CAN */
	MX28_PAD_GPMI_RDY2__CAN0_TX,
	MX28_PAD_GPMI_RDY3__CAN0_RX,

	/* I2C */
	MX28_PAD_I2C0_SCL__I2C0_SCL,
	MX28_PAD_I2C0_SDA__I2C0_SDA,

	/* TSC2007 */
	MX28_PAD_SAIF0_MCLK__GPIO_3_20 | MXS_PAD_3V3 | MXS_PAD_4MA | MXS_PAD_PULLUP,

	/* MMC0 */
	MX28_PAD_SSP0_DATA0__SSP0_D0 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA1__SSP0_D1 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA2__SSP0_D2 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA3__SSP0_D3 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA4__SSP0_D4 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA5__SSP0_D5 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA6__SSP0_D6 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA7__SSP0_D7 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_CMD__SSP0_CMD |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DETECT__SSP0_CARD_DETECT |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	MX28_PAD_SSP0_SCK__SSP0_SCK |
		(MXS_PAD_12MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
};

static struct gpio_led tx28_stk5v3_leds[] = {
	{
		.name = "GPIO-LED",
		.default_trigger = "heartbeat",
		.gpio = TX28_STK5_GPIO_LED,
	},
};

static const struct gpio_led_platform_data tx28_stk5v3_led_data __initconst = {
	.leds = tx28_stk5v3_leds,
	.num_leds = ARRAY_SIZE(tx28_stk5v3_leds),
};

static struct spi_board_info tx28_spi_board_info[] = {
	{
		.modalias = "spidev",
		.max_speed_hz = 20000000,
		.bus_num = 0,
		.chip_select = 1,
		.controller_data = (void *)SPI_GPIO_NO_CHIPSELECT,
		.mode = SPI_MODE_0,
	},
};

static struct i2c_board_info tx28_stk5v3_i2c_boardinfo[] __initdata = {
	{
		I2C_BOARD_INFO("ds1339", 0x68),
	},
};

static void __init tx28_stk5v3_init(void)
{
	mxs_iomux_setup_multiple_pads(tx28_stk5v3_pads,
			ARRAY_SIZE(tx28_stk5v3_pads));

	mx28_add_duart(); /* UART1 */
	mx28_add_auart(1); /* UART2 */

	tx28_add_fec0();
	/* spi via ssp will be added when available */
	spi_register_board_info(tx28_spi_board_info,
			ARRAY_SIZE(tx28_spi_board_info));
	mxs_add_platform_device("leds-gpio", 0, NULL, 0,
			&tx28_stk5v3_led_data, sizeof(tx28_stk5v3_led_data));
	mx28_add_mxs_i2c(0);
	i2c_register_board_info(0, tx28_stk5v3_i2c_boardinfo,
			ARRAY_SIZE(tx28_stk5v3_i2c_boardinfo));
}

static void __init tx28_timer_init(void)
{
	mx28_clocks_init();
}

static struct sys_timer tx28_timer = {
	.init = tx28_timer_init,
};

MACHINE_START(TX28, "Ka-Ro electronics TX28 module")
	.map_io = mx28_map_io,
	.init_irq = mx28_init_irq,
	.init_machine = tx28_stk5v3_init,
	.timer = &tx28_timer,
MACHINE_END
