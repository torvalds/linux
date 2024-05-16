// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

static const char * const qcs404_tiles[] = {
	"north",
	"south",
	"east"
};

enum {
	NORTH,
	SOUTH,
	EAST
};

#define PINGROUP(id, _tile, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{						\
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
		},					\
		.nfuncs = 10,				\
		.ctl_reg = 0x1000 * id,		\
		.io_reg = 0x1000 * id + 0x4,		\
		.intr_cfg_reg = 0x1000 * id + 0x8,	\
		.intr_status_reg = 0x1000 * id + 0xc,	\
		.intr_target_reg = 0x1000 * id + 0x8,	\
		.tile = _tile,			\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
		.oe_bit = 9,			\
		.in_bit = 0,			\
		.out_bit = 1,			\
		.intr_enable_bit = 0,		\
		.intr_status_bit = 0,		\
		.intr_target_bit = 5,		\
		.intr_target_kpss_val = 4,	\
		.intr_raw_status_bit = 4,	\
		.intr_polarity_bit = 1,		\
		.intr_detection_bit = 2,	\
		.intr_detection_width = 2,	\
	}

#define SDC_QDSD_PINGROUP(pg_name, ctl, pull, drv)	\
	{						\
		.grp = PINCTRL_PINGROUP(#pg_name, 	\
			pg_name##_pins, 		\
			ARRAY_SIZE(pg_name##_pins)),	\
		.ctl_reg = ctl,				\
		.io_reg = 0,				\
		.intr_cfg_reg = 0,			\
		.intr_status_reg = 0,			\
		.intr_target_reg = 0,			\
		.tile = SOUTH,				\
		.mux_bit = -1,				\
		.pull_bit = pull,			\
		.drv_bit = drv,				\
		.oe_bit = -1,				\
		.in_bit = -1,				\
		.out_bit = -1,				\
		.intr_enable_bit = -1,			\
		.intr_status_bit = -1,			\
		.intr_target_bit = -1,			\
		.intr_raw_status_bit = -1,		\
		.intr_polarity_bit = -1,		\
		.intr_detection_bit = -1,		\
		.intr_detection_width = -1,		\
	}

static const struct pinctrl_pin_desc qcs404_pins[] = {
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
	PINCTRL_PIN(80, "GPIO_80"),
	PINCTRL_PIN(81, "GPIO_81"),
	PINCTRL_PIN(82, "GPIO_82"),
	PINCTRL_PIN(83, "GPIO_83"),
	PINCTRL_PIN(84, "GPIO_84"),
	PINCTRL_PIN(85, "GPIO_85"),
	PINCTRL_PIN(86, "GPIO_86"),
	PINCTRL_PIN(87, "GPIO_87"),
	PINCTRL_PIN(88, "GPIO_88"),
	PINCTRL_PIN(89, "GPIO_89"),
	PINCTRL_PIN(90, "GPIO_90"),
	PINCTRL_PIN(91, "GPIO_91"),
	PINCTRL_PIN(92, "GPIO_92"),
	PINCTRL_PIN(93, "GPIO_93"),
	PINCTRL_PIN(94, "GPIO_94"),
	PINCTRL_PIN(95, "GPIO_95"),
	PINCTRL_PIN(96, "GPIO_96"),
	PINCTRL_PIN(97, "GPIO_97"),
	PINCTRL_PIN(98, "GPIO_98"),
	PINCTRL_PIN(99, "GPIO_99"),
	PINCTRL_PIN(100, "GPIO_100"),
	PINCTRL_PIN(101, "GPIO_101"),
	PINCTRL_PIN(102, "GPIO_102"),
	PINCTRL_PIN(103, "GPIO_103"),
	PINCTRL_PIN(104, "GPIO_104"),
	PINCTRL_PIN(105, "GPIO_105"),
	PINCTRL_PIN(106, "GPIO_106"),
	PINCTRL_PIN(107, "GPIO_107"),
	PINCTRL_PIN(108, "GPIO_108"),
	PINCTRL_PIN(109, "GPIO_109"),
	PINCTRL_PIN(110, "GPIO_110"),
	PINCTRL_PIN(111, "GPIO_111"),
	PINCTRL_PIN(112, "GPIO_112"),
	PINCTRL_PIN(113, "GPIO_113"),
	PINCTRL_PIN(114, "GPIO_114"),
	PINCTRL_PIN(115, "GPIO_115"),
	PINCTRL_PIN(116, "GPIO_116"),
	PINCTRL_PIN(117, "GPIO_117"),
	PINCTRL_PIN(118, "GPIO_118"),
	PINCTRL_PIN(119, "GPIO_119"),
	PINCTRL_PIN(120, "SDC1_RCLK"),
	PINCTRL_PIN(121, "SDC1_CLK"),
	PINCTRL_PIN(122, "SDC1_CMD"),
	PINCTRL_PIN(123, "SDC1_DATA"),
	PINCTRL_PIN(124, "SDC2_CLK"),
	PINCTRL_PIN(125, "SDC2_CMD"),
	PINCTRL_PIN(126, "SDC2_DATA"),
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
DECLARE_MSM_GPIO_PINS(80);
DECLARE_MSM_GPIO_PINS(81);
DECLARE_MSM_GPIO_PINS(82);
DECLARE_MSM_GPIO_PINS(83);
DECLARE_MSM_GPIO_PINS(84);
DECLARE_MSM_GPIO_PINS(85);
DECLARE_MSM_GPIO_PINS(86);
DECLARE_MSM_GPIO_PINS(87);
DECLARE_MSM_GPIO_PINS(88);
DECLARE_MSM_GPIO_PINS(89);
DECLARE_MSM_GPIO_PINS(90);
DECLARE_MSM_GPIO_PINS(91);
DECLARE_MSM_GPIO_PINS(92);
DECLARE_MSM_GPIO_PINS(93);
DECLARE_MSM_GPIO_PINS(94);
DECLARE_MSM_GPIO_PINS(95);
DECLARE_MSM_GPIO_PINS(96);
DECLARE_MSM_GPIO_PINS(97);
DECLARE_MSM_GPIO_PINS(98);
DECLARE_MSM_GPIO_PINS(99);
DECLARE_MSM_GPIO_PINS(100);
DECLARE_MSM_GPIO_PINS(101);
DECLARE_MSM_GPIO_PINS(102);
DECLARE_MSM_GPIO_PINS(103);
DECLARE_MSM_GPIO_PINS(104);
DECLARE_MSM_GPIO_PINS(105);
DECLARE_MSM_GPIO_PINS(106);
DECLARE_MSM_GPIO_PINS(107);
DECLARE_MSM_GPIO_PINS(108);
DECLARE_MSM_GPIO_PINS(109);
DECLARE_MSM_GPIO_PINS(110);
DECLARE_MSM_GPIO_PINS(111);
DECLARE_MSM_GPIO_PINS(112);
DECLARE_MSM_GPIO_PINS(113);
DECLARE_MSM_GPIO_PINS(114);
DECLARE_MSM_GPIO_PINS(115);
DECLARE_MSM_GPIO_PINS(116);
DECLARE_MSM_GPIO_PINS(117);
DECLARE_MSM_GPIO_PINS(118);
DECLARE_MSM_GPIO_PINS(119);

static const unsigned int sdc1_rclk_pins[] = { 120 };
static const unsigned int sdc1_clk_pins[] = { 121 };
static const unsigned int sdc1_cmd_pins[] = { 122 };
static const unsigned int sdc1_data_pins[] = { 123 };
static const unsigned int sdc2_clk_pins[] = { 124 };
static const unsigned int sdc2_cmd_pins[] = { 125 };
static const unsigned int sdc2_data_pins[] = { 126 };

enum qcs404_functions {
	msm_mux_gpio,
	msm_mux_hdmi_tx,
	msm_mux_hdmi_ddc,
	msm_mux_blsp_uart_tx_a2,
	msm_mux_blsp_spi2,
	msm_mux_m_voc,
	msm_mux_qdss_cti_trig_in_a0,
	msm_mux_blsp_uart_rx_a2,
	msm_mux_qdss_tracectl_a,
	msm_mux_blsp_uart2,
	msm_mux_aud_cdc,
	msm_mux_blsp_i2c_sda_a2,
	msm_mux_qdss_tracedata_a,
	msm_mux_blsp_i2c_scl_a2,
	msm_mux_qdss_tracectl_b,
	msm_mux_qdss_cti_trig_in_b0,
	msm_mux_blsp_uart1,
	msm_mux_blsp_spi_mosi_a1,
	msm_mux_blsp_spi_miso_a1,
	msm_mux_qdss_tracedata_b,
	msm_mux_blsp_i2c1,
	msm_mux_blsp_spi_cs_n_a1,
	msm_mux_gcc_plltest,
	msm_mux_blsp_spi_clk_a1,
	msm_mux_rgb_data0,
	msm_mux_blsp_uart5,
	msm_mux_blsp_spi5,
	msm_mux_adsp_ext,
	msm_mux_rgb_data1,
	msm_mux_prng_rosc,
	msm_mux_rgb_data2,
	msm_mux_blsp_i2c5,
	msm_mux_gcc_gp1_clk_b,
	msm_mux_rgb_data3,
	msm_mux_gcc_gp2_clk_b,
	msm_mux_blsp_spi0,
	msm_mux_blsp_uart0,
	msm_mux_gcc_gp3_clk_b,
	msm_mux_blsp_i2c0,
	msm_mux_qdss_traceclk_b,
	msm_mux_pcie_clk,
	msm_mux_nfc_irq,
	msm_mux_blsp_spi4,
	msm_mux_nfc_dwl,
	msm_mux_audio_ts,
	msm_mux_rgb_data4,
	msm_mux_spi_lcd,
	msm_mux_blsp_uart_tx_b2,
	msm_mux_gcc_gp3_clk_a,
	msm_mux_rgb_data5,
	msm_mux_blsp_uart_rx_b2,
	msm_mux_blsp_i2c_sda_b2,
	msm_mux_blsp_i2c_scl_b2,
	msm_mux_pwm_led11,
	msm_mux_i2s_3_data0_a,
	msm_mux_ebi2_lcd,
	msm_mux_i2s_3_data1_a,
	msm_mux_i2s_3_data2_a,
	msm_mux_atest_char,
	msm_mux_pwm_led3,
	msm_mux_i2s_3_data3_a,
	msm_mux_pwm_led4,
	msm_mux_i2s_4,
	msm_mux_ebi2_a,
	msm_mux_dsd_clk_b,
	msm_mux_pwm_led5,
	msm_mux_pwm_led6,
	msm_mux_pwm_led7,
	msm_mux_pwm_led8,
	msm_mux_pwm_led24,
	msm_mux_spkr_dac0,
	msm_mux_blsp_i2c4,
	msm_mux_pwm_led9,
	msm_mux_pwm_led10,
	msm_mux_spdifrx_opt,
	msm_mux_pwm_led12,
	msm_mux_pwm_led13,
	msm_mux_pwm_led14,
	msm_mux_wlan1_adc1,
	msm_mux_rgb_data_b0,
	msm_mux_pwm_led15,
	msm_mux_blsp_spi_mosi_b1,
	msm_mux_wlan1_adc0,
	msm_mux_rgb_data_b1,
	msm_mux_pwm_led16,
	msm_mux_blsp_spi_miso_b1,
	msm_mux_qdss_cti_trig_out_b0,
	msm_mux_wlan2_adc1,
	msm_mux_rgb_data_b2,
	msm_mux_pwm_led17,
	msm_mux_blsp_spi_cs_n_b1,
	msm_mux_wlan2_adc0,
	msm_mux_rgb_data_b3,
	msm_mux_pwm_led18,
	msm_mux_blsp_spi_clk_b1,
	msm_mux_rgb_data_b4,
	msm_mux_pwm_led19,
	msm_mux_ext_mclk1_b,
	msm_mux_qdss_traceclk_a,
	msm_mux_rgb_data_b5,
	msm_mux_pwm_led20,
	msm_mux_atest_char3,
	msm_mux_i2s_3_sck_b,
	msm_mux_ldo_update,
	msm_mux_bimc_dte0,
	msm_mux_rgb_hsync,
	msm_mux_pwm_led21,
	msm_mux_i2s_3_ws_b,
	msm_mux_dbg_out,
	msm_mux_rgb_vsync,
	msm_mux_i2s_3_data0_b,
	msm_mux_ldo_en,
	msm_mux_hdmi_dtest,
	msm_mux_rgb_de,
	msm_mux_i2s_3_data1_b,
	msm_mux_hdmi_lbk9,
	msm_mux_rgb_clk,
	msm_mux_atest_char1,
	msm_mux_i2s_3_data2_b,
	msm_mux_ebi_cdc,
	msm_mux_hdmi_lbk8,
	msm_mux_rgb_mdp,
	msm_mux_atest_char0,
	msm_mux_i2s_3_data3_b,
	msm_mux_hdmi_lbk7,
	msm_mux_rgb_data_b6,
	msm_mux_rgb_data_b7,
	msm_mux_hdmi_lbk6,
	msm_mux_rgmii_int,
	msm_mux_cri_trng1,
	msm_mux_rgmii_wol,
	msm_mux_cri_trng0,
	msm_mux_gcc_tlmm,
	msm_mux_rgmii_ck,
	msm_mux_rgmii_tx,
	msm_mux_hdmi_lbk5,
	msm_mux_hdmi_pixel,
	msm_mux_hdmi_rcv,
	msm_mux_hdmi_lbk4,
	msm_mux_rgmii_ctl,
	msm_mux_ext_lpass,
	msm_mux_rgmii_rx,
	msm_mux_cri_trng,
	msm_mux_hdmi_lbk3,
	msm_mux_hdmi_lbk2,
	msm_mux_qdss_cti_trig_out_b1,
	msm_mux_rgmii_mdio,
	msm_mux_hdmi_lbk1,
	msm_mux_rgmii_mdc,
	msm_mux_hdmi_lbk0,
	msm_mux_ir_in,
	msm_mux_wsa_en,
	msm_mux_rgb_data6,
	msm_mux_rgb_data7,
	msm_mux_atest_char2,
	msm_mux_ebi_ch0,
	msm_mux_blsp_uart3,
	msm_mux_blsp_spi3,
	msm_mux_sd_write,
	msm_mux_blsp_i2c3,
	msm_mux_gcc_gp1_clk_a,
	msm_mux_qdss_cti_trig_in_b1,
	msm_mux_gcc_gp2_clk_a,
	msm_mux_ext_mclk0,
	msm_mux_mclk_in1,
	msm_mux_i2s_1,
	msm_mux_dsd_clk_a,
	msm_mux_qdss_cti_trig_in_a1,
	msm_mux_rgmi_dll1,
	msm_mux_pwm_led22,
	msm_mux_pwm_led23,
	msm_mux_qdss_cti_trig_out_a0,
	msm_mux_rgmi_dll2,
	msm_mux_pwm_led1,
	msm_mux_qdss_cti_trig_out_a1,
	msm_mux_pwm_led2,
	msm_mux_i2s_2,
	msm_mux_pll_bist,
	msm_mux_ext_mclk1_a,
	msm_mux_mclk_in2,
	msm_mux_bimc_dte1,
	msm_mux_i2s_3_sck_a,
	msm_mux_i2s_3_ws_a,
	msm_mux__,
};

static const char * const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio13", "gpio14",
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21",
	"gpio21", "gpio21", "gpio22", "gpio22", "gpio23", "gpio23", "gpio24",
	"gpio25", "gpio26", "gpio27", "gpio28", "gpio29", "gpio30", "gpio31",
	"gpio32", "gpio33", "gpio34", "gpio35", "gpio36", "gpio36", "gpio36",
	"gpio36", "gpio37", "gpio37", "gpio37", "gpio38", "gpio38", "gpio38",
	"gpio39", "gpio39", "gpio40", "gpio40", "gpio41", "gpio41", "gpio41",
	"gpio42", "gpio43", "gpio44", "gpio45", "gpio46", "gpio47", "gpio48",
	"gpio49", "gpio50", "gpio51", "gpio52", "gpio53", "gpio54", "gpio55",
	"gpio56", "gpio57", "gpio58", "gpio59", "gpio59", "gpio60", "gpio61",
	"gpio62", "gpio63", "gpio64", "gpio65", "gpio66", "gpio67", "gpio68",
	"gpio69", "gpio70", "gpio71", "gpio72", "gpio73", "gpio74", "gpio75",
	"gpio76", "gpio77", "gpio77", "gpio78", "gpio78", "gpio78", "gpio79",
	"gpio79", "gpio79", "gpio80", "gpio81", "gpio81", "gpio82", "gpio83",
	"gpio84", "gpio85", "gpio86", "gpio87", "gpio88", "gpio89", "gpio90",
	"gpio91", "gpio92", "gpio93", "gpio94", "gpio95", "gpio96", "gpio97",
	"gpio98", "gpio99", "gpio100", "gpio101", "gpio102", "gpio103",
	"gpio104", "gpio105", "gpio106", "gpio107", "gpio108", "gpio108",
	"gpio108", "gpio109", "gpio109", "gpio110", "gpio111", "gpio112",
	"gpio113", "gpio114", "gpio115", "gpio116", "gpio117", "gpio118",
	"gpio119",
};

static const char * const hdmi_tx_groups[] = {
	"gpio14",
};

static const char * const hdmi_ddc_groups[] = {
	"gpio15", "gpio16",
};

static const char * const blsp_uart_tx_a2_groups[] = {
	"gpio17",
};

static const char * const blsp_spi2_groups[] = {
	"gpio17", "gpio18", "gpio19", "gpio20",
};

static const char * const m_voc_groups[] = {
	"gpio17", "gpio21",
};

static const char * const qdss_cti_trig_in_a0_groups[] = {
	"gpio17",
};

static const char * const blsp_uart_rx_a2_groups[] = {
	"gpio18",
};

static const char * const qdss_tracectl_a_groups[] = {
	"gpio18",
};

static const char * const blsp_uart2_groups[] = {
	"gpio19", "gpio20",
};

static const char * const aud_cdc_groups[] = {
	"gpio19", "gpio20",
};

static const char * const blsp_i2c_sda_a2_groups[] = {
	"gpio19",
};

static const char * const qdss_tracedata_a_groups[] = {
	"gpio19", "gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio30",
	"gpio31", "gpio32", "gpio36", "gpio38", "gpio39", "gpio42", "gpio43",
	"gpio82", "gpio83",
};

static const char * const blsp_i2c_scl_a2_groups[] = {
	"gpio20",
};

static const char * const qdss_tracectl_b_groups[] = {
	"gpio20",
};

static const char * const qdss_cti_trig_in_b0_groups[] = {
	"gpio21",
};

static const char * const blsp_uart1_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25",
};

static const char * const blsp_spi_mosi_a1_groups[] = {
	"gpio22",
};

static const char * const blsp_spi_miso_a1_groups[] = {
	"gpio23",
};

static const char * const qdss_tracedata_b_groups[] = {
	"gpio23", "gpio35", "gpio40", "gpio41", "gpio44", "gpio45", "gpio46",
	"gpio47", "gpio49", "gpio50", "gpio55", "gpio61", "gpio62", "gpio85",
	"gpio89", "gpio93",
};

static const char * const blsp_i2c1_groups[] = {
	"gpio24", "gpio25",
};

static const char * const blsp_spi_cs_n_a1_groups[] = {
	"gpio24",
};

static const char * const gcc_plltest_groups[] = {
	"gpio24", "gpio25",
};

static const char * const blsp_spi_clk_a1_groups[] = {
	"gpio25",
};

static const char * const rgb_data0_groups[] = {
	"gpio26", "gpio41",
};

static const char * const blsp_uart5_groups[] = {
	"gpio26", "gpio27", "gpio28", "gpio29",
};

static const char * const blsp_spi5_groups[] = {
	"gpio26", "gpio27", "gpio28", "gpio29", "gpio44", "gpio45", "gpio46",
};

static const char * const adsp_ext_groups[] = {
	"gpio26",
};

static const char * const rgb_data1_groups[] = {
	"gpio27", "gpio42",
};

static const char * const prng_rosc_groups[] = {
	"gpio27",
};

static const char * const rgb_data2_groups[] = {
	"gpio28", "gpio43",
};

static const char * const blsp_i2c5_groups[] = {
	"gpio28", "gpio29",
};

static const char * const gcc_gp1_clk_b_groups[] = {
	"gpio28",
};

static const char * const rgb_data3_groups[] = {
	"gpio29", "gpio44",
};

static const char * const gcc_gp2_clk_b_groups[] = {
	"gpio29",
};

static const char * const blsp_spi0_groups[] = {
	"gpio30", "gpio31", "gpio32", "gpio33",
};

static const char * const blsp_uart0_groups[] = {
	"gpio30", "gpio31", "gpio32", "gpio33",
};

static const char * const gcc_gp3_clk_b_groups[] = {
	"gpio30",
};

static const char * const blsp_i2c0_groups[] = {
	"gpio32", "gpio33",
};

static const char * const qdss_traceclk_b_groups[] = {
	"gpio34",
};

static const char * const pcie_clk_groups[] = {
	"gpio35",
};

static const char * const nfc_irq_groups[] = {
	"gpio37",
};

static const char * const blsp_spi4_groups[] = {
	"gpio37", "gpio38", "gpio117", "gpio118",
};

static const char * const nfc_dwl_groups[] = {
	"gpio38",
};

static const char * const audio_ts_groups[] = {
	"gpio38",
};

static const char * const rgb_data4_groups[] = {
	"gpio39", "gpio45",
};

static const char * const spi_lcd_groups[] = {
	"gpio39", "gpio40",
};

static const char * const blsp_uart_tx_b2_groups[] = {
	"gpio39",
};

static const char * const gcc_gp3_clk_a_groups[] = {
	"gpio39",
};

static const char * const rgb_data5_groups[] = {
	"gpio40", "gpio46",
};

static const char * const blsp_uart_rx_b2_groups[] = {
	"gpio40",
};

static const char * const blsp_i2c_sda_b2_groups[] = {
	"gpio41",
};

static const char * const blsp_i2c_scl_b2_groups[] = {
	"gpio42",
};

static const char * const pwm_led11_groups[] = {
	"gpio43",
};

static const char * const i2s_3_data0_a_groups[] = {
	"gpio106",
};

static const char * const ebi2_lcd_groups[] = {
	"gpio106", "gpio107", "gpio108", "gpio109",
};

static const char * const i2s_3_data1_a_groups[] = {
	"gpio107",
};

static const char * const i2s_3_data2_a_groups[] = {
	"gpio108",
};

static const char * const atest_char_groups[] = {
	"gpio108",
};

static const char * const pwm_led3_groups[] = {
	"gpio108",
};

static const char * const i2s_3_data3_a_groups[] = {
	"gpio109",
};

static const char * const pwm_led4_groups[] = {
	"gpio109",
};

static const char * const i2s_4_groups[] = {
	"gpio110", "gpio111", "gpio111", "gpio112", "gpio112", "gpio113",
	"gpio113", "gpio114", "gpio114", "gpio115", "gpio115", "gpio116",
};

static const char * const ebi2_a_groups[] = {
	"gpio110",
};

static const char * const dsd_clk_b_groups[] = {
	"gpio110",
};

static const char * const pwm_led5_groups[] = {
	"gpio110",
};

static const char * const pwm_led6_groups[] = {
	"gpio111",
};

static const char * const pwm_led7_groups[] = {
	"gpio112",
};

static const char * const pwm_led8_groups[] = {
	"gpio113",
};

static const char * const pwm_led24_groups[] = {
	"gpio114",
};

static const char * const spkr_dac0_groups[] = {
	"gpio116",
};

static const char * const blsp_i2c4_groups[] = {
	"gpio117", "gpio118",
};

static const char * const pwm_led9_groups[] = {
	"gpio117",
};

static const char * const pwm_led10_groups[] = {
	"gpio118",
};

static const char * const spdifrx_opt_groups[] = {
	"gpio119",
};

static const char * const pwm_led12_groups[] = {
	"gpio44",
};

static const char * const pwm_led13_groups[] = {
	"gpio45",
};

static const char * const pwm_led14_groups[] = {
	"gpio46",
};

static const char * const wlan1_adc1_groups[] = {
	"gpio46",
};

static const char * const rgb_data_b0_groups[] = {
	"gpio47",
};

static const char * const pwm_led15_groups[] = {
	"gpio47",
};

static const char * const blsp_spi_mosi_b1_groups[] = {
	"gpio47",
};

static const char * const wlan1_adc0_groups[] = {
	"gpio47",
};

static const char * const rgb_data_b1_groups[] = {
	"gpio48",
};

static const char * const pwm_led16_groups[] = {
	"gpio48",
};

static const char * const blsp_spi_miso_b1_groups[] = {
	"gpio48",
};

static const char * const qdss_cti_trig_out_b0_groups[] = {
	"gpio48",
};

static const char * const wlan2_adc1_groups[] = {
	"gpio48",
};

static const char * const rgb_data_b2_groups[] = {
	"gpio49",
};

static const char * const pwm_led17_groups[] = {
	"gpio49",
};

static const char * const blsp_spi_cs_n_b1_groups[] = {
	"gpio49",
};

static const char * const wlan2_adc0_groups[] = {
	"gpio49",
};

static const char * const rgb_data_b3_groups[] = {
	"gpio50",
};

static const char * const pwm_led18_groups[] = {
	"gpio50",
};

static const char * const blsp_spi_clk_b1_groups[] = {
	"gpio50",
};

static const char * const rgb_data_b4_groups[] = {
	"gpio51",
};

static const char * const pwm_led19_groups[] = {
	"gpio51",
};

static const char * const ext_mclk1_b_groups[] = {
	"gpio51",
};

static const char * const qdss_traceclk_a_groups[] = {
	"gpio51",
};

static const char * const rgb_data_b5_groups[] = {
	"gpio52",
};

static const char * const pwm_led20_groups[] = {
	"gpio52",
};

static const char * const atest_char3_groups[] = {
	"gpio52",
};

static const char * const i2s_3_sck_b_groups[] = {
	"gpio52",
};

static const char * const ldo_update_groups[] = {
	"gpio52",
};

static const char * const bimc_dte0_groups[] = {
	"gpio52", "gpio54",
};

static const char * const rgb_hsync_groups[] = {
	"gpio53",
};

static const char * const pwm_led21_groups[] = {
	"gpio53",
};

static const char * const i2s_3_ws_b_groups[] = {
	"gpio53",
};

static const char * const dbg_out_groups[] = {
	"gpio53",
};

static const char * const rgb_vsync_groups[] = {
	"gpio54",
};

static const char * const i2s_3_data0_b_groups[] = {
	"gpio54",
};

static const char * const ldo_en_groups[] = {
	"gpio54",
};

static const char * const hdmi_dtest_groups[] = {
	"gpio54",
};

static const char * const rgb_de_groups[] = {
	"gpio55",
};

static const char * const i2s_3_data1_b_groups[] = {
	"gpio55",
};

static const char * const hdmi_lbk9_groups[] = {
	"gpio55",
};

static const char * const rgb_clk_groups[] = {
	"gpio56",
};

static const char * const atest_char1_groups[] = {
	"gpio56",
};

static const char * const i2s_3_data2_b_groups[] = {
	"gpio56",
};

static const char * const ebi_cdc_groups[] = {
	"gpio56", "gpio58", "gpio106", "gpio107", "gpio108", "gpio111",
};

static const char * const hdmi_lbk8_groups[] = {
	"gpio56",
};

static const char * const rgb_mdp_groups[] = {
	"gpio57",
};

static const char * const atest_char0_groups[] = {
	"gpio57",
};

static const char * const i2s_3_data3_b_groups[] = {
	"gpio57",
};

static const char * const hdmi_lbk7_groups[] = {
	"gpio57",
};

static const char * const rgb_data_b6_groups[] = {
	"gpio58",
};

static const char * const rgb_data_b7_groups[] = {
	"gpio59",
};

static const char * const hdmi_lbk6_groups[] = {
	"gpio59",
};

static const char * const rgmii_int_groups[] = {
	"gpio61",
};

static const char * const cri_trng1_groups[] = {
	"gpio61",
};

static const char * const rgmii_wol_groups[] = {
	"gpio62",
};

static const char * const cri_trng0_groups[] = {
	"gpio62",
};

static const char * const gcc_tlmm_groups[] = {
	"gpio62",
};

static const char * const rgmii_ck_groups[] = {
	"gpio63", "gpio69",
};

static const char * const rgmii_tx_groups[] = {
	"gpio64", "gpio65", "gpio66", "gpio67",
};

static const char * const hdmi_lbk5_groups[] = {
	"gpio64",
};

static const char * const hdmi_pixel_groups[] = {
	"gpio65",
};

static const char * const hdmi_rcv_groups[] = {
	"gpio66",
};

static const char * const hdmi_lbk4_groups[] = {
	"gpio67",
};

static const char * const rgmii_ctl_groups[] = {
	"gpio68", "gpio74",
};

static const char * const ext_lpass_groups[] = {
	"gpio69",
};

static const char * const rgmii_rx_groups[] = {
	"gpio70", "gpio71", "gpio72", "gpio73",
};

static const char * const cri_trng_groups[] = {
	"gpio70",
};

static const char * const hdmi_lbk3_groups[] = {
	"gpio71",
};

static const char * const hdmi_lbk2_groups[] = {
	"gpio72",
};

static const char * const qdss_cti_trig_out_b1_groups[] = {
	"gpio73",
};

static const char * const rgmii_mdio_groups[] = {
	"gpio75",
};

static const char * const hdmi_lbk1_groups[] = {
	"gpio75",
};

static const char * const rgmii_mdc_groups[] = {
	"gpio76",
};

static const char * const hdmi_lbk0_groups[] = {
	"gpio76",
};

static const char * const ir_in_groups[] = {
	"gpio77",
};

static const char * const wsa_en_groups[] = {
	"gpio77",
};

static const char * const rgb_data6_groups[] = {
	"gpio78", "gpio80",
};

static const char * const rgb_data7_groups[] = {
	"gpio79", "gpio81",
};

static const char * const atest_char2_groups[] = {
	"gpio80",
};

static const char * const ebi_ch0_groups[] = {
	"gpio81",
};

static const char * const blsp_uart3_groups[] = {
	"gpio82", "gpio83", "gpio84", "gpio85",
};

static const char * const blsp_spi3_groups[] = {
	"gpio82", "gpio83", "gpio84", "gpio85",
};

static const char * const sd_write_groups[] = {
	"gpio82",
};

static const char * const blsp_i2c3_groups[] = {
	"gpio84", "gpio85",
};

static const char * const gcc_gp1_clk_a_groups[] = {
	"gpio84",
};

static const char * const qdss_cti_trig_in_b1_groups[] = {
	"gpio84",
};

static const char * const gcc_gp2_clk_a_groups[] = {
	"gpio85",
};

static const char * const ext_mclk0_groups[] = {
	"gpio86",
};

static const char * const mclk_in1_groups[] = {
	"gpio86",
};

static const char * const i2s_1_groups[] = {
	"gpio87", "gpio88", "gpio88", "gpio89", "gpio89", "gpio90", "gpio90",
	"gpio91", "gpio91", "gpio92", "gpio92", "gpio93", "gpio93", "gpio94",
	"gpio94", "gpio95", "gpio95", "gpio96",
};

static const char * const dsd_clk_a_groups[] = {
	"gpio87",
};

static const char * const qdss_cti_trig_in_a1_groups[] = {
	"gpio92",
};

static const char * const rgmi_dll1_groups[] = {
	"gpio92",
};

static const char * const pwm_led22_groups[] = {
	"gpio93",
};

static const char * const pwm_led23_groups[] = {
	"gpio94",
};

static const char * const qdss_cti_trig_out_a0_groups[] = {
	"gpio94",
};

static const char * const rgmi_dll2_groups[] = {
	"gpio94",
};

static const char * const pwm_led1_groups[] = {
	"gpio95",
};

static const char * const qdss_cti_trig_out_a1_groups[] = {
	"gpio95",
};

static const char * const pwm_led2_groups[] = {
	"gpio96",
};

static const char * const i2s_2_groups[] = {
	"gpio97", "gpio98", "gpio99", "gpio100", "gpio101", "gpio102",
};

static const char * const pll_bist_groups[] = {
	"gpio100",
};

static const char * const ext_mclk1_a_groups[] = {
	"gpio103",
};

static const char * const mclk_in2_groups[] = {
	"gpio103",
};

static const char * const bimc_dte1_groups[] = {
	"gpio103", "gpio109",
};

static const char * const i2s_3_sck_a_groups[] = {
	"gpio104",
};

static const char * const i2s_3_ws_a_groups[] = {
	"gpio105",
};

static const struct pinfunction qcs404_functions[] = {
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(hdmi_tx),
	MSM_PIN_FUNCTION(hdmi_ddc),
	MSM_PIN_FUNCTION(blsp_uart_tx_a2),
	MSM_PIN_FUNCTION(blsp_spi2),
	MSM_PIN_FUNCTION(m_voc),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a0),
	MSM_PIN_FUNCTION(blsp_uart_rx_a2),
	MSM_PIN_FUNCTION(qdss_tracectl_a),
	MSM_PIN_FUNCTION(blsp_uart2),
	MSM_PIN_FUNCTION(aud_cdc),
	MSM_PIN_FUNCTION(blsp_i2c_sda_a2),
	MSM_PIN_FUNCTION(qdss_tracedata_a),
	MSM_PIN_FUNCTION(blsp_i2c_scl_a2),
	MSM_PIN_FUNCTION(qdss_tracectl_b),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b0),
	MSM_PIN_FUNCTION(blsp_uart1),
	MSM_PIN_FUNCTION(blsp_spi_mosi_a1),
	MSM_PIN_FUNCTION(blsp_spi_miso_a1),
	MSM_PIN_FUNCTION(qdss_tracedata_b),
	MSM_PIN_FUNCTION(blsp_i2c1),
	MSM_PIN_FUNCTION(blsp_spi_cs_n_a1),
	MSM_PIN_FUNCTION(gcc_plltest),
	MSM_PIN_FUNCTION(blsp_spi_clk_a1),
	MSM_PIN_FUNCTION(rgb_data0),
	MSM_PIN_FUNCTION(blsp_uart5),
	MSM_PIN_FUNCTION(blsp_spi5),
	MSM_PIN_FUNCTION(adsp_ext),
	MSM_PIN_FUNCTION(rgb_data1),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(rgb_data2),
	MSM_PIN_FUNCTION(blsp_i2c5),
	MSM_PIN_FUNCTION(gcc_gp1_clk_b),
	MSM_PIN_FUNCTION(rgb_data3),
	MSM_PIN_FUNCTION(gcc_gp2_clk_b),
	MSM_PIN_FUNCTION(blsp_spi0),
	MSM_PIN_FUNCTION(blsp_uart0),
	MSM_PIN_FUNCTION(gcc_gp3_clk_b),
	MSM_PIN_FUNCTION(blsp_i2c0),
	MSM_PIN_FUNCTION(qdss_traceclk_b),
	MSM_PIN_FUNCTION(pcie_clk),
	MSM_PIN_FUNCTION(nfc_irq),
	MSM_PIN_FUNCTION(blsp_spi4),
	MSM_PIN_FUNCTION(nfc_dwl),
	MSM_PIN_FUNCTION(audio_ts),
	MSM_PIN_FUNCTION(rgb_data4),
	MSM_PIN_FUNCTION(spi_lcd),
	MSM_PIN_FUNCTION(blsp_uart_tx_b2),
	MSM_PIN_FUNCTION(gcc_gp3_clk_a),
	MSM_PIN_FUNCTION(rgb_data5),
	MSM_PIN_FUNCTION(blsp_uart_rx_b2),
	MSM_PIN_FUNCTION(blsp_i2c_sda_b2),
	MSM_PIN_FUNCTION(blsp_i2c_scl_b2),
	MSM_PIN_FUNCTION(pwm_led11),
	MSM_PIN_FUNCTION(i2s_3_data0_a),
	MSM_PIN_FUNCTION(ebi2_lcd),
	MSM_PIN_FUNCTION(i2s_3_data1_a),
	MSM_PIN_FUNCTION(i2s_3_data2_a),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(pwm_led3),
	MSM_PIN_FUNCTION(i2s_3_data3_a),
	MSM_PIN_FUNCTION(pwm_led4),
	MSM_PIN_FUNCTION(i2s_4),
	MSM_PIN_FUNCTION(ebi2_a),
	MSM_PIN_FUNCTION(dsd_clk_b),
	MSM_PIN_FUNCTION(pwm_led5),
	MSM_PIN_FUNCTION(pwm_led6),
	MSM_PIN_FUNCTION(pwm_led7),
	MSM_PIN_FUNCTION(pwm_led8),
	MSM_PIN_FUNCTION(pwm_led24),
	MSM_PIN_FUNCTION(spkr_dac0),
	MSM_PIN_FUNCTION(blsp_i2c4),
	MSM_PIN_FUNCTION(pwm_led9),
	MSM_PIN_FUNCTION(pwm_led10),
	MSM_PIN_FUNCTION(spdifrx_opt),
	MSM_PIN_FUNCTION(pwm_led12),
	MSM_PIN_FUNCTION(pwm_led13),
	MSM_PIN_FUNCTION(pwm_led14),
	MSM_PIN_FUNCTION(wlan1_adc1),
	MSM_PIN_FUNCTION(rgb_data_b0),
	MSM_PIN_FUNCTION(pwm_led15),
	MSM_PIN_FUNCTION(blsp_spi_mosi_b1),
	MSM_PIN_FUNCTION(wlan1_adc0),
	MSM_PIN_FUNCTION(rgb_data_b1),
	MSM_PIN_FUNCTION(pwm_led16),
	MSM_PIN_FUNCTION(blsp_spi_miso_b1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b0),
	MSM_PIN_FUNCTION(wlan2_adc1),
	MSM_PIN_FUNCTION(rgb_data_b2),
	MSM_PIN_FUNCTION(pwm_led17),
	MSM_PIN_FUNCTION(blsp_spi_cs_n_b1),
	MSM_PIN_FUNCTION(wlan2_adc0),
	MSM_PIN_FUNCTION(rgb_data_b3),
	MSM_PIN_FUNCTION(pwm_led18),
	MSM_PIN_FUNCTION(blsp_spi_clk_b1),
	MSM_PIN_FUNCTION(rgb_data_b4),
	MSM_PIN_FUNCTION(pwm_led19),
	MSM_PIN_FUNCTION(ext_mclk1_b),
	MSM_PIN_FUNCTION(qdss_traceclk_a),
	MSM_PIN_FUNCTION(rgb_data_b5),
	MSM_PIN_FUNCTION(pwm_led20),
	MSM_PIN_FUNCTION(atest_char3),
	MSM_PIN_FUNCTION(i2s_3_sck_b),
	MSM_PIN_FUNCTION(ldo_update),
	MSM_PIN_FUNCTION(bimc_dte0),
	MSM_PIN_FUNCTION(rgb_hsync),
	MSM_PIN_FUNCTION(pwm_led21),
	MSM_PIN_FUNCTION(i2s_3_ws_b),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(rgb_vsync),
	MSM_PIN_FUNCTION(i2s_3_data0_b),
	MSM_PIN_FUNCTION(ldo_en),
	MSM_PIN_FUNCTION(hdmi_dtest),
	MSM_PIN_FUNCTION(rgb_de),
	MSM_PIN_FUNCTION(i2s_3_data1_b),
	MSM_PIN_FUNCTION(hdmi_lbk9),
	MSM_PIN_FUNCTION(rgb_clk),
	MSM_PIN_FUNCTION(atest_char1),
	MSM_PIN_FUNCTION(i2s_3_data2_b),
	MSM_PIN_FUNCTION(ebi_cdc),
	MSM_PIN_FUNCTION(hdmi_lbk8),
	MSM_PIN_FUNCTION(rgb_mdp),
	MSM_PIN_FUNCTION(atest_char0),
	MSM_PIN_FUNCTION(i2s_3_data3_b),
	MSM_PIN_FUNCTION(hdmi_lbk7),
	MSM_PIN_FUNCTION(rgb_data_b6),
	MSM_PIN_FUNCTION(rgb_data_b7),
	MSM_PIN_FUNCTION(hdmi_lbk6),
	MSM_PIN_FUNCTION(rgmii_int),
	MSM_PIN_FUNCTION(cri_trng1),
	MSM_PIN_FUNCTION(rgmii_wol),
	MSM_PIN_FUNCTION(cri_trng0),
	MSM_PIN_FUNCTION(gcc_tlmm),
	MSM_PIN_FUNCTION(rgmii_ck),
	MSM_PIN_FUNCTION(rgmii_tx),
	MSM_PIN_FUNCTION(hdmi_lbk5),
	MSM_PIN_FUNCTION(hdmi_pixel),
	MSM_PIN_FUNCTION(hdmi_rcv),
	MSM_PIN_FUNCTION(hdmi_lbk4),
	MSM_PIN_FUNCTION(rgmii_ctl),
	MSM_PIN_FUNCTION(ext_lpass),
	MSM_PIN_FUNCTION(rgmii_rx),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(hdmi_lbk3),
	MSM_PIN_FUNCTION(hdmi_lbk2),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b1),
	MSM_PIN_FUNCTION(rgmii_mdio),
	MSM_PIN_FUNCTION(hdmi_lbk1),
	MSM_PIN_FUNCTION(rgmii_mdc),
	MSM_PIN_FUNCTION(hdmi_lbk0),
	MSM_PIN_FUNCTION(ir_in),
	MSM_PIN_FUNCTION(wsa_en),
	MSM_PIN_FUNCTION(rgb_data6),
	MSM_PIN_FUNCTION(rgb_data7),
	MSM_PIN_FUNCTION(atest_char2),
	MSM_PIN_FUNCTION(ebi_ch0),
	MSM_PIN_FUNCTION(blsp_uart3),
	MSM_PIN_FUNCTION(blsp_spi3),
	MSM_PIN_FUNCTION(sd_write),
	MSM_PIN_FUNCTION(blsp_i2c3),
	MSM_PIN_FUNCTION(gcc_gp1_clk_a),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b1),
	MSM_PIN_FUNCTION(gcc_gp2_clk_a),
	MSM_PIN_FUNCTION(ext_mclk0),
	MSM_PIN_FUNCTION(mclk_in1),
	MSM_PIN_FUNCTION(i2s_1),
	MSM_PIN_FUNCTION(dsd_clk_a),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a1),
	MSM_PIN_FUNCTION(rgmi_dll1),
	MSM_PIN_FUNCTION(pwm_led22),
	MSM_PIN_FUNCTION(pwm_led23),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a0),
	MSM_PIN_FUNCTION(rgmi_dll2),
	MSM_PIN_FUNCTION(pwm_led1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a1),
	MSM_PIN_FUNCTION(pwm_led2),
	MSM_PIN_FUNCTION(i2s_2),
	MSM_PIN_FUNCTION(pll_bist),
	MSM_PIN_FUNCTION(ext_mclk1_a),
	MSM_PIN_FUNCTION(mclk_in2),
	MSM_PIN_FUNCTION(bimc_dte1),
	MSM_PIN_FUNCTION(i2s_3_sck_a),
	MSM_PIN_FUNCTION(i2s_3_ws_a),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup qcs404_groups[] = {
	[0] = PINGROUP(0, SOUTH, _, _, _, _, _, _, _, _, _),
	[1] = PINGROUP(1, SOUTH, _, _, _, _, _, _, _, _, _),
	[2] = PINGROUP(2, SOUTH, _, _, _, _, _, _, _, _, _),
	[3] = PINGROUP(3, SOUTH, _, _, _, _, _, _, _, _, _),
	[4] = PINGROUP(4, SOUTH, _, _, _, _, _, _, _, _, _),
	[5] = PINGROUP(5, SOUTH, _, _, _, _, _, _, _, _, _),
	[6] = PINGROUP(6, SOUTH, _, _, _, _, _, _, _, _, _),
	[7] = PINGROUP(7, SOUTH, _, _, _, _, _, _, _, _, _),
	[8] = PINGROUP(8, SOUTH, _, _, _, _, _, _, _, _, _),
	[9] = PINGROUP(9, SOUTH, _, _, _, _, _, _, _, _, _),
	[10] = PINGROUP(10, SOUTH, _, _, _, _, _, _, _, _, _),
	[11] = PINGROUP(11, SOUTH, _, _, _, _, _, _, _, _, _),
	[12] = PINGROUP(12, SOUTH, _, _, _, _, _, _, _, _, _),
	[13] = PINGROUP(13, SOUTH, _, _, _, _, _, _, _, _, _),
	[14] = PINGROUP(14, SOUTH, hdmi_tx, _, _, _, _, _, _, _, _),
	[15] = PINGROUP(15, SOUTH, hdmi_ddc, _, _, _, _, _, _, _, _),
	[16] = PINGROUP(16, SOUTH, hdmi_ddc, _, _, _, _, _, _, _, _),
	[17] = PINGROUP(17, NORTH, blsp_uart_tx_a2, blsp_spi2, m_voc, _, _, _, _, _, _),
	[18] = PINGROUP(18, NORTH, blsp_uart_rx_a2, blsp_spi2, _, _, _, _, _, qdss_tracectl_a, _),
	[19] = PINGROUP(19, NORTH, blsp_uart2, aud_cdc, blsp_i2c_sda_a2, blsp_spi2, _, qdss_tracedata_a, _, _, _),
	[20] = PINGROUP(20, NORTH, blsp_uart2, aud_cdc, blsp_i2c_scl_a2, blsp_spi2, _, _, _, _, _),
	[21] = PINGROUP(21, SOUTH, m_voc, _, _, _, _, _, _, _, qdss_cti_trig_in_b0),
	[22] = PINGROUP(22, NORTH, blsp_uart1, blsp_spi_mosi_a1, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, NORTH, blsp_uart1, blsp_spi_miso_a1, _, _, _, _, _, qdss_tracedata_b, _),
	[24] = PINGROUP(24, NORTH, blsp_uart1, blsp_i2c1, blsp_spi_cs_n_a1, gcc_plltest, _, _, _, _, _),
	[25] = PINGROUP(25, NORTH, blsp_uart1, blsp_i2c1, blsp_spi_clk_a1, gcc_plltest, _, _, _, _, _),
	[26] = PINGROUP(26, EAST, rgb_data0, blsp_uart5, blsp_spi5, adsp_ext, _, _, _, _, _),
	[27] = PINGROUP(27, EAST, rgb_data1, blsp_uart5, blsp_spi5, prng_rosc, _, _, _, _, _),
	[28] = PINGROUP(28, EAST, rgb_data2, blsp_uart5, blsp_i2c5, blsp_spi5, gcc_gp1_clk_b, _, _, _, _),
	[29] = PINGROUP(29, EAST, rgb_data3, blsp_uart5, blsp_i2c5, blsp_spi5, gcc_gp2_clk_b, _, _, _, _),
	[30] = PINGROUP(30, NORTH, blsp_spi0, blsp_uart0, gcc_gp3_clk_b, _, _, _, _, _, _),
	[31] = PINGROUP(31, NORTH, blsp_spi0, blsp_uart0, _, _, _, _, _, _, _),
	[32] = PINGROUP(32, NORTH, blsp_spi0, blsp_uart0, blsp_i2c0, _, _, _, _, _, _),
	[33] = PINGROUP(33, NORTH, blsp_spi0, blsp_uart0, blsp_i2c0, _, _, _, _, _, _),
	[34] = PINGROUP(34, SOUTH, _, qdss_traceclk_b, _, _, _, _, _, _, _),
	[35] = PINGROUP(35, SOUTH, pcie_clk, _, qdss_tracedata_b, _, _, _, _, _, _),
	[36] = PINGROUP(36, NORTH, _, _, _, _, _, _, qdss_tracedata_a, _, _),
	[37] = PINGROUP(37, NORTH, nfc_irq, blsp_spi4, _, _, _, _, _, _, _),
	[38] = PINGROUP(38, NORTH, nfc_dwl, blsp_spi4, audio_ts, _, _, _, _, _, _),
	[39] = PINGROUP(39, EAST, rgb_data4, spi_lcd, blsp_uart_tx_b2, gcc_gp3_clk_a, qdss_tracedata_a, _, _, _, _),
	[40] = PINGROUP(40, EAST, rgb_data5, spi_lcd, blsp_uart_rx_b2, _, qdss_tracedata_b, _, _, _, _),
	[41] = PINGROUP(41, EAST, rgb_data0, blsp_i2c_sda_b2, _, qdss_tracedata_b, _, _, _, _, _),
	[42] = PINGROUP(42, EAST, rgb_data1, blsp_i2c_scl_b2, _, _, _, _, _, qdss_tracedata_a, _),
	[43] = PINGROUP(43, EAST, rgb_data2, pwm_led11, _, _, _, _, _, _, _),
	[44] = PINGROUP(44, EAST, rgb_data3, pwm_led12, blsp_spi5, _, _, _, _, _, _),
	[45] = PINGROUP(45, EAST, rgb_data4, pwm_led13, blsp_spi5, qdss_tracedata_b, _, _, _, _, _),
	[46] = PINGROUP(46, EAST, rgb_data5, pwm_led14, blsp_spi5, qdss_tracedata_b, _, wlan1_adc1, _, _, _),
	[47] = PINGROUP(47, EAST, rgb_data_b0, pwm_led15, blsp_spi_mosi_b1, qdss_tracedata_b, _, wlan1_adc0, _, _, _),
	[48] = PINGROUP(48, EAST, rgb_data_b1, pwm_led16, blsp_spi_miso_b1, _, qdss_cti_trig_out_b0, _, wlan2_adc1, _, _),
	[49] = PINGROUP(49, EAST, rgb_data_b2, pwm_led17, blsp_spi_cs_n_b1, _, qdss_tracedata_b, _, wlan2_adc0, _, _),
	[50] = PINGROUP(50, EAST, rgb_data_b3, pwm_led18, blsp_spi_clk_b1, qdss_tracedata_b, _, _, _, _, _),
	[51] = PINGROUP(51, EAST, rgb_data_b4, pwm_led19, ext_mclk1_b, qdss_traceclk_a, _, _, _, _, _),
	[52] = PINGROUP(52, EAST, rgb_data_b5, pwm_led20, atest_char3, i2s_3_sck_b, ldo_update, bimc_dte0, _, _, _),
	[53] = PINGROUP(53, EAST, rgb_hsync, pwm_led21, i2s_3_ws_b, dbg_out, _, _, _, _, _),
	[54] = PINGROUP(54, EAST, rgb_vsync, i2s_3_data0_b, ldo_en, bimc_dte0, _, hdmi_dtest, _, _, _),
	[55] = PINGROUP(55, EAST, rgb_de, i2s_3_data1_b, _, qdss_tracedata_b, _, hdmi_lbk9, _, _, _),
	[56] = PINGROUP(56, EAST, rgb_clk, atest_char1, i2s_3_data2_b, ebi_cdc, _, hdmi_lbk8, _, _, _),
	[57] = PINGROUP(57, EAST, rgb_mdp, atest_char0, i2s_3_data3_b, _, hdmi_lbk7, _, _, _, _),
	[58] = PINGROUP(58, EAST, rgb_data_b6, _, ebi_cdc, _, _, _, _, _, _),
	[59] = PINGROUP(59, EAST, rgb_data_b7, _, hdmi_lbk6, _, _, _, _, _, _),
	[60] = PINGROUP(60, NORTH, _, _, _, _, _, _, _, _, _),
	[61] = PINGROUP(61, NORTH, rgmii_int, cri_trng1, qdss_tracedata_b, _, _, _, _, _, _),
	[62] = PINGROUP(62, NORTH, rgmii_wol, cri_trng0, qdss_tracedata_b, gcc_tlmm, _, _, _, _, _),
	[63] = PINGROUP(63, NORTH, rgmii_ck, _, _, _, _, _, _, _, _),
	[64] = PINGROUP(64, NORTH, rgmii_tx, _, hdmi_lbk5, _, _, _, _, _, _),
	[65] = PINGROUP(65, NORTH, rgmii_tx, _, hdmi_pixel, _, _, _, _, _, _),
	[66] = PINGROUP(66, NORTH, rgmii_tx, _, hdmi_rcv, _, _, _, _, _, _),
	[67] = PINGROUP(67, NORTH, rgmii_tx, _, hdmi_lbk4, _, _, _, _, _, _),
	[68] = PINGROUP(68, NORTH, rgmii_ctl, _, _, _, _, _, _, _, _),
	[69] = PINGROUP(69, NORTH, rgmii_ck, ext_lpass, _, _, _, _, _, _, _),
	[70] = PINGROUP(70, NORTH, rgmii_rx, cri_trng, _, _, _, _, _, _, _),
	[71] = PINGROUP(71, NORTH, rgmii_rx, _, hdmi_lbk3, _, _, _, _, _, _),
	[72] = PINGROUP(72, NORTH, rgmii_rx, _, hdmi_lbk2, _, _, _, _, _, _),
	[73] = PINGROUP(73, NORTH, rgmii_rx, _, _, _, _, qdss_cti_trig_out_b1, _, _, _),
	[74] = PINGROUP(74, NORTH, rgmii_ctl, _, _, _, _, _, _, _, _),
	[75] = PINGROUP(75, NORTH, rgmii_mdio, _, hdmi_lbk1, _, _, _, _, _, _),
	[76] = PINGROUP(76, NORTH, rgmii_mdc, _, _, _, _, _, hdmi_lbk0, _, _),
	[77] = PINGROUP(77, NORTH, ir_in, wsa_en, _, _, _, _, _, _, _),
	[78] = PINGROUP(78, EAST, rgb_data6, _, _, _, _, _, _, _, _),
	[79] = PINGROUP(79, EAST, rgb_data7, _, _, _, _, _, _, _, _),
	[80] = PINGROUP(80, EAST, rgb_data6, atest_char2, _, _, _, _, _, _, _),
	[81] = PINGROUP(81, EAST, rgb_data7, ebi_ch0, _, _, _, _, _, _, _),
	[82] = PINGROUP(82, NORTH, blsp_uart3, blsp_spi3, sd_write, _, _, _, _, _, qdss_tracedata_a),
	[83] = PINGROUP(83, NORTH, blsp_uart3, blsp_spi3, _, _, _, _, qdss_tracedata_a, _, _),
	[84] = PINGROUP(84, NORTH, blsp_uart3, blsp_i2c3, blsp_spi3, gcc_gp1_clk_a, qdss_cti_trig_in_b1, _, _, _, _),
	[85] = PINGROUP(85, NORTH, blsp_uart3, blsp_i2c3, blsp_spi3, gcc_gp2_clk_a, qdss_tracedata_b, _, _, _, _),
	[86] = PINGROUP(86, EAST, ext_mclk0, mclk_in1, _, _, _, _, _, _, _),
	[87] = PINGROUP(87, EAST, i2s_1, dsd_clk_a, _, _, _, _, _, _, _),
	[88] = PINGROUP(88, EAST, i2s_1, i2s_1, _, _, _, _, _, _, _),
	[89] = PINGROUP(89, EAST, i2s_1, i2s_1, _, _, _, _, _, _, qdss_tracedata_b),
	[90] = PINGROUP(90, EAST, i2s_1, i2s_1, _, _, _, _, _, _, _),
	[91] = PINGROUP(91, EAST, i2s_1, i2s_1, _, _, _, _, _, _, _),
	[92] = PINGROUP(92, EAST, i2s_1, i2s_1, _, _, _, _, _, qdss_cti_trig_in_a1, _),
	[93] = PINGROUP(93, EAST, i2s_1, pwm_led22, i2s_1, _, _, _, _, _, qdss_tracedata_b),
	[94] = PINGROUP(94, EAST, i2s_1, pwm_led23, i2s_1, _, qdss_cti_trig_out_a0, _, rgmi_dll2, _, _),
	[95] = PINGROUP(95, EAST, i2s_1, pwm_led1, i2s_1, _, qdss_cti_trig_out_a1, _, _, _, _),
	[96] = PINGROUP(96, EAST, i2s_1, pwm_led2, _, _, _, _, _, _, _),
	[97] = PINGROUP(97, EAST, i2s_2, _, _, _, _, _, _, _, _),
	[98] = PINGROUP(98, EAST, i2s_2, _, _, _, _, _, _, _, _),
	[99] = PINGROUP(99, EAST, i2s_2, _, _, _, _, _, _, _, _),
	[100] = PINGROUP(100, EAST, i2s_2, pll_bist, _, _, _, _, _, _, _),
	[101] = PINGROUP(101, EAST, i2s_2, _, _, _, _, _, _, _, _),
	[102] = PINGROUP(102, EAST, i2s_2, _, _, _, _, _, _, _, _),
	[103] = PINGROUP(103, EAST, ext_mclk1_a, mclk_in2, bimc_dte1, _, _, _, _, _, _),
	[104] = PINGROUP(104, EAST, i2s_3_sck_a, _, _, _, _, _, _, _, _),
	[105] = PINGROUP(105, EAST, i2s_3_ws_a, _, _, _, _, _, _, _, _),
	[106] = PINGROUP(106, EAST, i2s_3_data0_a, ebi2_lcd, _, _, ebi_cdc, _, _, _, _),
	[107] = PINGROUP(107, EAST, i2s_3_data1_a, ebi2_lcd, _, _, ebi_cdc, _, _, _, _),
	[108] = PINGROUP(108, EAST, i2s_3_data2_a, ebi2_lcd, atest_char, pwm_led3, ebi_cdc, _, _, _, _),
	[109] = PINGROUP(109, EAST, i2s_3_data3_a, ebi2_lcd, pwm_led4, bimc_dte1, _, _, _, _, _),
	[110] = PINGROUP(110, EAST, i2s_4, ebi2_a, dsd_clk_b, pwm_led5, _, _, _, _, _),
	[111] = PINGROUP(111, EAST, i2s_4, i2s_4, pwm_led6, ebi_cdc, _, _, _, _, _),
	[112] = PINGROUP(112, EAST, i2s_4, i2s_4, pwm_led7, _, _, _, _, _, _),
	[113] = PINGROUP(113, EAST, i2s_4, i2s_4, pwm_led8, _, _, _, _, _, _),
	[114] = PINGROUP(114, EAST, i2s_4, i2s_4, pwm_led24, _, _, _, _, _, _),
	[115] = PINGROUP(115, EAST, i2s_4, i2s_4, _, _, _, _, _, _, _),
	[116] = PINGROUP(116, EAST, i2s_4, spkr_dac0, _, _, _, _, _, _, _),
	[117] = PINGROUP(117, NORTH, blsp_i2c4, blsp_spi4, pwm_led9, _, _, _, _, _, _),
	[118] = PINGROUP(118, NORTH, blsp_i2c4, blsp_spi4, pwm_led10, _, _, _, _, _, _),
	[119] = PINGROUP(119, EAST, spdifrx_opt, _, _, _, _, _, _, _, _),
	[120] = SDC_QDSD_PINGROUP(sdc1_rclk, 0xc2000, 15, 0),
	[121] = SDC_QDSD_PINGROUP(sdc1_clk, 0xc2000, 13, 6),
	[122] = SDC_QDSD_PINGROUP(sdc1_cmd, 0xc2000, 11, 3),
	[123] = SDC_QDSD_PINGROUP(sdc1_data, 0xc2000, 9, 0),
	[124] = SDC_QDSD_PINGROUP(sdc2_clk, 0xc3000, 14, 6),
	[125] = SDC_QDSD_PINGROUP(sdc2_cmd, 0xc3000, 11, 3),
	[126] = SDC_QDSD_PINGROUP(sdc2_data, 0xc3000, 9, 0),
};

static const struct msm_pinctrl_soc_data qcs404_pinctrl = {
	.pins = qcs404_pins,
	.npins = ARRAY_SIZE(qcs404_pins),
	.functions = qcs404_functions,
	.nfunctions = ARRAY_SIZE(qcs404_functions),
	.groups = qcs404_groups,
	.ngroups = ARRAY_SIZE(qcs404_groups),
	.ngpios = 120,
	.tiles = qcs404_tiles,
	.ntiles = ARRAY_SIZE(qcs404_tiles),
};

static int qcs404_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &qcs404_pinctrl);
}

static const struct of_device_id qcs404_pinctrl_of_match[] = {
	{ .compatible = "qcom,qcs404-pinctrl", },
	{ },
};

static struct platform_driver qcs404_pinctrl_driver = {
	.driver = {
		.name = "qcs404-pinctrl",
		.of_match_table = qcs404_pinctrl_of_match,
	},
	.probe = qcs404_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init qcs404_pinctrl_init(void)
{
	return platform_driver_register(&qcs404_pinctrl_driver);
}
arch_initcall(qcs404_pinctrl_init);

static void __exit qcs404_pinctrl_exit(void)
{
	platform_driver_unregister(&qcs404_pinctrl_driver);
}
module_exit(qcs404_pinctrl_exit);

MODULE_DESCRIPTION("Qualcomm QCS404 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, qcs404_pinctrl_of_match);
