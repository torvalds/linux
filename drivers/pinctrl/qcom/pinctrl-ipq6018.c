// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{					        \
		.grp = PINCTRL_PINGROUP("gpio" #id, 	\
			gpio##id##_pins, 		\
			ARRAY_SIZE(gpio##id##_pins)),	\
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

static const struct pinctrl_pin_desc ipq6018_pins[] = {
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
	PINCTRL_PIN(65, "GPIO_65"),
	PINCTRL_PIN(66, "GPIO_66"),
	PINCTRL_PIN(67, "GPIO_67"),
	PINCTRL_PIN(68, "GPIO_68"),
	PINCTRL_PIN(69, "GPIO_69"),
	PINCTRL_PIN(70, "GPIO_70"),
	PINCTRL_PIN(71, "GPIO_71"),
	PINCTRL_PIN(72, "GPIO_72"),
	PINCTRL_PIN(73, "GPIO_73"),
	PINCTRL_PIN(74, "GPIO_74"),
	PINCTRL_PIN(75, "GPIO_75"),
	PINCTRL_PIN(76, "GPIO_76"),
	PINCTRL_PIN(77, "GPIO_77"),
	PINCTRL_PIN(78, "GPIO_78"),
	PINCTRL_PIN(79, "GPIO_79"),
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
DECLARE_MSM_GPIO_PINS(65);
DECLARE_MSM_GPIO_PINS(66);
DECLARE_MSM_GPIO_PINS(67);
DECLARE_MSM_GPIO_PINS(68);
DECLARE_MSM_GPIO_PINS(69);
DECLARE_MSM_GPIO_PINS(70);
DECLARE_MSM_GPIO_PINS(71);
DECLARE_MSM_GPIO_PINS(72);
DECLARE_MSM_GPIO_PINS(73);
DECLARE_MSM_GPIO_PINS(74);
DECLARE_MSM_GPIO_PINS(75);
DECLARE_MSM_GPIO_PINS(76);
DECLARE_MSM_GPIO_PINS(77);
DECLARE_MSM_GPIO_PINS(78);
DECLARE_MSM_GPIO_PINS(79);

enum ipq6018_functions {
	msm_mux_atest_char,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_audio0,
	msm_mux_audio1,
	msm_mux_audio2,
	msm_mux_audio3,
	msm_mux_audio_rxbclk,
	msm_mux_audio_rxfsync,
	msm_mux_audio_rxmclk,
	msm_mux_audio_rxmclkin,
	msm_mux_audio_txbclk,
	msm_mux_audio_txfsync,
	msm_mux_audio_txmclk,
	msm_mux_audio_txmclkin,
	msm_mux_blsp0_i2c,
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
	msm_mux_burn0,
	msm_mux_burn1,
	msm_mux_cri_trng,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_cxc0,
	msm_mux_cxc1,
	msm_mux_dbg_out,
	msm_mux_gcc_plltest,
	msm_mux_gcc_tlmm,
	msm_mux_gpio,
	msm_mux_lpass_aud,
	msm_mux_lpass_aud0,
	msm_mux_lpass_aud1,
	msm_mux_lpass_aud2,
	msm_mux_lpass_pcm,
	msm_mux_lpass_pdm,
	msm_mux_mac00,
	msm_mux_mac01,
	msm_mux_mac10,
	msm_mux_mac11,
	msm_mux_mac12,
	msm_mux_mac13,
	msm_mux_mac20,
	msm_mux_mac21,
	msm_mux_mdc,
	msm_mux_mdio,
	msm_mux_pcie0_clk,
	msm_mux_pcie0_rst,
	msm_mux_pcie0_wake,
	msm_mux_prng_rosc,
	msm_mux_pta1_0,
	msm_mux_pta1_1,
	msm_mux_pta1_2,
	msm_mux_pta2_0,
	msm_mux_pta2_1,
	msm_mux_pta2_2,
	msm_mux_pwm00,
	msm_mux_pwm01,
	msm_mux_pwm02,
	msm_mux_pwm03,
	msm_mux_pwm04,
	msm_mux_pwm10,
	msm_mux_pwm11,
	msm_mux_pwm12,
	msm_mux_pwm13,
	msm_mux_pwm14,
	msm_mux_pwm20,
	msm_mux_pwm21,
	msm_mux_pwm22,
	msm_mux_pwm23,
	msm_mux_pwm24,
	msm_mux_pwm30,
	msm_mux_pwm31,
	msm_mux_pwm32,
	msm_mux_pwm33,
	msm_mux_qdss_cti_trig_in_a0,
	msm_mux_qdss_cti_trig_in_a1,
	msm_mux_qdss_cti_trig_out_a0,
	msm_mux_qdss_cti_trig_out_a1,
	msm_mux_qdss_cti_trig_in_b0,
	msm_mux_qdss_cti_trig_in_b1,
	msm_mux_qdss_cti_trig_out_b0,
	msm_mux_qdss_cti_trig_out_b1,
	msm_mux_qdss_traceclk_a,
	msm_mux_qdss_tracectl_a,
	msm_mux_qdss_tracedata_a,
	msm_mux_qdss_traceclk_b,
	msm_mux_qdss_tracectl_b,
	msm_mux_qdss_tracedata_b,
	msm_mux_qpic_pad,
	msm_mux_rx0,
	msm_mux_rx1,
	msm_mux_rx_swrm,
	msm_mux_rx_swrm0,
	msm_mux_rx_swrm1,
	msm_mux_sd_card,
	msm_mux_sd_write,
	msm_mux_tsens_max,
	msm_mux_tx_swrm,
	msm_mux_tx_swrm0,
	msm_mux_tx_swrm1,
	msm_mux_tx_swrm2,
	msm_mux_wci20,
	msm_mux_wci21,
	msm_mux_wci22,
	msm_mux_wci23,
	msm_mux_wsa_swrm,
	msm_mux__,
};

static const char * const blsp3_uart_groups[] = {
	"gpio73", "gpio74", "gpio75", "gpio76",
};

static const char * const blsp3_i2c_groups[] = {
	"gpio73", "gpio74",
};

static const char * const blsp3_spi_groups[] = {
	"gpio73", "gpio74", "gpio75", "gpio76", "gpio77", "gpio78", "gpio79",
};

static const char * const wci20_groups[] = {
	"gpio0", "gpio2",
};

static const char * const qpic_pad_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio9", "gpio10",
	"gpio11", "gpio17", "gpio15", "gpio12", "gpio13", "gpio14", "gpio5",
	"gpio6", "gpio7", "gpio8",
};

static const char * const burn0_groups[] = {
	"gpio0",
};

static const char * const mac12_groups[] = {
	"gpio1", "gpio11",
};

static const char * const qdss_tracectl_b_groups[] = {
	"gpio1",
};

static const char * const burn1_groups[] = {
	"gpio1",
};

static const char * const qdss_traceclk_b_groups[] = {
	"gpio0",
};

static const char * const qdss_tracedata_b_groups[] = {
	"gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7", "gpio8", "gpio9",
	"gpio10", "gpio11", "gpio12", "gpio13", "gpio14", "gpio15", "gpio16",
	"gpio17",
};

static const char * const mac01_groups[] = {
	"gpio3", "gpio4",
};

static const char * const mac21_groups[] = {
	"gpio5", "gpio6",
};

static const char * const atest_char_groups[] = {
	"gpio9",
};

static const char * const cxc0_groups[] = {
	"gpio9", "gpio16",
};

static const char * const mac13_groups[] = {
	"gpio9", "gpio16",
};

static const char * const dbg_out_groups[] = {
	"gpio9",
};

static const char * const wci22_groups[] = {
	"gpio11", "gpio17",
};

static const char * const pwm00_groups[] = {
	"gpio18",
};

static const char * const atest_char0_groups[] = {
	"gpio18",
};

static const char * const wci23_groups[] = {
	"gpio18", "gpio19",
};

static const char * const mac11_groups[] = {
	"gpio18", "gpio19",
};

static const char * const pwm10_groups[] = {
	"gpio19",
};

static const char * const atest_char1_groups[] = {
	"gpio19",
};

static const char * const pwm20_groups[] = {
	"gpio20",
};

static const char * const atest_char2_groups[] = {
	"gpio20",
};

static const char * const pwm30_groups[] = {
	"gpio21",
};

static const char * const atest_char3_groups[] = {
	"gpio21",
};

static const char * const audio_txmclk_groups[] = {
	"gpio22",
};

static const char * const audio_txmclkin_groups[] = {
	"gpio22",
};

static const char * const pwm02_groups[] = {
	"gpio22",
};

static const char * const tx_swrm0_groups[] = {
	"gpio22",
};

static const char * const qdss_cti_trig_out_b0_groups[] = {
	"gpio22",
};

static const char * const audio_txbclk_groups[] = {
	"gpio23",
};

static const char * const pwm12_groups[] = {
	"gpio23",
};

static const char * const wsa_swrm_groups[] = {
	"gpio23", "gpio24",
};

static const char * const tx_swrm1_groups[] = {
	"gpio23",
};

static const char * const qdss_cti_trig_in_b0_groups[] = {
	"gpio23",
};

static const char * const audio_txfsync_groups[] = {
	"gpio24",
};

static const char * const pwm22_groups[] = {
	"gpio24",
};

static const char * const tx_swrm2_groups[] = {
	"gpio24",
};

static const char * const qdss_cti_trig_out_b1_groups[] = {
	"gpio24",
};

static const char * const audio0_groups[] = {
	"gpio25", "gpio32",
};

static const char * const pwm32_groups[] = {
	"gpio25",
};

static const char * const tx_swrm_groups[] = {
	"gpio25",
};

static const char * const qdss_cti_trig_in_b1_groups[] = {
	"gpio25",
};

static const char * const audio1_groups[] = {
	"gpio26", "gpio33",
};

static const char * const pwm04_groups[] = {
	"gpio26",
};

static const char * const audio2_groups[] = {
	"gpio27",
};

static const char * const pwm14_groups[] = {
	"gpio27",
};

static const char * const audio3_groups[] = {
	"gpio28",
};

static const char * const pwm24_groups[] = {
	"gpio28",
};

static const char * const audio_rxmclk_groups[] = {
	"gpio29",
};

static const char * const audio_rxmclkin_groups[] = {
	"gpio29",
};

static const char * const pwm03_groups[] = {
	"gpio29",
};

static const char * const lpass_pdm_groups[] = {
	"gpio29", "gpio30", "gpio31", "gpio32",
};

static const char * const lpass_aud_groups[] = {
	"gpio29",
};

static const char * const qdss_cti_trig_in_a1_groups[] = {
	"gpio29",
};

static const char * const audio_rxbclk_groups[] = {
	"gpio30",
};

static const char * const pwm13_groups[] = {
	"gpio30",
};

static const char * const lpass_aud0_groups[] = {
	"gpio30",
};

static const char * const rx_swrm_groups[] = {
	"gpio30",
};

static const char * const qdss_cti_trig_out_a1_groups[] = {
	"gpio30",
};

static const char * const audio_rxfsync_groups[] = {
	"gpio31",
};

static const char * const pwm23_groups[] = {
	"gpio31",
};

static const char * const lpass_aud1_groups[] = {
	"gpio31",
};

static const char * const rx_swrm0_groups[] = {
	"gpio31",
};

static const char * const qdss_cti_trig_in_a0_groups[] = {
	"gpio31",
};

static const char * const pwm33_groups[] = {
	"gpio32",
};

static const char * const lpass_aud2_groups[] = {
	"gpio32",
};

static const char * const rx_swrm1_groups[] = {
	"gpio32",
};

static const char * const qdss_cti_trig_out_a0_groups[] = {
	"gpio32",
};

static const char * const lpass_pcm_groups[] = {
	"gpio34", "gpio35", "gpio36", "gpio37",
};

static const char * const mac10_groups[] = {
	"gpio34", "gpio35",
};

static const char * const mac00_groups[] = {
	"gpio34", "gpio35",
};

static const char * const mac20_groups[] = {
	"gpio36", "gpio37",
};

static const char * const blsp0_uart_groups[] = {
	"gpio38", "gpio39", "gpio40", "gpio41",
};

static const char * const blsp0_i2c_groups[] = {
	"gpio38", "gpio39",
};

static const char * const blsp0_spi_groups[] = {
	"gpio38", "gpio39", "gpio40", "gpio41",
};

static const char * const blsp2_uart_groups[] = {
	"gpio42", "gpio43", "gpio44", "gpio45",
};

static const char * const blsp2_i2c_groups[] = {
	"gpio42", "gpio43",
};

static const char * const blsp2_spi_groups[] = {
	"gpio42", "gpio43", "gpio44", "gpio45",
};

static const char * const blsp5_i2c_groups[] = {
	"gpio46", "gpio47",
};

static const char * const blsp5_uart_groups[] = {
	"gpio48", "gpio49",
};

static const char * const qdss_traceclk_a_groups[] = {
	"gpio48",
};

static const char * const qdss_tracectl_a_groups[] = {
	"gpio49",
};

static const char * const pwm01_groups[] = {
	"gpio50",
};

static const char * const pta1_1_groups[] = {
	"gpio51",
};

static const char * const pwm11_groups[] = {
	"gpio51",
};

static const char * const rx1_groups[] = {
	"gpio51",
};

static const char * const pta1_2_groups[] = {
	"gpio52",
};

static const char * const pwm21_groups[] = {
	"gpio52",
};

static const char * const pta1_0_groups[] = {
	"gpio53",
};

static const char * const pwm31_groups[] = {
	"gpio53",
};

static const char * const prng_rosc_groups[] = {
	"gpio53",
};

static const char * const blsp4_uart_groups[] = {
	"gpio55", "gpio56", "gpio57", "gpio58",
};

static const char * const blsp4_i2c_groups[] = {
	"gpio55", "gpio56",
};

static const char * const blsp4_spi_groups[] = {
	"gpio55", "gpio56", "gpio57", "gpio58",
};

static const char * const pcie0_clk_groups[] = {
	"gpio59",
};

static const char * const cri_trng0_groups[] = {
	"gpio59",
};

static const char * const pcie0_rst_groups[] = {
	"gpio60",
};

static const char * const cri_trng1_groups[] = {
	"gpio60",
};

static const char * const pcie0_wake_groups[] = {
	"gpio61",
};

static const char * const cri_trng_groups[] = {
	"gpio61",
};

static const char * const sd_card_groups[] = {
	"gpio62",
};

static const char * const sd_write_groups[] = {
	"gpio63",
};

static const char * const rx0_groups[] = {
	"gpio63",
};

static const char * const tsens_max_groups[] = {
	"gpio63",
};

static const char * const mdc_groups[] = {
	"gpio64",
};

static const char * const qdss_tracedata_a_groups[] = {
	"gpio64", "gpio65", "gpio66", "gpio67", "gpio68", "gpio69", "gpio70",
	"gpio71", "gpio72", "gpio73", "gpio74", "gpio75", "gpio76", "gpio77",
	"gpio78", "gpio79",
};

static const char * const mdio_groups[] = {
	"gpio65",
};

static const char * const pta2_0_groups[] = {
	"gpio66",
};

static const char * const wci21_groups[] = {
	"gpio66", "gpio68",
};

static const char * const cxc1_groups[] = {
	"gpio66", "gpio68",
};

static const char * const pta2_1_groups[] = {
	"gpio67",
};

static const char * const pta2_2_groups[] = {
	"gpio68",
};

static const char * const blsp1_uart_groups[] = {
	"gpio69", "gpio70", "gpio71", "gpio72",
};

static const char * const blsp1_i2c_groups[] = {
	"gpio69", "gpio70",
};

static const char * const blsp1_spi_groups[] = {
	"gpio69", "gpio70", "gpio71", "gpio72",
};

static const char * const gcc_plltest_groups[] = {
	"gpio69", "gpio71",
};

static const char * const gcc_tlmm_groups[] = {
	"gpio70",
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
	"gpio64", "gpio65", "gpio66", "gpio67", "gpio68", "gpio69", "gpio70",
	"gpio71", "gpio72", "gpio73", "gpio74", "gpio75", "gpio76", "gpio77",
	"gpio78", "gpio79",
};

static const struct pinfunction ipq6018_functions[] = {
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_char0),
	MSM_PIN_FUNCTION(atest_char1),
	MSM_PIN_FUNCTION(atest_char2),
	MSM_PIN_FUNCTION(atest_char3),
	MSM_PIN_FUNCTION(audio0),
	MSM_PIN_FUNCTION(audio1),
	MSM_PIN_FUNCTION(audio2),
	MSM_PIN_FUNCTION(audio3),
	MSM_PIN_FUNCTION(audio_rxbclk),
	MSM_PIN_FUNCTION(audio_rxfsync),
	MSM_PIN_FUNCTION(audio_rxmclk),
	MSM_PIN_FUNCTION(audio_rxmclkin),
	MSM_PIN_FUNCTION(audio_txbclk),
	MSM_PIN_FUNCTION(audio_txfsync),
	MSM_PIN_FUNCTION(audio_txmclk),
	MSM_PIN_FUNCTION(audio_txmclkin),
	MSM_PIN_FUNCTION(blsp0_i2c),
	MSM_PIN_FUNCTION(blsp0_spi),
	MSM_PIN_FUNCTION(blsp0_uart),
	MSM_PIN_FUNCTION(blsp1_i2c),
	MSM_PIN_FUNCTION(blsp1_spi),
	MSM_PIN_FUNCTION(blsp1_uart),
	MSM_PIN_FUNCTION(blsp2_i2c),
	MSM_PIN_FUNCTION(blsp2_spi),
	MSM_PIN_FUNCTION(blsp2_uart),
	MSM_PIN_FUNCTION(blsp3_i2c),
	MSM_PIN_FUNCTION(blsp3_spi),
	MSM_PIN_FUNCTION(blsp3_uart),
	MSM_PIN_FUNCTION(blsp4_i2c),
	MSM_PIN_FUNCTION(blsp4_spi),
	MSM_PIN_FUNCTION(blsp4_uart),
	MSM_PIN_FUNCTION(blsp5_i2c),
	MSM_PIN_FUNCTION(blsp5_uart),
	MSM_PIN_FUNCTION(burn0),
	MSM_PIN_FUNCTION(burn1),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(cri_trng0),
	MSM_PIN_FUNCTION(cri_trng1),
	MSM_PIN_FUNCTION(cxc0),
	MSM_PIN_FUNCTION(cxc1),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(gcc_plltest),
	MSM_PIN_FUNCTION(gcc_tlmm),
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(lpass_aud),
	MSM_PIN_FUNCTION(lpass_aud0),
	MSM_PIN_FUNCTION(lpass_aud1),
	MSM_PIN_FUNCTION(lpass_aud2),
	MSM_PIN_FUNCTION(lpass_pcm),
	MSM_PIN_FUNCTION(lpass_pdm),
	MSM_PIN_FUNCTION(mac00),
	MSM_PIN_FUNCTION(mac01),
	MSM_PIN_FUNCTION(mac10),
	MSM_PIN_FUNCTION(mac11),
	MSM_PIN_FUNCTION(mac12),
	MSM_PIN_FUNCTION(mac13),
	MSM_PIN_FUNCTION(mac20),
	MSM_PIN_FUNCTION(mac21),
	MSM_PIN_FUNCTION(mdc),
	MSM_PIN_FUNCTION(mdio),
	MSM_PIN_FUNCTION(pcie0_clk),
	MSM_PIN_FUNCTION(pcie0_rst),
	MSM_PIN_FUNCTION(pcie0_wake),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(pta1_0),
	MSM_PIN_FUNCTION(pta1_1),
	MSM_PIN_FUNCTION(pta1_2),
	MSM_PIN_FUNCTION(pta2_0),
	MSM_PIN_FUNCTION(pta2_1),
	MSM_PIN_FUNCTION(pta2_2),
	MSM_PIN_FUNCTION(pwm00),
	MSM_PIN_FUNCTION(pwm01),
	MSM_PIN_FUNCTION(pwm02),
	MSM_PIN_FUNCTION(pwm03),
	MSM_PIN_FUNCTION(pwm04),
	MSM_PIN_FUNCTION(pwm10),
	MSM_PIN_FUNCTION(pwm11),
	MSM_PIN_FUNCTION(pwm12),
	MSM_PIN_FUNCTION(pwm13),
	MSM_PIN_FUNCTION(pwm14),
	MSM_PIN_FUNCTION(pwm20),
	MSM_PIN_FUNCTION(pwm21),
	MSM_PIN_FUNCTION(pwm22),
	MSM_PIN_FUNCTION(pwm23),
	MSM_PIN_FUNCTION(pwm24),
	MSM_PIN_FUNCTION(pwm30),
	MSM_PIN_FUNCTION(pwm31),
	MSM_PIN_FUNCTION(pwm32),
	MSM_PIN_FUNCTION(pwm33),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a0),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a1),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b1),
	MSM_PIN_FUNCTION(qdss_traceclk_a),
	MSM_PIN_FUNCTION(qdss_tracectl_a),
	MSM_PIN_FUNCTION(qdss_tracedata_a),
	MSM_PIN_FUNCTION(qdss_traceclk_b),
	MSM_PIN_FUNCTION(qdss_tracectl_b),
	MSM_PIN_FUNCTION(qdss_tracedata_b),
	MSM_PIN_FUNCTION(qpic_pad),
	MSM_PIN_FUNCTION(rx0),
	MSM_PIN_FUNCTION(rx1),
	MSM_PIN_FUNCTION(rx_swrm),
	MSM_PIN_FUNCTION(rx_swrm0),
	MSM_PIN_FUNCTION(rx_swrm1),
	MSM_PIN_FUNCTION(sd_card),
	MSM_PIN_FUNCTION(sd_write),
	MSM_PIN_FUNCTION(tsens_max),
	MSM_PIN_FUNCTION(tx_swrm),
	MSM_PIN_FUNCTION(tx_swrm0),
	MSM_PIN_FUNCTION(tx_swrm1),
	MSM_PIN_FUNCTION(tx_swrm2),
	MSM_PIN_FUNCTION(wci20),
	MSM_PIN_FUNCTION(wci21),
	MSM_PIN_FUNCTION(wci22),
	MSM_PIN_FUNCTION(wci23),
	MSM_PIN_FUNCTION(wsa_swrm),
};

static const struct msm_pingroup ipq6018_groups[] = {
	PINGROUP(0, qpic_pad, wci20, qdss_traceclk_b, _, burn0, _, _, _, _),
	PINGROUP(1, qpic_pad, mac12, qdss_tracectl_b, _, burn1, _, _, _, _),
	PINGROUP(2, qpic_pad, wci20, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(3, qpic_pad, mac01, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(4, qpic_pad, mac01, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(5, qpic_pad, mac21, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(6, qpic_pad, mac21, qdss_tracedata_b, _, _, _, _, _, _),
	PINGROUP(7, qpic_pad, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(8, qpic_pad, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(9, qpic_pad, atest_char, cxc0, mac13, dbg_out, qdss_tracedata_b, _, _, _),
	PINGROUP(10, qpic_pad, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(11, qpic_pad, wci22, mac12, qdss_tracedata_b, _, _, _, _, _),
	PINGROUP(12, qpic_pad, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(13, qpic_pad, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(14, qpic_pad, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(15, qpic_pad, qdss_tracedata_b, _, _, _, _, _, _, _),
	PINGROUP(16, qpic_pad, cxc0, mac13, qdss_tracedata_b, _, _, _, _, _),
	PINGROUP(17, qpic_pad, qdss_tracedata_b, wci22, _, _, _, _, _, _),
	PINGROUP(18, pwm00, atest_char0, wci23, mac11, _, _, _, _, _),
	PINGROUP(19, pwm10, atest_char1, wci23, mac11, _, _, _, _, _),
	PINGROUP(20, pwm20, atest_char2, _, _, _, _, _, _, _),
	PINGROUP(21, pwm30, atest_char3, _, _, _, _, _, _, _),
	PINGROUP(22, audio_txmclk, audio_txmclkin, pwm02, tx_swrm0, _, qdss_cti_trig_out_b0, _, _, _),
	PINGROUP(23, audio_txbclk, pwm12, wsa_swrm, tx_swrm1, _, qdss_cti_trig_in_b0, _, _, _),
	PINGROUP(24, audio_txfsync, pwm22, wsa_swrm, tx_swrm2, _, qdss_cti_trig_out_b1, _, _, _),
	PINGROUP(25, audio0, pwm32, tx_swrm, _, qdss_cti_trig_in_b1, _, _, _, _),
	PINGROUP(26, audio1, pwm04, _, _, _, _, _, _, _),
	PINGROUP(27, audio2, pwm14, _, _, _, _, _, _, _),
	PINGROUP(28, audio3, pwm24, _, _, _, _, _, _, _),
	PINGROUP(29, audio_rxmclk, audio_rxmclkin, pwm03, lpass_pdm, lpass_aud, qdss_cti_trig_in_a1, _, _, _),
	PINGROUP(30, audio_rxbclk, pwm13, lpass_pdm, lpass_aud0, rx_swrm, _, qdss_cti_trig_out_a1, _, _),
	PINGROUP(31, audio_rxfsync, pwm23, lpass_pdm, lpass_aud1, rx_swrm0, _, qdss_cti_trig_in_a0, _, _),
	PINGROUP(32, audio0, pwm33, lpass_pdm, lpass_aud2, rx_swrm1, _, qdss_cti_trig_out_a0, _, _),
	PINGROUP(33, audio1, _, _, _, _, _, _, _, _),
	PINGROUP(34, lpass_pcm, mac10, mac00, _, _, _, _, _, _),
	PINGROUP(35, lpass_pcm, mac10, mac00, _, _, _, _, _, _),
	PINGROUP(36, lpass_pcm, mac20, _, _, _, _, _, _, _),
	PINGROUP(37, lpass_pcm, mac20, _, _, _, _, _, _, _),
	PINGROUP(38, blsp0_uart, blsp0_i2c, blsp0_spi, _, _, _, _, _, _),
	PINGROUP(39, blsp0_uart, blsp0_i2c, blsp0_spi, _, _, _, _, _, _),
	PINGROUP(40, blsp0_uart, blsp0_spi, _, _, _, _, _, _, _),
	PINGROUP(41, blsp0_uart, blsp0_spi, _, _, _, _, _, _, _),
	PINGROUP(42, blsp2_uart, blsp2_i2c, blsp2_spi, _, _, _, _, _, _),
	PINGROUP(43, blsp2_uart, blsp2_i2c, blsp2_spi, _, _, _, _, _, _),
	PINGROUP(44, blsp2_uart, blsp2_spi, _, _, _, _, _, _, _),
	PINGROUP(45, blsp2_uart, blsp2_spi, _, _, _, _, _, _, _),
	PINGROUP(46, blsp5_i2c, _, _, _, _, _, _, _, _),
	PINGROUP(47, blsp5_i2c, _, _, _, _, _, _, _, _),
	PINGROUP(48, blsp5_uart, _, qdss_traceclk_a, _, _, _, _, _, _),
	PINGROUP(49, blsp5_uart, _, qdss_tracectl_a, _, _, _, _, _, _),
	PINGROUP(50, pwm01, _, _, _, _, _, _, _, _),
	PINGROUP(51, pta1_1, pwm11, _, rx1, _, _, _, _, _),
	PINGROUP(52, pta1_2, pwm21, _, _, _, _, _, _, _),
	PINGROUP(53, pta1_0, pwm31, prng_rosc, _, _, _, _, _, _),
	PINGROUP(54, _, _, _, _, _, _, _, _, _),
	PINGROUP(55, blsp4_uart, blsp4_i2c, blsp4_spi, _, _, _, _, _, _),
	PINGROUP(56, blsp4_uart, blsp4_i2c, blsp4_spi, _, _, _, _, _, _),
	PINGROUP(57, blsp4_uart, blsp4_spi, _, _, _, _, _, _, _),
	PINGROUP(58, blsp4_uart, blsp4_spi, _, _, _, _, _, _, _),
	PINGROUP(59, pcie0_clk, _, _, cri_trng0, _, _, _, _, _),
	PINGROUP(60, pcie0_rst, _, _, cri_trng1, _, _, _, _, _),
	PINGROUP(61, pcie0_wake, _, _, cri_trng, _, _, _, _, _),
	PINGROUP(62, sd_card, _, _, _, _, _, _, _, _),
	PINGROUP(63, sd_write, rx0, _, tsens_max, _, _, _, _, _),
	PINGROUP(64, mdc, _, qdss_tracedata_a, _, _, _, _, _, _),
	PINGROUP(65, mdio, _, qdss_tracedata_a, _, _, _, _, _, _),
	PINGROUP(66, pta2_0, wci21, cxc1, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(67, pta2_1, qdss_tracedata_a, _, _, _, _, _, _, _),
	PINGROUP(68, pta2_2, wci21, cxc1, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(69, blsp1_uart, blsp1_i2c, blsp1_spi, gcc_plltest, qdss_tracedata_a, _, _, _, _),
	PINGROUP(70, blsp1_uart, blsp1_i2c, blsp1_spi, gcc_tlmm, qdss_tracedata_a, _, _, _, _),
	PINGROUP(71, blsp1_uart, blsp1_spi, gcc_plltest, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(72, blsp1_uart, blsp1_spi, qdss_tracedata_a, _, _, _, _, _, _),
	PINGROUP(73, blsp3_uart, blsp3_i2c, blsp3_spi, _, qdss_tracedata_a, _, _, _, _),
	PINGROUP(74, blsp3_uart, blsp3_i2c, blsp3_spi, _, qdss_tracedata_a, _, _, _, _),
	PINGROUP(75, blsp3_uart, blsp3_spi, _, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(76, blsp3_uart, blsp3_spi, _, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(77, blsp3_spi, _, qdss_tracedata_a, _, _, _, _, _, _),
	PINGROUP(78, blsp3_spi, _, qdss_tracedata_a, _, _, _, _, _, _),
	PINGROUP(79, blsp3_spi, _, qdss_tracedata_a, _, _, _, _, _, _),
};

static const struct msm_pinctrl_soc_data ipq6018_pinctrl = {
	.pins = ipq6018_pins,
	.npins = ARRAY_SIZE(ipq6018_pins),
	.functions = ipq6018_functions,
	.nfunctions = ARRAY_SIZE(ipq6018_functions),
	.groups = ipq6018_groups,
	.ngroups = ARRAY_SIZE(ipq6018_groups),
	.ngpios = 80,
};

static int ipq6018_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &ipq6018_pinctrl);
}

static const struct of_device_id ipq6018_pinctrl_of_match[] = {
	{ .compatible = "qcom,ipq6018-pinctrl", },
	{ },
};

static struct platform_driver ipq6018_pinctrl_driver = {
	.driver = {
		.name = "ipq6018-pinctrl",
		.of_match_table = ipq6018_pinctrl_of_match,
	},
	.probe = ipq6018_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init ipq6018_pinctrl_init(void)
{
	return platform_driver_register(&ipq6018_pinctrl_driver);
}
arch_initcall(ipq6018_pinctrl_init);

static void __exit ipq6018_pinctrl_exit(void)
{
	platform_driver_unregister(&ipq6018_pinctrl_driver);
}
module_exit(ipq6018_pinctrl_exit);

MODULE_DESCRIPTION("QTI ipq6018 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, ipq6018_pinctrl_of_match);
