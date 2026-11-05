// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013, Sony Mobile Communications AB.
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2026 Muzaffer Kadir <muzafferkadir@mainlining.org>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

static const struct pinctrl_pin_desc msm8952_pins[] = {
	PINCTRL_PIN(0, "GPIO_0"),	PINCTRL_PIN(1, "GPIO_1"),
	PINCTRL_PIN(2, "GPIO_2"),	PINCTRL_PIN(3, "GPIO_3"),
	PINCTRL_PIN(4, "GPIO_4"),	PINCTRL_PIN(5, "GPIO_5"),
	PINCTRL_PIN(6, "GPIO_6"),	PINCTRL_PIN(7, "GPIO_7"),
	PINCTRL_PIN(8, "GPIO_8"),	PINCTRL_PIN(9, "GPIO_9"),
	PINCTRL_PIN(10, "GPIO_10"),	PINCTRL_PIN(11, "GPIO_11"),
	PINCTRL_PIN(12, "GPIO_12"),	PINCTRL_PIN(13, "GPIO_13"),
	PINCTRL_PIN(14, "GPIO_14"),	PINCTRL_PIN(15, "GPIO_15"),
	PINCTRL_PIN(16, "GPIO_16"),	PINCTRL_PIN(17, "GPIO_17"),
	PINCTRL_PIN(18, "GPIO_18"),	PINCTRL_PIN(19, "GPIO_19"),
	PINCTRL_PIN(20, "GPIO_20"),	PINCTRL_PIN(21, "GPIO_21"),
	PINCTRL_PIN(22, "GPIO_22"),	PINCTRL_PIN(23, "GPIO_23"),
	PINCTRL_PIN(24, "GPIO_24"),	PINCTRL_PIN(25, "GPIO_25"),
	PINCTRL_PIN(26, "GPIO_26"),	PINCTRL_PIN(27, "GPIO_27"),
	PINCTRL_PIN(28, "GPIO_28"),	PINCTRL_PIN(29, "GPIO_29"),
	PINCTRL_PIN(30, "GPIO_30"),	PINCTRL_PIN(31, "GPIO_31"),
	PINCTRL_PIN(32, "GPIO_32"),	PINCTRL_PIN(33, "GPIO_33"),
	PINCTRL_PIN(34, "GPIO_34"),	PINCTRL_PIN(35, "GPIO_35"),
	PINCTRL_PIN(36, "GPIO_36"),	PINCTRL_PIN(37, "GPIO_37"),
	PINCTRL_PIN(38, "GPIO_38"),	PINCTRL_PIN(39, "GPIO_39"),
	PINCTRL_PIN(40, "GPIO_40"),	PINCTRL_PIN(41, "GPIO_41"),
	PINCTRL_PIN(42, "GPIO_42"),	PINCTRL_PIN(43, "GPIO_43"),
	PINCTRL_PIN(44, "GPIO_44"),	PINCTRL_PIN(45, "GPIO_45"),
	PINCTRL_PIN(46, "GPIO_46"),	PINCTRL_PIN(47, "GPIO_47"),
	PINCTRL_PIN(48, "GPIO_48"),	PINCTRL_PIN(49, "GPIO_49"),
	PINCTRL_PIN(50, "GPIO_50"),	PINCTRL_PIN(51, "GPIO_51"),
	PINCTRL_PIN(52, "GPIO_52"),	PINCTRL_PIN(53, "GPIO_53"),
	PINCTRL_PIN(54, "GPIO_54"),	PINCTRL_PIN(55, "GPIO_55"),
	PINCTRL_PIN(56, "GPIO_56"),	PINCTRL_PIN(57, "GPIO_57"),
	PINCTRL_PIN(58, "GPIO_58"),	PINCTRL_PIN(59, "GPIO_59"),
	PINCTRL_PIN(60, "GPIO_60"),	PINCTRL_PIN(61, "GPIO_61"),
	PINCTRL_PIN(62, "GPIO_62"),	PINCTRL_PIN(63, "GPIO_63"),
	PINCTRL_PIN(64, "GPIO_64"),	PINCTRL_PIN(65, "GPIO_65"),
	PINCTRL_PIN(66, "GPIO_66"),	PINCTRL_PIN(67, "GPIO_67"),
	PINCTRL_PIN(68, "GPIO_68"),	PINCTRL_PIN(69, "GPIO_69"),
	PINCTRL_PIN(70, "GPIO_70"),	PINCTRL_PIN(71, "GPIO_71"),
	PINCTRL_PIN(72, "GPIO_72"),	PINCTRL_PIN(73, "GPIO_73"),
	PINCTRL_PIN(74, "GPIO_74"),	PINCTRL_PIN(75, "GPIO_75"),
	PINCTRL_PIN(76, "GPIO_76"),	PINCTRL_PIN(77, "GPIO_77"),
	PINCTRL_PIN(78, "GPIO_78"),	PINCTRL_PIN(79, "GPIO_79"),
	PINCTRL_PIN(80, "GPIO_80"),	PINCTRL_PIN(81, "GPIO_81"),
	PINCTRL_PIN(82, "GPIO_82"),	PINCTRL_PIN(83, "GPIO_83"),
	PINCTRL_PIN(84, "GPIO_84"),	PINCTRL_PIN(85, "GPIO_85"),
	PINCTRL_PIN(86, "GPIO_86"),	PINCTRL_PIN(87, "GPIO_87"),
	PINCTRL_PIN(88, "GPIO_88"),	PINCTRL_PIN(89, "GPIO_89"),
	PINCTRL_PIN(90, "GPIO_90"),	PINCTRL_PIN(91, "GPIO_91"),
	PINCTRL_PIN(92, "GPIO_92"),	PINCTRL_PIN(93, "GPIO_93"),
	PINCTRL_PIN(94, "GPIO_94"),	PINCTRL_PIN(95, "GPIO_95"),
	PINCTRL_PIN(96, "GPIO_96"),	PINCTRL_PIN(97, "GPIO_97"),
	PINCTRL_PIN(98, "GPIO_98"),	PINCTRL_PIN(99, "GPIO_99"),
	PINCTRL_PIN(100, "GPIO_100"),	PINCTRL_PIN(101, "GPIO_101"),
	PINCTRL_PIN(102, "GPIO_102"),	PINCTRL_PIN(103, "GPIO_103"),
	PINCTRL_PIN(104, "GPIO_104"),	PINCTRL_PIN(105, "GPIO_105"),
	PINCTRL_PIN(106, "GPIO_106"),	PINCTRL_PIN(107, "GPIO_107"),
	PINCTRL_PIN(108, "GPIO_108"),	PINCTRL_PIN(109, "GPIO_109"),
	PINCTRL_PIN(110, "GPIO_110"),	PINCTRL_PIN(111, "GPIO_111"),
	PINCTRL_PIN(112, "GPIO_112"),	PINCTRL_PIN(113, "GPIO_113"),
	PINCTRL_PIN(114, "GPIO_114"),	PINCTRL_PIN(115, "GPIO_115"),
	PINCTRL_PIN(116, "GPIO_116"),	PINCTRL_PIN(117, "GPIO_117"),
	PINCTRL_PIN(118, "GPIO_118"),	PINCTRL_PIN(119, "GPIO_119"),
	PINCTRL_PIN(120, "GPIO_120"),	PINCTRL_PIN(121, "GPIO_121"),
	PINCTRL_PIN(122, "GPIO_122"),	PINCTRL_PIN(123, "GPIO_123"),
	PINCTRL_PIN(124, "GPIO_124"),	PINCTRL_PIN(125, "GPIO_125"),
	PINCTRL_PIN(126, "GPIO_126"),	PINCTRL_PIN(127, "GPIO_127"),
	PINCTRL_PIN(128, "GPIO_128"),	PINCTRL_PIN(129, "GPIO_129"),
	PINCTRL_PIN(130, "GPIO_130"),	PINCTRL_PIN(131, "GPIO_131"),
	PINCTRL_PIN(132, "GPIO_132"),	PINCTRL_PIN(133, "GPIO_133"),
	PINCTRL_PIN(134, "SDC1_CLK"),	PINCTRL_PIN(135, "SDC1_CMD"),
	PINCTRL_PIN(136, "SDC1_DATA"),	PINCTRL_PIN(137, "SDC1_RCLK"),
	PINCTRL_PIN(138, "SDC2_CLK"),	PINCTRL_PIN(139, "SDC2_CMD"),
	PINCTRL_PIN(140, "SDC2_DATA"),	PINCTRL_PIN(141, "QDSD_CLK"),
	PINCTRL_PIN(142, "QDSD_CMD"),	PINCTRL_PIN(143, "QDSD_DATA0"),
	PINCTRL_PIN(144, "QDSD_DATA1"), PINCTRL_PIN(145, "QDSD_DATA2"),
	PINCTRL_PIN(146, "QDSD_DATA3"),
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
DECLARE_MSM_GPIO_PINS(120);
DECLARE_MSM_GPIO_PINS(121);
DECLARE_MSM_GPIO_PINS(122);
DECLARE_MSM_GPIO_PINS(123);
DECLARE_MSM_GPIO_PINS(124);
DECLARE_MSM_GPIO_PINS(125);
DECLARE_MSM_GPIO_PINS(126);
DECLARE_MSM_GPIO_PINS(127);
DECLARE_MSM_GPIO_PINS(128);
DECLARE_MSM_GPIO_PINS(129);
DECLARE_MSM_GPIO_PINS(130);
DECLARE_MSM_GPIO_PINS(131);
DECLARE_MSM_GPIO_PINS(132);
DECLARE_MSM_GPIO_PINS(133);

static const unsigned int sdc1_clk_pins[] = { 134 };
static const unsigned int sdc1_cmd_pins[] = { 135 };
static const unsigned int sdc1_data_pins[] = { 136 };
static const unsigned int sdc1_rclk_pins[] = { 137 };
static const unsigned int sdc2_clk_pins[] = { 138 };
static const unsigned int sdc2_cmd_pins[] = { 139 };
static const unsigned int sdc2_data_pins[] = { 140 };
static const unsigned int qdsd_clk_pins[] = { 141 };
static const unsigned int qdsd_cmd_pins[] = { 142 };
static const unsigned int qdsd_data0_pins[] = { 143 };
static const unsigned int qdsd_data1_pins[] = { 144 };
static const unsigned int qdsd_data2_pins[] = { 145 };
static const unsigned int qdsd_data3_pins[] = { 146 };

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13,  \
		 f14)                                                         \
	{                                                                     \
		.grp = PINCTRL_PINGROUP("gpio" #id, gpio##id##_pins,          \
					ARRAY_SIZE(gpio##id##_pins)),         \
		.funcs = (int[]){ msm_mux_gpio, msm_mux_##f1, msm_mux_##f2,   \
				  msm_mux_##f3, msm_mux_##f4, msm_mux_##f5,   \
				  msm_mux_##f6, msm_mux_##f7, msm_mux_##f8,   \
				  msm_mux_##f9, msm_mux_##f10, msm_mux_##f11, \
				  msm_mux_##f12, msm_mux_##f13,               \
				  msm_mux_##f14 },                            \
		.nfuncs = 15,                                                 \
		.ctl_reg = 0x1000 * id,                                       \
		.io_reg = 0x4 + 0x1000 * id,                                  \
		.intr_cfg_reg = 0x8 + 0x1000 * id,                            \
		.intr_status_reg = 0xc + 0x1000 * id,                         \
		.mux_bit = 2,                                                 \
		.pull_bit = 0,                                                \
		.drv_bit = 6,                                                 \
		.oe_bit = 9,                                                  \
		.in_bit = 0,                                                  \
		.out_bit = 1,                                                 \
		.intr_enable_bit = 0,                                         \
		.intr_status_bit = 0,                                         \
		.intr_target_bit = 5,                                         \
		.intr_target_kpss_val = 4,                                    \
		.intr_raw_status_bit = 4,                                     \
		.intr_polarity_bit = 1,                                       \
		.intr_detection_bit = 2,                                      \
		.intr_detection_width = 2,                                    \
	}

#define SDC_PINGROUP(pg_name, ctl, pull, drv)                        \
	{                                                            \
		.grp = PINCTRL_PINGROUP(#pg_name, pg_name##_pins,    \
					ARRAY_SIZE(pg_name##_pins)), \
		.ctl_reg = ctl,                                      \
		.io_reg = 0,                                         \
		.intr_cfg_reg = 0,                                   \
		.intr_status_reg = 0,                                \
		.intr_target_reg = 0,                                \
		.mux_bit = -1,                                       \
		.pull_bit = pull,                                    \
		.drv_bit = drv,                                      \
		.oe_bit = -1,                                        \
		.in_bit = -1,                                        \
		.out_bit = -1,                                       \
		.intr_enable_bit = -1,                               \
		.intr_status_bit = -1,                               \
		.intr_target_bit = -1,                               \
		.intr_target_kpss_val = -1,                          \
		.intr_raw_status_bit = -1,                           \
		.intr_polarity_bit = -1,                             \
		.intr_detection_bit = -1,                            \
		.intr_detection_width = -1,                          \
	}

enum msm8952_functions {
	msm_mux_blsp_uart1,
	msm_mux_blsp_spi1,
	msm_mux_adsp_ext,
	msm_mux_gpio,
	msm_mux_blsp_i2c1,
	msm_mux_prng_rosc,
	msm_mux_qdss_cti_trig_out_b0,
	msm_mux_qdss_cti_trig_out_a1,
	msm_mux_blsp_spi2,
	msm_mux_blsp_uart2,
	msm_mux_ldo_update,
	msm_mux_qdss_tracedata_b,
	msm_mux_pbs1,
	msm_mux_pwr_modem_enabled_b,
	msm_mux_blsp_i2c3,
	msm_mux_gcc_gp2_clk_b,
	msm_mux_atest_combodac_to_gpio_native,
	msm_mux_ldo_en,
	msm_mux_blsp_i2c2,
	msm_mux_gcc_gp1_clk_b,
	msm_mux_atest_gpsadc_dtest0_native,
	msm_mux_blsp_spi3,
	msm_mux_blsp_uart3,
	msm_mux_sec_mi2s,
	msm_mux_pbs0,
	msm_mux_gcc_gp3_clk_b,
	msm_mux_blsp_spi4,
	msm_mux_blsp_uart4,
	msm_mux_pbs2,
	msm_mux_pwr_nav_enabled_b,
	msm_mux_pwr_crypto_enabled_b,
	msm_mux_blsp_i2c4,
	msm_mux_blsp_spi5,
	msm_mux_blsp_uart5,
	msm_mux_qdss_traceclk_a,
	msm_mux_atest_bbrx1,
	msm_mux_qdss_tracectl_b,
	msm_mux_qdss_cti_trig_in_b0,
	msm_mux_blsp_i2c6,
	msm_mux_qdss_traceclk_b,
	msm_mux_atest_wlan0,
	msm_mux_m_voc,
	msm_mux_qdss_cti_trig_in_a0,
	msm_mux_atest_bbrx0,
	msm_mux_blsp_i2c5,
	msm_mux_qdss_tracectl_a,
	msm_mux_atest_gpsadc_dtest1_native,
	msm_mux_qdss_tracedata_a,
	msm_mux_blsp_spi6,
	msm_mux_blsp_uart6,
	msm_mux_atest_wlan1,
	msm_mux_mdp_vsync,
	msm_mux_pri_mi2s_mclk_a,
	msm_mux_sec_mi2s_mclk_a,
	msm_mux_cam_mclk,
	msm_mux_cci_i2c,
	msm_mux_pwr_modem_enabled_a,
	msm_mux_cci_timer0,
	msm_mux_cci_timer1,
	msm_mux_pwr_nav_enabled_a,
	msm_mux_pwr_crypto_enabled_a,
	msm_mux_cci_async,
	msm_mux_sd_write,
	msm_mux_qdss_cti_trig_out_a0,
	msm_mux_gcc_gp1_clk_a,
	msm_mux_gcc_gp2_clk_a,
	msm_mux_gcc_gp3_clk_a,
	msm_mux_blsp6_spi,
	msm_mux_qdss_cti_trig_in_b1,
	msm_mux_uim_batt,
	msm_mux_qdss_cti_trig_out_b1,
	msm_mux_uim1_data,
	msm_mux_uim1_clk,
	msm_mux_uim1_reset,
	msm_mux_uim1_present,
	msm_mux_uim2_data,
	msm_mux_uim2_clk,
	msm_mux_uim2_reset,
	msm_mux_uim2_present,
	msm_mux_uim3_data,
	msm_mux_usb_fs,
	msm_mux_uim3_clk,
	msm_mux_uim3_reset,
	msm_mux_uim3_present,
	msm_mux_atest_char3,
	msm_mux_dbg_out,
	msm_mux_bimc_dte0,
	msm_mux_sec_mi2s_mclk_b,
	msm_mux_pri_mi2s,
	msm_mux_atest_char1,
	msm_mux_ebi_cdc,
	msm_mux_atest_char0,
	msm_mux_cdc_pdm0,
	msm_mux_gp_mn,
	msm_mux_gp_pdm_0a,
	msm_mux_gp_pdm_0b,
	msm_mux_gp_pdm_1a,
	msm_mux_gp_pdm_1b,
	msm_mux_gp_pdm_2a,
	msm_mux_gp_pdm_2b,
	msm_mux_gp0_clk,
	msm_mux_gp1_clk,
	msm_mux_pri_mi2s_mclk_b,
	msm_mux_lpass_slimbus,
	msm_mux_lpass_slimbus0,
	msm_mux_lpass_slimbus1,
	msm_mux_wcss_bt,
	msm_mux_atest_char2,
	msm_mux_ebi_ch0,
	msm_mux_wcss_wlan2,
	msm_mux_wcss_wlan1,
	msm_mux_wcss_wlan0,
	msm_mux_wcss_wlan,
	msm_mux_wcss_fm,
	msm_mux_ext_lpass,
	msm_mux_gcc_tlmm,
	msm_mux_dmic0_clk,
	msm_mux_dmic0_data,
	msm_mux_qdss_cti_trig_in_a1,
	msm_mux_blsp_spi7,
	msm_mux_blsp_uart7,
	msm_mux_pri_mi2s_ws,
	msm_mux_wsa_io,
	msm_mux_blsp_i2c7,
	msm_mux_blsp_spi8,
	msm_mux_blsp_uart8,
	msm_mux_blsp_i2c8,
	msm_mux_gcc_plltest,
	msm_mux_nav_pps_in_a,
	msm_mux_pa_indicator,
	msm_mux_nav_pps_in_b,
	msm_mux_nav_pps,
	msm_mux_modem_tsync,
	msm_mux_nav_tsync,
	msm_mux_ssbi_wtr1,
	msm_mux_gsm1_tx,
	msm_mux_gsm0_tx,
	msm_mux_atest_char,
	msm_mux_atest_tsens,
	msm_mux_bimc_dte1,
	msm_mux_NA,
};

static const char *const gpio_groups[] = {
	"gpio0",   "gpio1",   "gpio2",	 "gpio3",   "gpio4",   "gpio5",
	"gpio6",   "gpio7",   "gpio8",	 "gpio9",   "gpio10",  "gpio11",
	"gpio12",  "gpio13",  "gpio14",	 "gpio15",  "gpio16",  "gpio17",
	"gpio18",  "gpio19",  "gpio20",	 "gpio21",  "gpio22",  "gpio23",
	"gpio24",  "gpio25",  "gpio26",	 "gpio27",  "gpio28",  "gpio29",
	"gpio30",  "gpio31",  "gpio32",	 "gpio33",  "gpio34",  "gpio35",
	"gpio36",  "gpio37",  "gpio38",	 "gpio39",  "gpio40",  "gpio41",
	"gpio42",  "gpio43",  "gpio44",	 "gpio45",  "gpio46",  "gpio47",
	"gpio48",  "gpio49",  "gpio50",	 "gpio51",  "gpio52",  "gpio53",
	"gpio54",  "gpio55",  "gpio56",	 "gpio57",  "gpio58",  "gpio59",
	"gpio60",  "gpio61",  "gpio62",	 "gpio63",  "gpio64",  "gpio65",
	"gpio66",  "gpio67",  "gpio68",	 "gpio69",  "gpio70",  "gpio71",
	"gpio72",  "gpio73",  "gpio74",	 "gpio75",  "gpio76",  "gpio77",
	"gpio78",  "gpio79",  "gpio80",	 "gpio81",  "gpio82",  "gpio83",
	"gpio84",  "gpio85",  "gpio86",	 "gpio87",  "gpio88",  "gpio89",
	"gpio90",  "gpio91",  "gpio92",	 "gpio93",  "gpio94",  "gpio95",
	"gpio96",  "gpio97",  "gpio98",	 "gpio99",  "gpio100", "gpio101",
	"gpio102", "gpio103", "gpio104", "gpio105", "gpio106", "gpio107",
	"gpio108", "gpio109", "gpio110", "gpio111", "gpio112", "gpio113",
	"gpio114", "gpio115", "gpio116", "gpio117", "gpio118", "gpio119",
	"gpio120", "gpio121", "gpio122", "gpio123", "gpio124", "gpio125",
	"gpio126", "gpio127", "gpio128", "gpio129", "gpio130", "gpio131",
	"gpio132", "gpio133",
};
static const char *const blsp_uart1_groups[] = {
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
};
static const char *const blsp_spi1_groups[] = {
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
};
static const char *const adsp_ext_groups[] = { "gpio1" };
static const char *const qdss_cti_trig_out_b0_groups[] = { "gpio2" };
static const char *const qdss_cti_trig_out_a1_groups[] = { "gpio3" };
static const char *const blsp_spi2_groups[] = {
	"gpio4",
	"gpio5",
	"gpio6",
	"gpio7",
};
static const char *const blsp_uart2_groups[] = {
	"gpio4",
	"gpio5",
	"gpio6",
	"gpio7",
};
static const char *const ldo_update_groups[] = { "gpio4" };
static const char *const qdss_tracedata_b_groups[] = {
	"gpio8",  "gpio9",  "gpio12", "gpio13", "gpio23", "gpio42",
	"gpio43", "gpio44", "gpio45", "gpio46", "gpio47", "gpio66",
	"gpio86", "gpio87", "gpio88", "gpio92",
};
static const char *const blsp_i2c1_groups[] = {
	"gpio2",
	"gpio3",
};
static const char *const prng_rosc_groups[] = {
	"gpio2",
};
static const char *const pbs1_groups[] = { "gpio9" };
static const char *const pwr_modem_enabled_b_groups[] = { "gpio9" };
static const char *const blsp_i2c3_groups[] = {
	"gpio10",
	"gpio11",
};
static const char *const gcc_gp2_clk_b_groups[] = { "gpio10" };
static const char *const atest_combodac_to_gpio_native_groups[] = {
	"gpio4",  "gpio12", "gpio13", "gpio20", "gpio21", "gpio28",  "gpio29",
	"gpio30", "gpio39", "gpio40", "gpio41", "gpio42", "gpio43",  "gpio44",
	"gpio45", "gpio46", "gpio47", "gpio48", "gpio67", "gpio115",
};
static const char *const ldo_en_groups[] = { "gpio5" };
static const char *const blsp_i2c2_groups[] = {
	"gpio6",
	"gpio7",
};
static const char *const gcc_gp1_clk_b_groups[] = { "gpio6" };
static const char *const atest_gpsadc_dtest0_native_groups[] = { "gpio7" };
static const char *const gp_pdm_2a_groups[] = { "gpio7" };
static const char *const blsp_spi3_groups[] = {
	"gpio8",
	"gpio9",
	"gpio10",
	"gpio11",
};
static const char *const blsp_uart3_groups[] = {
	"gpio8",
	"gpio9",
	"gpio10",
	"gpio11",
};
static const char *const sec_mi2s_groups[] = {
	"gpio8",
	"gpio9",
	"gpio12",
	"gpio13",
};
static const char *const pbs0_groups[] = { "gpio8" };
static const char *const gcc_gp3_clk_b_groups[] = { "gpio11" };
static const char *const blsp_spi4_groups[] = {
	"gpio12",
	"gpio13",
	"gpio14",
	"gpio15",
};
static const char *const blsp_uart4_groups[] = {
	"gpio12",
	"gpio13",
	"gpio14",
	"gpio15",
};
static const char *const pbs2_groups[] = { "gpio12" };
static const char *const pwr_nav_enabled_b_groups[] = { "gpio12" };
static const char *const pwr_crypto_enabled_b_groups[] = { "gpio13" };
static const char *const blsp_i2c4_groups[] = {
	"gpio14",
	"gpio15",
};
static const char *const blsp_spi5_groups[] = {
	"gpio16",
	"gpio17",
	"gpio18",
	"gpio19",
};
static const char *const blsp_uart5_groups[] = {
	"gpio16",
	"gpio17",
	"gpio18",
	"gpio19",
};
static const char *const qdss_traceclk_a_groups[] = { "gpio16" };
static const char *const atest_bbrx1_groups[] = { "gpio16" };
static const char *const qdss_tracectl_b_groups[] = { "gpio20" };
static const char *const gp_pdm_1b_groups[] = { "gpio20" };
static const char *const qdss_cti_trig_in_b0_groups[] = { "gpio21" };
static const char *const blsp_i2c6_groups[] = {
	"gpio22",
	"gpio23",
};
static const char *const qdss_traceclk_b_groups[] = { "gpio22" };
static const char *const atest_wlan0_groups[] = { "gpio22" };
static const char *const m_voc_groups[] = {
	"gpio17",
	"gpio21",
};
static const char *const qdss_cti_trig_in_a0_groups[] = { "gpio17" };
static const char *const atest_bbrx0_groups[] = { "gpio17" };
static const char *const blsp_i2c5_groups[] = {
	"gpio18",
	"gpio19",
};
static const char *const qdss_tracectl_a_groups[] = { "gpio18" };
static const char *const atest_gpsadc_dtest1_native_groups[] = { "gpio18" };
static const char *const qdss_tracedata_a_groups[] = {
	"gpio19", "gpio26", "gpio27", "gpio28", "gpio29", "gpio30",
	"gpio31", "gpio32", "gpio33", "gpio34", "gpio35", "gpio36",
	"gpio37", "gpio38", "gpio39", "gpio40",
};
static const char *const blsp_spi6_groups[] = {
	"gpio20",
	"gpio21",
	"gpio22",
	"gpio23",
};
static const char *const blsp_uart6_groups[] = {
	"gpio20",
	"gpio21",
	"gpio22",
	"gpio23",
};
static const char *const atest_wlan1_groups[] = { "gpio23" };
static const char *const mdp_vsync_groups[] = {
	"gpio24",
	"gpio25",
};
static const char *const pri_mi2s_mclk_a_groups[] = { "gpio25" };
static const char *const sec_mi2s_mclk_a_groups[] = { "gpio25" };
static const char *const cam_mclk_groups[] = {
	"gpio26",
	"gpio27",
	"gpio28",
};
static const char *const cci_i2c_groups[] = {
	"gpio29",
	"gpio30",
	"gpio31",
	"gpio32",
};
static const char *const pwr_modem_enabled_a_groups[] = { "gpio29" };
static const char *const cci_timer0_groups[] = { "gpio33" };
static const char *const cci_timer1_groups[] = { "gpio34" };
static const char *const pwr_nav_enabled_a_groups[] = { "gpio35" };
static const char *const pwr_crypto_enabled_a_groups[] = { "gpio36" };
static const char *const gp_pdm_2b_groups[] = { "gpio38" };
static const char *const cci_async_groups[] = { "gpio39" };
static const char *const gp_mn_groups[] = { "gpio39" };
static const char *const sd_write_groups[] = { "gpio41" };
static const char *const qdss_cti_trig_out_a0_groups[] = { "gpio41" };
static const char *const gcc_gp1_clk_a_groups[] = { "gpio42" };
static const char *const gcc_gp2_clk_a_groups[] = { "gpio43" };
static const char *const gcc_gp3_clk_a_groups[] = { "gpio44" };
static const char *const gp_pdm_1a_groups[] = { "gpio45" };
static const char *const gp0_clk_groups[] = { "gpio46" };
static const char *const gp1_clk_groups[] = { "gpio47" };
static const char *const blsp6_spi_groups[] = {
	"gpio47",
	"gpio48",
};
static const char *const gp_pdm_0b_groups[] = { "gpio48" };
static const char *const qdss_cti_trig_in_b1_groups[] = { "gpio48" };
static const char *const uim_batt_groups[] = { "gpio49" };
static const char *const qdss_cti_trig_out_b1_groups[] = { "gpio50" };
static const char *const uim1_data_groups[] = { "gpio51" };
static const char *const uim1_clk_groups[] = { "gpio52" };
static const char *const uim1_reset_groups[] = { "gpio53" };
static const char *const uim1_present_groups[] = { "gpio54" };
static const char *const uim2_data_groups[] = { "gpio55" };
static const char *const uim2_clk_groups[] = { "gpio56" };
static const char *const uim2_reset_groups[] = { "gpio57" };
static const char *const uim2_present_groups[] = { "gpio58" };
static const char *const uim3_data_groups[] = { "gpio59" };
static const char *const usb_fs_groups[] = {
	"gpio59",
	"gpio60",
};
static const char *const uim3_clk_groups[] = { "gpio60" };
static const char *const uim3_reset_groups[] = { "gpio61" };
static const char *const uim3_present_groups[] = { "gpio62" };
static const char *const atest_char3_groups[] = { "gpio63" };
static const char *const dbg_out_groups[] = { "gpio63" };
static const char *const bimc_dte0_groups[] = {
	"gpio63",
	"gpio65",
};
static const char *const sec_mi2s_mclk_b_groups[] = { "gpio66" };
static const char *const gp_pdm_0a_groups[] = { "gpio66" };
static const char *const pri_mi2s_groups[] = {
	"gpio66",
	"gpio88",
	"gpio91",
	"gpio93",
};
static const char *const atest_char1_groups[] = { "gpio67" };
static const char *const ebi_cdc_groups[] = {
	"gpio67", "gpio69", "gpio118", "gpio119", "gpio120", "gpio123",
};
static const char *const atest_char0_groups[] = { "gpio68" };
static const char *const cdc_pdm0_groups[] = {
	"gpio69", "gpio70", "gpio71", "gpio72", "gpio73", "gpio74",
};
static const char *const pri_mi2s_mclk_b_groups[] = { "gpio69" };
static const char *const lpass_slimbus_groups[] = { "gpio70" };
static const char *const lpass_slimbus0_groups[] = { "gpio71" };
static const char *const lpass_slimbus1_groups[] = { "gpio72" };
static const char *const wcss_bt_groups[] = {
	"gpio75",
	"gpio83",
	"gpio84",
};
static const char *const atest_char2_groups[] = { "gpio75" };
static const char *const ebi_ch0_groups[] = { "gpio75" };
static const char *const wcss_wlan2_groups[] = { "gpio76" };
static const char *const wcss_wlan1_groups[] = { "gpio77" };
static const char *const wcss_wlan0_groups[] = { "gpio78" };
static const char *const wcss_wlan_groups[] = {
	"gpio79",
	"gpio80",
};
static const char *const wcss_fm_groups[] = {
	"gpio81",
	"gpio82",
};
static const char *const ext_lpass_groups[] = { "gpio81" };
static const char *const gcc_tlmm_groups[] = { "gpio87" };
static const char *const dmic0_clk_groups[] = { "gpio89" };
static const char *const dmic0_data_groups[] = { "gpio90" };
static const char *const qdss_cti_trig_in_a1_groups[] = { "gpio91" };
static const char *const blsp_spi7_groups[] = {
	"gpio92",
	"gpio93",
	"gpio94",
	"gpio95",
};
static const char *const blsp_uart7_groups[] = {
	"gpio92",
	"gpio93",
	"gpio94",
	"gpio95",
};
static const char *const pri_mi2s_ws_groups[] = { "gpio92" };
static const char *const wsa_io_groups[] = {
	"gpio94",
	"gpio95",
};
static const char *const blsp_i2c7_groups[] = {
	"gpio94",
	"gpio95",
};
static const char *const blsp_spi8_groups[] = {
	"gpio96",
	"gpio97",
	"gpio98",
	"gpio99",
};
static const char *const blsp_uart8_groups[] = {
	"gpio96",
	"gpio97",
	"gpio98",
	"gpio99",
};
static const char *const blsp_i2c8_groups[] = {
	"gpio98",
	"gpio99",
};
static const char *const gcc_plltest_groups[] = {
	"gpio98",
	"gpio99",
};
static const char *const nav_pps_in_a_groups[] = { "gpio111" };
static const char *const pa_indicator_groups[] = { "gpio112" };
static const char *const nav_pps_in_b_groups[] = { "gpio113" };
static const char *const nav_pps_groups[] = { "gpio113" };
static const char *const modem_tsync_groups[] = { "gpio113" };
static const char *const nav_tsync_groups[] = { "gpio113" };
static const char *const ssbi_wtr1_groups[] = {
	"gpio114",
	"gpio123",
};
static const char *const gsm1_tx_groups[] = { "gpio115" };
static const char *const gsm0_tx_groups[] = { "gpio117" };
static const char *const atest_char_groups[] = { "gpio120" };
static const char *const atest_tsens_groups[] = { "gpio120" };
static const char *const bimc_dte1_groups[] = {
	"gpio121",
	"gpio122",
};

static const struct pinfunction msm8952_functions[] = {
	MSM_PIN_FUNCTION(blsp_uart1),
	MSM_PIN_FUNCTION(blsp_spi1),
	MSM_PIN_FUNCTION(adsp_ext),
	MSM_GPIO_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(blsp_i2c1),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a1),
	MSM_PIN_FUNCTION(blsp_spi2),
	MSM_PIN_FUNCTION(blsp_uart2),
	MSM_PIN_FUNCTION(ldo_update),
	MSM_PIN_FUNCTION(qdss_tracedata_b),
	MSM_PIN_FUNCTION(pbs1),
	MSM_PIN_FUNCTION(pwr_modem_enabled_b),
	MSM_PIN_FUNCTION(blsp_i2c3),
	MSM_PIN_FUNCTION(gcc_gp2_clk_b),
	MSM_PIN_FUNCTION(atest_combodac_to_gpio_native),
	MSM_PIN_FUNCTION(ldo_en),
	MSM_PIN_FUNCTION(blsp_i2c2),
	MSM_PIN_FUNCTION(gcc_gp1_clk_b),
	MSM_PIN_FUNCTION(atest_gpsadc_dtest0_native),
	MSM_PIN_FUNCTION(blsp_spi3),
	MSM_PIN_FUNCTION(blsp_uart3),
	MSM_PIN_FUNCTION(sec_mi2s),
	MSM_PIN_FUNCTION(pbs0),
	MSM_PIN_FUNCTION(gcc_gp3_clk_b),
	MSM_PIN_FUNCTION(blsp_spi4),
	MSM_PIN_FUNCTION(blsp_uart4),
	MSM_PIN_FUNCTION(pbs2),
	MSM_PIN_FUNCTION(pwr_nav_enabled_b),
	MSM_PIN_FUNCTION(pwr_crypto_enabled_b),
	MSM_PIN_FUNCTION(blsp_i2c4),
	MSM_PIN_FUNCTION(blsp_spi5),
	MSM_PIN_FUNCTION(blsp_uart5),
	MSM_PIN_FUNCTION(qdss_traceclk_a),
	MSM_PIN_FUNCTION(atest_bbrx1),
	MSM_PIN_FUNCTION(qdss_tracectl_b),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b0),
	MSM_PIN_FUNCTION(blsp_i2c6),
	MSM_PIN_FUNCTION(qdss_traceclk_b),
	MSM_PIN_FUNCTION(atest_wlan0),
	MSM_PIN_FUNCTION(m_voc),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a0),
	MSM_PIN_FUNCTION(atest_bbrx0),
	MSM_PIN_FUNCTION(blsp_i2c5),
	MSM_PIN_FUNCTION(qdss_tracectl_a),
	MSM_PIN_FUNCTION(atest_gpsadc_dtest1_native),
	MSM_PIN_FUNCTION(qdss_tracedata_a),
	MSM_PIN_FUNCTION(blsp_spi6),
	MSM_PIN_FUNCTION(blsp_uart6),
	MSM_PIN_FUNCTION(atest_wlan1),
	MSM_PIN_FUNCTION(mdp_vsync),
	MSM_PIN_FUNCTION(pri_mi2s_mclk_a),
	MSM_PIN_FUNCTION(sec_mi2s_mclk_a),
	MSM_PIN_FUNCTION(cam_mclk),
	MSM_PIN_FUNCTION(cci_i2c),
	MSM_PIN_FUNCTION(pwr_modem_enabled_a),
	MSM_PIN_FUNCTION(cci_timer0),
	MSM_PIN_FUNCTION(cci_timer1),
	MSM_PIN_FUNCTION(pwr_nav_enabled_a),
	MSM_PIN_FUNCTION(pwr_crypto_enabled_a),
	MSM_PIN_FUNCTION(cci_async),
	MSM_PIN_FUNCTION(sd_write),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a0),
	MSM_PIN_FUNCTION(gcc_gp1_clk_a),
	MSM_PIN_FUNCTION(gcc_gp2_clk_a),
	MSM_PIN_FUNCTION(gcc_gp3_clk_a),
	MSM_PIN_FUNCTION(blsp6_spi),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b1),
	MSM_PIN_FUNCTION(uim_batt),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b1),
	MSM_PIN_FUNCTION(uim1_data),
	MSM_PIN_FUNCTION(uim1_clk),
	MSM_PIN_FUNCTION(uim1_reset),
	MSM_PIN_FUNCTION(uim1_present),
	MSM_PIN_FUNCTION(uim2_data),
	MSM_PIN_FUNCTION(uim2_clk),
	MSM_PIN_FUNCTION(uim2_reset),
	MSM_PIN_FUNCTION(uim2_present),
	MSM_PIN_FUNCTION(uim3_data),
	MSM_PIN_FUNCTION(usb_fs),
	MSM_PIN_FUNCTION(uim3_clk),
	MSM_PIN_FUNCTION(uim3_reset),
	MSM_PIN_FUNCTION(uim3_present),
	MSM_PIN_FUNCTION(atest_char3),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(bimc_dte0),
	MSM_PIN_FUNCTION(sec_mi2s_mclk_b),
	MSM_PIN_FUNCTION(pri_mi2s),
	MSM_PIN_FUNCTION(atest_char1),
	MSM_PIN_FUNCTION(ebi_cdc),
	MSM_PIN_FUNCTION(atest_char0),
	MSM_PIN_FUNCTION(cdc_pdm0),
	MSM_PIN_FUNCTION(gp_mn),
	MSM_PIN_FUNCTION(gp_pdm_0a),
	MSM_PIN_FUNCTION(gp_pdm_0b),
	MSM_PIN_FUNCTION(gp_pdm_1a),
	MSM_PIN_FUNCTION(gp_pdm_1b),
	MSM_PIN_FUNCTION(gp_pdm_2a),
	MSM_PIN_FUNCTION(gp_pdm_2b),
	MSM_PIN_FUNCTION(gp0_clk),
	MSM_PIN_FUNCTION(gp1_clk),
	MSM_PIN_FUNCTION(pri_mi2s_mclk_b),
	MSM_PIN_FUNCTION(lpass_slimbus),
	MSM_PIN_FUNCTION(lpass_slimbus0),
	MSM_PIN_FUNCTION(lpass_slimbus1),
	MSM_PIN_FUNCTION(wcss_bt),
	MSM_PIN_FUNCTION(atest_char2),
	MSM_PIN_FUNCTION(ebi_ch0),
	MSM_PIN_FUNCTION(wcss_wlan2),
	MSM_PIN_FUNCTION(wcss_wlan1),
	MSM_PIN_FUNCTION(wcss_wlan0),
	MSM_PIN_FUNCTION(wcss_wlan),
	MSM_PIN_FUNCTION(wcss_fm),
	MSM_PIN_FUNCTION(ext_lpass),
	MSM_PIN_FUNCTION(gcc_tlmm),
	MSM_PIN_FUNCTION(dmic0_clk),
	MSM_PIN_FUNCTION(dmic0_data),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a1),
	MSM_PIN_FUNCTION(blsp_spi7),
	MSM_PIN_FUNCTION(blsp_uart7),
	MSM_PIN_FUNCTION(pri_mi2s_ws),
	MSM_PIN_FUNCTION(wsa_io),
	MSM_PIN_FUNCTION(blsp_i2c7),
	MSM_PIN_FUNCTION(blsp_spi8),
	MSM_PIN_FUNCTION(blsp_uart8),
	MSM_PIN_FUNCTION(blsp_i2c8),
	MSM_PIN_FUNCTION(gcc_plltest),
	MSM_PIN_FUNCTION(nav_pps_in_a),
	MSM_PIN_FUNCTION(pa_indicator),
	MSM_PIN_FUNCTION(nav_pps_in_b),
	MSM_PIN_FUNCTION(nav_pps),
	MSM_PIN_FUNCTION(modem_tsync),
	MSM_PIN_FUNCTION(nav_tsync),
	MSM_PIN_FUNCTION(ssbi_wtr1),
	MSM_PIN_FUNCTION(gsm1_tx),
	MSM_PIN_FUNCTION(gsm0_tx),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_tsens),
	MSM_PIN_FUNCTION(bimc_dte1),
};

static const struct msm_pingroup msm8952_groups[] = {
	PINGROUP(0, blsp_spi1, blsp_uart1, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA),
	PINGROUP(1, blsp_spi1, blsp_uart1, adsp_ext, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA),
	PINGROUP(2, blsp_spi1, blsp_uart1, blsp_i2c1, prng_rosc, NA, NA, NA, NA,
		 NA, NA, NA, qdss_cti_trig_out_b0, NA, NA),
	PINGROUP(3, blsp_spi1, blsp_uart1, blsp_i2c1, NA, NA, NA, NA, NA, NA,
		 NA, qdss_cti_trig_out_a1, NA, NA, NA),
	PINGROUP(4, blsp_spi2, blsp_uart2, ldo_update, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(5, blsp_spi2, blsp_uart2, ldo_en, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA),
	PINGROUP(6, blsp_spi2, blsp_uart2, blsp_i2c2, gcc_gp1_clk_b, NA, NA, NA,
		 NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(7, blsp_spi2, blsp_uart2, blsp_i2c2, gp_pdm_2a, NA,
		 atest_gpsadc_dtest0_native, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(8, blsp_spi3, blsp_uart3, sec_mi2s, pbs0, NA, NA, NA, NA, NA,
		 NA, qdss_tracedata_b, NA, NA, NA),
	PINGROUP(9, blsp_spi3, blsp_uart3, sec_mi2s, pbs1, pwr_modem_enabled_b,
		 NA, NA, NA, NA, NA, NA, qdss_tracedata_b, NA, NA),
	PINGROUP(10, blsp_spi3, blsp_uart3, blsp_i2c3, gcc_gp2_clk_b, NA, NA,
		 NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(11, blsp_spi3, blsp_uart3, blsp_i2c3, gcc_gp3_clk_b, NA, NA,
		 NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(12, blsp_spi4, blsp_uart4, sec_mi2s, pbs2, pwr_nav_enabled_b,
		 NA, NA, NA, NA, NA, NA, qdss_tracedata_b, NA,
		 atest_combodac_to_gpio_native),
	PINGROUP(13, blsp_spi4, blsp_uart4, sec_mi2s, pwr_crypto_enabled_b, NA,
		 NA, NA, NA, NA, NA, NA, qdss_tracedata_b, NA,
		 atest_combodac_to_gpio_native),
	PINGROUP(14, blsp_spi4, blsp_uart4, blsp_i2c4, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA, NA),
	PINGROUP(15, blsp_spi4, blsp_uart4, blsp_i2c4, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA, NA),
	PINGROUP(16, blsp_spi5, blsp_uart5, NA, NA, NA, NA, qdss_traceclk_a, NA,
		 atest_bbrx1, NA, NA, NA, NA, NA),
	PINGROUP(17, blsp_spi5, blsp_uart5, m_voc, qdss_cti_trig_in_a0, NA,
		 atest_bbrx0, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(18, blsp_spi5, blsp_uart5, blsp_i2c5, qdss_tracectl_a, NA,
		 atest_gpsadc_dtest1_native, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(19, blsp_spi5, blsp_uart5, blsp_i2c5, qdss_tracedata_a, NA, NA,
		 NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(20, blsp_spi6, blsp_uart6, gp_pdm_1b, NA, NA, NA, NA, NA,
		 qdss_tracectl_b, NA, atest_combodac_to_gpio_native, NA, NA,
		 NA),
	PINGROUP(21, blsp_spi6, blsp_uart6, m_voc, NA, NA, NA, NA, NA,
		 qdss_cti_trig_in_b0, NA, atest_combodac_to_gpio_native, NA, NA,
		 NA),
	PINGROUP(22, blsp_spi6, blsp_uart6, blsp_i2c6, qdss_traceclk_b, NA,
		 atest_wlan0, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(23, blsp_spi6, blsp_uart6, blsp_i2c6, qdss_tracedata_b, NA,
		 atest_wlan1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(24, mdp_vsync, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(25, mdp_vsync, pri_mi2s_mclk_a, sec_mi2s_mclk_a, NA, NA, NA,
		 NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(26, cam_mclk, NA, NA, NA, NA, NA, qdss_tracedata_a, NA, NA, NA,
		 NA, NA, NA, NA),
	PINGROUP(27, cam_mclk, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_a, NA,
		 NA, NA, NA, NA),
	PINGROUP(28, cam_mclk, NA, NA, NA, NA, NA, qdss_tracedata_a, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA),
	PINGROUP(29, cci_i2c, pwr_modem_enabled_a, NA, NA, NA, NA, NA,
		 qdss_tracedata_a, NA, atest_combodac_to_gpio_native, NA, NA,
		 NA, NA),
	PINGROUP(30, cci_i2c, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_a, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA),
	PINGROUP(31, cci_i2c, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_a, NA,
		 NA, NA, NA, NA),
	PINGROUP(32, cci_i2c, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_a, NA,
		 NA, NA, NA, NA),
	PINGROUP(33, cci_timer0, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_a,
		 NA, NA, NA, NA, NA),
	PINGROUP(34, cci_timer1, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_a,
		 NA, NA, NA, NA, NA),
	PINGROUP(35, pwr_nav_enabled_a, NA, NA, NA, NA, NA, NA, NA,
		 qdss_tracedata_a, NA, NA, NA, NA, NA),
	PINGROUP(36, pwr_crypto_enabled_a, NA, NA, NA, NA, NA, NA, NA,
		 qdss_tracedata_a, NA, NA, NA, NA, NA),
	PINGROUP(37, NA, NA, NA, NA, NA, qdss_tracedata_a, NA, NA, NA, NA, NA,
		 NA, NA, NA),
	PINGROUP(38, gp_pdm_2b, qdss_tracedata_a, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA, NA),
	PINGROUP(39, cci_async, gp_mn, NA, NA, NA, NA, qdss_tracedata_a, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA),
	PINGROUP(40, NA, NA, NA, NA, qdss_tracedata_a, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(41, sd_write, NA, NA, NA, NA, NA, NA, qdss_cti_trig_out_a0, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA),
	PINGROUP(42, gcc_gp1_clk_a, qdss_tracedata_b, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(43, gcc_gp2_clk_a, qdss_tracedata_b, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(44, gcc_gp3_clk_a, qdss_tracedata_b, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(45, gp_pdm_1a, qdss_tracedata_b, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(46, gp0_clk, qdss_tracedata_b, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(47, blsp6_spi, gp1_clk, qdss_tracedata_b, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(48, blsp6_spi, gp_pdm_0b, qdss_cti_trig_in_b1, NA,
		 atest_combodac_to_gpio_native, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(49, uim_batt, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(50, qdss_cti_trig_out_b1, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA),
	PINGROUP(51, uim1_data, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(52, uim1_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(53, uim1_reset, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(54, uim1_present, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(55, uim2_data, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(56, uim2_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(57, uim2_reset, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(58, uim2_present, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(59, uim3_data, usb_fs, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(60, uim3_clk, usb_fs, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(61, uim3_reset, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(62, uim3_present, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(63, atest_char3, dbg_out, bimc_dte0, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA, NA),
	PINGROUP(64, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(65, bimc_dte0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(66, sec_mi2s_mclk_b, pri_mi2s, gp_pdm_0a, qdss_tracedata_b, NA,
		 NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(67, atest_char1, ebi_cdc, NA, atest_combodac_to_gpio_native,
		 NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(68, atest_char0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(69, cdc_pdm0, pri_mi2s_mclk_b, ebi_cdc, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA, NA),
	PINGROUP(70, lpass_slimbus, cdc_pdm0, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA),
	PINGROUP(71, lpass_slimbus0, cdc_pdm0, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA),
	PINGROUP(72, lpass_slimbus1, cdc_pdm0, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA),
	PINGROUP(73, cdc_pdm0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(74, cdc_pdm0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(75, wcss_bt, atest_char2, NA, ebi_ch0, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA),
	PINGROUP(76, wcss_wlan2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(77, wcss_wlan1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(78, wcss_wlan0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(79, wcss_wlan, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(80, wcss_wlan, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(81, wcss_fm, ext_lpass, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(82, wcss_fm, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(83, wcss_bt, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(84, wcss_bt, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(85, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(86, qdss_tracedata_b, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA),
	PINGROUP(87, qdss_tracedata_b, gcc_tlmm, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA),
	PINGROUP(88, pri_mi2s, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_b, NA,
		 NA, NA, NA, NA),
	PINGROUP(89, dmic0_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(90, dmic0_data, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(91, pri_mi2s, NA, NA, NA, NA, NA, qdss_cti_trig_in_a1, NA, NA,
		 NA, NA, NA, NA, NA),
	PINGROUP(92, blsp_spi7, blsp_uart7, pri_mi2s_ws, NA, NA, NA, NA, NA,
		 qdss_tracedata_b, NA, NA, NA, NA, NA),
	PINGROUP(93, blsp_spi7, blsp_uart7, pri_mi2s, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA, NA),
	PINGROUP(94, wsa_io, blsp_spi7, blsp_uart7, blsp_i2c7, NA, NA, NA, NA,
		 NA, NA, NA, NA, NA, NA),
	PINGROUP(95, wsa_io, blsp_spi7, blsp_uart7, blsp_i2c7, NA, NA, NA, NA,
		 NA, NA, NA, NA, NA, NA),
	PINGROUP(96, blsp_spi8, blsp_uart8, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA),
	PINGROUP(97, blsp_spi8, blsp_uart8, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA),
	PINGROUP(98, blsp_i2c8, blsp_spi8, blsp_uart8, gcc_plltest, NA, NA, NA,
		 NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(99, blsp_i2c8, blsp_spi8, blsp_uart8, gcc_plltest, NA, NA, NA,
		 NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(100, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(101, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(102, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(103, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(104, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(105, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(106, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(107, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(108, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(109, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(110, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(111, NA, NA, nav_pps_in_a, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(112, NA, pa_indicator, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(113, NA, nav_pps_in_b, nav_pps, modem_tsync, nav_tsync, NA, NA,
		 NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(114, NA, ssbi_wtr1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(115, NA, gsm1_tx, NA, atest_combodac_to_gpio_native, NA, NA,
		 NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(116, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(117, gsm0_tx, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(118, NA, ebi_cdc, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(119, NA, ebi_cdc, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(120, NA, atest_char, ebi_cdc, atest_tsens, NA, NA, NA, NA, NA,
		 NA, NA, NA, NA, NA),
	PINGROUP(121, NA, NA, NA, bimc_dte1, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(122, NA, NA, NA, bimc_dte1, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(123, NA, ssbi_wtr1, ebi_cdc, NA, NA, NA, NA, NA, NA, NA, NA,
		 NA, NA, NA),
	PINGROUP(124, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(125, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(126, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(127, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(128, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(129, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(130, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(131, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(132, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(133, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	SDC_PINGROUP(sdc1_clk, 0x10a000, 13, 6),
	SDC_PINGROUP(sdc1_cmd, 0x10a000, 11, 3),
	SDC_PINGROUP(sdc1_data, 0x10a000, 9, 0),
	SDC_PINGROUP(sdc1_rclk, 0x10a000, 15, 0),
	SDC_PINGROUP(sdc2_clk, 0x109000, 14, 6),
	SDC_PINGROUP(sdc2_cmd, 0x109000, 11, 3),
	SDC_PINGROUP(sdc2_data, 0x109000, 9, 0),
	SDC_PINGROUP(qdsd_clk, 0x19c000, 3, 0),
	SDC_PINGROUP(qdsd_cmd, 0x19c000, 8, 5),
	SDC_PINGROUP(qdsd_data0, 0x19c000, 13, 10),
	SDC_PINGROUP(qdsd_data1, 0x19c000, 18, 15),
	SDC_PINGROUP(qdsd_data2, 0x19c000, 23, 20),
	SDC_PINGROUP(qdsd_data3, 0x19c000, 28, 25),
};

static const struct msm_gpio_wakeirq_map msm8952_mpm_map[] = {
	{ 1, 4 },   { 5, 5 },	{ 9, 6 },   { 12, 21 }, { 13, 10 }, { 17, 12 },
	{ 21, 13 }, { 25, 20 }, { 28, 18 }, { 31, 16 }, { 34, 15 }, { 35, 11 },
	{ 36, 9 },  { 37, 8 },	{ 38, 3 },  { 42, 19 }, { 43, 22 }, { 44, 23 },
	{ 45, 24 }, { 46, 25 }, { 48, 26 }, { 54, 14 }, { 58, 17 }, { 62, 53 },
	{ 63, 30 }, { 65, 27 }, { 67, 50 }, { 70, 31 }, { 71, 32 }, { 72, 33 },
	{ 73, 51 }, { 74, 52 }, { 81, 34 }, { 85, 35 }, { 90, 36 }, { 93, 28 },
	{ 97, 29 },
};

static const struct msm_pinctrl_soc_data msm8952_pinctrl = {
	.pins = msm8952_pins,
	.npins = ARRAY_SIZE(msm8952_pins),
	.functions = msm8952_functions,
	.nfunctions = ARRAY_SIZE(msm8952_functions),
	.groups = msm8952_groups,
	.ngroups = ARRAY_SIZE(msm8952_groups),
	.ngpios = 134,
	.wakeirq_map = msm8952_mpm_map,
	.nwakeirq_map = ARRAY_SIZE(msm8952_mpm_map),
};

static int msm8952_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &msm8952_pinctrl);
}

static const struct of_device_id msm8952_pinctrl_of_match[] = {
	{ .compatible = "qcom,msm8952-pinctrl", },
	{ },
};
MODULE_DEVICE_TABLE(of, msm8952_pinctrl_of_match);

static struct platform_driver msm8952_pinctrl_driver = {
	.driver = {
		.name = "msm8952-pinctrl",
		.of_match_table = msm8952_pinctrl_of_match,
	},
	.probe = msm8952_pinctrl_probe,
};

static int __init msm8952_pinctrl_init(void)
{
	return platform_driver_register(&msm8952_pinctrl_driver);
}
arch_initcall(msm8952_pinctrl_init);

static void __exit msm8952_pinctrl_exit(void)
{
	platform_driver_unregister(&msm8952_pinctrl_driver);
}
module_exit(msm8952_pinctrl_exit);

MODULE_DESCRIPTION("Qualcomm msm8952 pinctrl driver");
MODULE_LICENSE("GPL");
