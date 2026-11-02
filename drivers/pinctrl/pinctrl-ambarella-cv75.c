// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ambarella CV75 pinctrl data
 *
 * Copyright (C) 2026, Ambarella, Inc.
 */

#include <linux/array_size.h>
#include <linux/build_bug.h>
#include <linux/types.h>

#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-ambarella.h"

#define CV75_EXPAND(...) __VA_ARGS__
#define CV75_GROUP_PINS_ALTS(_name, _pin_list, _alt_list)		\
	static const unsigned int cv75_##_name##_pins[] = { CV75_EXPAND _pin_list }; \
	static const u8 cv75_##_name##_alts[] = { CV75_EXPAND _alt_list }; \
	static const struct pingroup cv75_##_name##_grp =		\
		PINCTRL_PINGROUP(#_name, cv75_##_name##_pins,		\
				 ARRAY_SIZE(cv75_##_name##_pins));	\
	static_assert(ARRAY_SIZE(cv75_##_name##_pins) ==		\
		      ARRAY_SIZE(cv75_##_name##_alts))

#define CV75_GROUP(_name)						\
{									\
	.grp = &cv75_##_name##_grp,					\
	.alts = cv75_##_name##_alts,					\
}

#define CV75_FUNCTION(_name) \
	PINCTRL_PINFUNCTION(#_name, cv75_##_name##_groups, ARRAY_SIZE(cv75_##_name##_groups))

/* UART */
CV75_GROUP_PINS_ALTS(uart0, (44, 45), (1, 1));
CV75_GROUP_PINS_ALTS(uart1, (46, 47), (1, 1));
CV75_GROUP_PINS_ALTS(uart1_flow, (48, 49), (1, 1));
CV75_GROUP_PINS_ALTS(uart2_a, (46, 47), (2, 2));
CV75_GROUP_PINS_ALTS(uart2_b, (50, 51), (2, 2));
CV75_GROUP_PINS_ALTS(uart2_c, (66, 68), (2, 2));
CV75_GROUP_PINS_ALTS(uart2_flow_a, (48, 49), (2, 2));
CV75_GROUP_PINS_ALTS(uart2_flow_b, (65, 67), (3, 3));
CV75_GROUP_PINS_ALTS(uart3_a, (70, 72), (3, 3));
CV75_GROUP_PINS_ALTS(uart3_b, (80, 79), (3, 3));
CV75_GROUP_PINS_ALTS(uart3_flow_a, (71, 69), (3, 3));
CV75_GROUP_PINS_ALTS(uart3_flow_b, (81, 82), (3, 3));
CV75_GROUP_PINS_ALTS(uart4_a, (29, 30), (3, 3));
CV75_GROUP_PINS_ALTS(uart4_b, (76, 77), (3, 3));
CV75_GROUP_PINS_ALTS(uart4_flow_a, (28, 31), (3, 3));
CV75_GROUP_PINS_ALTS(uart4_flow_b, (74, 75), (3, 3));

/* Flash */
CV75_GROUP_PINS_ALTS(snand, (79, 80, 81, 82, 83, 84), (1, 1, 1, 1, 1, 1));
CV75_GROUP_PINS_ALTS(spinor, (79, 80, 81, 82, 83, 84, 85),
		     (2, 2, 2, 2, 2, 2, 2));

/* SD/MMC */
CV75_GROUP_PINS_ALTS(sdmmc0_cd, (6), (1));
CV75_GROUP_PINS_ALTS(sdmmc0_wp, (7), (1));
CV75_GROUP_PINS_ALTS(sdmmc0_reset, (8), (1));
CV75_GROUP_PINS_ALTS(sdmmc0_hs_sel, (93), (1));
CV75_GROUP_PINS_ALTS(sdmmc0_1bit, (0, 4, 5), (1, 1, 1));
CV75_GROUP_PINS_ALTS(sdmmc0_4bit, (0, 1, 2, 3, 4, 5), (1, 1, 1, 1, 1, 1));
CV75_GROUP_PINS_ALTS(sdmmc1_cd, (15), (1));
CV75_GROUP_PINS_ALTS(sdmmc1_wp, (16), (1));
CV75_GROUP_PINS_ALTS(sdmmc1_reset, (17), (1));
CV75_GROUP_PINS_ALTS(sdmmc1_hs_sel, (94), (1));
CV75_GROUP_PINS_ALTS(sdmmc1_1bit, (9, 13, 14), (1, 1, 1));
CV75_GROUP_PINS_ALTS(sdmmc1_4bit, (9, 10, 11, 12, 13, 14), (1, 1, 1, 1, 1, 1));

/* Ethernet */
CV75_GROUP_PINS_ALTS(enet_ext_osc_clk, (77), (1));
CV75_GROUP_PINS_ALTS(enet_2nd_ref_clk_a, (78), (1));
CV75_GROUP_PINS_ALTS(enet_2nd_ref_clk_b, (76), (2));
CV75_GROUP_PINS_ALTS(enet0_ptp_pps_o, (74), (1));
CV75_GROUP_PINS_ALTS(rgmii0, (62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 75, 76),
		     (1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1));
CV75_GROUP_PINS_ALTS(rmii0, (62, 63, 64, 67, 68, 71, 72, 73, 75),
		     (1, 1, 1, 1, 1, 1, 1, 1, 2));

/* I2C */
CV75_GROUP_PINS_ALTS(i2c0_a, (67, 68), (2, 2));
CV75_GROUP_PINS_ALTS(i2c0_b, (86, 87), (1, 1));
CV75_GROUP_PINS_ALTS(i2c1_a, (22, 23), (2, 2));
CV75_GROUP_PINS_ALTS(i2c1_b, (69, 70), (2, 2));
CV75_GROUP_PINS_ALTS(i2c2, (88, 89), (1, 1));
CV75_GROUP_PINS_ALTS(i2c3_a, (24, 25), (2, 2));
CV75_GROUP_PINS_ALTS(i2c3_b, (48, 49), (3, 3));
CV75_GROUP_PINS_ALTS(i2c3_c, (58, 59), (3, 3));
CV75_GROUP_PINS_ALTS(i2cs_a, (19, 21), (2, 2));
CV75_GROUP_PINS_ALTS(i2cs_b, (60, 61), (3, 3));
CV75_GROUP_PINS_ALTS(i2cs_c, (71, 72), (2, 2));
CV75_GROUP_PINS_ALTS(i2cs_d, (86, 87), (2, 2));

/* CAN, IR, WDT */
CV75_GROUP_PINS_ALTS(can0, (50, 51), (1, 1));
CV75_GROUP_PINS_ALTS(can1, (52, 53), (1, 1));
CV75_GROUP_PINS_ALTS(ir, (18), (1));
CV75_GROUP_PINS_ALTS(wdt_a, (20), (2));
CV75_GROUP_PINS_ALTS(wdt_b, (27), (3));
CV75_GROUP_PINS_ALTS(wdt_c, (39), (2));
CV75_GROUP_PINS_ALTS(wdt_d, (83), (3));
CV75_GROUP_PINS_ALTS(wdt_e, (85), (4));
CV75_GROUP_PINS_ALTS(wdt_f, (90), (1));

/* I2S */
CV75_GROUP_PINS_ALTS(i2s0, (54, 55, 56, 57), (1, 1, 1, 1));
CV75_GROUP_PINS_ALTS(i2s1, (58, 59, 60, 61), (1, 1, 1, 1));
CV75_GROUP_PINS_ALTS(dmic0, (54, 55), (2, 2));

/* PWM */
CV75_GROUP_PINS_ALTS(pwm0, (40), (1));
CV75_GROUP_PINS_ALTS(pwm1, (41), (1));
CV75_GROUP_PINS_ALTS(pwm2, (42), (1));
CV75_GROUP_PINS_ALTS(pwm3, (43), (1));
CV75_GROUP_PINS_ALTS(pwm4_a, (19), (3));
CV75_GROUP_PINS_ALTS(pwm4_b, (32), (4));
CV75_GROUP_PINS_ALTS(pwm5_a, (20), (3));
CV75_GROUP_PINS_ALTS(pwm5_b, (33), (4));
CV75_GROUP_PINS_ALTS(pwm6_a, (21), (3));
CV75_GROUP_PINS_ALTS(pwm6_b, (34), (4));
CV75_GROUP_PINS_ALTS(pwm7_a, (22), (3));
CV75_GROUP_PINS_ALTS(pwm7_b, (35), (4));
CV75_GROUP_PINS_ALTS(pwm8_a, (23), (3));
CV75_GROUP_PINS_ALTS(pwm8_b, (36), (4));
CV75_GROUP_PINS_ALTS(pwm9_a, (24), (3));
CV75_GROUP_PINS_ALTS(pwm9_b, (37), (4));
CV75_GROUP_PINS_ALTS(pwm10_a, (25), (3));
CV75_GROUP_PINS_ALTS(pwm10_b, (38), (4));
CV75_GROUP_PINS_ALTS(pwm11_a, (26), (3));
CV75_GROUP_PINS_ALTS(pwm11_b, (39), (4));

/* SPI */
CV75_GROUP_PINS_ALTS(spi0, (19, 20, 21), (1, 1, 1));
CV75_GROUP_PINS_ALTS(spi1, (24, 25, 26), (1, 1, 1));
CV75_GROUP_PINS_ALTS(spi2, (28, 29, 30), (1, 1, 1));
CV75_GROUP_PINS_ALTS(spi3_a, (32, 33, 34), (5, 5, 5));
CV75_GROUP_PINS_ALTS(spi3_b, (40, 41, 43), (2, 2, 2));
CV75_GROUP_PINS_ALTS(spi3_c, (58, 59, 60), (2, 2, 2));
CV75_GROUP_PINS_ALTS(spi_slave_a, (24, 25, 26, 27), (4, 4, 4, 4));
CV75_GROUP_PINS_ALTS(spi_slave_b, (28, 29, 30, 31), (2, 2, 2, 2));
CV75_GROUP_PINS_ALTS(spi_slave_c, (36, 37, 38, 39), (5, 5, 5, 5));
CV75_GROUP_PINS_ALTS(spi_slave_d, (50, 51, 52, 53), (3, 3, 3, 3));
CV75_GROUP_PINS_ALTS(spi_slave_e, (81, 82, 83, 84), (4, 4, 4, 4));

/* VIN master sync */
CV75_GROUP_PINS_ALTS(vin_master_sync_a, (91, 92), (1, 1));
CV75_GROUP_PINS_ALTS(vin_master_sync_b, (91, 92), (2, 2));
CV75_GROUP_PINS_ALTS(vin_master_sync_c, (40, 41), (3, 3));
CV75_GROUP_PINS_ALTS(vin_master_sync_d, (46, 47), (3, 3));
CV75_GROUP_PINS_ALTS(vin_master_sync_e, (79, 80), (4, 4));
CV75_GROUP_PINS_ALTS(vsync0, (32), (1));
CV75_GROUP_PINS_ALTS(vsync1, (33), (1));
CV75_GROUP_PINS_ALTS(vsync2, (34), (1));
CV75_GROUP_PINS_ALTS(vsync3, (35), (1));
CV75_GROUP_PINS_ALTS(hsync0, (36), (1));
CV75_GROUP_PINS_ALTS(hsync1, (37), (1));

static const struct amb_pinmux_group cv75_pin_groups[] = {
	/* UART */
	CV75_GROUP(uart0),
	CV75_GROUP(uart1),
	CV75_GROUP(uart1_flow),
	CV75_GROUP(uart2_a),
	CV75_GROUP(uart2_b),
	CV75_GROUP(uart2_c),
	CV75_GROUP(uart2_flow_a),
	CV75_GROUP(uart2_flow_b),
	CV75_GROUP(uart3_a),
	CV75_GROUP(uart3_b),
	CV75_GROUP(uart3_flow_a),
	CV75_GROUP(uart3_flow_b),
	CV75_GROUP(uart4_a),
	CV75_GROUP(uart4_b),
	CV75_GROUP(uart4_flow_a),
	CV75_GROUP(uart4_flow_b),
	/* Flash */
	CV75_GROUP(snand),
	CV75_GROUP(spinor),
	/* SD/MMC */
	CV75_GROUP(sdmmc0_cd),
	CV75_GROUP(sdmmc0_wp),
	CV75_GROUP(sdmmc0_reset),
	CV75_GROUP(sdmmc0_hs_sel),
	CV75_GROUP(sdmmc0_1bit),
	CV75_GROUP(sdmmc0_4bit),
	CV75_GROUP(sdmmc1_cd),
	CV75_GROUP(sdmmc1_wp),
	CV75_GROUP(sdmmc1_reset),
	CV75_GROUP(sdmmc1_hs_sel),
	CV75_GROUP(sdmmc1_1bit),
	CV75_GROUP(sdmmc1_4bit),
	/* Ethernet */
	CV75_GROUP(enet_ext_osc_clk),
	CV75_GROUP(enet_2nd_ref_clk_a),
	CV75_GROUP(enet_2nd_ref_clk_b),
	CV75_GROUP(enet0_ptp_pps_o),
	CV75_GROUP(rgmii0),
	CV75_GROUP(rmii0),
	/* I2C */
	CV75_GROUP(i2c0_a),
	CV75_GROUP(i2c0_b),
	CV75_GROUP(i2c1_a),
	CV75_GROUP(i2c1_b),
	CV75_GROUP(i2c2),
	CV75_GROUP(i2c3_a),
	CV75_GROUP(i2c3_b),
	CV75_GROUP(i2c3_c),
	CV75_GROUP(i2cs_a),
	CV75_GROUP(i2cs_b),
	CV75_GROUP(i2cs_c),
	CV75_GROUP(i2cs_d),
	/* CAN, IR, WDT */
	CV75_GROUP(can0),
	CV75_GROUP(can1),
	CV75_GROUP(ir),
	CV75_GROUP(wdt_a),
	CV75_GROUP(wdt_b),
	CV75_GROUP(wdt_c),
	CV75_GROUP(wdt_d),
	CV75_GROUP(wdt_e),
	CV75_GROUP(wdt_f),
	/* I2S */
	CV75_GROUP(i2s0),
	CV75_GROUP(i2s1),
	CV75_GROUP(dmic0),
	/* PWM */
	CV75_GROUP(pwm0),
	CV75_GROUP(pwm1),
	CV75_GROUP(pwm2),
	CV75_GROUP(pwm3),
	CV75_GROUP(pwm4_a),
	CV75_GROUP(pwm4_b),
	CV75_GROUP(pwm5_a),
	CV75_GROUP(pwm5_b),
	CV75_GROUP(pwm6_a),
	CV75_GROUP(pwm6_b),
	CV75_GROUP(pwm7_a),
	CV75_GROUP(pwm7_b),
	CV75_GROUP(pwm8_a),
	CV75_GROUP(pwm8_b),
	CV75_GROUP(pwm9_a),
	CV75_GROUP(pwm9_b),
	CV75_GROUP(pwm10_a),
	CV75_GROUP(pwm10_b),
	CV75_GROUP(pwm11_a),
	CV75_GROUP(pwm11_b),
	/* SPI */
	CV75_GROUP(spi0),
	CV75_GROUP(spi1),
	CV75_GROUP(spi2),
	CV75_GROUP(spi3_a),
	CV75_GROUP(spi3_b),
	CV75_GROUP(spi3_c),
	CV75_GROUP(spi_slave_a),
	CV75_GROUP(spi_slave_b),
	CV75_GROUP(spi_slave_c),
	CV75_GROUP(spi_slave_d),
	CV75_GROUP(spi_slave_e),
	/* VIN master sync */
	CV75_GROUP(vin_master_sync_a),
	CV75_GROUP(vin_master_sync_b),
	CV75_GROUP(vin_master_sync_c),
	CV75_GROUP(vin_master_sync_d),
	CV75_GROUP(vin_master_sync_e),
	CV75_GROUP(vsync0),
	CV75_GROUP(vsync1),
	CV75_GROUP(vsync2),
	CV75_GROUP(vsync3),
	CV75_GROUP(hsync0),
	CV75_GROUP(hsync1),
};

static const char * const cv75_uart0_groups[] = {
	"uart0",
};

static const char * const cv75_uart1_groups[] = {
	"uart1",
	"uart1_flow",
};

static const char * const cv75_uart2_groups[] = {
	"uart2_a",
	"uart2_b",
	"uart2_c",
	"uart2_flow_a",
	"uart2_flow_b",
};

static const char * const cv75_uart3_groups[] = {
	"uart3_a",
	"uart3_b",
	"uart3_flow_a",
	"uart3_flow_b",
};

static const char * const cv75_uart4_groups[] = {
	"uart4_a",
	"uart4_b",
	"uart4_flow_a",
	"uart4_flow_b",
};

static const char * const cv75_snand_groups[] = {
	"snand",
};

static const char * const cv75_spinor_groups[] = {
	"spinor",
};

static const char * const cv75_sdmmc0_groups[] = {
	"sdmmc0_cd",
	"sdmmc0_wp",
	"sdmmc0_reset",
	"sdmmc0_hs_sel",
	"sdmmc0_1bit",
	"sdmmc0_4bit",
};

static const char * const cv75_sdmmc1_groups[] = {
	"sdmmc1_cd",
	"sdmmc1_wp",
	"sdmmc1_reset",
	"sdmmc1_hs_sel",
	"sdmmc1_1bit",
	"sdmmc1_4bit",
};

static const char * const cv75_enet0_groups[] = {
	"enet_ext_osc_clk",
	"enet_2nd_ref_clk_a",
	"enet_2nd_ref_clk_b",
	"enet0_ptp_pps_o",
	"rgmii0",
	"rmii0",
};

static const char * const cv75_i2c0_groups[] = {
	"i2c0_a",
	"i2c0_b",
};

static const char * const cv75_i2c1_groups[] = {
	"i2c1_a",
	"i2c1_b",
};

static const char * const cv75_i2c2_groups[] = {
	"i2c2",
};

static const char * const cv75_i2c3_groups[] = {
	"i2c3_a",
	"i2c3_b",
	"i2c3_c",
};

static const char * const cv75_i2cs_groups[] = {
	"i2cs_a",
	"i2cs_b",
	"i2cs_c",
	"i2cs_d",
};

static const char * const cv75_can0_groups[] = {
	"can0",
};

static const char * const cv75_can1_groups[] = {
	"can1",
};

static const char * const cv75_ir_groups[] = {
	"ir",
};

static const char * const cv75_wdt_groups[] = {
	"wdt_a",
	"wdt_b",
	"wdt_c",
	"wdt_d",
	"wdt_e",
	"wdt_f",
};

static const char * const cv75_i2s0_groups[] = {
	"i2s0",
};

static const char * const cv75_i2s1_groups[] = {
	"i2s1",
};

static const char * const cv75_dmic0_groups[] = {
	"dmic0",
};

static const char * const cv75_pwm0_groups[] = {
	"pwm0",
};

static const char * const cv75_pwm1_groups[] = {
	"pwm1",
};

static const char * const cv75_pwm2_groups[] = {
	"pwm2",
};

static const char * const cv75_pwm3_groups[] = {
	"pwm3",
};

static const char * const cv75_pwm4_groups[] = {
	"pwm4_a",
	"pwm4_b",
};

static const char * const cv75_pwm5_groups[] = {
	"pwm5_a",
	"pwm5_b",
};

static const char * const cv75_pwm6_groups[] = {
	"pwm6_a",
	"pwm6_b",
};

static const char * const cv75_pwm7_groups[] = {
	"pwm7_a",
	"pwm7_b",
};

static const char * const cv75_pwm8_groups[] = {
	"pwm8_a",
	"pwm8_b",
};

static const char * const cv75_pwm9_groups[] = {
	"pwm9_a",
	"pwm9_b",
};

static const char * const cv75_pwm10_groups[] = {
	"pwm10_a",
	"pwm10_b",
};

static const char * const cv75_pwm11_groups[] = {
	"pwm11_a",
	"pwm11_b",
};

static const char * const cv75_spi0_groups[] = {
	"spi0",
};

static const char * const cv75_spi1_groups[] = {
	"spi1",
};

static const char * const cv75_spi2_groups[] = {
	"spi2",
};

static const char * const cv75_spi3_groups[] = {
	"spi3_a",
	"spi3_b",
	"spi3_c",
};

static const char * const cv75_spi_slave_groups[] = {
	"spi_slave_a",
	"spi_slave_b",
	"spi_slave_c",
	"spi_slave_d",
	"spi_slave_e",
};

static const char * const cv75_vin_master_sync_groups[] = {
	"vin_master_sync_a",
	"vin_master_sync_b",
	"vin_master_sync_c",
	"vin_master_sync_d",
	"vin_master_sync_e",
};

static const char * const cv75_vsync0_groups[] = {
	"vsync0",
};

static const char * const cv75_vsync1_groups[] = {
	"vsync1",
};

static const char * const cv75_vsync2_groups[] = {
	"vsync2",
};

static const char * const cv75_vsync3_groups[] = {
	"vsync3",
};

static const char * const cv75_hsync0_groups[] = {
	"hsync0",
};

static const char * const cv75_hsync1_groups[] = {
	"hsync1",
};

static const struct pinfunction cv75_pin_functions[] = {
	CV75_FUNCTION(uart0),
	CV75_FUNCTION(uart1),
	CV75_FUNCTION(uart2),
	CV75_FUNCTION(uart3),
	CV75_FUNCTION(uart4),
	CV75_FUNCTION(snand),
	CV75_FUNCTION(spinor),
	CV75_FUNCTION(sdmmc0),
	CV75_FUNCTION(sdmmc1),
	CV75_FUNCTION(enet0),
	CV75_FUNCTION(i2c0),
	CV75_FUNCTION(i2c1),
	CV75_FUNCTION(i2c2),
	CV75_FUNCTION(i2c3),
	CV75_FUNCTION(i2cs),
	CV75_FUNCTION(can0),
	CV75_FUNCTION(can1),
	CV75_FUNCTION(ir),
	CV75_FUNCTION(wdt),
	CV75_FUNCTION(i2s0),
	CV75_FUNCTION(i2s1),
	CV75_FUNCTION(dmic0),
	CV75_FUNCTION(pwm0),
	CV75_FUNCTION(pwm1),
	CV75_FUNCTION(pwm2),
	CV75_FUNCTION(pwm3),
	CV75_FUNCTION(pwm4),
	CV75_FUNCTION(pwm5),
	CV75_FUNCTION(pwm6),
	CV75_FUNCTION(pwm7),
	CV75_FUNCTION(pwm8),
	CV75_FUNCTION(pwm9),
	CV75_FUNCTION(pwm10),
	CV75_FUNCTION(pwm11),
	CV75_FUNCTION(spi0),
	CV75_FUNCTION(spi1),
	CV75_FUNCTION(spi2),
	CV75_FUNCTION(spi3),
	CV75_FUNCTION(spi_slave),
	CV75_FUNCTION(vin_master_sync),
	CV75_FUNCTION(vsync0),
	CV75_FUNCTION(vsync1),
	CV75_FUNCTION(vsync2),
	CV75_FUNCTION(vsync3),
	CV75_FUNCTION(hsync0),
	CV75_FUNCTION(hsync1),
};

const struct amb_pinctrl_data ambarella_cv75_pinctrl_data = {
	.groups = cv75_pin_groups,
	.functions = cv75_pin_functions,
	.ngroups = ARRAY_SIZE(cv75_pin_groups),
	.nfunctions = ARRAY_SIZE(cv75_pin_functions),
	.nr_banks = 3,
	.npins = 96,
	.ds0 = {
		0x314, 0x320, 0x32c,
	},
	.ds1 = {
		0x318, 0x324, 0x330,
	},
	.ds2 = {
		0x31c, 0x328, 0x334,
	},
	.pull_en = {
		0x60, 0x64, 0x68,
	},
	.pull_dir = {
		0x7c, 0x80, 0x84,
	},
	.have_ds2 = true,
};
