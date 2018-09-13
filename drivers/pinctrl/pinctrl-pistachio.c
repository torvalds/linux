/*
 * Pistachio SoC pinctrl driver
 *
 * Copyright (C) 2014 Imagination Technologies Ltd.
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "pinctrl-utils.h"

#define PADS_SCHMITT_EN0		0x000
#define PADS_SCHMITT_EN_REG(pin)	(PADS_SCHMITT_EN0 + 0x4 * ((pin) / 32))
#define PADS_SCHMITT_EN_BIT(pin)	BIT((pin) % 32)

#define PADS_PU_PD0			0x040
#define PADS_PU_PD_REG(pin)		(PADS_PU_PD0 + 0x4 * ((pin) / 16))
#define PADS_PU_PD_SHIFT(pin)		(2 * ((pin) % 16))
#define PADS_PU_PD_MASK			0x3
#define PADS_PU_PD_HIGHZ		0x0
#define PADS_PU_PD_UP			0x1
#define PADS_PU_PD_DOWN			0x2
#define PADS_PU_PD_BUS			0x3

#define PADS_FUNCTION_SELECT0		0x0c0
#define PADS_FUNCTION_SELECT1		0x0c4
#define PADS_FUNCTION_SELECT2		0x0c8
#define PADS_SCENARIO_SELECT		0x0f8

#define PADS_SLEW_RATE0			0x100
#define PADS_SLEW_RATE_REG(pin)		(PADS_SLEW_RATE0 + 0x4 * ((pin) / 32))
#define PADS_SLEW_RATE_BIT(pin)		BIT((pin) % 32)

#define PADS_DRIVE_STRENGTH0		0x120
#define PADS_DRIVE_STRENGTH_REG(pin)					\
	(PADS_DRIVE_STRENGTH0 + 0x4 * ((pin) / 16))
#define PADS_DRIVE_STRENGTH_SHIFT(pin)	(2 * ((pin) % 16))
#define PADS_DRIVE_STRENGTH_MASK	0x3
#define PADS_DRIVE_STRENGTH_2MA		0x0
#define PADS_DRIVE_STRENGTH_4MA		0x1
#define PADS_DRIVE_STRENGTH_8MA		0x2
#define PADS_DRIVE_STRENGTH_12MA	0x3

#define GPIO_BANK_BASE(bank)		(0x200 + 0x24 * (bank))

#define GPIO_BIT_EN			0x00
#define GPIO_OUTPUT_EN			0x04
#define GPIO_OUTPUT			0x08
#define GPIO_INPUT			0x0c
#define GPIO_INPUT_POLARITY		0x10
#define GPIO_INTERRUPT_TYPE		0x14
#define GPIO_INTERRUPT_TYPE_LEVEL	0x0
#define GPIO_INTERRUPT_TYPE_EDGE	0x1
#define GPIO_INTERRUPT_EDGE		0x18
#define GPIO_INTERRUPT_EDGE_SINGLE	0x0
#define GPIO_INTERRUPT_EDGE_DUAL	0x1
#define GPIO_INTERRUPT_EN		0x1c
#define GPIO_INTERRUPT_STATUS		0x20

struct pistachio_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
	const int *scenarios;
	unsigned int nscenarios;
	unsigned int scenario_reg;
	unsigned int scenario_shift;
	unsigned int scenario_mask;
};

struct pistachio_pin_group {
	const char *name;
	unsigned int pin;
	int mux_option[3];
	int mux_reg;
	int mux_shift;
	int mux_mask;
};

struct pistachio_gpio_bank {
	struct pistachio_pinctrl *pctl;
	void __iomem *base;
	unsigned int pin_base;
	unsigned int npins;
	struct gpio_chip gpio_chip;
	struct irq_chip irq_chip;
};

struct pistachio_pinctrl {
	struct device *dev;
	void __iomem *base;
	struct pinctrl_dev *pctldev;
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct pistachio_function *functions;
	unsigned int nfunctions;
	const struct pistachio_pin_group *groups;
	unsigned int ngroups;
	struct pistachio_gpio_bank *gpio_banks;
	unsigned int nbanks;
};

#define PISTACHIO_PIN_MFIO(p)		(p)
#define PISTACHIO_PIN_TCK		90
#define PISTACHIO_PIN_TRSTN		91
#define PISTACHIO_PIN_TDI		92
#define PISTACHIO_PIN_TMS		93
#define PISTACHIO_PIN_TDO		94
#define PISTACHIO_PIN_JTAG_COMPLY	95
#define PISTACHIO_PIN_SAFE_MODE		96
#define PISTACHIO_PIN_POR_DISABLE	97
#define PISTACHIO_PIN_RESETN		98

#define MFIO_PIN_DESC(p)	PINCTRL_PIN(PISTACHIO_PIN_MFIO(p), "mfio" #p)

static const struct pinctrl_pin_desc pistachio_pins[] = {
	MFIO_PIN_DESC(0),
	MFIO_PIN_DESC(1),
	MFIO_PIN_DESC(2),
	MFIO_PIN_DESC(3),
	MFIO_PIN_DESC(4),
	MFIO_PIN_DESC(5),
	MFIO_PIN_DESC(6),
	MFIO_PIN_DESC(7),
	MFIO_PIN_DESC(8),
	MFIO_PIN_DESC(9),
	MFIO_PIN_DESC(10),
	MFIO_PIN_DESC(11),
	MFIO_PIN_DESC(12),
	MFIO_PIN_DESC(13),
	MFIO_PIN_DESC(14),
	MFIO_PIN_DESC(15),
	MFIO_PIN_DESC(16),
	MFIO_PIN_DESC(17),
	MFIO_PIN_DESC(18),
	MFIO_PIN_DESC(19),
	MFIO_PIN_DESC(20),
	MFIO_PIN_DESC(21),
	MFIO_PIN_DESC(22),
	MFIO_PIN_DESC(23),
	MFIO_PIN_DESC(24),
	MFIO_PIN_DESC(25),
	MFIO_PIN_DESC(26),
	MFIO_PIN_DESC(27),
	MFIO_PIN_DESC(28),
	MFIO_PIN_DESC(29),
	MFIO_PIN_DESC(30),
	MFIO_PIN_DESC(31),
	MFIO_PIN_DESC(32),
	MFIO_PIN_DESC(33),
	MFIO_PIN_DESC(34),
	MFIO_PIN_DESC(35),
	MFIO_PIN_DESC(36),
	MFIO_PIN_DESC(37),
	MFIO_PIN_DESC(38),
	MFIO_PIN_DESC(39),
	MFIO_PIN_DESC(40),
	MFIO_PIN_DESC(41),
	MFIO_PIN_DESC(42),
	MFIO_PIN_DESC(43),
	MFIO_PIN_DESC(44),
	MFIO_PIN_DESC(45),
	MFIO_PIN_DESC(46),
	MFIO_PIN_DESC(47),
	MFIO_PIN_DESC(48),
	MFIO_PIN_DESC(49),
	MFIO_PIN_DESC(50),
	MFIO_PIN_DESC(51),
	MFIO_PIN_DESC(52),
	MFIO_PIN_DESC(53),
	MFIO_PIN_DESC(54),
	MFIO_PIN_DESC(55),
	MFIO_PIN_DESC(56),
	MFIO_PIN_DESC(57),
	MFIO_PIN_DESC(58),
	MFIO_PIN_DESC(59),
	MFIO_PIN_DESC(60),
	MFIO_PIN_DESC(61),
	MFIO_PIN_DESC(62),
	MFIO_PIN_DESC(63),
	MFIO_PIN_DESC(64),
	MFIO_PIN_DESC(65),
	MFIO_PIN_DESC(66),
	MFIO_PIN_DESC(67),
	MFIO_PIN_DESC(68),
	MFIO_PIN_DESC(69),
	MFIO_PIN_DESC(70),
	MFIO_PIN_DESC(71),
	MFIO_PIN_DESC(72),
	MFIO_PIN_DESC(73),
	MFIO_PIN_DESC(74),
	MFIO_PIN_DESC(75),
	MFIO_PIN_DESC(76),
	MFIO_PIN_DESC(77),
	MFIO_PIN_DESC(78),
	MFIO_PIN_DESC(79),
	MFIO_PIN_DESC(80),
	MFIO_PIN_DESC(81),
	MFIO_PIN_DESC(82),
	MFIO_PIN_DESC(83),
	MFIO_PIN_DESC(84),
	MFIO_PIN_DESC(85),
	MFIO_PIN_DESC(86),
	MFIO_PIN_DESC(87),
	MFIO_PIN_DESC(88),
	MFIO_PIN_DESC(89),
	PINCTRL_PIN(PISTACHIO_PIN_TCK, "tck"),
	PINCTRL_PIN(PISTACHIO_PIN_TRSTN, "trstn"),
	PINCTRL_PIN(PISTACHIO_PIN_TDI, "tdi"),
	PINCTRL_PIN(PISTACHIO_PIN_TMS, "tms"),
	PINCTRL_PIN(PISTACHIO_PIN_TDO, "tdo"),
	PINCTRL_PIN(PISTACHIO_PIN_JTAG_COMPLY, "jtag_comply"),
	PINCTRL_PIN(PISTACHIO_PIN_SAFE_MODE, "safe_mode"),
	PINCTRL_PIN(PISTACHIO_PIN_POR_DISABLE, "por_disable"),
	PINCTRL_PIN(PISTACHIO_PIN_RESETN, "resetn"),
};

static const char * const pistachio_spim0_groups[] = {
	"mfio1", "mfio2", "mfio8", "mfio9", "mfio10", "mfio28", "mfio29",
	"mfio30", "mfio55", "mfio56", "mfio57",
};

static const char * const pistachio_spim1_groups[] = {
	"mfio0", "mfio1", "mfio2", "mfio3", "mfio4", "mfio5", "mfio6",
	"mfio7", "mfio31", "mfio55", "mfio56", "mfio57", "mfio58",
};

static const char * const pistachio_spis_groups[] = {
	"mfio11", "mfio12", "mfio13", "mfio14",
};

static const char *const pistachio_sdhost_groups[] = {
	"mfio15", "mfio16", "mfio17", "mfio18", "mfio19", "mfio20",
	"mfio21", "mfio22", "mfio23", "mfio24", "mfio25", "mfio26",
	"mfio27",
};

static const char * const pistachio_i2c0_groups[] = {
	"mfio28", "mfio29",
};

static const char * const pistachio_i2c1_groups[] = {
	"mfio30", "mfio31",
};

static const char * const pistachio_i2c2_groups[] = {
	"mfio32", "mfio33",
};

static const char * const pistachio_i2c3_groups[] = {
	"mfio34", "mfio35",
};

static const char * const pistachio_audio_clk_in_groups[] = {
	"mfio36",
};

static const char * const pistachio_i2s_out_groups[] = {
	"mfio36", "mfio37", "mfio38", "mfio39", "mfio40", "mfio41",
	"mfio42", "mfio43", "mfio44",
};

static const char * const pistachio_debug_raw_cca_ind_groups[] = {
	"mfio37",
};

static const char * const pistachio_debug_ed_sec20_cca_ind_groups[] = {
	"mfio38",
};

static const char * const pistachio_debug_ed_sec40_cca_ind_groups[] = {
	"mfio39",
};

static const char * const pistachio_debug_agc_done_0_groups[] = {
	"mfio40",
};

static const char * const pistachio_debug_agc_done_1_groups[] = {
	"mfio41",
};

static const char * const pistachio_debug_ed_cca_ind_groups[] = {
	"mfio42",
};

static const char * const pistachio_debug_s2l_done_groups[] = {
	"mfio43",
};

static const char * const pistachio_i2s_dac_clk_groups[] = {
	"mfio45",
};

static const char * const pistachio_audio_sync_groups[] = {
	"mfio45",
};

static const char * const pistachio_audio_trigger_groups[] = {
	"mfio46",
};

static const char * const pistachio_i2s_in_groups[] = {
	"mfio47", "mfio48", "mfio49", "mfio50", "mfio51", "mfio52",
	"mfio53", "mfio54",
};

static const char * const pistachio_uart0_groups[] = {
	"mfio55", "mfio56", "mfio57", "mfio58",
};

static const char * const pistachio_uart1_groups[] = {
	"mfio59", "mfio60", "mfio1", "mfio2",
};

static const char * const pistachio_spdif_out_groups[] = {
	"mfio61",
};

static const char * const pistachio_spdif_in_groups[] = {
	"mfio62", "mfio54",
};
static const int pistachio_spdif_in_scenarios[] = {
	PISTACHIO_PIN_MFIO(62),
	PISTACHIO_PIN_MFIO(54),
};

static const char * const pistachio_eth_groups[] = {
	"mfio63", "mfio64", "mfio65", "mfio66", "mfio67", "mfio68",
	"mfio69", "mfio70", "mfio71",
};

static const char * const pistachio_ir_groups[] = {
	"mfio72",
};

static const char * const pistachio_pwmpdm_groups[] = {
	"mfio73", "mfio74", "mfio75", "mfio76",
};

static const char * const pistachio_mips_trace_clk_groups[] = {
	"mfio15", "mfio63", "mfio73",
};

static const char * const pistachio_mips_trace_dint_groups[] = {
	"mfio16", "mfio64", "mfio74",
};
static const int pistachio_mips_trace_dint_scenarios[] = {
	PISTACHIO_PIN_MFIO(16),
	PISTACHIO_PIN_MFIO(64),
	PISTACHIO_PIN_MFIO(74),
};

static const char * const pistachio_mips_trace_trigout_groups[] = {
	"mfio17", "mfio65", "mfio75",
};

static const char * const pistachio_mips_trace_trigin_groups[] = {
	"mfio18", "mfio66", "mfio76",
};
static const int pistachio_mips_trace_trigin_scenarios[] = {
	PISTACHIO_PIN_MFIO(18),
	PISTACHIO_PIN_MFIO(66),
	PISTACHIO_PIN_MFIO(76),
};

static const char * const pistachio_mips_trace_dm_groups[] = {
	"mfio19", "mfio67", "mfio77",
};

static const char * const pistachio_mips_probe_n_groups[] = {
	"mfio20", "mfio68", "mfio78",
};
static const int pistachio_mips_probe_n_scenarios[] = {
	PISTACHIO_PIN_MFIO(20),
	PISTACHIO_PIN_MFIO(68),
	PISTACHIO_PIN_MFIO(78),
};

static const char * const pistachio_mips_trace_data_groups[] = {
	"mfio15", "mfio16", "mfio17", "mfio18", "mfio19", "mfio20",
	"mfio21", "mfio22", "mfio63", "mfio64", "mfio65", "mfio66",
	"mfio67", "mfio68", "mfio69", "mfio70", "mfio79", "mfio80",
	"mfio81", "mfio82", "mfio83", "mfio84", "mfio85", "mfio86",
};

static const char * const pistachio_sram_debug_groups[] = {
	"mfio73", "mfio74",
};

static const char * const pistachio_rom_debug_groups[] = {
	"mfio75", "mfio76",
};

static const char * const pistachio_rpu_debug_groups[] = {
	"mfio77", "mfio78",
};

static const char * const pistachio_mips_debug_groups[] = {
	"mfio79", "mfio80",
};

static const char * const pistachio_eth_debug_groups[] = {
	"mfio81", "mfio82",
};

static const char * const pistachio_usb_debug_groups[] = {
	"mfio83", "mfio84",
};

static const char * const pistachio_sdhost_debug_groups[] = {
	"mfio85", "mfio86",
};

static const char * const pistachio_socif_debug_groups[] = {
	"mfio87", "mfio88",
};

static const char * const pistachio_mdc_debug_groups[] = {
	"mfio77", "mfio78",
};

static const char * const pistachio_ddr_debug_groups[] = {
	"mfio79", "mfio80",
};

static const char * const pistachio_dreq0_groups[] = {
	"mfio81",
};

static const char * const pistachio_dreq1_groups[] = {
	"mfio82",
};

static const char * const pistachio_dreq2_groups[] = {
	"mfio87",
};

static const char * const pistachio_dreq3_groups[] = {
	"mfio88",
};

static const char * const pistachio_dreq4_groups[] = {
	"mfio89",
};

static const char * const pistachio_dreq5_groups[] = {
	"mfio89",
};

static const char * const pistachio_mips_pll_lock_groups[] = {
	"mfio83",
};

static const char * const pistachio_audio_pll_lock_groups[] = {
	"mfio84",
};

static const char * const pistachio_rpu_v_pll_lock_groups[] = {
	"mfio85",
};

static const char * const pistachio_rpu_l_pll_lock_groups[] = {
	"mfio86",
};

static const char * const pistachio_sys_pll_lock_groups[] = {
	"mfio87",
};

static const char * const pistachio_wifi_pll_lock_groups[] = {
	"mfio88",
};

static const char * const pistachio_bt_pll_lock_groups[] = {
	"mfio89",
};

#define FUNCTION(_name)							\
	{								\
		.name = #_name,						\
		.groups = pistachio_##_name##_groups,			\
		.ngroups = ARRAY_SIZE(pistachio_##_name##_groups),	\
	}

#define FUNCTION_SCENARIO(_name, _reg, _shift, _mask)			\
	{								\
		.name = #_name,						\
		.groups = pistachio_##_name##_groups,			\
		.ngroups = ARRAY_SIZE(pistachio_##_name##_groups),	\
		.scenarios = pistachio_##_name##_scenarios,		\
		.nscenarios = ARRAY_SIZE(pistachio_##_name##_scenarios),\
		.scenario_reg = _reg,					\
		.scenario_shift = _shift,				\
		.scenario_mask = _mask,					\
	}

enum pistachio_mux_option {
	PISTACHIO_FUNCTION_NONE = -1,
	PISTACHIO_FUNCTION_SPIM0,
	PISTACHIO_FUNCTION_SPIM1,
	PISTACHIO_FUNCTION_SPIS,
	PISTACHIO_FUNCTION_SDHOST,
	PISTACHIO_FUNCTION_I2C0,
	PISTACHIO_FUNCTION_I2C1,
	PISTACHIO_FUNCTION_I2C2,
	PISTACHIO_FUNCTION_I2C3,
	PISTACHIO_FUNCTION_AUDIO_CLK_IN,
	PISTACHIO_FUNCTION_I2S_OUT,
	PISTACHIO_FUNCTION_I2S_DAC_CLK,
	PISTACHIO_FUNCTION_AUDIO_SYNC,
	PISTACHIO_FUNCTION_AUDIO_TRIGGER,
	PISTACHIO_FUNCTION_I2S_IN,
	PISTACHIO_FUNCTION_UART0,
	PISTACHIO_FUNCTION_UART1,
	PISTACHIO_FUNCTION_SPDIF_OUT,
	PISTACHIO_FUNCTION_SPDIF_IN,
	PISTACHIO_FUNCTION_ETH,
	PISTACHIO_FUNCTION_IR,
	PISTACHIO_FUNCTION_PWMPDM,
	PISTACHIO_FUNCTION_MIPS_TRACE_CLK,
	PISTACHIO_FUNCTION_MIPS_TRACE_DINT,
	PISTACHIO_FUNCTION_MIPS_TRACE_TRIGOUT,
	PISTACHIO_FUNCTION_MIPS_TRACE_TRIGIN,
	PISTACHIO_FUNCTION_MIPS_TRACE_DM,
	PISTACHIO_FUNCTION_MIPS_TRACE_PROBE_N,
	PISTACHIO_FUNCTION_MIPS_TRACE_DATA,
	PISTACHIO_FUNCTION_SRAM_DEBUG,
	PISTACHIO_FUNCTION_ROM_DEBUG,
	PISTACHIO_FUNCTION_RPU_DEBUG,
	PISTACHIO_FUNCTION_MIPS_DEBUG,
	PISTACHIO_FUNCTION_ETH_DEBUG,
	PISTACHIO_FUNCTION_USB_DEBUG,
	PISTACHIO_FUNCTION_SDHOST_DEBUG,
	PISTACHIO_FUNCTION_SOCIF_DEBUG,
	PISTACHIO_FUNCTION_MDC_DEBUG,
	PISTACHIO_FUNCTION_DDR_DEBUG,
	PISTACHIO_FUNCTION_DREQ0,
	PISTACHIO_FUNCTION_DREQ1,
	PISTACHIO_FUNCTION_DREQ2,
	PISTACHIO_FUNCTION_DREQ3,
	PISTACHIO_FUNCTION_DREQ4,
	PISTACHIO_FUNCTION_DREQ5,
	PISTACHIO_FUNCTION_MIPS_PLL_LOCK,
	PISTACHIO_FUNCTION_AUDIO_PLL_LOCK,
	PISTACHIO_FUNCTION_RPU_V_PLL_LOCK,
	PISTACHIO_FUNCTION_RPU_L_PLL_LOCK,
	PISTACHIO_FUNCTION_SYS_PLL_LOCK,
	PISTACHIO_FUNCTION_WIFI_PLL_LOCK,
	PISTACHIO_FUNCTION_BT_PLL_LOCK,
	PISTACHIO_FUNCTION_DEBUG_RAW_CCA_IND,
	PISTACHIO_FUNCTION_DEBUG_ED_SEC20_CCA_IND,
	PISTACHIO_FUNCTION_DEBUG_ED_SEC40_CCA_IND,
	PISTACHIO_FUNCTION_DEBUG_AGC_DONE_0,
	PISTACHIO_FUNCTION_DEBUG_AGC_DONE_1,
	PISTACHIO_FUNCTION_DEBUG_ED_CCA_IND,
	PISTACHIO_FUNCTION_DEBUG_S2L_DONE,
};

static const struct pistachio_function pistachio_functions[] = {
	FUNCTION(spim0),
	FUNCTION(spim1),
	FUNCTION(spis),
	FUNCTION(sdhost),
	FUNCTION(i2c0),
	FUNCTION(i2c1),
	FUNCTION(i2c2),
	FUNCTION(i2c3),
	FUNCTION(audio_clk_in),
	FUNCTION(i2s_out),
	FUNCTION(i2s_dac_clk),
	FUNCTION(audio_sync),
	FUNCTION(audio_trigger),
	FUNCTION(i2s_in),
	FUNCTION(uart0),
	FUNCTION(uart1),
	FUNCTION(spdif_out),
	FUNCTION_SCENARIO(spdif_in, PADS_SCENARIO_SELECT, 0, 0x1),
	FUNCTION(eth),
	FUNCTION(ir),
	FUNCTION(pwmpdm),
	FUNCTION(mips_trace_clk),
	FUNCTION_SCENARIO(mips_trace_dint, PADS_SCENARIO_SELECT, 1, 0x3),
	FUNCTION(mips_trace_trigout),
	FUNCTION_SCENARIO(mips_trace_trigin, PADS_SCENARIO_SELECT, 3, 0x3),
	FUNCTION(mips_trace_dm),
	FUNCTION_SCENARIO(mips_probe_n, PADS_SCENARIO_SELECT, 5, 0x3),
	FUNCTION(mips_trace_data),
	FUNCTION(sram_debug),
	FUNCTION(rom_debug),
	FUNCTION(rpu_debug),
	FUNCTION(mips_debug),
	FUNCTION(eth_debug),
	FUNCTION(usb_debug),
	FUNCTION(sdhost_debug),
	FUNCTION(socif_debug),
	FUNCTION(mdc_debug),
	FUNCTION(ddr_debug),
	FUNCTION(dreq0),
	FUNCTION(dreq1),
	FUNCTION(dreq2),
	FUNCTION(dreq3),
	FUNCTION(dreq4),
	FUNCTION(dreq5),
	FUNCTION(mips_pll_lock),
	FUNCTION(audio_pll_lock),
	FUNCTION(rpu_v_pll_lock),
	FUNCTION(rpu_l_pll_lock),
	FUNCTION(sys_pll_lock),
	FUNCTION(wifi_pll_lock),
	FUNCTION(bt_pll_lock),
	FUNCTION(debug_raw_cca_ind),
	FUNCTION(debug_ed_sec20_cca_ind),
	FUNCTION(debug_ed_sec40_cca_ind),
	FUNCTION(debug_agc_done_0),
	FUNCTION(debug_agc_done_1),
	FUNCTION(debug_ed_cca_ind),
	FUNCTION(debug_s2l_done),
};

#define PIN_GROUP(_pin, _name)					\
	{							\
		.name = #_name,					\
		.pin = PISTACHIO_PIN_##_pin,			\
		.mux_option = {					\
			PISTACHIO_FUNCTION_NONE,		\
			PISTACHIO_FUNCTION_NONE,		\
			PISTACHIO_FUNCTION_NONE,		\
		},						\
		.mux_reg = -1,					\
		.mux_shift = -1,				\
		.mux_mask = -1,					\
	}

#define MFIO_PIN_GROUP(_pin, _func)				\
	{							\
		.name = "mfio" #_pin,				\
		.pin = PISTACHIO_PIN_MFIO(_pin),		\
		.mux_option = {					\
			PISTACHIO_FUNCTION_##_func,		\
			PISTACHIO_FUNCTION_NONE,		\
			PISTACHIO_FUNCTION_NONE,		\
		},						\
		.mux_reg = -1,					\
		.mux_shift = -1,				\
		.mux_mask = -1,					\
	}

#define MFIO_MUX_PIN_GROUP(_pin, _f0, _f1, _f2, _reg, _shift, _mask)	\
	{								\
		.name = "mfio" #_pin,					\
		.pin = PISTACHIO_PIN_MFIO(_pin),			\
		.mux_option = {						\
			PISTACHIO_FUNCTION_##_f0,			\
			PISTACHIO_FUNCTION_##_f1,			\
			PISTACHIO_FUNCTION_##_f2,			\
		},							\
		.mux_reg = _reg,					\
		.mux_shift = _shift,					\
		.mux_mask = _mask,					\
	}

static const struct pistachio_pin_group pistachio_groups[] = {
	MFIO_PIN_GROUP(0, SPIM1),
	MFIO_MUX_PIN_GROUP(1, SPIM1, SPIM0, UART1,
			   PADS_FUNCTION_SELECT0, 0, 0x3),
	MFIO_MUX_PIN_GROUP(2, SPIM1, SPIM0, UART1,
			   PADS_FUNCTION_SELECT0, 2, 0x3),
	MFIO_PIN_GROUP(3, SPIM1),
	MFIO_PIN_GROUP(4, SPIM1),
	MFIO_PIN_GROUP(5, SPIM1),
	MFIO_PIN_GROUP(6, SPIM1),
	MFIO_PIN_GROUP(7, SPIM1),
	MFIO_PIN_GROUP(8, SPIM0),
	MFIO_PIN_GROUP(9, SPIM0),
	MFIO_PIN_GROUP(10, SPIM0),
	MFIO_PIN_GROUP(11, SPIS),
	MFIO_PIN_GROUP(12, SPIS),
	MFIO_PIN_GROUP(13, SPIS),
	MFIO_PIN_GROUP(14, SPIS),
	MFIO_MUX_PIN_GROUP(15, SDHOST, MIPS_TRACE_CLK, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT0, 4, 0x3),
	MFIO_MUX_PIN_GROUP(16, SDHOST, MIPS_TRACE_DINT, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT0, 6, 0x3),
	MFIO_MUX_PIN_GROUP(17, SDHOST, MIPS_TRACE_TRIGOUT, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT0, 8, 0x3),
	MFIO_MUX_PIN_GROUP(18, SDHOST, MIPS_TRACE_TRIGIN, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT0, 10, 0x3),
	MFIO_MUX_PIN_GROUP(19, SDHOST, MIPS_TRACE_DM, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT0, 12, 0x3),
	MFIO_MUX_PIN_GROUP(20, SDHOST, MIPS_TRACE_PROBE_N, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT0, 14, 0x3),
	MFIO_MUX_PIN_GROUP(21, SDHOST, NONE, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT0, 16, 0x3),
	MFIO_MUX_PIN_GROUP(22, SDHOST, NONE, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT0, 18, 0x3),
	MFIO_PIN_GROUP(23, SDHOST),
	MFIO_PIN_GROUP(24, SDHOST),
	MFIO_PIN_GROUP(25, SDHOST),
	MFIO_PIN_GROUP(26, SDHOST),
	MFIO_PIN_GROUP(27, SDHOST),
	MFIO_MUX_PIN_GROUP(28, I2C0, SPIM0, NONE,
			   PADS_FUNCTION_SELECT0, 20, 0x1),
	MFIO_MUX_PIN_GROUP(29, I2C0, SPIM0, NONE,
			   PADS_FUNCTION_SELECT0, 21, 0x1),
	MFIO_MUX_PIN_GROUP(30, I2C1, SPIM0, NONE,
			   PADS_FUNCTION_SELECT0, 22, 0x1),
	MFIO_MUX_PIN_GROUP(31, I2C1, SPIM1, NONE,
			   PADS_FUNCTION_SELECT0, 23, 0x1),
	MFIO_PIN_GROUP(32, I2C2),
	MFIO_PIN_GROUP(33, I2C2),
	MFIO_PIN_GROUP(34, I2C3),
	MFIO_PIN_GROUP(35, I2C3),
	MFIO_MUX_PIN_GROUP(36, I2S_OUT, AUDIO_CLK_IN, NONE,
			   PADS_FUNCTION_SELECT0, 24, 0x1),
	MFIO_MUX_PIN_GROUP(37, I2S_OUT, DEBUG_RAW_CCA_IND, NONE,
			   PADS_FUNCTION_SELECT0, 25, 0x1),
	MFIO_MUX_PIN_GROUP(38, I2S_OUT, DEBUG_ED_SEC20_CCA_IND, NONE,
			   PADS_FUNCTION_SELECT0, 26, 0x1),
	MFIO_MUX_PIN_GROUP(39, I2S_OUT, DEBUG_ED_SEC40_CCA_IND, NONE,
			   PADS_FUNCTION_SELECT0, 27, 0x1),
	MFIO_MUX_PIN_GROUP(40, I2S_OUT, DEBUG_AGC_DONE_0, NONE,
			   PADS_FUNCTION_SELECT0, 28, 0x1),
	MFIO_MUX_PIN_GROUP(41, I2S_OUT, DEBUG_AGC_DONE_1, NONE,
			   PADS_FUNCTION_SELECT0, 29, 0x1),
	MFIO_MUX_PIN_GROUP(42, I2S_OUT, DEBUG_ED_CCA_IND, NONE,
			   PADS_FUNCTION_SELECT0, 30, 0x1),
	MFIO_MUX_PIN_GROUP(43, I2S_OUT, DEBUG_S2L_DONE, NONE,
			   PADS_FUNCTION_SELECT0, 31, 0x1),
	MFIO_PIN_GROUP(44, I2S_OUT),
	MFIO_MUX_PIN_GROUP(45, I2S_DAC_CLK, AUDIO_SYNC, NONE,
			   PADS_FUNCTION_SELECT1, 0, 0x1),
	MFIO_PIN_GROUP(46, AUDIO_TRIGGER),
	MFIO_PIN_GROUP(47, I2S_IN),
	MFIO_PIN_GROUP(48, I2S_IN),
	MFIO_PIN_GROUP(49, I2S_IN),
	MFIO_PIN_GROUP(50, I2S_IN),
	MFIO_PIN_GROUP(51, I2S_IN),
	MFIO_PIN_GROUP(52, I2S_IN),
	MFIO_PIN_GROUP(53, I2S_IN),
	MFIO_MUX_PIN_GROUP(54, I2S_IN, NONE, SPDIF_IN,
			   PADS_FUNCTION_SELECT1, 1, 0x3),
	MFIO_MUX_PIN_GROUP(55, UART0, SPIM0, SPIM1,
			   PADS_FUNCTION_SELECT1, 3, 0x3),
	MFIO_MUX_PIN_GROUP(56, UART0, SPIM0, SPIM1,
			   PADS_FUNCTION_SELECT1, 5, 0x3),
	MFIO_MUX_PIN_GROUP(57, UART0, SPIM0, SPIM1,
			   PADS_FUNCTION_SELECT1, 7, 0x3),
	MFIO_MUX_PIN_GROUP(58, UART0, SPIM1, NONE,
			   PADS_FUNCTION_SELECT1, 9, 0x1),
	MFIO_PIN_GROUP(59, UART1),
	MFIO_PIN_GROUP(60, UART1),
	MFIO_PIN_GROUP(61, SPDIF_OUT),
	MFIO_PIN_GROUP(62, SPDIF_IN),
	MFIO_MUX_PIN_GROUP(63, ETH, MIPS_TRACE_CLK, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT1, 10, 0x3),
	MFIO_MUX_PIN_GROUP(64, ETH, MIPS_TRACE_DINT, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT1, 12, 0x3),
	MFIO_MUX_PIN_GROUP(65, ETH, MIPS_TRACE_TRIGOUT, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT1, 14, 0x3),
	MFIO_MUX_PIN_GROUP(66, ETH, MIPS_TRACE_TRIGIN, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT1, 16, 0x3),
	MFIO_MUX_PIN_GROUP(67, ETH, MIPS_TRACE_DM, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT1, 18, 0x3),
	MFIO_MUX_PIN_GROUP(68, ETH, MIPS_TRACE_PROBE_N, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT1, 20, 0x3),
	MFIO_MUX_PIN_GROUP(69, ETH, NONE, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT1, 22, 0x3),
	MFIO_MUX_PIN_GROUP(70, ETH, NONE, MIPS_TRACE_DATA,
			   PADS_FUNCTION_SELECT1, 24, 0x3),
	MFIO_PIN_GROUP(71, ETH),
	MFIO_PIN_GROUP(72, IR),
	MFIO_MUX_PIN_GROUP(73, PWMPDM, MIPS_TRACE_CLK, SRAM_DEBUG,
			   PADS_FUNCTION_SELECT1, 26, 0x3),
	MFIO_MUX_PIN_GROUP(74, PWMPDM, MIPS_TRACE_DINT, SRAM_DEBUG,
			   PADS_FUNCTION_SELECT1, 28, 0x3),
	MFIO_MUX_PIN_GROUP(75, PWMPDM, MIPS_TRACE_TRIGOUT, ROM_DEBUG,
			   PADS_FUNCTION_SELECT1, 30, 0x3),
	MFIO_MUX_PIN_GROUP(76, PWMPDM, MIPS_TRACE_TRIGIN, ROM_DEBUG,
			   PADS_FUNCTION_SELECT2, 0, 0x3),
	MFIO_MUX_PIN_GROUP(77, MDC_DEBUG, MIPS_TRACE_DM, RPU_DEBUG,
			   PADS_FUNCTION_SELECT2, 2, 0x3),
	MFIO_MUX_PIN_GROUP(78, MDC_DEBUG, MIPS_TRACE_PROBE_N, RPU_DEBUG,
			   PADS_FUNCTION_SELECT2, 4, 0x3),
	MFIO_MUX_PIN_GROUP(79, DDR_DEBUG, MIPS_TRACE_DATA, MIPS_DEBUG,
			   PADS_FUNCTION_SELECT2, 6, 0x3),
	MFIO_MUX_PIN_GROUP(80, DDR_DEBUG, MIPS_TRACE_DATA, MIPS_DEBUG,
			   PADS_FUNCTION_SELECT2, 8, 0x3),
	MFIO_MUX_PIN_GROUP(81, DREQ0, MIPS_TRACE_DATA, ETH_DEBUG,
			   PADS_FUNCTION_SELECT2, 10, 0x3),
	MFIO_MUX_PIN_GROUP(82, DREQ1, MIPS_TRACE_DATA, ETH_DEBUG,
			   PADS_FUNCTION_SELECT2, 12, 0x3),
	MFIO_MUX_PIN_GROUP(83, MIPS_PLL_LOCK, MIPS_TRACE_DATA, USB_DEBUG,
			   PADS_FUNCTION_SELECT2, 14, 0x3),
	MFIO_MUX_PIN_GROUP(84, AUDIO_PLL_LOCK, MIPS_TRACE_DATA, USB_DEBUG,
			   PADS_FUNCTION_SELECT2, 16, 0x3),
	MFIO_MUX_PIN_GROUP(85, RPU_V_PLL_LOCK, MIPS_TRACE_DATA, SDHOST_DEBUG,
			   PADS_FUNCTION_SELECT2, 18, 0x3),
	MFIO_MUX_PIN_GROUP(86, RPU_L_PLL_LOCK, MIPS_TRACE_DATA, SDHOST_DEBUG,
			   PADS_FUNCTION_SELECT2, 20, 0x3),
	MFIO_MUX_PIN_GROUP(87, SYS_PLL_LOCK, DREQ2, SOCIF_DEBUG,
			   PADS_FUNCTION_SELECT2, 22, 0x3),
	MFIO_MUX_PIN_GROUP(88, WIFI_PLL_LOCK, DREQ3, SOCIF_DEBUG,
			   PADS_FUNCTION_SELECT2, 24, 0x3),
	MFIO_MUX_PIN_GROUP(89, BT_PLL_LOCK, DREQ4, DREQ5,
			   PADS_FUNCTION_SELECT2, 26, 0x3),
	PIN_GROUP(TCK, "tck"),
	PIN_GROUP(TRSTN, "trstn"),
	PIN_GROUP(TDI, "tdi"),
	PIN_GROUP(TMS, "tms"),
	PIN_GROUP(TDO, "tdo"),
	PIN_GROUP(JTAG_COMPLY, "jtag_comply"),
	PIN_GROUP(SAFE_MODE, "safe_mode"),
	PIN_GROUP(POR_DISABLE, "por_disable"),
	PIN_GROUP(RESETN, "resetn"),
};

static inline u32 pctl_readl(struct pistachio_pinctrl *pctl, u32 reg)
{
	return readl(pctl->base + reg);
}

static inline void pctl_writel(struct pistachio_pinctrl *pctl, u32 val, u32 reg)
{
	writel(val, pctl->base + reg);
}

static inline struct pistachio_gpio_bank *irqd_to_bank(struct irq_data *d)
{
	return gpiochip_get_data(irq_data_get_irq_chip_data(d));
}

static inline u32 gpio_readl(struct pistachio_gpio_bank *bank, u32 reg)
{
	return readl(bank->base + reg);
}

static inline void gpio_writel(struct pistachio_gpio_bank *bank, u32 val,
			       u32 reg)
{
	writel(val, bank->base + reg);
}

static inline void gpio_mask_writel(struct pistachio_gpio_bank *bank,
				    u32 reg, unsigned int bit, u32 val)
{
	/*
	 * For most of the GPIO registers, bit 16 + X must be set in order to
	 * write bit X.
	 */
	gpio_writel(bank, (0x10000 | val) << bit, reg);
}

static inline void gpio_enable(struct pistachio_gpio_bank *bank,
			       unsigned offset)
{
	gpio_mask_writel(bank, GPIO_BIT_EN, offset, 1);
}

static inline void gpio_disable(struct pistachio_gpio_bank *bank,
				unsigned offset)
{
	gpio_mask_writel(bank, GPIO_BIT_EN, offset, 0);
}

static int pistachio_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct pistachio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->ngroups;
}

static const char *pistachio_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						    unsigned group)
{
	struct pistachio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->groups[group].name;
}

static int pistachio_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					    unsigned group,
					    const unsigned **pins,
					    unsigned *num_pins)
{
	struct pistachio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pctl->groups[group].pin;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops pistachio_pinctrl_ops = {
	.get_groups_count = pistachio_pinctrl_get_groups_count,
	.get_group_name = pistachio_pinctrl_get_group_name,
	.get_group_pins = pistachio_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static int pistachio_pinmux_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct pistachio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->nfunctions;
}

static const char *
pistachio_pinmux_get_function_name(struct pinctrl_dev *pctldev, unsigned func)
{
	struct pistachio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->functions[func].name;
}

static int pistachio_pinmux_get_function_groups(struct pinctrl_dev *pctldev,
						unsigned func,
						const char * const **groups,
						unsigned * const num_groups)
{
	struct pistachio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctl->functions[func].groups;
	*num_groups = pctl->functions[func].ngroups;

	return 0;
}

static int pistachio_pinmux_enable(struct pinctrl_dev *pctldev,
				   unsigned func, unsigned group)
{
	struct pistachio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct pistachio_pin_group *pg = &pctl->groups[group];
	const struct pistachio_function *pf = &pctl->functions[func];
	struct pinctrl_gpio_range *range;
	unsigned int i;
	u32 val;

	if (pg->mux_reg > 0) {
		for (i = 0; i < ARRAY_SIZE(pg->mux_option); i++) {
			if (pg->mux_option[i] == func)
				break;
		}
		if (i == ARRAY_SIZE(pg->mux_option)) {
			dev_err(pctl->dev, "Cannot mux pin %u to function %u\n",
				group, func);
			return -EINVAL;
		}

		val = pctl_readl(pctl, pg->mux_reg);
		val &= ~(pg->mux_mask << pg->mux_shift);
		val |= i << pg->mux_shift;
		pctl_writel(pctl, val, pg->mux_reg);

		if (pf->scenarios) {
			for (i = 0; i < pf->nscenarios; i++) {
				if (pf->scenarios[i] == group)
					break;
			}
			if (WARN_ON(i == pf->nscenarios))
				return -EINVAL;

			val = pctl_readl(pctl, pf->scenario_reg);
			val &= ~(pf->scenario_mask << pf->scenario_shift);
			val |= i << pf->scenario_shift;
			pctl_writel(pctl, val, pf->scenario_reg);
		}
	}

	range = pinctrl_find_gpio_range_from_pin(pctl->pctldev, pg->pin);
	if (range)
		gpio_disable(gpiochip_get_data(range->gc), pg->pin - range->pin_base);

	return 0;
}

static const struct pinmux_ops pistachio_pinmux_ops = {
	.get_functions_count = pistachio_pinmux_get_functions_count,
	.get_function_name = pistachio_pinmux_get_function_name,
	.get_function_groups = pistachio_pinmux_get_function_groups,
	.set_mux = pistachio_pinmux_enable,
};

static int pistachio_pinconf_get(struct pinctrl_dev *pctldev, unsigned pin,
				 unsigned long *config)
{
	struct pistachio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 val, arg;

	switch (param) {
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		val = pctl_readl(pctl, PADS_SCHMITT_EN_REG(pin));
		arg = !!(val & PADS_SCHMITT_EN_BIT(pin));
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		val = pctl_readl(pctl, PADS_PU_PD_REG(pin)) >>
			PADS_PU_PD_SHIFT(pin);
		arg = (val & PADS_PU_PD_MASK) == PADS_PU_PD_HIGHZ;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		val = pctl_readl(pctl, PADS_PU_PD_REG(pin)) >>
			PADS_PU_PD_SHIFT(pin);
		arg = (val & PADS_PU_PD_MASK) == PADS_PU_PD_UP;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		val = pctl_readl(pctl, PADS_PU_PD_REG(pin)) >>
			PADS_PU_PD_SHIFT(pin);
		arg = (val & PADS_PU_PD_MASK) == PADS_PU_PD_DOWN;
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
		val = pctl_readl(pctl, PADS_PU_PD_REG(pin)) >>
			PADS_PU_PD_SHIFT(pin);
		arg = (val & PADS_PU_PD_MASK) == PADS_PU_PD_BUS;
		break;
	case PIN_CONFIG_SLEW_RATE:
		val = pctl_readl(pctl, PADS_SLEW_RATE_REG(pin));
		arg = !!(val & PADS_SLEW_RATE_BIT(pin));
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		val = pctl_readl(pctl, PADS_DRIVE_STRENGTH_REG(pin)) >>
			PADS_DRIVE_STRENGTH_SHIFT(pin);
		switch (val & PADS_DRIVE_STRENGTH_MASK) {
		case PADS_DRIVE_STRENGTH_2MA:
			arg = 2;
			break;
		case PADS_DRIVE_STRENGTH_4MA:
			arg = 4;
			break;
		case PADS_DRIVE_STRENGTH_8MA:
			arg = 8;
			break;
		case PADS_DRIVE_STRENGTH_12MA:
		default:
			arg = 12;
			break;
		}
		break;
	default:
		dev_dbg(pctl->dev, "Property %u not supported\n", param);
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int pistachio_pinconf_set(struct pinctrl_dev *pctldev, unsigned pin,
				 unsigned long *configs, unsigned num_configs)
{
	struct pistachio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u32 drv, val, arg;
	unsigned int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			val = pctl_readl(pctl, PADS_SCHMITT_EN_REG(pin));
			if (arg)
				val |= PADS_SCHMITT_EN_BIT(pin);
			else
				val &= ~PADS_SCHMITT_EN_BIT(pin);
			pctl_writel(pctl, val, PADS_SCHMITT_EN_REG(pin));
			break;
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			val = pctl_readl(pctl, PADS_PU_PD_REG(pin));
			val &= ~(PADS_PU_PD_MASK << PADS_PU_PD_SHIFT(pin));
			val |= PADS_PU_PD_HIGHZ << PADS_PU_PD_SHIFT(pin);
			pctl_writel(pctl, val, PADS_PU_PD_REG(pin));
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			val = pctl_readl(pctl, PADS_PU_PD_REG(pin));
			val &= ~(PADS_PU_PD_MASK << PADS_PU_PD_SHIFT(pin));
			val |= PADS_PU_PD_UP << PADS_PU_PD_SHIFT(pin);
			pctl_writel(pctl, val, PADS_PU_PD_REG(pin));
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			val = pctl_readl(pctl, PADS_PU_PD_REG(pin));
			val &= ~(PADS_PU_PD_MASK << PADS_PU_PD_SHIFT(pin));
			val |= PADS_PU_PD_DOWN << PADS_PU_PD_SHIFT(pin);
			pctl_writel(pctl, val, PADS_PU_PD_REG(pin));
			break;
		case PIN_CONFIG_BIAS_BUS_HOLD:
			val = pctl_readl(pctl, PADS_PU_PD_REG(pin));
			val &= ~(PADS_PU_PD_MASK << PADS_PU_PD_SHIFT(pin));
			val |= PADS_PU_PD_BUS << PADS_PU_PD_SHIFT(pin);
			pctl_writel(pctl, val, PADS_PU_PD_REG(pin));
			break;
		case PIN_CONFIG_SLEW_RATE:
			val = pctl_readl(pctl, PADS_SLEW_RATE_REG(pin));
			if (arg)
				val |= PADS_SLEW_RATE_BIT(pin);
			else
				val &= ~PADS_SLEW_RATE_BIT(pin);
			pctl_writel(pctl, val, PADS_SLEW_RATE_REG(pin));
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			val = pctl_readl(pctl, PADS_DRIVE_STRENGTH_REG(pin));
			val &= ~(PADS_DRIVE_STRENGTH_MASK <<
				 PADS_DRIVE_STRENGTH_SHIFT(pin));
			switch (arg) {
			case 2:
				drv = PADS_DRIVE_STRENGTH_2MA;
				break;
			case 4:
				drv = PADS_DRIVE_STRENGTH_4MA;
				break;
			case 8:
				drv = PADS_DRIVE_STRENGTH_8MA;
				break;
			case 12:
				drv = PADS_DRIVE_STRENGTH_12MA;
				break;
			default:
				dev_err(pctl->dev,
					"Drive strength %umA not supported\n",
					arg);
				return -EINVAL;
			}
			val |= drv << PADS_DRIVE_STRENGTH_SHIFT(pin);
			pctl_writel(pctl, val, PADS_DRIVE_STRENGTH_REG(pin));
			break;
		default:
			dev_err(pctl->dev, "Property %u not supported\n",
				param);
			return -ENOTSUPP;
		}
	}

	return 0;
}

static const struct pinconf_ops pistachio_pinconf_ops = {
	.pin_config_get = pistachio_pinconf_get,
	.pin_config_set = pistachio_pinconf_set,
	.is_generic = true,
};

static struct pinctrl_desc pistachio_pinctrl_desc = {
	.name = "pistachio-pinctrl",
	.pctlops = &pistachio_pinctrl_ops,
	.pmxops = &pistachio_pinmux_ops,
	.confops = &pistachio_pinconf_ops,
};

static int pistachio_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct pistachio_gpio_bank *bank = gpiochip_get_data(chip);

	return !(gpio_readl(bank, GPIO_OUTPUT_EN) & BIT(offset));
}

static int pistachio_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct pistachio_gpio_bank *bank = gpiochip_get_data(chip);
	u32 reg;

	if (gpio_readl(bank, GPIO_OUTPUT_EN) & BIT(offset))
		reg = GPIO_OUTPUT;
	else
		reg = GPIO_INPUT;

	return !!(gpio_readl(bank, reg) & BIT(offset));
}

static void pistachio_gpio_set(struct gpio_chip *chip, unsigned offset,
			       int value)
{
	struct pistachio_gpio_bank *bank = gpiochip_get_data(chip);

	gpio_mask_writel(bank, GPIO_OUTPUT, offset, !!value);
}

static int pistachio_gpio_direction_input(struct gpio_chip *chip,
					  unsigned offset)
{
	struct pistachio_gpio_bank *bank = gpiochip_get_data(chip);

	gpio_mask_writel(bank, GPIO_OUTPUT_EN, offset, 0);
	gpio_enable(bank, offset);

	return 0;
}

static int pistachio_gpio_direction_output(struct gpio_chip *chip,
					   unsigned offset, int value)
{
	struct pistachio_gpio_bank *bank = gpiochip_get_data(chip);

	pistachio_gpio_set(chip, offset, value);
	gpio_mask_writel(bank, GPIO_OUTPUT_EN, offset, 1);
	gpio_enable(bank, offset);

	return 0;
}

static void pistachio_gpio_irq_ack(struct irq_data *data)
{
	struct pistachio_gpio_bank *bank = irqd_to_bank(data);

	gpio_mask_writel(bank, GPIO_INTERRUPT_STATUS, data->hwirq, 0);
}

static void pistachio_gpio_irq_mask(struct irq_data *data)
{
	struct pistachio_gpio_bank *bank = irqd_to_bank(data);

	gpio_mask_writel(bank, GPIO_INTERRUPT_EN, data->hwirq, 0);
}

static void pistachio_gpio_irq_unmask(struct irq_data *data)
{
	struct pistachio_gpio_bank *bank = irqd_to_bank(data);

	gpio_mask_writel(bank, GPIO_INTERRUPT_EN, data->hwirq, 1);
}

static unsigned int pistachio_gpio_irq_startup(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);

	pistachio_gpio_direction_input(chip, data->hwirq);
	pistachio_gpio_irq_unmask(data);

	return 0;
}

static int pistachio_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct pistachio_gpio_bank *bank = irqd_to_bank(data);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		gpio_mask_writel(bank, GPIO_INPUT_POLARITY, data->hwirq, 1);
		gpio_mask_writel(bank, GPIO_INTERRUPT_TYPE, data->hwirq,
				 GPIO_INTERRUPT_TYPE_EDGE);
		gpio_mask_writel(bank, GPIO_INTERRUPT_EDGE, data->hwirq,
				 GPIO_INTERRUPT_EDGE_SINGLE);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		gpio_mask_writel(bank, GPIO_INPUT_POLARITY, data->hwirq, 0);
		gpio_mask_writel(bank, GPIO_INTERRUPT_TYPE, data->hwirq,
				 GPIO_INTERRUPT_TYPE_EDGE);
		gpio_mask_writel(bank, GPIO_INTERRUPT_EDGE, data->hwirq,
				 GPIO_INTERRUPT_EDGE_SINGLE);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		gpio_mask_writel(bank, GPIO_INTERRUPT_TYPE, data->hwirq,
				 GPIO_INTERRUPT_TYPE_EDGE);
		gpio_mask_writel(bank, GPIO_INTERRUPT_EDGE, data->hwirq,
				 GPIO_INTERRUPT_EDGE_DUAL);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		gpio_mask_writel(bank, GPIO_INPUT_POLARITY, data->hwirq, 1);
		gpio_mask_writel(bank, GPIO_INTERRUPT_TYPE, data->hwirq,
				 GPIO_INTERRUPT_TYPE_LEVEL);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		gpio_mask_writel(bank, GPIO_INPUT_POLARITY, data->hwirq, 0);
		gpio_mask_writel(bank, GPIO_INTERRUPT_TYPE, data->hwirq,
				 GPIO_INTERRUPT_TYPE_LEVEL);
		break;
	default:
		return -EINVAL;
	}

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(data, handle_level_irq);
	else
		irq_set_handler_locked(data, handle_edge_irq);

	return 0;
}

static void pistachio_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct pistachio_gpio_bank *bank = gpiochip_get_data(gc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long pending;
	unsigned int pin;

	chained_irq_enter(chip, desc);
	pending = gpio_readl(bank, GPIO_INTERRUPT_STATUS) &
		gpio_readl(bank, GPIO_INTERRUPT_EN);
	for_each_set_bit(pin, &pending, 16)
		generic_handle_irq(irq_linear_revmap(gc->irq.domain, pin));
	chained_irq_exit(chip, desc);
}

#define GPIO_BANK(_bank, _pin_base, _npins)				\
	{								\
		.pin_base = _pin_base,					\
		.npins = _npins,					\
		.gpio_chip = {						\
			.label = "GPIO" #_bank,				\
			.request = gpiochip_generic_request,		\
			.free = gpiochip_generic_free,			\
			.get_direction = pistachio_gpio_get_direction,	\
			.direction_input = pistachio_gpio_direction_input, \
			.direction_output = pistachio_gpio_direction_output, \
			.get = pistachio_gpio_get,			\
			.set = pistachio_gpio_set,			\
			.base = _pin_base,				\
			.ngpio = _npins,				\
		},							\
		.irq_chip = {						\
			.name = "GPIO" #_bank,				\
			.irq_startup = pistachio_gpio_irq_startup,	\
			.irq_ack = pistachio_gpio_irq_ack,		\
			.irq_mask = pistachio_gpio_irq_mask,		\
			.irq_unmask = pistachio_gpio_irq_unmask,	\
			.irq_set_type = pistachio_gpio_irq_set_type,	\
		},							\
	}

static struct pistachio_gpio_bank pistachio_gpio_banks[] = {
	GPIO_BANK(0, PISTACHIO_PIN_MFIO(0), 16),
	GPIO_BANK(1, PISTACHIO_PIN_MFIO(16), 16),
	GPIO_BANK(2, PISTACHIO_PIN_MFIO(32), 16),
	GPIO_BANK(3, PISTACHIO_PIN_MFIO(48), 16),
	GPIO_BANK(4, PISTACHIO_PIN_MFIO(64), 16),
	GPIO_BANK(5, PISTACHIO_PIN_MFIO(80), 10),
};

static int pistachio_gpio_register(struct pistachio_pinctrl *pctl)
{
	struct device_node *node = pctl->dev->of_node;
	struct pistachio_gpio_bank *bank;
	unsigned int i;
	int irq, ret = 0;

	for (i = 0; i < pctl->nbanks; i++) {
		char child_name[sizeof("gpioXX")];
		struct device_node *child;

		snprintf(child_name, sizeof(child_name), "gpio%d", i);
		child = of_get_child_by_name(node, child_name);
		if (!child) {
			dev_err(pctl->dev, "No node for bank %u\n", i);
			ret = -ENODEV;
			goto err;
		}

		if (!of_find_property(child, "gpio-controller", NULL)) {
			dev_err(pctl->dev,
				"No gpio-controller property for bank %u\n", i);
			ret = -ENODEV;
			goto err;
		}

		irq = irq_of_parse_and_map(child, 0);
		if (irq < 0) {
			dev_err(pctl->dev, "No IRQ for bank %u: %d\n", i, irq);
			ret = irq;
			goto err;
		}

		bank = &pctl->gpio_banks[i];
		bank->pctl = pctl;
		bank->base = pctl->base + GPIO_BANK_BASE(i);

		bank->gpio_chip.parent = pctl->dev;
		bank->gpio_chip.of_node = child;
		ret = gpiochip_add_data(&bank->gpio_chip, bank);
		if (ret < 0) {
			dev_err(pctl->dev, "Failed to add GPIO chip %u: %d\n",
				i, ret);
			goto err;
		}

		ret = gpiochip_irqchip_add(&bank->gpio_chip, &bank->irq_chip,
					   0, handle_level_irq, IRQ_TYPE_NONE);
		if (ret < 0) {
			dev_err(pctl->dev, "Failed to add IRQ chip %u: %d\n",
				i, ret);
			gpiochip_remove(&bank->gpio_chip);
			goto err;
		}
		gpiochip_set_chained_irqchip(&bank->gpio_chip, &bank->irq_chip,
					     irq, pistachio_gpio_irq_handler);

		ret = gpiochip_add_pin_range(&bank->gpio_chip,
					     dev_name(pctl->dev), 0,
					     bank->pin_base, bank->npins);
		if (ret < 0) {
			dev_err(pctl->dev, "Failed to add GPIO range %u: %d\n",
				i, ret);
			gpiochip_remove(&bank->gpio_chip);
			goto err;
		}
	}

	return 0;
err:
	for (; i > 0; i--) {
		bank = &pctl->gpio_banks[i - 1];
		gpiochip_remove(&bank->gpio_chip);
	}
	return ret;
}

static const struct of_device_id pistachio_pinctrl_of_match[] = {
	{ .compatible = "img,pistachio-system-pinctrl", },
	{ },
};

static int pistachio_pinctrl_probe(struct platform_device *pdev)
{
	struct pistachio_pinctrl *pctl;
	struct resource *res;

	pctl = devm_kzalloc(&pdev->dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;
	pctl->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, pctl);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pctl->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pctl->base))
		return PTR_ERR(pctl->base);

	pctl->pins = pistachio_pins;
	pctl->npins = ARRAY_SIZE(pistachio_pins);
	pctl->functions = pistachio_functions;
	pctl->nfunctions = ARRAY_SIZE(pistachio_functions);
	pctl->groups = pistachio_groups;
	pctl->ngroups = ARRAY_SIZE(pistachio_groups);
	pctl->gpio_banks = pistachio_gpio_banks;
	pctl->nbanks = ARRAY_SIZE(pistachio_gpio_banks);

	pistachio_pinctrl_desc.pins = pctl->pins;
	pistachio_pinctrl_desc.npins = pctl->npins;

	pctl->pctldev = devm_pinctrl_register(&pdev->dev, &pistachio_pinctrl_desc,
					      pctl);
	if (IS_ERR(pctl->pctldev)) {
		dev_err(&pdev->dev, "Failed to register pinctrl device\n");
		return PTR_ERR(pctl->pctldev);
	}

	return pistachio_gpio_register(pctl);
}

static struct platform_driver pistachio_pinctrl_driver = {
	.driver = {
		.name = "pistachio-pinctrl",
		.of_match_table = pistachio_pinctrl_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = pistachio_pinctrl_probe,
};

static int __init pistachio_pinctrl_register(void)
{
	return platform_driver_register(&pistachio_pinctrl_driver);
}
arch_initcall(pistachio_pinctrl_register);
