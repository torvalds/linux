// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020-2021, Linaro Limited
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define REG_SIZE 0x1000

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9) \
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

static const struct pinctrl_pin_desc sm8350_pins[] = {
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
	PINCTRL_PIN(156, "GPIO_156"),
	PINCTRL_PIN(157, "GPIO_157"),
	PINCTRL_PIN(158, "GPIO_158"),
	PINCTRL_PIN(159, "GPIO_159"),
	PINCTRL_PIN(160, "GPIO_160"),
	PINCTRL_PIN(161, "GPIO_161"),
	PINCTRL_PIN(162, "GPIO_162"),
	PINCTRL_PIN(163, "GPIO_163"),
	PINCTRL_PIN(164, "GPIO_164"),
	PINCTRL_PIN(165, "GPIO_165"),
	PINCTRL_PIN(166, "GPIO_166"),
	PINCTRL_PIN(167, "GPIO_167"),
	PINCTRL_PIN(168, "GPIO_168"),
	PINCTRL_PIN(169, "GPIO_169"),
	PINCTRL_PIN(170, "GPIO_170"),
	PINCTRL_PIN(171, "GPIO_171"),
	PINCTRL_PIN(172, "GPIO_172"),
	PINCTRL_PIN(173, "GPIO_173"),
	PINCTRL_PIN(174, "GPIO_174"),
	PINCTRL_PIN(175, "GPIO_175"),
	PINCTRL_PIN(176, "GPIO_176"),
	PINCTRL_PIN(177, "GPIO_177"),
	PINCTRL_PIN(178, "GPIO_178"),
	PINCTRL_PIN(179, "GPIO_179"),
	PINCTRL_PIN(180, "GPIO_180"),
	PINCTRL_PIN(181, "GPIO_181"),
	PINCTRL_PIN(182, "GPIO_182"),
	PINCTRL_PIN(183, "GPIO_183"),
	PINCTRL_PIN(184, "GPIO_184"),
	PINCTRL_PIN(185, "GPIO_185"),
	PINCTRL_PIN(186, "GPIO_186"),
	PINCTRL_PIN(187, "GPIO_187"),
	PINCTRL_PIN(188, "GPIO_188"),
	PINCTRL_PIN(189, "GPIO_189"),
	PINCTRL_PIN(190, "GPIO_190"),
	PINCTRL_PIN(191, "GPIO_191"),
	PINCTRL_PIN(192, "GPIO_192"),
	PINCTRL_PIN(193, "GPIO_193"),
	PINCTRL_PIN(194, "GPIO_194"),
	PINCTRL_PIN(195, "GPIO_195"),
	PINCTRL_PIN(196, "GPIO_196"),
	PINCTRL_PIN(197, "GPIO_197"),
	PINCTRL_PIN(198, "GPIO_198"),
	PINCTRL_PIN(199, "GPIO_199"),
	PINCTRL_PIN(200, "GPIO_200"),
	PINCTRL_PIN(201, "GPIO_201"),
	PINCTRL_PIN(202, "GPIO_202"),
	PINCTRL_PIN(203, "UFS_RESET"),
	PINCTRL_PIN(204, "SDC2_CLK"),
	PINCTRL_PIN(205, "SDC2_CMD"),
	PINCTRL_PIN(206, "SDC2_DATA"),
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
DECLARE_MSM_GPIO_PINS(156);
DECLARE_MSM_GPIO_PINS(157);
DECLARE_MSM_GPIO_PINS(158);
DECLARE_MSM_GPIO_PINS(159);
DECLARE_MSM_GPIO_PINS(160);
DECLARE_MSM_GPIO_PINS(161);
DECLARE_MSM_GPIO_PINS(162);
DECLARE_MSM_GPIO_PINS(163);
DECLARE_MSM_GPIO_PINS(164);
DECLARE_MSM_GPIO_PINS(165);
DECLARE_MSM_GPIO_PINS(166);
DECLARE_MSM_GPIO_PINS(167);
DECLARE_MSM_GPIO_PINS(168);
DECLARE_MSM_GPIO_PINS(169);
DECLARE_MSM_GPIO_PINS(170);
DECLARE_MSM_GPIO_PINS(171);
DECLARE_MSM_GPIO_PINS(172);
DECLARE_MSM_GPIO_PINS(173);
DECLARE_MSM_GPIO_PINS(174);
DECLARE_MSM_GPIO_PINS(175);
DECLARE_MSM_GPIO_PINS(176);
DECLARE_MSM_GPIO_PINS(177);
DECLARE_MSM_GPIO_PINS(178);
DECLARE_MSM_GPIO_PINS(179);
DECLARE_MSM_GPIO_PINS(180);
DECLARE_MSM_GPIO_PINS(181);
DECLARE_MSM_GPIO_PINS(182);
DECLARE_MSM_GPIO_PINS(183);
DECLARE_MSM_GPIO_PINS(184);
DECLARE_MSM_GPIO_PINS(185);
DECLARE_MSM_GPIO_PINS(186);
DECLARE_MSM_GPIO_PINS(187);
DECLARE_MSM_GPIO_PINS(188);
DECLARE_MSM_GPIO_PINS(189);
DECLARE_MSM_GPIO_PINS(190);
DECLARE_MSM_GPIO_PINS(191);
DECLARE_MSM_GPIO_PINS(192);
DECLARE_MSM_GPIO_PINS(193);
DECLARE_MSM_GPIO_PINS(194);
DECLARE_MSM_GPIO_PINS(195);
DECLARE_MSM_GPIO_PINS(196);
DECLARE_MSM_GPIO_PINS(197);
DECLARE_MSM_GPIO_PINS(198);
DECLARE_MSM_GPIO_PINS(199);
DECLARE_MSM_GPIO_PINS(200);
DECLARE_MSM_GPIO_PINS(201);
DECLARE_MSM_GPIO_PINS(202);

static const unsigned int ufs_reset_pins[] = { 203 };
static const unsigned int sdc2_clk_pins[] = { 204 };
static const unsigned int sdc2_cmd_pins[] = { 205 };
static const unsigned int sdc2_data_pins[] = { 206 };

enum sm8350_functions {
	msm_mux_atest_char,
	msm_mux_atest_usb,
	msm_mux_audio_ref,
	msm_mux_cam_mclk,
	msm_mux_cci_async,
	msm_mux_cci_i2c,
	msm_mux_cci_timer,
	msm_mux_cmu_rng,
	msm_mux_coex_uart1,
	msm_mux_coex_uart2,
	msm_mux_cri_trng,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_dbg_out,
	msm_mux_ddr_bist,
	msm_mux_ddr_pxi0,
	msm_mux_ddr_pxi1,
	msm_mux_ddr_pxi2,
	msm_mux_ddr_pxi3,
	msm_mux_dp_hot,
	msm_mux_dp_lcd,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_gpio,
	msm_mux_ibi_i3c,
	msm_mux_jitter_bist,
	msm_mux_lpass_slimbus,
	msm_mux_mdp_vsync,
	msm_mux_mdp_vsync0,
	msm_mux_mdp_vsync1,
	msm_mux_mdp_vsync2,
	msm_mux_mdp_vsync3,
	msm_mux_mi2s0_data0,
	msm_mux_mi2s0_data1,
	msm_mux_mi2s0_sck,
	msm_mux_mi2s0_ws,
	msm_mux_mi2s1_data0,
	msm_mux_mi2s1_data1,
	msm_mux_mi2s1_sck,
	msm_mux_mi2s1_ws,
	msm_mux_mi2s2_data0,
	msm_mux_mi2s2_data1,
	msm_mux_mi2s2_sck,
	msm_mux_mi2s2_ws,
	msm_mux_mss_grfc0,
	msm_mux_mss_grfc1,
	msm_mux_mss_grfc10,
	msm_mux_mss_grfc11,
	msm_mux_mss_grfc12,
	msm_mux_mss_grfc2,
	msm_mux_mss_grfc3,
	msm_mux_mss_grfc4,
	msm_mux_mss_grfc5,
	msm_mux_mss_grfc6,
	msm_mux_mss_grfc7,
	msm_mux_mss_grfc8,
	msm_mux_mss_grfc9,
	msm_mux_nav_gpio,
	msm_mux_pa_indicator,
	msm_mux_pcie0_clkreqn,
	msm_mux_pcie1_clkreqn,
	msm_mux_phase_flag,
	msm_mux_pll_bist,
	msm_mux_pll_clk,
	msm_mux_pri_mi2s,
	msm_mux_prng_rosc,
	msm_mux_qdss_cti,
	msm_mux_qdss_gpio,
	msm_mux_qlink0_enable,
	msm_mux_qlink0_request,
	msm_mux_qlink0_wmss,
	msm_mux_qlink1_enable,
	msm_mux_qlink1_request,
	msm_mux_qlink1_wmss,
	msm_mux_qlink2_enable,
	msm_mux_qlink2_request,
	msm_mux_qlink2_wmss,
	msm_mux_qspi0,
	msm_mux_qspi1,
	msm_mux_qspi2,
	msm_mux_qspi3,
	msm_mux_qspi_clk,
	msm_mux_qspi_cs,
	msm_mux_qup0,
	msm_mux_qup1,
	msm_mux_qup10,
	msm_mux_qup11,
	msm_mux_qup12,
	msm_mux_qup13,
	msm_mux_qup14,
	msm_mux_qup15,
	msm_mux_qup16,
	msm_mux_qup17,
	msm_mux_qup18,
	msm_mux_qup19,
	msm_mux_qup2,
	msm_mux_qup3,
	msm_mux_qup4,
	msm_mux_qup5,
	msm_mux_qup6,
	msm_mux_qup7,
	msm_mux_qup8,
	msm_mux_qup9,
	msm_mux_qup_l4,
	msm_mux_qup_l5,
	msm_mux_qup_l6,
	msm_mux_sd_write,
	msm_mux_sdc40,
	msm_mux_sdc41,
	msm_mux_sdc42,
	msm_mux_sdc43,
	msm_mux_sdc4_clk,
	msm_mux_sdc4_cmd,
	msm_mux_sec_mi2s,
	msm_mux_tb_trig,
	msm_mux_tgu_ch0,
	msm_mux_tgu_ch1,
	msm_mux_tgu_ch2,
	msm_mux_tgu_ch3,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_uim0_clk,
	msm_mux_uim0_data,
	msm_mux_uim0_present,
	msm_mux_uim0_reset,
	msm_mux_uim1_clk,
	msm_mux_uim1_data,
	msm_mux_uim1_present,
	msm_mux_uim1_reset,
	msm_mux_usb2phy_ac,
	msm_mux_usb_phy,
	msm_mux_vfr_0,
	msm_mux_vfr_1,
	msm_mux_vsense_trigger,
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
	"gpio147", "gpio148", "gpio149", "gpio150", "gpio151", "gpio152",
	"gpio153", "gpio154", "gpio155", "gpio156", "gpio157", "gpio158",
	"gpio159", "gpio160", "gpio161", "gpio162", "gpio163", "gpio164",
	"gpio165", "gpio166", "gpio167", "gpio168", "gpio169", "gpio170",
	"gpio171", "gpio172", "gpio173", "gpio174", "gpio175", "gpio176",
	"gpio177", "gpio178", "gpio179", "gpio180", "gpio181", "gpio182",
	"gpio183", "gpio184", "gpio185", "gpio186", "gpio187", "gpio188",
	"gpio189", "gpio190", "gpio191", "gpio192", "gpio193", "gpio194",
	"gpio195", "gpio196", "gpio197", "gpio198", "gpio199", "gpio200",
	"gpio201", "gpio202",
};

static const char * const atest_char_groups[] = {
	"gpio85", "gpio86", "gpio87", "gpio115", "gpio117",
};

static const char * const atest_usb_groups[] = {
	"gpio55", "gpio80", "gpio81", "gpio151", "gpio152",
	"gpio153", "gpio154", "gpio158", "gpio159", "gpio161",
};

static const char * const audio_ref_groups[] = {
	"gpio124",
};

static const char * const cam_mclk_groups[] = {
	"gpio100", "gpio101", "gpio102", "gpio103", "gpio104", "gpio105",
};

static const char * const cci_async_groups[] = {
	"gpio106", "gpio118", "gpio119",
};

static const char * const cci_i2c_groups[] = {
	"gpio107", "gpio108", "gpio109", "gpio110", "gpio111", "gpio112",
	"gpio113", "gpio114",
};

static const char * const cci_timer_groups[] = {
	"gpio115", "gpio116", "gpio117", "gpio118", "gpio119",
};

static const char * const cmu_rng_groups[] = {
	"gpio174", "gpio175", "gpio176", "gpio177",
};

static const char * const coex_uart1_groups[] = {
	"gpio151", "gpio152",
};

static const char * const coex_uart2_groups[] = {
	"gpio153", "gpio154",
};

static const char * const cri_trng_groups[] = {
	"gpio186",
};

static const char * const cri_trng0_groups[] = {
	"gpio183",
};

static const char * const cri_trng1_groups[] = {
	"gpio184",
};

static const char * const dbg_out_groups[] = {
	"gpio14",
};

static const char * const ddr_bist_groups[] = {
	"gpio36", "gpio37", "gpio40", "gpio41",
};

static const char * const ddr_pxi0_groups[] = {
	"gpio51", "gpio52",
};

static const char * const ddr_pxi1_groups[] = {
	"gpio48", "gpio49",
};

static const char * const ddr_pxi2_groups[] = {
	"gpio45", "gpio47",
};

static const char * const ddr_pxi3_groups[] = {
	"gpio43", "gpio44",
};

static const char * const dp_hot_groups[] = {
	"gpio87",
};

static const char * const dp_lcd_groups[] = {
	"gpio83",
};

static const char * const gcc_gp1_groups[] = {
	"gpio115", "gpio129",
};

static const char * const gcc_gp2_groups[] = {
	"gpio116", "gpio130",
};

static const char * const gcc_gp3_groups[] = {
	"gpio117", "gpio131",
};

static const char * const ibi_i3c_groups[] = {
	"gpio36", "gpio37", "gpio56", "gpio57", "gpio60", "gpio61",
};

static const char * const jitter_bist_groups[] = {
	"gpio80",
};

static const char * const lpass_slimbus_groups[] = {
	"gpio129", "gpio130",
};

static const char * const mdp_vsync_groups[] = {
	"gpio15", "gpio26", "gpio82", "gpio83", "gpio84",
};

static const char * const mdp_vsync0_groups[] = {
	"gpio86",
};

static const char * const mdp_vsync1_groups[] = {
	"gpio86",
};

static const char * const mdp_vsync2_groups[] = {
	"gpio87",
};

static const char * const mdp_vsync3_groups[] = {
	"gpio87",
};

static const char * const mi2s0_data0_groups[] = {
	"gpio126",
};

static const char * const mi2s0_data1_groups[] = {
	"gpio127",
};

static const char * const mi2s0_sck_groups[] = {
	"gpio125",
};

static const char * const mi2s0_ws_groups[] = {
	"gpio128",
};

static const char * const mi2s1_data0_groups[] = {
	"gpio130",
};

static const char * const mi2s1_data1_groups[] = {
	"gpio131",
};

static const char * const mi2s1_sck_groups[] = {
	"gpio129",
};

static const char * const mi2s1_ws_groups[] = {
	"gpio132",
};

static const char * const mi2s2_data0_groups[] = {
	"gpio121",
};

static const char * const mi2s2_data1_groups[] = {
	"gpio124",
};

static const char * const mi2s2_sck_groups[] = {
	"gpio120",
};

static const char * const mi2s2_ws_groups[] = {
	"gpio122",
};

static const char * const mss_grfc0_groups[] = {
	"gpio141", "gpio158",
};

static const char * const mss_grfc1_groups[] = {
	"gpio142",
};

static const char * const mss_grfc10_groups[] = {
	"gpio153",
};

static const char * const mss_grfc11_groups[] = {
	"gpio154",
};

static const char * const mss_grfc12_groups[] = {
	"gpio157",
};

static const char * const mss_grfc2_groups[] = {
	"gpio143",
};

static const char * const mss_grfc3_groups[] = {
	"gpio144",
};

static const char * const mss_grfc4_groups[] = {
	"gpio145",
};

static const char * const mss_grfc5_groups[] = {
	"gpio146",
};

static const char * const mss_grfc6_groups[] = {
	"gpio147",
};

static const char * const mss_grfc7_groups[] = {
	"gpio148",
};

static const char * const mss_grfc8_groups[] = {
	"gpio149",
};

static const char * const mss_grfc9_groups[] = {
	"gpio150",
};

static const char * const nav_gpio_groups[] = {
	"gpio155", "gpio156", "gpio157",
};

static const char * const pa_indicator_groups[] = {
	"gpio157",
};

static const char * const pcie0_clkreqn_groups[] = {
	"gpio95",
};

static const char * const pcie1_clkreqn_groups[] = {
	"gpio98",
};

static const char * const phase_flag_groups[] = {
	"gpio12", "gpio13", "gpio16", "gpio17", "gpio28", "gpio29", "gpio30",
	"gpio31", "gpio32", "gpio33", "gpio34", "gpio35", "gpio72", "gpio73",
	"gpio74", "gpio75", "gpio76", "gpio77", "gpio78", "gpio79", "gpio103",
	"gpio104", "gpio105", "gpio106", "gpio107", "gpio108", "gpio109",
	"gpio110", "gpio111", "gpio112", "gpio113", "gpio114",
};

static const char * const pll_bist_groups[] = {
	"gpio81",
};

static const char * const pll_clk_groups[] = {
	"gpio81",
};

static const char * const pri_mi2s_groups[] = {
	"gpio123",
};

static const char * const prng_rosc_groups[] = {
	"gpio185",
};

static const char * const qdss_cti_groups[] = {
	"gpio14", "gpio27", "gpio87", "gpio88", "gpio89", "gpio90", "gpio91", "gpio92",
};

static const char * const qdss_gpio_groups[] = {
	"gpio100", "gpio101", "gpio102", "gpio103", "gpio104", "gpio105", "gpio106", "gpio107",
	"gpio108", "gpio109", "gpio110", "gpio111", "gpio112", "gpio113", "gpio114", "gpio115",
	"gpio116", "gpio117", "gpio183", "gpio184", "gpio185", "gpio186", "gpio187", "gpio188",
	"gpio189", "gpio190", "gpio191", "gpio192", "gpio193", "gpio194", "gpio195", "gpio196",
	"gpio197", "gpio198", "gpio199", "gpio200",
};

static const char * const qlink0_enable_groups[] = {
	"gpio160",
};

static const char * const qlink0_request_groups[] = {
	"gpio159",
};

static const char * const qlink0_wmss_groups[] = {
	"gpio161",
};

static const char * const qlink1_enable_groups[] = {
	"gpio163",
};

static const char * const qlink1_request_groups[] = {
	"gpio162",
};

static const char * const qlink1_wmss_groups[] = {
	"gpio164",
};

static const char * const qlink2_enable_groups[] = {
	"gpio166",
};

static const char * const qlink2_request_groups[] = {
	"gpio165",
};

static const char * const qlink2_wmss_groups[] = {
	"gpio167",
};

static const char * const qspi0_groups[] = {
	"gpio44",
};

static const char * const qspi1_groups[] = {
	"gpio45",
};

static const char * const qspi2_groups[] = {
	"gpio48",
};

static const char * const qspi3_groups[] = {
	"gpio49",
};

static const char * const qspi_clk_groups[] = {
	"gpio50",
};

static const char * const qspi_cs_groups[] = {
	"gpio47", "gpio51",
};

static const char * const qup0_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};

static const char * const qup1_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};

static const char * const qup10_groups[] = {
	"gpio44", "gpio45", "gpio46", "gpio47",
};

static const char * const qup11_groups[] = {
	"gpio48", "gpio49", "gpio50", "gpio51",
};

static const char * const qup12_groups[] = {
	"gpio52", "gpio53", "gpio54", "gpio55",
};

static const char * const qup13_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};

static const char * const qup14_groups[] = {
	"gpio56", "gpio57", "gpio58", "gpio59",
};

static const char * const qup15_groups[] = {
	"gpio60", "gpio61", "gpio62", "gpio63",
};

static const char * const qup16_groups[] = {
	"gpio64", "gpio65", "gpio66", "gpio67",
};

static const char * const qup17_groups[] = {
	"gpio72", "gpio73", "gpio74", "gpio75",
};

static const char * const qup18_groups[] = {
	"gpio68", "gpio69", "gpio70", "gpio71",
};

static const char * const qup19_groups[] = {
	"gpio76", "gpio77", "gpio78", "gpio79",
};

static const char * const qup2_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15",
};

static const char * const qup3_groups[] = {
	"gpio16", "gpio17", "gpio18", "gpio19",
};

static const char * const qup4_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23",
};

static const char * const qup5_groups[] = {
	"gpio24", "gpio25", "gpio26", "gpio27",
};

static const char * const qup6_groups[] = {
	"gpio28", "gpio29", "gpio30", "gpio31",
};

static const char * const qup7_groups[] = {
	"gpio32", "gpio33", "gpio34", "gpio35",
};

static const char * const qup8_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};

static const char * const qup9_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43",
};

static const char * const qup_l4_groups[] = {
	"gpio2", "gpio6", "gpio58", "gpio63",
};

static const char * const qup_l5_groups[] = {
	"gpio3", "gpio7", "gpio59", "gpio66",
};

static const char * const qup_l6_groups[] = {
	"gpio10", "gpio42", "gpio62", "gpio67",
};

static const char * const sd_write_groups[] = {
	"gpio93",
};

static const char * const sdc40_groups[] = {
	"gpio44",
};

static const char * const sdc41_groups[] = {
	"gpio45",
};

static const char * const sdc42_groups[] = {
	"gpio48",
};

static const char * const sdc43_groups[] = {
	"gpio49",
};

static const char * const sdc4_clk_groups[] = {
	"gpio50",
};

static const char * const sdc4_cmd_groups[] = {
	"gpio51",
};

static const char * const sec_mi2s_groups[] = {
	"gpio124",
};

static const char * const tb_trig_groups[] = {
	"gpio64", "gpio136",
};

static const char * const tgu_ch0_groups[] = {
	"gpio99",
};

static const char * const tgu_ch1_groups[] = {
	"gpio100",
};

static const char * const tgu_ch2_groups[] = {
	"gpio101",
};

static const char * const tgu_ch3_groups[] = {
	"gpio102",
};

static const char * const tsense_pwm1_groups[] = {
	"gpio88",
};

static const char * const tsense_pwm2_groups[] = {
	"gpio88",
};

static const char * const uim0_clk_groups[] = {
	"gpio138",
};

static const char * const uim0_data_groups[] = {
	"gpio137",
};

static const char * const uim0_present_groups[] = {
	"gpio140",
};

static const char * const uim0_reset_groups[] = {
	"gpio139",
};

static const char * const uim1_clk_groups[] = {
	"gpio134",
};

static const char * const uim1_data_groups[] = {
	"gpio133",
};

static const char * const uim1_present_groups[] = {
	"gpio136",
};

static const char * const uim1_reset_groups[] = {
	"gpio135",
};

static const char * const usb2phy_ac_groups[] = {
	"gpio39", "gpio80",
};

static const char * const usb_phy_groups[] = {
	"gpio81",
};

static const char * const vfr_0_groups[] = {
	"gpio84",
};

static const char * const vfr_1_groups[] = {
	"gpio90",
};

static const char * const vsense_trigger_groups[] = {
	"gpio78",
};

static const struct pinfunction sm8350_functions[] = {
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_usb),
	MSM_PIN_FUNCTION(audio_ref),
	MSM_PIN_FUNCTION(cam_mclk),
	MSM_PIN_FUNCTION(cci_async),
	MSM_PIN_FUNCTION(cci_i2c),
	MSM_PIN_FUNCTION(cci_timer),
	MSM_PIN_FUNCTION(cmu_rng),
	MSM_PIN_FUNCTION(coex_uart1),
	MSM_PIN_FUNCTION(coex_uart2),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(cri_trng0),
	MSM_PIN_FUNCTION(cri_trng1),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(ddr_bist),
	MSM_PIN_FUNCTION(ddr_pxi0),
	MSM_PIN_FUNCTION(ddr_pxi1),
	MSM_PIN_FUNCTION(ddr_pxi2),
	MSM_PIN_FUNCTION(ddr_pxi3),
	MSM_PIN_FUNCTION(dp_hot),
	MSM_PIN_FUNCTION(dp_lcd),
	MSM_PIN_FUNCTION(gcc_gp1),
	MSM_PIN_FUNCTION(gcc_gp2),
	MSM_PIN_FUNCTION(gcc_gp3),
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(ibi_i3c),
	MSM_PIN_FUNCTION(jitter_bist),
	MSM_PIN_FUNCTION(lpass_slimbus),
	MSM_PIN_FUNCTION(mdp_vsync),
	MSM_PIN_FUNCTION(mdp_vsync0),
	MSM_PIN_FUNCTION(mdp_vsync1),
	MSM_PIN_FUNCTION(mdp_vsync2),
	MSM_PIN_FUNCTION(mdp_vsync3),
	MSM_PIN_FUNCTION(mi2s0_data0),
	MSM_PIN_FUNCTION(mi2s0_data1),
	MSM_PIN_FUNCTION(mi2s0_sck),
	MSM_PIN_FUNCTION(mi2s0_ws),
	MSM_PIN_FUNCTION(mi2s1_data0),
	MSM_PIN_FUNCTION(mi2s1_data1),
	MSM_PIN_FUNCTION(mi2s1_sck),
	MSM_PIN_FUNCTION(mi2s1_ws),
	MSM_PIN_FUNCTION(mi2s2_data0),
	MSM_PIN_FUNCTION(mi2s2_data1),
	MSM_PIN_FUNCTION(mi2s2_sck),
	MSM_PIN_FUNCTION(mi2s2_ws),
	MSM_PIN_FUNCTION(mss_grfc0),
	MSM_PIN_FUNCTION(mss_grfc1),
	MSM_PIN_FUNCTION(mss_grfc10),
	MSM_PIN_FUNCTION(mss_grfc11),
	MSM_PIN_FUNCTION(mss_grfc12),
	MSM_PIN_FUNCTION(mss_grfc2),
	MSM_PIN_FUNCTION(mss_grfc3),
	MSM_PIN_FUNCTION(mss_grfc4),
	MSM_PIN_FUNCTION(mss_grfc5),
	MSM_PIN_FUNCTION(mss_grfc6),
	MSM_PIN_FUNCTION(mss_grfc7),
	MSM_PIN_FUNCTION(mss_grfc8),
	MSM_PIN_FUNCTION(mss_grfc9),
	MSM_PIN_FUNCTION(nav_gpio),
	MSM_PIN_FUNCTION(pa_indicator),
	MSM_PIN_FUNCTION(pcie0_clkreqn),
	MSM_PIN_FUNCTION(pcie1_clkreqn),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(pll_bist),
	MSM_PIN_FUNCTION(pll_clk),
	MSM_PIN_FUNCTION(pri_mi2s),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qdss_gpio),
	MSM_PIN_FUNCTION(qlink0_enable),
	MSM_PIN_FUNCTION(qlink0_request),
	MSM_PIN_FUNCTION(qlink0_wmss),
	MSM_PIN_FUNCTION(qlink1_enable),
	MSM_PIN_FUNCTION(qlink1_request),
	MSM_PIN_FUNCTION(qlink1_wmss),
	MSM_PIN_FUNCTION(qlink2_enable),
	MSM_PIN_FUNCTION(qlink2_request),
	MSM_PIN_FUNCTION(qlink2_wmss),
	MSM_PIN_FUNCTION(qspi0),
	MSM_PIN_FUNCTION(qspi1),
	MSM_PIN_FUNCTION(qspi2),
	MSM_PIN_FUNCTION(qspi3),
	MSM_PIN_FUNCTION(qspi_clk),
	MSM_PIN_FUNCTION(qspi_cs),
	MSM_PIN_FUNCTION(qup0),
	MSM_PIN_FUNCTION(qup1),
	MSM_PIN_FUNCTION(qup10),
	MSM_PIN_FUNCTION(qup11),
	MSM_PIN_FUNCTION(qup12),
	MSM_PIN_FUNCTION(qup13),
	MSM_PIN_FUNCTION(qup14),
	MSM_PIN_FUNCTION(qup15),
	MSM_PIN_FUNCTION(qup16),
	MSM_PIN_FUNCTION(qup17),
	MSM_PIN_FUNCTION(qup18),
	MSM_PIN_FUNCTION(qup19),
	MSM_PIN_FUNCTION(qup2),
	MSM_PIN_FUNCTION(qup3),
	MSM_PIN_FUNCTION(qup4),
	MSM_PIN_FUNCTION(qup5),
	MSM_PIN_FUNCTION(qup6),
	MSM_PIN_FUNCTION(qup7),
	MSM_PIN_FUNCTION(qup8),
	MSM_PIN_FUNCTION(qup9),
	MSM_PIN_FUNCTION(qup_l4),
	MSM_PIN_FUNCTION(qup_l5),
	MSM_PIN_FUNCTION(qup_l6),
	MSM_PIN_FUNCTION(sd_write),
	MSM_PIN_FUNCTION(sdc40),
	MSM_PIN_FUNCTION(sdc41),
	MSM_PIN_FUNCTION(sdc42),
	MSM_PIN_FUNCTION(sdc43),
	MSM_PIN_FUNCTION(sdc4_clk),
	MSM_PIN_FUNCTION(sdc4_cmd),
	MSM_PIN_FUNCTION(sec_mi2s),
	MSM_PIN_FUNCTION(tb_trig),
	MSM_PIN_FUNCTION(tgu_ch0),
	MSM_PIN_FUNCTION(tgu_ch1),
	MSM_PIN_FUNCTION(tgu_ch2),
	MSM_PIN_FUNCTION(tgu_ch3),
	MSM_PIN_FUNCTION(tsense_pwm1),
	MSM_PIN_FUNCTION(tsense_pwm2),
	MSM_PIN_FUNCTION(uim0_clk),
	MSM_PIN_FUNCTION(uim0_data),
	MSM_PIN_FUNCTION(uim0_present),
	MSM_PIN_FUNCTION(uim0_reset),
	MSM_PIN_FUNCTION(uim1_clk),
	MSM_PIN_FUNCTION(uim1_data),
	MSM_PIN_FUNCTION(uim1_present),
	MSM_PIN_FUNCTION(uim1_reset),
	MSM_PIN_FUNCTION(usb2phy_ac),
	MSM_PIN_FUNCTION(usb_phy),
	MSM_PIN_FUNCTION(vfr_0),
	MSM_PIN_FUNCTION(vfr_1),
	MSM_PIN_FUNCTION(vsense_trigger),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup sm8350_groups[] = {
	[0] = PINGROUP(0, qup13, _, _, _, _, _, _, _, _),
	[1] = PINGROUP(1, qup13, _, _, _, _, _, _, _, _),
	[2] = PINGROUP(2, qup13, qup_l4, _, _, _, _, _, _, _),
	[3] = PINGROUP(3, qup13, qup_l5, _, _, _, _, _, _, _),
	[4] = PINGROUP(4, qup0, _, _, _, _, _, _, _, _),
	[5] = PINGROUP(5, qup0, _, _, _, _, _, _, _, _),
	[6] = PINGROUP(6, qup0, qup_l4, _, _, _, _, _, _, _),
	[7] = PINGROUP(7, qup0, qup_l5, _, _, _, _, _, _, _),
	[8] = PINGROUP(8, qup1, _, _, _, _, _, _, _, _),
	[9] = PINGROUP(9, qup1, _, _, _, _, _, _, _, _),
	[10] = PINGROUP(10, qup1, qup_l6, _, _, _, _, _, _, _),
	[11] = PINGROUP(11, qup1, _, _, _, _, _, _, _, _),
	[12] = PINGROUP(12, qup2, phase_flag, _, _, _, _, _, _, _),
	[13] = PINGROUP(13, qup2, phase_flag, _, _, _, _, _, _, _),
	[14] = PINGROUP(14, qup2, qdss_cti, dbg_out, _, _, _, _, _, _),
	[15] = PINGROUP(15, qup2, mdp_vsync, _, _, _, _, _, _, _),
	[16] = PINGROUP(16, qup3, phase_flag, _, _, _, _, _, _, _),
	[17] = PINGROUP(17, qup3, phase_flag, _, _, _, _, _, _, _),
	[18] = PINGROUP(18, qup3, _, _, _, _, _, _, _, _),
	[19] = PINGROUP(19, qup3, _, _, _, _, _, _, _, _),
	[20] = PINGROUP(20, qup4, _, _, _, _, _, _, _, _),
	[21] = PINGROUP(21, qup4, _, _, _, _, _, _, _, _),
	[22] = PINGROUP(22, qup4, _, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, qup4, _, _, _, _, _, _, _, _),
	[24] = PINGROUP(24, qup5, _, _, _, _, _, _, _, _),
	[25] = PINGROUP(25, qup5, _, _, _, _, _, _, _, _),
	[26] = PINGROUP(26, qup5, mdp_vsync, _, _, _, _, _, _, _),
	[27] = PINGROUP(27, qup5, qdss_cti, _, _, _, _, _, _, _),
	[28] = PINGROUP(28, qup6, phase_flag, _, _, _, _, _, _, _),
	[29] = PINGROUP(29, qup6, phase_flag, _, _, _, _, _, _, _),
	[30] = PINGROUP(30, qup6, phase_flag, _, _, _, _, _, _, _),
	[31] = PINGROUP(31, qup6, phase_flag, _, _, _, _, _, _, _),
	[32] = PINGROUP(32, qup7, phase_flag, _, _, _, _, _, _, _),
	[33] = PINGROUP(33, qup7, phase_flag, _, _, _, _, _, _, _),
	[34] = PINGROUP(34, qup7, phase_flag, _, _, _, _, _, _, _),
	[35] = PINGROUP(35, qup7, phase_flag, _, _, _, _, _, _, _),
	[36] = PINGROUP(36, qup8, ibi_i3c, ddr_bist, _, _, _, _, _, _),
	[37] = PINGROUP(37, qup8, ibi_i3c, ddr_bist, _, _, _, _, _, _),
	[38] = PINGROUP(38, qup8, _, _, _, _, _, _, _, _),
	[39] = PINGROUP(39, qup8, usb2phy_ac, _, _, _, _, _, _, _),
	[40] = PINGROUP(40, qup9, ddr_bist, _, _, _, _, _, _, _),
	[41] = PINGROUP(41, qup9, ddr_bist, _, _, _, _, _, _, _),
	[42] = PINGROUP(42, qup9, qup_l6, _, _, _, _, _, _, _),
	[43] = PINGROUP(43, qup9, ddr_pxi3, _, _, _, _, _, _, _),
	[44] = PINGROUP(44, qup10, qspi0, sdc40, ddr_pxi3, _, _, _, _, _),
	[45] = PINGROUP(45, qup10, qspi1, sdc41, ddr_pxi2, _, _, _, _, _),
	[46] = PINGROUP(46, qup10, _, _, _, _, _, _, _, _),
	[47] = PINGROUP(47, qup10, qspi_cs, ddr_pxi2, _, _, _, _, _, _),
	[48] = PINGROUP(48, qup11, qspi2, sdc42, ddr_pxi1, _, _, _, _, _),
	[49] = PINGROUP(49, qup11, qspi3, sdc43, ddr_pxi1, _, _, _, _, _),
	[50] = PINGROUP(50, qup11, qspi_clk, sdc4_clk, _, _, _, _, _, _),
	[51] = PINGROUP(51, qup11, qspi_cs, sdc4_cmd, ddr_pxi0, _, _, _, _, _),
	[52] = PINGROUP(52, qup12, ddr_pxi0, _, _, _, _, _, _, _),
	[53] = PINGROUP(53, qup12, _, _, _, _, _, _, _, _),
	[54] = PINGROUP(54, qup12, _, _, _, _, _, _, _, _),
	[55] = PINGROUP(55, qup12, atest_usb, _, _, _, _, _, _, _),
	[56] = PINGROUP(56, qup14, ibi_i3c, _, _, _, _, _, _, _),
	[57] = PINGROUP(57, qup14, ibi_i3c, _, _, _, _, _, _, _),
	[58] = PINGROUP(58, qup14, qup_l4, _, _, _, _, _, _, _),
	[59] = PINGROUP(59, qup14, qup_l5, _, _, _, _, _, _, _),
	[60] = PINGROUP(60, qup15, ibi_i3c, _, _, _, _, _, _, _),
	[61] = PINGROUP(61, qup15, ibi_i3c, _, _, _, _, _, _, _),
	[62] = PINGROUP(62, qup15, qup_l6, _, _, _, _, _, _, _),
	[63] = PINGROUP(63, qup15, qup_l4, _, _, _, _, _, _, _),
	[64] = PINGROUP(64, qup16, tb_trig, _, _, _, _, _, _, _),
	[65] = PINGROUP(65, qup16, _, _, _, _, _, _, _, _),
	[66] = PINGROUP(66, qup16, qup_l5, _, _, _, _, _, _, _),
	[67] = PINGROUP(67, qup16, qup_l6, _, _, _, _, _, _, _),
	[68] = PINGROUP(68, qup18, _, _, _, _, _, _, _, _),
	[69] = PINGROUP(69, qup18, _, _, _, _, _, _, _, _),
	[70] = PINGROUP(70, qup18, _, _, _, _, _, _, _, _),
	[71] = PINGROUP(71, qup18, _, _, _, _, _, _, _, _),
	[72] = PINGROUP(72, qup17, phase_flag, _, _, _, _, _, _, _),
	[73] = PINGROUP(73, qup17, phase_flag, _, _, _, _, _, _, _),
	[74] = PINGROUP(74, qup17, phase_flag, _, _, _, _, _, _, _),
	[75] = PINGROUP(75, qup17, phase_flag, _, _, _, _, _, _, _),
	[76] = PINGROUP(76, qup19, phase_flag, _, _, _, _, _, _, _),
	[77] = PINGROUP(77, qup19, phase_flag, _, _, _, _, _, _, _),
	[78] = PINGROUP(78, qup19, phase_flag, _, vsense_trigger, _, _, _, _, _),
	[79] = PINGROUP(79, qup19, phase_flag, _, _, _, _, _, _, _),
	[80] = PINGROUP(80, usb2phy_ac, jitter_bist, atest_usb, _, _, _, _, _, _),
	[81] = PINGROUP(81, usb_phy, pll_bist, pll_clk, atest_usb, _, _, _, _, _),
	[82] = PINGROUP(82, mdp_vsync, _, _, _, _, _, _, _, _),
	[83] = PINGROUP(83, mdp_vsync, dp_lcd, _, _, _, _, _, _, _),
	[84] = PINGROUP(84, mdp_vsync, vfr_0, _, _, _, _, _, _, _),
	[85] = PINGROUP(85, atest_char, _, _, _, _, _, _, _, _),
	[86] = PINGROUP(86, mdp_vsync0, mdp_vsync1, atest_char, _, _, _, _, _, _),
	[87] = PINGROUP(87, dp_hot, mdp_vsync2, mdp_vsync3, qdss_cti, atest_char, _, _, _, _),
	[88] = PINGROUP(88, qdss_cti, tsense_pwm1, tsense_pwm2, _, _, _, _, _, _),
	[89] = PINGROUP(89, qdss_cti, _, _, _, _, _, _, _, _),
	[90] = PINGROUP(90, vfr_1, qdss_cti, _, _, _, _, _, _, _),
	[91] = PINGROUP(91, qdss_cti, _, _, _, _, _, _, _, _),
	[92] = PINGROUP(92, qdss_cti, _, _, _, _, _, _, _, _),
	[93] = PINGROUP(93, sd_write, _, _, _, _, _, _, _, _),
	[94] = PINGROUP(94, _, _, _, _, _, _, _, _, _),
	[95] = PINGROUP(95, pcie0_clkreqn, _, _, _, _, _, _, _, _),
	[96] = PINGROUP(96, _, _, _, _, _, _, _, _, _),
	[97] = PINGROUP(97, _, _, _, _, _, _, _, _, _),
	[98] = PINGROUP(98, pcie1_clkreqn, _, _, _, _, _, _, _, _),
	[99] = PINGROUP(99, tgu_ch0, _, _, _, _, _, _, _, _),
	[100] = PINGROUP(100, cam_mclk, tgu_ch1, qdss_gpio, _, _, _, _, _, _),
	[101] = PINGROUP(101, cam_mclk, tgu_ch2, qdss_gpio, _, _, _, _, _, _),
	[102] = PINGROUP(102, cam_mclk, tgu_ch3, qdss_gpio, _, _, _, _, _, _),
	[103] = PINGROUP(103, cam_mclk, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[104] = PINGROUP(104, cam_mclk, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[105] = PINGROUP(105, cam_mclk, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[106] = PINGROUP(106, cci_async, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[107] = PINGROUP(107, cci_i2c, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[108] = PINGROUP(108, cci_i2c, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[109] = PINGROUP(109, cci_i2c, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[110] = PINGROUP(110, cci_i2c, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[111] = PINGROUP(111, cci_i2c, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[112] = PINGROUP(112, cci_i2c, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[113] = PINGROUP(113, cci_i2c, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[114] = PINGROUP(114, cci_i2c, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[115] = PINGROUP(115, cci_timer, gcc_gp1, qdss_gpio, atest_char, _, _, _, _, _),
	[116] = PINGROUP(116, cci_timer, gcc_gp2, qdss_gpio, _, _, _, _, _, _),
	[117] = PINGROUP(117, cci_timer, gcc_gp3, qdss_gpio, atest_char, _, _, _, _, _),
	[118] = PINGROUP(118, cci_timer, cci_async, _, _, _, _, _, _, _),
	[119] = PINGROUP(119, cci_timer, cci_async, _, _, _, _, _, _, _),
	[120] = PINGROUP(120, mi2s2_sck, _, _, _, _, _, _, _, _),
	[121] = PINGROUP(121, mi2s2_data0, _, _, _, _, _, _, _, _),
	[122] = PINGROUP(122, mi2s2_ws, _, _, _, _, _, _, _, _),
	[123] = PINGROUP(123, pri_mi2s, _, _, _, _, _, _, _, _),
	[124] = PINGROUP(124, sec_mi2s, audio_ref, mi2s2_data1, _, _, _, _, _, _),
	[125] = PINGROUP(125, mi2s0_sck, _, _, _, _, _, _, _, _),
	[126] = PINGROUP(126, mi2s0_data0, _, _, _, _, _, _, _, _),
	[127] = PINGROUP(127, mi2s0_data1, _, _, _, _, _, _, _, _),
	[128] = PINGROUP(128, mi2s0_ws, _, _, _, _, _, _, _, _),
	[129] = PINGROUP(129, lpass_slimbus, mi2s1_sck, gcc_gp1, _, _, _, _, _, _),
	[130] = PINGROUP(130, lpass_slimbus, mi2s1_data0, gcc_gp2, _, _, _, _, _, _),
	[131] = PINGROUP(131, mi2s1_data1, gcc_gp3, _, _, _, _, _, _, _),
	[132] = PINGROUP(132, mi2s1_ws, _, _, _, _, _, _, _, _),
	[133] = PINGROUP(133, uim1_data, _, _, _, _, _, _, _, _),
	[134] = PINGROUP(134, uim1_clk, _, _, _, _, _, _, _, _),
	[135] = PINGROUP(135, uim1_reset, _, _, _, _, _, _, _, _),
	[136] = PINGROUP(136, uim1_present, tb_trig, _, _, _, _, _, _, _),
	[137] = PINGROUP(137, uim0_data, _, _, _, _, _, _, _, _),
	[138] = PINGROUP(138, uim0_clk, _, _, _, _, _, _, _, _),
	[139] = PINGROUP(139, uim0_reset, _, _, _, _, _, _, _, _),
	[140] = PINGROUP(140, uim0_present, _, _, _, _, _, _, _, _),
	[141] = PINGROUP(141, _, mss_grfc0, _, _, _, _, _, _, _),
	[142] = PINGROUP(142, _, mss_grfc1, _, _, _, _, _, _, _),
	[143] = PINGROUP(143, _, mss_grfc2, _, _, _, _, _, _, _),
	[144] = PINGROUP(144, _, mss_grfc3, _, _, _, _, _, _, _),
	[145] = PINGROUP(145, _, mss_grfc4, _, _, _, _, _, _, _),
	[146] = PINGROUP(146, _, mss_grfc5, _, _, _, _, _, _, _),
	[147] = PINGROUP(147, _, mss_grfc6, _, _, _, _, _, _, _),
	[148] = PINGROUP(148, _, mss_grfc7, _, _, _, _, _, _, _),
	[149] = PINGROUP(149, _, mss_grfc8, _, _, _, _, _, _, _),
	[150] = PINGROUP(150, _, mss_grfc9, _, _, _, _, _, _, _),
	[151] = PINGROUP(151, coex_uart1, atest_usb, _, _, _, _, _, _, _),
	[152] = PINGROUP(152, coex_uart1, atest_usb, _, _, _, _, _, _, _),
	[153] = PINGROUP(153, coex_uart2, mss_grfc10, atest_usb, _, _, _, _, _, _),
	[154] = PINGROUP(154, coex_uart2, mss_grfc11, atest_usb, _, _, _, _, _, _),
	[155] = PINGROUP(155, nav_gpio, _, _, _, _, _, _, _, _),
	[156] = PINGROUP(156, nav_gpio, _, _, _, _, _, _, _, _),
	[157] = PINGROUP(157, mss_grfc12, pa_indicator, nav_gpio, _, _, _, _, _, _),
	[158] = PINGROUP(158, mss_grfc0, atest_usb, _, _, _, _, _, _, _),
	[159] = PINGROUP(159, qlink0_request, atest_usb, _, _, _, _, _, _, _),
	[160] = PINGROUP(160, qlink0_enable, _, _, _, _, _, _, _, _),
	[161] = PINGROUP(161, qlink0_wmss, atest_usb, _, _, _, _, _, _, _),
	[162] = PINGROUP(162, qlink1_request, _, _, _, _, _, _, _, _),
	[163] = PINGROUP(163, qlink1_enable, _, _, _, _, _, _, _, _),
	[164] = PINGROUP(164, qlink1_wmss, _, _, _, _, _, _, _, _),
	[165] = PINGROUP(165, qlink2_request, _, _, _, _, _, _, _, _),
	[166] = PINGROUP(166, qlink2_enable, _, _, _, _, _, _, _, _),
	[167] = PINGROUP(167, qlink2_wmss, _, _, _, _, _, _, _, _),
	[168] = PINGROUP(168, _, _, _, _, _, _, _, _, _),
	[169] = PINGROUP(169, _, _, _, _, _, _, _, _, _),
	[170] = PINGROUP(170, _, _, _, _, _, _, _, _, _),
	[171] = PINGROUP(171, _, _, _, _, _, _, _, _, _),
	[172] = PINGROUP(172, _, _, _, _, _, _, _, _, _),
	[173] = PINGROUP(173, _, _, _, _, _, _, _, _, _),
	[174] = PINGROUP(174, cmu_rng, _, _, _, _, _, _, _, _),
	[175] = PINGROUP(175, cmu_rng, _, _, _, _, _, _, _, _),
	[176] = PINGROUP(176, cmu_rng, _, _, _, _, _, _, _, _),
	[177] = PINGROUP(177, cmu_rng, _, _, _, _, _, _, _, _),
	[178] = PINGROUP(178, _, _, _, _, _, _, _, _, _),
	[179] = PINGROUP(179, _, _, _, _, _, _, _, _, _),
	[180] = PINGROUP(180, _, _, _, _, _, _, _, _, _),
	[181] = PINGROUP(181, _, _, _, _, _, _, _, _, _),
	[182] = PINGROUP(182, _, _, _, _, _, _, _, _, _),
	[183] = PINGROUP(183, cri_trng0, qdss_gpio, _, _, _, _, _, _, _),
	[184] = PINGROUP(184, cri_trng1, qdss_gpio, _, _, _, _, _, _, _),
	[185] = PINGROUP(185, prng_rosc, qdss_gpio, _, _, _, _, _, _, _),
	[186] = PINGROUP(186, cri_trng, qdss_gpio, _, _, _, _, _, _, _),
	[187] = PINGROUP(187, qdss_gpio, _, _, _, _, _, _, _, _),
	[188] = PINGROUP(188, qdss_gpio, _, _, _, _, _, _, _, _),
	[189] = PINGROUP(189, qdss_gpio, _, _, _, _, _, _, _, _),
	[190] = PINGROUP(190, qdss_gpio, _, _, _, _, _, _, _, _),
	[191] = PINGROUP(191, qdss_gpio, _, _, _, _, _, _, _, _),
	[192] = PINGROUP(192, qdss_gpio, _, _, _, _, _, _, _, _),
	[193] = PINGROUP(193, qdss_gpio, _, _, _, _, _, _, _, _),
	[194] = PINGROUP(194, qdss_gpio, _, _, _, _, _, _, _, _),
	[195] = PINGROUP(195, qdss_gpio, _, _, _, _, _, _, _, _),
	[196] = PINGROUP(196, qdss_gpio, _, _, _, _, _, _, _, _),
	[197] = PINGROUP(197, qdss_gpio, _, _, _, _, _, _, _, _),
	[198] = PINGROUP(198, qdss_gpio, _, _, _, _, _, _, _, _),
	[199] = PINGROUP(199, qdss_gpio, _, _, _, _, _, _, _, _),
	[200] = PINGROUP(200, qdss_gpio, _, _, _, _, _, _, _, _),
	[201] = PINGROUP(201, _, _, _, _, _, _, _, _, _),
	[202] = PINGROUP(202, _, _, _, _, _, _, _, _, _),
	[203] = UFS_RESET(ufs_reset, 0xd8000),
	[204] = SDC_PINGROUP(sdc2_clk, 0xcf000, 14, 6),
	[205] = SDC_PINGROUP(sdc2_cmd, 0xcf000, 11, 3),
	[206] = SDC_PINGROUP(sdc2_data, 0xcf000, 9, 0),
};

static const struct msm_gpio_wakeirq_map sm8350_pdc_map[] = {
	{ 2, 117 }, { 7, 82 }, { 11, 83 }, { 14, 80 }, { 15, 146 },
	{ 19, 121 }, { 23, 84 }, { 26, 86 }, { 27, 75 }, { 31, 85 },
	{ 32, 97 }, { 34, 98 }, { 35, 131 }, { 36, 79 }, { 38, 99 },
	{ 39, 92 }, { 40, 101 }, { 43, 137 }, { 44, 102 }, { 46, 96 },
	{ 47, 93 }, { 50, 108 }, { 51, 127 }, { 55, 128 }, { 56, 81 },
	{ 59, 112 }, { 60, 119 }, { 63, 73 }, { 67, 74 }, { 71, 134 },
	{ 75, 103 }, { 79, 104 }, { 80, 126 }, { 81, 139 }, { 82, 140 },
	{ 83, 141 }, { 84, 124 }, { 85, 109 }, { 86, 143 }, { 87, 138 },
	{ 88, 122 }, { 89, 113 }, { 90, 114 }, { 91, 115 }, { 92, 76 },
	{ 95, 147 }, { 96, 148 }, { 98, 149 }, { 99, 150 }, { 115, 125 },
	{ 116, 106 }, { 117, 105 }, { 118, 116 }, { 119, 123 }, { 130, 145 },
	{ 136, 72 }, { 140, 100 }, { 151, 110 }, { 153, 95 }, { 155, 107 },
	{ 156, 94 }, { 157, 111 }, { 159, 118 }, { 162, 77 }, { 165, 78 },
	{ 169, 70 }, { 172, 132 }, { 174, 87 }, { 175, 88 }, { 177, 89 },
	{ 179, 120 }, { 180, 129 }, { 183, 90 }, { 185, 136 }, { 187, 142 },
	{ 190, 144 }, { 198, 91 }, { 200, 133 }, { 202, 135 },
};

static const struct msm_pinctrl_soc_data sm8350_tlmm = {
	.pins = sm8350_pins,
	.npins = ARRAY_SIZE(sm8350_pins),
	.functions = sm8350_functions,
	.nfunctions = ARRAY_SIZE(sm8350_functions),
	.groups = sm8350_groups,
	.ngroups = ARRAY_SIZE(sm8350_groups),
	.ngpios = 204,
	.wakeirq_map = sm8350_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(sm8350_pdc_map),
};

static int sm8350_tlmm_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &sm8350_tlmm);
}

static const struct of_device_id sm8350_tlmm_of_match[] = {
	{ .compatible = "qcom,sm8350-tlmm", },
	{ },
};

static struct platform_driver sm8350_tlmm_driver = {
	.driver = {
		.name = "sm8350-tlmm",
		.of_match_table = sm8350_tlmm_of_match,
	},
	.probe = sm8350_tlmm_probe,
	.remove = msm_pinctrl_remove,
};

static int __init sm8350_tlmm_init(void)
{
	return platform_driver_register(&sm8350_tlmm_driver);
}
arch_initcall(sm8350_tlmm_init);

static void __exit sm8350_tlmm_exit(void)
{
	platform_driver_unregister(&sm8350_tlmm_driver);
}
module_exit(sm8350_tlmm_exit);

MODULE_DESCRIPTION("QTI SM8350 TLMM driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, sm8350_tlmm_of_match);
