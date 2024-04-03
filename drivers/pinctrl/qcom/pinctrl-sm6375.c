// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Konrad Dybcio <konrad.dybcio@somainline.org>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define REG_BASE 0x100000
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
		.ctl_reg = REG_SIZE * id,		\
		.io_reg = REG_SIZE * id + 0x4,		\
		.intr_cfg_reg = REG_SIZE * id + 0x8,	\
		.intr_status_reg = REG_SIZE * id + 0xc,	\
		.intr_target_reg = REG_SIZE * id + 0x8,	\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
		.egpio_enable = 12,		\
		.egpio_present = 11,		\
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

#define SDC_PINGROUP(pg_name, ctl, pull, drv)	\
	{					        \
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
		.intr_raw_status_bit = -1,		\
		.intr_polarity_bit = -1,		\
		.intr_detection_bit = -1,		\
		.intr_detection_width = -1,		\
	}

#define UFS_RESET(pg_name, offset)				\
	{					        \
		.grp = PINCTRL_PINGROUP(#pg_name, 	\
			pg_name##_pins, 		\
			ARRAY_SIZE(pg_name##_pins)),	\
		.ctl_reg = offset,			\
		.io_reg = offset + 0x4,			\
		.intr_cfg_reg = 0,			\
		.intr_status_reg = 0,			\
		.intr_target_reg = 0,			\
		.mux_bit = -1,				\
		.pull_bit = 3,				\
		.drv_bit = 0,				\
		.oe_bit = -1,				\
		.in_bit = -1,				\
		.out_bit = 0,				\
		.intr_enable_bit = -1,			\
		.intr_status_bit = -1,			\
		.intr_target_bit = -1,			\
		.intr_raw_status_bit = -1,		\
		.intr_polarity_bit = -1,		\
		.intr_detection_bit = -1,		\
		.intr_detection_width = -1,		\
	}

static const struct pinctrl_pin_desc sm6375_pins[] = {
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
	PINCTRL_PIN(152, "GPIO_152"),
	PINCTRL_PIN(153, "GPIO_153"),
	PINCTRL_PIN(154, "GPIO_154"),
	PINCTRL_PIN(155, "GPIO_155"),
	PINCTRL_PIN(156, "UFS_RESET"),
	PINCTRL_PIN(157, "SDC1_RCLK"),
	PINCTRL_PIN(158, "SDC1_CLK"),
	PINCTRL_PIN(159, "SDC1_CMD"),
	PINCTRL_PIN(160, "SDC1_DATA"),
	PINCTRL_PIN(161, "SDC2_CLK"),
	PINCTRL_PIN(162, "SDC2_CMD"),
	PINCTRL_PIN(163, "SDC2_DATA"),
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
DECLARE_MSM_GPIO_PINS(152);
DECLARE_MSM_GPIO_PINS(153);
DECLARE_MSM_GPIO_PINS(154);
DECLARE_MSM_GPIO_PINS(155);


static const unsigned int sdc1_rclk_pins[] = { 157 };
static const unsigned int sdc1_clk_pins[] = { 158 };
static const unsigned int sdc1_cmd_pins[] = { 159 };
static const unsigned int sdc1_data_pins[] = { 160 };
static const unsigned int sdc2_clk_pins[] = { 161 };
static const unsigned int sdc2_cmd_pins[] = { 162 };
static const unsigned int sdc2_data_pins[] = { 163 };
static const unsigned int ufs_reset_pins[] = { 156 };

enum sm6375_functions {
	msm_mux_adsp_ext,
	msm_mux_agera_pll,
	msm_mux_atest_char,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_atest_tsens,
	msm_mux_atest_tsens2,
	msm_mux_atest_usb1,
	msm_mux_atest_usb10,
	msm_mux_atest_usb11,
	msm_mux_atest_usb12,
	msm_mux_atest_usb13,
	msm_mux_atest_usb2,
	msm_mux_atest_usb20,
	msm_mux_atest_usb21,
	msm_mux_atest_usb22,
	msm_mux_atest_usb23,
	msm_mux_audio_ref,
	msm_mux_btfm_slimbus,
	msm_mux_cam_mclk,
	msm_mux_cci_async,
	msm_mux_cci_i2c,
	msm_mux_cci_timer0,
	msm_mux_cci_timer1,
	msm_mux_cci_timer2,
	msm_mux_cci_timer3,
	msm_mux_cci_timer4,
	msm_mux_cri_trng,
	msm_mux_dbg_out,
	msm_mux_ddr_bist,
	msm_mux_ddr_pxi0,
	msm_mux_ddr_pxi1,
	msm_mux_ddr_pxi2,
	msm_mux_ddr_pxi3,
	msm_mux_dp_hot,
	msm_mux_edp_lcd,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_gp_pdm0,
	msm_mux_gp_pdm1,
	msm_mux_gp_pdm2,
	msm_mux_gpio,
	msm_mux_gps_tx,
	msm_mux_ibi_i3c,
	msm_mux_jitter_bist,
	msm_mux_ldo_en,
	msm_mux_ldo_update,
	msm_mux_lpass_ext,
	msm_mux_m_voc,
	msm_mux_mclk,
	msm_mux_mdp_vsync,
	msm_mux_mdp_vsync0,
	msm_mux_mdp_vsync1,
	msm_mux_mdp_vsync2,
	msm_mux_mdp_vsync3,
	msm_mux_mi2s_0,
	msm_mux_mi2s_1,
	msm_mux_mi2s_2,
	msm_mux_mss_lte,
	msm_mux_nav_gpio,
	msm_mux_nav_pps,
	msm_mux_pa_indicator,
	msm_mux_phase_flag0,
	msm_mux_phase_flag1,
	msm_mux_phase_flag10,
	msm_mux_phase_flag11,
	msm_mux_phase_flag12,
	msm_mux_phase_flag13,
	msm_mux_phase_flag14,
	msm_mux_phase_flag15,
	msm_mux_phase_flag16,
	msm_mux_phase_flag17,
	msm_mux_phase_flag18,
	msm_mux_phase_flag19,
	msm_mux_phase_flag2,
	msm_mux_phase_flag20,
	msm_mux_phase_flag21,
	msm_mux_phase_flag22,
	msm_mux_phase_flag23,
	msm_mux_phase_flag24,
	msm_mux_phase_flag25,
	msm_mux_phase_flag26,
	msm_mux_phase_flag27,
	msm_mux_phase_flag28,
	msm_mux_phase_flag29,
	msm_mux_phase_flag3,
	msm_mux_phase_flag30,
	msm_mux_phase_flag31,
	msm_mux_phase_flag4,
	msm_mux_phase_flag5,
	msm_mux_phase_flag6,
	msm_mux_phase_flag7,
	msm_mux_phase_flag8,
	msm_mux_phase_flag9,
	msm_mux_pll_bist,
	msm_mux_pll_bypassnl,
	msm_mux_pll_clk,
	msm_mux_pll_reset,
	msm_mux_prng_rosc0,
	msm_mux_prng_rosc1,
	msm_mux_prng_rosc2,
	msm_mux_prng_rosc3,
	msm_mux_qdss_cti,
	msm_mux_qdss_gpio,
	msm_mux_qdss_gpio0,
	msm_mux_qdss_gpio1,
	msm_mux_qdss_gpio10,
	msm_mux_qdss_gpio11,
	msm_mux_qdss_gpio12,
	msm_mux_qdss_gpio13,
	msm_mux_qdss_gpio14,
	msm_mux_qdss_gpio15,
	msm_mux_qdss_gpio2,
	msm_mux_qdss_gpio3,
	msm_mux_qdss_gpio4,
	msm_mux_qdss_gpio5,
	msm_mux_qdss_gpio6,
	msm_mux_qdss_gpio7,
	msm_mux_qdss_gpio8,
	msm_mux_qdss_gpio9,
	msm_mux_qlink0_enable,
	msm_mux_qlink0_request,
	msm_mux_qlink0_wmss,
	msm_mux_qlink1_enable,
	msm_mux_qlink1_request,
	msm_mux_qlink1_wmss,
	msm_mux_qup00,
	msm_mux_qup01,
	msm_mux_qup02,
	msm_mux_qup10,
	msm_mux_qup11_f1,
	msm_mux_qup11_f2,
	msm_mux_qup12,
	msm_mux_qup13_f1,
	msm_mux_qup13_f2,
	msm_mux_qup14,
	msm_mux_sd_write,
	msm_mux_sdc1_tb,
	msm_mux_sdc2_tb,
	msm_mux_sp_cmu,
	msm_mux_tgu_ch0,
	msm_mux_tgu_ch1,
	msm_mux_tgu_ch2,
	msm_mux_tgu_ch3,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_uim1_clk,
	msm_mux_uim1_data,
	msm_mux_uim1_present,
	msm_mux_uim1_reset,
	msm_mux_uim2_clk,
	msm_mux_uim2_data,
	msm_mux_uim2_present,
	msm_mux_uim2_reset,
	msm_mux_usb2phy_ac,
	msm_mux_usb_phy,
	msm_mux_vfr_1,
	msm_mux_vsense_trigger,
	msm_mux_wlan1_adc0,
	msm_mux_wlan1_adc1,
	msm_mux_wlan2_adc0,
	msm_mux_wlan2_adc1,
	msm_mux__,
};

static const char * const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio11", "gpio12", "gpio13", "gpio14", "gpio15",
	"gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21", "gpio22",
	"gpio23", "gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio29",
	"gpio30", "gpio31", "gpio32", "gpio33", "gpio34", "gpio35", "gpio36",
	"gpio37", "gpio38", "gpio39", "gpio40", "gpio41", "gpio42", "gpio43",
	"gpio44", "gpio45", "gpio46", "gpio47", "gpio48", "gpio49", "gpio50",
	"gpio51", "gpio52", "gpio53", "gpio56", "gpio57", "gpio58", "gpio59",
	"gpio60", "gpio61", "gpio62", "gpio63", "gpio64", "gpio65", "gpio66",
	"gpio67", "gpio68", "gpio69", "gpio75", "gpio76", "gpio77", "gpio78",
	"gpio79", "gpio80", "gpio81", "gpio82", "gpio83", "gpio84", "gpio85",
	"gpio86", "gpio87", "gpio88", "gpio89", "gpio90", "gpio91", "gpio92",
	"gpio93", "gpio94", "gpio95", "gpio96", "gpio97", "gpio98", "gpio99",
	"gpio100", "gpio101", "gpio102", "gpio103", "gpio104", "gpio105",
	"gpio106", "gpio107", "gpio108", "gpio109", "gpio110", "gpio111",
	"gpio112", "gpio113", "gpio114", "gpio115", "gpio116", "gpio117",
	"gpio118", "gpio119", "gpio120", "gpio124", "gpio125", "gpio126",
	"gpio127", "gpio128", "gpio129", "gpio130", "gpio131", "gpio132",
	"gpio133", "gpio134", "gpio135", "gpio136", "gpio141", "gpio142",
	"gpio143", "gpio150", "gpio151", "gpio152", "gpio153", "gpio154",
	"gpio155",
};
static const char * const agera_pll_groups[] = {
	"gpio89",
};
static const char * const cci_async_groups[] = {
	"gpio35", "gpio36", "gpio48", "gpio52", "gpio53",
};
static const char * const cci_i2c_groups[] = {
	"gpio2", "gpio3", "gpio39", "gpio40", "gpio41", "gpio42", "gpio43",
	"gpio44",
};
static const char * const gps_tx_groups[] = {
	"gpio101", "gpio102", "gpio107", "gpio108",
};
static const char * const gp_pdm0_groups[] = {
	"gpio37", "gpio68",
};
static const char * const gp_pdm1_groups[] = {
	"gpio8", "gpio52",
};
static const char * const gp_pdm2_groups[] = {
	"gpio57",
};
static const char * const jitter_bist_groups[] = {
	"gpio90",
};
static const char * const mclk_groups[] = {
	"gpio93",
};
static const char * const mdp_vsync_groups[] = {
	"gpio6", "gpio23", "gpio24", "gpio27", "gpio28",
};
static const char * const mss_lte_groups[] = {
	"gpio65", "gpio66",
};
static const char * const nav_pps_groups[] = {
	"gpio101", "gpio101", "gpio102", "gpio102",
};
static const char * const pll_bist_groups[] = {
	"gpio27",
};
static const char * const qlink0_wmss_groups[] = {
	"gpio103",
};
static const char * const qlink1_wmss_groups[] = {
	"gpio106",
};
static const char * const usb_phy_groups[] = {
	"gpio124",
};
static const char * const adsp_ext_groups[] = {
	"gpio87",
};
static const char * const atest_char_groups[] = {
	"gpio95",
};
static const char * const atest_char0_groups[] = {
	"gpio96",
};
static const char * const atest_char1_groups[] = {
	"gpio97",
};
static const char * const atest_char2_groups[] = {
	"gpio98",
};
static const char * const atest_char3_groups[] = {
	"gpio99",
};
static const char * const atest_tsens_groups[] = {
	"gpio92",
};
static const char * const atest_tsens2_groups[] = {
	"gpio93",
};
static const char * const atest_usb1_groups[] = {
	"gpio83",
};
static const char * const atest_usb10_groups[] = {
	"gpio84",
};
static const char * const atest_usb11_groups[] = {
	"gpio85",
};
static const char * const atest_usb12_groups[] = {
	"gpio86",
};
static const char * const atest_usb13_groups[] = {
	"gpio87",
};
static const char * const atest_usb2_groups[] = {
	"gpio88",
};
static const char * const atest_usb20_groups[] = {
	"gpio89",
};
static const char * const atest_usb21_groups[] = {
	"gpio90",
};
static const char * const atest_usb22_groups[] = {
	"gpio91",
};
static const char * const atest_usb23_groups[] = {
	"gpio92",
};
static const char * const audio_ref_groups[] = {
	"gpio60",
};
static const char * const btfm_slimbus_groups[] = {
	"gpio67", "gpio68", "gpio86", "gpio87",
};
static const char * const cam_mclk_groups[] = {
	"gpio29", "gpio30", "gpio31", "gpio32", "gpio33",
};
static const char * const cci_timer0_groups[] = {
	"gpio34",
};
static const char * const cci_timer1_groups[] = {
	"gpio35",
};
static const char * const cci_timer2_groups[] = {
	"gpio36",
};
static const char * const cci_timer3_groups[] = {
	"gpio37",
};
static const char * const cci_timer4_groups[] = {
	"gpio38",
};
static const char * const cri_trng_groups[] = {
	"gpio0", "gpio1", "gpio2",
};
static const char * const dbg_out_groups[] = {
	"gpio3",
};
static const char * const ddr_bist_groups[] = {
	"gpio19", "gpio20", "gpio21", "gpio22",
};
static const char * const ddr_pxi0_groups[] = {
	"gpio86", "gpio90",
};
static const char * const ddr_pxi1_groups[] = {
	"gpio87", "gpio91",
};
static const char * const ddr_pxi2_groups[] = {
	"gpio88", "gpio92",
};
static const char * const ddr_pxi3_groups[] = {
	"gpio89", "gpio93",
};
static const char * const dp_hot_groups[] = {
	"gpio12", "gpio118",
};
static const char * const edp_lcd_groups[] = {
	"gpio23",
};
static const char * const gcc_gp1_groups[] = {
	"gpio48", "gpio58",
};
static const char * const gcc_gp2_groups[] = {
	"gpio21",
};
static const char * const gcc_gp3_groups[] = {
	"gpio22",
};
static const char * const ibi_i3c_groups[] = {
	"gpio0", "gpio1",
};
static const char * const ldo_en_groups[] = {
	"gpio95",
};
static const char * const ldo_update_groups[] = {
	"gpio96",
};
static const char * const lpass_ext_groups[] = {
	"gpio60", "gpio93",
};
static const char * const m_voc_groups[] = {
	"gpio12",
};
static const char * const mdp_vsync0_groups[] = {
	"gpio47",
};
static const char * const mdp_vsync1_groups[] = {
	"gpio48",
};
static const char * const mdp_vsync2_groups[] = {
	"gpio56",
};
static const char * const mdp_vsync3_groups[] = {
	"gpio57",
};
static const char * const mi2s_0_groups[] = {
	"gpio88", "gpio89", "gpio90", "gpio91",
};
static const char * const mi2s_1_groups[] = {
	"gpio67", "gpio68", "gpio86", "gpio87",
};
static const char * const mi2s_2_groups[] = {
	"gpio60",
};
static const char * const nav_gpio_groups[] = {
	"gpio101", "gpio102",
};
static const char * const pa_indicator_groups[] = {
	"gpio118",
};
static const char * const phase_flag0_groups[] = {
	"gpio12",
};
static const char * const phase_flag1_groups[] = {
	"gpio17",
};
static const char * const phase_flag10_groups[] = {
	"gpio41",
};
static const char * const phase_flag11_groups[] = {
	"gpio42",
};
static const char * const phase_flag12_groups[] = {
	"gpio43",
};
static const char * const phase_flag13_groups[] = {
	"gpio44",
};
static const char * const phase_flag14_groups[] = {
	"gpio45",
};
static const char * const phase_flag15_groups[] = {
	"gpio46",
};
static const char * const phase_flag16_groups[] = {
	"gpio47",
};
static const char * const phase_flag17_groups[] = {
	"gpio48",
};
static const char * const phase_flag18_groups[] = {
	"gpio49",
};
static const char * const phase_flag19_groups[] = {
	"gpio50",
};
static const char * const phase_flag2_groups[] = {
	"gpio18",
};
static const char * const phase_flag20_groups[] = {
	"gpio51",
};
static const char * const phase_flag21_groups[] = {
	"gpio52",
};
static const char * const phase_flag22_groups[] = {
	"gpio53",
};
static const char * const phase_flag23_groups[] = {
	"gpio56",
};
static const char * const phase_flag24_groups[] = {
	"gpio57",
};
static const char * const phase_flag25_groups[] = {
	"gpio60",
};
static const char * const phase_flag26_groups[] = {
	"gpio61",
};
static const char * const phase_flag27_groups[] = {
	"gpio62",
};
static const char * const phase_flag28_groups[] = {
	"gpio63",
};
static const char * const phase_flag29_groups[] = {
	"gpio64",
};
static const char * const phase_flag3_groups[] = {
	"gpio34",
};
static const char * const phase_flag30_groups[] = {
	"gpio67",
};
static const char * const phase_flag31_groups[] = {
	"gpio68",
};
static const char * const phase_flag4_groups[] = {
	"gpio35",
};
static const char * const phase_flag5_groups[] = {
	"gpio36",
};
static const char * const phase_flag6_groups[] = {
	"gpio37",
};
static const char * const phase_flag7_groups[] = {
	"gpio38",
};
static const char * const phase_flag8_groups[] = {
	"gpio39",
};
static const char * const phase_flag9_groups[] = {
	"gpio40",
};
static const char * const pll_bypassnl_groups[] = {
	"gpio13",
};
static const char * const pll_clk_groups[] = {
	"gpio98",
};
static const char * const pll_reset_groups[] = {
	"gpio14",
};
static const char * const prng_rosc0_groups[] = {
	"gpio97",
};
static const char * const prng_rosc1_groups[] = {
	"gpio98",
};
static const char * const prng_rosc2_groups[] = {
	"gpio99",
};
static const char * const prng_rosc3_groups[] = {
	"gpio100",
};
static const char * const qdss_cti_groups[] = {
	"gpio2", "gpio3", "gpio6", "gpio7", "gpio61", "gpio62", "gpio86",
	"gpio87",
};
static const char * const qdss_gpio_groups[] = {
	"gpio8", "gpio9", "gpio63", "gpio64",
};
static const char * const qdss_gpio0_groups[] = {
	"gpio39", "gpio65",
};
static const char * const qdss_gpio1_groups[] = {
	"gpio40", "gpio66",
};
static const char * const qdss_gpio10_groups[] = {
	"gpio50", "gpio56",
};
static const char * const qdss_gpio11_groups[] = {
	"gpio51", "gpio57",
};
static const char * const qdss_gpio12_groups[] = {
	"gpio34", "gpio52",
};
static const char * const qdss_gpio13_groups[] = {
	"gpio35", "gpio53",
};
static const char * const qdss_gpio14_groups[] = {
	"gpio27", "gpio36",
};
static const char * const qdss_gpio15_groups[] = {
	"gpio28", "gpio37",
};
static const char * const qdss_gpio2_groups[] = {
	"gpio38", "gpio41",
};
static const char * const qdss_gpio3_groups[] = {
	"gpio42", "gpio47",
};
static const char * const qdss_gpio4_groups[] = {
	"gpio43", "gpio88",
};
static const char * const qdss_gpio5_groups[] = {
	"gpio44", "gpio89",
};
static const char * const qdss_gpio6_groups[] = {
	"gpio45", "gpio90",
};
static const char * const qdss_gpio7_groups[] = {
	"gpio46", "gpio91",
};
static const char * const qdss_gpio8_groups[] = {
	"gpio48", "gpio92",
};
static const char * const qdss_gpio9_groups[] = {
	"gpio49", "gpio93",
};
static const char * const qlink0_enable_groups[] = {
	"gpio105",
};
static const char * const qlink0_request_groups[] = {
	"gpio104",
};
static const char * const qlink1_enable_groups[] = {
	"gpio108",
};
static const char * const qlink1_request_groups[] = {
	"gpio107",
};
static const char * const qup00_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const qup01_groups[] = {
	"gpio61", "gpio62", "gpio63", "gpio64",
};
static const char * const qup02_groups[] = {
	"gpio45", "gpio46", "gpio48", "gpio56", "gpio57",
};
static const char * const qup10_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16", "gpio17",
};
static const char * const qup11_f1_groups[] = {
	"gpio27", "gpio28",
};
static const char * const qup11_f2_groups[] = {
	"gpio27", "gpio28",
};

static const char * const qup12_groups[] = {
	"gpio19", "gpio19", "gpio20", "gpio20",
};
static const char * const qup13_f1_groups[] = {
	"gpio25", "gpio26",
};
static const char * const qup13_f2_groups[] = {
	"gpio25", "gpio26",
};
static const char * const qup14_groups[] = {
	"gpio4", "gpio4", "gpio5", "gpio5",
};
static const char * const sd_write_groups[] = {
	"gpio85",
};
static const char * const sdc1_tb_groups[] = {
	"gpio4",
};
static const char * const sdc2_tb_groups[] = {
	"gpio5",
};
static const char * const sp_cmu_groups[] = {
	"gpio3",
};
static const char * const tgu_ch0_groups[] = {
	"gpio61",
};
static const char * const tgu_ch1_groups[] = {
	"gpio62",
};
static const char * const tgu_ch2_groups[] = {
	"gpio63",
};
static const char * const tgu_ch3_groups[] = {
	"gpio64",
};
static const char * const tsense_pwm1_groups[] = {
	"gpio88",
};
static const char * const tsense_pwm2_groups[] = {
	"gpio88",
};
static const char * const uim1_clk_groups[] = {
	"gpio80",
};
static const char * const uim1_data_groups[] = {
	"gpio79",
};
static const char * const uim1_present_groups[] = {
	"gpio82",
};
static const char * const uim1_reset_groups[] = {
	"gpio81",
};
static const char * const uim2_clk_groups[] = {
	"gpio76",
};
static const char * const uim2_data_groups[] = {
	"gpio75",
};
static const char * const uim2_present_groups[] = {
	"gpio78",
};
static const char * const uim2_reset_groups[] = {
	"gpio77",
};
static const char * const usb2phy_ac_groups[] = {
	"gpio47",
};
static const char * const vfr_1_groups[] = {
	"gpio49",
};
static const char * const vsense_trigger_groups[] = {
	"gpio89",
};
static const char * const wlan1_adc0_groups[] = {
	"gpio90",
};
static const char * const wlan1_adc1_groups[] = {
	"gpio92",
};
static const char * const wlan2_adc0_groups[] = {
	"gpio91",
};
static const char * const wlan2_adc1_groups[] = {
	"gpio93",
};

static const struct pinfunction sm6375_functions[] = {
	MSM_PIN_FUNCTION(adsp_ext),
	MSM_PIN_FUNCTION(agera_pll),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_char0),
	MSM_PIN_FUNCTION(atest_char1),
	MSM_PIN_FUNCTION(atest_char2),
	MSM_PIN_FUNCTION(atest_char3),
	MSM_PIN_FUNCTION(atest_tsens),
	MSM_PIN_FUNCTION(atest_tsens2),
	MSM_PIN_FUNCTION(atest_usb1),
	MSM_PIN_FUNCTION(atest_usb10),
	MSM_PIN_FUNCTION(atest_usb11),
	MSM_PIN_FUNCTION(atest_usb12),
	MSM_PIN_FUNCTION(atest_usb13),
	MSM_PIN_FUNCTION(atest_usb2),
	MSM_PIN_FUNCTION(atest_usb20),
	MSM_PIN_FUNCTION(atest_usb21),
	MSM_PIN_FUNCTION(atest_usb22),
	MSM_PIN_FUNCTION(atest_usb23),
	MSM_PIN_FUNCTION(audio_ref),
	MSM_PIN_FUNCTION(btfm_slimbus),
	MSM_PIN_FUNCTION(cam_mclk),
	MSM_PIN_FUNCTION(cci_async),
	MSM_PIN_FUNCTION(cci_i2c),
	MSM_PIN_FUNCTION(cci_timer0),
	MSM_PIN_FUNCTION(cci_timer1),
	MSM_PIN_FUNCTION(cci_timer2),
	MSM_PIN_FUNCTION(cci_timer3),
	MSM_PIN_FUNCTION(cci_timer4),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(ddr_bist),
	MSM_PIN_FUNCTION(ddr_pxi0),
	MSM_PIN_FUNCTION(ddr_pxi1),
	MSM_PIN_FUNCTION(ddr_pxi2),
	MSM_PIN_FUNCTION(ddr_pxi3),
	MSM_PIN_FUNCTION(dp_hot),
	MSM_PIN_FUNCTION(edp_lcd),
	MSM_PIN_FUNCTION(gcc_gp1),
	MSM_PIN_FUNCTION(gcc_gp2),
	MSM_PIN_FUNCTION(gcc_gp3),
	MSM_PIN_FUNCTION(gp_pdm0),
	MSM_PIN_FUNCTION(gp_pdm1),
	MSM_PIN_FUNCTION(gp_pdm2),
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(gps_tx),
	MSM_PIN_FUNCTION(ibi_i3c),
	MSM_PIN_FUNCTION(jitter_bist),
	MSM_PIN_FUNCTION(ldo_en),
	MSM_PIN_FUNCTION(ldo_update),
	MSM_PIN_FUNCTION(lpass_ext),
	MSM_PIN_FUNCTION(m_voc),
	MSM_PIN_FUNCTION(mclk),
	MSM_PIN_FUNCTION(mdp_vsync),
	MSM_PIN_FUNCTION(mdp_vsync0),
	MSM_PIN_FUNCTION(mdp_vsync1),
	MSM_PIN_FUNCTION(mdp_vsync2),
	MSM_PIN_FUNCTION(mdp_vsync3),
	MSM_PIN_FUNCTION(mi2s_0),
	MSM_PIN_FUNCTION(mi2s_1),
	MSM_PIN_FUNCTION(mi2s_2),
	MSM_PIN_FUNCTION(mss_lte),
	MSM_PIN_FUNCTION(nav_gpio),
	MSM_PIN_FUNCTION(nav_pps),
	MSM_PIN_FUNCTION(pa_indicator),
	MSM_PIN_FUNCTION(phase_flag0),
	MSM_PIN_FUNCTION(phase_flag1),
	MSM_PIN_FUNCTION(phase_flag10),
	MSM_PIN_FUNCTION(phase_flag11),
	MSM_PIN_FUNCTION(phase_flag12),
	MSM_PIN_FUNCTION(phase_flag13),
	MSM_PIN_FUNCTION(phase_flag14),
	MSM_PIN_FUNCTION(phase_flag15),
	MSM_PIN_FUNCTION(phase_flag16),
	MSM_PIN_FUNCTION(phase_flag17),
	MSM_PIN_FUNCTION(phase_flag18),
	MSM_PIN_FUNCTION(phase_flag19),
	MSM_PIN_FUNCTION(phase_flag2),
	MSM_PIN_FUNCTION(phase_flag20),
	MSM_PIN_FUNCTION(phase_flag21),
	MSM_PIN_FUNCTION(phase_flag22),
	MSM_PIN_FUNCTION(phase_flag23),
	MSM_PIN_FUNCTION(phase_flag24),
	MSM_PIN_FUNCTION(phase_flag25),
	MSM_PIN_FUNCTION(phase_flag26),
	MSM_PIN_FUNCTION(phase_flag27),
	MSM_PIN_FUNCTION(phase_flag28),
	MSM_PIN_FUNCTION(phase_flag29),
	MSM_PIN_FUNCTION(phase_flag3),
	MSM_PIN_FUNCTION(phase_flag30),
	MSM_PIN_FUNCTION(phase_flag31),
	MSM_PIN_FUNCTION(phase_flag4),
	MSM_PIN_FUNCTION(phase_flag5),
	MSM_PIN_FUNCTION(phase_flag6),
	MSM_PIN_FUNCTION(phase_flag7),
	MSM_PIN_FUNCTION(phase_flag8),
	MSM_PIN_FUNCTION(phase_flag9),
	MSM_PIN_FUNCTION(pll_bist),
	MSM_PIN_FUNCTION(pll_bypassnl),
	MSM_PIN_FUNCTION(pll_clk),
	MSM_PIN_FUNCTION(pll_reset),
	MSM_PIN_FUNCTION(prng_rosc0),
	MSM_PIN_FUNCTION(prng_rosc1),
	MSM_PIN_FUNCTION(prng_rosc2),
	MSM_PIN_FUNCTION(prng_rosc3),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qdss_gpio),
	MSM_PIN_FUNCTION(qdss_gpio0),
	MSM_PIN_FUNCTION(qdss_gpio1),
	MSM_PIN_FUNCTION(qdss_gpio10),
	MSM_PIN_FUNCTION(qdss_gpio11),
	MSM_PIN_FUNCTION(qdss_gpio12),
	MSM_PIN_FUNCTION(qdss_gpio13),
	MSM_PIN_FUNCTION(qdss_gpio14),
	MSM_PIN_FUNCTION(qdss_gpio15),
	MSM_PIN_FUNCTION(qdss_gpio2),
	MSM_PIN_FUNCTION(qdss_gpio3),
	MSM_PIN_FUNCTION(qdss_gpio4),
	MSM_PIN_FUNCTION(qdss_gpio5),
	MSM_PIN_FUNCTION(qdss_gpio6),
	MSM_PIN_FUNCTION(qdss_gpio7),
	MSM_PIN_FUNCTION(qdss_gpio8),
	MSM_PIN_FUNCTION(qdss_gpio9),
	MSM_PIN_FUNCTION(qlink0_enable),
	MSM_PIN_FUNCTION(qlink0_request),
	MSM_PIN_FUNCTION(qlink0_wmss),
	MSM_PIN_FUNCTION(qlink1_enable),
	MSM_PIN_FUNCTION(qlink1_request),
	MSM_PIN_FUNCTION(qlink1_wmss),
	MSM_PIN_FUNCTION(qup00),
	MSM_PIN_FUNCTION(qup01),
	MSM_PIN_FUNCTION(qup02),
	MSM_PIN_FUNCTION(qup10),
	MSM_PIN_FUNCTION(qup11_f1),
	MSM_PIN_FUNCTION(qup11_f2),
	MSM_PIN_FUNCTION(qup12),
	MSM_PIN_FUNCTION(qup13_f1),
	MSM_PIN_FUNCTION(qup13_f2),
	MSM_PIN_FUNCTION(qup14),
	MSM_PIN_FUNCTION(sd_write),
	MSM_PIN_FUNCTION(sdc1_tb),
	MSM_PIN_FUNCTION(sdc2_tb),
	MSM_PIN_FUNCTION(sp_cmu),
	MSM_PIN_FUNCTION(tgu_ch0),
	MSM_PIN_FUNCTION(tgu_ch1),
	MSM_PIN_FUNCTION(tgu_ch2),
	MSM_PIN_FUNCTION(tgu_ch3),
	MSM_PIN_FUNCTION(tsense_pwm1),
	MSM_PIN_FUNCTION(tsense_pwm2),
	MSM_PIN_FUNCTION(uim1_clk),
	MSM_PIN_FUNCTION(uim1_data),
	MSM_PIN_FUNCTION(uim1_present),
	MSM_PIN_FUNCTION(uim1_reset),
	MSM_PIN_FUNCTION(uim2_clk),
	MSM_PIN_FUNCTION(uim2_data),
	MSM_PIN_FUNCTION(uim2_present),
	MSM_PIN_FUNCTION(uim2_reset),
	MSM_PIN_FUNCTION(usb2phy_ac),
	MSM_PIN_FUNCTION(usb_phy),
	MSM_PIN_FUNCTION(vfr_1),
	MSM_PIN_FUNCTION(vsense_trigger),
	MSM_PIN_FUNCTION(wlan1_adc0),
	MSM_PIN_FUNCTION(wlan1_adc1),
	MSM_PIN_FUNCTION(wlan2_adc0),
	MSM_PIN_FUNCTION(wlan2_adc1),
};

/*
 * Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup sm6375_groups[] = {
	[0] = PINGROUP(0, ibi_i3c, qup00, cri_trng, _, _, _, _, _, _),
	[1] = PINGROUP(1, ibi_i3c, qup00, cri_trng, _, _, _, _, _, _),
	[2] = PINGROUP(2, qup00, cci_i2c, cri_trng, qdss_cti, _, _, _, _, _),
	[3] = PINGROUP(3, qup00, cci_i2c, sp_cmu, dbg_out, qdss_cti, _, _, _, _),
	[4] = PINGROUP(4, qup14, qup14, sdc1_tb, _, _, _, _, _, _),
	[5] = PINGROUP(5, qup14, qup14, sdc2_tb, _, _, _, _, _, _),
	[6] = PINGROUP(6, mdp_vsync, qdss_cti, _, _, _, _, _, _, _),
	[7] = PINGROUP(7, qdss_cti, _, _, _, _, _, _, _, _),
	[8] = PINGROUP(8, gp_pdm1, qdss_gpio, _, _, _, _, _, _, _),
	[9] = PINGROUP(9, qdss_gpio, _, _, _, _, _, _, _, _),
	[10] = PINGROUP(10, _, _, _, _, _, _, _, _, _),
	[11] = PINGROUP(11, _, _, _, _, _, _, _, _, _),
	[12] = PINGROUP(12, m_voc, dp_hot, _, phase_flag0, _, _, _, _, _),
	[13] = PINGROUP(13, qup10, pll_bypassnl, _, _, _, _, _, _, _),
	[14] = PINGROUP(14, qup10, pll_reset, _, _, _, _, _, _, _),
	[15] = PINGROUP(15, qup10, _, _, _, _, _, _, _, _),
	[16] = PINGROUP(16, qup10, _, _, _, _, _, _, _, _),
	[17] = PINGROUP(17, _, phase_flag1, qup10, _, _, _, _, _, _),
	[18] = PINGROUP(18, _, phase_flag2, _, _, _, _, _, _, _),
	[19] = PINGROUP(19, qup12, qup12, ddr_bist, _, _, _, _, _, _),
	[20] = PINGROUP(20, qup12, qup12, ddr_bist, _, _, _, _, _, _),
	[21] = PINGROUP(21, gcc_gp2, ddr_bist, _, _, _, _, _, _, _),
	[22] = PINGROUP(22, gcc_gp3, ddr_bist, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, mdp_vsync, edp_lcd, _, _, _, _, _, _, _),
	[24] = PINGROUP(24, mdp_vsync, _, _, _, _, _, _, _, _),
	[25] = PINGROUP(25, qup13_f1, qup13_f2, _, _, _, _, _, _, _),
	[26] = PINGROUP(26, qup13_f1, qup13_f2, _, _, _, _, _, _, _),
	[27] = PINGROUP(27, qup11_f1, qup11_f2, mdp_vsync, pll_bist, _, qdss_gpio14, _, _, _),
	[28] = PINGROUP(28, qup11_f1, qup11_f2, mdp_vsync, _, qdss_gpio15, _, _, _, _),
	[29] = PINGROUP(29, cam_mclk, _, _, _, _, _, _, _, _),
	[30] = PINGROUP(30, cam_mclk, _, _, _, _, _, _, _, _),
	[31] = PINGROUP(31, cam_mclk, _, _, _, _, _, _, _, _),
	[32] = PINGROUP(32, cam_mclk, _, _, _, _, _, _, _, _),
	[33] = PINGROUP(33, cam_mclk, _, _, _, _, _, _, _, _),
	[34] = PINGROUP(34, cci_timer0, _, phase_flag3, qdss_gpio12, _, _, _, _, _),
	[35] = PINGROUP(35, cci_timer1, cci_async, _, phase_flag4, qdss_gpio13, _, _, _, _),
	[36] = PINGROUP(36, cci_timer2, cci_async, _, phase_flag5, qdss_gpio14, _, _, _, _),
	[37] = PINGROUP(37, cci_timer3, gp_pdm0, _, phase_flag6, qdss_gpio15, _, _, _, _),
	[38] = PINGROUP(38, cci_timer4, _, phase_flag7, qdss_gpio2, _, _, _, _, _),
	[39] = PINGROUP(39, cci_i2c, _, phase_flag8, qdss_gpio0, _, _, _, _, _),
	[40] = PINGROUP(40, cci_i2c, _, phase_flag9, qdss_gpio1, _, _, _, _, _),
	[41] = PINGROUP(41, cci_i2c, _, phase_flag10, qdss_gpio2, _, _, _, _, _),
	[42] = PINGROUP(42, cci_i2c, _, phase_flag11, qdss_gpio3, _, _, _, _, _),
	[43] = PINGROUP(43, cci_i2c, _, phase_flag12, qdss_gpio4, _, _, _, _, _),
	[44] = PINGROUP(44, cci_i2c, _, phase_flag13, qdss_gpio5, _, _, _, _, _),
	[45] = PINGROUP(45, qup02, _, phase_flag14, qdss_gpio6, _, _, _, _, _),
	[46] = PINGROUP(46, qup02, _, phase_flag15, qdss_gpio7, _, _, _, _, _),
	[47] = PINGROUP(47, mdp_vsync0, _, phase_flag16, qdss_gpio3, _, _, usb2phy_ac, _, _),
	[48] = PINGROUP(48, cci_async, mdp_vsync1, gcc_gp1, _, phase_flag17, qdss_gpio8, qup02,
			_, _),
	[49] = PINGROUP(49, vfr_1, _, phase_flag18, qdss_gpio9, _, _, _, _, _),
	[50] = PINGROUP(50, _, phase_flag19, qdss_gpio10, _, _, _, _, _, _),
	[51] = PINGROUP(51, _, phase_flag20, qdss_gpio11, _, _, _, _, _, _),
	[52] = PINGROUP(52, cci_async, gp_pdm1, _, phase_flag21, qdss_gpio12, _, _, _, _),
	[53] = PINGROUP(53, cci_async, _, phase_flag22, qdss_gpio13, _, _, _, _, _),
	[54] = PINGROUP(54, _, _, _, _, _, _, _, _, _),
	[55] = PINGROUP(55, _, _, _, _, _, _, _, _, _),
	[56] = PINGROUP(56, qup02, mdp_vsync2, _, phase_flag23, qdss_gpio10, _, _, _, _),
	[57] = PINGROUP(57, qup02, mdp_vsync3, gp_pdm2, _, phase_flag24, qdss_gpio11, _, _, _),
	[58] = PINGROUP(58, gcc_gp1, _, _, _, _, _, _, _, _),
	[59] = PINGROUP(59, _, _, _, _, _, _, _, _, _),
	[60] = PINGROUP(60, audio_ref, lpass_ext, mi2s_2, _, phase_flag25, _, _, _, _),
	[61] = PINGROUP(61, qup01, tgu_ch0, _, phase_flag26, qdss_cti, _, _, _, _),
	[62] = PINGROUP(62, qup01, tgu_ch1, _, phase_flag27, qdss_cti, _, _, _, _),
	[63] = PINGROUP(63, qup01, tgu_ch2, _, phase_flag28, qdss_gpio, _, _, _, _),
	[64] = PINGROUP(64, qup01, tgu_ch3, _, phase_flag29, qdss_gpio, _, _, _, _),
	[65] = PINGROUP(65, mss_lte, _, qdss_gpio0, _, _, _, _, _, _),
	[66] = PINGROUP(66, mss_lte, _, qdss_gpio1, _, _, _, _, _, _),
	[67] = PINGROUP(67, btfm_slimbus, mi2s_1, _, phase_flag30, _, _, _, _, _),
	[68] = PINGROUP(68, btfm_slimbus, mi2s_1, gp_pdm0, _, phase_flag31, _, _, _, _),
	[69] = PINGROUP(69, _, _, _, _, _, _, _, _, _),
	[70] = PINGROUP(70, _, _, _, _, _, _, _, _, _),
	[71] = PINGROUP(71, _, _, _, _, _, _, _, _, _),
	[72] = PINGROUP(72, _, _, _, _, _, _, _, _, _),
	[73] = PINGROUP(73, _, _, _, _, _, _, _, _, _),
	[74] = PINGROUP(74, _, _, _, _, _, _, _, _, _),
	[75] = PINGROUP(75, uim2_data, _, _, _, _, _, _, _, _),
	[76] = PINGROUP(76, uim2_clk, _, _, _, _, _, _, _, _),
	[77] = PINGROUP(77, uim2_reset, _, _, _, _, _, _, _, _),
	[78] = PINGROUP(78, uim2_present, _, _, _, _, _, _, _, _),
	[79] = PINGROUP(79, uim1_data, _, _, _, _, _, _, _, _),
	[80] = PINGROUP(80, uim1_clk, _, _, _, _, _, _, _, _),
	[81] = PINGROUP(81, uim1_reset, _, _, _, _, _, _, _, _),
	[82] = PINGROUP(82, uim1_present, _, _, _, _, _, _, _, _),
	[83] = PINGROUP(83, atest_usb1, _, _, _, _, _, _, _, _),
	[84] = PINGROUP(84, _, atest_usb10, _, _, _, _, _, _, _),
	[85] = PINGROUP(85, sd_write, _, atest_usb11, _, _, _, _, _, _),
	[86] = PINGROUP(86, btfm_slimbus, mi2s_1, _, qdss_cti, atest_usb12, ddr_pxi0, _, _, _),
	[87] = PINGROUP(87, btfm_slimbus, mi2s_1, adsp_ext, _, qdss_cti, atest_usb13, ddr_pxi1, _,
			_),
	[88] = PINGROUP(88, mi2s_0, _, qdss_gpio4, _, atest_usb2, ddr_pxi2, tsense_pwm1,
			tsense_pwm2, _),
	[89] = PINGROUP(89, mi2s_0, agera_pll, _, qdss_gpio5, _, vsense_trigger, atest_usb20,
			ddr_pxi3, _),
	[90] = PINGROUP(90, mi2s_0, jitter_bist, _, qdss_gpio6, _, wlan1_adc0, atest_usb21,
			ddr_pxi0, _),
	[91] = PINGROUP(91, mi2s_0, _, qdss_gpio7, _, wlan2_adc0, atest_usb22, ddr_pxi1, _, _),
	[92] = PINGROUP(92, _, qdss_gpio8, atest_tsens, wlan1_adc1, atest_usb23, ddr_pxi2, _, _,
			_),
	[93] = PINGROUP(93, mclk, lpass_ext, _, qdss_gpio9, atest_tsens2, wlan2_adc1, ddr_pxi3,
			_, _),
	[94] = PINGROUP(94, _, _, _, _, _, _, _, _, _),
	[95] = PINGROUP(95, ldo_en, _, atest_char, _, _, _, _, _, _),
	[96] = PINGROUP(96, ldo_update, _, atest_char0, _, _, _, _, _, _),
	[97] = PINGROUP(97, prng_rosc0, _, atest_char1, _, _, _, _, _, _),
	[98] = PINGROUP(98, _, atest_char2, _, _, prng_rosc1, pll_clk, _, _, _),
	[99] = PINGROUP(99, _, atest_char3, _, _, prng_rosc2, _, _, _, _),
	[100] = PINGROUP(100, _, _, prng_rosc3, _, _, _, _, _, _),
	[101] = PINGROUP(101, nav_gpio, nav_pps, nav_pps, gps_tx, _, _, _, _, _),
	[102] = PINGROUP(102, nav_gpio, nav_pps, nav_pps, gps_tx, _, _, _, _, _),
	[103] = PINGROUP(103, qlink0_wmss, _, _, _, _, _, _, _, _),
	[104] = PINGROUP(104, qlink0_request, _, _, _, _, _, _, _, _),
	[105] = PINGROUP(105, qlink0_enable, _, _, _, _, _, _, _, _),
	[106] = PINGROUP(106, qlink1_wmss, _, _, _, _, _, _, _, _),
	[107] = PINGROUP(107, qlink1_request, gps_tx, _, _, _, _, _, _, _),
	[108] = PINGROUP(108, qlink1_enable, gps_tx, _, _, _, _, _, _, _),
	[109] = PINGROUP(109, _, _, _, _, _, _, _, _, _),
	[110] = PINGROUP(110, _, _, _, _, _, _, _, _, _),
	[111] = PINGROUP(111, _, _, _, _, _, _, _, _, _),
	[112] = PINGROUP(112, _, _, _, _, _, _, _, _, _),
	[113] = PINGROUP(113, _, _, _, _, _, _, _, _, _),
	[114] = PINGROUP(114, _, _, _, _, _, _, _, _, _),
	[115] = PINGROUP(115, _, _, _, _, _, _, _, _, _),
	[116] = PINGROUP(116, _, _, _, _, _, _, _, _, _),
	[117] = PINGROUP(117, _, _, _, _, _, _, _, _, _),
	[118] = PINGROUP(118, _, _, pa_indicator, dp_hot, _, _, _, _, _),
	[119] = PINGROUP(119, _, _, _, _, _, _, _, _, _),
	[120] = PINGROUP(120, _, _, _, _, _, _, _, _, _),
	[121] = PINGROUP(121, _, _, _, _, _, _, _, _, _),
	[122] = PINGROUP(122, _, _, _, _, _, _, _, _, _),
	[123] = PINGROUP(123, _, _, _, _, _, _, _, _, _),
	[124] = PINGROUP(124, usb_phy, _, _, _, _, _, _, _, _),
	[125] = PINGROUP(125, _, _, _, _, _, _, _, _, _),
	[126] = PINGROUP(126, _, _, _, _, _, _, _, _, _),
	[127] = PINGROUP(127, _, _, _, _, _, _, _, _, _),
	[128] = PINGROUP(128, _, _, _, _, _, _, _, _, _),
	[129] = PINGROUP(129, _, _, _, _, _, _, _, _, _),
	[130] = PINGROUP(130, _, _, _, _, _, _, _, _, _),
	[131] = PINGROUP(131, _, _, _, _, _, _, _, _, _),
	[132] = PINGROUP(132, _, _, _, _, _, _, _, _, _),
	[133] = PINGROUP(133, _, _, _, _, _, _, _, _, _),
	[134] = PINGROUP(134, _, _, _, _, _, _, _, _, _),
	[135] = PINGROUP(135, _, _, _, _, _, _, _, _, _),
	[136] = PINGROUP(136, _, _, _, _, _, _, _, _, _),
	[137] = PINGROUP(137, _, _, _, _, _, _, _, _, _),
	[138] = PINGROUP(138, _, _, _, _, _, _, _, _, _),
	[139] = PINGROUP(139, _, _, _, _, _, _, _, _, _),
	[140] = PINGROUP(140, _, _, _, _, _, _, _, _, _),
	[141] = PINGROUP(141, _, _, _, _, _, _, _, _, _),
	[142] = PINGROUP(142, _, _, _, _, _, _, _, _, _),
	[143] = PINGROUP(143, _, _, _, _, _, _, _, _, _),
	[144] = PINGROUP(144, _, _, _, _, _, _, _, _, _),
	[145] = PINGROUP(145, _, _, _, _, _, _, _, _, _),
	[146] = PINGROUP(146, _, _, _, _, _, _, _, _, _),
	[147] = PINGROUP(147, _, _, _, _, _, _, _, _, _),
	[148] = PINGROUP(148, _, _, _, _, _, _, _, _, _),
	[149] = PINGROUP(149, _, _, _, _, _, _, _, _, _),
	[150] = PINGROUP(150, _, _, _, _, _, _, _, _, _),
	[151] = PINGROUP(151, _, _, _, _, _, _, _, _, _),
	[152] = PINGROUP(152, _, _, _, _, _, _, _, _, _),
	[153] = PINGROUP(153, _, _, _, _, _, _, _, _, _),
	[154] = PINGROUP(154, _, _, _, _, _, _, _, _, _),
	[155] = PINGROUP(155, _, _, _, _, _, _, _, _, _),
	[156] = UFS_RESET(ufs_reset, 0x1ae000),
	[157] = SDC_PINGROUP(sdc1_rclk, 0x1a1000, 0, 0),
	[158] = SDC_PINGROUP(sdc1_clk, 0x1a0000, 13, 6),
	[159] = SDC_PINGROUP(sdc1_cmd, 0x1a0000, 11, 3),
	[160] = SDC_PINGROUP(sdc1_data, 0x1a0000, 9, 0),
	[161] = SDC_PINGROUP(sdc2_clk, 0x1a2000, 14, 6),
	[162] = SDC_PINGROUP(sdc2_cmd, 0x1a2000, 11, 3),
	[163] = SDC_PINGROUP(sdc2_data, 0x1a2000, 9, 0),
};

static const struct msm_gpio_wakeirq_map sm6375_mpm_map[] = {
	{ 0, 84 }, { 3, 6 }, { 4, 7 }, { 7, 8 }, { 8, 9 }, { 9, 10 }, { 11, 11 }, { 12, 13 },
	{ 13, 14 }, { 16, 16 }, { 17, 17 }, { 18, 18 }, { 19, 19 }, { 21, 20 }, { 22, 21 },
	{ 23, 23 }, { 24, 24 }, { 25, 25 }, { 27, 26 }, { 28, 27 }, { 37, 28 }, { 38, 29 },
	{ 48, 30 }, { 50, 31 }, { 51, 32 }, { 52, 33 }, { 57, 34 }, { 59, 35 }, { 60, 37 },
	{ 61, 38 }, { 62, 39 }, { 64, 40 }, { 66, 41 }, { 67, 42 }, { 68, 43 }, { 69, 44 },
	{ 78, 45 }, { 82, 36 }, { 83, 47 }, { 84, 48 }, { 85, 49 }, { 87, 50 }, { 88, 51 },
	{ 91, 52 }, { 94, 53 }, { 95, 54 }, { 96, 55 }, { 97, 56 }, { 98, 57 }, { 99, 58 },
	{ 100, 59 }, { 104, 60 }, { 107, 61 }, { 118, 62 }, { 124, 63 }, { 125, 64 }, { 126, 65 },
	{ 128, 66 }, { 129, 67 }, { 131, 69 }, { 133, 70 }, { 134, 71 }, { 136, 73 }, { 142, 74 },
	{ 150, 75 }, { 153, 76 }, { 155, 77 },
};

static const struct msm_pinctrl_soc_data sm6375_tlmm = {
	.pins = sm6375_pins,
	.npins = ARRAY_SIZE(sm6375_pins),
	.functions = sm6375_functions,
	.nfunctions = ARRAY_SIZE(sm6375_functions),
	.groups = sm6375_groups,
	.ngroups = ARRAY_SIZE(sm6375_groups),
	.ngpios = 157,
	.wakeirq_map = sm6375_mpm_map,
	.nwakeirq_map = ARRAY_SIZE(sm6375_mpm_map),
};

static int sm6375_tlmm_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &sm6375_tlmm);
}

static const struct of_device_id sm6375_tlmm_of_match[] = {
	{ .compatible = "qcom,sm6375-tlmm", },
	{ },
};

static struct platform_driver sm6375_tlmm_driver = {
	.driver = {
		.name = "sm6375-tlmm",
		.of_match_table = sm6375_tlmm_of_match,
	},
	.probe = sm6375_tlmm_probe,
	.remove_new = msm_pinctrl_remove,
};

static int __init sm6375_tlmm_init(void)
{
	return platform_driver_register(&sm6375_tlmm_driver);
}
arch_initcall(sm6375_tlmm_init);

static void __exit sm6375_tlmm_exit(void)
{
	platform_driver_unregister(&sm6375_tlmm_driver);
}
module_exit(sm6375_tlmm_exit);

MODULE_DESCRIPTION("QTI SM6375 TLMM driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, sm6375_tlmm_of_match);
