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

static const struct pinctrl_pin_desc ipq5210_pins[] = {
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

enum ipq5210_functions {
	msm_mux_atest_char_start,
	msm_mux_atest_char_status0,
	msm_mux_atest_char_status1,
	msm_mux_atest_char_status2,
	msm_mux_atest_char_status3,
	msm_mux_atest_tic_en,
	msm_mux_audio_pri,
	msm_mux_audio_pri_mclk_out0,
	msm_mux_audio_pri_mclk_in0,
	msm_mux_audio_pri_mclk_out1,
	msm_mux_audio_pri_mclk_in1,
	msm_mux_audio_pri_mclk_out2,
	msm_mux_audio_pri_mclk_in2,
	msm_mux_audio_pri_mclk_out3,
	msm_mux_audio_pri_mclk_in3,
	msm_mux_audio_sec,
	msm_mux_audio_sec_mclk_out0,
	msm_mux_audio_sec_mclk_in0,
	msm_mux_audio_sec_mclk_out1,
	msm_mux_audio_sec_mclk_in1,
	msm_mux_audio_sec_mclk_out2,
	msm_mux_audio_sec_mclk_in2,
	msm_mux_audio_sec_mclk_out3,
	msm_mux_audio_sec_mclk_in3,
	msm_mux_core_voltage_0,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_cri_trng2,
	msm_mux_cri_trng3,
	msm_mux_dbg_out_clk,
	msm_mux_dg_out,
	msm_mux_gcc_plltest_bypassnl,
	msm_mux_gcc_plltest_resetn,
	msm_mux_gcc_tlmm,
	msm_mux_gpio,
	msm_mux_led0,
	msm_mux_led1,
	msm_mux_led2,
	msm_mux_mdc_mst,
	msm_mux_mdc_slv0,
	msm_mux_mdc_slv1,
	msm_mux_mdc_slv2,
	msm_mux_mdio_mst,
	msm_mux_mdio_slv0,
	msm_mux_mdio_slv1,
	msm_mux_mdio_slv2,
	msm_mux_mux_tod_out,
	msm_mux_pcie0_clk_req_n,
	msm_mux_pcie0_wake,
	msm_mux_pcie1_clk_req_n,
	msm_mux_pcie1_wake,
	msm_mux_pll_test,
	msm_mux_pon_active_led,
	msm_mux_pon_mux_sel,
	msm_mux_pon_rx,
	msm_mux_pon_rx_los,
	msm_mux_pon_tx,
	msm_mux_pon_tx_burst,
	msm_mux_pon_tx_dis,
	msm_mux_pon_tx_fault,
	msm_mux_pon_tx_sd,
	msm_mux_gpn_rx_los,
	msm_mux_gpn_tx_burst,
	msm_mux_gpn_tx_dis,
	msm_mux_gpn_tx_fault,
	msm_mux_gpn_tx_sd,
	msm_mux_pps,
	msm_mux_pwm0,
	msm_mux_pwm1,
	msm_mux_pwm2,
	msm_mux_pwm3,
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
	msm_mux_qrng_rosc0,
	msm_mux_qrng_rosc1,
	msm_mux_qrng_rosc2,
	msm_mux_qspi_data,
	msm_mux_qspi_clk,
	msm_mux_qspi_cs_n,
	msm_mux_qup_se0,
	msm_mux_qup_se1,
	msm_mux_qup_se2,
	msm_mux_qup_se3,
	msm_mux_qup_se4,
	msm_mux_qup_se5,
	msm_mux_qup_se5_l1,
	msm_mux_resout,
	msm_mux_rx_los0,
	msm_mux_rx_los1,
	msm_mux_rx_los2,
	msm_mux_sdc_clk,
	msm_mux_sdc_cmd,
	msm_mux_sdc_data,
	msm_mux_tsens_max,
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
	"gpio46",
};

static const char *const atest_char_status0_groups[] = {
	"gpio34",
};

static const char *const atest_char_status1_groups[] = {
	"gpio35",
};

static const char *const atest_char_status2_groups[] = {
	"gpio36",
};

static const char *const atest_char_status3_groups[] = {
	"gpio37",
};

static const char *const atest_tic_en_groups[] = {
	"gpio42",
};

static const char *const audio_pri_groups[] = {
	"gpio34", "gpio35", "gpio36", "gpio37",
};

static const char *const audio_pri_mclk_out0_groups[] = {
	"gpio12",
};

static const char *const audio_pri_mclk_in0_groups[] = {
	"gpio12",
};

static const char *const audio_pri_mclk_out1_groups[] = {
	"gpio19",
};

static const char *const audio_pri_mclk_in1_groups[] = {
	"gpio19",
};

static const char *const audio_pri_mclk_out2_groups[] = {
	"gpio8",
};

static const char *const audio_pri_mclk_in2_groups[] = {
	"gpio8",
};

static const char *const audio_pri_mclk_out3_groups[] = {
	"gpio13",
};

static const char *const audio_pri_mclk_in3_groups[] = {
	"gpio13",
};

static const char *const audio_sec_mclk_out0_groups[] = {
	"gpio17",
};

static const char *const audio_sec_mclk_in0_groups[] = {
	"gpio17",
};

static const char *const audio_sec_mclk_out1_groups[] = {
	"gpio16",
};

static const char *const audio_sec_mclk_in1_groups[] = {
	"gpio16",
};

static const char *const audio_sec_mclk_out2_groups[] = {
	"gpio49",
};

static const char *const audio_sec_mclk_in2_groups[] = {
	"gpio49",
};

static const char *const audio_sec_mclk_out3_groups[] = {
	"gpio50",
};

static const char *const audio_sec_mclk_in3_groups[] = {
	"gpio50",
};

static const char *const audio_sec_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43",
};

static const char *const core_voltage_0_groups[] = {
	"gpio22",
};

static const char *const cri_trng0_groups[] = {
	"gpio6",
};

static const char *const cri_trng1_groups[] = {
	"gpio7",
};

static const char *const cri_trng2_groups[] = {
	"gpio8",
};

static const char *const cri_trng3_groups[] = {
	"gpio9",
};

static const char *const dbg_out_clk_groups[] = {
	"gpio23",
};

static const char *const dg_out_groups[] = {
	"gpio46",
};

static const char *const gcc_plltest_bypassnl_groups[] = {
	"gpio38",
};

static const char *const gcc_plltest_resetn_groups[] = {
	"gpio40",
};

static const char *const gcc_tlmm_groups[] = {
	"gpio39",
};

static const char *const led0_groups[] = {
	"gpio6", "gpio23", "gpio39",
};

static const char *const led1_groups[] = {
	"gpio7", "gpio27", "gpio39",
};

static const char *const led2_groups[] = {
	"gpio9", "gpio26", "gpio38",
};

static const char *const mdc_mst_groups[] = {
	"gpio26",
};

static const char *const mdc_slv0_groups[] = {
	"gpio31",
};

static const char *const mdc_slv1_groups[] = {
	"gpio20",
};

static const char *const mdc_slv2_groups[] = {
	"gpio47",
};

static const char *const mdio_mst_groups[] = {
	"gpio27",
};

static const char *const mdio_slv0_groups[] = {
	"gpio33",
};

static const char *const mdio_slv1_groups[] = {
	"gpio21",
};

static const char *const mdio_slv2_groups[] = {
	"gpio49",
};

static const char *const mux_tod_out_groups[] = {
	"gpio19",
};

static const char *const pcie0_clk_req_n_groups[] = {
	"gpio31",
};

static const char *const pcie0_wake_groups[] = {
	"gpio33",
};

static const char *const pcie1_clk_req_n_groups[] = {
	"gpio28",
};

static const char *const pcie1_wake_groups[] = {
	"gpio30",
};

static const char *const pll_test_groups[] = {
	"gpio18",
};

static const char *const pon_active_led_groups[] = {
	"gpio11",
};

static const char *const pon_mux_sel_groups[] = {
	"gpio45",
};

static const char *const pon_rx_groups[] = {
	"gpio48",
};

static const char *const pon_rx_los_groups[] = {
	"gpio10",
};

static const char *const pon_tx_groups[] = {
	"gpio15",
};

static const char *const pon_tx_burst_groups[] = {
	"gpio14",
};

static const char *const pon_tx_dis_groups[] = {
	"gpio12",
};

static const char *const pon_tx_fault_groups[] = {
	"gpio17",
};

static const char *const pon_tx_sd_groups[] = {
	"gpio16",
};

static const char *const gpn_rx_los_groups[] = {
	"gpio47",
};

static const char *const gpn_tx_burst_groups[] = {
	"gpio51",
};

static const char *const gpn_tx_dis_groups[] = {
	"gpio13",
};

static const char *const gpn_tx_fault_groups[] = {
	"gpio49",
};

static const char *const gpn_tx_sd_groups[] = {
	"gpio50",
};

static const char *const pps_groups[] = {
	"gpio18",
};

static const char *const pwm0_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13",
};

static const char *const pwm1_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9",
};

static const char *const pwm2_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};

static const char *const pwm3_groups[] = {
	"gpio22",
};

static const char *const qdss_cti_trig_in_a0_groups[] = {
	"gpio30",
};

static const char *const qdss_cti_trig_in_a1_groups[] = {
	"gpio33",
};

static const char *const qdss_cti_trig_in_b0_groups[] = {
	"gpio34",
};

static const char *const qdss_cti_trig_in_b1_groups[] = {
	"gpio37",
};

static const char *const qdss_cti_trig_out_a0_groups[] = {
	"gpio28",
};

static const char *const qdss_cti_trig_out_a1_groups[] = {
	"gpio31",
};

static const char *const qdss_cti_trig_out_b0_groups[] = {
	"gpio16",
};

static const char *const qdss_cti_trig_out_b1_groups[] = {
	"gpio35",
};

static const char *const qdss_traceclk_a_groups[] = {
	"gpio23",
};

static const char *const qdss_tracectl_a_groups[] = {
	"gpio26",
};

static const char *const qdss_tracedata_a_groups[] = {
	"gpio6",  "gpio7",  "gpio8",  "gpio9",	"gpio10", "gpio11",
	"gpio12", "gpio13", "gpio14", "gpio15", "gpio20", "gpio21",
	"gpio38", "gpio39", "gpio40", "gpio41",
};

static const char *const qrng_rosc0_groups[] = {
	"gpio12",
};

static const char *const qrng_rosc1_groups[] = {
	"gpio13",
};

static const char *const qrng_rosc2_groups[] = {
	"gpio14",
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
	"gpio6", "gpio7", "gpio8", "gpio9", "gpio14", "gpio15",
};

static const char *const qup_se1_groups[] = {
	"gpio28", "gpio30", "gpio38", "gpio39",
};

static const char *const qup_se2_groups[] = {
	"gpio12", "gpio13", "gpio20", "gpio21", "gpio52", "gpio53",
};

static const char *const qup_se3_groups[] = {
	"gpio10", "gpio11", "gpio22", "gpio23",
};

static const char *const qup_se4_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43", "gpio52", "gpio53",
};

static const char *const qup_se5_groups[] = {
	"gpio47", "gpio48", "gpio49", "gpio50", "gpio51", "gpio52",
};

static const char *const qup_se5_l1_groups[] = {
	"gpio52", "gpio53",
};

static const char *const resout_groups[] = {
	"gpio44",
};

static const char *const rx_los0_groups[] = {
	"gpio37", "gpio42",
};

static const char *const rx_los1_groups[] = {
	"gpio36", "gpio41",
};

static const char *const rx_los2_groups[] = {
	"gpio35", "gpio40",
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
	"gpio20",
};

static const struct pinfunction ipq5210_functions[] = {
	MSM_PIN_FUNCTION(atest_char_start),
	MSM_PIN_FUNCTION(atest_char_status0),
	MSM_PIN_FUNCTION(atest_char_status1),
	MSM_PIN_FUNCTION(atest_char_status2),
	MSM_PIN_FUNCTION(atest_char_status3),
	MSM_PIN_FUNCTION(atest_tic_en),
	MSM_PIN_FUNCTION(audio_pri),
	MSM_PIN_FUNCTION(audio_pri_mclk_out0),
	MSM_PIN_FUNCTION(audio_pri_mclk_in0),
	MSM_PIN_FUNCTION(audio_pri_mclk_out1),
	MSM_PIN_FUNCTION(audio_pri_mclk_in1),
	MSM_PIN_FUNCTION(audio_pri_mclk_out2),
	MSM_PIN_FUNCTION(audio_pri_mclk_in2),
	MSM_PIN_FUNCTION(audio_pri_mclk_out3),
	MSM_PIN_FUNCTION(audio_pri_mclk_in3),
	MSM_PIN_FUNCTION(audio_sec),
	MSM_PIN_FUNCTION(audio_sec_mclk_out0),
	MSM_PIN_FUNCTION(audio_sec_mclk_in0),
	MSM_PIN_FUNCTION(audio_sec_mclk_out1),
	MSM_PIN_FUNCTION(audio_sec_mclk_in1),
	MSM_PIN_FUNCTION(audio_sec_mclk_out2),
	MSM_PIN_FUNCTION(audio_sec_mclk_in2),
	MSM_PIN_FUNCTION(audio_sec_mclk_out3),
	MSM_PIN_FUNCTION(audio_sec_mclk_in3),
	MSM_PIN_FUNCTION(core_voltage_0),
	MSM_PIN_FUNCTION(cri_trng0),
	MSM_PIN_FUNCTION(cri_trng1),
	MSM_PIN_FUNCTION(cri_trng2),
	MSM_PIN_FUNCTION(cri_trng3),
	MSM_PIN_FUNCTION(dbg_out_clk),
	MSM_PIN_FUNCTION(dg_out),
	MSM_PIN_FUNCTION(gcc_plltest_bypassnl),
	MSM_PIN_FUNCTION(gcc_plltest_resetn),
	MSM_PIN_FUNCTION(gcc_tlmm),
	MSM_GPIO_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(led0),
	MSM_PIN_FUNCTION(led1),
	MSM_PIN_FUNCTION(led2),
	MSM_PIN_FUNCTION(mdc_mst),
	MSM_PIN_FUNCTION(mdc_slv0),
	MSM_PIN_FUNCTION(mdc_slv1),
	MSM_PIN_FUNCTION(mdc_slv2),
	MSM_PIN_FUNCTION(mdio_mst),
	MSM_PIN_FUNCTION(mdio_slv0),
	MSM_PIN_FUNCTION(mdio_slv1),
	MSM_PIN_FUNCTION(mdio_slv2),
	MSM_PIN_FUNCTION(mux_tod_out),
	MSM_PIN_FUNCTION(pcie0_clk_req_n),
	MSM_PIN_FUNCTION(pcie0_wake),
	MSM_PIN_FUNCTION(pcie1_clk_req_n),
	MSM_PIN_FUNCTION(pcie1_wake),
	MSM_PIN_FUNCTION(pll_test),
	MSM_PIN_FUNCTION(pon_active_led),
	MSM_PIN_FUNCTION(pon_mux_sel),
	MSM_PIN_FUNCTION(pon_rx),
	MSM_PIN_FUNCTION(pon_rx_los),
	MSM_PIN_FUNCTION(pon_tx),
	MSM_PIN_FUNCTION(pon_tx_burst),
	MSM_PIN_FUNCTION(pon_tx_dis),
	MSM_PIN_FUNCTION(pon_tx_fault),
	MSM_PIN_FUNCTION(pon_tx_sd),
	MSM_PIN_FUNCTION(gpn_rx_los),
	MSM_PIN_FUNCTION(gpn_tx_burst),
	MSM_PIN_FUNCTION(gpn_tx_dis),
	MSM_PIN_FUNCTION(gpn_tx_fault),
	MSM_PIN_FUNCTION(gpn_tx_sd),
	MSM_PIN_FUNCTION(pps),
	MSM_PIN_FUNCTION(pwm0),
	MSM_PIN_FUNCTION(pwm1),
	MSM_PIN_FUNCTION(pwm2),
	MSM_PIN_FUNCTION(pwm3),
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
	MSM_PIN_FUNCTION(qrng_rosc0),
	MSM_PIN_FUNCTION(qrng_rosc1),
	MSM_PIN_FUNCTION(qrng_rosc2),
	MSM_PIN_FUNCTION(qspi_data),
	MSM_PIN_FUNCTION(qspi_clk),
	MSM_PIN_FUNCTION(qspi_cs_n),
	MSM_PIN_FUNCTION(qup_se0),
	MSM_PIN_FUNCTION(qup_se1),
	MSM_PIN_FUNCTION(qup_se2),
	MSM_PIN_FUNCTION(qup_se3),
	MSM_PIN_FUNCTION(qup_se4),
	MSM_PIN_FUNCTION(qup_se5),
	MSM_PIN_FUNCTION(qup_se5_l1),
	MSM_PIN_FUNCTION(resout),
	MSM_PIN_FUNCTION(rx_los0),
	MSM_PIN_FUNCTION(rx_los1),
	MSM_PIN_FUNCTION(rx_los2),
	MSM_PIN_FUNCTION(sdc_clk),
	MSM_PIN_FUNCTION(sdc_cmd),
	MSM_PIN_FUNCTION(sdc_data),
	MSM_PIN_FUNCTION(tsens_max),
};

static const struct msm_pingroup ipq5210_groups[] = {
	[0] = PINGROUP(0, sdc_data, qspi_data, pwm2, _, _, _, _, _, _),
	[1] = PINGROUP(1, sdc_data, qspi_data, pwm2, _, _, _, _, _, _),
	[2] = PINGROUP(2, sdc_data, qspi_data, pwm2, _, _, _, _, _, _),
	[3] = PINGROUP(3, sdc_data, qspi_data, pwm2, _, _, _, _, _, _),
	[4] = PINGROUP(4, sdc_cmd, qspi_cs_n, _, _, _, _, _, _, _),
	[5] = PINGROUP(5, sdc_clk, qspi_clk, _, _, _, _, _, _, _),
	[6] = PINGROUP(6, qup_se0, led0, pwm1, _, cri_trng0, qdss_tracedata_a, _, _, _),
	[7] = PINGROUP(7, qup_se0, led1, pwm1, _, cri_trng1, qdss_tracedata_a, _, _, _),
	[8] = PINGROUP(8, qup_se0, pwm1, audio_pri_mclk_out2, audio_pri_mclk_in2, _, cri_trng2, qdss_tracedata_a, _, _),
	[9] = PINGROUP(9, qup_se0, led2, pwm1, _, cri_trng3, qdss_tracedata_a, _, _, _),
	[10] = PINGROUP(10, pon_rx_los, qup_se3, pwm0, _, _, qdss_tracedata_a, _, _, _),
	[11] = PINGROUP(11, pon_active_led, qup_se3, pwm0, _, _, qdss_tracedata_a, _, _, _),
	[12] = PINGROUP(12, pon_tx_dis, qup_se2, pwm0, audio_pri_mclk_out0, audio_pri_mclk_in0, _, qrng_rosc0, qdss_tracedata_a, _),
	[13] = PINGROUP(13, gpn_tx_dis, qup_se2, pwm0, audio_pri_mclk_out3, audio_pri_mclk_in3, _, qrng_rosc1, qdss_tracedata_a, _),
	[14] = PINGROUP(14, pon_tx_burst, qup_se0, _, qrng_rosc2, qdss_tracedata_a, _, _, _, _),
	[15] = PINGROUP(15, pon_tx, qup_se0, _, qdss_tracedata_a, _, _, _, _, _),
	[16] = PINGROUP(16, pon_tx_sd, audio_sec_mclk_out1, audio_sec_mclk_in1, qdss_cti_trig_out_b0, _, _, _, _, _),
	[17] = PINGROUP(17, pon_tx_fault, audio_sec_mclk_out0, audio_sec_mclk_in0, _, _, _, _, _, _),
	[18] = PINGROUP(18, pps, pll_test, _, _, _, _, _, _, _),
	[19] = PINGROUP(19, mux_tod_out, audio_pri_mclk_out1, audio_pri_mclk_in1, _, _, _, _, _, _),
	[20] = PINGROUP(20, qup_se2, mdc_slv1, tsens_max, qdss_tracedata_a, _, _, _, _, _),
	[21] = PINGROUP(21, qup_se2, mdio_slv1, qdss_tracedata_a, _, _, _, _, _, _),
	[22] = PINGROUP(22, core_voltage_0, qup_se3, pwm3, _, _, _, _, _, _),
	[23] = PINGROUP(23, led0, qup_se3, dbg_out_clk, qdss_traceclk_a, _, _, _, _, _),
	[24] = PINGROUP(24, _, _, _, _, _, _, _, _, _),
	[25] = PINGROUP(25, _, _, _, _, _, _, _, _, _),
	[26] = PINGROUP(26, mdc_mst, led2, _, qdss_tracectl_a, _, _, _, _, _),
	[27] = PINGROUP(27, mdio_mst, led1, _, _, _, _, _, _, _),
	[28] = PINGROUP(28, pcie1_clk_req_n, qup_se1, _, _, qdss_cti_trig_out_a0, _, _, _, _),
	[29] = PINGROUP(29, _, _, _, _, _, _, _, _, _),
	[30] = PINGROUP(30, pcie1_wake, qup_se1, _, _, qdss_cti_trig_in_a0, _, _, _, _),
	[31] = PINGROUP(31, pcie0_clk_req_n, mdc_slv0, _, qdss_cti_trig_out_a1, _, _, _, _, _),
	[32] = PINGROUP(32, _, _, _, _, _, _, _, _, _),
	[33] = PINGROUP(33, pcie0_wake, mdio_slv0, qdss_cti_trig_in_a1, _, _, _, _, _, _),
	[34] = PINGROUP(34, audio_pri, atest_char_status0, qdss_cti_trig_in_b0, _, _, _, _, _, _),
	[35] = PINGROUP(35, audio_pri, rx_los2, atest_char_status1, qdss_cti_trig_out_b1, _, _, _, _, _),
	[36] = PINGROUP(36, audio_pri, _, rx_los1, atest_char_status2, _, _, _, _, _),
	[37] = PINGROUP(37, audio_pri, rx_los0, atest_char_status3, _, qdss_cti_trig_in_b1, _, _, _, _),
	[38] = PINGROUP(38, qup_se1, led2, gcc_plltest_bypassnl, qdss_tracedata_a, _, _, _, _, _),
	[39] = PINGROUP(39, qup_se1, led1, led0, gcc_tlmm, qdss_tracedata_a, _, _, _, _),
	[40] = PINGROUP(40, qup_se4, rx_los2, audio_sec, gcc_plltest_resetn, qdss_tracedata_a, _, _, _, _),
	[41] = PINGROUP(41, qup_se4, rx_los1, audio_sec, qdss_tracedata_a, _, _, _, _, _),
	[42] = PINGROUP(42, qup_se4, rx_los0, audio_sec, atest_tic_en, _, _, _, _, _),
	[43] = PINGROUP(43, qup_se4, audio_sec, _, _, _, _, _, _, _),
	[44] = PINGROUP(44, resout, _, _, _, _, _, _, _, _),
	[45] = PINGROUP(45, pon_mux_sel, _, _, _, _, _, _, _, _),
	[46] = PINGROUP(46, dg_out, atest_char_start, _, _, _, _, _, _, _),
	[47] = PINGROUP(47, gpn_rx_los, mdc_slv2, qup_se5, _, _, _, _, _, _),
	[48] = PINGROUP(48, pon_rx, qup_se5, _, _, _, _, _, _, _),
	[49] = PINGROUP(49, gpn_tx_fault, mdio_slv2, qup_se5, audio_sec_mclk_out2, audio_sec_mclk_in2, _, _, _, _),
	[50] = PINGROUP(50, gpn_tx_sd, qup_se5, audio_sec_mclk_out3, audio_sec_mclk_in3, _, _, _, _, _),
	[51] = PINGROUP(51, gpn_tx_burst, qup_se5, _, _, _, _, _, _, _),
	[52] = PINGROUP(52, qup_se2, qup_se5, qup_se4, qup_se5_l1, _, _, _, _, _),
	[53] = PINGROUP(53, qup_se2, qup_se4, qup_se5_l1, _, _, _, _, _, _),
};

static const struct msm_pinctrl_soc_data ipq5210_tlmm = {
	.pins = ipq5210_pins,
	.npins = ARRAY_SIZE(ipq5210_pins),
	.functions = ipq5210_functions,
	.nfunctions = ARRAY_SIZE(ipq5210_functions),
	.groups = ipq5210_groups,
	.ngroups = ARRAY_SIZE(ipq5210_groups),
	.ngpios = 54,
};

static const struct of_device_id ipq5210_tlmm_of_match[] = {
	{ .compatible = "qcom,ipq5210-tlmm", },
	{ },
};

static int ipq5210_tlmm_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &ipq5210_tlmm);
}

static struct platform_driver ipq5210_tlmm_driver = {
	.driver = {
		.name = "ipq5210-tlmm",
		.of_match_table = ipq5210_tlmm_of_match,
	},
	.probe = ipq5210_tlmm_probe,
};

static int __init ipq5210_tlmm_init(void)
{
	return platform_driver_register(&ipq5210_tlmm_driver);
}
arch_initcall(ipq5210_tlmm_init);

static void __exit ipq5210_tlmm_exit(void)
{
	platform_driver_unregister(&ipq5210_tlmm_driver);
}
module_exit(ipq5210_tlmm_exit);

MODULE_DESCRIPTION("QTI IPQ5210 TLMM driver");
MODULE_LICENSE("GPL");
