// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018,2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{					        \
		.grp = PINCTRL_PINGROUP("gpio" #id,     \
			gpio##id##_pins,                \
			ARRAY_SIZE(gpio##id##_pins)),   \
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
		.ctl_reg = REG_SIZE * id,	        \
		.io_reg = 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = 0x8 + REG_SIZE * id,	\
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
		.intr_target_kpss_val = 3,      \
		.intr_raw_status_bit = 4,	\
		.intr_polarity_bit = 1,		\
		.intr_detection_bit = 2,	\
		.intr_detection_width = 2,	\
	}

static const struct pinctrl_pin_desc ipq5424_pins[] = {
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

enum ipq5424_functions {
	msm_mux_atest_char,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_atest_tic,
	msm_mux_audio_pri,
	msm_mux_audio_pri0,
	msm_mux_audio_pri1,
	msm_mux_audio_sec,
	msm_mux_audio_sec0,
	msm_mux_audio_sec1,
	msm_mux_core_voltage,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_cri_trng2,
	msm_mux_cri_trng3,
	msm_mux_cxc_clk,
	msm_mux_cxc_data,
	msm_mux_dbg_out,
	msm_mux_gcc_plltest,
	msm_mux_gcc_tlmm,
	msm_mux_gpio,
	msm_mux_i2c0_scl,
	msm_mux_i2c0_sda,
	msm_mux_i2c1_scl,
	msm_mux_i2c1_sda,
	msm_mux_i2c11,
	msm_mux_mac0,
	msm_mux_mac1,
	msm_mux_mdc_mst,
	msm_mux_mdc_slv,
	msm_mux_mdio_mst,
	msm_mux_mdio_slv,
	msm_mux_pcie0_clk,
	msm_mux_pcie0_wake,
	msm_mux_pcie1_clk,
	msm_mux_pcie1_wake,
	msm_mux_pcie2_clk,
	msm_mux_pcie2_wake,
	msm_mux_pcie3_clk,
	msm_mux_pcie3_wake,
	msm_mux_pll_test,
	msm_mux_prng_rosc0,
	msm_mux_prng_rosc1,
	msm_mux_prng_rosc2,
	msm_mux_prng_rosc3,
	msm_mux_PTA0_0,
	msm_mux_PTA0_1,
	msm_mux_PTA0_2,
	msm_mux_PTA10,
	msm_mux_PTA11,
	msm_mux_pwm0,
	msm_mux_pwm1,
	msm_mux_pwm2,
	msm_mux_qdss_cti_trig_in_a0,
	msm_mux_qdss_cti_trig_out_a0,
	msm_mux_qdss_cti_trig_in_a1,
	msm_mux_qdss_cti_trig_out_a1,
	msm_mux_qdss_cti_trig_in_b0,
	msm_mux_qdss_cti_trig_out_b0,
	msm_mux_qdss_cti_trig_in_b1,
	msm_mux_qdss_cti_trig_out_b1,
	msm_mux_qdss_traceclk_a,
	msm_mux_qdss_tracectl_a,
	msm_mux_qdss_tracedata_a,
	msm_mux_qspi_clk,
	msm_mux_qspi_cs,
	msm_mux_qspi_data,
	msm_mux_resout,
	msm_mux_rx0,
	msm_mux_rx1,
	msm_mux_rx2,
	msm_mux_sdc_clk,
	msm_mux_sdc_cmd,
	msm_mux_sdc_data,
	msm_mux_spi0_clk,
	msm_mux_spi0_cs,
	msm_mux_spi0_miso,
	msm_mux_spi0_mosi,
	msm_mux_spi1,
	msm_mux_spi10,
	msm_mux_spi11,
	msm_mux_tsens_max,
	msm_mux_uart0,
	msm_mux_uart1,
	msm_mux_wci_txd,
	msm_mux_wci_rxd,
	msm_mux_wsi_clk,
	msm_mux_wsi_data,
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
};

static const char * const sdc_data_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};

static const char * const qspi_data_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};

static const char * const pwm2_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};

static const char * const wci_txd_groups[] = {
	"gpio0", "gpio1", "gpio8", "gpio10", "gpio11", "gpio40", "gpio41",
};

static const char * const wci_rxd_groups[] = {
	"gpio0", "gpio1", "gpio8", "gpio10", "gpio11", "gpio40", "gpio41",
};

static const char * const sdc_cmd_groups[] = {
	"gpio4",
};

static const char * const qspi_cs_groups[] = {
	"gpio4",
};

static const char * const qdss_cti_trig_out_a1_groups[] = {
	"gpio27",
};

static const char * const sdc_clk_groups[] = {
	"gpio5",
};

static const char * const qspi_clk_groups[] = {
	"gpio5",
};

static const char * const spi0_clk_groups[] = {
	"gpio6",
};

static const char * const pwm1_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9",
};

static const char * const cri_trng0_groups[] = {
	"gpio6",
};

static const char * const qdss_tracedata_a_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9", "gpio10", "gpio11", "gpio12",
	"gpio13", "gpio14", "gpio15", "gpio20", "gpio21", "gpio36", "gpio37",
	"gpio38", "gpio39",
};

static const char * const spi0_cs_groups[] = {
	"gpio7",
};

static const char * const cri_trng1_groups[] = {
	"gpio7",
};

static const char * const spi0_miso_groups[] = {
	"gpio8",
};

static const char * const cri_trng2_groups[] = {
	"gpio8",
};

static const char * const spi0_mosi_groups[] = {
	"gpio9",
};

static const char * const cri_trng3_groups[] = {
	"gpio9",
};

static const char * const uart0_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13",
};

static const char * const pwm0_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13",
};

static const char * const prng_rosc0_groups[] = {
	"gpio12",
};

static const char * const prng_rosc1_groups[] = {
	"gpio13",
};

static const char * const i2c0_scl_groups[] = {
	"gpio14",
};

static const char * const tsens_max_groups[] = {
	"gpio14",
};

static const char * const prng_rosc2_groups[] = {
	"gpio14",
};

static const char * const i2c0_sda_groups[] = {
	"gpio15",
};

static const char * const prng_rosc3_groups[] = {
	"gpio15",
};

static const char * const core_voltage_groups[] = {
	"gpio16", "gpio17",
};

static const char * const i2c1_scl_groups[] = {
	"gpio16",
};

static const char * const i2c1_sda_groups[] = {
	"gpio17",
};

static const char * const mdc_slv_groups[] = {
	"gpio20",
};

static const char * const atest_char0_groups[] = {
	"gpio20",
};

static const char * const mdio_slv_groups[] = {
	"gpio21",
};

static const char * const atest_char1_groups[] = {
	"gpio21",
};

static const char * const mdc_mst_groups[] = {
	"gpio22",
};

static const char * const atest_char2_groups[] = {
	"gpio22",
};

static const char * const mdio_mst_groups[] = {
	"gpio23",
};

static const char * const atest_char3_groups[] = {
	"gpio23",
};

static const char * const pcie0_clk_groups[] = {
	"gpio24",
};

static const char * const PTA10_groups[] = {
	"gpio24", "gpio26", "gpio27",
};

static const char * const mac0_groups[] = {
	"gpio24", "gpio26",
};

static const char * const atest_char_groups[] = {
	"gpio24",
};

static const char * const pcie0_wake_groups[] = {
	"gpio26",
};

static const char * const pcie1_clk_groups[] = {
	"gpio27",
};

static const char * const i2c11_groups[] = {
	"gpio27", "gpio29",
};

static const char * const pcie1_wake_groups[] = {
	"gpio29",
};

static const char * const pcie2_clk_groups[] = {
	"gpio30",
};

static const char * const mac1_groups[] = {
	"gpio30", "gpio32",
};

static const char * const pcie2_wake_groups[] = {
	"gpio32",
};

static const char * const PTA11_groups[] = {
	"gpio30", "gpio32", "gpio33",
};

static const char * const audio_pri0_groups[] = {
	"gpio32", "gpio32",
};

static const char * const pcie3_clk_groups[] = {
	"gpio33",
};

static const char * const audio_pri1_groups[] = {
	"gpio33", "gpio33",
};

static const char * const pcie3_wake_groups[] = {
	"gpio35",
};

static const char * const audio_sec1_groups[] = {
	"gpio35", "gpio35",
};

static const char * const audio_pri_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};

static const char * const spi1_groups[] = {
	"gpio11", "gpio36", "gpio37", "gpio38", "gpio46",
};

static const char * const audio_sec0_groups[] = {
	"gpio36", "gpio36",
};

static const char * const rx1_groups[] = {
	"gpio38", "gpio46",
};

static const char * const pll_test_groups[] = {
	"gpio38",
};

static const char * const dbg_out_groups[] = {
	"gpio46",
};

static const char * const PTA0_0_groups[] = {
	"gpio40",
};

static const char * const atest_tic_groups[] = {
	"gpio40",
};

static const char * const PTA0_1_groups[] = {
	"gpio41",
};

static const char * const cxc_data_groups[] = {
	"gpio41",
};

static const char * const PTA0_2_groups[] = {
	"gpio42",
};

static const char * const cxc_clk_groups[] = {
	"gpio42",
};

static const char * const uart1_groups[] = {
	"gpio43", "gpio44",
};

static const char * const audio_sec_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48",
};

static const char * const gcc_plltest_groups[] = {
	"gpio43", "gpio45",
};

static const char * const gcc_tlmm_groups[] = {
	"gpio44",
};

static const char * const qdss_cti_trig_out_b1_groups[] = {
	"gpio33",
};

static const char * const rx0_groups[] = {
	"gpio39", "gpio47",
};

static const char * const qdss_traceclk_a_groups[] = {
	"gpio45",
};

static const char * const qdss_tracectl_a_groups[] = {
	"gpio46",
};

static const char * const qdss_cti_trig_out_a0_groups[] = {
	"gpio24",
};

static const char * const qdss_cti_trig_in_a0_groups[] = {
	"gpio26",
};

static const char * const resout_groups[] = {
	"gpio49",
};

static const char * const qdss_cti_trig_in_a1_groups[] = {
	"gpio29",
};

static const char * const qdss_cti_trig_out_b0_groups[] = {
	"gpio30",
};

static const char * const qdss_cti_trig_in_b0_groups[] = {
	"gpio32",
};

static const char * const qdss_cti_trig_in_b1_groups[] = {
	"gpio35",
};

static const char * const spi10_groups[] = {
	"gpio45", "gpio47", "gpio48",
};

static const char * const spi11_groups[] = {
	"gpio10", "gpio12", "gpio13",
};

static const char * const wsi_clk_groups[] = {
	"gpio24", "gpio27",
};

static const char * const wsi_data_groups[] = {
	"gpio26", "gpio29",
};

static const char * const rx2_groups[] = {
	"gpio37", "gpio45",
};

static const struct pinfunction ipq5424_functions[] = {
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_char0),
	MSM_PIN_FUNCTION(atest_char1),
	MSM_PIN_FUNCTION(atest_char2),
	MSM_PIN_FUNCTION(atest_char3),
	MSM_PIN_FUNCTION(atest_tic),
	MSM_PIN_FUNCTION(audio_pri),
	MSM_PIN_FUNCTION(audio_pri0),
	MSM_PIN_FUNCTION(audio_pri1),
	MSM_PIN_FUNCTION(audio_sec),
	MSM_PIN_FUNCTION(audio_sec0),
	MSM_PIN_FUNCTION(audio_sec1),
	MSM_PIN_FUNCTION(core_voltage),
	MSM_PIN_FUNCTION(cri_trng0),
	MSM_PIN_FUNCTION(cri_trng1),
	MSM_PIN_FUNCTION(cri_trng2),
	MSM_PIN_FUNCTION(cri_trng3),
	MSM_PIN_FUNCTION(cxc_clk),
	MSM_PIN_FUNCTION(cxc_data),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(gcc_plltest),
	MSM_PIN_FUNCTION(gcc_tlmm),
	MSM_GPIO_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(i2c0_scl),
	MSM_PIN_FUNCTION(i2c0_sda),
	MSM_PIN_FUNCTION(i2c1_scl),
	MSM_PIN_FUNCTION(i2c1_sda),
	MSM_PIN_FUNCTION(i2c11),
	MSM_PIN_FUNCTION(mac0),
	MSM_PIN_FUNCTION(mac1),
	MSM_PIN_FUNCTION(mdc_mst),
	MSM_PIN_FUNCTION(mdc_slv),
	MSM_PIN_FUNCTION(mdio_mst),
	MSM_PIN_FUNCTION(mdio_slv),
	MSM_PIN_FUNCTION(pcie0_clk),
	MSM_PIN_FUNCTION(pcie0_wake),
	MSM_PIN_FUNCTION(pcie1_clk),
	MSM_PIN_FUNCTION(pcie1_wake),
	MSM_PIN_FUNCTION(pcie2_clk),
	MSM_PIN_FUNCTION(pcie2_wake),
	MSM_PIN_FUNCTION(pcie3_clk),
	MSM_PIN_FUNCTION(pcie3_wake),
	MSM_PIN_FUNCTION(pll_test),
	MSM_PIN_FUNCTION(prng_rosc0),
	MSM_PIN_FUNCTION(prng_rosc1),
	MSM_PIN_FUNCTION(prng_rosc2),
	MSM_PIN_FUNCTION(prng_rosc3),
	MSM_PIN_FUNCTION(PTA0_0),
	MSM_PIN_FUNCTION(PTA0_1),
	MSM_PIN_FUNCTION(PTA0_2),
	MSM_PIN_FUNCTION(PTA10),
	MSM_PIN_FUNCTION(PTA11),
	MSM_PIN_FUNCTION(pwm0),
	MSM_PIN_FUNCTION(pwm1),
	MSM_PIN_FUNCTION(pwm2),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a0),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a1),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b1),
	MSM_PIN_FUNCTION(qdss_traceclk_a),
	MSM_PIN_FUNCTION(qdss_tracectl_a),
	MSM_PIN_FUNCTION(qdss_tracedata_a),
	MSM_PIN_FUNCTION(qspi_clk),
	MSM_PIN_FUNCTION(qspi_cs),
	MSM_PIN_FUNCTION(qspi_data),
	MSM_PIN_FUNCTION(resout),
	MSM_PIN_FUNCTION(rx0),
	MSM_PIN_FUNCTION(rx1),
	MSM_PIN_FUNCTION(rx2),
	MSM_PIN_FUNCTION(sdc_clk),
	MSM_PIN_FUNCTION(sdc_cmd),
	MSM_PIN_FUNCTION(sdc_data),
	MSM_PIN_FUNCTION(spi0_clk),
	MSM_PIN_FUNCTION(spi0_cs),
	MSM_PIN_FUNCTION(spi0_miso),
	MSM_PIN_FUNCTION(spi0_mosi),
	MSM_PIN_FUNCTION(spi1),
	MSM_PIN_FUNCTION(spi10),
	MSM_PIN_FUNCTION(spi11),
	MSM_PIN_FUNCTION(tsens_max),
	MSM_PIN_FUNCTION(uart0),
	MSM_PIN_FUNCTION(uart1),
	MSM_PIN_FUNCTION(wci_txd),
	MSM_PIN_FUNCTION(wci_rxd),
	MSM_PIN_FUNCTION(wsi_clk),
	MSM_PIN_FUNCTION(wsi_data),
};

static const struct msm_pingroup ipq5424_groups[] = {
	PINGROUP(0, sdc_data, qspi_data, pwm2, wci_txd, wci_rxd, _, _, _, _),
	PINGROUP(1, sdc_data, qspi_data, pwm2, wci_txd, wci_rxd, _, _, _, _),
	PINGROUP(2, sdc_data, qspi_data, pwm2, _, _, _, _, _, _),
	PINGROUP(3, sdc_data, qspi_data, pwm2, _, _, _, _, _, _),
	PINGROUP(4, sdc_cmd, qspi_cs, _, _, _, _, _, _, _),
	PINGROUP(5, sdc_clk, qspi_clk, _, _, _, _, _, _, _),
	PINGROUP(6, spi0_clk, pwm1, _, cri_trng0, qdss_tracedata_a, _, _, _, _),
	PINGROUP(7, spi0_cs, pwm1, _, cri_trng1, qdss_tracedata_a, _, _, _, _),
	PINGROUP(8, spi0_miso, pwm1, wci_txd, wci_rxd, _, cri_trng2, qdss_tracedata_a, _, _),
	PINGROUP(9, spi0_mosi, pwm1, _, cri_trng3, qdss_tracedata_a, _, _, _, _),
	PINGROUP(10, uart0, pwm0, spi11, _, wci_txd, wci_rxd, _, qdss_tracedata_a, _),
	PINGROUP(11, uart0, pwm0, spi1, _, wci_txd, wci_rxd, _, qdss_tracedata_a, _),
	PINGROUP(12, uart0, pwm0, spi11, _, prng_rosc0, qdss_tracedata_a, _, _, _),
	PINGROUP(13, uart0, pwm0, spi11, _, prng_rosc1, qdss_tracedata_a, _, _, _),
	PINGROUP(14, i2c0_scl, tsens_max, _, prng_rosc2, qdss_tracedata_a, _, _, _, _),
	PINGROUP(15, i2c0_sda, _, prng_rosc3, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(16, core_voltage, i2c1_scl, _, _, _, _, _, _, _),
	PINGROUP(17, core_voltage, i2c1_sda, _, _, _, _, _, _, _),
	PINGROUP(18, _, _, _, _, _, _, _, _, _),
	PINGROUP(19, _, _, _, _, _, _, _, _, _),
	PINGROUP(20, mdc_slv, atest_char0, _, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(21, mdio_slv, atest_char1, _, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(22, mdc_mst, atest_char2, _, _, _, _, _, _, _),
	PINGROUP(23, mdio_mst, atest_char3, _, _, _, _, _, _, _),
	PINGROUP(24, pcie0_clk, PTA10, mac0, _, wsi_clk, _, atest_char, qdss_cti_trig_out_a0, _),
	PINGROUP(25, _, _, _, _, _, _, _, _, _),
	PINGROUP(26, pcie0_wake, PTA10, mac0, _, wsi_data, _, qdss_cti_trig_in_a0, _, _),
	PINGROUP(27, pcie1_clk, i2c11, PTA10, wsi_clk, qdss_cti_trig_out_a1, _, _, _, _),
	PINGROUP(28, _, _, _, _, _, _, _, _, _),
	PINGROUP(29, pcie1_wake, i2c11, wsi_data, qdss_cti_trig_in_a1, _, _, _, _, _),
	PINGROUP(30, pcie2_clk, PTA11, mac1, qdss_cti_trig_out_b0, _, _, _, _, _),
	PINGROUP(31, _, _, _, _, _, _, _, _, _),
	PINGROUP(32, pcie2_wake, PTA11, mac1, audio_pri0, audio_pri0, qdss_cti_trig_in_b0, _, _, _),
	PINGROUP(33, pcie3_clk, PTA11, audio_pri1, audio_pri1, qdss_cti_trig_out_b1, _, _, _, _),
	PINGROUP(34, _, _, _, _, _, _, _, _, _),
	PINGROUP(35, pcie3_wake, audio_sec1, audio_sec1, qdss_cti_trig_in_b1, _, _, _, _, _),
	PINGROUP(36, audio_pri, spi1, audio_sec0, audio_sec0, qdss_tracedata_a, _, _, _, _),
	PINGROUP(37, audio_pri, spi1, rx2, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(38, audio_pri, spi1, pll_test, rx1, qdss_tracedata_a, _, _, _, _),
	PINGROUP(39, audio_pri, rx0, _, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(40, PTA0_0, wci_txd, wci_rxd, _, atest_tic, _, _, _, _),
	PINGROUP(41, PTA0_1, wci_txd, wci_rxd, cxc_data, _, _, _, _, _),
	PINGROUP(42, PTA0_2, cxc_clk, _, _, _, _, _, _, _),
	PINGROUP(43, uart1, gcc_plltest, _, _, _, _, _, _, _),
	PINGROUP(44, uart1, gcc_tlmm, _, _, _, _, _, _, _),
	PINGROUP(45, spi10, rx2, audio_sec, gcc_plltest, _, qdss_traceclk_a, _, _, _),
	PINGROUP(46, spi1, rx1, audio_sec, dbg_out, qdss_tracectl_a, _, _, _, _),
	PINGROUP(47, spi10, rx0, audio_sec, _, _, _, _, _, _),
	PINGROUP(48, spi10, audio_sec, _, _, _, _, _, _, _),
	PINGROUP(49, resout, _, _, _, _, _, _, _, _),
};

static const struct msm_pinctrl_soc_data ipq5424_pinctrl = {
	.pins = ipq5424_pins,
	.npins = ARRAY_SIZE(ipq5424_pins),
	.functions = ipq5424_functions,
	.nfunctions = ARRAY_SIZE(ipq5424_functions),
	.groups = ipq5424_groups,
	.ngroups = ARRAY_SIZE(ipq5424_groups),
	.ngpios = 50,
};

static int ipq5424_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &ipq5424_pinctrl);
}

static const struct of_device_id ipq5424_pinctrl_of_match[] = {
	{ .compatible = "qcom,ipq5424-tlmm", },
	{ },
};
MODULE_DEVICE_TABLE(of, ipq5424_pinctrl_of_match);

static struct platform_driver ipq5424_pinctrl_driver = {
	.driver = {
		.name = "ipq5424-tlmm",
		.of_match_table = ipq5424_pinctrl_of_match,
	},
	.probe = ipq5424_pinctrl_probe,
};

static int __init ipq5424_pinctrl_init(void)
{
	return platform_driver_register(&ipq5424_pinctrl_driver);
}
arch_initcall(ipq5424_pinctrl_init);

static void __exit ipq5424_pinctrl_exit(void)
{
	platform_driver_unregister(&ipq5424_pinctrl_driver);
}
module_exit(ipq5424_pinctrl_exit);

MODULE_DESCRIPTION("QTI IPQ5424 TLMM driver");
MODULE_LICENSE("GPL");
