// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, Sony Mobile Communications AB.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinmux.h>

#include "pinctrl-msm.h"

static const struct pinctrl_pin_desc msm8960_pins[] = {
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
	PINCTRL_PIN(122, "GPIO_122"),
	PINCTRL_PIN(123, "GPIO_123"),
	PINCTRL_PIN(124, "GPIO_124"),
	PINCTRL_PIN(125, "GPIO_125"),
	PINCTRL_PIN(126, "GPIO_126"),
	PINCTRL_PIN(127, "GPIO_127"),
	PINCTRL_PIN(128, "GPIO_128"),
	PINCTRL_PIN(129, "GPIO_129"),
	PINCTRL_PIN(130, "GPIO_130"),
	PINCTRL_PIN(131, "GPIO_131"),
	PINCTRL_PIN(132, "GPIO_132"),
	PINCTRL_PIN(133, "GPIO_133"),
	PINCTRL_PIN(134, "GPIO_134"),
	PINCTRL_PIN(135, "GPIO_135"),
	PINCTRL_PIN(136, "GPIO_136"),
	PINCTRL_PIN(137, "GPIO_137"),
	PINCTRL_PIN(138, "GPIO_138"),
	PINCTRL_PIN(139, "GPIO_139"),
	PINCTRL_PIN(140, "GPIO_140"),
	PINCTRL_PIN(141, "GPIO_141"),
	PINCTRL_PIN(142, "GPIO_142"),
	PINCTRL_PIN(143, "GPIO_143"),
	PINCTRL_PIN(144, "GPIO_144"),
	PINCTRL_PIN(145, "GPIO_145"),
	PINCTRL_PIN(146, "GPIO_146"),
	PINCTRL_PIN(147, "GPIO_147"),
	PINCTRL_PIN(148, "GPIO_148"),
	PINCTRL_PIN(149, "GPIO_149"),
	PINCTRL_PIN(150, "GPIO_150"),
	PINCTRL_PIN(151, "GPIO_151"),

	PINCTRL_PIN(152, "SDC1_CLK"),
	PINCTRL_PIN(153, "SDC1_CMD"),
	PINCTRL_PIN(154, "SDC1_DATA"),
	PINCTRL_PIN(155, "SDC3_CLK"),
	PINCTRL_PIN(156, "SDC3_CMD"),
	PINCTRL_PIN(157, "SDC3_DATA"),
};

#define DECLARE_MSM_GPIO_PINS(pin) static const unsigned int gpio##pin##_pins[] = { pin }
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
DECLARE_MSM_GPIO_PINS(134);
DECLARE_MSM_GPIO_PINS(135);
DECLARE_MSM_GPIO_PINS(136);
DECLARE_MSM_GPIO_PINS(137);
DECLARE_MSM_GPIO_PINS(138);
DECLARE_MSM_GPIO_PINS(139);
DECLARE_MSM_GPIO_PINS(140);
DECLARE_MSM_GPIO_PINS(141);
DECLARE_MSM_GPIO_PINS(142);
DECLARE_MSM_GPIO_PINS(143);
DECLARE_MSM_GPIO_PINS(144);
DECLARE_MSM_GPIO_PINS(145);
DECLARE_MSM_GPIO_PINS(146);
DECLARE_MSM_GPIO_PINS(147);
DECLARE_MSM_GPIO_PINS(148);
DECLARE_MSM_GPIO_PINS(149);
DECLARE_MSM_GPIO_PINS(150);
DECLARE_MSM_GPIO_PINS(151);

static const unsigned int sdc1_clk_pins[] = { 152 };
static const unsigned int sdc1_cmd_pins[] = { 153 };
static const unsigned int sdc1_data_pins[] = { 154 };
static const unsigned int sdc3_clk_pins[] = { 155 };
static const unsigned int sdc3_cmd_pins[] = { 156 };
static const unsigned int sdc3_data_pins[] = { 157 };

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11) \
	{						\
		.grp = PINCTRL_PINGROUP("gpio" #id, 	\
			gpio##id##_pins, 		\
			ARRAY_SIZE(gpio##id##_pins)),	\
		.funcs = (int[]){			\
			msm_mux_gpio,			\
			msm_mux_##f1,			\
			msm_mux_##f2,			\
			msm_mux_##f3,			\
			msm_mux_##f4,			\
			msm_mux_##f5,			\
			msm_mux_##f6,			\
			msm_mux_##f7,			\
			msm_mux_##f8,			\
			msm_mux_##f9,			\
			msm_mux_##f10,			\
			msm_mux_##f11			\
		},					\
		.nfuncs = 12,				\
		.ctl_reg = 0x1000 + 0x10 * id,		\
		.io_reg = 0x1004 + 0x10 * id,		\
		.intr_cfg_reg = 0x1008 + 0x10 * id,	\
		.intr_status_reg = 0x100c + 0x10 * id,	\
		.intr_target_reg = 0x400 + 0x4 * id,	\
		.mux_bit = 2,				\
		.pull_bit = 0,				\
		.drv_bit = 6,				\
		.oe_bit = 9,				\
		.in_bit = 0,				\
		.out_bit = 1,				\
		.intr_enable_bit = 0,			\
		.intr_status_bit = 0,			\
		.intr_ack_high = 1,			\
		.intr_target_bit = 0,			\
		.intr_target_kpss_val = 4,		\
		.intr_raw_status_bit = 3,		\
		.intr_polarity_bit = 1,			\
		.intr_detection_bit = 2,		\
		.intr_detection_width = 1,		\
	}

#define SDC_PINGROUP(pg_name, ctl, pull, drv)		\
	{						\
		.grp = PINCTRL_PINGROUP(#pg_name, 	\
			pg_name##_pins, 		\
			ARRAY_SIZE(pg_name##_pins)),	\
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

enum msm8960_functions {
	msm_mux_audio_pcm,
	msm_mux_bt,
	msm_mux_cam_mclk0,
	msm_mux_cam_mclk1,
	msm_mux_cam_mclk2,
	msm_mux_codec_mic_i2s,
	msm_mux_codec_spkr_i2s,
	msm_mux_ext_gps,
	msm_mux_fm,
	msm_mux_gps_blanking,
	msm_mux_gps_pps_in,
	msm_mux_gps_pps_out,
	msm_mux_gp_clk_0a,
	msm_mux_gp_clk_0b,
	msm_mux_gp_clk_1a,
	msm_mux_gp_clk_1b,
	msm_mux_gp_clk_2a,
	msm_mux_gp_clk_2b,
	msm_mux_gp_mn,
	msm_mux_gp_pdm_0a,
	msm_mux_gp_pdm_0b,
	msm_mux_gp_pdm_1a,
	msm_mux_gp_pdm_1b,
	msm_mux_gp_pdm_2a,
	msm_mux_gp_pdm_2b,
	msm_mux_gpio,
	msm_mux_gsbi1,
	msm_mux_gsbi1_spi_cs1_n,
	msm_mux_gsbi1_spi_cs2a_n,
	msm_mux_gsbi1_spi_cs2b_n,
	msm_mux_gsbi1_spi_cs3_n,
	msm_mux_gsbi2,
	msm_mux_gsbi2_spi_cs1_n,
	msm_mux_gsbi2_spi_cs2_n,
	msm_mux_gsbi2_spi_cs3_n,
	msm_mux_gsbi3,
	msm_mux_gsbi4,
	msm_mux_gsbi4_3d_cam_i2c_l,
	msm_mux_gsbi4_3d_cam_i2c_r,
	msm_mux_gsbi5,
	msm_mux_gsbi5_3d_cam_i2c_l,
	msm_mux_gsbi5_3d_cam_i2c_r,
	msm_mux_gsbi6,
	msm_mux_gsbi7,
	msm_mux_gsbi8,
	msm_mux_gsbi9,
	msm_mux_gsbi10,
	msm_mux_gsbi11,
	msm_mux_gsbi11_spi_cs1a_n,
	msm_mux_gsbi11_spi_cs1b_n,
	msm_mux_gsbi11_spi_cs2a_n,
	msm_mux_gsbi11_spi_cs2b_n,
	msm_mux_gsbi11_spi_cs3_n,
	msm_mux_gsbi12,
	msm_mux_hdmi_cec,
	msm_mux_hdmi_ddc_clock,
	msm_mux_hdmi_ddc_data,
	msm_mux_hdmi_hot_plug_detect,
	msm_mux_hsic,
	msm_mux_mdp_vsync,
	msm_mux_mi2s,
	msm_mux_mic_i2s,
	msm_mux_pmb_clk,
	msm_mux_pmb_ext_ctrl,
	msm_mux_ps_hold,
	msm_mux_rpm_wdog,
	msm_mux_sdc2,
	msm_mux_sdc4,
	msm_mux_sdc5,
	msm_mux_slimbus1,
	msm_mux_slimbus2,
	msm_mux_spkr_i2s,
	msm_mux_ssbi1,
	msm_mux_ssbi2,
	msm_mux_ssbi_ext_gps,
	msm_mux_ssbi_pmic2,
	msm_mux_ssbi_qpa1,
	msm_mux_ssbi_ts,
	msm_mux_tsif1,
	msm_mux_tsif2,
	msm_mux_ts_eoc,
	msm_mux_usb_fs1,
	msm_mux_usb_fs1_oe,
	msm_mux_usb_fs1_oe_n,
	msm_mux_usb_fs2,
	msm_mux_usb_fs2_oe,
	msm_mux_usb_fs2_oe_n,
	msm_mux_vfe_camif_timer1_a,
	msm_mux_vfe_camif_timer1_b,
	msm_mux_vfe_camif_timer2,
	msm_mux_vfe_camif_timer3_a,
	msm_mux_vfe_camif_timer3_b,
	msm_mux_vfe_camif_timer4_a,
	msm_mux_vfe_camif_timer4_b,
	msm_mux_vfe_camif_timer4_c,
	msm_mux_vfe_camif_timer5_a,
	msm_mux_vfe_camif_timer5_b,
	msm_mux_vfe_camif_timer6_a,
	msm_mux_vfe_camif_timer6_b,
	msm_mux_vfe_camif_timer6_c,
	msm_mux_vfe_camif_timer7_a,
	msm_mux_vfe_camif_timer7_b,
	msm_mux_vfe_camif_timer7_c,
	msm_mux_wlan,
	msm_mux_NA,
};

static const char * const audio_pcm_groups[] = {
	"gpio63", "gpio64", "gpio65", "gpio66"
};

static const char * const bt_groups[] = {
	"gpio28", "gpio29", "gpio83"
};

static const char * const cam_mclk0_groups[] = {
	"gpio5"
};

static const char * const cam_mclk1_groups[] = {
	"gpio4"
};

static const char * const cam_mclk2_groups[] = {
	"gpio2"
};

static const char * const codec_mic_i2s_groups[] = {
	"gpio54", "gpio55", "gpio56", "gpio57", "gpio58"
};

static const char * const codec_spkr_i2s_groups[] = {
	"gpio59", "gpio60", "gpio61", "gpio62"
};

static const char * const ext_gps_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25"
};

static const char * const fm_groups[] = {
	"gpio26", "gpio27"
};

static const char * const gps_blanking_groups[] = {
	"gpio137"
};

static const char * const gps_pps_in_groups[] = {
	"gpio37"
};

static const char * const gps_pps_out_groups[] = {
	"gpio37"
};

static const char * const gp_clk_0a_groups[] = {
	"gpio3"
};

static const char * const gp_clk_0b_groups[] = {
	"gpio54"
};

static const char * const gp_clk_1a_groups[] = {
	"gpio4"
};

static const char * const gp_clk_1b_groups[] = {
	"gpio70"
};

static const char * const gp_clk_2a_groups[] = {
	"gpio52"
};

static const char * const gp_clk_2b_groups[] = {
	"gpio37"
};

static const char * const gp_mn_groups[] = {
	"gpio2"
};

static const char * const gp_pdm_0a_groups[] = {
	"gpio58"
};

static const char * const gp_pdm_0b_groups[] = {
	"gpio39"
};

static const char * const gp_pdm_1a_groups[] = {
	"gpio94"
};

static const char * const gp_pdm_1b_groups[] = {
	"gpio64"
};

static const char * const gp_pdm_2a_groups[] = {
	"gpio69"
};

static const char * const gp_pdm_2b_groups[] = {
	"gpio53"
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
	"gpio117", "gpio118", "gpio119", "gpio120", "gpio121", "gpio122",
	"gpio123", "gpio124", "gpio125", "gpio126", "gpio127", "gpio128",
	"gpio129", "gpio130", "gpio131", "gpio132", "gpio133", "gpio134",
	"gpio135", "gpio136", "gpio137", "gpio138", "gpio139", "gpio140",
	"gpio141", "gpio142", "gpio143", "gpio144", "gpio145", "gpio146",
	"gpio147", "gpio148", "gpio149", "gpio150", "gpio151"
};

static const char * const gsbi1_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9"
};

static const char * const gsbi1_spi_cs1_n_groups[] = {
	"gpio14"
};

static const char * const gsbi1_spi_cs2a_n_groups[] = {
	"gpio15"
};

static const char * const gsbi1_spi_cs2b_n_groups[] = {
	"gpio17"
};

static const char * const gsbi1_spi_cs3_n_groups[] = {
	"gpio16"
};

static const char * const gsbi2_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13"
};

static const char * const gsbi2_spi_cs1_n_groups[] = {
	"gpio52"
};

static const char * const gsbi2_spi_cs2_n_groups[] = {
	"gpio68"
};

static const char * const gsbi2_spi_cs3_n_groups[] = {
	"gpio56"
};

static const char * const gsbi3_groups[] = {
	"gpio14", "gpio15", "gpio16", "gpio17"
};

static const char * const gsbi4_groups[] = {
	"gpio18", "gpio19", "gpio20", "gpio21"
};

static const char * const gsbi4_3d_cam_i2c_l_groups[] = {
	"gpio18", "gpio19"
};

static const char * const gsbi4_3d_cam_i2c_r_groups[] = {
	"gpio20", "gpio21"
};

static const char * const gsbi5_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25"
};

static const char * const gsbi5_3d_cam_i2c_l_groups[] = {
	"gpio22", "gpio23"
};

static const char * const gsbi5_3d_cam_i2c_r_groups[] = {
	"gpio24", "gpio25"
};

static const char * const gsbi6_groups[] = {
	"gpio26", "gpio27", "gpio28", "gpio29"
};

static const char * const gsbi7_groups[] = {
	"gpio30", "gpio31", "gpio32", "gpio33"
};

static const char * const gsbi8_groups[] = {
	"gpio34", "gpio35", "gpio36", "gpio37"
};

static const char * const gsbi9_groups[] = {
	"gpio93", "gpio94", "gpio95", "gpio96"
};

static const char * const gsbi10_groups[] = {
	"gpio71", "gpio72", "gpio73", "gpio74"
};

static const char * const gsbi11_groups[] = {
	"gpio38", "gpio39", "gpio40", "gpio41"
};

static const char * const gsbi11_spi_cs1a_n_groups[] = {
	"gpio36"
};

static const char * const gsbi11_spi_cs1b_n_groups[] = {
	"gpio18"
};

static const char * const gsbi11_spi_cs2a_n_groups[] = {
	"gpio37"
};

static const char * const gsbi11_spi_cs2b_n_groups[] = {
	"gpio19"
};

static const char * const gsbi11_spi_cs3_n_groups[] = {
	"gpio76"
};

static const char * const gsbi12_groups[] = {
	"gpio42", "gpio43", "gpio44", "gpio45"
};

static const char * const hdmi_cec_groups[] = {
	"gpio99"
};

static const char * const hdmi_ddc_clock_groups[] = {
	"gpio100"
};

static const char * const hdmi_ddc_data_groups[] = {
	"gpio101"
};

static const char * const hdmi_hot_plug_detect_groups[] = {
	"gpio102"
};

static const char * const hsic_groups[] = {
	"gpio150", "gpio151"
};

static const char * const mdp_vsync_groups[] = {
	"gpio0", "gpio1", "gpio19"
};

static const char * const mi2s_groups[] = {
	"gpio47", "gpio48", "gpio49", "gpio50", "gpio51", "gpio52", "gpio53"
};

static const char * const mic_i2s_groups[] = {
	"gpio71", "gpio72", "gpio73", "gpio74"
};

static const char * const pmb_clk_groups[] = {
	"gpio21", "gpio86", "gpio112"
};

static const char * const pmb_ext_ctrl_groups[] = {
	"gpio4", "gpio5"
};

static const char * const ps_hold_groups[] = {
	"gpio108"
};

static const char * const rpm_wdog_groups[] = {
	"gpio12"
};

static const char * const sdc2_groups[] = {
	"gpio89", "gpio90", "gpio91", "gpio92", "gpio93", "gpio94", "gpio95",
	"gpio96", "gpio97", "gpio98"
};

static const char * const sdc4_groups[] = {
	"gpio83", "gpio84", "gpio85", "gpio86", "gpio87", "gpio88"
};

static const char * const sdc5_groups[] = {
	"gpio77", "gpio78", "gpio79", "gpio80", "gpio81", "gpio82"
};

static const char * const slimbus1_groups[] = {
	"gpio50", "gpio51", "gpio60", "gpio61"
};

static const char * const slimbus2_groups[] = {
	"gpio42", "gpio43"
};

static const char * const spkr_i2s_groups[] = {
	"gpio67", "gpio68", "gpio69", "gpio70"
};

static const char * const ssbi1_groups[] = {
	"gpio141", "gpio143"
};

static const char * const ssbi2_groups[] = {
	"gpio140", "gpio142"
};

static const char * const ssbi_ext_gps_groups[] = {
	"gpio23"
};

static const char * const ssbi_pmic2_groups[] = {
	"gpio149"
};

static const char * const ssbi_qpa1_groups[] = {
	"gpio131"
};

static const char * const ssbi_ts_groups[] = {
	"gpio10"
};

static const char * const tsif1_groups[] = {
	"gpio75", "gpio76", "gpio77", "gpio82"
};

static const char * const tsif2_groups[] = {
	"gpio78", "gpio79", "gpio80", "gpio81"
};

static const char * const ts_eoc_groups[] = {
	"gpio11"
};

static const char * const usb_fs1_groups[] = {
	"gpio32", "gpio33"
};

static const char * const usb_fs1_oe_groups[] = {
	"gpio31"
};

static const char * const usb_fs1_oe_n_groups[] = {
	"gpio31"
};

static const char * const usb_fs2_groups[] = {
	"gpio34", "gpio35"
};

static const char * const usb_fs2_oe_groups[] = {
	"gpio36"
};

static const char * const usb_fs2_oe_n_groups[] = {
	"gpio36"
};

static const char * const vfe_camif_timer1_a_groups[] = {
	"gpio2"
};

static const char * const vfe_camif_timer1_b_groups[] = {
	"gpio38"
};

static const char * const vfe_camif_timer2_groups[] = {
	"gpio3"
};

static const char * const vfe_camif_timer3_a_groups[] = {
	"gpio4"
};

static const char * const vfe_camif_timer3_b_groups[] = {
	"gpio151"
};

static const char * const vfe_camif_timer4_a_groups[] = {
	"gpio65"
};

static const char * const vfe_camif_timer4_b_groups[] = {
	"gpio150"
};

static const char * const vfe_camif_timer4_c_groups[] = {
	"gpio10"
};

static const char * const vfe_camif_timer5_a_groups[] = {
	"gpio66"
};

static const char * const vfe_camif_timer5_b_groups[] = {
	"gpio39"
};

static const char * const vfe_camif_timer6_a_groups[] = {
	"gpio71"
};

static const char * const vfe_camif_timer6_b_groups[] = {
	"gpio0"
};

static const char * const vfe_camif_timer6_c_groups[] = {
	"gpio18"
};

static const char * const vfe_camif_timer7_a_groups[] = {
	"gpio67"
};

static const char * const vfe_camif_timer7_b_groups[] = {
	"gpio1"
};

static const char * const vfe_camif_timer7_c_groups[] = {
	"gpio19"
};

static const char * const wlan_groups[] = {
	"gpio84", "gpio85", "gpio86", "gpio87", "gpio88"
};

static const struct pinfunction msm8960_functions[] = {
	MSM_PIN_FUNCTION(audio_pcm),
	MSM_PIN_FUNCTION(bt),
	MSM_PIN_FUNCTION(cam_mclk0),
	MSM_PIN_FUNCTION(cam_mclk1),
	MSM_PIN_FUNCTION(cam_mclk2),
	MSM_PIN_FUNCTION(codec_mic_i2s),
	MSM_PIN_FUNCTION(codec_spkr_i2s),
	MSM_PIN_FUNCTION(ext_gps),
	MSM_PIN_FUNCTION(fm),
	MSM_PIN_FUNCTION(gps_blanking),
	MSM_PIN_FUNCTION(gps_pps_in),
	MSM_PIN_FUNCTION(gps_pps_out),
	MSM_PIN_FUNCTION(gp_clk_0a),
	MSM_PIN_FUNCTION(gp_clk_0b),
	MSM_PIN_FUNCTION(gp_clk_1a),
	MSM_PIN_FUNCTION(gp_clk_1b),
	MSM_PIN_FUNCTION(gp_clk_2a),
	MSM_PIN_FUNCTION(gp_clk_2b),
	MSM_PIN_FUNCTION(gp_mn),
	MSM_PIN_FUNCTION(gp_pdm_0a),
	MSM_PIN_FUNCTION(gp_pdm_0b),
	MSM_PIN_FUNCTION(gp_pdm_1a),
	MSM_PIN_FUNCTION(gp_pdm_1b),
	MSM_PIN_FUNCTION(gp_pdm_2a),
	MSM_PIN_FUNCTION(gp_pdm_2b),
	MSM_GPIO_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(gsbi1),
	MSM_PIN_FUNCTION(gsbi1_spi_cs1_n),
	MSM_PIN_FUNCTION(gsbi1_spi_cs2a_n),
	MSM_PIN_FUNCTION(gsbi1_spi_cs2b_n),
	MSM_PIN_FUNCTION(gsbi1_spi_cs3_n),
	MSM_PIN_FUNCTION(gsbi2),
	MSM_PIN_FUNCTION(gsbi2_spi_cs1_n),
	MSM_PIN_FUNCTION(gsbi2_spi_cs2_n),
	MSM_PIN_FUNCTION(gsbi2_spi_cs3_n),
	MSM_PIN_FUNCTION(gsbi3),
	MSM_PIN_FUNCTION(gsbi4),
	MSM_PIN_FUNCTION(gsbi4_3d_cam_i2c_l),
	MSM_PIN_FUNCTION(gsbi4_3d_cam_i2c_r),
	MSM_PIN_FUNCTION(gsbi5),
	MSM_PIN_FUNCTION(gsbi5_3d_cam_i2c_l),
	MSM_PIN_FUNCTION(gsbi5_3d_cam_i2c_r),
	MSM_PIN_FUNCTION(gsbi6),
	MSM_PIN_FUNCTION(gsbi7),
	MSM_PIN_FUNCTION(gsbi8),
	MSM_PIN_FUNCTION(gsbi9),
	MSM_PIN_FUNCTION(gsbi10),
	MSM_PIN_FUNCTION(gsbi11),
	MSM_PIN_FUNCTION(gsbi11_spi_cs1a_n),
	MSM_PIN_FUNCTION(gsbi11_spi_cs1b_n),
	MSM_PIN_FUNCTION(gsbi11_spi_cs2a_n),
	MSM_PIN_FUNCTION(gsbi11_spi_cs2b_n),
	MSM_PIN_FUNCTION(gsbi11_spi_cs3_n),
	MSM_PIN_FUNCTION(gsbi12),
	MSM_PIN_FUNCTION(hdmi_cec),
	MSM_PIN_FUNCTION(hdmi_ddc_clock),
	MSM_PIN_FUNCTION(hdmi_ddc_data),
	MSM_PIN_FUNCTION(hdmi_hot_plug_detect),
	MSM_PIN_FUNCTION(hsic),
	MSM_PIN_FUNCTION(mdp_vsync),
	MSM_PIN_FUNCTION(mi2s),
	MSM_PIN_FUNCTION(mic_i2s),
	MSM_PIN_FUNCTION(pmb_clk),
	MSM_PIN_FUNCTION(pmb_ext_ctrl),
	MSM_PIN_FUNCTION(ps_hold),
	MSM_PIN_FUNCTION(rpm_wdog),
	MSM_PIN_FUNCTION(sdc2),
	MSM_PIN_FUNCTION(sdc4),
	MSM_PIN_FUNCTION(sdc5),
	MSM_PIN_FUNCTION(slimbus1),
	MSM_PIN_FUNCTION(slimbus2),
	MSM_PIN_FUNCTION(spkr_i2s),
	MSM_PIN_FUNCTION(ssbi1),
	MSM_PIN_FUNCTION(ssbi2),
	MSM_PIN_FUNCTION(ssbi_ext_gps),
	MSM_PIN_FUNCTION(ssbi_pmic2),
	MSM_PIN_FUNCTION(ssbi_qpa1),
	MSM_PIN_FUNCTION(ssbi_ts),
	MSM_PIN_FUNCTION(tsif1),
	MSM_PIN_FUNCTION(tsif2),
	MSM_PIN_FUNCTION(ts_eoc),
	MSM_PIN_FUNCTION(usb_fs1),
	MSM_PIN_FUNCTION(usb_fs1_oe),
	MSM_PIN_FUNCTION(usb_fs1_oe_n),
	MSM_PIN_FUNCTION(usb_fs2),
	MSM_PIN_FUNCTION(usb_fs2_oe),
	MSM_PIN_FUNCTION(usb_fs2_oe_n),
	MSM_PIN_FUNCTION(vfe_camif_timer1_a),
	MSM_PIN_FUNCTION(vfe_camif_timer1_b),
	MSM_PIN_FUNCTION(vfe_camif_timer2),
	MSM_PIN_FUNCTION(vfe_camif_timer3_a),
	MSM_PIN_FUNCTION(vfe_camif_timer3_b),
	MSM_PIN_FUNCTION(vfe_camif_timer4_a),
	MSM_PIN_FUNCTION(vfe_camif_timer4_b),
	MSM_PIN_FUNCTION(vfe_camif_timer4_c),
	MSM_PIN_FUNCTION(vfe_camif_timer5_a),
	MSM_PIN_FUNCTION(vfe_camif_timer5_b),
	MSM_PIN_FUNCTION(vfe_camif_timer6_a),
	MSM_PIN_FUNCTION(vfe_camif_timer6_b),
	MSM_PIN_FUNCTION(vfe_camif_timer6_c),
	MSM_PIN_FUNCTION(vfe_camif_timer7_a),
	MSM_PIN_FUNCTION(vfe_camif_timer7_b),
	MSM_PIN_FUNCTION(vfe_camif_timer7_c),
	MSM_PIN_FUNCTION(wlan),
};

static const struct msm_pingroup msm8960_groups[] = {
	PINGROUP(0, mdp_vsync, vfe_camif_timer6_b, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(1, mdp_vsync, vfe_camif_timer7_b, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(2, vfe_camif_timer1_a, gp_mn, NA, cam_mclk2, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(3, vfe_camif_timer2, gp_clk_0a, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(4, vfe_camif_timer3_a, cam_mclk1, gp_clk_1a, pmb_ext_ctrl, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(5, cam_mclk0, pmb_ext_ctrl, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(6, gsbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(7, gsbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(8, gsbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(9, gsbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(10, gsbi2, ssbi_ts, NA, vfe_camif_timer4_c, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(11, gsbi2, ts_eoc, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(12, gsbi2, rpm_wdog, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(13, gsbi2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(14, gsbi3, gsbi1_spi_cs1_n, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(15, gsbi3, gsbi1_spi_cs2a_n, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(16, gsbi3, gsbi1_spi_cs3_n, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(17, gsbi3, gsbi1_spi_cs2b_n, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(18, gsbi4, gsbi11_spi_cs1b_n, NA, NA, gsbi4_3d_cam_i2c_l, vfe_camif_timer6_c, NA, NA, NA, NA, NA),
	PINGROUP(19, gsbi4, gsbi11_spi_cs2b_n, NA, mdp_vsync, NA, gsbi4_3d_cam_i2c_l, vfe_camif_timer7_c, NA, NA, NA, NA),
	PINGROUP(20, gsbi4, gsbi4_3d_cam_i2c_r, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(21, gsbi4, pmb_clk, gsbi4_3d_cam_i2c_r, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(22, gsbi5, ext_gps, NA, NA, NA, NA, NA, NA, NA, gsbi5_3d_cam_i2c_l, NA),
	PINGROUP(23, gsbi5, ssbi_ext_gps, NA, NA, NA, NA, NA, NA, NA, gsbi5_3d_cam_i2c_l, NA),
	PINGROUP(24, gsbi5, ext_gps, NA, NA, NA, NA, NA, NA, NA, gsbi5_3d_cam_i2c_r, NA),
	PINGROUP(25, gsbi5, ext_gps, NA, NA, NA, NA, NA, NA, NA, gsbi5_3d_cam_i2c_r, NA),
	PINGROUP(26, fm, gsbi6, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(27, fm, gsbi6, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(28, bt, gsbi6, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(29, bt, gsbi6, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(30, gsbi7, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(31, gsbi7, usb_fs1_oe, usb_fs1_oe_n, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(32, gsbi7, usb_fs1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(33, gsbi7, usb_fs1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(34, gsbi8, usb_fs2, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(35, gsbi8, usb_fs2, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(36, gsbi8, usb_fs2_oe, usb_fs2_oe_n, gsbi11_spi_cs1a_n, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(37, gsbi8, gps_pps_out, gps_pps_in, gsbi11_spi_cs2a_n, gp_clk_2b, NA, NA, NA, NA, NA, NA),
	PINGROUP(38, gsbi11, NA, NA, NA, NA, NA, NA, NA, NA, vfe_camif_timer1_b, NA),
	PINGROUP(39, gsbi11, gp_pdm_0b, NA, NA, NA, NA, NA, NA, NA, NA, vfe_camif_timer5_b),
	PINGROUP(40, gsbi11, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(41, gsbi11, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(42, gsbi12, slimbus2, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(43, gsbi12, slimbus2, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(44, gsbi12, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(45, gsbi12, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(46, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(47, mi2s, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(48, mi2s, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(49, mi2s, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(50, mi2s, slimbus1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(51, mi2s, slimbus1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(52, mi2s, gp_clk_2a, gsbi2_spi_cs1_n, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(53, mi2s, gp_pdm_2b, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(54, codec_mic_i2s, gp_clk_0b, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(55, codec_mic_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(56, codec_mic_i2s, gsbi2_spi_cs3_n, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(57, codec_mic_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(58, codec_mic_i2s, gp_pdm_0a, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(59, codec_spkr_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(60, slimbus1, codec_spkr_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(61, slimbus1, codec_spkr_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(62, codec_spkr_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(63, audio_pcm, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(64, audio_pcm, gp_pdm_1b, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(65, audio_pcm, vfe_camif_timer4_a, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(66, audio_pcm, vfe_camif_timer5_a, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(67, spkr_i2s, vfe_camif_timer7_a, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(68, spkr_i2s, gsbi2_spi_cs2_n, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(69, spkr_i2s, gp_pdm_2a, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(70, spkr_i2s, gp_clk_1b, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(71, mic_i2s, gsbi10, vfe_camif_timer6_a, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(72, mic_i2s, gsbi10, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(73, mic_i2s, gsbi10, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(74, mic_i2s, gsbi10, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(75, tsif1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(76, tsif1, gsbi11_spi_cs3_n, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(77, tsif1, sdc5, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(78, tsif2, sdc5, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(79, tsif2, sdc5, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(80, tsif2, sdc5, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(81, tsif2, sdc5, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(82, tsif1, sdc5, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(83, bt, sdc4, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(84, wlan, sdc4, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(85, wlan, sdc4, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(86, wlan, sdc4, pmb_clk, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(87, wlan, sdc4, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(88, wlan, sdc4, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(89, sdc2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(90, sdc2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(91, sdc2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(92, sdc2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(93, sdc2, gsbi9, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(94, sdc2, gsbi9, gp_pdm_1a, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(95, sdc2, gsbi9, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(96, sdc2, gsbi9, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(97, sdc2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(98, sdc2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(99, hdmi_cec, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(100, hdmi_ddc_clock, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(101, hdmi_ddc_data, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(102, hdmi_hot_plug_detect, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(103, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(104, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(105, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(106, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(107, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(108, ps_hold, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(109, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(110, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(111, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(112, NA, pmb_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(113, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(114, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(115, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(116, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(117, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(118, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(119, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(120, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(121, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(122, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(123, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(124, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(125, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(126, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(127, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(128, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(129, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(130, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(131, NA, ssbi_qpa1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(132, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(133, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(134, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(135, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(136, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(137, gps_blanking, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(138, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(139, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(140, ssbi2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(141, ssbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(142, ssbi2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(143, ssbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(144, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(145, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(146, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(147, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(148, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(149, ssbi_pmic2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(150, hsic, NA, vfe_camif_timer4_b, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(151, hsic, NA, vfe_camif_timer3_b, NA, NA, NA, NA, NA, NA, NA, NA),

	SDC_PINGROUP(sdc1_clk, 0x20a0, 13, 6),
	SDC_PINGROUP(sdc1_cmd, 0x20a0, 11, 3),
	SDC_PINGROUP(sdc1_data, 0x20a0, 9, 0),

	SDC_PINGROUP(sdc3_clk, 0x20a4, 14, 6),
	SDC_PINGROUP(sdc3_cmd, 0x20a4, 11, 3),
	SDC_PINGROUP(sdc3_data, 0x20a4, 9, 0),
};

#define NUM_GPIO_PINGROUPS 152

static const struct msm_pinctrl_soc_data msm8960_pinctrl = {
	.pins = msm8960_pins,
	.npins = ARRAY_SIZE(msm8960_pins),
	.functions = msm8960_functions,
	.nfunctions = ARRAY_SIZE(msm8960_functions),
	.groups = msm8960_groups,
	.ngroups = ARRAY_SIZE(msm8960_groups),
	.ngpios = NUM_GPIO_PINGROUPS,
};

static int msm8960_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &msm8960_pinctrl);
}

static const struct of_device_id msm8960_pinctrl_of_match[] = {
	{ .compatible = "qcom,msm8960-pinctrl", },
	{ },
};

static struct platform_driver msm8960_pinctrl_driver = {
	.driver = {
		.name = "msm8960-pinctrl",
		.of_match_table = msm8960_pinctrl_of_match,
	},
	.probe = msm8960_pinctrl_probe,
};

static int __init msm8960_pinctrl_init(void)
{
	return platform_driver_register(&msm8960_pinctrl_driver);
}
arch_initcall(msm8960_pinctrl_init);

static void __exit msm8960_pinctrl_exit(void)
{
	platform_driver_unregister(&msm8960_pinctrl_driver);
}
module_exit(msm8960_pinctrl_exit);

MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@sonymobile.com>");
MODULE_DESCRIPTION("Qualcomm MSM8960 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, msm8960_pinctrl_of_match);
