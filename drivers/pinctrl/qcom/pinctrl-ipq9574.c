// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * Copyright (c) 2023 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

#define FUNCTION(fname)			                \
	[msm_mux_##fname] = {		                \
		.name = #fname,				\
		.groups = fname##_groups,               \
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{					        \
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = (unsigned int)ARRAY_SIZE(gpio##id##_pins),	\
		.funcs = (int[]){			\
			msm_mux_gpio, /* gpio mode */	\
			msm_mux_##f1,			\
			msm_mux_##f2,			\
			msm_mux_##f3,			\
			msm_mux_##f4,			\
			msm_mux_##f5,			\
			msm_mux_##f6,			\
			msm_mux_##f7,			\
			msm_mux_##f8,			\
			msm_mux_##f9			\
		},				        \
		.nfuncs = 10,				\
		.ctl_reg = REG_SIZE * id,			\
		.io_reg = 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = 0x8 + REG_SIZE * id,		\
		.intr_status_reg = 0xc + REG_SIZE * id,	\
		.intr_target_reg = 0x8 + REG_SIZE * id,	\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
		.oe_bit = 9,			\
		.in_bit = 0,			\
		.out_bit = 1,			\
		.intr_enable_bit = 0,		\
		.intr_status_bit = 0,		\
		.intr_target_bit = 5,		\
		.intr_target_kpss_val = 3,	\
		.intr_raw_status_bit = 4,	\
		.intr_polarity_bit = 1,		\
		.intr_detection_bit = 2,	\
		.intr_detection_width = 2,	\
	}

static const struct pinctrl_pin_desc ipq9574_pins[] = {
	PINCTRL_PIN(0, "GPIO_0"),
	PINCTRL_PIN(1, "GPIO_1"),
	PINCTRL_PIN(2, "GPIO_2"),
	PINCTRL_PIN(3, "GPIO_3"),
	PINCTRL_PIN(4, "GPIO_4"),
	PINCTRL_PIN(5, "GPIO_5"),
	PINCTRL_PIN(6, "GPIO_6"),
	PINCTRL_PIN(7, "GPIO_7"),
	PINCTRL_PIN(8, "GPIO_8"),
	PINCTRL_PIN(9, "GPIO_9"),
	PINCTRL_PIN(10, "GPIO_10"),
	PINCTRL_PIN(11, "GPIO_11"),
	PINCTRL_PIN(12, "GPIO_12"),
	PINCTRL_PIN(13, "GPIO_13"),
	PINCTRL_PIN(14, "GPIO_14"),
	PINCTRL_PIN(15, "GPIO_15"),
	PINCTRL_PIN(16, "GPIO_16"),
	PINCTRL_PIN(17, "GPIO_17"),
	PINCTRL_PIN(18, "GPIO_18"),
	PINCTRL_PIN(19, "GPIO_19"),
	PINCTRL_PIN(20, "GPIO_20"),
	PINCTRL_PIN(21, "GPIO_21"),
	PINCTRL_PIN(22, "GPIO_22"),
	PINCTRL_PIN(23, "GPIO_23"),
	PINCTRL_PIN(24, "GPIO_24"),
	PINCTRL_PIN(25, "GPIO_25"),
	PINCTRL_PIN(26, "GPIO_26"),
	PINCTRL_PIN(27, "GPIO_27"),
	PINCTRL_PIN(28, "GPIO_28"),
	PINCTRL_PIN(29, "GPIO_29"),
	PINCTRL_PIN(30, "GPIO_30"),
	PINCTRL_PIN(31, "GPIO_31"),
	PINCTRL_PIN(32, "GPIO_32"),
	PINCTRL_PIN(33, "GPIO_33"),
	PINCTRL_PIN(34, "GPIO_34"),
	PINCTRL_PIN(35, "GPIO_35"),
	PINCTRL_PIN(36, "GPIO_36"),
	PINCTRL_PIN(37, "GPIO_37"),
	PINCTRL_PIN(38, "GPIO_38"),
	PINCTRL_PIN(39, "GPIO_39"),
	PINCTRL_PIN(40, "GPIO_40"),
	PINCTRL_PIN(41, "GPIO_41"),
	PINCTRL_PIN(42, "GPIO_42"),
	PINCTRL_PIN(43, "GPIO_43"),
	PINCTRL_PIN(44, "GPIO_44"),
	PINCTRL_PIN(45, "GPIO_45"),
	PINCTRL_PIN(46, "GPIO_46"),
	PINCTRL_PIN(47, "GPIO_47"),
	PINCTRL_PIN(48, "GPIO_48"),
	PINCTRL_PIN(49, "GPIO_49"),
	PINCTRL_PIN(50, "GPIO_50"),
	PINCTRL_PIN(51, "GPIO_51"),
	PINCTRL_PIN(52, "GPIO_52"),
	PINCTRL_PIN(53, "GPIO_53"),
	PINCTRL_PIN(54, "GPIO_54"),
	PINCTRL_PIN(55, "GPIO_55"),
	PINCTRL_PIN(56, "GPIO_56"),
	PINCTRL_PIN(57, "GPIO_57"),
	PINCTRL_PIN(58, "GPIO_58"),
	PINCTRL_PIN(59, "GPIO_59"),
	PINCTRL_PIN(60, "GPIO_60"),
	PINCTRL_PIN(61, "GPIO_61"),
	PINCTRL_PIN(62, "GPIO_62"),
	PINCTRL_PIN(63, "GPIO_63"),
	PINCTRL_PIN(64, "GPIO_64"),
};

#define DECLARE_MSM_GPIO_PINS(pin) \
	static const unsigned int gpio##pin##_pins[] = { pin }
DECLARE_MSM_GPIO_PINS(0);
DECLARE_MSM_GPIO_PINS(1);
DECLARE_MSM_GPIO_PINS(2);
DECLARE_MSM_GPIO_PINS(3);
DECLARE_MSM_GPIO_PINS(4);
DECLARE_MSM_GPIO_PINS(5);
DECLARE_MSM_GPIO_PINS(6);
DECLARE_MSM_GPIO_PINS(7);
DECLARE_MSM_GPIO_PINS(8);
DECLARE_MSM_GPIO_PINS(9);
DECLARE_MSM_GPIO_PINS(10);
DECLARE_MSM_GPIO_PINS(11);
DECLARE_MSM_GPIO_PINS(12);
DECLARE_MSM_GPIO_PINS(13);
DECLARE_MSM_GPIO_PINS(14);
DECLARE_MSM_GPIO_PINS(15);
DECLARE_MSM_GPIO_PINS(16);
DECLARE_MSM_GPIO_PINS(17);
DECLARE_MSM_GPIO_PINS(18);
DECLARE_MSM_GPIO_PINS(19);
DECLARE_MSM_GPIO_PINS(20);
DECLARE_MSM_GPIO_PINS(21);
DECLARE_MSM_GPIO_PINS(22);
DECLARE_MSM_GPIO_PINS(23);
DECLARE_MSM_GPIO_PINS(24);
DECLARE_MSM_GPIO_PINS(25);
DECLARE_MSM_GPIO_PINS(26);
DECLARE_MSM_GPIO_PINS(27);
DECLARE_MSM_GPIO_PINS(28);
DECLARE_MSM_GPIO_PINS(29);
DECLARE_MSM_GPIO_PINS(30);
DECLARE_MSM_GPIO_PINS(31);
DECLARE_MSM_GPIO_PINS(32);
DECLARE_MSM_GPIO_PINS(33);
DECLARE_MSM_GPIO_PINS(34);
DECLARE_MSM_GPIO_PINS(35);
DECLARE_MSM_GPIO_PINS(36);
DECLARE_MSM_GPIO_PINS(37);
DECLARE_MSM_GPIO_PINS(38);
DECLARE_MSM_GPIO_PINS(39);
DECLARE_MSM_GPIO_PINS(40);
DECLARE_MSM_GPIO_PINS(41);
DECLARE_MSM_GPIO_PINS(42);
DECLARE_MSM_GPIO_PINS(43);
DECLARE_MSM_GPIO_PINS(44);
DECLARE_MSM_GPIO_PINS(45);
DECLARE_MSM_GPIO_PINS(46);
DECLARE_MSM_GPIO_PINS(47);
DECLARE_MSM_GPIO_PINS(48);
DECLARE_MSM_GPIO_PINS(49);
DECLARE_MSM_GPIO_PINS(50);
DECLARE_MSM_GPIO_PINS(51);
DECLARE_MSM_GPIO_PINS(52);
DECLARE_MSM_GPIO_PINS(53);
DECLARE_MSM_GPIO_PINS(54);
DECLARE_MSM_GPIO_PINS(55);
DECLARE_MSM_GPIO_PINS(56);
DECLARE_MSM_GPIO_PINS(57);
DECLARE_MSM_GPIO_PINS(58);
DECLARE_MSM_GPIO_PINS(59);
DECLARE_MSM_GPIO_PINS(60);
DECLARE_MSM_GPIO_PINS(61);
DECLARE_MSM_GPIO_PINS(62);
DECLARE_MSM_GPIO_PINS(63);
DECLARE_MSM_GPIO_PINS(64);

enum ipq9574_functions {
	msm_mux_atest_char,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_audio_pdm0,
	msm_mux_audio_pdm1,
	msm_mux_audio_pri,
	msm_mux_audio_sec,
	msm_mux_blsp0_spi,
	msm_mux_blsp0_uart,
	msm_mux_blsp1_i2c,
	msm_mux_blsp1_spi,
	msm_mux_blsp1_uart,
	msm_mux_blsp2_i2c,
	msm_mux_blsp2_spi,
	msm_mux_blsp2_uart,
	msm_mux_blsp3_i2c,
	msm_mux_blsp3_spi,
	msm_mux_blsp3_uart,
	msm_mux_blsp4_i2c,
	msm_mux_blsp4_spi,
	msm_mux_blsp4_uart,
	msm_mux_blsp5_i2c,
	msm_mux_blsp5_uart,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_cri_trng2,
	msm_mux_cri_trng3,
	msm_mux_cxc0,
	msm_mux_cxc1,
	msm_mux_dbg_out,
	msm_mux_dwc_ddrphy,
	msm_mux_gcc_plltest,
	msm_mux_gcc_tlmm,
	msm_mux_gpio,
	msm_mux_mac,
	msm_mux_mdc,
	msm_mux_mdio,
	msm_mux_pcie0_clk,
	msm_mux_pcie0_wake,
	msm_mux_pcie1_clk,
	msm_mux_pcie1_wake,
	msm_mux_pcie2_clk,
	msm_mux_pcie2_wake,
	msm_mux_pcie3_clk,
	msm_mux_pcie3_wake,
	msm_mux_prng_rosc0,
	msm_mux_prng_rosc1,
	msm_mux_prng_rosc2,
	msm_mux_prng_rosc3,
	msm_mux_pta,
	msm_mux_pwm,
	msm_mux_qdss_cti_trig_in_a0,
	msm_mux_qdss_cti_trig_in_a1,
	msm_mux_qdss_cti_trig_in_b0,
	msm_mux_qdss_cti_trig_in_b1,
	msm_mux_qdss_cti_trig_out_a0,
	msm_mux_qdss_cti_trig_out_a1,
	msm_mux_qdss_cti_trig_out_b0,
	msm_mux_qdss_cti_trig_out_b1,
	msm_mux_qdss_traceclk_a,
	msm_mux_qdss_traceclk_b,
	msm_mux_qdss_tracectl_a,
	msm_mux_qdss_tracectl_b,
	msm_mux_qdss_tracedata_a,
	msm_mux_qdss_tracedata_b,
	msm_mux_qspi_data,
	msm_mux_qspi_clk,
	msm_mux_qspi_cs,
	msm_mux_rx0,
	msm_mux_rx1,
	msm_mux_sdc_data,
	msm_mux_sdc_clk,
	msm_mux_sdc_cmd,
	msm_mux_sdc_rclk,
	msm_mux_tsens_max,
	msm_mux_wci20,
	msm_mux_wci21,
	msm_mux_wsa_swrm,
	msm_mux__,
};

static const char * const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio13", "gpio14",
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21",
	"gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio27", "gpio28",
	"gpio29", "gpio30", "gpio31", "gpio32", "gpio33", "gpio34", "gpio35",
	"gpio36", "gpio37", "gpio38", "gpio39", "gpio40", "gpio41", "gpio42",
	"gpio43", "gpio44", "gpio45", "gpio46", "gpio47", "gpio48", "gpio49",
	"gpio50", "gpio51", "gpio52", "gpio53", "gpio54", "gpio55", "gpio56",
	"gpio57", "gpio58", "gpio59", "gpio60", "gpio61", "gpio62", "gpio63",
	"gpio64",
};

static const char * const sdc_data_groups[] = {
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
	"gpio6",
	"gpio7",
	"gpio8",
	"gpio9",
};

static const char * const qspi_data_groups[] = {
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
};

static const char * const qdss_traceclk_b_groups[] = {
	"gpio0",
};

static const char * const qdss_tracectl_b_groups[] = {
	"gpio1",
};

static const char * const qdss_tracedata_b_groups[] = {
	"gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7", "gpio8", "gpio9",
	"gpio10", "gpio11", "gpio12", "gpio13", "gpio14", "gpio15", "gpio16",
	"gpio17",
};

static const char * const sdc_cmd_groups[] = {
	"gpio4",
};

static const char * const qspi_cs_groups[] = {
	"gpio4",
};

static const char * const sdc_clk_groups[] = {
	"gpio5",
};

static const char * const qspi_clk_groups[] = {
	"gpio5",
};

static const char * const sdc_rclk_groups[] = {
	"gpio10",
};

static const char * const blsp0_spi_groups[] = {
	"gpio11", "gpio12", "gpio13", "gpio14",
};

static const char * const blsp0_uart_groups[] = {
	"gpio11", "gpio12", "gpio13", "gpio14",
};

static const char * const blsp3_spi_groups[] = {
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21",
};

static const char * const blsp3_i2c_groups[] = {
	"gpio15", "gpio16",
};

static const char * const blsp3_uart_groups[] = {
	"gpio15", "gpio16", "gpio17", "gpio18",
};

static const char * const dbg_out_groups[] = {
	"gpio17",
};

static const char * const cri_trng0_groups[] = {
	"gpio20", "gpio38",
};

static const char * const cri_trng1_groups[] = {
	"gpio21", "gpio34",
};

static const char * const pcie0_clk_groups[] = {
	"gpio22",
};

static const char * const pta_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio54", "gpio55", "gpio56", "gpio61",
	"gpio62", "gpio63",
};

static const char * const wci21_groups[] = {
	"gpio23", "gpio24",
};

static const char * const cxc0_groups[] = {
	"gpio23", "gpio24",
};

static const char * const pcie0_wake_groups[] = {
	"gpio24",
};

static const char * const qdss_cti_trig_out_b0_groups[] = {
	"gpio24",
};

static const char * const pcie1_clk_groups[] = {
	"gpio25",
};

static const char * const qdss_cti_trig_in_b0_groups[] = {
	"gpio25",
};

static const char * const atest_char0_groups[] = {
	"gpio26",
};

static const char * const qdss_cti_trig_out_b1_groups[] = {
	"gpio26",
};

static const char * const pcie1_wake_groups[] = {
	"gpio27",
};

static const char * const atest_char1_groups[] = {
	"gpio27",
};

static const char * const qdss_cti_trig_in_b1_groups[] = {
	"gpio27",
};

static const char * const pcie2_clk_groups[] = {
	"gpio28",
};

static const char * const atest_char2_groups[] = {
	"gpio28",
};

static const char * const atest_char3_groups[] = {
	"gpio29",
};

static const char * const pcie2_wake_groups[] = {
	"gpio30",
};

static const char * const pwm_groups[] = {
	"gpio30", "gpio31", "gpio32", "gpio33", "gpio44", "gpio45", "gpio46",
	"gpio47", "gpio50", "gpio51", "gpio52", "gpio53", "gpio54", "gpio55",
	"gpio56", "gpio57", "gpio58", "gpio59", "gpio60",
};

static const char * const atest_char_groups[] = {
	"gpio30",
};

static const char * const pcie3_clk_groups[] = {
	"gpio31",
};

static const char * const qdss_cti_trig_in_a1_groups[] = {
	"gpio31",
};

static const char * const qdss_cti_trig_out_a1_groups[] = {
	"gpio32",
};

static const char * const pcie3_wake_groups[] = {
	"gpio33",
};

static const char * const qdss_cti_trig_in_a0_groups[] = {
	"gpio33",
};

static const char * const blsp2_uart_groups[] = {
	"gpio34", "gpio35",
};

static const char * const blsp2_i2c_groups[] = {
	"gpio34", "gpio35",
};

static const char * const blsp2_spi_groups[] = {
	"gpio34", "gpio35", "gpio36", "gpio37",
};

static const char * const blsp1_uart_groups[] = {
	"gpio34", "gpio35", "gpio36", "gpio37",
};

static const char * const qdss_cti_trig_out_a0_groups[] = {
	"gpio34",
};

static const char * const cri_trng2_groups[] = {
	"gpio35",
};

static const char * const blsp1_i2c_groups[] = {
	"gpio36", "gpio37",
};

static const char * const cri_trng3_groups[] = {
	"gpio36",
};

static const char * const dwc_ddrphy_groups[] = {
	"gpio37",
};

static const char * const mdc_groups[] = {
	"gpio38",
};

static const char * const mdio_groups[] = {
	"gpio39",
};

static const char * const audio_pri_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43", "gpio61", "gpio61",
};

static const char * const audio_pdm0_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43",
};

static const char * const qdss_traceclk_a_groups[] = {
	"gpio43",
};

static const char * const audio_sec_groups[] = {
	"gpio44", "gpio45", "gpio46", "gpio47", "gpio62", "gpio62",
};

static const char * const wsa_swrm_groups[] = {
	"gpio44", "gpio45",
};

static const char * const qdss_tracectl_a_groups[] = {
	"gpio44",
};

static const char * const qdss_tracedata_a_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48", "gpio49", "gpio50", "gpio51",
	"gpio52", "gpio53", "gpio54", "gpio55", "gpio56", "gpio57", "gpio58",
	"gpio59", "gpio60",
};

static const char * const rx1_groups[] = {
	"gpio46",
};

static const char * const mac_groups[] = {
	"gpio46", "gpio47", "gpio57", "gpio58",
};

static const char * const blsp5_i2c_groups[] = {
	"gpio48", "gpio49",
};

static const char * const blsp5_uart_groups[] = {
	"gpio48", "gpio49",
};

static const char * const blsp4_uart_groups[] = {
	"gpio50", "gpio51", "gpio52", "gpio53",
};

static const char * const blsp4_i2c_groups[] = {
	"gpio50", "gpio51",
};

static const char * const blsp4_spi_groups[] = {
	"gpio50", "gpio51", "gpio52", "gpio53",
};

static const char * const wci20_groups[] = {
	"gpio57", "gpio58",
};

static const char * const cxc1_groups[] = {
	"gpio57", "gpio58",
};

static const char * const rx0_groups[] = {
	"gpio59",
};

static const char * const prng_rosc0_groups[] = {
	"gpio60",
};

static const char * const gcc_plltest_groups[] = {
	"gpio60", "gpio62",
};

static const char * const blsp1_spi_groups[] = {
	"gpio61", "gpio62", "gpio63", "gpio64",
};

static const char * const audio_pdm1_groups[] = {
	"gpio61", "gpio62", "gpio63", "gpio64",
};

static const char * const prng_rosc1_groups[] = {
	"gpio61",
};

static const char * const gcc_tlmm_groups[] = {
	"gpio61",
};

static const char * const prng_rosc2_groups[] = {
	"gpio62",
};

static const char * const prng_rosc3_groups[] = {
	"gpio63",
};

static const char * const tsens_max_groups[] = {
	"gpio64",
};

static const struct msm_function ipq9574_functions[] = {
	FUNCTION(atest_char),
	FUNCTION(atest_char0),
	FUNCTION(atest_char1),
	FUNCTION(atest_char2),
	FUNCTION(atest_char3),
	FUNCTION(audio_pdm0),
	FUNCTION(audio_pdm1),
	FUNCTION(audio_pri),
	FUNCTION(audio_sec),
	FUNCTION(blsp0_spi),
	FUNCTION(blsp0_uart),
	FUNCTION(blsp1_i2c),
	FUNCTION(blsp1_spi),
	FUNCTION(blsp1_uart),
	FUNCTION(blsp2_i2c),
	FUNCTION(blsp2_spi),
	FUNCTION(blsp2_uart),
	FUNCTION(blsp3_i2c),
	FUNCTION(blsp3_spi),
	FUNCTION(blsp3_uart),
	FUNCTION(blsp4_i2c),
	FUNCTION(blsp4_spi),
	FUNCTION(blsp4_uart),
	FUNCTION(blsp5_i2c),
	FUNCTION(blsp5_uart),
	FUNCTION(cri_trng0),
	FUNCTION(cri_trng1),
	FUNCTION(cri_trng2),
	FUNCTION(cri_trng3),
	FUNCTION(cxc0),
	FUNCTION(cxc1),
	FUNCTION(dbg_out),
	FUNCTION(dwc_ddrphy),
	FUNCTION(gcc_plltest),
	FUNCTION(gcc_tlmm),
	FUNCTION(gpio),
	FUNCTION(mac),
	FUNCTION(mdc),
	FUNCTION(mdio),
	FUNCTION(pcie0_clk),
	FUNCTION(pcie0_wake),
	FUNCTION(pcie1_clk),
	FUNCTION(pcie1_wake),
	FUNCTION(pcie2_clk),
	FUNCTION(pcie2_wake),
	FUNCTION(pcie3_clk),
	FUNCTION(pcie3_wake),
	FUNCTION(prng_rosc0),
	FUNCTION(prng_rosc1),
	FUNCTION(prng_rosc2),
	FUNCTION(prng_rosc3),
	FUNCTION(pta),
	FUNCTION(pwm),
	FUNCTION(qdss_cti_trig_in_a0),
	FUNCTION(qdss_cti_trig_in_a1),
	FUNCTION(qdss_cti_trig_in_b0),
	FUNCTION(qdss_cti_trig_in_b1),
	FUNCTION(qdss_cti_trig_out_a0),
	FUNCTION(qdss_cti_trig_out_a1),
	FUNCTION(qdss_cti_trig_out_b0),
	FUNCTION(qdss_cti_trig_out_b1),
	FUNCTION(qdss_traceclk_a),
	FUNCTION(qdss_traceclk_b),
	FUNCTION(qdss_tracectl_a),
	FUNCTION(qdss_tracectl_b),
	FUNCTION(qdss_tracedata_a),
	FUNCTION(qdss_tracedata_b),
	FUNCTION(qspi_data),
	FUNCTION(qspi_clk),
	FUNCTION(qspi_cs),
	FUNCTION(rx0),
	FUNCTION(rx1),
	FUNCTION(sdc_data),
	FUNCTION(sdc_clk),
	FUNCTION(sdc_cmd),
	FUNCTION(sdc_rclk),
	FUNCTION(tsens_max),
	FUNCTION(wci20),
	FUNCTION(wci21),
	FUNCTION(wsa_swrm),
};

static const struct msm_pingroup ipq9574_groups[] = {
	PINGROUP(0, sdc_data, qspi_data, qdss_traceclk_b, _, _, _, _, _, _),
	PINGROUP(1, sdc_data, qspi_data, qdss_tracectl_b, _, _, _, _, _, _),
	PINGROUP(2, sdc_data, qspi_data, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(3, sdc_data, qspi_data, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(4, sdc_cmd, qspi_cs, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(5, sdc_clk, qspi_clk, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(6, sdc_data, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(7, sdc_data, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(8, sdc_data, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(9, sdc_data, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(10, sdc_rclk, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(11, blsp0_spi, blsp0_uart, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(12, blsp0_spi, blsp0_uart, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(13, blsp0_spi, blsp0_uart, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(14, blsp0_spi, blsp0_uart, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(15, blsp3_spi, blsp3_i2c, blsp3_uart, qdss_tracedata_b, _, _, _, _, _),
	PINGROUP(16, blsp3_spi, blsp3_i2c, blsp3_uart, qdss_tracedata_b, _, _, _, _, _),
	PINGROUP(17, blsp3_spi, blsp3_uart, dbg_out, qdss_tracedata_b, _, _, _, _, _),
	PINGROUP(18, blsp3_spi, blsp3_uart, _, _, _, _, _, _, _),
	PINGROUP(19, blsp3_spi, _, _, _, _, _, _, _, _),
	PINGROUP(20, blsp3_spi, _, cri_trng0, _, _, _, _, _, _),
	PINGROUP(21, blsp3_spi, _, cri_trng1, _, _, _, _, _, _),
	PINGROUP(22, pcie0_clk, _, pta, _, _, _, _, _, _),
	PINGROUP(23, _, pta, wci21, cxc0, _, _, _, _, _),
	PINGROUP(24, pcie0_wake, _, pta, wci21, cxc0, _, qdss_cti_trig_out_b0, _, _),
	PINGROUP(25, pcie1_clk, _, _, qdss_cti_trig_in_b0, _, _, _, _, _),
	PINGROUP(26, _, atest_char0, _, qdss_cti_trig_out_b1, _, _, _, _, _),
	PINGROUP(27, pcie1_wake, _, atest_char1, qdss_cti_trig_in_b1, _, _, _, _, _),
	PINGROUP(28, pcie2_clk, atest_char2, _, _, _, _, _, _, _),
	PINGROUP(29, atest_char3, _, _, _, _, _, _, _, _),
	PINGROUP(30, pcie2_wake, pwm, atest_char, _, _, _, _, _, _),
	PINGROUP(31, pcie3_clk, pwm, _, qdss_cti_trig_in_a1, _, _, _, _, _),
	PINGROUP(32, pwm, _, qdss_cti_trig_out_a1, _, _, _, _, _, _),
	PINGROUP(33, pcie3_wake, pwm, _, qdss_cti_trig_in_a0, _, _, _, _, _),
	PINGROUP(34, blsp2_uart, blsp2_i2c, blsp2_spi, blsp1_uart, _, cri_trng1, qdss_cti_trig_out_a0, _, _),
	PINGROUP(35, blsp2_uart, blsp2_i2c, blsp2_spi, blsp1_uart, _, cri_trng2, _, _, _),
	PINGROUP(36, blsp1_uart, blsp1_i2c, blsp2_spi, _, cri_trng3, _, _, _, _),
	PINGROUP(37, blsp1_uart, blsp1_i2c, blsp2_spi, _, dwc_ddrphy, _, _, _, _),
	PINGROUP(38, mdc, _, cri_trng0, _, _, _, _, _, _),
	PINGROUP(39, mdio, _, _, _, _, _, _, _, _),
	PINGROUP(40, audio_pri, audio_pdm0, _, _, _, _, _, _, _),
	PINGROUP(41, audio_pri, audio_pdm0, _, _, _, _, _, _, _),
	PINGROUP(42, audio_pri, audio_pdm0, _, _, _, _, _, _, _),
	PINGROUP(43, audio_pri, audio_pdm0, _, qdss_traceclk_a, _, _, _, _, _),
	PINGROUP(44, pwm, audio_sec, wsa_swrm, _, qdss_tracectl_a, _, _, _, _),
	PINGROUP(45, pwm, audio_sec, wsa_swrm, _, qdss_tracedata_a, _, _, _, _),
	PINGROUP(46, pwm, audio_sec, rx1, mac, _, qdss_tracedata_a, _, _, _),
	PINGROUP(47, pwm, audio_sec, mac, _, qdss_tracedata_a, _, _, _, _),
	PINGROUP(48, blsp5_i2c, blsp5_uart, _, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(49, blsp5_i2c, blsp5_uart, _, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(50, blsp4_uart, blsp4_i2c, blsp4_spi, pwm, qdss_tracedata_a, _, _, _, _),
	PINGROUP(51, blsp4_uart, blsp4_i2c, blsp4_spi, pwm, qdss_tracedata_a, _, _, _, _),
	PINGROUP(52, blsp4_uart, blsp4_spi, pwm, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(53, blsp4_uart, blsp4_spi, pwm, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(54, pta, pwm, qdss_tracedata_a, _, _, _, _, _, _),
	PINGROUP(55, pta, pwm, qdss_tracedata_a, _, _, _, _, _, _),
	PINGROUP(56, pta, pwm, qdss_tracedata_a, _, _, _, _, _, _),
	PINGROUP(57, wci20, cxc1, mac, pwm, qdss_tracedata_a, _, _, _, _),
	PINGROUP(58, wci20, cxc1, mac, pwm, qdss_tracedata_a, _, _, _, _),
	PINGROUP(59, rx0, pwm, qdss_tracedata_a, _, _, _, _, _, _),
	PINGROUP(60, pwm, prng_rosc0, qdss_tracedata_a, _, gcc_plltest, _, _, _, _),
	PINGROUP(61, blsp1_spi, audio_pri, audio_pdm1, audio_pri, pta, prng_rosc1, gcc_tlmm, _, _),
	PINGROUP(62, blsp1_spi, audio_sec, audio_pdm1, audio_sec, pta, prng_rosc2, gcc_plltest, _, _),
	PINGROUP(63, blsp1_spi, audio_pdm1, pta, prng_rosc3, _, _, _, _, _),
	PINGROUP(64, blsp1_spi, audio_pdm1, tsens_max, _, _, _, _, _, _),
};

/* Reserving GPIO59 for controlling the QFPROM LDO regulator */
static const int ipq9574_reserved_gpios[] = {
	59, -1
};

static const struct msm_pinctrl_soc_data ipq9574_pinctrl = {
	.pins = ipq9574_pins,
	.npins = ARRAY_SIZE(ipq9574_pins),
	.functions = ipq9574_functions,
	.nfunctions = ARRAY_SIZE(ipq9574_functions),
	.groups = ipq9574_groups,
	.ngroups = ARRAY_SIZE(ipq9574_groups),
	.reserved_gpios = ipq9574_reserved_gpios,
	.ngpios = 65,
};

static int ipq9574_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &ipq9574_pinctrl);
}

static const struct of_device_id ipq9574_pinctrl_of_match[] = {
	{ .compatible = "qcom,ipq9574-tlmm", },
	{ }
};
MODULE_DEVICE_TABLE(of, ipq9574_pinctrl_of_match);

static struct platform_driver ipq9574_pinctrl_driver = {
	.driver = {
		.name = "ipq9574-tlmm",
		.of_match_table = ipq9574_pinctrl_of_match,
	},
	.probe = ipq9574_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init ipq9574_pinctrl_init(void)
{
	return platform_driver_register(&ipq9574_pinctrl_driver);
}
arch_initcall(ipq9574_pinctrl_init);

static void __exit ipq9574_pinctrl_exit(void)
{
	platform_driver_unregister(&ipq9574_pinctrl_driver);
}
module_exit(ipq9574_pinctrl_exit);

MODULE_DESCRIPTION("QTI IPQ9574 TLMM driver");
MODULE_LICENSE("GPL");
