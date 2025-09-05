// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11)    \
	{                                                             \
		.grp = PINCTRL_PINGROUP("gpio" #id,                   \
					gpio##id##_pins,              \
					ARRAY_SIZE(gpio##id##_pins)), \
		.ctl_reg = REG_SIZE * id,                             \
		.io_reg = 0x4 + REG_SIZE * id,                        \
		.intr_cfg_reg = 0x8 + REG_SIZE * id,                  \
		.intr_status_reg = 0xc + REG_SIZE * id,               \
		.intr_target_reg = 0x8 + REG_SIZE * id,               \
		.mux_bit = 2,                                         \
		.pull_bit = 0,                                        \
		.drv_bit = 6,                                         \
		.egpio_enable = 12,                                   \
		.egpio_present = 11,                                  \
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
			msm_mux_##f10,                                \
			msm_mux_##f11 /* egpio mode */                \
		},                                                    \
		.nfuncs = 12,                                         \
	}

#define SDC_QDSD_PINGROUP(pg_name, ctl, pull, drv)                   \
	{                                                            \
		.grp = PINCTRL_PINGROUP(#pg_name,                    \
					pg_name##_pins,              \
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
		.intr_raw_status_bit = -1,                           \
		.intr_polarity_bit = -1,                             \
		.intr_detection_bit = -1,                            \
		.intr_detection_width = -1,                          \
	}

#define UFS_RESET(pg_name, ctl, io)			\
	{					        \
		.grp = PINCTRL_PINGROUP(#pg_name,	\
			pg_name##_pins,			\
			ARRAY_SIZE(pg_name##_pins)),	\
		.ctl_reg = ctl,				\
		.io_reg = io,				\
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

static const struct pinctrl_pin_desc glymur_pins[] = {
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
	PINCTRL_PIN(203, "GPIO_203"),
	PINCTRL_PIN(204, "GPIO_204"),
	PINCTRL_PIN(205, "GPIO_205"),
	PINCTRL_PIN(206, "GPIO_206"),
	PINCTRL_PIN(207, "GPIO_207"),
	PINCTRL_PIN(208, "GPIO_208"),
	PINCTRL_PIN(209, "GPIO_209"),
	PINCTRL_PIN(210, "GPIO_210"),
	PINCTRL_PIN(211, "GPIO_211"),
	PINCTRL_PIN(212, "GPIO_212"),
	PINCTRL_PIN(213, "GPIO_213"),
	PINCTRL_PIN(214, "GPIO_214"),
	PINCTRL_PIN(215, "GPIO_215"),
	PINCTRL_PIN(216, "GPIO_216"),
	PINCTRL_PIN(217, "GPIO_217"),
	PINCTRL_PIN(218, "GPIO_218"),
	PINCTRL_PIN(219, "GPIO_219"),
	PINCTRL_PIN(220, "GPIO_220"),
	PINCTRL_PIN(221, "GPIO_221"),
	PINCTRL_PIN(222, "GPIO_222"),
	PINCTRL_PIN(223, "GPIO_223"),
	PINCTRL_PIN(224, "GPIO_224"),
	PINCTRL_PIN(225, "GPIO_225"),
	PINCTRL_PIN(226, "GPIO_226"),
	PINCTRL_PIN(227, "GPIO_227"),
	PINCTRL_PIN(228, "GPIO_228"),
	PINCTRL_PIN(229, "GPIO_229"),
	PINCTRL_PIN(230, "GPIO_230"),
	PINCTRL_PIN(231, "GPIO_231"),
	PINCTRL_PIN(232, "GPIO_232"),
	PINCTRL_PIN(233, "GPIO_233"),
	PINCTRL_PIN(234, "GPIO_234"),
	PINCTRL_PIN(235, "GPIO_235"),
	PINCTRL_PIN(236, "GPIO_236"),
	PINCTRL_PIN(237, "GPIO_237"),
	PINCTRL_PIN(238, "GPIO_238"),
	PINCTRL_PIN(239, "GPIO_239"),
	PINCTRL_PIN(240, "GPIO_240"),
	PINCTRL_PIN(241, "GPIO_241"),
	PINCTRL_PIN(242, "GPIO_242"),
	PINCTRL_PIN(243, "GPIO_243"),
	PINCTRL_PIN(244, "GPIO_244"),
	PINCTRL_PIN(245, "GPIO_245"),
	PINCTRL_PIN(246, "GPIO_246"),
	PINCTRL_PIN(247, "GPIO_247"),
	PINCTRL_PIN(248, "GPIO_248"),
	PINCTRL_PIN(249, "GPIO_249"),
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
DECLARE_MSM_GPIO_PINS(203);
DECLARE_MSM_GPIO_PINS(204);
DECLARE_MSM_GPIO_PINS(205);
DECLARE_MSM_GPIO_PINS(206);
DECLARE_MSM_GPIO_PINS(207);
DECLARE_MSM_GPIO_PINS(208);
DECLARE_MSM_GPIO_PINS(209);
DECLARE_MSM_GPIO_PINS(210);
DECLARE_MSM_GPIO_PINS(211);
DECLARE_MSM_GPIO_PINS(212);
DECLARE_MSM_GPIO_PINS(213);
DECLARE_MSM_GPIO_PINS(214);
DECLARE_MSM_GPIO_PINS(215);
DECLARE_MSM_GPIO_PINS(216);
DECLARE_MSM_GPIO_PINS(217);
DECLARE_MSM_GPIO_PINS(218);
DECLARE_MSM_GPIO_PINS(219);
DECLARE_MSM_GPIO_PINS(220);
DECLARE_MSM_GPIO_PINS(221);
DECLARE_MSM_GPIO_PINS(222);
DECLARE_MSM_GPIO_PINS(223);
DECLARE_MSM_GPIO_PINS(224);
DECLARE_MSM_GPIO_PINS(225);
DECLARE_MSM_GPIO_PINS(226);
DECLARE_MSM_GPIO_PINS(227);
DECLARE_MSM_GPIO_PINS(228);
DECLARE_MSM_GPIO_PINS(229);
DECLARE_MSM_GPIO_PINS(230);
DECLARE_MSM_GPIO_PINS(231);
DECLARE_MSM_GPIO_PINS(232);
DECLARE_MSM_GPIO_PINS(233);
DECLARE_MSM_GPIO_PINS(234);
DECLARE_MSM_GPIO_PINS(235);
DECLARE_MSM_GPIO_PINS(236);
DECLARE_MSM_GPIO_PINS(237);
DECLARE_MSM_GPIO_PINS(238);
DECLARE_MSM_GPIO_PINS(239);
DECLARE_MSM_GPIO_PINS(240);
DECLARE_MSM_GPIO_PINS(241);
DECLARE_MSM_GPIO_PINS(242);
DECLARE_MSM_GPIO_PINS(243);
DECLARE_MSM_GPIO_PINS(244);
DECLARE_MSM_GPIO_PINS(245);
DECLARE_MSM_GPIO_PINS(246);
DECLARE_MSM_GPIO_PINS(247);
DECLARE_MSM_GPIO_PINS(248);
DECLARE_MSM_GPIO_PINS(249);

static const unsigned int ufs_reset_pins[] = { 250 };
static const unsigned int sdc2_clk_pins[] = { 251 };
static const unsigned int sdc2_cmd_pins[] = { 252 };
static const unsigned int sdc2_data_pins[] = { 253 };

enum glymur_functions {
	msm_mux_gpio,
	msm_mux_resout_gpio_n,
	msm_mux_aoss_cti,
	msm_mux_asc_cci,
	msm_mux_atest_char,
	msm_mux_atest_usb,
	msm_mux_audio_ext_mclk0,
	msm_mux_audio_ext_mclk1,
	msm_mux_audio_ref_clk,
	msm_mux_cam_asc_mclk4,
	msm_mux_cam_mclk,
	msm_mux_cci_async_in,
	msm_mux_cci_i2c_scl,
	msm_mux_cci_i2c_sda,
	msm_mux_cci_timer,
	msm_mux_cmu_rng,
	msm_mux_cri_trng,
	msm_mux_dbg_out_clk,
	msm_mux_ddr_bist_complete,
	msm_mux_ddr_bist_fail,
	msm_mux_ddr_bist_start,
	msm_mux_ddr_bist_stop,
	msm_mux_ddr_pxi,
	msm_mux_edp0_hot,
	msm_mux_edp0_lcd,
	msm_mux_edp1_lcd,
	msm_mux_egpio,
	msm_mux_eusb_ac_en,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_host2wlan_sol,
	msm_mux_i2c0_s_scl,
	msm_mux_i2c0_s_sda,
	msm_mux_i2s0_data,
	msm_mux_i2s0_sck,
	msm_mux_i2s0_ws,
	msm_mux_i2s1_data,
	msm_mux_i2s1_sck,
	msm_mux_i2s1_ws,
	msm_mux_ibi_i3c,
	msm_mux_jitter_bist,
	msm_mux_mdp_vsync_out,
	msm_mux_mdp_vsync_e,
	msm_mux_mdp_vsync_p,
	msm_mux_mdp_vsync_s,
	msm_mux_pcie3a_clk,
	msm_mux_pcie3a_rst_n,
	msm_mux_pcie3b_clk,
	msm_mux_pcie4_clk_req_n,
	msm_mux_pcie5_clk_req_n,
	msm_mux_pcie6_clk_req_n,
	msm_mux_phase_flag,
	msm_mux_pll_bist_sync,
	msm_mux_pll_clk_aux,
	msm_mux_pmc_oca_n,
	msm_mux_pmc_uva_n,
	msm_mux_prng_rosc,
	msm_mux_qdss_cti,
	msm_mux_qdss_gpio,
	msm_mux_qspi0,
	msm_mux_qup0_se0,
	msm_mux_qup0_se1,
	msm_mux_qup0_se2,
	msm_mux_qup0_se3,
	msm_mux_qup0_se4,
	msm_mux_qup0_se5,
	msm_mux_qup0_se6,
	msm_mux_qup0_se7,
	msm_mux_qup1_se0,
	msm_mux_qup1_se1,
	msm_mux_qup1_se2,
	msm_mux_qup1_se3,
	msm_mux_qup1_se4,
	msm_mux_qup1_se5,
	msm_mux_qup1_se6,
	msm_mux_qup1_se7,
	msm_mux_qup2_se0,
	msm_mux_qup2_se1,
	msm_mux_qup2_se2,
	msm_mux_qup2_se3,
	msm_mux_qup2_se4,
	msm_mux_qup2_se5,
	msm_mux_qup2_se6,
	msm_mux_qup2_se7,
	msm_mux_qup3_se0,
	msm_mux_qup3_se1,
	msm_mux_sd_write_protect,
	msm_mux_sdc4_clk,
	msm_mux_sdc4_cmd,
	msm_mux_sdc4_data,
	msm_mux_smb_acok_n,
	msm_mux_sys_throttle,
	msm_mux_tb_trig_sdc2,
	msm_mux_tb_trig_sdc4,
	msm_mux_tmess_prng,
	msm_mux_tsense_pwm,
	msm_mux_tsense_therm,
	msm_mux_usb0_dp,
	msm_mux_usb0_phy_ps,
	msm_mux_usb0_sbrx,
	msm_mux_usb0_sbtx,
	msm_mux_usb0_tmu,
	msm_mux_usb1_dbg,
	msm_mux_usb1_dp,
	msm_mux_usb1_phy_ps,
	msm_mux_usb1_sbrx,
	msm_mux_usb1_sbtx,
	msm_mux_usb1_tmu,
	msm_mux_usb2_dp,
	msm_mux_usb2_phy_ps,
	msm_mux_usb2_sbrx,
	msm_mux_usb2_sbtx,
	msm_mux_usb2_tmu,
	msm_mux_vsense_trigger_mirnat,
	msm_mux_wcn_sw,
	msm_mux_wcn_sw_ctrl,
	msm_mux__,
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
	"gpio132", "gpio133", "gpio134", "gpio135", "gpio136", "gpio137",
	"gpio138", "gpio139", "gpio140", "gpio141", "gpio142", "gpio143",
	"gpio144", "gpio145", "gpio146", "gpio147", "gpio148", "gpio149",
	"gpio150", "gpio151", "gpio152", "gpio153", "gpio154", "gpio155",
	"gpio156", "gpio157", "gpio158", "gpio159", "gpio160", "gpio161",
	"gpio162", "gpio163", "gpio164", "gpio165", "gpio166", "gpio167",
	"gpio168", "gpio169", "gpio170", "gpio171", "gpio172", "gpio173",
	"gpio174", "gpio175", "gpio176", "gpio177", "gpio178", "gpio179",
	"gpio180", "gpio181", "gpio182", "gpio183", "gpio184", "gpio185",
	"gpio186", "gpio187", "gpio188", "gpio189", "gpio190", "gpio191",
	"gpio192", "gpio193", "gpio194", "gpio195", "gpio196", "gpio197",
	"gpio198", "gpio199", "gpio200", "gpio201", "gpio202", "gpio203",
	"gpio204", "gpio205", "gpio206", "gpio207", "gpio208", "gpio209",
	"gpio210", "gpio211", "gpio212", "gpio213", "gpio214", "gpio215",
	"gpio216", "gpio217", "gpio218", "gpio219", "gpio220", "gpio221",
	"gpio222", "gpio223", "gpio224", "gpio225", "gpio226", "gpio227",
	"gpio228", "gpio229", "gpio230", "gpio231", "gpio232", "gpio233",
	"gpio234", "gpio235", "gpio236", "gpio237", "gpio238", "gpio239",
	"gpio240", "gpio241", "gpio242", "gpio243", "gpio244", "gpio245",
	"gpio246", "gpio247", "gpio248", "gpio249",
};

static const char *const resout_gpio_n_groups[] = {
	"gpio160",
};

static const char *const aoss_cti_groups[] = {
	"gpio60",
	"gpio61",
	"gpio62",
	"gpio63",
};

static const char *const asc_cci_groups[] = {
	"gpio235",
	"gpio236",
};

static const char *const atest_char_groups[] = {
	"gpio172", "gpio184", "gpio188", "gpio164",
	"gpio163",
};

static const char *const atest_usb_groups[] = {
	"gpio39", "gpio40", "gpio41", "gpio38",
	"gpio44", "gpio45", "gpio42", "gpio43",
	"gpio49", "gpio50", "gpio51", "gpio48",
	"gpio54", "gpio55", "gpio52", "gpio53",
	"gpio65", "gpio66", "gpio46", "gpio47",
	"gpio72", "gpio73", "gpio80", "gpio81",
};

static const char *const audio_ext_mclk0_groups[] = {
	"gpio134",
};

static const char *const audio_ext_mclk1_groups[] = {
	"gpio142",
};

static const char *const audio_ref_clk_groups[] = {
	"gpio142",
};

static const char *const cam_asc_mclk4_groups[] = {
	"gpio100",
};

static const char *const cam_mclk_groups[] = {
	"gpio96",
	"gpio97",
	"gpio98",
	"gpio99",
};

static const char *const cci_async_in_groups[] = {
	"gpio113", "gpio112", "gpio111",
};

static const char *const cci_i2c_scl_groups[] = {
	"gpio102", "gpio104", "gpio106",
};

static const char *const cci_i2c_sda_groups[] = {
	"gpio101", "gpio103", "gpio105",
};

static const char *const cci_timer_groups[] = {
	"gpio109", "gpio110", "gpio111", "gpio112",
	"gpio113",
};

static const char *const cmu_rng_groups[] = {
	"gpio48", "gpio47", "gpio46", "gpio45",
};

static const char *const cri_trng_groups[] = {
	"gpio173",
};

static const char *const dbg_out_clk_groups[] = {
	"gpio51",
};

static const char *const ddr_bist_complete_groups[] = {
	"gpio57",
};

static const char *const ddr_bist_fail_groups[] = {
	"gpio56",
};

static const char *const ddr_bist_start_groups[] = {
	"gpio54",
};

static const char *const ddr_bist_stop_groups[] = {
	"gpio55",
};

static const char *const ddr_pxi_groups[] = {
	"gpio38", "gpio39", "gpio40", "gpio41",
	"gpio72", "gpio73", "gpio80", "gpio81",
	"gpio42", "gpio43", "gpio44", "gpio45",
	"gpio46", "gpio47", "gpio48", "gpio49",
	"gpio50", "gpio51", "gpio52", "gpio53",
	"gpio54", "gpio55", "gpio65", "gpio66",
};

static const char *const edp0_hot_groups[] = {
	"gpio119",
};

static const char *const edp0_lcd_groups[] = {
	"gpio120",
};

static const char *const edp1_lcd_groups[] = {
	"gpio115",
	"gpio119",
};

static const char *const egpio_groups[] = {
	"gpio192", "gpio193", "gpio194", "gpio195", "gpio196", "gpio197",
	"gpio198", "gpio199", "gpio200", "gpio201", "gpio202", "gpio203",
	"gpio204", "gpio205", "gpio206", "gpio207", "gpio208", "gpio209",
	"gpio210", "gpio211", "gpio212", "gpio213", "gpio214", "gpio215",
	"gpio216", "gpio217", "gpio218", "gpio219", "gpio220", "gpio221",
	"gpio222", "gpio223", "gpio224", "gpio225", "gpio226", "gpio227",
	"gpio228", "gpio229", "gpio230", "gpio231", "gpio232", "gpio233",
	"gpio234", "gpio235", "gpio236", "gpio237", "gpio238", "gpio239",
	"gpio240", "gpio241", "gpio242", "gpio243", "gpio244",
};

static const char *const eusb_ac_en_groups[] = {
	"gpio168", "gpio177", "gpio186", "gpio69",
	"gpio187", "gpio178",
};

static const char *const gcc_gp1_groups[] = {
	"gpio71",
	"gpio72",
};

static const char *const gcc_gp2_groups[] = {
	"gpio64",
	"gpio73",
};

static const char *const gcc_gp3_groups[] = {
	"gpio74",
	"gpio82",
};

static const char *const host2wlan_sol_groups[] = {
	"gpio118",
};

static const char *const i2c0_s_scl_groups[] = {
	"gpio7",
};

static const char *const i2c0_s_sda_groups[] = {
	"gpio6",
};

static const char *const i2s0_data_groups[] = {
	"gpio136", "gpio137",
};

static const char *const i2s0_sck_groups[] = {
	"gpio135",
};

static const char *const i2s0_ws_groups[] = {
	"gpio138",
};

static const char *const i2s1_data_groups[] = {
	"gpio140", "gpio142",
};

static const char *const i2s1_sck_groups[] = {
	"gpio139",
};

static const char *const i2s1_ws_groups[] = {
	"gpio141",
};

static const char *const ibi_i3c_groups[] = {
	"gpio0",  "gpio1",  "gpio4",  "gpio5",	"gpio32", "gpio33",
	"gpio36", "gpio37", "gpio64", "gpio65", "gpio68", "gpio69",
};

static const char *const jitter_bist_groups[] = {
	"gpio52",
};

static const char *const mdp_vsync_out_groups[] = {
	"gpio114", "gpio114", "gpio115", "gpio115",
	"gpio109", "gpio110", "gpio111", "gpio112",
	"gpio113",
};

static const char *const mdp_vsync_e_groups[] = {
	"gpio106",
};

static const char *const mdp_vsync_p_groups[] = {
	"gpio98",
};

static const char *const mdp_vsync_s_groups[] = {
	"gpio105",
};

static const char *const pcie3a_clk_groups[] = {
	"gpio144",
};

static const char *const pcie3a_rst_n_groups[] = {
	"gpio143",
};

static const char *const pcie3b_clk_groups[] = {
	"gpio156",
};

static const char *const pcie4_clk_req_n_groups[] = {
	"gpio147",
};

static const char *const pcie5_clk_req_n_groups[] = {
	"gpio153",
};

static const char *const pcie6_clk_req_n_groups[] = {
	"gpio150",
};

static const char *const phase_flag_groups[] = {
	"gpio6",   "gpio7",   "gpio16",  "gpio17",
	"gpio18",  "gpio19",  "gpio20",  "gpio21",
	"gpio22",  "gpio23",  "gpio24",  "gpio25",
	"gpio8",   "gpio26",  "gpio27",  "gpio163",
	"gpio164", "gpio188", "gpio184", "gpio172",
	"gpio186", "gpio173", "gpio76",  "gpio9",
	"gpio77",  "gpio78",  "gpio10",  "gpio11",
	"gpio12",  "gpio13",  "gpio14",  "gpio15",
};

static const char *const pll_bist_sync_groups[] = {
	"gpio28",
};

static const char *const pll_clk_aux_groups[] = {
	"gpio35",
};

static const char *const pmc_oca_n_groups[] = {
	"gpio249",
};

static const char *const pmc_uva_n_groups[] = {
	"gpio248",
};

static const char *const prng_rosc_groups[] = {
	"gpio186", "gpio188", "gpio164", "gpio163",
};

static const char *const qdss_cti_groups[] = {
	"gpio18",  "gpio19",  "gpio23",	 "gpio27",
	"gpio161", "gpio162", "gpio215", "gpio217",
};

static const char *const qdss_gpio_groups[] = {
	"gpio104", "gpio151", "gpio227", "gpio228",
	"gpio96",  "gpio219", "gpio97",  "gpio220",
	"gpio108", "gpio231", "gpio109", "gpio232",
	"gpio110", "gpio233", "gpio111", "gpio234",
	"gpio112", "gpio235", "gpio113", "gpio236",
	"gpio149", "gpio221", "gpio99",  "gpio222",
	"gpio100", "gpio223", "gpio101", "gpio224",
	"gpio102", "gpio225", "gpio103", "gpio226",
	"gpio152", "gpio237", "gpio107", "gpio238",
};

static const char *const qspi0_groups[] = {
	"gpio127", "gpio132", "gpio133", "gpio128",
	"gpio129", "gpio130", "gpio131",
};

static const char *const qup0_se0_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};

static const char *const qup0_se1_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};

static const char *const qup0_se2_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
	"gpio17", "gpio18", "gpio19",
};

static const char *const qup0_se3_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15",
	"gpio21", "gpio22", "gpio23",
};

static const char *const qup0_se4_groups[] = {
	"gpio16", "gpio17", "gpio18", "gpio19",
};

static const char *const qup0_se5_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23",
};

static const char *const qup0_se6_groups[] = {
	"gpio6", "gpio7", "gpio4", "gpio5",
};

static const char *const qup0_se7_groups[] = {
	"gpio14", "gpio15", "gpio12", "gpio13",
};

static const char *const qup1_se0_groups[] = {
	"gpio32", "gpio33", "gpio34", "gpio35",
};

static const char *const qup1_se1_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};

static const char *const qup1_se2_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43",
	"gpio49", "gpio50", "gpio51",
};

static const char *const qup1_se3_groups[] = {
	"gpio44", "gpio45", "gpio46", "gpio47",
	"gpio33", "gpio34", "gpio35",
};

static const char *const qup1_se4_groups[] = {
	"gpio48", "gpio49", "gpio50", "gpio51",
};

static const char *const qup1_se5_groups[] = {
	"gpio52", "gpio53", "gpio54", "gpio55",
};

static const char *const qup1_se6_groups[] = {
	"gpio56", "gpio57", "gpio58", "gpio59",
};

static const char *const qup1_se7_groups[] = {
	"gpio54", "gpio55", "gpio52", "gpio53",
};

static const char *const qup2_se0_groups[] = {
	"gpio64", "gpio65", "gpio66", "gpio67",
};

static const char *const qup2_se1_groups[] = {
	"gpio68", "gpio69", "gpio70", "gpio71",
};

static const char *const qup2_se2_groups[] = {
	"gpio72", "gpio73", "gpio74", "gpio75",
	"gpio81", "gpio82", "gpio83",
};

static const char *const qup2_se3_groups[] = {
	"gpio76", "gpio77", "gpio78", "gpio79",
	"gpio65", "gpio66", "gpio67",
};

static const char *const qup2_se4_groups[] = {
	"gpio80", "gpio81", "gpio82", "gpio83",
};

static const char *const qup2_se5_groups[] = {
	"gpio84", "gpio85", "gpio86", "gpio87",
};

static const char *const qup2_se6_groups[] = {
	"gpio88", "gpio89", "gpio90", "gpio91",
};

static const char *const qup2_se7_groups[] = {
	"gpio80", "gpio81", "gpio82", "gpio83",
};

static const char *const qup3_se0_groups[] = {
	"gpio128", "gpio129", "gpio127", "gpio132",
	"gpio130", "gpio131", "gpio133", "gpio247",
};

static const char *const qup3_se1_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43",
	"gpio49", "gpio50", "gpio51", "gpio48",
};

static const char *const sd_write_protect_groups[] = {
	"gpio162",
};

static const char *const sdc4_clk_groups[] = {
	"gpio127",
};

static const char *const sdc4_cmd_groups[] = {
	"gpio132",
};

static const char *const sdc4_data_groups[] = {
	"gpio128",
	"gpio129",
	"gpio130",
	"gpio131",
};

static const char *const smb_acok_n_groups[] = {
	"gpio245",
};

static const char *const sys_throttle_groups[] = {
	"gpio39",
	"gpio94",
};

static const char *const tb_trig_sdc2_groups[] = {
	"gpio137",
};

static const char *const tb_trig_sdc4_groups[] = {
	"gpio133",
};

static const char *const tmess_prng_groups[] = {
	"gpio92", "gpio93", "gpio94", "gpio95",
};

static const char *const tsense_pwm_groups[] = {
	"gpio28", "gpio29", "gpio30", "gpio31",
	"gpio34", "gpio138", "gpio139", "gpio140",
};

static const char *const tsense_therm_groups[] = {
	"gpio141",
};

static const char *const usb0_dp_groups[] = {
	"gpio122",
};

static const char *const usb0_phy_ps_groups[] = {
	"gpio121",
};

static const char *const usb0_sbrx_groups[] = {
	"gpio163",
};

static const char *const usb0_sbtx_groups[] = {
	"gpio164",
	"gpio165",
};

static const char *const usb0_tmu_groups[] = {
	"gpio98",
};

static const char *const usb1_dbg_groups[] = {
	"gpio105",
	"gpio106",
};

static const char *const usb1_dp_groups[] = {
	"gpio124",
};

static const char *const usb1_phy_ps_groups[] = {
	"gpio123",
};

static const char *const usb1_sbrx_groups[] = {
	"gpio172",
};

static const char *const usb1_sbtx_groups[] = {
	"gpio173",
	"gpio174",
};

static const char *const usb1_tmu_groups[] = {
	"gpio98",
};

static const char *const usb2_dp_groups[] = {
	"gpio126",
};

static const char *const usb2_phy_ps_groups[] = {
	"gpio125",
};

static const char *const usb2_sbrx_groups[] = {
	"gpio181",
};

static const char *const usb2_sbtx_groups[] = {
	"gpio182",
	"gpio183",
};

static const char *const usb2_tmu_groups[] = {
	"gpio98",
};

static const char *const vsense_trigger_mirnat_groups[] = {
	"gpio38",
};

static const char *const wcn_sw_groups[] = {
	"gpio221",
};

static const char *const wcn_sw_ctrl_groups[] = {
	"gpio214",
};

static const struct pinfunction glymur_functions[] = {
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(resout_gpio_n),
	MSM_PIN_FUNCTION(aoss_cti),
	MSM_PIN_FUNCTION(asc_cci),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_usb),
	MSM_PIN_FUNCTION(audio_ext_mclk0),
	MSM_PIN_FUNCTION(audio_ext_mclk1),
	MSM_PIN_FUNCTION(audio_ref_clk),
	MSM_PIN_FUNCTION(cam_asc_mclk4),
	MSM_PIN_FUNCTION(cam_mclk),
	MSM_PIN_FUNCTION(cci_async_in),
	MSM_PIN_FUNCTION(cci_i2c_scl),
	MSM_PIN_FUNCTION(cci_i2c_sda),
	MSM_PIN_FUNCTION(cci_timer),
	MSM_PIN_FUNCTION(cmu_rng),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(dbg_out_clk),
	MSM_PIN_FUNCTION(ddr_bist_complete),
	MSM_PIN_FUNCTION(ddr_bist_fail),
	MSM_PIN_FUNCTION(ddr_bist_start),
	MSM_PIN_FUNCTION(ddr_bist_stop),
	MSM_PIN_FUNCTION(ddr_pxi),
	MSM_PIN_FUNCTION(edp0_hot),
	MSM_PIN_FUNCTION(edp0_lcd),
	MSM_PIN_FUNCTION(edp1_lcd),
	MSM_PIN_FUNCTION(egpio),
	MSM_PIN_FUNCTION(eusb_ac_en),
	MSM_PIN_FUNCTION(gcc_gp1),
	MSM_PIN_FUNCTION(gcc_gp2),
	MSM_PIN_FUNCTION(gcc_gp3),
	MSM_PIN_FUNCTION(host2wlan_sol),
	MSM_PIN_FUNCTION(i2c0_s_scl),
	MSM_PIN_FUNCTION(i2c0_s_sda),
	MSM_PIN_FUNCTION(i2s0_data),
	MSM_PIN_FUNCTION(i2s0_sck),
	MSM_PIN_FUNCTION(i2s0_ws),
	MSM_PIN_FUNCTION(i2s1_data),
	MSM_PIN_FUNCTION(i2s1_sck),
	MSM_PIN_FUNCTION(i2s1_ws),
	MSM_PIN_FUNCTION(ibi_i3c),
	MSM_PIN_FUNCTION(jitter_bist),
	MSM_PIN_FUNCTION(mdp_vsync_out),
	MSM_PIN_FUNCTION(mdp_vsync_e),
	MSM_PIN_FUNCTION(mdp_vsync_p),
	MSM_PIN_FUNCTION(mdp_vsync_s),
	MSM_PIN_FUNCTION(pcie3a_clk),
	MSM_PIN_FUNCTION(pcie3a_rst_n),
	MSM_PIN_FUNCTION(pcie3b_clk),
	MSM_PIN_FUNCTION(pcie4_clk_req_n),
	MSM_PIN_FUNCTION(pcie5_clk_req_n),
	MSM_PIN_FUNCTION(pcie6_clk_req_n),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(pll_bist_sync),
	MSM_PIN_FUNCTION(pll_clk_aux),
	MSM_PIN_FUNCTION(pmc_oca_n),
	MSM_PIN_FUNCTION(pmc_uva_n),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qdss_gpio),
	MSM_PIN_FUNCTION(qspi0),
	MSM_PIN_FUNCTION(qup0_se0),
	MSM_PIN_FUNCTION(qup0_se1),
	MSM_PIN_FUNCTION(qup0_se2),
	MSM_PIN_FUNCTION(qup0_se3),
	MSM_PIN_FUNCTION(qup0_se4),
	MSM_PIN_FUNCTION(qup0_se5),
	MSM_PIN_FUNCTION(qup0_se6),
	MSM_PIN_FUNCTION(qup0_se7),
	MSM_PIN_FUNCTION(qup1_se0),
	MSM_PIN_FUNCTION(qup1_se1),
	MSM_PIN_FUNCTION(qup1_se2),
	MSM_PIN_FUNCTION(qup1_se3),
	MSM_PIN_FUNCTION(qup1_se4),
	MSM_PIN_FUNCTION(qup1_se5),
	MSM_PIN_FUNCTION(qup1_se6),
	MSM_PIN_FUNCTION(qup1_se7),
	MSM_PIN_FUNCTION(qup2_se0),
	MSM_PIN_FUNCTION(qup2_se1),
	MSM_PIN_FUNCTION(qup2_se2),
	MSM_PIN_FUNCTION(qup2_se3),
	MSM_PIN_FUNCTION(qup2_se4),
	MSM_PIN_FUNCTION(qup2_se5),
	MSM_PIN_FUNCTION(qup2_se6),
	MSM_PIN_FUNCTION(qup2_se7),
	MSM_PIN_FUNCTION(qup3_se0),
	MSM_PIN_FUNCTION(qup3_se1),
	MSM_PIN_FUNCTION(sd_write_protect),
	MSM_PIN_FUNCTION(sdc4_clk),
	MSM_PIN_FUNCTION(sdc4_cmd),
	MSM_PIN_FUNCTION(sdc4_data),
	MSM_PIN_FUNCTION(smb_acok_n),
	MSM_PIN_FUNCTION(sys_throttle),
	MSM_PIN_FUNCTION(tb_trig_sdc2),
	MSM_PIN_FUNCTION(tb_trig_sdc4),
	MSM_PIN_FUNCTION(tmess_prng),
	MSM_PIN_FUNCTION(tsense_pwm),
	MSM_PIN_FUNCTION(tsense_therm),
	MSM_PIN_FUNCTION(usb0_dp),
	MSM_PIN_FUNCTION(usb0_phy_ps),
	MSM_PIN_FUNCTION(usb0_sbrx),
	MSM_PIN_FUNCTION(usb0_sbtx),
	MSM_PIN_FUNCTION(usb0_tmu),
	MSM_PIN_FUNCTION(usb1_dbg),
	MSM_PIN_FUNCTION(usb1_dp),
	MSM_PIN_FUNCTION(usb1_phy_ps),
	MSM_PIN_FUNCTION(usb1_sbrx),
	MSM_PIN_FUNCTION(usb1_sbtx),
	MSM_PIN_FUNCTION(usb1_tmu),
	MSM_PIN_FUNCTION(usb2_dp),
	MSM_PIN_FUNCTION(usb2_phy_ps),
	MSM_PIN_FUNCTION(usb2_sbrx),
	MSM_PIN_FUNCTION(usb2_sbtx),
	MSM_PIN_FUNCTION(usb2_tmu),
	MSM_PIN_FUNCTION(vsense_trigger_mirnat),
	MSM_PIN_FUNCTION(wcn_sw),
	MSM_PIN_FUNCTION(wcn_sw_ctrl),
};

static const struct msm_pingroup glymur_groups[] = {
	[0] = PINGROUP(0, qup0_se0, ibi_i3c, _, _, _, _, _, _, _, _, _),
	[1] = PINGROUP(1, qup0_se0, ibi_i3c, _, _, _, _, _, _, _, _, _),
	[2] = PINGROUP(2, qup0_se0, _, _, _, _, _, _, _, _, _, _),
	[3] = PINGROUP(3, qup0_se0, _, _, _, _, _, _, _, _, _, _),
	[4] = PINGROUP(4, qup0_se1, qup0_se6, ibi_i3c, _, _, _, _, _, _, _, _),
	[5] = PINGROUP(5, qup0_se1, qup0_se6, ibi_i3c, _, _, _, _, _, _, _, _),
	[6] = PINGROUP(6, qup0_se1, qup0_se6, i2c0_s_sda, phase_flag, _, _, _, _, _, _, _),
	[7] = PINGROUP(7, qup0_se1, qup0_se6, i2c0_s_scl, phase_flag, _, _, _, _, _, _, _),
	[8] = PINGROUP(8, qup0_se2, phase_flag, _, _, _, _, _, _, _, _, _),
	[9] = PINGROUP(9, qup0_se2, phase_flag, _, _, _, _, _, _, _, _, _),
	[10] = PINGROUP(10, qup0_se2, phase_flag, _, _, _, _, _, _, _, _, _),
	[11] = PINGROUP(11, qup0_se2, phase_flag, _, _, _, _, _, _, _, _, _),
	[12] = PINGROUP(12, qup0_se3, qup0_se7, phase_flag, _, _, _, _, _, _, _, _),
	[13] = PINGROUP(13, qup0_se3, qup0_se7, phase_flag, _, _, _, _, _, _, _, _),
	[14] = PINGROUP(14, qup0_se3, qup0_se7, phase_flag, _, _, _, _, _, _, _, _),
	[15] = PINGROUP(15, qup0_se3, qup0_se7, phase_flag, _, _, _, _, _, _, _, _),
	[16] = PINGROUP(16, qup0_se4, phase_flag, _, _, _, _, _, _, _, _, _),
	[17] = PINGROUP(17, qup0_se4, qup0_se2, phase_flag, _, _, _, _, _, _, _, _),
	[18] = PINGROUP(18, qup0_se4, qup0_se2, phase_flag, _, qdss_cti, _, _, _, _, _, _),
	[19] = PINGROUP(19, qup0_se4, qup0_se2, phase_flag, _, qdss_cti, _, _, _, _, _, _),
	[20] = PINGROUP(20, qup0_se5, _, phase_flag, _, _, _, _, _, _, _, _),
	[21] = PINGROUP(21, qup0_se5, qup0_se3, _, phase_flag, _, _, _, _, _, _, _),
	[22] = PINGROUP(22, qup0_se5, qup0_se3, _, phase_flag, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, qup0_se5, qup0_se3, phase_flag, _, qdss_cti, _, _, _, _, _, _),
	[24] = PINGROUP(24, phase_flag, _, _, _, _, _, _, _, _, _, _),
	[25] = PINGROUP(25, phase_flag, _, _, _, _, _, _, _, _, _, _),
	[26] = PINGROUP(26, phase_flag, _, _, _, _, _, _, _, _, _, _),
	[27] = PINGROUP(27, phase_flag, _, qdss_cti, _, _, _, _, _, _, _, _),
	[28] = PINGROUP(28, pll_bist_sync, tsense_pwm, _, _, _, _, _, _, _, _, _),
	[29] = PINGROUP(29, tsense_pwm, _, _, _, _, _, _, _, _, _, _),
	[30] = PINGROUP(30, tsense_pwm, _, _, _, _, _, _, _, _, _, _),
	[31] = PINGROUP(31, tsense_pwm, _, _, _, _, _, _, _, _, _, _),
	[32] = PINGROUP(32, qup1_se0, ibi_i3c, _, _, _, _, _, _, _, _, _),
	[33] = PINGROUP(33, qup1_se0, ibi_i3c, qup1_se3, _, _, _, _, _, _, _, _),
	[34] = PINGROUP(34, qup1_se0, qup1_se3, tsense_pwm, _, _, _, _, _, _, _, _),
	[35] = PINGROUP(35, qup1_se0, qup1_se3, pll_clk_aux, _, _, _, _, _, _, _, _),
	[36] = PINGROUP(36, qup1_se1, ibi_i3c, _, _, _, _, _, _, _, _, _),
	[37] = PINGROUP(37, qup1_se1, ibi_i3c, _, _, _, _, _, _, _, _, _),
	[38] = PINGROUP(38, qup1_se1, atest_usb, ddr_pxi, vsense_trigger_mirnat, _, _, _, _,
			_, _, _),
	[39] = PINGROUP(39, qup1_se1, sys_throttle, _, atest_usb, ddr_pxi, _, _, _, _, _, _),
	[40] = PINGROUP(40, qup1_se2, qup3_se1, _, atest_usb, ddr_pxi, _, _, _, _, _, _),
	[41] = PINGROUP(41, qup1_se2, qup3_se1, qup3_se0, atest_usb, ddr_pxi, _, _, _, _,
			_, _),
	[42] = PINGROUP(42, qup1_se2, qup3_se1, qup0_se1, atest_usb, ddr_pxi, _, _, _, _,
			_, _),
	[43] = PINGROUP(43, qup1_se2, qup3_se1, _, atest_usb, ddr_pxi, _, _, _, _, _, _),
	[44] = PINGROUP(44, qup1_se3, _, atest_usb, ddr_pxi, _, _, _, _, _, _, _),
	[45] = PINGROUP(45, qup1_se3, cmu_rng, _, atest_usb, ddr_pxi, _, _, _, _, _, _),
	[46] = PINGROUP(46, qup1_se3, cmu_rng, _, atest_usb, ddr_pxi, _, _, _, _, _, _),
	[47] = PINGROUP(47, qup1_se3, cmu_rng, _, atest_usb, ddr_pxi, _, _, _, _, _, _),
	[48] = PINGROUP(48, qup1_se4, qup3_se1, cmu_rng, _, atest_usb, ddr_pxi, _, _, _,
			_, _),
	[49] = PINGROUP(49, qup1_se4, qup1_se2, qup3_se1, _, atest_usb, ddr_pxi, _, _,
			_, _, _),
	[50] = PINGROUP(50, qup1_se4, qup1_se2, qup3_se1, _, atest_usb, ddr_pxi, _, _,
			_, _, _),
	[51] = PINGROUP(51, qup1_se4, qup1_se2, qup3_se1, dbg_out_clk, atest_usb,
			ddr_pxi, _, _, _, _, _),
	[52] = PINGROUP(52, qup1_se5, qup1_se7, jitter_bist, atest_usb, ddr_pxi, _, _, _,
			_, _, _),
	[53] = PINGROUP(53, qup1_se5, qup1_se7, _, atest_usb, ddr_pxi, _, _, _, _, _, _),
	[54] = PINGROUP(54, qup1_se5, qup1_se7, ddr_bist_start, atest_usb, ddr_pxi, _, _,
			_, _, _, _),
	[55] = PINGROUP(55, qup1_se5, qup1_se7, ddr_bist_stop, atest_usb, ddr_pxi, _, _,
			_, _, _, _),
	[56] = PINGROUP(56, qup1_se6, ddr_bist_fail, _, _, _, _, _, _, _, _, _),
	[57] = PINGROUP(57, qup1_se6, ddr_bist_complete, _, _, _, _, _, _, _, _, _),
	[58] = PINGROUP(58, qup1_se6, _, _, _, _, _, _, _, _, _, _),
	[59] = PINGROUP(59, qup1_se6, _, _, _, _, _, _, _, _, _, _),
	[60] = PINGROUP(60, aoss_cti, _, _, _, _, _, _, _, _, _, _),
	[61] = PINGROUP(61, aoss_cti, _, _, _, _, _, _, _, _, _, _),
	[62] = PINGROUP(62, aoss_cti, _, _, _, _, _, _, _, _, _, _),
	[63] = PINGROUP(63, aoss_cti, _, _, _, _, _, _, _, _, _, _),
	[64] = PINGROUP(64, qup2_se0, ibi_i3c, gcc_gp2, _, _, _, _, _, _, _, _),
	[65] = PINGROUP(65, qup2_se0, qup2_se3, ibi_i3c, atest_usb, ddr_pxi, _, _, _, _,
			_, _),
	[66] = PINGROUP(66, qup2_se0, qup2_se3, atest_usb, ddr_pxi, _, _, _, _, _, _, _),
	[67] = PINGROUP(67, qup2_se0, qup2_se3, _, _, _, _, _, _, _, _, _),
	[68] = PINGROUP(68, qup2_se1, ibi_i3c, _, _, _, _, _, _, _, _, _),
	[69] = PINGROUP(69, qup2_se1, ibi_i3c, _, _, _, _, _, _, _, _, _),
	[70] = PINGROUP(70, qup2_se1, _, _, _, _, _, _, _, _, _, _),
	[71] = PINGROUP(71, qup2_se1, gcc_gp1, _, _, _, _, _, _, _, _, _),
	[72] = PINGROUP(72, qup2_se2, gcc_gp1, atest_usb, ddr_pxi, _, _, _, _, _, _, _),
	[73] = PINGROUP(73, qup2_se2, gcc_gp2, atest_usb, ddr_pxi, _, _, _, _, _, _, _),
	[74] = PINGROUP(74, qup2_se2, gcc_gp3, _, _, _, _, _, _, _, _, _),
	[75] = PINGROUP(75, qup2_se2, _, _, _, _, _, _, _, _, _, _),
	[76] = PINGROUP(76, qup2_se3, phase_flag, _, _, _, _, _, _, _, _, _),
	[77] = PINGROUP(77, qup2_se3, phase_flag, _, _, _, _, _, _, _, _, _),
	[78] = PINGROUP(78, qup2_se3, phase_flag, _, _, _, _, _, _, _, _, _),
	[79] = PINGROUP(79, qup2_se3, _, _, _, _, _, _, _, _, _, _),
	[80] = PINGROUP(80, qup2_se4, qup2_se7, atest_usb, ddr_pxi, _, _, _, _, _, _, _),
	[81] = PINGROUP(81, qup2_se4, qup2_se2, qup2_se7, atest_usb, ddr_pxi, _, _, _,
			_, _, _),
	[82] = PINGROUP(82, qup2_se4, qup2_se2, qup2_se7, gcc_gp3, _, _, _, _, _, _, _),
	[83] = PINGROUP(83, qup2_se4, qup2_se2, qup2_se7, _, _, _, _, _, _, _, _),
	[84] = PINGROUP(84, qup2_se5, _, _, _, _, _, _, _, _, _, _),
	[85] = PINGROUP(85, qup2_se5, _, _, _, _, _, _, _, _, _, _),
	[86] = PINGROUP(86, qup2_se5, _, _, _, _, _, _, _, _, _, _),
	[87] = PINGROUP(87, qup2_se5, _, _, _, _, _, _, _, _, _, _),
	[88] = PINGROUP(88, qup2_se6, _, _, _, _, _, _, _, _, _, _),
	[89] = PINGROUP(89, qup2_se6, _, _, _, _, _, _, _, _, _, _),
	[90] = PINGROUP(90, qup2_se6, _, _, _, _, _, _, _, _, _, _),
	[91] = PINGROUP(91, qup2_se6, _, _, _, _, _, _, _, _, _, _),
	[92] = PINGROUP(92, tmess_prng, _, _, _, _, _, _, _, _, _, _),
	[93] = PINGROUP(93, tmess_prng, _, _, _, _, _, _, _, _, _, _),
	[94] = PINGROUP(94, sys_throttle, tmess_prng, _, _, _, _, _, _, _, _, _),
	[95] = PINGROUP(95, tmess_prng, _, _, _, _, _, _, _, _, _, _),
	[96] = PINGROUP(96, cam_mclk, qdss_gpio, _, _, _, _, _, _, _, _, _),
	[97] = PINGROUP(97, cam_mclk, qdss_gpio, _, _, _, _, _, _, _, _, _),
	[98] = PINGROUP(98, cam_mclk, mdp_vsync_p, usb0_tmu, usb1_tmu, usb2_tmu, _, _, _, _, _, _),
	[99] = PINGROUP(99, cam_mclk, qdss_gpio, _, _, _, _, _, _, _, _, _),
	[100] = PINGROUP(100, cam_asc_mclk4, qdss_gpio, _, _, _, _, _, _, _, _, _),
	[101] = PINGROUP(101, cci_i2c_sda, qdss_gpio, _, _, _, _, _, _, _, _, _),
	[102] = PINGROUP(102, cci_i2c_scl, qdss_gpio, _, _, _, _, _, _, _, _, _),
	[103] = PINGROUP(103, cci_i2c_sda, qdss_gpio, _, _, _, _, _, _, _, _, _),
	[104] = PINGROUP(104, cci_i2c_scl, qdss_gpio, _, _, _, _, _, _, _, _, _),
	[105] = PINGROUP(105, cci_i2c_sda, mdp_vsync_s, usb1_dbg, _, _, _, _, _, _, _, _),
	[106] = PINGROUP(106, cci_i2c_scl, mdp_vsync_e, usb1_dbg, _, _, _, _, _, _, _, _),
	[107] = PINGROUP(107, qdss_gpio, _, _, _, _, _, _, _, _, _, _),
	[108] = PINGROUP(108, qdss_gpio, _, _, _, _, _, _, _, _, _, _),
	[109] = PINGROUP(109, cci_timer, mdp_vsync_out, qdss_gpio, _, _, _, _, _, _, _, _),
	[110] = PINGROUP(110, cci_timer, mdp_vsync_out, qdss_gpio, _, _, _, _, _, _, _, _),
	[111] = PINGROUP(111, cci_timer, cci_async_in, mdp_vsync_out, qdss_gpio, _, _, _, _,
			_, _, _),
	[112] = PINGROUP(112, cci_timer, cci_async_in, mdp_vsync_out, qdss_gpio, _, _, _, _,
			_, _, _),
	[113] = PINGROUP(113, cci_timer, cci_async_in, mdp_vsync_out, qdss_gpio, _, _, _, _,
			_, _, _),
	[114] = PINGROUP(114, mdp_vsync_out, mdp_vsync_out, _, _, _, _, _, _, _, _, _),
	[115] = PINGROUP(115, mdp_vsync_out, mdp_vsync_out, edp1_lcd, _, _, _, _, _, _, _, _),
	[116] = PINGROUP(116, _, _, _, _, _, _, _, _, _, _, _),
	[117] = PINGROUP(117, _, _, _, _, _, _, _, _, _, _, _),
	[118] = PINGROUP(118, host2wlan_sol, _, _, _, _, _, _, _, _, _, _),
	[119] = PINGROUP(119, edp0_hot, edp1_lcd, _, _, _, _, _, _, _, _, _),
	[120] = PINGROUP(120, edp0_lcd, _, _, _, _, _, _, _, _, _, _),
	[121] = PINGROUP(121, usb0_phy_ps, _, _, _, _, _, _, _, _, _, _),
	[122] = PINGROUP(122, usb0_dp, _, _, _, _, _, _, _, _, _, _),
	[123] = PINGROUP(123, usb1_phy_ps, _, _, _, _, _, _, _, _, _, _),
	[124] = PINGROUP(124, usb1_dp, _, _, _, _, _, _, _, _, _, _),
	[125] = PINGROUP(125, usb2_phy_ps, _, _, _, _, _, _, _, _, _, _),
	[126] = PINGROUP(126, usb2_dp, _, _, _, _, _, _, _, _, _, _),
	[127] = PINGROUP(127, qspi0, sdc4_clk, qup3_se0, _, _, _, _, _, _, _, _),
	[128] = PINGROUP(128, qspi0, sdc4_data, qup3_se0, _, _, _, _, _, _, _, _),
	[129] = PINGROUP(129, qspi0, sdc4_data, qup3_se0, _, _, _, _, _, _, _, _),
	[130] = PINGROUP(130, qspi0, sdc4_data, qup3_se0, _, _, _, _, _, _, _, _),
	[131] = PINGROUP(131, qspi0, sdc4_data, qup3_se0, _, _, _, _, _, _, _, _),
	[132] = PINGROUP(132, qspi0, sdc4_cmd, qup3_se0, _, _, _, _, _, _, _, _),
	[133] = PINGROUP(133, qspi0, tb_trig_sdc4, qup3_se0, _, _, _, _, _, _, _, _),
	[134] = PINGROUP(134, audio_ext_mclk0, _, _, _, _, _, _, _, _, _, _),
	[135] = PINGROUP(135, i2s0_sck, _, _, _, _, _, _, _, _, _, _),
	[136] = PINGROUP(136, i2s0_data, _, _, _, _, _, _, _, _, _, _),
	[137] = PINGROUP(137, i2s0_data, tb_trig_sdc2, _, _, _, _, _, _, _, _, _),
	[138] = PINGROUP(138, i2s0_ws, tsense_pwm, _, _, _, _, _, _, _, _, _),
	[139] = PINGROUP(139, i2s1_sck, tsense_pwm, _, _, _, _, _, _, _, _, _),
	[140] = PINGROUP(140, i2s1_data, tsense_pwm, _, _, _, _, _, _, _, _, _),
	[141] = PINGROUP(141, i2s1_ws, tsense_therm, _, _, _, _, _, _, _, _, _),
	[142] = PINGROUP(142, i2s1_data, audio_ext_mclk1, audio_ref_clk, _, _, _, _, _, _, _, _),
	[143] = PINGROUP(143, pcie3a_rst_n, _, _, _, _, _, _, _, _, _, _),
	[144] = PINGROUP(144, pcie3a_clk, _, _, _, _, _, _, _, _, _, _),
	[145] = PINGROUP(145, _, _, _, _, _, _, _, _, _, _, _),
	[146] = PINGROUP(146, _, _, _, _, _, _, _, _, _, _, _),
	[147] = PINGROUP(147, pcie4_clk_req_n, _, _, _, _, _, _, _, _, _, _),
	[148] = PINGROUP(148, _, _, _, _, _, _, _, _, _, _, _),
	[149] = PINGROUP(149, qdss_gpio, _, _, _, _, _, _, _, _, _, _),
	[150] = PINGROUP(150, pcie6_clk_req_n, _, _, _, _, _, _, _, _, _, _),
	[151] = PINGROUP(151, qdss_gpio, _, _, _, _, _, _, _, _, _, _),
	[152] = PINGROUP(152, qdss_gpio, _, _, _, _, _, _, _, _, _, _),
	[153] = PINGROUP(153, pcie5_clk_req_n, _, _, _, _, _, _, _, _, _, _),
	[154] = PINGROUP(154, _, _, _, _, _, _, _, _, _, _, _),
	[155] = PINGROUP(155, _, _, _, _, _, _, _, _, _, _, _),
	[156] = PINGROUP(156, pcie3b_clk, _, _, _, _, _, _, _, _, _, _),
	[157] = PINGROUP(157, _, _, _, _, _, _, _, _, _, _, _),
	[158] = PINGROUP(158, _, _, _, _, _, _, _, _, _, _, _),
	[159] = PINGROUP(159, _, _, _, _, _, _, _, _, _, _, _),
	[160] = PINGROUP(160, resout_gpio_n, _, _, _, _, _, _, _, _, _, _),
	[161] = PINGROUP(161, qdss_cti, _, _, _, _, _, _, _, _, _, _),
	[162] = PINGROUP(162, sd_write_protect, qdss_cti, _, _, _, _, _, _, _, _, _),
	[163] = PINGROUP(163, usb0_sbrx, prng_rosc, phase_flag, _, atest_char, _, _, _,
			_, _, _),
	[164] = PINGROUP(164, usb0_sbtx, prng_rosc, phase_flag, _, atest_char, _, _, _, _, _,
			_),
	[165] = PINGROUP(165, usb0_sbtx, _, _, _, _, _, _, _, _, _, _),
	[166] = PINGROUP(166, _, _, _, _, _, _, _, _, _, _, _),
	[167] = PINGROUP(167, _, _, _, _, _, _, _, _, _, _, _),
	[168] = PINGROUP(168, eusb_ac_en, _, _, _, _, _, _, _, _, _, _),
	[169] = PINGROUP(169, eusb_ac_en, _, _, _, _, _, _, _, _, _, _),
	[170] = PINGROUP(170, _, _, _, _, _, _, _, _, _, _, _),
	[171] = PINGROUP(171, _, _, _, _, _, _, _, _, _, _, _),
	[172] = PINGROUP(172, usb1_sbrx, phase_flag, _, atest_char, _, _, _, _, _, _, _),
	[173] = PINGROUP(173, usb1_sbtx, cri_trng, phase_flag, _, _, _, _, _, _, _, _),
	[174] = PINGROUP(174, usb1_sbtx, _, _, _, _, _, _, _, _, _, _),
	[175] = PINGROUP(175, _, _, _, _, _, _, _, _, _, _, _),
	[176] = PINGROUP(176, _, _, _, _, _, _, _, _, _, _, _),
	[177] = PINGROUP(177, eusb_ac_en, _, _, _, _, _, _, _, _, _, _),
	[178] = PINGROUP(178, eusb_ac_en, _, _, _, _, _, _, _, _, _, _),
	[179] = PINGROUP(179, _, _, _, _, _, _, _, _, _, _, _),
	[180] = PINGROUP(180, _, _, _, _, _, _, _, _, _, _, _),
	[181] = PINGROUP(181, usb2_sbrx, _, _, _, _, _, _, _, _, _, _),
	[182] = PINGROUP(182, usb2_sbtx, _, _, _, _, _, _, _, _, _, _),
	[183] = PINGROUP(183, usb2_sbtx, _, _, _, _, _, _, _, _, _, _),
	[184] = PINGROUP(184, phase_flag, _, atest_char, _, _, _, _, _, _, _, _),
	[185] = PINGROUP(185, _, _, _, _, _, _, _, _, _, _, _),
	[186] = PINGROUP(186, eusb_ac_en, prng_rosc, phase_flag, _, _, _, _, _, _, _, _),
	[187] = PINGROUP(187, eusb_ac_en, _, _, _, _, _, _, _, _, _, _),
	[188] = PINGROUP(188, prng_rosc, phase_flag, _, atest_char, _, _, _, _, _, _, _),
	[189] = PINGROUP(189, _, _, _, _, _, _, _, _, _, _, _),
	[190] = PINGROUP(190, _, _, _, _, _, _, _, _, _, _, _),
	[191] = PINGROUP(191, _, _, _, _, _, _, _, _, _, _, _),
	[192] = PINGROUP(192, _, _, _, _, _, _, _, _, _, _, egpio),
	[193] = PINGROUP(193, _, _, _, _, _, _, _, _, _, _, egpio),
	[194] = PINGROUP(194, _, _, _, _, _, _, _, _, _, _, egpio),
	[195] = PINGROUP(195, _, _, _, _, _, _, _, _, _, _, egpio),
	[196] = PINGROUP(196, _, _, _, _, _, _, _, _, _, _, egpio),
	[197] = PINGROUP(197, _, _, _, _, _, _, _, _, _, _, egpio),
	[198] = PINGROUP(198, _, _, _, _, _, _, _, _, _, _, egpio),
	[199] = PINGROUP(199, _, _, _, _, _, _, _, _, _, _, egpio),
	[200] = PINGROUP(200, _, _, _, _, _, _, _, _, _, _, egpio),
	[201] = PINGROUP(201, _, _, _, _, _, _, _, _, _, _, egpio),
	[202] = PINGROUP(202, _, _, _, _, _, _, _, _, _, _, egpio),
	[203] = PINGROUP(203, _, _, _, _, _, _, _, _, _, _, egpio),
	[204] = PINGROUP(204, _, _, _, _, _, _, _, _, _, _, egpio),
	[205] = PINGROUP(205, _, _, _, _, _, _, _, _, _, _, egpio),
	[206] = PINGROUP(206, _, _, _, _, _, _, _, _, _, _, egpio),
	[207] = PINGROUP(207, _, _, _, _, _, _, _, _, _, _, egpio),
	[208] = PINGROUP(208, _, _, _, _, _, _, _, _, _, _, egpio),
	[209] = PINGROUP(209, _, _, _, _, _, _, _, _, _, _, egpio),
	[210] = PINGROUP(210, _, _, _, _, _, _, _, _, _, _, egpio),
	[211] = PINGROUP(211, _, _, _, _, _, _, _, _, _, _, egpio),
	[212] = PINGROUP(212, _, _, _, _, _, _, _, _, _, _, egpio),
	[213] = PINGROUP(213, _, _, _, _, _, _, _, _, _, _, egpio),
	[214] = PINGROUP(214, wcn_sw_ctrl, _, _, _, _, _, _, _, _, _, egpio),
	[215] = PINGROUP(215, _, qdss_cti, _, _, _, _, _, _, _, _, egpio),
	[216] = PINGROUP(216, _, _, _, _, _, _, _, _, _, _, egpio),
	[217] = PINGROUP(217, _, qdss_cti, _, _, _, _, _, _, _, _, egpio),
	[218] = PINGROUP(218, _, _, _, _, _, _, _, _, _, _, egpio),
	[219] = PINGROUP(219, _, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[220] = PINGROUP(220, _, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[221] = PINGROUP(221, wcn_sw, _, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[222] = PINGROUP(222, _, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[223] = PINGROUP(223, _, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[224] = PINGROUP(224, _, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[225] = PINGROUP(225, _, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[226] = PINGROUP(226, _, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[227] = PINGROUP(227, _, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[228] = PINGROUP(228, _, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[229] = PINGROUP(229, _, _, _, _, _, _, _, _, _, _, egpio),
	[230] = PINGROUP(230, _, _, _, _, _, _, _, _, _, _, egpio),
	[231] = PINGROUP(231, qdss_gpio, _, _, _, _, _, _, _, _, _, egpio),
	[232] = PINGROUP(232, qdss_gpio, _, _, _, _, _, _, _, _, _, egpio),
	[233] = PINGROUP(233, qdss_gpio, _, _, _, _, _, _, _, _, _, egpio),
	[234] = PINGROUP(234, qdss_gpio, _, _, _, _, _, _, _, _, _, egpio),
	[235] = PINGROUP(235, asc_cci, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[236] = PINGROUP(236, asc_cci, qdss_gpio, _, _, _, _, _, _, _, _, egpio),
	[237] = PINGROUP(237, qdss_gpio, _, _, _, _, _, _, _, _, _, egpio),
	[238] = PINGROUP(238, qdss_gpio, _, _, _, _, _, _, _, _, _, egpio),
	[239] = PINGROUP(239, _, _, _, _, _, _, _, _, _, _, egpio),
	[240] = PINGROUP(240, _, _, _, _, _, _, _, _, _, _, egpio),
	[241] = PINGROUP(241, _, _, _, _, _, _, _, _, _, _, egpio),
	[242] = PINGROUP(242, _, _, _, _, _, _, _, _, _, _, egpio),
	[243] = PINGROUP(243, _, _, _, _, _, _, _, _, _, _, egpio),
	[244] = PINGROUP(244, _, _, _, _, _, _, _, _, _, _, egpio),
	[245] = PINGROUP(245, smb_acok_n, _, _, _, _, _, _, _, _, _, _),
	[246] = PINGROUP(246, _, _, _, _, _, _, _, _, _, _, _),
	[247] = PINGROUP(247, qup3_se0, _, _, _, _, _, _, _, _, _, _),
	[248] = PINGROUP(248, pmc_uva_n, _, _, _, _, _, _, _, _, _, _),
	[249] = PINGROUP(249, pmc_oca_n, _, _, _, _, _, _, _, _, _, _),
	[250] = UFS_RESET(ufs_reset, 0x104004, 0x105000),
	[251] = SDC_QDSD_PINGROUP(sdc2_clk, 0xff000, 14, 6),
	[252] = SDC_QDSD_PINGROUP(sdc2_cmd, 0xff000, 11, 3),
	[253] = SDC_QDSD_PINGROUP(sdc2_data, 0xff000, 9, 0),
};

static const struct msm_gpio_wakeirq_map glymur_pdc_map[] = {
	{ 0, 116 },   { 2, 114 },   { 3, 115 },	  { 4, 175 },	{ 5, 176 },
	{ 7, 111 },   { 11, 129 },  { 13, 130 },  { 15, 112 },	{ 19, 113 },
	{ 23, 187 },  { 27, 188 },  { 28, 121 },  { 29, 122 },	{ 30, 136 },
	{ 31, 203 },  { 32, 189 },  { 34, 174 },  { 35, 190 },	{ 36, 191 },
	{ 39, 124 },  { 43, 192 },  { 47, 193 },  { 51, 123 },	{ 53, 133 },
	{ 55, 125 },  { 59, 131 },  { 64, 134 },  { 65, 150 },	{ 66, 186 },
	{ 67, 132 },  { 68, 195 },  { 71, 135 },  { 75, 196 },	{ 79, 197 },
	{ 83, 198 },  { 84, 181 },  { 85, 199 },  { 87, 200 },	{ 91, 201 },
	{ 92, 182 },  { 93, 183 },  { 94, 184 },  { 95, 185 },	{ 98, 202 },
	{ 105, 157 }, { 113, 128 }, { 121, 117 }, { 123, 118 }, { 125, 119 },
	{ 129, 120 }, { 131, 126 }, { 132, 160 }, { 133, 194 }, { 134, 127 },
	{ 141, 137 }, { 143, 159 }, { 144, 138 }, { 145, 139 }, { 147, 140 },
	{ 148, 141 }, { 150, 146 }, { 151, 147 }, { 153, 148 }, { 154, 144 },
	{ 156, 149 }, { 157, 151 }, { 163, 142 }, { 172, 143 }, { 181, 145 },
	{ 193, 161 }, { 196, 152 }, { 203, 177 }, { 208, 178 }, { 215, 162 },
	{ 217, 153 }, { 220, 154 }, { 221, 155 }, { 228, 179 }, { 230, 180 },
	{ 232, 206 }, { 234, 172 }, { 235, 173 }, { 242, 158 }, { 244, 156 },
};

static const struct msm_pinctrl_soc_data glymur_tlmm = {
	.pins = glymur_pins,
	.npins = ARRAY_SIZE(glymur_pins),
	.functions = glymur_functions,
	.nfunctions = ARRAY_SIZE(glymur_functions),
	.groups = glymur_groups,
	.ngroups = ARRAY_SIZE(glymur_groups),
	.ngpios = 251,
	.wakeirq_map = glymur_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(glymur_pdc_map),
	.egpio_func = 11,
};

static const struct of_device_id glymur_tlmm_of_match[] = {
	{ .compatible = "qcom,glymur-tlmm", .data = &glymur_tlmm },
	{ }
};

static int glymur_tlmm_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &glymur_tlmm);
}

static struct platform_driver glymur_tlmm_driver = {
	.driver = {
		.name = "glymur-tlmm",
		.of_match_table = glymur_tlmm_of_match,
	},
	.probe = glymur_tlmm_probe,
};

static int __init glymur_tlmm_init(void)
{
	return platform_driver_register(&glymur_tlmm_driver);
}
arch_initcall(glymur_tlmm_init);

static void __exit glymur_tlmm_exit(void)
{
	platform_driver_unregister(&glymur_tlmm_driver);
}
module_exit(glymur_tlmm_exit);

MODULE_DESCRIPTION("QTI GLYMUR TLMM driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, glymur_tlmm_of_match);
