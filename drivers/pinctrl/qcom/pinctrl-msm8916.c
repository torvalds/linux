// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

static const struct pinctrl_pin_desc msm8916_pins[] = {
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
	PINCTRL_PIN(120, "GPIO_120"),
	PINCTRL_PIN(121, "GPIO_121"),
	PINCTRL_PIN(122, "SDC1_CLK"),
	PINCTRL_PIN(123, "SDC1_CMD"),
	PINCTRL_PIN(124, "SDC1_DATA"),
	PINCTRL_PIN(125, "SDC2_CLK"),
	PINCTRL_PIN(126, "SDC2_CMD"),
	PINCTRL_PIN(127, "SDC2_DATA"),
	PINCTRL_PIN(128, "QDSD_CLK"),
	PINCTRL_PIN(129, "QDSD_CMD"),
	PINCTRL_PIN(130, "QDSD_DATA0"),
	PINCTRL_PIN(131, "QDSD_DATA1"),
	PINCTRL_PIN(132, "QDSD_DATA2"),
	PINCTRL_PIN(133, "QDSD_DATA3"),
};

#define DECLARE_MSM_GPIO_PINS(pin)	\
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

static const unsigned int sdc1_clk_pins[] = { 122 };
static const unsigned int sdc1_cmd_pins[] = { 123 };
static const unsigned int sdc1_data_pins[] = { 124 };
static const unsigned int sdc2_clk_pins[] = { 125 };
static const unsigned int sdc2_cmd_pins[] = { 126 };
static const unsigned int sdc2_data_pins[] = { 127 };
static const unsigned int qdsd_clk_pins[] = { 128 };
static const unsigned int qdsd_cmd_pins[] = { 129 };
static const unsigned int qdsd_data0_pins[] = { 130 };
static const unsigned int qdsd_data1_pins[] = { 131 };
static const unsigned int qdsd_data2_pins[] = { 132 };
static const unsigned int qdsd_data3_pins[] = { 133 };

#define FUNCTION(fname)			                \
	[MSM_MUX_##fname] = {		                \
		.name = #fname,				\
		.groups = fname##_groups,               \
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{							\
		.name = "gpio" #id,				\
		.pins = gpio##id##_pins,			\
		.npins = ARRAY_SIZE(gpio##id##_pins),		\
		.funcs = (int[]){				\
			MSM_MUX_gpio,				\
			MSM_MUX_##f1,				\
			MSM_MUX_##f2,				\
			MSM_MUX_##f3,				\
			MSM_MUX_##f4,				\
			MSM_MUX_##f5,				\
			MSM_MUX_##f6,				\
			MSM_MUX_##f7,				\
			MSM_MUX_##f8,				\
			MSM_MUX_##f9				\
		},				        	\
		.nfuncs = 10,					\
		.ctl_reg = 0x1000 * id,	        		\
		.io_reg = 0x4 + 0x1000 * id,			\
		.intr_cfg_reg = 0x8 + 0x1000 * id,		\
		.intr_status_reg = 0xc + 0x1000 * id,		\
		.intr_target_reg = 0x8 + 0x1000 * id,		\
		.mux_bit = 2,					\
		.pull_bit = 0,					\
		.drv_bit = 6,					\
		.oe_bit = 9,					\
		.in_bit = 0,					\
		.out_bit = 1,					\
		.intr_enable_bit = 0,				\
		.intr_status_bit = 0,				\
		.intr_target_bit = 5,				\
		.intr_target_kpss_val = 4,			\
		.intr_raw_status_bit = 4,			\
		.intr_polarity_bit = 1,				\
		.intr_detection_bit = 2,			\
		.intr_detection_width = 2,			\
	}

#define SDC_PINGROUP(pg_name, ctl, pull, drv)	\
	{					        \
		.name = #pg_name,			\
		.pins = pg_name##_pins,			\
		.npins = ARRAY_SIZE(pg_name##_pins),	\
		.ctl_reg = ctl,				\
		.io_reg = 0,				\
		.intr_cfg_reg = 0,			\
		.intr_status_reg = 0,			\
		.intr_target_reg = 0,			\
		.mux_bit = -1,				\
		.pull_bit = pull,			\
		.drv_bit = drv,				\
		.oe_bit = -1,				\
		.in_bit = -1,				\
		.out_bit = -1,				\
		.intr_enable_bit = -1,			\
		.intr_status_bit = -1,			\
		.intr_target_bit = -1,			\
		.intr_target_kpss_val = -1,		\
		.intr_raw_status_bit = -1,		\
		.intr_polarity_bit = -1,		\
		.intr_detection_bit = -1,		\
		.intr_detection_width = -1,		\
	}

enum msm8916_functions {
	MSM_MUX_adsp_ext,
	MSM_MUX_alsp_int,
	MSM_MUX_atest_bbrx0,
	MSM_MUX_atest_bbrx1,
	MSM_MUX_atest_char,
	MSM_MUX_atest_char0,
	MSM_MUX_atest_char1,
	MSM_MUX_atest_char2,
	MSM_MUX_atest_char3,
	MSM_MUX_atest_combodac,
	MSM_MUX_atest_gpsadc0,
	MSM_MUX_atest_gpsadc1,
	MSM_MUX_atest_tsens,
	MSM_MUX_atest_wlan0,
	MSM_MUX_atest_wlan1,
	MSM_MUX_backlight_en,
	MSM_MUX_bimc_dte0,
	MSM_MUX_bimc_dte1,
	MSM_MUX_blsp_i2c1,
	MSM_MUX_blsp_i2c2,
	MSM_MUX_blsp_i2c3,
	MSM_MUX_blsp_i2c4,
	MSM_MUX_blsp_i2c5,
	MSM_MUX_blsp_i2c6,
	MSM_MUX_blsp_spi1,
	MSM_MUX_blsp_spi1_cs1,
	MSM_MUX_blsp_spi1_cs2,
	MSM_MUX_blsp_spi1_cs3,
	MSM_MUX_blsp_spi2,
	MSM_MUX_blsp_spi2_cs1,
	MSM_MUX_blsp_spi2_cs2,
	MSM_MUX_blsp_spi2_cs3,
	MSM_MUX_blsp_spi3,
	MSM_MUX_blsp_spi3_cs1,
	MSM_MUX_blsp_spi3_cs2,
	MSM_MUX_blsp_spi3_cs3,
	MSM_MUX_blsp_spi4,
	MSM_MUX_blsp_spi5,
	MSM_MUX_blsp_spi6,
	MSM_MUX_blsp_uart1,
	MSM_MUX_blsp_uart2,
	MSM_MUX_blsp_uim1,
	MSM_MUX_blsp_uim2,
	MSM_MUX_cam1_rst,
	MSM_MUX_cam1_standby,
	MSM_MUX_cam_mclk0,
	MSM_MUX_cam_mclk1,
	MSM_MUX_cci_async,
	MSM_MUX_cci_i2c,
	MSM_MUX_cci_timer0,
	MSM_MUX_cci_timer1,
	MSM_MUX_cci_timer2,
	MSM_MUX_cdc_pdm0,
	MSM_MUX_codec_mad,
	MSM_MUX_dbg_out,
	MSM_MUX_display_5v,
	MSM_MUX_dmic0_clk,
	MSM_MUX_dmic0_data,
	MSM_MUX_dsi_rst,
	MSM_MUX_ebi0_wrcdc,
	MSM_MUX_euro_us,
	MSM_MUX_ext_lpass,
	MSM_MUX_flash_strobe,
	MSM_MUX_gcc_gp1_clk_a,
	MSM_MUX_gcc_gp1_clk_b,
	MSM_MUX_gcc_gp2_clk_a,
	MSM_MUX_gcc_gp2_clk_b,
	MSM_MUX_gcc_gp3_clk_a,
	MSM_MUX_gcc_gp3_clk_b,
	MSM_MUX_gpio,
	MSM_MUX_gsm0_tx0,
	MSM_MUX_gsm0_tx1,
	MSM_MUX_gsm1_tx0,
	MSM_MUX_gsm1_tx1,
	MSM_MUX_gyro_accl,
	MSM_MUX_kpsns0,
	MSM_MUX_kpsns1,
	MSM_MUX_kpsns2,
	MSM_MUX_ldo_en,
	MSM_MUX_ldo_update,
	MSM_MUX_mag_int,
	MSM_MUX_mdp_vsync,
	MSM_MUX_modem_tsync,
	MSM_MUX_m_voc,
	MSM_MUX_nav_pps,
	MSM_MUX_nav_tsync,
	MSM_MUX_pa_indicator,
	MSM_MUX_pbs0,
	MSM_MUX_pbs1,
	MSM_MUX_pbs2,
	MSM_MUX_pri_mi2s,
	MSM_MUX_pri_mi2s_ws,
	MSM_MUX_prng_rosc,
	MSM_MUX_pwr_crypto_enabled_a,
	MSM_MUX_pwr_crypto_enabled_b,
	MSM_MUX_pwr_modem_enabled_a,
	MSM_MUX_pwr_modem_enabled_b,
	MSM_MUX_pwr_nav_enabled_a,
	MSM_MUX_pwr_nav_enabled_b,
	MSM_MUX_qdss_ctitrig_in_a0,
	MSM_MUX_qdss_ctitrig_in_a1,
	MSM_MUX_qdss_ctitrig_in_b0,
	MSM_MUX_qdss_ctitrig_in_b1,
	MSM_MUX_qdss_ctitrig_out_a0,
	MSM_MUX_qdss_ctitrig_out_a1,
	MSM_MUX_qdss_ctitrig_out_b0,
	MSM_MUX_qdss_ctitrig_out_b1,
	MSM_MUX_qdss_traceclk_a,
	MSM_MUX_qdss_traceclk_b,
	MSM_MUX_qdss_tracectl_a,
	MSM_MUX_qdss_tracectl_b,
	MSM_MUX_qdss_tracedata_a,
	MSM_MUX_qdss_tracedata_b,
	MSM_MUX_reset_n,
	MSM_MUX_sd_card,
	MSM_MUX_sd_write,
	MSM_MUX_sec_mi2s,
	MSM_MUX_smb_int,
	MSM_MUX_ssbi_wtr0,
	MSM_MUX_ssbi_wtr1,
	MSM_MUX_uim1,
	MSM_MUX_uim2,
	MSM_MUX_uim3,
	MSM_MUX_uim_batt,
	MSM_MUX_wcss_bt,
	MSM_MUX_wcss_fm,
	MSM_MUX_wcss_wlan,
	MSM_MUX_webcam1_rst,
	MSM_MUX_NA,
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
	"gpio78", "gpio79", "gpio80", "gpio81", "gpio82", "gpio83", "gpio84",
	"gpio85", "gpio86", "gpio87", "gpio88", "gpio89", "gpio90", "gpio91",
	"gpio92", "gpio93", "gpio94", "gpio95", "gpio96", "gpio97", "gpio98",
	"gpio99", "gpio100", "gpio101", "gpio102", "gpio103", "gpio104",
	"gpio105", "gpio106", "gpio107", "gpio108", "gpio109", "gpio110",
	"gpio111", "gpio112", "gpio113", "gpio114", "gpio115", "gpio116",
	"gpio117", "gpio118", "gpio119", "gpio120", "gpio121"
};
static const char * const adsp_ext_groups[] = { "gpio38" };
static const char * const alsp_int_groups[] = { "gpio113" };
static const char * const atest_bbrx0_groups[] = { "gpio17" };
static const char * const atest_bbrx1_groups[] = { "gpio16" };
static const char * const atest_char_groups[] = { "gpio62" };
static const char * const atest_char0_groups[] = { "gpio60" };
static const char * const atest_char1_groups[] = { "gpio59" };
static const char * const atest_char2_groups[] = { "gpio58" };
static const char * const atest_char3_groups[] = { "gpio57" };
static const char * const atest_combodac_groups[] = {
	"gpio4", "gpio12", "gpio13", "gpio20", "gpio21", "gpio28", "gpio29",
	"gpio30", "gpio39", "gpio40", "gpio41", "gpio42", "gpio43", "gpio44",
	"gpio45", "gpio46", "gpio47", "gpio48", "gpio69", "gpio107"
};
static const char * const atest_gpsadc0_groups[] = { "gpio7" };
static const char * const atest_gpsadc1_groups[] = { "gpio18" };
static const char * const atest_tsens_groups[] = { "gpio112" };
static const char * const atest_wlan0_groups[] = { "gpio22" };
static const char * const atest_wlan1_groups[] = { "gpio23" };
static const char * const backlight_en_groups[] = { "gpio98" };
static const char * const bimc_dte0_groups[] = { "gpio63", "gpio65" };
static const char * const bimc_dte1_groups[] = { "gpio64", "gpio66" };
static const char * const blsp_i2c1_groups[] = { "gpio2", "gpio3" };
static const char * const blsp_i2c2_groups[] = { "gpio6", "gpio7" };
static const char * const blsp_i2c3_groups[] = { "gpio10", "gpio11" };
static const char * const blsp_i2c4_groups[] = { "gpio14", "gpio15" };
static const char * const blsp_i2c5_groups[] = { "gpio18", "gpio19" };
static const char * const blsp_i2c6_groups[] = { "gpio22", "gpio23" };
static const char * const blsp_spi1_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3"
};
static const char * const blsp_spi1_cs1_groups[] = { "gpio110" };
static const char * const blsp_spi1_cs2_groups[] = { "gpio16" };
static const char * const blsp_spi1_cs3_groups[] = { "gpio4" };
static const char * const blsp_spi2_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7"
};
static const char * const blsp_spi2_cs1_groups[] = { "gpio121" };
static const char * const blsp_spi2_cs2_groups[] = { "gpio17" };
static const char * const blsp_spi2_cs3_groups[] = { "gpio5" };
static const char * const blsp_spi3_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};
static const char * const blsp_spi3_cs1_groups[] = { "gpio120" };
static const char * const blsp_spi3_cs2_groups[] = { "gpio37" };
static const char * const blsp_spi3_cs3_groups[] = { "gpio69" };
static const char * const blsp_spi4_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15"
};
static const char * const blsp_spi5_groups[] = {
	"gpio16", "gpio17", "gpio18", "gpio19"
};
static const char * const blsp_spi6_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23"
};
static const char * const blsp_uart1_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3"
};
static const char * const blsp_uart2_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7"
};
static const char * const blsp_uim1_groups[] = { "gpio0", "gpio1" };
static const char * const blsp_uim2_groups[] = { "gpio4", "gpio5" };
static const char * const cam1_rst_groups[] = { "gpio35" };
static const char * const cam1_standby_groups[] = { "gpio34" };
static const char * const cam_mclk0_groups[] = { "gpio26" };
static const char * const cam_mclk1_groups[] = { "gpio27" };
static const char * const cci_async_groups[] = { "gpio33" };
static const char * const cci_i2c_groups[] = { "gpio29", "gpio30" };
static const char * const cci_timer0_groups[] = { "gpio31" };
static const char * const cci_timer1_groups[] = { "gpio32" };
static const char * const cci_timer2_groups[] = { "gpio38" };
static const char * const cdc_pdm0_groups[] = {
	"gpio63", "gpio64", "gpio65", "gpio66", "gpio67", "gpio68"
};
static const char * const codec_mad_groups[] = { "gpio16" };
static const char * const dbg_out_groups[] = { "gpio47" };
static const char * const display_5v_groups[] = { "gpio97" };
static const char * const dmic0_clk_groups[] = { "gpio0" };
static const char * const dmic0_data_groups[] = { "gpio1" };
static const char * const dsi_rst_groups[] = { "gpio25" };
static const char * const ebi0_wrcdc_groups[] = { "gpio67" };
static const char * const euro_us_groups[] = { "gpio120" };
static const char * const ext_lpass_groups[] = { "gpio45" };
static const char * const flash_strobe_groups[] = { "gpio31", "gpio32" };
static const char * const gcc_gp1_clk_a_groups[] = { "gpio49" };
static const char * const gcc_gp1_clk_b_groups[] = { "gpio97" };
static const char * const gcc_gp2_clk_a_groups[] = { "gpio50" };
static const char * const gcc_gp2_clk_b_groups[] = { "gpio12" };
static const char * const gcc_gp3_clk_a_groups[] = { "gpio51" };
static const char * const gcc_gp3_clk_b_groups[] = { "gpio13" };
static const char * const gsm0_tx0_groups[] = { "gpio99" };
static const char * const gsm0_tx1_groups[] = { "gpio100" };
static const char * const gsm1_tx0_groups[] = { "gpio101" };
static const char * const gsm1_tx1_groups[] = { "gpio102" };
static const char * const gyro_accl_groups[] = {"gpio115" };
static const char * const kpsns0_groups[] = { "gpio107" };
static const char * const kpsns1_groups[] = { "gpio108" };
static const char * const kpsns2_groups[] = { "gpio109" };
static const char * const ldo_en_groups[] = { "gpio121" };
static const char * const ldo_update_groups[] = { "gpio120" };
static const char * const mag_int_groups[] = { "gpio69" };
static const char * const mdp_vsync_groups[] = { "gpio24", "gpio25" };
static const char * const modem_tsync_groups[] = { "gpio95" };
static const char * const m_voc_groups[] = { "gpio8", "gpio119" };
static const char * const nav_pps_groups[] = { "gpio95" };
static const char * const nav_tsync_groups[] = { "gpio95" };
static const char * const pa_indicator_groups[] = { "gpio86" };
static const char * const pbs0_groups[] = { "gpio107" };
static const char * const pbs1_groups[] = { "gpio108" };
static const char * const pbs2_groups[] = { "gpio109" };
static const char * const pri_mi2s_groups[] = {
	"gpio113", "gpio114", "gpio115", "gpio116"
};
static const char * const pri_mi2s_ws_groups[] = { "gpio110" };
static const char * const prng_rosc_groups[] = { "gpio43" };
static const char * const pwr_crypto_enabled_a_groups[] = { "gpio35" };
static const char * const pwr_crypto_enabled_b_groups[] = { "gpio115" };
static const char * const pwr_modem_enabled_a_groups[] = { "gpio28" };
static const char * const pwr_modem_enabled_b_groups[] = { "gpio113" };
static const char * const pwr_nav_enabled_a_groups[] = { "gpio34" };
static const char * const pwr_nav_enabled_b_groups[] = { "gpio114" };
static const char * const qdss_ctitrig_in_a0_groups[] = { "gpio20" };
static const char * const qdss_ctitrig_in_a1_groups[] = { "gpio49" };
static const char * const qdss_ctitrig_in_b0_groups[] = { "gpio21" };
static const char * const qdss_ctitrig_in_b1_groups[] = { "gpio50" };
static const char * const qdss_ctitrig_out_a0_groups[] = { "gpio23" };
static const char * const qdss_ctitrig_out_a1_groups[] = { "gpio52" };
static const char * const qdss_ctitrig_out_b0_groups[] = { "gpio22" };
static const char * const qdss_ctitrig_out_b1_groups[] = { "gpio51" };
static const char * const qdss_traceclk_a_groups[] = { "gpio46" };
static const char * const qdss_traceclk_b_groups[] = { "gpio5" };
static const char * const qdss_tracectl_a_groups[] = { "gpio45" };
static const char * const qdss_tracectl_b_groups[] = { "gpio4" };
static const char * const qdss_tracedata_a_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio39", "gpio40", "gpio41", "gpio42",
	"gpio43", "gpio47", "gpio48", "gpio62", "gpio69", "gpio112", "gpio113",
	"gpio114", "gpio115"
};
static const char * const qdss_tracedata_b_groups[] = {
	"gpio26", "gpio27", "gpio28", "gpio29", "gpio30", "gpio31", "gpio32",
	"gpio33", "gpio34", "gpio35", "gpio36", "gpio37", "gpio110", "gpio111",
	"gpio120", "gpio121"
};
static const char * const reset_n_groups[] = { "gpio36" };
static const char * const sd_card_groups[] = { "gpio38" };
static const char * const sd_write_groups[] = { "gpio121" };
static const char * const sec_mi2s_groups[] = {
	"gpio112", "gpio117", "gpio118", "gpio119"
};
static const char * const smb_int_groups[] = { "gpio62" };
static const char * const ssbi_wtr0_groups[] = { "gpio103", "gpio104" };
static const char * const ssbi_wtr1_groups[] = { "gpio105", "gpio106" };
static const char * const uim1_groups[] = {
	"gpio57", "gpio58", "gpio59", "gpio60"
};

static const char * const uim2_groups[] = {
	"gpio53", "gpio54", "gpio55", "gpio56"
};
static const char * const uim3_groups[] = {
	"gpio49",  "gpio50", "gpio51", "gpio52"
};
static const char * const uim_batt_groups[] = { "gpio61" };
static const char * const wcss_bt_groups[] = { "gpio39", "gpio47", "gpio48" };
static const char * const wcss_fm_groups[] = { "gpio45", "gpio46" };
static const char * const wcss_wlan_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43", "gpio44"
};
static const char * const webcam1_rst_groups[] = { "gpio28" };

static const struct msm_function msm8916_functions[] = {
	FUNCTION(adsp_ext),
	FUNCTION(alsp_int),
	FUNCTION(atest_bbrx0),
	FUNCTION(atest_bbrx1),
	FUNCTION(atest_char),
	FUNCTION(atest_char0),
	FUNCTION(atest_char1),
	FUNCTION(atest_char2),
	FUNCTION(atest_char3),
	FUNCTION(atest_combodac),
	FUNCTION(atest_gpsadc0),
	FUNCTION(atest_gpsadc1),
	FUNCTION(atest_tsens),
	FUNCTION(atest_wlan0),
	FUNCTION(atest_wlan1),
	FUNCTION(backlight_en),
	FUNCTION(bimc_dte0),
	FUNCTION(bimc_dte1),
	FUNCTION(blsp_i2c1),
	FUNCTION(blsp_i2c2),
	FUNCTION(blsp_i2c3),
	FUNCTION(blsp_i2c4),
	FUNCTION(blsp_i2c5),
	FUNCTION(blsp_i2c6),
	FUNCTION(blsp_spi1),
	FUNCTION(blsp_spi1_cs1),
	FUNCTION(blsp_spi1_cs2),
	FUNCTION(blsp_spi1_cs3),
	FUNCTION(blsp_spi2),
	FUNCTION(blsp_spi2_cs1),
	FUNCTION(blsp_spi2_cs2),
	FUNCTION(blsp_spi2_cs3),
	FUNCTION(blsp_spi3),
	FUNCTION(blsp_spi3_cs1),
	FUNCTION(blsp_spi3_cs2),
	FUNCTION(blsp_spi3_cs3),
	FUNCTION(blsp_spi4),
	FUNCTION(blsp_spi5),
	FUNCTION(blsp_spi6),
	FUNCTION(blsp_uart1),
	FUNCTION(blsp_uart2),
	FUNCTION(blsp_uim1),
	FUNCTION(blsp_uim2),
	FUNCTION(cam1_rst),
	FUNCTION(cam1_standby),
	FUNCTION(cam_mclk0),
	FUNCTION(cam_mclk1),
	FUNCTION(cci_async),
	FUNCTION(cci_i2c),
	FUNCTION(cci_timer0),
	FUNCTION(cci_timer1),
	FUNCTION(cci_timer2),
	FUNCTION(cdc_pdm0),
	FUNCTION(codec_mad),
	FUNCTION(dbg_out),
	FUNCTION(display_5v),
	FUNCTION(dmic0_clk),
	FUNCTION(dmic0_data),
	FUNCTION(dsi_rst),
	FUNCTION(ebi0_wrcdc),
	FUNCTION(euro_us),
	FUNCTION(ext_lpass),
	FUNCTION(flash_strobe),
	FUNCTION(gcc_gp1_clk_a),
	FUNCTION(gcc_gp1_clk_b),
	FUNCTION(gcc_gp2_clk_a),
	FUNCTION(gcc_gp2_clk_b),
	FUNCTION(gcc_gp3_clk_a),
	FUNCTION(gcc_gp3_clk_b),
	FUNCTION(gpio),
	FUNCTION(gsm0_tx0),
	FUNCTION(gsm0_tx1),
	FUNCTION(gsm1_tx0),
	FUNCTION(gsm1_tx1),
	FUNCTION(gyro_accl),
	FUNCTION(kpsns0),
	FUNCTION(kpsns1),
	FUNCTION(kpsns2),
	FUNCTION(ldo_en),
	FUNCTION(ldo_update),
	FUNCTION(mag_int),
	FUNCTION(mdp_vsync),
	FUNCTION(modem_tsync),
	FUNCTION(m_voc),
	FUNCTION(nav_pps),
	FUNCTION(nav_tsync),
	FUNCTION(pa_indicator),
	FUNCTION(pbs0),
	FUNCTION(pbs1),
	FUNCTION(pbs2),
	FUNCTION(pri_mi2s),
	FUNCTION(pri_mi2s_ws),
	FUNCTION(prng_rosc),
	FUNCTION(pwr_crypto_enabled_a),
	FUNCTION(pwr_crypto_enabled_b),
	FUNCTION(pwr_modem_enabled_a),
	FUNCTION(pwr_modem_enabled_b),
	FUNCTION(pwr_nav_enabled_a),
	FUNCTION(pwr_nav_enabled_b),
	FUNCTION(qdss_ctitrig_in_a0),
	FUNCTION(qdss_ctitrig_in_a1),
	FUNCTION(qdss_ctitrig_in_b0),
	FUNCTION(qdss_ctitrig_in_b1),
	FUNCTION(qdss_ctitrig_out_a0),
	FUNCTION(qdss_ctitrig_out_a1),
	FUNCTION(qdss_ctitrig_out_b0),
	FUNCTION(qdss_ctitrig_out_b1),
	FUNCTION(qdss_traceclk_a),
	FUNCTION(qdss_traceclk_b),
	FUNCTION(qdss_tracectl_a),
	FUNCTION(qdss_tracectl_b),
	FUNCTION(qdss_tracedata_a),
	FUNCTION(qdss_tracedata_b),
	FUNCTION(reset_n),
	FUNCTION(sd_card),
	FUNCTION(sd_write),
	FUNCTION(sec_mi2s),
	FUNCTION(smb_int),
	FUNCTION(ssbi_wtr0),
	FUNCTION(ssbi_wtr1),
	FUNCTION(uim1),
	FUNCTION(uim2),
	FUNCTION(uim3),
	FUNCTION(uim_batt),
	FUNCTION(wcss_bt),
	FUNCTION(wcss_fm),
	FUNCTION(wcss_wlan),
	FUNCTION(webcam1_rst)
};

static const struct msm_pingroup msm8916_groups[] = {
	PINGROUP(0, blsp_spi1, blsp_uart1, blsp_uim1, dmic0_clk, NA, NA, NA, NA, NA),
	PINGROUP(1, blsp_spi1, blsp_uart1, blsp_uim1, dmic0_data, NA, NA, NA, NA, NA),
	PINGROUP(2, blsp_spi1, blsp_uart1, blsp_i2c1, NA, NA, NA, NA, NA, NA),
	PINGROUP(3, blsp_spi1, blsp_uart1, blsp_i2c1, NA, NA, NA, NA, NA, NA),
	PINGROUP(4, blsp_spi2, blsp_uart2, blsp_uim2, blsp_spi1_cs3, qdss_tracectl_b, NA, atest_combodac, NA, NA),
	PINGROUP(5, blsp_spi2, blsp_uart2, blsp_uim2, blsp_spi2_cs3, qdss_traceclk_b, NA, NA, NA, NA),
	PINGROUP(6, blsp_spi2, blsp_uart2, blsp_i2c2, NA, NA, NA, NA, NA, NA),
	PINGROUP(7, blsp_spi2, blsp_uart2, blsp_i2c2, NA, NA, NA, NA, NA, NA),
	PINGROUP(8, blsp_spi3, m_voc, qdss_tracedata_a, NA, NA, NA, NA, NA, NA),
	PINGROUP(9, blsp_spi3, qdss_tracedata_a, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(10, blsp_spi3, blsp_i2c3, qdss_tracedata_a, NA, NA, NA, NA, NA, NA),
	PINGROUP(11, blsp_spi3, blsp_i2c3, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(12, blsp_spi4, gcc_gp2_clk_b, NA, atest_combodac, NA, NA, NA, NA, NA),
	PINGROUP(13, blsp_spi4, gcc_gp3_clk_b, NA, atest_combodac, NA, NA, NA, NA, NA),
	PINGROUP(14, blsp_spi4, blsp_i2c4, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(15, blsp_spi4, blsp_i2c4, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(16, blsp_spi5, blsp_spi1_cs2, NA, atest_bbrx1, NA, NA, NA, NA, NA),
	PINGROUP(17, blsp_spi5, blsp_spi2_cs2, NA, atest_bbrx0, NA, NA, NA, NA, NA),
	PINGROUP(18, blsp_spi5, blsp_i2c5, NA, atest_gpsadc1, NA, NA, NA, NA, NA),
	PINGROUP(19, blsp_spi5, blsp_i2c5, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(20, blsp_spi6, NA, NA, NA, NA, NA, NA, qdss_ctitrig_in_a0, NA),
	PINGROUP(21, blsp_spi6, NA, NA, NA, NA, NA, NA, qdss_ctitrig_in_b0, NA),
	PINGROUP(22, blsp_spi6, blsp_i2c6, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(23, blsp_spi6, blsp_i2c6, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(24, mdp_vsync, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(25, mdp_vsync, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(26, cam_mclk0, NA, NA, NA, NA, NA, qdss_tracedata_b, NA, NA),
	PINGROUP(27, cam_mclk1, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_b),
	PINGROUP(28, pwr_modem_enabled_a, NA, NA, NA, NA, NA, qdss_tracedata_b, NA, atest_combodac),
	PINGROUP(29, cci_i2c, NA, NA, NA, NA, NA, qdss_tracedata_b, NA, atest_combodac),
	PINGROUP(30, cci_i2c, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_b),
	PINGROUP(31, cci_timer0, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(32, cci_timer1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(33, cci_async, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_b),
	PINGROUP(34, pwr_nav_enabled_a, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_b),
	PINGROUP(35, pwr_crypto_enabled_a, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_b),
	PINGROUP(36, NA, NA, NA, NA, NA, NA, NA, qdss_tracedata_b, NA),
	PINGROUP(37, blsp_spi3_cs2, NA, NA, NA, NA, NA, qdss_tracedata_b, NA, NA),
	PINGROUP(38, cci_timer2, adsp_ext, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(39, wcss_bt, qdss_tracedata_a, NA, atest_combodac, NA, NA, NA, NA, NA),
	PINGROUP(40, wcss_wlan, qdss_tracedata_a, NA, atest_combodac, NA, NA, NA, NA, NA),
	PINGROUP(41, wcss_wlan, qdss_tracedata_a, NA, atest_combodac, NA, NA, NA, NA, NA),
	PINGROUP(42, wcss_wlan, qdss_tracedata_a, NA, atest_combodac, NA, NA, NA, NA, NA),
	PINGROUP(43, wcss_wlan, prng_rosc, qdss_tracedata_a, NA, atest_combodac, NA, NA, NA, NA),
	PINGROUP(44, wcss_wlan, NA, atest_combodac, NA, NA, NA, NA, NA, NA),
	PINGROUP(45, wcss_fm, ext_lpass, qdss_tracectl_a, NA, atest_combodac, NA, NA, NA, NA),
	PINGROUP(46, wcss_fm, qdss_traceclk_a, NA, atest_combodac, NA, NA, NA, NA, NA),
	PINGROUP(47, wcss_bt, dbg_out, qdss_tracedata_a, NA, atest_combodac, NA, NA, NA, NA),
	PINGROUP(48, wcss_bt, qdss_tracedata_a, NA, atest_combodac, NA, NA, NA, NA, NA),
	PINGROUP(49, uim3, gcc_gp1_clk_a, qdss_ctitrig_in_a1, NA, NA, NA, NA, NA, NA),
	PINGROUP(50, uim3, gcc_gp2_clk_a, qdss_ctitrig_in_b1, NA, NA, NA, NA, NA, NA),
	PINGROUP(51, uim3, gcc_gp3_clk_a, qdss_ctitrig_out_b1, NA, NA, NA, NA, NA, NA),
	PINGROUP(52, uim3, NA, qdss_ctitrig_out_a1, NA, NA, NA, NA, NA, NA),
	PINGROUP(53, uim2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(54, uim2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(55, uim2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(56, uim2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(57, uim1, atest_char3, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(58, uim1, atest_char2, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(59, uim1, atest_char1, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(60, uim1, atest_char0, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(61, uim_batt, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(62, atest_char, qdss_tracedata_a, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(63, cdc_pdm0, bimc_dte0, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(64, cdc_pdm0, bimc_dte1, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(65, cdc_pdm0, bimc_dte0, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(66, cdc_pdm0, bimc_dte1, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(67, cdc_pdm0, ebi0_wrcdc, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(68, cdc_pdm0, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(69, blsp_spi3_cs3, qdss_tracedata_a, NA, atest_combodac, NA, NA, NA, NA, NA),
	PINGROUP(70, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(71, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(72, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(73, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(74, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(75, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(76, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(77, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(78, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(79, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(80, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(81, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(82, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(83, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(84, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(85, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(86, NA, pa_indicator, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(87, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(88, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(89, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(90, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(91, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(92, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(93, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(94, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(95, NA, modem_tsync, nav_tsync, nav_pps, NA, NA, NA, NA, NA),
	PINGROUP(96, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(97, gcc_gp1_clk_b, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(98, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(99, gsm0_tx0, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(100, gsm0_tx1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(101, gsm1_tx0, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(102, gsm1_tx1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(103, ssbi_wtr0, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(104, ssbi_wtr0, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(105, ssbi_wtr1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(106, ssbi_wtr1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(107, pbs0, NA, atest_combodac, NA, NA, NA, NA, NA, NA),
	PINGROUP(108, pbs1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(109, pbs2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(110, blsp_spi1_cs1, pri_mi2s_ws, NA, qdss_tracedata_b, NA, NA, NA, NA, NA),
	PINGROUP(111, qdss_tracedata_b, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(112, sec_mi2s, NA, NA, NA, qdss_tracedata_a, NA, atest_tsens, NA, NA),
	PINGROUP(113, pri_mi2s, NA, pwr_modem_enabled_b, NA, NA, NA, NA, NA, qdss_tracedata_a),
	PINGROUP(114, pri_mi2s, pwr_nav_enabled_b, NA, NA, NA, NA, NA, qdss_tracedata_a, NA),
	PINGROUP(115, pri_mi2s, pwr_crypto_enabled_b, NA, NA, NA, NA, NA, qdss_tracedata_a, NA),
	PINGROUP(116, pri_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(117, sec_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(118, sec_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(119, sec_mi2s, m_voc, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(120, blsp_spi3_cs1, ldo_update, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(121, sd_write, blsp_spi2_cs1, ldo_en, NA, NA, NA, NA, NA, NA),
	SDC_PINGROUP(sdc1_clk, 0x10a000, 13, 6),
	SDC_PINGROUP(sdc1_cmd, 0x10a000, 11, 3),
	SDC_PINGROUP(sdc1_data, 0x10a000, 9, 0),
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

#define NUM_GPIO_PINGROUPS	122

static const struct msm_pinctrl_soc_data msm8916_pinctrl = {
	.pins = msm8916_pins,
	.npins = ARRAY_SIZE(msm8916_pins),
	.functions = msm8916_functions,
	.nfunctions = ARRAY_SIZE(msm8916_functions),
	.groups = msm8916_groups,
	.ngroups = ARRAY_SIZE(msm8916_groups),
	.ngpios = NUM_GPIO_PINGROUPS,
};

static int msm8916_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &msm8916_pinctrl);
}

static const struct of_device_id msm8916_pinctrl_of_match[] = {
	{ .compatible = "qcom,msm8916-pinctrl", },
	{ },
};

static struct platform_driver msm8916_pinctrl_driver = {
	.driver = {
		.name = "msm8916-pinctrl",
		.of_match_table = msm8916_pinctrl_of_match,
	},
	.probe = msm8916_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init msm8916_pinctrl_init(void)
{
	return platform_driver_register(&msm8916_pinctrl_driver);
}
arch_initcall(msm8916_pinctrl_init);

static void __exit msm8916_pinctrl_exit(void)
{
	platform_driver_unregister(&msm8916_pinctrl_driver);
}
module_exit(msm8916_pinctrl_exit);

MODULE_DESCRIPTION("Qualcomm msm8916 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, msm8916_pinctrl_of_match);
