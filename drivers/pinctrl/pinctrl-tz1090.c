/*
 * Pinctrl driver for the Toumaz Xenif TZ1090 SoC
 *
 * Copyright (c) 2013, Imagination Technologies Ltd.
 *
 * Derived from Tegra code:
 * Copyright (c) 2011-2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * Derived from code:
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2010 NVIDIA Corporation
 * Copyright (C) 2009-2011 ST-Ericsson AB
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/*
 * The registers may be shared with other threads/cores, so we need to use the
 * metag global lock2 for atomicity.
 */
#include <asm/global_lock.h>

#include "core.h"
#include "pinconf.h"

/* Register offsets from bank base address */
#define REG_PINCTRL_SELECT	0x10
#define REG_PINCTRL_SCHMITT	0x90
#define REG_PINCTRL_PU_PD	0xa0
#define REG_PINCTRL_SR		0xc0
#define REG_PINCTRL_DR		0xd0
#define REG_PINCTRL_IF_CTL	0xe0

/* REG_PINCTRL_PU_PD field values */
#define REG_PU_PD_TRISTATE	0
#define REG_PU_PD_UP		1
#define REG_PU_PD_DOWN		2
#define REG_PU_PD_REPEATER	3

/* REG_PINCTRL_DR field values */
#define REG_DR_2mA		0
#define REG_DR_4mA		1
#define REG_DR_8mA		2
#define REG_DR_12mA		3

/**
 * struct tz1090_function - TZ1090 pinctrl mux function
 * @name:	The name of the function, exported to pinctrl core.
 * @groups:	An array of pin groups that may select this function.
 * @ngroups:	The number of entries in @groups.
 */
struct tz1090_function {
	const char		*name;
	const char * const	*groups;
	unsigned int		ngroups;
};

/**
 * struct tz1090_muxdesc - TZ1090 individual mux description
 * @funcs:	Function for each mux value.
 * @reg:	Mux register offset. 0 if unsupported.
 * @bit:	Mux register bit. 0 if unsupported.
 * @width:	Mux field width. 0 if unsupported.
 *
 * A representation of a group of signals (possibly just one signal) in the
 * TZ1090 which can be muxed to a set of functions or sub muxes.
 */
struct tz1090_muxdesc {
	int	funcs[5];
	u16	reg;
	u8	bit;
	u8	width;
};

/**
 * struct tz1090_pingroup - TZ1090 pin group
 * @name:	Name of pin group.
 * @pins:	Array of pin numbers in this pin group.
 * @npins:	Number of pins in this pin group.
 * @mux:	Top level mux.
 * @drv:	Drive control supported, 0 if unsupported.
 *		This means Schmitt, Slew, and Drive strength.
 * @slw_bit:	Slew register bit. 0 if unsupported.
 *		The same bit is used for Schmitt, and Drive (*2).
 * @func:	Currently muxed function.
 * @func_count:	Number of pins using current mux function.
 *
 * A representation of a group of pins (possibly just one pin) in the TZ1090
 * pin controller. Each group allows some parameter or parameters to be
 * configured. The most common is mux function selection.
 */
struct tz1090_pingroup {
	const char		*name;
	const unsigned int	*pins;
	unsigned int		npins;
	struct tz1090_muxdesc	mux;

	bool			drv;
	u8			slw_bit;

	int			func;
	unsigned int		func_count;
};

/*
 * Most pins affected by the pinmux can also be GPIOs. Define these first.
 * These must match how the GPIO driver names/numbers its pins.
 */

enum tz1090_pin {
	/* GPIO pins */
	TZ1090_PIN_SDIO_CLK,
	TZ1090_PIN_SDIO_CMD,
	TZ1090_PIN_SDIO_D0,
	TZ1090_PIN_SDIO_D1,
	TZ1090_PIN_SDIO_D2,
	TZ1090_PIN_SDIO_D3,
	TZ1090_PIN_SDH_CD,
	TZ1090_PIN_SDH_WP,
	TZ1090_PIN_SPI0_MCLK,
	TZ1090_PIN_SPI0_CS0,
	TZ1090_PIN_SPI0_CS1,
	TZ1090_PIN_SPI0_CS2,
	TZ1090_PIN_SPI0_DOUT,
	TZ1090_PIN_SPI0_DIN,
	TZ1090_PIN_SPI1_MCLK,
	TZ1090_PIN_SPI1_CS0,
	TZ1090_PIN_SPI1_CS1,
	TZ1090_PIN_SPI1_CS2,
	TZ1090_PIN_SPI1_DOUT,
	TZ1090_PIN_SPI1_DIN,
	TZ1090_PIN_UART0_RXD,
	TZ1090_PIN_UART0_TXD,
	TZ1090_PIN_UART0_CTS,
	TZ1090_PIN_UART0_RTS,
	TZ1090_PIN_UART1_RXD,
	TZ1090_PIN_UART1_TXD,
	TZ1090_PIN_SCB0_SDAT,
	TZ1090_PIN_SCB0_SCLK,
	TZ1090_PIN_SCB1_SDAT,
	TZ1090_PIN_SCB1_SCLK,
	TZ1090_PIN_SCB2_SDAT,
	TZ1090_PIN_SCB2_SCLK,
	TZ1090_PIN_I2S_MCLK,
	TZ1090_PIN_I2S_BCLK_OUT,
	TZ1090_PIN_I2S_LRCLK_OUT,
	TZ1090_PIN_I2S_DOUT0,
	TZ1090_PIN_I2S_DOUT1,
	TZ1090_PIN_I2S_DOUT2,
	TZ1090_PIN_I2S_DIN,
	TZ1090_PIN_PDM_A,
	TZ1090_PIN_PDM_B,
	TZ1090_PIN_PDM_C,
	TZ1090_PIN_PDM_D,
	TZ1090_PIN_TFT_RED0,
	TZ1090_PIN_TFT_RED1,
	TZ1090_PIN_TFT_RED2,
	TZ1090_PIN_TFT_RED3,
	TZ1090_PIN_TFT_RED4,
	TZ1090_PIN_TFT_RED5,
	TZ1090_PIN_TFT_RED6,
	TZ1090_PIN_TFT_RED7,
	TZ1090_PIN_TFT_GREEN0,
	TZ1090_PIN_TFT_GREEN1,
	TZ1090_PIN_TFT_GREEN2,
	TZ1090_PIN_TFT_GREEN3,
	TZ1090_PIN_TFT_GREEN4,
	TZ1090_PIN_TFT_GREEN5,
	TZ1090_PIN_TFT_GREEN6,
	TZ1090_PIN_TFT_GREEN7,
	TZ1090_PIN_TFT_BLUE0,
	TZ1090_PIN_TFT_BLUE1,
	TZ1090_PIN_TFT_BLUE2,
	TZ1090_PIN_TFT_BLUE3,
	TZ1090_PIN_TFT_BLUE4,
	TZ1090_PIN_TFT_BLUE5,
	TZ1090_PIN_TFT_BLUE6,
	TZ1090_PIN_TFT_BLUE7,
	TZ1090_PIN_TFT_VDDEN_GD,
	TZ1090_PIN_TFT_PANELCLK,
	TZ1090_PIN_TFT_BLANK_LS,
	TZ1090_PIN_TFT_VSYNC_NS,
	TZ1090_PIN_TFT_HSYNC_NR,
	TZ1090_PIN_TFT_VD12ACB,
	TZ1090_PIN_TFT_PWRSAVE,
	TZ1090_PIN_TX_ON,
	TZ1090_PIN_RX_ON,
	TZ1090_PIN_PLL_ON,
	TZ1090_PIN_PA_ON,
	TZ1090_PIN_RX_HP,
	TZ1090_PIN_GAIN0,
	TZ1090_PIN_GAIN1,
	TZ1090_PIN_GAIN2,
	TZ1090_PIN_GAIN3,
	TZ1090_PIN_GAIN4,
	TZ1090_PIN_GAIN5,
	TZ1090_PIN_GAIN6,
	TZ1090_PIN_GAIN7,
	TZ1090_PIN_ANT_SEL0,
	TZ1090_PIN_ANT_SEL1,
	TZ1090_PIN_SDH_CLK_IN,

	/* Non-GPIO pins */
	TZ1090_PIN_TCK,
	TZ1090_PIN_TRST,
	TZ1090_PIN_TDI,
	TZ1090_PIN_TDO,
	TZ1090_PIN_TMS,
	TZ1090_PIN_CLK_OUT0,
	TZ1090_PIN_CLK_OUT1,

	NUM_GPIOS = TZ1090_PIN_TCK,
};

/* Pin names */

static const struct pinctrl_pin_desc tz1090_pins[] = {
	/* GPIO pins */
	PINCTRL_PIN(TZ1090_PIN_SDIO_CLK,	"sdio_clk"),
	PINCTRL_PIN(TZ1090_PIN_SDIO_CMD,	"sdio_cmd"),
	PINCTRL_PIN(TZ1090_PIN_SDIO_D0,		"sdio_d0"),
	PINCTRL_PIN(TZ1090_PIN_SDIO_D1,		"sdio_d1"),
	PINCTRL_PIN(TZ1090_PIN_SDIO_D2,		"sdio_d2"),
	PINCTRL_PIN(TZ1090_PIN_SDIO_D3,		"sdio_d3"),
	PINCTRL_PIN(TZ1090_PIN_SDH_CD,		"sdh_cd"),
	PINCTRL_PIN(TZ1090_PIN_SDH_WP,		"sdh_wp"),
	PINCTRL_PIN(TZ1090_PIN_SPI0_MCLK,	"spi0_mclk"),
	PINCTRL_PIN(TZ1090_PIN_SPI0_CS0,	"spi0_cs0"),
	PINCTRL_PIN(TZ1090_PIN_SPI0_CS1,	"spi0_cs1"),
	PINCTRL_PIN(TZ1090_PIN_SPI0_CS2,	"spi0_cs2"),
	PINCTRL_PIN(TZ1090_PIN_SPI0_DOUT,	"spi0_dout"),
	PINCTRL_PIN(TZ1090_PIN_SPI0_DIN,	"spi0_din"),
	PINCTRL_PIN(TZ1090_PIN_SPI1_MCLK,	"spi1_mclk"),
	PINCTRL_PIN(TZ1090_PIN_SPI1_CS0,	"spi1_cs0"),
	PINCTRL_PIN(TZ1090_PIN_SPI1_CS1,	"spi1_cs1"),
	PINCTRL_PIN(TZ1090_PIN_SPI1_CS2,	"spi1_cs2"),
	PINCTRL_PIN(TZ1090_PIN_SPI1_DOUT,	"spi1_dout"),
	PINCTRL_PIN(TZ1090_PIN_SPI1_DIN,	"spi1_din"),
	PINCTRL_PIN(TZ1090_PIN_UART0_RXD,	"uart0_rxd"),
	PINCTRL_PIN(TZ1090_PIN_UART0_TXD,	"uart0_txd"),
	PINCTRL_PIN(TZ1090_PIN_UART0_CTS,	"uart0_cts"),
	PINCTRL_PIN(TZ1090_PIN_UART0_RTS,	"uart0_rts"),
	PINCTRL_PIN(TZ1090_PIN_UART1_RXD,	"uart1_rxd"),
	PINCTRL_PIN(TZ1090_PIN_UART1_TXD,	"uart1_txd"),
	PINCTRL_PIN(TZ1090_PIN_SCB0_SDAT,	"scb0_sdat"),
	PINCTRL_PIN(TZ1090_PIN_SCB0_SCLK,	"scb0_sclk"),
	PINCTRL_PIN(TZ1090_PIN_SCB1_SDAT,	"scb1_sdat"),
	PINCTRL_PIN(TZ1090_PIN_SCB1_SCLK,	"scb1_sclk"),
	PINCTRL_PIN(TZ1090_PIN_SCB2_SDAT,	"scb2_sdat"),
	PINCTRL_PIN(TZ1090_PIN_SCB2_SCLK,	"scb2_sclk"),
	PINCTRL_PIN(TZ1090_PIN_I2S_MCLK,	"i2s_mclk"),
	PINCTRL_PIN(TZ1090_PIN_I2S_BCLK_OUT,	"i2s_bclk_out"),
	PINCTRL_PIN(TZ1090_PIN_I2S_LRCLK_OUT,	"i2s_lrclk_out"),
	PINCTRL_PIN(TZ1090_PIN_I2S_DOUT0,	"i2s_dout0"),
	PINCTRL_PIN(TZ1090_PIN_I2S_DOUT1,	"i2s_dout1"),
	PINCTRL_PIN(TZ1090_PIN_I2S_DOUT2,	"i2s_dout2"),
	PINCTRL_PIN(TZ1090_PIN_I2S_DIN,		"i2s_din"),
	PINCTRL_PIN(TZ1090_PIN_PDM_A,		"pdm_a"),
	PINCTRL_PIN(TZ1090_PIN_PDM_B,		"pdm_b"),
	PINCTRL_PIN(TZ1090_PIN_PDM_C,		"pdm_c"),
	PINCTRL_PIN(TZ1090_PIN_PDM_D,		"pdm_d"),
	PINCTRL_PIN(TZ1090_PIN_TFT_RED0,	"tft_red0"),
	PINCTRL_PIN(TZ1090_PIN_TFT_RED1,	"tft_red1"),
	PINCTRL_PIN(TZ1090_PIN_TFT_RED2,	"tft_red2"),
	PINCTRL_PIN(TZ1090_PIN_TFT_RED3,	"tft_red3"),
	PINCTRL_PIN(TZ1090_PIN_TFT_RED4,	"tft_red4"),
	PINCTRL_PIN(TZ1090_PIN_TFT_RED5,	"tft_red5"),
	PINCTRL_PIN(TZ1090_PIN_TFT_RED6,	"tft_red6"),
	PINCTRL_PIN(TZ1090_PIN_TFT_RED7,	"tft_red7"),
	PINCTRL_PIN(TZ1090_PIN_TFT_GREEN0,	"tft_green0"),
	PINCTRL_PIN(TZ1090_PIN_TFT_GREEN1,	"tft_green1"),
	PINCTRL_PIN(TZ1090_PIN_TFT_GREEN2,	"tft_green2"),
	PINCTRL_PIN(TZ1090_PIN_TFT_GREEN3,	"tft_green3"),
	PINCTRL_PIN(TZ1090_PIN_TFT_GREEN4,	"tft_green4"),
	PINCTRL_PIN(TZ1090_PIN_TFT_GREEN5,	"tft_green5"),
	PINCTRL_PIN(TZ1090_PIN_TFT_GREEN6,	"tft_green6"),
	PINCTRL_PIN(TZ1090_PIN_TFT_GREEN7,	"tft_green7"),
	PINCTRL_PIN(TZ1090_PIN_TFT_BLUE0,	"tft_blue0"),
	PINCTRL_PIN(TZ1090_PIN_TFT_BLUE1,	"tft_blue1"),
	PINCTRL_PIN(TZ1090_PIN_TFT_BLUE2,	"tft_blue2"),
	PINCTRL_PIN(TZ1090_PIN_TFT_BLUE3,	"tft_blue3"),
	PINCTRL_PIN(TZ1090_PIN_TFT_BLUE4,	"tft_blue4"),
	PINCTRL_PIN(TZ1090_PIN_TFT_BLUE5,	"tft_blue5"),
	PINCTRL_PIN(TZ1090_PIN_TFT_BLUE6,	"tft_blue6"),
	PINCTRL_PIN(TZ1090_PIN_TFT_BLUE7,	"tft_blue7"),
	PINCTRL_PIN(TZ1090_PIN_TFT_VDDEN_GD,	"tft_vdden_gd"),
	PINCTRL_PIN(TZ1090_PIN_TFT_PANELCLK,	"tft_panelclk"),
	PINCTRL_PIN(TZ1090_PIN_TFT_BLANK_LS,	"tft_blank_ls"),
	PINCTRL_PIN(TZ1090_PIN_TFT_VSYNC_NS,	"tft_vsync_ns"),
	PINCTRL_PIN(TZ1090_PIN_TFT_HSYNC_NR,	"tft_hsync_nr"),
	PINCTRL_PIN(TZ1090_PIN_TFT_VD12ACB,	"tft_vd12acb"),
	PINCTRL_PIN(TZ1090_PIN_TFT_PWRSAVE,	"tft_pwrsave"),
	PINCTRL_PIN(TZ1090_PIN_TX_ON,		"tx_on"),
	PINCTRL_PIN(TZ1090_PIN_RX_ON,		"rx_on"),
	PINCTRL_PIN(TZ1090_PIN_PLL_ON,		"pll_on"),
	PINCTRL_PIN(TZ1090_PIN_PA_ON,		"pa_on"),
	PINCTRL_PIN(TZ1090_PIN_RX_HP,		"rx_hp"),
	PINCTRL_PIN(TZ1090_PIN_GAIN0,		"gain0"),
	PINCTRL_PIN(TZ1090_PIN_GAIN1,		"gain1"),
	PINCTRL_PIN(TZ1090_PIN_GAIN2,		"gain2"),
	PINCTRL_PIN(TZ1090_PIN_GAIN3,		"gain3"),
	PINCTRL_PIN(TZ1090_PIN_GAIN4,		"gain4"),
	PINCTRL_PIN(TZ1090_PIN_GAIN5,		"gain5"),
	PINCTRL_PIN(TZ1090_PIN_GAIN6,		"gain6"),
	PINCTRL_PIN(TZ1090_PIN_GAIN7,		"gain7"),
	PINCTRL_PIN(TZ1090_PIN_ANT_SEL0,	"ant_sel0"),
	PINCTRL_PIN(TZ1090_PIN_ANT_SEL1,	"ant_sel1"),
	PINCTRL_PIN(TZ1090_PIN_SDH_CLK_IN,	"sdh_clk_in"),

	/* Non-GPIO pins */
	PINCTRL_PIN(TZ1090_PIN_TCK,		"tck"),
	PINCTRL_PIN(TZ1090_PIN_TRST,		"trst"),
	PINCTRL_PIN(TZ1090_PIN_TDI,		"tdi"),
	PINCTRL_PIN(TZ1090_PIN_TDO,		"tdo"),
	PINCTRL_PIN(TZ1090_PIN_TMS,		"tms"),
	PINCTRL_PIN(TZ1090_PIN_CLK_OUT0,	"clk_out0"),
	PINCTRL_PIN(TZ1090_PIN_CLK_OUT1,	"clk_out1"),
};

/* Pins in each pin group */

static const unsigned int spi1_cs2_pins[] = {
	TZ1090_PIN_SPI1_CS2,
};

static const unsigned int pdm_d_pins[] = {
	TZ1090_PIN_PDM_D,
};

static const unsigned int tft_pins[] = {
	TZ1090_PIN_TFT_RED0,
	TZ1090_PIN_TFT_RED1,
	TZ1090_PIN_TFT_RED2,
	TZ1090_PIN_TFT_RED3,
	TZ1090_PIN_TFT_RED4,
	TZ1090_PIN_TFT_RED5,
	TZ1090_PIN_TFT_RED6,
	TZ1090_PIN_TFT_RED7,
	TZ1090_PIN_TFT_GREEN0,
	TZ1090_PIN_TFT_GREEN1,
	TZ1090_PIN_TFT_GREEN2,
	TZ1090_PIN_TFT_GREEN3,
	TZ1090_PIN_TFT_GREEN4,
	TZ1090_PIN_TFT_GREEN5,
	TZ1090_PIN_TFT_GREEN6,
	TZ1090_PIN_TFT_GREEN7,
	TZ1090_PIN_TFT_BLUE0,
	TZ1090_PIN_TFT_BLUE1,
	TZ1090_PIN_TFT_BLUE2,
	TZ1090_PIN_TFT_BLUE3,
	TZ1090_PIN_TFT_BLUE4,
	TZ1090_PIN_TFT_BLUE5,
	TZ1090_PIN_TFT_BLUE6,
	TZ1090_PIN_TFT_BLUE7,
	TZ1090_PIN_TFT_VDDEN_GD,
	TZ1090_PIN_TFT_PANELCLK,
	TZ1090_PIN_TFT_BLANK_LS,
	TZ1090_PIN_TFT_VSYNC_NS,
	TZ1090_PIN_TFT_HSYNC_NR,
	TZ1090_PIN_TFT_VD12ACB,
	TZ1090_PIN_TFT_PWRSAVE,
};

static const unsigned int afe_pins[] = {
	TZ1090_PIN_TX_ON,
	TZ1090_PIN_RX_ON,
	TZ1090_PIN_PLL_ON,
	TZ1090_PIN_PA_ON,
	TZ1090_PIN_RX_HP,
	TZ1090_PIN_ANT_SEL0,
	TZ1090_PIN_ANT_SEL1,
	TZ1090_PIN_GAIN0,
	TZ1090_PIN_GAIN1,
	TZ1090_PIN_GAIN2,
	TZ1090_PIN_GAIN3,
	TZ1090_PIN_GAIN4,
	TZ1090_PIN_GAIN5,
	TZ1090_PIN_GAIN6,
	TZ1090_PIN_GAIN7,
};

static const unsigned int sdio_pins[] = {
	TZ1090_PIN_SDIO_CLK,
	TZ1090_PIN_SDIO_CMD,
	TZ1090_PIN_SDIO_D0,
	TZ1090_PIN_SDIO_D1,
	TZ1090_PIN_SDIO_D2,
	TZ1090_PIN_SDIO_D3,
};

static const unsigned int sdh_pins[] = {
	TZ1090_PIN_SDH_CD,
	TZ1090_PIN_SDH_WP,
	TZ1090_PIN_SDH_CLK_IN,
};

static const unsigned int spi0_pins[] = {
	TZ1090_PIN_SPI0_MCLK,
	TZ1090_PIN_SPI0_CS0,
	TZ1090_PIN_SPI0_CS1,
	TZ1090_PIN_SPI0_CS2,
	TZ1090_PIN_SPI0_DOUT,
	TZ1090_PIN_SPI0_DIN,
};

static const unsigned int spi1_pins[] = {
	TZ1090_PIN_SPI1_MCLK,
	TZ1090_PIN_SPI1_CS0,
	TZ1090_PIN_SPI1_CS1,
	TZ1090_PIN_SPI1_CS2,
	TZ1090_PIN_SPI1_DOUT,
	TZ1090_PIN_SPI1_DIN,
};

static const unsigned int uart0_pins[] = {
	TZ1090_PIN_UART0_RTS,
	TZ1090_PIN_UART0_CTS,
	TZ1090_PIN_UART0_TXD,
	TZ1090_PIN_UART0_RXD,
};

static const unsigned int uart1_pins[] = {
	TZ1090_PIN_UART1_TXD,
	TZ1090_PIN_UART1_RXD,
};

static const unsigned int uart_pins[] = {
	TZ1090_PIN_UART1_TXD,
	TZ1090_PIN_UART1_RXD,
	TZ1090_PIN_UART0_RTS,
	TZ1090_PIN_UART0_CTS,
	TZ1090_PIN_UART0_TXD,
	TZ1090_PIN_UART0_RXD,
};

static const unsigned int scb0_pins[] = {
	TZ1090_PIN_SCB0_SDAT,
	TZ1090_PIN_SCB0_SCLK,
};

static const unsigned int scb1_pins[] = {
	TZ1090_PIN_SCB1_SDAT,
	TZ1090_PIN_SCB1_SCLK,
};

static const unsigned int scb2_pins[] = {
	TZ1090_PIN_SCB2_SDAT,
	TZ1090_PIN_SCB2_SCLK,
};

static const unsigned int i2s_pins[] = {
	TZ1090_PIN_I2S_MCLK,
	TZ1090_PIN_I2S_BCLK_OUT,
	TZ1090_PIN_I2S_LRCLK_OUT,
	TZ1090_PIN_I2S_DOUT0,
	TZ1090_PIN_I2S_DOUT1,
	TZ1090_PIN_I2S_DOUT2,
	TZ1090_PIN_I2S_DIN,
};

static const unsigned int jtag_pins[] = {
	TZ1090_PIN_TCK,
	TZ1090_PIN_TRST,
	TZ1090_PIN_TDI,
	TZ1090_PIN_TDO,
	TZ1090_PIN_TMS,
};

/* Pins in each drive pin group */

static const unsigned int drive_sdio_pins[] = {
	TZ1090_PIN_SDIO_CLK,
	TZ1090_PIN_SDIO_CMD,
	TZ1090_PIN_SDIO_D0,
	TZ1090_PIN_SDIO_D1,
	TZ1090_PIN_SDIO_D2,
	TZ1090_PIN_SDIO_D3,
	TZ1090_PIN_SDH_WP,
	TZ1090_PIN_SDH_CD,
	TZ1090_PIN_SDH_CLK_IN,
};

static const unsigned int drive_i2s_pins[] = {
	TZ1090_PIN_CLK_OUT1,
	TZ1090_PIN_I2S_DIN,
	TZ1090_PIN_I2S_DOUT0,
	TZ1090_PIN_I2S_DOUT1,
	TZ1090_PIN_I2S_DOUT2,
	TZ1090_PIN_I2S_LRCLK_OUT,
	TZ1090_PIN_I2S_BCLK_OUT,
	TZ1090_PIN_I2S_MCLK,
};

static const unsigned int drive_scb0_pins[] = {
	TZ1090_PIN_SCB0_SCLK,
	TZ1090_PIN_SCB0_SDAT,
	TZ1090_PIN_PDM_D,
	TZ1090_PIN_PDM_C,
};

static const unsigned int drive_pdm_pins[] = {
	TZ1090_PIN_CLK_OUT0,
	TZ1090_PIN_PDM_B,
	TZ1090_PIN_PDM_A,
};

/* Pin groups each function can be muxed to */

/*
 * The magic "perip" function allows otherwise non-muxing pins to be enabled in
 * peripheral mode.
 */
static const char * const perip_groups[] = {
	/* non-muxing convenient gpio pingroups */
	"uart",
	"uart0",
	"uart1",
	"spi0",
	"spi1",
	"scb0",
	"scb1",
	"scb2",
	"i2s",
	/* individual pins not part of a pin mux group */
	"spi0_mclk",
	"spi0_cs0",
	"spi0_cs1",
	"spi0_cs2",
	"spi0_dout",
	"spi0_din",
	"spi1_mclk",
	"spi1_cs0",
	"spi1_cs1",
	"spi1_dout",
	"spi1_din",
	"uart0_rxd",
	"uart0_txd",
	"uart0_cts",
	"uart0_rts",
	"uart1_rxd",
	"uart1_txd",
	"scb0_sdat",
	"scb0_sclk",
	"scb1_sdat",
	"scb1_sclk",
	"scb2_sdat",
	"scb2_sclk",
	"i2s_mclk",
	"i2s_bclk_out",
	"i2s_lrclk_out",
	"i2s_dout0",
	"i2s_dout1",
	"i2s_dout2",
	"i2s_din",
	"pdm_a",
	"pdm_b",
	"pdm_c",
};

static const char * const sdh_sdio_groups[] = {
	"sdh",
	"sdio",
	/* sdh pins */
	"sdh_cd",
	"sdh_wp",
	"sdh_clk_in",
	/* sdio pins */
	"sdio_clk",
	"sdio_cmd",
	"sdio_d0",
	"sdio_d1",
	"sdio_d2",
	"sdio_d3",
};

static const char * const spi1_cs2_groups[] = {
	"spi1_cs2",
};

static const char * const pdm_dac_groups[] = {
	"pdm_d",
};

static const char * const usb_vbus_groups[] = {
	"spi1_cs2",
	"pdm_d",
};

static const char * const afe_groups[] = {
	"afe",
	/* afe pins */
	"tx_on",
	"rx_on",
	"pll_on",
	"pa_on",
	"rx_hp",
	"ant_sel0",
	"ant_sel1",
	"gain0",
	"gain1",
	"gain2",
	"gain3",
	"gain4",
	"gain5",
	"gain6",
	"gain7",
};

static const char * const tft_groups[] = {
	"tft",
	/* tft pins */
	"tft_red0",
	"tft_red1",
	"tft_red2",
	"tft_red3",
	"tft_red4",
	"tft_red5",
	"tft_red6",
	"tft_red7",
	"tft_green0",
	"tft_green1",
	"tft_green2",
	"tft_green3",
	"tft_green4",
	"tft_green5",
	"tft_green6",
	"tft_green7",
	"tft_blue0",
	"tft_blue1",
	"tft_blue2",
	"tft_blue3",
	"tft_blue4",
	"tft_blue5",
	"tft_blue6",
	"tft_blue7",
	"tft_vdden_gd",
	"tft_panelclk",
	"tft_blank_ls",
	"tft_vsync_ns",
	"tft_hsync_nr",
	"tft_vd12acb",
	"tft_pwrsave",
};

/* Mux functions that can be used by a mux */

enum tz1090_mux {
	/* internal placeholder */
	TZ1090_MUX_NA = -1,
	/* magic per-non-muxing-GPIO-pin peripheral mode mux */
	TZ1090_MUX_PERIP,
	/* SDH/SDIO mux */
	TZ1090_MUX_SDH,
	TZ1090_MUX_SDIO,
	/* USB_VBUS muxes */
	TZ1090_MUX_SPI1_CS2,
	TZ1090_MUX_PDM_DAC,
	TZ1090_MUX_USB_VBUS,
	/* AFE mux */
	TZ1090_MUX_AFE,
	TZ1090_MUX_TS_OUT_0,
	/* EXT_DAC mux */
	TZ1090_MUX_DAC,
	TZ1090_MUX_NOT_IQADC_STB,
	TZ1090_MUX_IQDAC_STB,
	/* TFT mux */
	TZ1090_MUX_TFT,
	TZ1090_MUX_EXT_DAC,
	TZ1090_MUX_TS_OUT_1,
	TZ1090_MUX_LCD_TRACE,
	TZ1090_MUX_PHY_RINGOSC,
};

#define FUNCTION(mux, fname, group)			\
	[(TZ1090_MUX_ ## mux)] = {			\
		.name = #fname,				\
		.groups = group##_groups,		\
		.ngroups = ARRAY_SIZE(group##_groups),	\
	}
/* For intermediate functions with submuxes */
#define NULL_FUNCTION(mux, fname)			\
	[(TZ1090_MUX_ ## mux)] = {			\
		.name = #fname,				\
	}

/* Must correlate with enum tz1090_mux */
static const struct tz1090_function tz1090_functions[] = {
	/*	 FUNCTION	function name	pingroups */
	FUNCTION(PERIP,		perip,		perip),
	FUNCTION(SDH,		sdh,		sdh_sdio),
	FUNCTION(SDIO,		sdio,		sdh_sdio),
	FUNCTION(SPI1_CS2,	spi1_cs2,	spi1_cs2),
	FUNCTION(PDM_DAC,	pdm_dac,	pdm_dac),
	FUNCTION(USB_VBUS,	usb_vbus,	usb_vbus),
	FUNCTION(AFE,		afe,		afe),
	FUNCTION(TS_OUT_0,	ts_out_0,	afe),
	FUNCTION(DAC,		ext_dac,	tft),
	FUNCTION(NOT_IQADC_STB,	not_iqadc_stb,	tft),
	FUNCTION(IQDAC_STB,	iqdac_stb,	tft),
	FUNCTION(TFT,		tft,		tft),
	NULL_FUNCTION(EXT_DAC,	_ext_dac),
	FUNCTION(TS_OUT_1,	ts_out_1,	tft),
	FUNCTION(LCD_TRACE,	lcd_trace,	tft),
	FUNCTION(PHY_RINGOSC,	phy_ringosc,	tft),
};

/* Sub muxes */

/**
 * MUX() - Initialise a mux description.
 * @f0:		Function 0 (TZ1090_MUX_ is prepended, NA for none)
 * @f1:		Function 1 (TZ1090_MUX_ is prepended, NA for none)
 * @f2:		Function 2 (TZ1090_MUX_ is prepended, NA for none)
 * @f3:		Function 3 (TZ1090_MUX_ is prepended, NA for none)
 * @f4:		Function 4 (TZ1090_MUX_ is prepended, NA for none)
 * @mux_r:	Mux register (REG_PINCTRL_ is prepended)
 * @mux_b:	Bit number in register that the mux field begins
 * @mux_w:	Width of mux field in register
 */
#define MUX(f0, f1, f2, f3, f4, mux_r, mux_b, mux_w)		\
	{							\
		.funcs = {					\
			TZ1090_MUX_ ## f0,			\
			TZ1090_MUX_ ## f1,			\
			TZ1090_MUX_ ## f2,			\
			TZ1090_MUX_ ## f3,			\
			TZ1090_MUX_ ## f4,			\
		},						\
		.reg = (REG_PINCTRL_ ## mux_r),			\
		.bit = (mux_b),					\
		.width = (mux_w),				\
	}

/**
 * DEFINE_SUBMUX() - Defines a submux description separate from a pin group.
 * @mux:	Mux name (_submux is appended)
 * @f0:		Function 0 (TZ1090_MUX_ is prepended, NA for none)
 * @f1:		Function 1 (TZ1090_MUX_ is prepended, NA for none)
 * @f2:		Function 2 (TZ1090_MUX_ is prepended, NA for none)
 * @f3:		Function 3 (TZ1090_MUX_ is prepended, NA for none)
 * @f4:		Function 4 (TZ1090_MUX_ is prepended, NA for none)
 * @mux_r:	Mux register (REG_PINCTRL_ is prepended)
 * @mux_b:	Bit number in register that the mux field begins
 * @mux_w:	Width of mux field in register
 *
 * A sub mux is a nested mux that can be bound to a magic function number used
 * by another mux description. For example value 4 of the top level mux might
 * correspond to a function which has a submux pointed to in tz1090_submux[].
 * The outer mux can then take on any function in the top level mux or the
 * submux, and if a submux function is chosen both muxes are updated to route
 * the signal from the submux.
 *
 * The submux can be defined with DEFINE_SUBMUX and pointed to from
 * tz1090_submux[] using SUBMUX.
 */
#define DEFINE_SUBMUX(mux, f0, f1, f2, f3, f4, mux_r, mux_b, mux_w)	\
	static struct tz1090_muxdesc mux ## _submux =			\
		MUX(f0, f1, f2, f3, f4, mux_r, mux_b, mux_w)

/**
 * SUBMUX() - Link a submux to a function number.
 * @f:		Function name (TZ1090_MUX_ is prepended)
 * @submux:	Submux name (_submux is appended)
 *
 * For use in tz1090_submux[] initialisation to link an intermediate function
 * number to a particular submux description. It indicates that when the
 * function is chosen the signal is connected to the submux.
 */
#define SUBMUX(f, submux)	[(TZ1090_MUX_ ## f)] = &(submux ## _submux)

/**
 * MUX_PG() - Initialise a pin group with mux control
 * @pg_name:	Pin group name (stringified, _pins appended to get pins array)
 * @f0:		Function 0 (TZ1090_MUX_ is prepended, NA for none)
 * @f1:		Function 1 (TZ1090_MUX_ is prepended, NA for none)
 * @f2:		Function 2 (TZ1090_MUX_ is prepended, NA for none)
 * @f3:		Function 3 (TZ1090_MUX_ is prepended, NA for none)
 * @f4:		Function 4 (TZ1090_MUX_ is prepended, NA for none)
 * @mux_r:	Mux register (REG_PINCTRL_ is prepended)
 * @mux_b:	Bit number in register that the mux field begins
 * @mux_w:	Width of mux field in register
 */
#define MUX_PG(pg_name, f0, f1, f2, f3, f4,			\
	       mux_r, mux_b, mux_w)				\
	{							\
		.name = #pg_name,				\
		.pins = pg_name##_pins,				\
		.npins = ARRAY_SIZE(pg_name##_pins),		\
		.mux = MUX(f0, f1, f2, f3, f4,			\
			   mux_r, mux_b, mux_w),		\
	}

/**
 * SIMPLE_PG() - Initialise a simple convenience pin group
 * @pg_name:	Pin group name (stringified, _pins appended to get pins array)
 *
 * A simple pin group is simply used for binding pins together so they can be
 * referred to by a single name instead of having to list every pin
 * individually.
 */
#define SIMPLE_PG(pg_name)					\
	{							\
		.name = #pg_name,				\
		.pins = pg_name##_pins,				\
		.npins = ARRAY_SIZE(pg_name##_pins),		\
	}

/**
 * DRV_PG() - Initialise a pin group with drive control
 * @pg_name:	Pin group name (stringified, _pins appended to get pins array)
 * @slw_b:	Slew register bit.
 *		The same bit is used for Schmitt, and Drive (*2).
 */
#define DRV_PG(pg_name, slw_b)					\
	{							\
		.name = #pg_name,				\
		.pins = pg_name##_pins,				\
		.npins = ARRAY_SIZE(pg_name##_pins),		\
		.drv = true,					\
		.slw_bit = (slw_b),				\
	}

/*
 * Define main muxing pin groups
 */

/* submuxes */

/*            name     f0,  f1,            f2,        f3, f4, mux r/b/w */
DEFINE_SUBMUX(ext_dac, DAC, NOT_IQADC_STB, IQDAC_STB, NA, NA, IF_CTL, 6, 2);

/* bind submuxes to internal functions */
static struct tz1090_muxdesc *tz1090_submux[] = {
	SUBMUX(EXT_DAC, ext_dac),
};

/*
 * These are the pin mux groups. Pin muxing can be enabled and disabled for each
 * pin individually so these groups are internal. The mapping of pins to pin mux
 * group is below (tz1090_mux_pins).
 */
static struct tz1090_pingroup tz1090_mux_groups[] = {
	/* Muxing pin groups */
	/*     pg_name,  f0,       f1,       f2,       f3,        f4,          mux r/b/w */
	MUX_PG(sdh,      SDH,      SDIO,     NA,       NA,        NA,          IF_CTL, 20, 2),
	MUX_PG(sdio,     SDIO,     SDH,      NA,       NA,        NA,          IF_CTL, 16, 2),
	MUX_PG(spi1_cs2, SPI1_CS2, USB_VBUS, NA,       NA,        NA,          IF_CTL, 10, 2),
	MUX_PG(pdm_d,    PDM_DAC,  USB_VBUS, NA,       NA,        NA,          IF_CTL,  8, 2),
	MUX_PG(afe,      AFE,      TS_OUT_0, NA,       NA,        NA,          IF_CTL,  4, 2),
	MUX_PG(tft,      TFT,      EXT_DAC,  TS_OUT_1, LCD_TRACE, PHY_RINGOSC, IF_CTL,  0, 3),
};

/*
 * This is the mapping from GPIO pins to pin mux groups in tz1090_mux_groups[].
 * Pins which aren't muxable to multiple peripherals are set to
 * TZ1090_MUX_GROUP_MAX to enable the "perip" function to enable/disable
 * peripheral control of the pin.
 *
 * This array is initialised in tz1090_init_mux_pins().
 */
static u8 tz1090_mux_pins[NUM_GPIOS];

/* TZ1090_MUX_GROUP_MAX is used in tz1090_mux_pins[] for non-muxing pins */
#define TZ1090_MUX_GROUP_MAX ARRAY_SIZE(tz1090_mux_groups)

/**
 * tz1090_init_mux_pins() - Initialise GPIO pin to mux group mapping.
 *
 * Initialises the tz1090_mux_pins[] array to be the inverse of the pin lists in
 * each pin mux group in tz1090_mux_groups[].
 *
 * It is assumed that no pin mux groups overlap (share pins).
 */
static void __init tz1090_init_mux_pins(void)
{
	unsigned int g, p;
	const struct tz1090_pingroup *grp;
	const unsigned int *pin;

	for (p = 0; p < NUM_GPIOS; ++p)
		tz1090_mux_pins[p] = TZ1090_MUX_GROUP_MAX;

	grp = tz1090_mux_groups;
	for (g = 0, grp = tz1090_mux_groups;
	     g < ARRAY_SIZE(tz1090_mux_groups); ++g, ++grp)
		for (pin = grp->pins, p = 0; p < grp->npins; ++p, ++pin)
			tz1090_mux_pins[*pin] = g;
}

/*
 * These are the externally visible pin groups. Some of them allow group control
 * of drive configuration. Some are just simple convenience pingroups. All the
 * internal pin mux groups in tz1090_mux_groups[] are mirrored here with the
 * same pins.
 * Pseudo pin groups follow in the group numbers after this array for each GPIO
 * pin. Any group used for muxing must have all pins belonging to the same pin
 * mux group.
 */
static struct tz1090_pingroup tz1090_groups[] = {
	/* Pin groups with drive control (with no out of place pins) */
	/*     pg_name,		slw/schmitt/drv b */
	DRV_PG(jtag,		11 /* 11, 22 */),
	DRV_PG(tft,		10 /* 10, 20 */),
	DRV_PG(scb2,		9  /*  9, 18 */),
	DRV_PG(spi0,		7  /*  7, 14 */),
	DRV_PG(uart,		5  /*  5, 10 */),
	DRV_PG(scb1,		4  /*  4,  8 */),
	DRV_PG(spi1,		3  /*  3,  6 */),
	DRV_PG(afe,		0  /*  0,  0 */),

	/*
	 * Drive specific pin groups (with odd combinations of pins which makes
	 * the pin group naming somewhat arbitrary)
	 */
	/*     pg_name,		slw/schmitt/drv b */
	DRV_PG(drive_sdio,	8  /*  8, 16 */), /* sdio_* + sdh_* */
	DRV_PG(drive_i2s,	6  /*  6, 12 */), /* i2s_* + clk_out1 */
	DRV_PG(drive_scb0,	2  /*  2,  4 */), /* scb0_* + pdm_{c,d} */
	DRV_PG(drive_pdm,	1  /*  1,  2 */), /* pdm_{a,b} + clk_out0 */

	/* Convenience pin groups */
	/*        pg_name */
	SIMPLE_PG(uart0),
	SIMPLE_PG(uart1),
	SIMPLE_PG(scb0),
	SIMPLE_PG(i2s),
	SIMPLE_PG(sdh),
	SIMPLE_PG(sdio),

	/* pseudo-pingroups for each GPIO pin follow */
};

/**
 * struct tz1090_pmx - Private pinctrl data
 * @dev:	Platform device
 * @pctl:	Pin control device
 * @regs:	Register region
 * @lock:	Lock protecting coherency of pin_en, gpio_en, and SELECT regs
 * @pin_en:	Pins that have been enabled (32 pins packed into each element)
 * @gpio_en:	GPIOs that have been enabled (32 pins packed into each element)
 */
struct tz1090_pmx {
	struct device		*dev;
	struct pinctrl_dev	*pctl;
	void __iomem		*regs;
	spinlock_t		lock;
	u32			pin_en[3];
	u32			gpio_en[3];
};

static inline u32 pmx_read(struct tz1090_pmx *pmx, u32 reg)
{
	return ioread32(pmx->regs + reg);
}

static inline void pmx_write(struct tz1090_pmx *pmx, u32 val, u32 reg)
{
	iowrite32(val, pmx->regs + reg);
}

/*
 * Pin control operations
 */

/* each GPIO pin has it's own pseudo pingroup containing only itself */

static int tz1090_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(tz1090_groups) + NUM_GPIOS;
}

static const char *tz1090_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						 unsigned int group)
{
	if (group < ARRAY_SIZE(tz1090_groups)) {
		/* normal pingroup */
		return tz1090_groups[group].name;
	} else {
		/* individual gpio pin pseudo-pingroup */
		unsigned int pin = group - ARRAY_SIZE(tz1090_groups);
		return tz1090_pins[pin].name;
	}
}

static int tz1090_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					 unsigned int group,
					 const unsigned int **pins,
					 unsigned int *num_pins)
{
	if (group < ARRAY_SIZE(tz1090_groups)) {
		/* normal pingroup */
		*pins = tz1090_groups[group].pins;
		*num_pins = tz1090_groups[group].npins;
	} else {
		/* individual gpio pin pseudo-pingroup */
		unsigned int pin = group - ARRAY_SIZE(tz1090_groups);
		*pins = &tz1090_pins[pin].number;
		*num_pins = 1;
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void tz1090_pinctrl_pin_dbg_show(struct pinctrl_dev *pctldev,
					struct seq_file *s,
					unsigned int offset)
{
	seq_printf(s, " %s", dev_name(pctldev->dev));
}
#endif

static int reserve_map(struct device *dev, struct pinctrl_map **map,
		       unsigned int *reserved_maps, unsigned int *num_maps,
		       unsigned int reserve)
{
	unsigned int old_num = *reserved_maps;
	unsigned int new_num = *num_maps + reserve;
	struct pinctrl_map *new_map;

	if (old_num >= new_num)
		return 0;

	new_map = krealloc(*map, sizeof(*new_map) * new_num, GFP_KERNEL);
	if (!new_map) {
		dev_err(dev, "krealloc(map) failed\n");
		return -ENOMEM;
	}

	memset(new_map + old_num, 0, (new_num - old_num) * sizeof(*new_map));

	*map = new_map;
	*reserved_maps = new_num;

	return 0;
}

static int add_map_mux(struct pinctrl_map **map, unsigned int *reserved_maps,
		       unsigned int *num_maps, const char *group,
		       const char *function)
{
	if (WARN_ON(*num_maps == *reserved_maps))
		return -ENOSPC;

	(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)[*num_maps].data.mux.group = group;
	(*map)[*num_maps].data.mux.function = function;
	(*num_maps)++;

	return 0;
}

static int add_map_configs(struct device *dev,
			   struct pinctrl_map **map,
			   unsigned int *reserved_maps, unsigned int *num_maps,
			   const char *group, unsigned long *configs,
			   unsigned int num_configs)
{
	unsigned long *dup_configs;

	if (WARN_ON(*num_maps == *reserved_maps))
		return -ENOSPC;

	dup_configs = kmemdup(configs, num_configs * sizeof(*dup_configs),
			      GFP_KERNEL);
	if (!dup_configs) {
		dev_err(dev, "kmemdup(configs) failed\n");
		return -ENOMEM;
	}

	(*map)[*num_maps].type = PIN_MAP_TYPE_CONFIGS_GROUP;
	(*map)[*num_maps].data.configs.group_or_pin = group;
	(*map)[*num_maps].data.configs.configs = dup_configs;
	(*map)[*num_maps].data.configs.num_configs = num_configs;
	(*num_maps)++;

	return 0;
}

static void tz1090_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
				       struct pinctrl_map *map,
				       unsigned int num_maps)
{
	int i;

	for (i = 0; i < num_maps; i++)
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP)
			kfree(map[i].data.configs.configs);

	kfree(map);
}

static int tz1090_pinctrl_dt_subnode_to_map(struct device *dev,
					    struct device_node *np,
					    struct pinctrl_map **map,
					    unsigned int *reserved_maps,
					    unsigned int *num_maps)
{
	int ret;
	const char *function;
	unsigned long *configs = NULL;
	unsigned int num_configs = 0;
	unsigned int reserve;
	struct property *prop;
	const char *group;

	ret = of_property_read_string(np, "tz1090,function", &function);
	if (ret < 0) {
		/* EINVAL=missing, which is fine since it's optional */
		if (ret != -EINVAL)
			dev_err(dev, "could not parse property function\n");
		function = NULL;
	}

	ret = pinconf_generic_parse_dt_config(np, &configs, &num_configs);
	if (ret)
		return ret;

	reserve = 0;
	if (function != NULL)
		reserve++;
	if (num_configs)
		reserve++;
	ret = of_property_count_strings(np, "tz1090,pins");
	if (ret < 0) {
		dev_err(dev, "could not parse property pins\n");
		goto exit;
	}
	reserve *= ret;

	ret = reserve_map(dev, map, reserved_maps, num_maps, reserve);
	if (ret < 0)
		goto exit;

	of_property_for_each_string(np, "tz1090,pins", prop, group) {
		if (function) {
			ret = add_map_mux(map, reserved_maps, num_maps,
					  group, function);
			if (ret < 0)
				goto exit;
		}

		if (num_configs) {
			ret = add_map_configs(dev, map, reserved_maps,
					      num_maps, group, configs,
					      num_configs);
			if (ret < 0)
				goto exit;
		}
	}

	ret = 0;

exit:
	kfree(configs);
	return ret;
}

static int tz1090_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					 struct device_node *np_config,
					 struct pinctrl_map **map,
					 unsigned int *num_maps)
{
	unsigned int reserved_maps;
	struct device_node *np;
	int ret;

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	for_each_child_of_node(np_config, np) {
		ret = tz1090_pinctrl_dt_subnode_to_map(pctldev->dev, np, map,
						       &reserved_maps,
						       num_maps);
		if (ret < 0) {
			tz1090_pinctrl_dt_free_map(pctldev, *map, *num_maps);
			return ret;
		}
	}

	return 0;
}

static struct pinctrl_ops tz1090_pinctrl_ops = {
	.get_groups_count	= tz1090_pinctrl_get_groups_count,
	.get_group_name		= tz1090_pinctrl_get_group_name,
	.get_group_pins		= tz1090_pinctrl_get_group_pins,
#ifdef CONFIG_DEBUG_FS
	.pin_dbg_show		= tz1090_pinctrl_pin_dbg_show,
#endif
	.dt_node_to_map		= tz1090_pinctrl_dt_node_to_map,
	.dt_free_map		= tz1090_pinctrl_dt_free_map,
};

/*
 * Pin mux operations
 */

static int tz1090_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(tz1090_functions);
}

static const char *tz1090_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
						unsigned int function)
{
	return tz1090_functions[function].name;
}

static int tz1090_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
					  unsigned int function,
					  const char * const **groups,
					  unsigned int * const num_groups)
{
	/* pingroup functions */
	*groups = tz1090_functions[function].groups;
	*num_groups = tz1090_functions[function].ngroups;
	return 0;
}

/**
 * tz1090_pinctrl_select() - update bit in SELECT register
 * @pmx:		Pinmux data
 * @pin:		Pin number (must be within GPIO range)
 */
static void tz1090_pinctrl_select(struct tz1090_pmx *pmx,
				  unsigned int pin)
{
	u32 reg, reg_shift, select, val;
	unsigned int pmx_index, pmx_shift;
	unsigned long flags;

	/* uses base 32 instead of base 30 */
	pmx_index = pin >> 5;
	pmx_shift = pin & 0x1f;

	/* select = !perip || gpio */
	select = ((~pmx->pin_en[pmx_index] |
		   pmx->gpio_en[pmx_index]) >> pmx_shift) & 1;

	/* find register and bit offset (base 30) */
	reg = REG_PINCTRL_SELECT + 4*(pin / 30);
	reg_shift = pin % 30;

	/* modify gpio select bit */
	__global_lock2(flags);
	val = pmx_read(pmx, reg);
	val &= ~BIT(reg_shift);
	val |= select << reg_shift;
	pmx_write(pmx, val, reg);
	__global_unlock2(flags);
}

/**
 * tz1090_pinctrl_gpio_select() - enable/disable GPIO usage for a pin
 * @pmx:		Pinmux data
 * @pin:		Pin number
 * @gpio_select:	true to enable pin as GPIO,
 *			false to leave control to whatever function is enabled
 *
 * Records that GPIO usage is enabled/disabled so that enabling a function
 * doesn't override the SELECT register bit.
 */
static void tz1090_pinctrl_gpio_select(struct tz1090_pmx *pmx,
				       unsigned int pin,
				       bool gpio_select)
{
	unsigned int index, shift;
	u32 gpio_en;

	if (pin >= NUM_GPIOS)
		return;

	/* uses base 32 instead of base 30 */
	index = pin >> 5;
	shift = pin & 0x1f;

	spin_lock(&pmx->lock);

	/* keep a record whether gpio is selected */
	gpio_en = pmx->gpio_en[index];
	gpio_en &= ~BIT(shift);
	if (gpio_select)
		gpio_en |= BIT(shift);
	pmx->gpio_en[index] = gpio_en;

	/* update the select bit */
	tz1090_pinctrl_select(pmx, pin);

	spin_unlock(&pmx->lock);
}

/**
 * tz1090_pinctrl_perip_select() - enable/disable peripheral interface for a pin
 * @pmx:		Pinmux data
 * @pin:		Pin number
 * @perip_select:	true to enable peripheral interface when not GPIO,
 *			false to leave pin in GPIO mode
 *
 * Records that peripheral usage is enabled/disabled so that SELECT register can
 * be set appropriately when GPIO is disabled.
 */
static void tz1090_pinctrl_perip_select(struct tz1090_pmx *pmx,
					unsigned int pin,
					bool perip_select)
{
	unsigned int index, shift;
	u32 pin_en;

	if (pin >= NUM_GPIOS)
		return;

	/* uses base 32 instead of base 30 */
	index = pin >> 5;
	shift = pin & 0x1f;

	spin_lock(&pmx->lock);

	/* keep a record whether peripheral is selected */
	pin_en = pmx->pin_en[index];
	pin_en &= ~BIT(shift);
	if (perip_select)
		pin_en |= BIT(shift);
	pmx->pin_en[index] = pin_en;

	/* update the select bit */
	tz1090_pinctrl_select(pmx, pin);

	spin_unlock(&pmx->lock);
}

/**
 * tz1090_pinctrl_enable_mux() - Switch a pin mux group to a function.
 * @pmx:		Pinmux data
 * @desc:		Pinmux description
 * @function:		Function to switch to
 *
 * Enable a particular function on a pin mux group. Since pin mux descriptions
 * are nested this function is recursive.
 */
static int tz1090_pinctrl_enable_mux(struct tz1090_pmx *pmx,
				     const struct tz1090_muxdesc *desc,
				     unsigned int function)
{
	const int *fit;
	unsigned long flags;
	int mux;
	unsigned int func, ret;
	u32 reg, mask;

	/* find the mux value for this function, searching recursively */
	for (mux = 0, fit = desc->funcs;
	     mux < ARRAY_SIZE(desc->funcs); ++mux, ++fit) {
		func = *fit;
		if (func == function)
			goto found_mux;

		/* maybe it's a sub-mux */
		if (func < ARRAY_SIZE(tz1090_submux) && tz1090_submux[func]) {
			ret = tz1090_pinctrl_enable_mux(pmx,
							tz1090_submux[func],
							function);
			if (!ret)
				goto found_mux;
		}
	}

	return -EINVAL;
found_mux:

	/* Set up the mux */
	if (desc->width) {
		mask = (BIT(desc->width) - 1) << desc->bit;
		__global_lock2(flags);
		reg = pmx_read(pmx, desc->reg);
		reg &= ~mask;
		reg |= (mux << desc->bit) & mask;
		pmx_write(pmx, reg, desc->reg);
		__global_unlock2(flags);
	}

	return 0;
}

/**
 * tz1090_pinctrl_enable() - Enable a function on a pin group.
 * @pctldev:		Pin control data
 * @function:		Function index to enable
 * @group:		Group index to enable
 *
 * Enable a particular function on a group of pins. The per GPIO pin pseudo pin
 * groups can be used (in which case the pin will be enabled in peripheral mode
 * and if it belongs to a pin mux group the mux will be switched if it isn't
 * already in use. Some convenience pin groups can also be used in which case
 * the effect is the same as enabling the function on each individual pin in the
 * group.
 */
static int tz1090_pinctrl_enable(struct pinctrl_dev *pctldev,
				 unsigned int function, unsigned int group)
{
	struct tz1090_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct tz1090_pingroup *grp;
	int ret;
	unsigned int pin_num, mux_group, i, npins;
	const unsigned int *pins;

	/* group of pins? */
	if (group < ARRAY_SIZE(tz1090_groups)) {
		grp = &tz1090_groups[group];
		npins = grp->npins;
		pins = grp->pins;
		/*
		 * All pins in the group must belong to the same mux group,
		 * which allows us to just use the mux group of the first pin.
		 * By explicitly listing permitted pingroups for each function
		 * the pinmux core should ensure this is always the case.
		 */
	} else {
		pin_num = group - ARRAY_SIZE(tz1090_groups);
		npins = 1;
		pins = &pin_num;
	}
	mux_group = tz1090_mux_pins[*pins];

	/* no mux group, but can still be individually muxed to peripheral */
	if (mux_group >= TZ1090_MUX_GROUP_MAX) {
		if (function == TZ1090_MUX_PERIP)
			goto mux_pins;
		return -EINVAL;
	}

	/* mux group already set to a different function? */
	grp = &tz1090_mux_groups[mux_group];
	if (grp->func_count && grp->func != function) {
		dev_err(pctldev->dev,
			"%s: can't mux pin(s) to '%s', group already muxed to '%s'\n",
			__func__, tz1090_functions[function].name,
			tz1090_functions[grp->func].name);
		return -EBUSY;
	}

	dev_dbg(pctldev->dev, "%s: muxing %u pin(s) in '%s' to '%s'\n",
		__func__, npins, grp->name, tz1090_functions[function].name);

	/* if first pin in mux group to be enabled, enable the group mux */
	if (!grp->func_count) {
		grp->func = function;
		ret = tz1090_pinctrl_enable_mux(pmx, &grp->mux, function);
		if (ret)
			return ret;
	}
	/* add pins to ref count and mux individually to peripheral */
	grp->func_count += npins;
mux_pins:
	for (i = 0; i < npins; ++i)
		tz1090_pinctrl_perip_select(pmx, pins[i], true);

	return 0;
}

/**
 * tz1090_pinctrl_gpio_request_enable() - Put pin in GPIO mode.
 * @pctldev:		Pin control data
 * @range:		GPIO range
 * @pin:		Pin number
 *
 * Puts a particular pin into GPIO mode, disabling peripheral control until it's
 * disabled again.
 */
static int tz1090_pinctrl_gpio_request_enable(struct pinctrl_dev *pctldev,
					      struct pinctrl_gpio_range *range,
					      unsigned int pin)
{
	struct tz1090_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	tz1090_pinctrl_gpio_select(pmx, pin, true);
	return 0;
}

/**
 * tz1090_pinctrl_gpio_disable_free() - Take pin out of GPIO mode.
 * @pctldev:		Pin control data
 * @range:		GPIO range
 * @pin:		Pin number
 *
 * Take a particular pin out of GPIO mode. If the pin is enabled for a
 * peripheral it will return to peripheral mode.
 */
static void tz1090_pinctrl_gpio_disable_free(struct pinctrl_dev *pctldev,
					     struct pinctrl_gpio_range *range,
					     unsigned int pin)
{
	struct tz1090_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	tz1090_pinctrl_gpio_select(pmx, pin, false);
}

static struct pinmux_ops tz1090_pinmux_ops = {
	.get_functions_count	= tz1090_pinctrl_get_funcs_count,
	.get_function_name	= tz1090_pinctrl_get_func_name,
	.get_function_groups	= tz1090_pinctrl_get_func_groups,
	.enable			= tz1090_pinctrl_enable,
	.gpio_request_enable	= tz1090_pinctrl_gpio_request_enable,
	.gpio_disable_free	= tz1090_pinctrl_gpio_disable_free,
};

/*
 * Pin config operations
 */

struct tz1090_pinconf_pullup {
	unsigned char index;
	unsigned char shift;
};

/* The mapping of pin to pull up/down register index and shift */
static struct tz1090_pinconf_pullup tz1090_pinconf_pullup[] = {
	{5, 22}, /*  0 - TZ1090_PIN_SDIO_CLK */
	{0, 14}, /*  1 - TZ1090_PIN_SDIO_CMD */
	{0,  6}, /*  2 - TZ1090_PIN_SDIO_D0 */
	{0,  8}, /*  3 - TZ1090_PIN_SDIO_D1 */
	{0, 10}, /*  4 - TZ1090_PIN_SDIO_D2 */
	{0, 12}, /*  5 - TZ1090_PIN_SDIO_D3 */
	{0,  2}, /*  6 - TZ1090_PIN_SDH_CD */
	{0,  4}, /*  7 - TZ1090_PIN_SDH_WP */
	{0, 16}, /*  8 - TZ1090_PIN_SPI0_MCLK */
	{0, 18}, /*  9 - TZ1090_PIN_SPI0_CS0 */
	{0, 20}, /* 10 - TZ1090_PIN_SPI0_CS1 */
	{0, 22}, /* 11 - TZ1090_PIN_SPI0_CS2 */
	{0, 24}, /* 12 - TZ1090_PIN_SPI0_DOUT */
	{0, 26}, /* 13 - TZ1090_PIN_SPI0_DIN */
	{0, 28}, /* 14 - TZ1090_PIN_SPI1_MCLK */
	{0, 30}, /* 15 - TZ1090_PIN_SPI1_CS0 */
	{1,  0}, /* 16 - TZ1090_PIN_SPI1_CS1 */
	{1,  2}, /* 17 - TZ1090_PIN_SPI1_CS2 */
	{1,  4}, /* 18 - TZ1090_PIN_SPI1_DOUT */
	{1,  6}, /* 19 - TZ1090_PIN_SPI1_DIN */
	{1,  8}, /* 20 - TZ1090_PIN_UART0_RXD */
	{1, 10}, /* 21 - TZ1090_PIN_UART0_TXD */
	{1, 12}, /* 22 - TZ1090_PIN_UART0_CTS */
	{1, 14}, /* 23 - TZ1090_PIN_UART0_RTS */
	{1, 16}, /* 24 - TZ1090_PIN_UART1_RXD */
	{1, 18}, /* 25 - TZ1090_PIN_UART1_TXD */
	{1, 20}, /* 26 - TZ1090_PIN_SCB0_SDAT */
	{1, 22}, /* 27 - TZ1090_PIN_SCB0_SCLK */
	{1, 24}, /* 28 - TZ1090_PIN_SCB1_SDAT */
	{1, 26}, /* 29 - TZ1090_PIN_SCB1_SCLK */

	{1, 28}, /* 30 - TZ1090_PIN_SCB2_SDAT */
	{1, 30}, /* 31 - TZ1090_PIN_SCB2_SCLK */
	{2,  0}, /* 32 - TZ1090_PIN_I2S_MCLK */
	{2,  2}, /* 33 - TZ1090_PIN_I2S_BCLK_OUT */
	{2,  4}, /* 34 - TZ1090_PIN_I2S_LRCLK_OUT */
	{2,  6}, /* 35 - TZ1090_PIN_I2S_DOUT0 */
	{2,  8}, /* 36 - TZ1090_PIN_I2S_DOUT1 */
	{2, 10}, /* 37 - TZ1090_PIN_I2S_DOUT2 */
	{2, 12}, /* 38 - TZ1090_PIN_I2S_DIN */
	{4, 12}, /* 39 - TZ1090_PIN_PDM_A */
	{4, 14}, /* 40 - TZ1090_PIN_PDM_B */
	{4, 18}, /* 41 - TZ1090_PIN_PDM_C */
	{4, 20}, /* 42 - TZ1090_PIN_PDM_D */
	{2, 14}, /* 43 - TZ1090_PIN_TFT_RED0 */
	{2, 16}, /* 44 - TZ1090_PIN_TFT_RED1 */
	{2, 18}, /* 45 - TZ1090_PIN_TFT_RED2 */
	{2, 20}, /* 46 - TZ1090_PIN_TFT_RED3 */
	{2, 22}, /* 47 - TZ1090_PIN_TFT_RED4 */
	{2, 24}, /* 48 - TZ1090_PIN_TFT_RED5 */
	{2, 26}, /* 49 - TZ1090_PIN_TFT_RED6 */
	{2, 28}, /* 50 - TZ1090_PIN_TFT_RED7 */
	{2, 30}, /* 51 - TZ1090_PIN_TFT_GREEN0 */
	{3,  0}, /* 52 - TZ1090_PIN_TFT_GREEN1 */
	{3,  2}, /* 53 - TZ1090_PIN_TFT_GREEN2 */
	{3,  4}, /* 54 - TZ1090_PIN_TFT_GREEN3 */
	{3,  6}, /* 55 - TZ1090_PIN_TFT_GREEN4 */
	{3,  8}, /* 56 - TZ1090_PIN_TFT_GREEN5 */
	{3, 10}, /* 57 - TZ1090_PIN_TFT_GREEN6 */
	{3, 12}, /* 58 - TZ1090_PIN_TFT_GREEN7 */
	{3, 14}, /* 59 - TZ1090_PIN_TFT_BLUE0 */

	{3, 16}, /* 60 - TZ1090_PIN_TFT_BLUE1 */
	{3, 18}, /* 61 - TZ1090_PIN_TFT_BLUE2 */
	{3, 20}, /* 62 - TZ1090_PIN_TFT_BLUE3 */
	{3, 22}, /* 63 - TZ1090_PIN_TFT_BLUE4 */
	{3, 24}, /* 64 - TZ1090_PIN_TFT_BLUE5 */
	{3, 26}, /* 65 - TZ1090_PIN_TFT_BLUE6 */
	{3, 28}, /* 66 - TZ1090_PIN_TFT_BLUE7 */
	{3, 30}, /* 67 - TZ1090_PIN_TFT_VDDEN_GD */
	{4,  0}, /* 68 - TZ1090_PIN_TFT_PANELCLK */
	{4,  2}, /* 69 - TZ1090_PIN_TFT_BLANK_LS */
	{4,  4}, /* 70 - TZ1090_PIN_TFT_VSYNC_NS */
	{4,  6}, /* 71 - TZ1090_PIN_TFT_HSYNC_NR */
	{4,  8}, /* 72 - TZ1090_PIN_TFT_VD12ACB */
	{4, 10}, /* 73 - TZ1090_PIN_TFT_PWRSAVE */
	{4, 24}, /* 74 - TZ1090_PIN_TX_ON */
	{4, 26}, /* 75 - TZ1090_PIN_RX_ON */
	{4, 28}, /* 76 - TZ1090_PIN_PLL_ON */
	{4, 30}, /* 77 - TZ1090_PIN_PA_ON */
	{5,  0}, /* 78 - TZ1090_PIN_RX_HP */
	{5,  6}, /* 79 - TZ1090_PIN_GAIN0 */
	{5,  8}, /* 80 - TZ1090_PIN_GAIN1 */
	{5, 10}, /* 81 - TZ1090_PIN_GAIN2 */
	{5, 12}, /* 82 - TZ1090_PIN_GAIN3 */
	{5, 14}, /* 83 - TZ1090_PIN_GAIN4 */
	{5, 16}, /* 84 - TZ1090_PIN_GAIN5 */
	{5, 18}, /* 85 - TZ1090_PIN_GAIN6 */
	{5, 20}, /* 86 - TZ1090_PIN_GAIN7 */
	{5,  2}, /* 87 - TZ1090_PIN_ANT_SEL0 */
	{5,  4}, /* 88 - TZ1090_PIN_ANT_SEL1 */
	{0,  0}, /* 89 - TZ1090_PIN_SDH_CLK_IN */

	{5, 24}, /* 90 - TZ1090_PIN_TCK */
	{5, 26}, /* 91 - TZ1090_PIN_TRST */
	{5, 28}, /* 92 - TZ1090_PIN_TDI */
	{5, 30}, /* 93 - TZ1090_PIN_TDO */
	{6,  0}, /* 94 - TZ1090_PIN_TMS */
	{4, 16}, /* 95 - TZ1090_PIN_CLK_OUT0 */
	{4, 22}, /* 96 - TZ1090_PIN_CLK_OUT1 */
};

static int tz1090_pinconf_reg(struct pinctrl_dev *pctldev,
			      unsigned int pin,
			      enum pin_config_param param,
			      bool report_err,
			      u32 *reg, u32 *width, u32 *mask, u32 *shift,
			      u32 *val)
{
	struct tz1090_pinconf_pullup *pu;

	/* All supported pins have controllable input bias */
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		*val = REG_PU_PD_TRISTATE;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		*val = REG_PU_PD_UP;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		*val = REG_PU_PD_DOWN;
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
		*val = REG_PU_PD_REPEATER;
		break;
	default:
		return -ENOTSUPP;
	};

	/* Only input bias parameters supported */
	pu = &tz1090_pinconf_pullup[pin];
	*reg = REG_PINCTRL_PU_PD + 4*pu->index;
	*shift = pu->shift;
	*width = 2;

	/* Calculate field information */
	*mask = (BIT(*width) - 1) << *shift;

	return 0;
}

static int tz1090_pinconf_get(struct pinctrl_dev *pctldev,
			      unsigned int pin, unsigned long *config)
{
	struct tz1090_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	int ret;
	u32 reg, width, mask, shift, val, tmp, arg;

	/* Get register information */
	ret = tz1090_pinconf_reg(pctldev, pin, param, true,
				 &reg, &width, &mask, &shift, &val);
	if (ret < 0)
		return ret;

	/* Extract field from register */
	tmp = pmx_read(pmx, reg);
	arg = ((tmp & mask) >> shift) == val;

	/* Config not active */
	if (!arg)
		return -EINVAL;

	/* And pack config */
	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int tz1090_pinconf_set(struct pinctrl_dev *pctldev,
			      unsigned int pin, unsigned long *configs,
			      unsigned num_configs)
{
	struct tz1090_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	unsigned int arg;
	int ret;
	u32 reg, width, mask, shift, val, tmp;
	unsigned long flags;
	int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		dev_dbg(pctldev->dev, "%s(pin=%s, config=%#lx)\n",
			__func__, tz1090_pins[pin].name, configs[i]);

		/* Get register information */
		ret = tz1090_pinconf_reg(pctldev, pin, param, true,
					 &reg, &width, &mask, &shift, &val);
		if (ret < 0)
			return ret;

		/* Unpack argument and range check it */
		if (arg > 1) {
			dev_dbg(pctldev->dev, "%s: arg %u out of range\n",
				__func__, arg);
			return -EINVAL;
		}

		/* Write register field */
		__global_lock2(flags);
		tmp = pmx_read(pmx, reg);
		tmp &= ~mask;
		if (arg)
			tmp |= val << shift;
		pmx_write(pmx, tmp, reg);
		__global_unlock2(flags);
	} /* for each config */

	return 0;
}

static const int tz1090_boolean_map[] = {
	[0]		= -EINVAL,
	[1]		= 1,
};

static const int tz1090_dr_map[] = {
	[REG_DR_2mA]	= 2,
	[REG_DR_4mA]	= 4,
	[REG_DR_8mA]	= 8,
	[REG_DR_12mA]	= 12,
};

static int tz1090_pinconf_group_reg(struct pinctrl_dev *pctldev,
				    const struct tz1090_pingroup *g,
				    enum pin_config_param param,
				    bool report_err,
				    u32 *reg, u32 *width, u32 *mask, u32 *shift,
				    const int **map)
{
	/* Drive configuration applies in groups, but not to all groups. */
	if (!g->drv) {
		if (report_err)
			dev_dbg(pctldev->dev,
				"%s: group %s has no drive control\n",
				__func__, g->name);
		return -ENOTSUPP;
	}

	/* Find information about drive parameter's register */
	switch (param) {
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		*reg = REG_PINCTRL_SCHMITT;
		*width = 1;
		*map = tz1090_boolean_map;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		*reg = REG_PINCTRL_DR;
		*width = 2;
		*map = tz1090_dr_map;
		break;
	default:
		return -ENOTSUPP;
	};

	/* Calculate field information */
	*shift = g->slw_bit * *width;
	*mask = (BIT(*width) - 1) << *shift;

	return 0;
}

static int tz1090_pinconf_group_get(struct pinctrl_dev *pctldev,
				    unsigned int group,
				    unsigned long *config)
{
	struct tz1090_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tz1090_pingroup *g;
	enum pin_config_param param = pinconf_to_config_param(*config);
	int ret, arg;
	unsigned int pin;
	u32 reg, width, mask, shift, val;
	const int *map;

	if (group >= ARRAY_SIZE(tz1090_groups)) {
		pin = group - ARRAY_SIZE(tz1090_groups);
		return tz1090_pinconf_get(pctldev, pin, config);
	}

	g = &tz1090_groups[group];
	if (g->npins == 1) {
		pin = g->pins[0];
		ret = tz1090_pinconf_get(pctldev, pin, config);
		if (ret != -ENOTSUPP)
			return ret;
	}

	/* Get register information */
	ret = tz1090_pinconf_group_reg(pctldev, g, param, true,
				       &reg, &width, &mask, &shift, &map);
	if (ret < 0)
		return ret;

	/* Extract field from register */
	val = pmx_read(pmx, reg);
	arg = map[(val & mask) >> shift];
	if (arg < 0)
		return arg;

	/* And pack config */
	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int tz1090_pinconf_group_set(struct pinctrl_dev *pctldev,
				    unsigned int group, unsigned long *configs,
				    unsigned num_configs)
{
	struct tz1090_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tz1090_pingroup *g;
	enum pin_config_param param;
	unsigned int arg, pin, i;
	const unsigned int *pit;
	int ret;
	u32 reg, width, mask, shift, val;
	unsigned long flags;
	const int *map;
	int j;

	if (group >= ARRAY_SIZE(tz1090_groups)) {
		pin = group - ARRAY_SIZE(tz1090_groups);
		return tz1090_pinconf_set(pctldev, pin, configs, num_configs);
	}

	g = &tz1090_groups[group];
	if (g->npins == 1) {
		pin = g->pins[0];
		ret = tz1090_pinconf_set(pctldev, pin, configs, num_configs);
		if (ret != -ENOTSUPP)
			return ret;
	}

	for (j = 0; j < num_configs; j++) {
		param = pinconf_to_config_param(configs[j]);

		dev_dbg(pctldev->dev, "%s(group=%s, config=%#lx)\n",
			__func__, g->name, configs[j]);

		/* Get register information */
		ret = tz1090_pinconf_group_reg(pctldev, g, param, true, &reg,
						&width, &mask, &shift, &map);
		if (ret < 0) {
			/*
			 * Maybe we're trying to set a per-pin configuration
			 * of a group, so do the pins one by one. This is
			 * mainly as a convenience.
			 */
			for (i = 0, pit = g->pins; i < g->npins; ++i, ++pit) {
				ret = tz1090_pinconf_set(pctldev, *pit, configs,
					num_configs);
				if (ret)
					return ret;
			}
			return 0;
		}

		/* Unpack argument and map it to register value */
		arg = pinconf_to_config_argument(configs[j]);
		for (i = 0; i < BIT(width); ++i) {
			if (map[i] == arg || (map[i] == -EINVAL && !arg)) {
				/* Write register field */
				__global_lock2(flags);
				val = pmx_read(pmx, reg);
				val &= ~mask;
				val |= i << shift;
				pmx_write(pmx, val, reg);
				__global_unlock2(flags);
				goto next_config;
			}
		}

		dev_dbg(pctldev->dev, "%s: arg %u not supported\n",
			__func__, arg);
		return -EINVAL;

next_config:
		;
	} /* for each config */

	return 0;
}

static struct pinconf_ops tz1090_pinconf_ops = {
	.is_generic			= true,
	.pin_config_get			= tz1090_pinconf_get,
	.pin_config_set			= tz1090_pinconf_set,
	.pin_config_group_get		= tz1090_pinconf_group_get,
	.pin_config_group_set		= tz1090_pinconf_group_set,
	.pin_config_config_dbg_show	= pinconf_generic_dump_config,
};

/*
 * Pin control driver setup
 */

static struct pinctrl_desc tz1090_pinctrl_desc = {
	.pctlops	= &tz1090_pinctrl_ops,
	.pmxops		= &tz1090_pinmux_ops,
	.confops	= &tz1090_pinconf_ops,
	.owner		= THIS_MODULE,
};

static int tz1090_pinctrl_probe(struct platform_device *pdev)
{
	struct tz1090_pmx *pmx;
	struct resource *res;

	pmx = devm_kzalloc(&pdev->dev, sizeof(*pmx), GFP_KERNEL);
	if (!pmx) {
		dev_err(&pdev->dev, "Can't alloc tz1090_pmx\n");
		return -ENOMEM;
	}
	pmx->dev = &pdev->dev;
	spin_lock_init(&pmx->lock);

	tz1090_pinctrl_desc.name = dev_name(&pdev->dev);
	tz1090_pinctrl_desc.pins = tz1090_pins;
	tz1090_pinctrl_desc.npins = ARRAY_SIZE(tz1090_pins);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pmx->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmx->regs))
		return PTR_ERR(pmx->regs);

	pmx->pctl = pinctrl_register(&tz1090_pinctrl_desc, &pdev->dev, pmx);
	if (!pmx->pctl) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, pmx);

	dev_info(&pdev->dev, "TZ1090 pinctrl driver initialised\n");

	return 0;
}

static int tz1090_pinctrl_remove(struct platform_device *pdev)
{
	struct tz1090_pmx *pmx = platform_get_drvdata(pdev);

	pinctrl_unregister(pmx->pctl);

	return 0;
}

static struct of_device_id tz1090_pinctrl_of_match[] = {
	{ .compatible = "img,tz1090-pinctrl", },
	{ },
};

static struct platform_driver tz1090_pinctrl_driver = {
	.driver = {
		.name		= "tz1090-pinctrl",
		.owner		= THIS_MODULE,
		.of_match_table	= tz1090_pinctrl_of_match,
	},
	.probe	= tz1090_pinctrl_probe,
	.remove	= tz1090_pinctrl_remove,
};

static int __init tz1090_pinctrl_init(void)
{
	tz1090_init_mux_pins();
	return platform_driver_register(&tz1090_pinctrl_driver);
}
arch_initcall(tz1090_pinctrl_init);

static void __exit tz1090_pinctrl_exit(void)
{
	platform_driver_unregister(&tz1090_pinctrl_driver);
}
module_exit(tz1090_pinctrl_exit);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("Toumaz Xenif TZ1090 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, tz1090_pinctrl_of_match);
