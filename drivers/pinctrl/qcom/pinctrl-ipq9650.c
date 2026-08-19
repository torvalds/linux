// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	      \
	{                                                             \
		.grp = PINCTRL_PINGROUP("gpio" #id,                   \
					gpio##id##_pins,              \
					ARRAY_SIZE(gpio##id##_pins)), \
		.ctl_reg = REG_SIZE * id,                             \
		.io_reg = 0x4 + REG_SIZE * id,                        \
		.intr_cfg_reg = 0x8 + REG_SIZE * id,                  \
		.intr_status_reg = 0xc + REG_SIZE * id,               \
		.mux_bit = 2,                                         \
		.pull_bit = 0,                                        \
		.drv_bit = 6,                                         \
		.oe_bit = 9,                                          \
		.in_bit = 0,                                          \
		.out_bit = 1,                                         \
		.intr_enable_bit = 0,                                 \
		.intr_status_bit = 0,                                 \
		.intr_target_bit = 5,                                 \
		.intr_target_kpss_val = 3,                            \
		.intr_raw_status_bit = 4,                             \
		.intr_polarity_bit = 1,                               \
		.intr_detection_bit = 2,                              \
		.intr_detection_width = 2,                            \
		.funcs = (int[]){                                     \
			msm_mux_gpio, /* gpio mode */                 \
			msm_mux_##f1,                                 \
			msm_mux_##f2,                                 \
			msm_mux_##f3,                                 \
			msm_mux_##f4,                                 \
			msm_mux_##f5,                                 \
			msm_mux_##f6,                                 \
			msm_mux_##f7,                                 \
			msm_mux_##f8,                                 \
			msm_mux_##f9,                                 \
		},                                                    \
		.nfuncs = 10,                                         \
	}

static const struct pinctrl_pin_desc ipq9650_pins[] = {
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

enum ipq9650_functions {
	msm_mux_atest_char_start,
	msm_mux_atest_char_status0,
	msm_mux_atest_char_status1,
	msm_mux_atest_char_status2,
	msm_mux_atest_char_status3,
	msm_mux_atest_tic_en,
	msm_mux_audio_pri_mclk_in0,
	msm_mux_audio_pri_mclk_out0,
	msm_mux_audio_pri_mclk_in1,
	msm_mux_audio_pri_mclk_out1,
	msm_mux_audio_pri,
	msm_mux_audio_sec,
	msm_mux_audio_sec_mclk_in0,
	msm_mux_audio_sec_mclk_out0,
	msm_mux_audio_sec_mclk_in1,
	msm_mux_audio_sec_mclk_out1,
	msm_mux_core_voltage_0,
	msm_mux_core_voltage_1,
	msm_mux_core_voltage_2,
	msm_mux_core_voltage_3,
	msm_mux_core_voltage_4,
	msm_mux_cri_rng0,
	msm_mux_cri_rng1,
	msm_mux_cri_rng2,
	msm_mux_dbg_out_clk,
	msm_mux_gcc_plltest_bypassnl,
	msm_mux_gcc_plltest_resetn,
	msm_mux_gcc_tlmm,
	msm_mux_gpio,
	msm_mux_mdc_mst,
	msm_mux_mdc_slv0,
	msm_mux_mdc_slv1,
	msm_mux_mdio_mst,
	msm_mux_mdio_slv,
	msm_mux_mdio_slv0,
	msm_mux_mdio_slv1,
	msm_mux_pcie0_clk_req_n,
	msm_mux_pcie0_wake,
	msm_mux_pcie1_clk_req_n,
	msm_mux_pcie1_wake,
	msm_mux_pcie2_clk_req_n,
	msm_mux_pcie2_wake,
	msm_mux_pcie3_clk_req_n,
	msm_mux_pcie3_wake,
	msm_mux_pcie4_clk_req_n,
	msm_mux_pcie4_wake,
	msm_mux_pll_bist_sync,
	msm_mux_pll_test,
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
	msm_mux_qdss_tracectl_a,
	msm_mux_qdss_tracedata_a,
	msm_mux_qspi_data,
	msm_mux_qspi_clk,
	msm_mux_qspi_cs_n,
	msm_mux_qup_se0,
	msm_mux_qup_se1,
	msm_mux_qup_se2,
	msm_mux_qup_se3,
	msm_mux_qup_se4,
	msm_mux_qup_se5,
	msm_mux_qup_se6,
	msm_mux_qup_se7,
	msm_mux_resout,
	msm_mux_rx_los0,
	msm_mux_rx_los1,
	msm_mux_rx_los2,
	msm_mux_sdc_clk,
	msm_mux_sdc_cmd,
	msm_mux_sdc_data,
	msm_mux_tsens_max,
	msm_mux_tsn,
	msm_mux__,
};

static const char *const gpio_groups[] = {
	"gpio0",  "gpio1",  "gpio2",  "gpio3",	"gpio4",  "gpio5",  "gpio6",
	"gpio7",  "gpio8",  "gpio9",  "gpio10", "gpio11", "gpio12", "gpio13",
	"gpio14", "gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20",
	"gpio21", "gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio27",
	"gpio28", "gpio29", "gpio30", "gpio31", "gpio32", "gpio33", "gpio34",
	"gpio35", "gpio36", "gpio37", "gpio38", "gpio39", "gpio40", "gpio41",
	"gpio42", "gpio43", "gpio44", "gpio45", "gpio46", "gpio47", "gpio48",
	"gpio49", "gpio50", "gpio51", "gpio52", "gpio53",
};

static const char *const atest_char_start_groups[] = {
	"gpio21",
};

static const char *const atest_char_status0_groups[] = {
	"gpio33",
};

static const char *const atest_char_status1_groups[] = {
	"gpio35",
};

static const char *const atest_char_status2_groups[] = {
	"gpio22",
};

static const char *const atest_char_status3_groups[] = {
	"gpio23",
};

static const char *const atest_tic_en_groups[] = {
	"gpio53",
};

static const char *const audio_pri_mclk_in0_groups[] = {
	"gpio53",
};

static const char *const audio_pri_mclk_out0_groups[] = {
	"gpio53",
};

static const char *const audio_pri_mclk_in1_groups[] = {
	"gpio51",
};

static const char *const audio_pri_mclk_out1_groups[] = {
	"gpio51",
};

static const char *const audio_pri_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};

static const char *const audio_sec_mclk_in0_groups[] = {
	"gpio37",
};

static const char *const audio_sec_mclk_out0_groups[] = {
	"gpio37",
};

static const char *const audio_sec_mclk_in1_groups[] = {
	"gpio37",
};

static const char *const audio_sec_mclk_out1_groups[] = {
	"gpio37",
};

static const char *const audio_sec_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48",
};

static const char *const core_voltage_0_groups[] = {
	"gpio16",
};

static const char *const core_voltage_1_groups[] = {
	"gpio17",
};

static const char *const core_voltage_2_groups[] = {
	"gpio33",
};

static const char *const core_voltage_3_groups[] = {
	"gpio34",
};

static const char *const core_voltage_4_groups[] = {
	"gpio35",
};

static const char *const cri_rng0_groups[] = {
	"gpio6",
};

static const char *const cri_rng1_groups[] = {
	"gpio7",
};

static const char *const cri_rng2_groups[] = {
	"gpio8",
};

static const char *const dbg_out_clk_groups[] = {
	"gpio46",
};

static const char *const gcc_plltest_bypassnl_groups[] = {
	"gpio33",
};

static const char *const gcc_plltest_resetn_groups[] = {
	"gpio35",
};

static const char *const gcc_tlmm_groups[] = {
	"gpio34",
};

static const char *const mdc_mst_groups[] = {
	"gpio22",
};

static const char *const mdc_slv0_groups[] = {
	"gpio20",
};

static const char *const mdc_slv1_groups[] = {
	"gpio14",
};

static const char *const mdio_mst_groups[] = {
	"gpio23",
};

static const char *const mdio_slv_groups[] = {
	"gpio46",
	"gpio47",
};

static const char *const mdio_slv0_groups[] = {
	"gpio21",
};

static const char *const mdio_slv1_groups[] = {
	"gpio15",
};

static const char *const pcie0_clk_req_n_groups[] = {
	"gpio24",
};

static const char *const pcie0_wake_groups[] = {
	"gpio26",
};

static const char *const pcie1_clk_req_n_groups[] = {
	"gpio27",
};

static const char *const pcie1_wake_groups[] = {
	"gpio29",
};

static const char *const pcie2_clk_req_n_groups[] = {
	"gpio51",
};

static const char *const pcie2_wake_groups[] = {
	"gpio53",
};

static const char *const pcie3_clk_req_n_groups[] = {
	"gpio40",
};

static const char *const pcie3_wake_groups[] = {
	"gpio42",
};

static const char *const pcie4_clk_req_n_groups[] = {
	"gpio30",
};

static const char *const pcie4_wake_groups[] = {
	"gpio32",
};

static const char *const pll_bist_sync_groups[] = {
	"gpio47",
};

static const char *const pll_test_groups[] = {
	"gpio39",
};

static const char *const pwm_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9", "gpio10", "gpio11", "gpio16",
	"gpio17", "gpio33", "gpio34", "gpio35", "gpio43", "gpio44", "gpio45",
	"gpio46", "gpio47", "gpio48",
};

static const char *const qdss_cti_trig_in_a0_groups[] = {
	"gpio53",
};

static const char *const qdss_cti_trig_in_a1_groups[] = {
	"gpio29",
};

static const char *const qdss_cti_trig_in_b0_groups[] = {
	"gpio42",
};

static const char *const qdss_cti_trig_in_b1_groups[] = {
	"gpio43",
};

static const char *const qdss_cti_trig_out_a0_groups[] = {
	"gpio51",
};

static const char *const qdss_cti_trig_out_a1_groups[] = {
	"gpio27",
};

static const char *const qdss_cti_trig_out_b0_groups[] = {
	"gpio40",
};

static const char *const qdss_cti_trig_out_b1_groups[] = {
	"gpio44",
};

static const char *const qdss_traceclk_a_groups[] = {
	"gpio45",
};

static const char *const qdss_tracectl_a_groups[] = {
	"gpio46",
};

static const char *const qdss_tracedata_a_groups[] = {
	"gpio6",  "gpio7",  "gpio8",  "gpio9",	"gpio10", "gpio11",
	"gpio12", "gpio13", "gpio14", "gpio15", "gpio20", "gpio21",
	"gpio36", "gpio37", "gpio38", "gpio39",
};

static const char *const qspi_data_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};

static const char *const qspi_clk_groups[] = {
	"gpio5",
};

static const char *const qspi_cs_n_groups[] = {
	"gpio4",
};

static const char *const qup_se0_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9", "gpio51", "gpio53",
};

static const char *const qup_se1_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13", "gpio27", "gpio29",
};

static const char *const qup_se2_groups[] = {
	"gpio27", "gpio29", "gpio33", "gpio34",
};

static const char *const qup_se3_groups[] = {
	"gpio16", "gpio17", "gpio20", "gpio21",
};

static const char *const qup_se4_groups[] = {
	"gpio14", "gpio15", "gpio40", "gpio42", "gpio43", "gpio44",
};

static const char *const qup_se5_groups[] = {
	"gpio40", "gpio42", "gpio45", "gpio46", "gpio47", "gpio48",
};

static const char *const qup_se6_groups[] = {
	"gpio43", "gpio44", "gpio51", "gpio53",
};

static const char *const qup_se7_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};

static const char *const resout_groups[] = {
	"gpio49",
};

static const char *const rx_los0_groups[] = {
	"gpio39", "gpio47", "gpio50",
};

static const char *const rx_los1_groups[] = {
	"gpio38", "gpio46",
};

static const char *const rx_los2_groups[] = {
	"gpio37", "gpio45",
};

static const char *const sdc_clk_groups[] = {
	"gpio5",
};

static const char *const sdc_cmd_groups[] = {
	"gpio4",
};

static const char *const sdc_data_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};

static const char *const tsens_max_groups[] = {
	"gpio14",
};

static const char *const tsn_groups[] = {
	"gpio50",
};

static const struct pinfunction ipq9650_functions[] = {
	MSM_PIN_FUNCTION(atest_char_start),
	MSM_PIN_FUNCTION(atest_char_status0),
	MSM_PIN_FUNCTION(atest_char_status1),
	MSM_PIN_FUNCTION(atest_char_status2),
	MSM_PIN_FUNCTION(atest_char_status3),
	MSM_PIN_FUNCTION(atest_tic_en),
	MSM_PIN_FUNCTION(audio_pri_mclk_in0),
	MSM_PIN_FUNCTION(audio_pri_mclk_out0),
	MSM_PIN_FUNCTION(audio_pri_mclk_in1),
	MSM_PIN_FUNCTION(audio_pri_mclk_out1),
	MSM_PIN_FUNCTION(audio_pri),
	MSM_PIN_FUNCTION(audio_sec),
	MSM_PIN_FUNCTION(audio_sec_mclk_in0),
	MSM_PIN_FUNCTION(audio_sec_mclk_out0),
	MSM_PIN_FUNCTION(audio_sec_mclk_in1),
	MSM_PIN_FUNCTION(audio_sec_mclk_out1),
	MSM_PIN_FUNCTION(core_voltage_0),
	MSM_PIN_FUNCTION(core_voltage_1),
	MSM_PIN_FUNCTION(core_voltage_2),
	MSM_PIN_FUNCTION(core_voltage_3),
	MSM_PIN_FUNCTION(core_voltage_4),
	MSM_PIN_FUNCTION(cri_rng0),
	MSM_PIN_FUNCTION(cri_rng1),
	MSM_PIN_FUNCTION(cri_rng2),
	MSM_PIN_FUNCTION(dbg_out_clk),
	MSM_PIN_FUNCTION(gcc_plltest_bypassnl),
	MSM_PIN_FUNCTION(gcc_plltest_resetn),
	MSM_PIN_FUNCTION(gcc_tlmm),
	MSM_GPIO_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(mdc_mst),
	MSM_PIN_FUNCTION(mdc_slv0),
	MSM_PIN_FUNCTION(mdc_slv1),
	MSM_PIN_FUNCTION(mdio_mst),
	MSM_PIN_FUNCTION(mdio_slv),
	MSM_PIN_FUNCTION(mdio_slv0),
	MSM_PIN_FUNCTION(mdio_slv1),
	MSM_PIN_FUNCTION(pcie0_clk_req_n),
	MSM_PIN_FUNCTION(pcie0_wake),
	MSM_PIN_FUNCTION(pcie1_clk_req_n),
	MSM_PIN_FUNCTION(pcie1_wake),
	MSM_PIN_FUNCTION(pcie2_clk_req_n),
	MSM_PIN_FUNCTION(pcie2_wake),
	MSM_PIN_FUNCTION(pcie3_clk_req_n),
	MSM_PIN_FUNCTION(pcie3_wake),
	MSM_PIN_FUNCTION(pcie4_clk_req_n),
	MSM_PIN_FUNCTION(pcie4_wake),
	MSM_PIN_FUNCTION(pll_bist_sync),
	MSM_PIN_FUNCTION(pll_test),
	MSM_PIN_FUNCTION(pwm),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a0),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a1),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b1),
	MSM_PIN_FUNCTION(qdss_traceclk_a),
	MSM_PIN_FUNCTION(qdss_tracectl_a),
	MSM_PIN_FUNCTION(qdss_tracedata_a),
	MSM_PIN_FUNCTION(qspi_data),
	MSM_PIN_FUNCTION(qspi_clk),
	MSM_PIN_FUNCTION(qspi_cs_n),
	MSM_PIN_FUNCTION(qup_se0),
	MSM_PIN_FUNCTION(qup_se1),
	MSM_PIN_FUNCTION(qup_se2),
	MSM_PIN_FUNCTION(qup_se3),
	MSM_PIN_FUNCTION(qup_se4),
	MSM_PIN_FUNCTION(qup_se5),
	MSM_PIN_FUNCTION(qup_se6),
	MSM_PIN_FUNCTION(qup_se7),
	MSM_PIN_FUNCTION(resout),
	MSM_PIN_FUNCTION(rx_los0),
	MSM_PIN_FUNCTION(rx_los1),
	MSM_PIN_FUNCTION(rx_los2),
	MSM_PIN_FUNCTION(sdc_clk),
	MSM_PIN_FUNCTION(sdc_cmd),
	MSM_PIN_FUNCTION(sdc_data),
	MSM_PIN_FUNCTION(tsens_max),
	MSM_PIN_FUNCTION(tsn),
};

static const struct msm_pingroup ipq9650_groups[] = {
	[0] = PINGROUP(0, sdc_data, qspi_data, _, _, _, _, _, _, _),
	[1] = PINGROUP(1, sdc_data, qspi_data, _, _, _, _, _, _, _),
	[2] = PINGROUP(2, sdc_data, qspi_data, _, _, _, _, _, _, _),
	[3] = PINGROUP(3, sdc_data, qspi_data, _, _, _, _, _, _, _),
	[4] = PINGROUP(4, sdc_cmd, qspi_cs_n, _, _, _, _, _, _, _),
	[5] = PINGROUP(5, sdc_clk, qspi_clk, _, _, _, _, _, _, _),
	[6] = PINGROUP(6, qup_se0, pwm, _, cri_rng0, qdss_tracedata_a, _, _, _, _),
	[7] = PINGROUP(7, qup_se0, pwm, _, cri_rng1, qdss_tracedata_a, _, _, _, _),
	[8] = PINGROUP(8, qup_se0, pwm, _, cri_rng2, qdss_tracedata_a, _, _, _, _),
	[9] = PINGROUP(9, qup_se0, pwm, _, qdss_tracedata_a, _, _, _, _, _),
	[10] = PINGROUP(10, qup_se1, pwm, _, _, qdss_tracedata_a, _, _, _, _),
	[11] = PINGROUP(11, qup_se1, pwm, _, _, qdss_tracedata_a, _, _, _, _),
	[12] = PINGROUP(12, qup_se1, _, qdss_tracedata_a, _, _, _, _, _, _),
	[13] = PINGROUP(13, qup_se1, _, qdss_tracedata_a, _, _, _, _, _, _),
	[14] = PINGROUP(14, qup_se4, mdc_slv1, tsens_max, _, qdss_tracedata_a, _, _, _, _),
	[15] = PINGROUP(15, qup_se4, mdio_slv1, _, qdss_tracedata_a, _, _, _, _, _),
	[16] = PINGROUP(16, core_voltage_0, qup_se3, pwm, _, _, _, _, _, _),
	[17] = PINGROUP(17, core_voltage_1, qup_se3, pwm, _, _, _, _, _, _),
	[18] = PINGROUP(18, _, _, _, _, _, _, _, _, _),
	[19] = PINGROUP(19, _, _, _, _, _, _, _, _, _),
	[20] = PINGROUP(20, mdc_slv0, qup_se3, _, qdss_tracedata_a, _, _, _, _, _),
	[21] = PINGROUP(21, mdio_slv0, qup_se3, atest_char_start, _, qdss_tracedata_a, _, _, _, _),
	[22] = PINGROUP(22, mdc_mst, atest_char_status2, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, mdio_mst, atest_char_status3, _, _, _, _, _, _, _),
	[24] = PINGROUP(24, pcie0_clk_req_n, _, _, _, _, _, _, _, _),
	[25] = PINGROUP(25, _, _, _, _, _, _, _, _, _),
	[26] = PINGROUP(26, pcie0_wake, _, _, _, _, _, _, _, _),
	[27] = PINGROUP(27, pcie1_clk_req_n, qup_se2, qup_se1, _, qdss_cti_trig_out_a1, _, _, _, _),
	[28] = PINGROUP(28, _, _, _, _, _, _, _, _, _),
	[29] = PINGROUP(29, pcie1_wake, qup_se2, qup_se1, _, qdss_cti_trig_in_a1, _, _, _, _),
	[30] = PINGROUP(30, pcie4_clk_req_n, _, _, _, _, _, _, _, _),
	[31] = PINGROUP(31, _, _, _, _, _, _, _, _, _),
	[32] = PINGROUP(32, pcie4_wake, _, _, _, _, _, _, _, _),
	[33] = PINGROUP(33, core_voltage_2, qup_se2, gcc_plltest_bypassnl, pwm, atest_char_status0, _, _, _, _),
	[34] = PINGROUP(34, core_voltage_3, qup_se2, gcc_tlmm, pwm, _, _, _, _, _),
	[35] = PINGROUP(35, core_voltage_4, gcc_plltest_resetn, pwm, atest_char_status1, _, _, _, _, _),
	[36] = PINGROUP(36, audio_pri, qup_se7, qdss_tracedata_a, _, _, _, _, _, _),
	[37] = PINGROUP(37, audio_pri, qup_se7, audio_sec_mclk_out0, audio_sec_mclk_in0, rx_los2, qdss_tracedata_a, _, _, _),
	[38] = PINGROUP(38, audio_pri, qup_se7, rx_los1, qdss_tracedata_a, _, _, _, _, _),
	[39] = PINGROUP(39, audio_pri, qup_se7, audio_sec_mclk_out1, audio_sec_mclk_in1, pll_test, rx_los0, _, qdss_tracedata_a, _),
	[40] = PINGROUP(40, pcie3_clk_req_n, qup_se5, qup_se4, _, qdss_cti_trig_out_b0, _, _, _, _),
	[41] = PINGROUP(41, _, _, _, _, _, _, _, _, _),
	[42] = PINGROUP(42, pcie3_wake, qup_se5, qup_se4, _, qdss_cti_trig_in_b0, _, _, _, _),
	[43] = PINGROUP(43, qup_se4, qup_se6, pwm, _, qdss_cti_trig_in_b1, _, _, _, _),
	[44] = PINGROUP(44, qup_se4, qup_se6, pwm, _, qdss_cti_trig_out_b1, _, _, _, _),
	[45] = PINGROUP(45, qup_se5, rx_los2, audio_sec, pwm, _, qdss_traceclk_a, _, _, _),
	[46] = PINGROUP(46, qup_se5, rx_los1, audio_sec, mdio_slv, pwm, dbg_out_clk, qdss_tracectl_a, _, _),
	[47] = PINGROUP(47, qup_se5, rx_los0, audio_sec, mdio_slv, pll_bist_sync, pwm, _, _, _),
	[48] = PINGROUP(48, qup_se5, audio_sec, pwm, _, _, _, _, _, _),
	[49] = PINGROUP(49, resout, _, _, _, _, _, _, _, _),
	[50] = PINGROUP(50, tsn, rx_los0, _, _, _, _, _, _, _),
	[51] = PINGROUP(51, pcie2_clk_req_n, qup_se6, qup_se0, audio_pri_mclk_out1, audio_pri_mclk_in1, qdss_cti_trig_out_a0, _, _, _),
	[52] = PINGROUP(52, _, _, _, _, _, _, _, _, _),
	[53] = PINGROUP(53, pcie2_wake, qup_se6, qup_se0, audio_pri_mclk_out0, audio_pri_mclk_in0, qdss_cti_trig_in_a0, _, atest_tic_en, _),
};

static const struct msm_pinctrl_soc_data ipq9650_tlmm = {
	.pins = ipq9650_pins,
	.npins = ARRAY_SIZE(ipq9650_pins),
	.functions = ipq9650_functions,
	.nfunctions = ARRAY_SIZE(ipq9650_functions),
	.groups = ipq9650_groups,
	.ngroups = ARRAY_SIZE(ipq9650_groups),
	.ngpios = 54,
};

static const struct of_device_id ipq9650_tlmm_of_match[] = {
	{ .compatible = "qcom,ipq9650-tlmm", },
	{},
};

static int ipq9650_tlmm_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &ipq9650_tlmm);
}

static struct platform_driver ipq9650_tlmm_driver = {
	.driver = {
		.name = "ipq9650-tlmm",
		.of_match_table = ipq9650_tlmm_of_match,
	},
	.probe = ipq9650_tlmm_probe,
};

static int __init ipq9650_tlmm_init(void)
{
	return platform_driver_register(&ipq9650_tlmm_driver);
}
arch_initcall(ipq9650_tlmm_init);

static void __exit ipq9650_tlmm_exit(void)
{
	platform_driver_unregister(&ipq9650_tlmm_driver);
}
module_exit(ipq9650_tlmm_exit);

MODULE_DESCRIPTION("QTI IPQ9650 TLMM driver");
MODULE_LICENSE("GPL");
