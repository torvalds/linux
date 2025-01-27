// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Ltd.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7)	\
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
		},					\
		.nfuncs = 8,				\
		.ctl_reg = REG_SIZE * id,		\
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
		.egpio_enable = 12,		\
		.egpio_present = 11,		\
		.intr_enable_bit = 0,		\
		.intr_status_bit = 0,		\
		.intr_target_bit = 5,		\
		.intr_target_kpss_val = 3,	\
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
	{						\
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
static const struct pinctrl_pin_desc sc8280xp_pins[] = {
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
	PINCTRL_PIN(228, "UFS_RESET"),
	PINCTRL_PIN(229, "UFS1_RESET"),
	PINCTRL_PIN(230, "SDC2_CLK"),
	PINCTRL_PIN(231, "SDC2_CMD"),
	PINCTRL_PIN(232, "SDC2_DATA"),
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

static const unsigned int ufs_reset_pins[] = { 228 };
static const unsigned int ufs1_reset_pins[] = { 229 };
static const unsigned int sdc2_clk_pins[] = { 230 };
static const unsigned int sdc2_cmd_pins[] = { 231 };
static const unsigned int sdc2_data_pins[] = { 232 };

enum sc8280xp_functions {
	msm_mux_atest_char,
	msm_mux_atest_usb,
	msm_mux_audio_ref,
	msm_mux_cam_mclk,
	msm_mux_cci_async,
	msm_mux_cci_i2c,
	msm_mux_cci_timer0,
	msm_mux_cci_timer1,
	msm_mux_cci_timer2,
	msm_mux_cci_timer3,
	msm_mux_cci_timer4,
	msm_mux_cci_timer5,
	msm_mux_cci_timer6,
	msm_mux_cci_timer7,
	msm_mux_cci_timer8,
	msm_mux_cci_timer9,
	msm_mux_cmu_rng,
	msm_mux_cri_trng,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_dbg_out,
	msm_mux_ddr_bist,
	msm_mux_ddr_pxi0,
	msm_mux_ddr_pxi1,
	msm_mux_ddr_pxi2,
	msm_mux_ddr_pxi3,
	msm_mux_ddr_pxi4,
	msm_mux_ddr_pxi5,
	msm_mux_ddr_pxi6,
	msm_mux_ddr_pxi7,
	msm_mux_dp2_hot,
	msm_mux_dp3_hot,
	msm_mux_edp0_lcd,
	msm_mux_edp1_lcd,
	msm_mux_edp2_lcd,
	msm_mux_edp3_lcd,
	msm_mux_edp_hot,
	msm_mux_egpio,
	msm_mux_emac0_dll,
	msm_mux_emac0_mcg0,
	msm_mux_emac0_mcg1,
	msm_mux_emac0_mcg2,
	msm_mux_emac0_mcg3,
	msm_mux_emac0_phy,
	msm_mux_emac0_ptp,
	msm_mux_emac1_dll0,
	msm_mux_emac1_dll1,
	msm_mux_emac1_mcg0,
	msm_mux_emac1_mcg1,
	msm_mux_emac1_mcg2,
	msm_mux_emac1_mcg3,
	msm_mux_emac1_phy,
	msm_mux_emac1_ptp,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_gcc_gp4,
	msm_mux_gcc_gp5,
	msm_mux_gpio,
	msm_mux_hs1_mi2s,
	msm_mux_hs2_mi2s,
	msm_mux_hs3_mi2s,
	msm_mux_ibi_i3c,
	msm_mux_jitter_bist,
	msm_mux_lpass_slimbus,
	msm_mux_mdp0_vsync0,
	msm_mux_mdp0_vsync1,
	msm_mux_mdp0_vsync2,
	msm_mux_mdp0_vsync3,
	msm_mux_mdp0_vsync4,
	msm_mux_mdp0_vsync5,
	msm_mux_mdp0_vsync6,
	msm_mux_mdp0_vsync7,
	msm_mux_mdp0_vsync8,
	msm_mux_mdp1_vsync0,
	msm_mux_mdp1_vsync1,
	msm_mux_mdp1_vsync2,
	msm_mux_mdp1_vsync3,
	msm_mux_mdp1_vsync4,
	msm_mux_mdp1_vsync5,
	msm_mux_mdp1_vsync6,
	msm_mux_mdp1_vsync7,
	msm_mux_mdp1_vsync8,
	msm_mux_mdp_vsync,
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
	msm_mux_mi2s_mclk1,
	msm_mux_mi2s_mclk2,
	msm_mux_pcie2a_clkreq,
	msm_mux_pcie2b_clkreq,
	msm_mux_pcie3a_clkreq,
	msm_mux_pcie3b_clkreq,
	msm_mux_pcie4_clkreq,
	msm_mux_phase_flag,
	msm_mux_pll_bist,
	msm_mux_pll_clk,
	msm_mux_prng_rosc0,
	msm_mux_prng_rosc1,
	msm_mux_prng_rosc2,
	msm_mux_prng_rosc3,
	msm_mux_qdss_cti,
	msm_mux_qdss_gpio,
	msm_mux_qspi,
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
	msm_mux_qup20,
	msm_mux_qup21,
	msm_mux_qup22,
	msm_mux_qup23,
	msm_mux_qup3,
	msm_mux_qup4,
	msm_mux_qup5,
	msm_mux_qup6,
	msm_mux_qup7,
	msm_mux_qup8,
	msm_mux_qup9,
	msm_mux_rgmii_0,
	msm_mux_rgmii_1,
	msm_mux_sd_write,
	msm_mux_sdc40,
	msm_mux_sdc42,
	msm_mux_sdc43,
	msm_mux_sdc4_clk,
	msm_mux_sdc4_cmd,
	msm_mux_tb_trig,
	msm_mux_tgu,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_tsense_pwm3,
	msm_mux_tsense_pwm4,
	msm_mux_usb0_dp,
	msm_mux_usb0_phy,
	msm_mux_usb0_sbrx,
	msm_mux_usb0_sbtx,
	msm_mux_usb0_usb4,
	msm_mux_usb1_dp,
	msm_mux_usb1_phy,
	msm_mux_usb1_sbrx,
	msm_mux_usb1_sbtx,
	msm_mux_usb1_usb4,
	msm_mux_usb2phy_ac,
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
	"gpio71", "gpio72", "gpio73", "gpio74", "gpio75", "gpio76", "gpio78",
	"gpio79", "gpio80", "gpio81", "gpio82", "gpio83", "gpio84", "gpio85",
	"gpio86", "gpio87", "gpio88", "gpio89", "gpio90", "gpio91", "gpio92",
	"gpio93", "gpio94", "gpio95", "gpio96", "gpio97", "gpio98", "gpio99",
	"gpio100", "gpio101", "gpio102", "gpio103", "gpio104", "gpio105",
	"gpio106", "gpio107", "gpio108", "gpio109", "gpio110", "gpio111",
	"gpio112", "gpio113", "gpio114", "gpio115", "gpio116", "gpio117",
	"gpio118", "gpio119", "gpio120", "gpio121", "gpio122", "gpio123",
	"gpio124", "gpio125", "gpio126", "gpio127", "gpio128", "gpio129",
	"gpio130", "gpio131", "gpio132", "gpio133", "gpio134", "gpio135",
	"gpio136", "gpio137", "gpio138", "gpio139", "gpio140", "gpio141",
	"gpio142", "gpio143", "gpio144", "gpio145", "gpio146", "gpio147",
	"gpio148", "gpio149", "gpio150", "gpio151", "gpio152", "gpio153",
	"gpio154", "gpio155", "gpio156", "gpio157", "gpio158", "gpio159",
	"gpio160", "gpio161", "gpio162", "gpio163", "gpio164", "gpio165",
	"gpio166", "gpio167", "gpio168", "gpio169", "gpio170", "gpio171",
	"gpio172", "gpio173", "gpio174", "gpio175", "gpio176", "gpio177",
	"gpio178", "gpio179", "gpio180", "gpio181", "gpio182", "gpio183",
	"gpio184", "gpio185", "gpio186", "gpio187", "gpio188", "gpio189",
	"gpio190", "gpio191", "gpio192", "gpio193", "gpio194", "gpio195",
	"gpio196", "gpio197", "gpio198", "gpio199", "gpio200", "gpio201",
	"gpio202", "gpio203", "gpio204", "gpio205", "gpio206", "gpio207",
	"gpio208", "gpio209", "gpio210", "gpio211", "gpio212", "gpio213",
	"gpio214", "gpio215", "gpio216", "gpio217", "gpio218", "gpio219",
	"gpio220", "gpio221", "gpio222", "gpio223", "gpio224", "gpio225",
	"gpio226", "gpio227",
};

static const char * const atest_char_groups[] = {
	"gpio134", "gpio139", "gpio140", "gpio142", "gpio143",
};

static const char * const atest_usb_groups[] = {
	"gpio71", "gpio72", "gpio73", "gpio74", "gpio75", "gpio76", "gpio78",
	"gpio79", "gpio97", "gpio98", "gpio101", "gpio102", "gpio103",
	"gpio104", "gpio105", "gpio110", "gpio111", "gpio112", "gpio113",
	"gpio114", "gpio121", "gpio122", "gpio130", "gpio131", "gpio135",
	"gpio137", "gpio138", "gpio148", "gpio149",
};

static const char * const audio_ref_groups[] = {
	"gpio80",
};

static const char * const cam_mclk_groups[] = {
	"gpio6", "gpio7", "gpio16", "gpio17", "gpio33", "gpio34", "gpio119",
	"gpio120",
};

static const char * const cci_async_groups[] = {
	"gpio15", "gpio119", "gpio120", "gpio160", "gpio161", "gpio167",
};

static const char * const cci_i2c_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13", "gpio113", "gpio114",
	"gpio115", "gpio116", "gpio117", "gpio118", "gpio123", "gpio124",
	"gpio145", "gpio146", "gpio164", "gpio165",
};

static const char * const cci_timer0_groups[] = {
	"gpio119",
};

static const char * const cci_timer1_groups[] = {
	"gpio120",
};

static const char * const cci_timer2_groups[] = {
	"gpio14",
};

static const char * const cci_timer3_groups[] = {
	"gpio15",
};

static const char * const cci_timer4_groups[] = {
	"gpio161",
};

static const char * const cci_timer5_groups[] = {
	"gpio139",
};

static const char * const cci_timer6_groups[] = {
	"gpio162",
};

static const char * const cci_timer7_groups[] = {
	"gpio163",
};

static const char * const cci_timer8_groups[] = {
	"gpio167",
};

static const char * const cci_timer9_groups[] = {
	"gpio160",
};

static const char * const cmu_rng_groups[] = {
	"gpio123", "gpio124", "gpio126", "gpio136",
};

static const char * const cri_trng0_groups[] = {
	"gpio187",
};

static const char * const cri_trng1_groups[] = {
	"gpio188",
};

static const char * const cri_trng_groups[] = {
	"gpio190",
};

static const char * const dbg_out_groups[] = {
	"gpio125",
};

static const char * const ddr_bist_groups[] = {
	"gpio42", "gpio45", "gpio46", "gpio47",
};

static const char * const ddr_pxi0_groups[] = {
	"gpio121", "gpio126",
};

static const char * const ddr_pxi1_groups[] = {
	"gpio124", "gpio125",
};

static const char * const ddr_pxi2_groups[] = {
	"gpio123", "gpio138",
};

static const char * const ddr_pxi3_groups[] = {
	"gpio120", "gpio137",
};

static const char * const ddr_pxi4_groups[] = {
	"gpio216", "gpio217",
};

static const char * const ddr_pxi5_groups[] = {
	"gpio214", "gpio215",
};

static const char * const ddr_pxi6_groups[] = {
	"gpio79", "gpio218",
};

static const char * const ddr_pxi7_groups[] = {
	"gpio135", "gpio136",
};

static const char * const dp2_hot_groups[] = {
	"gpio20",
};

static const char * const dp3_hot_groups[] = {
	"gpio45",
};

static const char * const edp0_lcd_groups[] = {
	"gpio26",
};

static const char * const edp1_lcd_groups[] = {
	"gpio27",
};

static const char * const edp2_lcd_groups[] = {
	"gpio28",
};

static const char * const edp3_lcd_groups[] = {
	"gpio29",
};

static const char * const edp_hot_groups[] = {
	"gpio2", "gpio3", "gpio6", "gpio7",
};

static const char * const egpio_groups[] = {
	"gpio189", "gpio190", "gpio191", "gpio192", "gpio193", "gpio194",
	"gpio195", "gpio196", "gpio197", "gpio198", "gpio199", "gpio200",
	"gpio201", "gpio202", "gpio203", "gpio204", "gpio205", "gpio206",
	"gpio207", "gpio208", "gpio209", "gpio210", "gpio211", "gpio212",
	"gpio213", "gpio214", "gpio215", "gpio216", "gpio217", "gpio218",
	"gpio219", "gpio220", "gpio221", "gpio222", "gpio223", "gpio224",
	"gpio225", "gpio226", "gpio227",
};

static const char * const emac0_dll_groups[] = {
	"gpio216", "gpio217",
};

static const char * const emac0_mcg0_groups[] = {
	"gpio160",
};

static const char * const emac0_mcg1_groups[] = {
	"gpio161",
};

static const char * const emac0_mcg2_groups[] = {
	"gpio162",
};

static const char * const emac0_mcg3_groups[] = {
	"gpio163",
};

static const char * const emac0_phy_groups[] = {
	"gpio127",
};

static const char * const emac0_ptp_groups[] = {
	"gpio130", "gpio130", "gpio131", "gpio131", "gpio156", "gpio156",
	"gpio157", "gpio157", "gpio158", "gpio158", "gpio159", "gpio159",
};

static const char * const emac1_dll0_groups[] = {
	"gpio215",
};

static const char * const emac1_dll1_groups[] = {
	"gpio218",
};

static const char * const emac1_mcg0_groups[] = {
	"gpio57",
};

static const char * const emac1_mcg1_groups[] = {
	"gpio58",
};

static const char * const emac1_mcg2_groups[] = {
	"gpio68",
};

static const char * const emac1_mcg3_groups[] = {
	"gpio69",
};

static const char * const emac1_phy_groups[] = {
	"gpio54",
};

static const char * const emac1_ptp_groups[] = {
	"gpio55", "gpio55", "gpio56", "gpio56", "gpio93", "gpio93", "gpio94",
	"gpio94", "gpio95", "gpio95", "gpio96", "gpio96",
};

static const char * const gcc_gp1_groups[] = {
	"gpio119", "gpio149",
};

static const char * const gcc_gp2_groups[] = {
	"gpio114", "gpio120",
};

static const char * const gcc_gp3_groups[] = {
	"gpio115", "gpio139",
};

static const char * const gcc_gp4_groups[] = {
	"gpio160", "gpio162",
};

static const char * const gcc_gp5_groups[] = {
	"gpio167", "gpio168",
};

static const char * const hs1_mi2s_groups[] = {
	"gpio208", "gpio209", "gpio210", "gpio211",
};

static const char * const hs2_mi2s_groups[] = {
	"gpio91", "gpio92", "gpio218", "gpio219",
};

static const char * const hs3_mi2s_groups[] = {
	"gpio224", "gpio225", "gpio226", "gpio227",
};

static const char * const ibi_i3c_groups[] = {
	"gpio4", "gpio5", "gpio36", "gpio37", "gpio128", "gpio129", "gpio154",
	"gpio155",
};

static const char * const jitter_bist_groups[] = {
	"gpio140",
};

static const char * const lpass_slimbus_groups[] = {
	"gpio220", "gpio221",
};

static const char * const mdp0_vsync0_groups[] = {
	"gpio1",
};

static const char * const mdp0_vsync1_groups[] = {
	"gpio2",
};

static const char * const mdp0_vsync2_groups[] = {
	"gpio8",
};

static const char * const mdp0_vsync3_groups[] = {
	"gpio9",
};

static const char * const mdp0_vsync4_groups[] = {
	"gpio10",
};

static const char * const mdp0_vsync5_groups[] = {
	"gpio11",
};

static const char * const mdp0_vsync6_groups[] = {
	"gpio12",
};

static const char * const mdp0_vsync7_groups[] = {
	"gpio13",
};

static const char * const mdp0_vsync8_groups[] = {
	"gpio16",
};

static const char * const mdp1_vsync0_groups[] = {
	"gpio17",
};

static const char * const mdp1_vsync1_groups[] = {
	"gpio18",
};

static const char * const mdp1_vsync2_groups[] = {
	"gpio19",
};

static const char * const mdp1_vsync3_groups[] = {
	"gpio20",
};

static const char * const mdp1_vsync4_groups[] = {
	"gpio36",
};

static const char * const mdp1_vsync5_groups[] = {
	"gpio37",
};

static const char * const mdp1_vsync6_groups[] = {
	"gpio38",
};

static const char * const mdp1_vsync7_groups[] = {
	"gpio39",
};

static const char * const mdp1_vsync8_groups[] = {
	"gpio40",
};

static const char * const mdp_vsync_groups[] = {
	"gpio8", "gpio100", "gpio101",
};

static const char * const mi2s0_data0_groups[] = {
	"gpio95",
};

static const char * const mi2s0_data1_groups[] = {
	"gpio96",
};

static const char * const mi2s0_sck_groups[] = {
	"gpio93",
};

static const char * const mi2s0_ws_groups[] = {
	"gpio94",
};

static const char * const mi2s1_data0_groups[] = {
	"gpio222",
};

static const char * const mi2s1_data1_groups[] = {
	"gpio223",
};

static const char * const mi2s1_sck_groups[] = {
	"gpio220",
};

static const char * const mi2s1_ws_groups[] = {
	"gpio221",
};

static const char * const mi2s2_data0_groups[] = {
	"gpio214",
};

static const char * const mi2s2_data1_groups[] = {
	"gpio215",
};

static const char * const mi2s2_sck_groups[] = {
	"gpio212",
};

static const char * const mi2s2_ws_groups[] = {
	"gpio213",
};

static const char * const mi2s_mclk1_groups[] = {
	"gpio80", "gpio216",
};

static const char * const mi2s_mclk2_groups[] = {
	"gpio217",
};

static const char * const pcie2a_clkreq_groups[] = {
	"gpio142",
};

static const char * const pcie2b_clkreq_groups[] = {
	"gpio144",
};

static const char * const pcie3a_clkreq_groups[] = {
	"gpio150",
};

static const char * const pcie3b_clkreq_groups[] = {
	"gpio152",
};

static const char * const pcie4_clkreq_groups[] = {
	"gpio140",
};

static const char * const phase_flag_groups[] = {
	"gpio80", "gpio81", "gpio82", "gpio83", "gpio87", "gpio88", "gpio89",
	"gpio90", "gpio91", "gpio92", "gpio93", "gpio94", "gpio95", "gpio132",
	"gpio144", "gpio145", "gpio146", "gpio147", "gpio195", "gpio196",
	"gpio197", "gpio198", "gpio202", "gpio219", "gpio220", "gpio221",
	"gpio222", "gpio223", "gpio224", "gpio225", "gpio226", "gpio227",
};

static const char * const pll_bist_groups[] = {
	"gpio84",
};

static const char * const pll_clk_groups[] = {
	"gpio84", "gpio86",
};

static const char * const prng_rosc0_groups[] = {
	"gpio189",
};

static const char * const prng_rosc1_groups[] = {
	"gpio191",
};

static const char * const prng_rosc2_groups[] = {
	"gpio193",
};

static const char * const prng_rosc3_groups[] = {
	"gpio194",
};

static const char * const qdss_cti_groups[] = {
	"gpio3", "gpio4", "gpio7", "gpio21", "gpio30", "gpio30", "gpio31",
	"gpio31",
};

static const char * const qdss_gpio_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13", "gpio14", "gpio15", "gpio16",
	"gpio17", "gpio80", "gpio96", "gpio115", "gpio116", "gpio117",
	"gpio118", "gpio119", "gpio120", "gpio121", "gpio122", "gpio161",
	"gpio162", "gpio195", "gpio196", "gpio197", "gpio198", "gpio201",
	"gpio202", "gpio206", "gpio207", "gpio212", "gpio213", "gpio214",
	"gpio215", "gpio216", "gpio217", "gpio222", "gpio223",
};

static const char * const qspi_clk_groups[] = {
	"gpio74",
};

static const char * const qspi_cs_groups[] = {
	"gpio75", "gpio81",
};

static const char * const qspi_groups[] = {
	"gpio76", "gpio78", "gpio79",
};

static const char * const qup0_groups[] = {
	"gpio135", "gpio136", "gpio137", "gpio138",
};

static const char * const qup10_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25",
};

static const char * const qup11_groups[] = {
	"gpio18", "gpio19", "gpio20", "gpio21",
};

static const char * const qup12_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};

static const char * const qup13_groups[] = {
	"gpio26", "gpio27", "gpio28", "gpio29",
};

static const char * const qup14_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};

static const char * const qup15_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};

static const char * const qup16_groups[] = {
	"gpio70", "gpio71", "gpio72", "gpio73",
};

static const char * const qup17_groups[] = {
	"gpio61", "gpio62", "gpio63", "gpio64",
};

static const char * const qup18_groups[] = {
	"gpio66", "gpio67", "gpio68", "gpio69",
};

static const char * const qup19_groups[] = {
	"gpio55", "gpio56", "gpio57", "gpio58",
};

static const char * const qup1_groups[] = {
	"gpio158", "gpio159", "gpio160", "gpio161",
};

static const char * const qup20_groups[] = {
	"gpio87", "gpio88", "gpio89", "gpio90", "gpio91", "gpio92", "gpio110",
};

static const char * const qup21_groups[] = {
	"gpio81", "gpio82", "gpio83", "gpio84",
};

static const char * const qup22_groups[] = {
	"gpio83", "gpio84", "gpio85", "gpio86",
};

static const char * const qup23_groups[] = {
	"gpio59", "gpio60", "gpio61", "gpio62",
};

static const char * const qup2_groups[] = {
	"gpio121", "gpio122", "gpio123", "gpio124",
};

static const char * const qup3_groups[] = {
	"gpio135", "gpio136", "gpio137", "gpio138",
};

static const char * const qup4_groups[] = {
	"gpio111", "gpio112", "gpio171", "gpio172", "gpio173", "gpio174",
	"gpio175",
};

static const char * const qup5_groups[] = {
	"gpio111", "gpio112", "gpio145", "gpio146",
};

static const char * const qup6_groups[] = {
	"gpio154", "gpio155", "gpio156", "gpio157",
};

static const char * const qup7_groups[] = {
	"gpio125", "gpio126", "gpio128", "gpio129",
};

static const char * const qup8_groups[] = {
	"gpio43", "gpio44", "gpio45", "gpio46",
};

static const char * const qup9_groups[] = {
	"gpio41", "gpio42", "gpio43", "gpio44",
};

static const char * const rgmii_0_groups[] = {
	"gpio175", "gpio176", "gpio177", "gpio178", "gpio179", "gpio180",
	"gpio181", "gpio182", "gpio183", "gpio184", "gpio185", "gpio186",
	"gpio187", "gpio188",
};

static const char * const rgmii_1_groups[] = {
	"gpio97", "gpio98", "gpio99", "gpio100", "gpio101", "gpio102",
	"gpio103", "gpio104", "gpio105", "gpio106", "gpio107", "gpio108",
	"gpio109", "gpio110",
};

static const char * const sd_write_groups[] = {
	"gpio130",
};

static const char * const sdc40_groups[] = {
	"gpio76",
};

static const char * const sdc42_groups[] = {
	"gpio78",
};

static const char * const sdc43_groups[] = {
	"gpio79",
};

static const char * const sdc4_clk_groups[] = {
	"gpio74",
};

static const char * const sdc4_cmd_groups[] = {
	"gpio75",
};

static const char * const tb_trig_groups[] = {
	"gpio153", "gpio157",
};

static const char * const tgu_groups[] = {
	"gpio101", "gpio102", "gpio103", "gpio104", "gpio105", "gpio106",
	"gpio107", "gpio108",
};

static const char * const tsense_pwm1_groups[] = {
	"gpio70",
};

static const char * const tsense_pwm2_groups[] = {
	"gpio69",
};

static const char * const tsense_pwm3_groups[] = {
	"gpio67",
};

static const char * const tsense_pwm4_groups[] = {
	"gpio65",
};

static const char * const usb0_dp_groups[] = {
	"gpio21",
};

static const char * const usb0_phy_groups[] = {
	"gpio166",
};

static const char * const usb0_sbrx_groups[] = {
	"gpio170",
};

static const char * const usb0_sbtx_groups[] = {
	"gpio168", "gpio169",
};

static const char * const usb0_usb4_groups[] = {
	"gpio132",
};

static const char * const usb1_dp_groups[] = {
	"gpio9",
};

static const char * const usb1_phy_groups[] = {
	"gpio49",
};

static const char * const usb1_sbrx_groups[] = {
	"gpio53",
};

static const char * const usb1_sbtx_groups[] = {
	"gpio51", "gpio52",
};

static const char * const usb1_usb4_groups[] = {
	"gpio32",
};

static const char * const usb2phy_ac_groups[] = {
	"gpio24", "gpio25", "gpio133", "gpio134", "gpio148", "gpio149",
};

static const char * const vsense_trigger_groups[] = {
	"gpio81",
};

static const struct pinfunction sc8280xp_functions[] = {
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_usb),
	MSM_PIN_FUNCTION(audio_ref),
	MSM_PIN_FUNCTION(cam_mclk),
	MSM_PIN_FUNCTION(cci_async),
	MSM_PIN_FUNCTION(cci_i2c),
	MSM_PIN_FUNCTION(cci_timer0),
	MSM_PIN_FUNCTION(cci_timer1),
	MSM_PIN_FUNCTION(cci_timer2),
	MSM_PIN_FUNCTION(cci_timer3),
	MSM_PIN_FUNCTION(cci_timer4),
	MSM_PIN_FUNCTION(cci_timer5),
	MSM_PIN_FUNCTION(cci_timer6),
	MSM_PIN_FUNCTION(cci_timer7),
	MSM_PIN_FUNCTION(cci_timer8),
	MSM_PIN_FUNCTION(cci_timer9),
	MSM_PIN_FUNCTION(cmu_rng),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(cri_trng0),
	MSM_PIN_FUNCTION(cri_trng1),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(ddr_bist),
	MSM_PIN_FUNCTION(ddr_pxi0),
	MSM_PIN_FUNCTION(ddr_pxi1),
	MSM_PIN_FUNCTION(ddr_pxi2),
	MSM_PIN_FUNCTION(ddr_pxi3),
	MSM_PIN_FUNCTION(ddr_pxi4),
	MSM_PIN_FUNCTION(ddr_pxi5),
	MSM_PIN_FUNCTION(ddr_pxi6),
	MSM_PIN_FUNCTION(ddr_pxi7),
	MSM_PIN_FUNCTION(dp2_hot),
	MSM_PIN_FUNCTION(dp3_hot),
	MSM_PIN_FUNCTION(edp0_lcd),
	MSM_PIN_FUNCTION(edp1_lcd),
	MSM_PIN_FUNCTION(edp2_lcd),
	MSM_PIN_FUNCTION(edp3_lcd),
	MSM_PIN_FUNCTION(edp_hot),
	MSM_PIN_FUNCTION(egpio),
	MSM_PIN_FUNCTION(emac0_dll),
	MSM_PIN_FUNCTION(emac0_mcg0),
	MSM_PIN_FUNCTION(emac0_mcg1),
	MSM_PIN_FUNCTION(emac0_mcg2),
	MSM_PIN_FUNCTION(emac0_mcg3),
	MSM_PIN_FUNCTION(emac0_phy),
	MSM_PIN_FUNCTION(emac0_ptp),
	MSM_PIN_FUNCTION(emac1_dll0),
	MSM_PIN_FUNCTION(emac1_dll1),
	MSM_PIN_FUNCTION(emac1_mcg0),
	MSM_PIN_FUNCTION(emac1_mcg1),
	MSM_PIN_FUNCTION(emac1_mcg2),
	MSM_PIN_FUNCTION(emac1_mcg3),
	MSM_PIN_FUNCTION(emac1_phy),
	MSM_PIN_FUNCTION(emac1_ptp),
	MSM_PIN_FUNCTION(gcc_gp1),
	MSM_PIN_FUNCTION(gcc_gp2),
	MSM_PIN_FUNCTION(gcc_gp3),
	MSM_PIN_FUNCTION(gcc_gp4),
	MSM_PIN_FUNCTION(gcc_gp5),
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(hs1_mi2s),
	MSM_PIN_FUNCTION(hs2_mi2s),
	MSM_PIN_FUNCTION(hs3_mi2s),
	MSM_PIN_FUNCTION(ibi_i3c),
	MSM_PIN_FUNCTION(jitter_bist),
	MSM_PIN_FUNCTION(lpass_slimbus),
	MSM_PIN_FUNCTION(mdp0_vsync0),
	MSM_PIN_FUNCTION(mdp0_vsync1),
	MSM_PIN_FUNCTION(mdp0_vsync2),
	MSM_PIN_FUNCTION(mdp0_vsync3),
	MSM_PIN_FUNCTION(mdp0_vsync4),
	MSM_PIN_FUNCTION(mdp0_vsync5),
	MSM_PIN_FUNCTION(mdp0_vsync6),
	MSM_PIN_FUNCTION(mdp0_vsync7),
	MSM_PIN_FUNCTION(mdp0_vsync8),
	MSM_PIN_FUNCTION(mdp1_vsync0),
	MSM_PIN_FUNCTION(mdp1_vsync1),
	MSM_PIN_FUNCTION(mdp1_vsync2),
	MSM_PIN_FUNCTION(mdp1_vsync3),
	MSM_PIN_FUNCTION(mdp1_vsync4),
	MSM_PIN_FUNCTION(mdp1_vsync5),
	MSM_PIN_FUNCTION(mdp1_vsync6),
	MSM_PIN_FUNCTION(mdp1_vsync7),
	MSM_PIN_FUNCTION(mdp1_vsync8),
	MSM_PIN_FUNCTION(mdp_vsync),
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
	MSM_PIN_FUNCTION(mi2s_mclk1),
	MSM_PIN_FUNCTION(mi2s_mclk2),
	MSM_PIN_FUNCTION(pcie2a_clkreq),
	MSM_PIN_FUNCTION(pcie2b_clkreq),
	MSM_PIN_FUNCTION(pcie3a_clkreq),
	MSM_PIN_FUNCTION(pcie3b_clkreq),
	MSM_PIN_FUNCTION(pcie4_clkreq),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(pll_bist),
	MSM_PIN_FUNCTION(pll_clk),
	MSM_PIN_FUNCTION(prng_rosc0),
	MSM_PIN_FUNCTION(prng_rosc1),
	MSM_PIN_FUNCTION(prng_rosc2),
	MSM_PIN_FUNCTION(prng_rosc3),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qdss_gpio),
	MSM_PIN_FUNCTION(qspi),
	MSM_PIN_FUNCTION(qspi_clk),
	MSM_PIN_FUNCTION(qspi_cs),
	MSM_PIN_FUNCTION(qup0),
	MSM_PIN_FUNCTION(qup1),
	MSM_PIN_FUNCTION(qup2),
	MSM_PIN_FUNCTION(qup3),
	MSM_PIN_FUNCTION(qup4),
	MSM_PIN_FUNCTION(qup5),
	MSM_PIN_FUNCTION(qup6),
	MSM_PIN_FUNCTION(qup7),
	MSM_PIN_FUNCTION(qup8),
	MSM_PIN_FUNCTION(qup9),
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
	MSM_PIN_FUNCTION(qup20),
	MSM_PIN_FUNCTION(qup21),
	MSM_PIN_FUNCTION(qup22),
	MSM_PIN_FUNCTION(qup23),
	MSM_PIN_FUNCTION(rgmii_0),
	MSM_PIN_FUNCTION(rgmii_1),
	MSM_PIN_FUNCTION(sd_write),
	MSM_PIN_FUNCTION(sdc40),
	MSM_PIN_FUNCTION(sdc42),
	MSM_PIN_FUNCTION(sdc43),
	MSM_PIN_FUNCTION(sdc4_clk),
	MSM_PIN_FUNCTION(sdc4_cmd),
	MSM_PIN_FUNCTION(tb_trig),
	MSM_PIN_FUNCTION(tgu),
	MSM_PIN_FUNCTION(tsense_pwm1),
	MSM_PIN_FUNCTION(tsense_pwm2),
	MSM_PIN_FUNCTION(tsense_pwm3),
	MSM_PIN_FUNCTION(tsense_pwm4),
	MSM_PIN_FUNCTION(usb0_dp),
	MSM_PIN_FUNCTION(usb0_phy),
	MSM_PIN_FUNCTION(usb0_sbrx),
	MSM_PIN_FUNCTION(usb0_sbtx),
	MSM_PIN_FUNCTION(usb0_usb4),
	MSM_PIN_FUNCTION(usb1_dp),
	MSM_PIN_FUNCTION(usb1_phy),
	MSM_PIN_FUNCTION(usb1_sbrx),
	MSM_PIN_FUNCTION(usb1_sbtx),
	MSM_PIN_FUNCTION(usb1_usb4),
	MSM_PIN_FUNCTION(usb2phy_ac),
	MSM_PIN_FUNCTION(vsense_trigger),
};

static const struct msm_pingroup sc8280xp_groups[] = {
	[0] = PINGROUP(0, qup12, _, _, _, _, _, _),
	[1] = PINGROUP(1, qup12, mdp0_vsync0, _, _, _, _, _),
	[2] = PINGROUP(2, edp_hot, qup12, mdp0_vsync1, _, _, _, _),
	[3] = PINGROUP(3, edp_hot, qup12, qdss_cti, _, _, _, _),
	[4] = PINGROUP(4, qup14, ibi_i3c, qdss_cti, _, _, _, _),
	[5] = PINGROUP(5, qup14, ibi_i3c, _, _, _, _, _),
	[6] = PINGROUP(6, edp_hot, qup14, cam_mclk, _, _, _, _),
	[7] = PINGROUP(7, edp_hot, qup14, qdss_cti, cam_mclk, _, _, _),
	[8] = PINGROUP(8, mdp_vsync, mdp0_vsync2, _, _, _, _, _),
	[9] = PINGROUP(9, usb1_dp, mdp0_vsync3, _, _, _, _, _),
	[10] = PINGROUP(10, cci_i2c, mdp0_vsync4, _, qdss_gpio, _, _, _),
	[11] = PINGROUP(11, cci_i2c, mdp0_vsync5, _, qdss_gpio, _, _, _),
	[12] = PINGROUP(12, cci_i2c, mdp0_vsync6, _, qdss_gpio, _, _, _),
	[13] = PINGROUP(13, cci_i2c, mdp0_vsync7, _, qdss_gpio, _, _, _),
	[14] = PINGROUP(14, cci_timer2, qdss_gpio, _, _, _, _, _),
	[15] = PINGROUP(15, cci_timer3, cci_async, _, qdss_gpio, _, _, _),
	[16] = PINGROUP(16, cam_mclk, mdp0_vsync8, _, qdss_gpio, _, _, _),
	[17] = PINGROUP(17, cam_mclk, mdp1_vsync0, _, qdss_gpio, _, _, _),
	[18] = PINGROUP(18, qup11, mdp1_vsync1, _, _, _, _, _),
	[19] = PINGROUP(19, qup11, mdp1_vsync2, _, _, _, _, _),
	[20] = PINGROUP(20, qup11, dp2_hot, mdp1_vsync3, _, _, _, _),
	[21] = PINGROUP(21, qup11, usb0_dp, qdss_cti, _, _, _, _),
	[22] = PINGROUP(22, qup10, _, _, _, _, _, _),
	[23] = PINGROUP(23, qup10, _, _, _, _, _, _),
	[24] = PINGROUP(24, qup10, usb2phy_ac, _, _, _, _, _),
	[25] = PINGROUP(25, qup10, usb2phy_ac, _, _, _, _, _),
	[26] = PINGROUP(26, qup13, edp0_lcd, _, _, _, _, _),
	[27] = PINGROUP(27, qup13, edp1_lcd, _, _, _, _, _),
	[28] = PINGROUP(28, qup13, edp2_lcd, _, _, _, _, _),
	[29] = PINGROUP(29, qup13, edp3_lcd, _, _, _, _, _),
	[30] = PINGROUP(30, qdss_cti, qdss_cti, _, _, _, _, _),
	[31] = PINGROUP(31, qdss_cti, qdss_cti, _, _, _, _, _),
	[32] = PINGROUP(32, usb1_usb4, _, _, _, _, _, _),
	[33] = PINGROUP(33, cam_mclk, _, _, _, _, _, _),
	[34] = PINGROUP(34, cam_mclk, _, _, _, _, _, _),
	[35] = PINGROUP(35, _, _, _, _, _, _, _),
	[36] = PINGROUP(36, qup15, ibi_i3c, mdp1_vsync4, _, _, _, _),
	[37] = PINGROUP(37, qup15, ibi_i3c, mdp1_vsync5, _, _, _, _),
	[38] = PINGROUP(38, qup15, mdp1_vsync6, _, _, _, _, _),
	[39] = PINGROUP(39, qup15, mdp1_vsync7, _, _, _, _, _),
	[40] = PINGROUP(40, mdp1_vsync8, _, _, _, _, _, _),
	[41] = PINGROUP(41, qup9, _, _, _, _, _, _),
	[42] = PINGROUP(42, qup9, ddr_bist, _, _, _, _, _),
	[43] = PINGROUP(43, qup8, qup9, _, _, _, _, _),
	[44] = PINGROUP(44, qup8, qup9, _, _, _, _, _),
	[45] = PINGROUP(45, qup8, dp3_hot, ddr_bist, _, _, _, _),
	[46] = PINGROUP(46, qup8, ddr_bist, _, _, _, _, _),
	[47] = PINGROUP(47, ddr_bist, _, _, _, _, _, _),
	[48] = PINGROUP(48, _, _, _, _, _, _, _),
	[49] = PINGROUP(49, usb1_phy, _, _, _, _, _, _),
	[50] = PINGROUP(50, _, _, _, _, _, _, _),
	[51] = PINGROUP(51, usb1_sbtx, _, _, _, _, _, _),
	[52] = PINGROUP(52, usb1_sbtx, _, _, _, _, _, _),
	[53] = PINGROUP(53, usb1_sbrx, _, _, _, _, _, _),
	[54] = PINGROUP(54, emac1_phy, _, _, _, _, _, _),
	[55] = PINGROUP(55, emac1_ptp, emac1_ptp, qup19, _, _, _, _),
	[56] = PINGROUP(56, emac1_ptp, emac1_ptp, qup19, _, _, _, _),
	[57] = PINGROUP(57, qup19, emac1_mcg0, _, _, _, _, _),
	[58] = PINGROUP(58, qup19, emac1_mcg1, _, _, _, _, _),
	[59] = PINGROUP(59, qup23, _, _, _, _, _, _),
	[60] = PINGROUP(60, qup23, _, _, _, _, _, _),
	[61] = PINGROUP(61, qup23, qup17, _, _, _, _, _),
	[62] = PINGROUP(62, qup23, qup17, _, _, _, _, _),
	[63] = PINGROUP(63, qup17, _, _, _, _, _, _),
	[64] = PINGROUP(64, qup17, _, _, _, _, _, _),
	[65] = PINGROUP(65, tsense_pwm4, _, _, _, _, _, _),
	[66] = PINGROUP(66, qup18, _, _, _, _, _, _),
	[67] = PINGROUP(67, qup18, tsense_pwm3, _, _, _, _, _),
	[68] = PINGROUP(68, qup18, emac1_mcg2, _, _, _, _, _),
	[69] = PINGROUP(69, qup18, emac1_mcg3, tsense_pwm2, _, _, _, _),
	[70] = PINGROUP(70, qup16, tsense_pwm1, _, _, _, _, _),
	[71] = PINGROUP(71, qup16, atest_usb, _, _, _, _, _),
	[72] = PINGROUP(72, qup16, atest_usb, _, _, _, _, _),
	[73] = PINGROUP(73, qup16, atest_usb, _, _, _, _, _),
	[74] = PINGROUP(74, qspi_clk, sdc4_clk, atest_usb, _, _, _, _),
	[75] = PINGROUP(75, qspi_cs, sdc4_cmd, atest_usb, _, _, _, _),
	[76] = PINGROUP(76, qspi, sdc40, atest_usb, _, _, _, _),
	[77] = PINGROUP(77, _, _, _, _, _, _, _),
	[78] = PINGROUP(78, qspi, sdc42, atest_usb, _, _, _, _),
	[79] = PINGROUP(79, qspi, sdc43, atest_usb, ddr_pxi6, _, _, _),
	[80] = PINGROUP(80, mi2s_mclk1, audio_ref, phase_flag, _, qdss_gpio, _, _),
	[81] = PINGROUP(81, qup21, qspi_cs, phase_flag, _, vsense_trigger, _, _),
	[82] = PINGROUP(82, qup21, phase_flag, _, _, _, _, _),
	[83] = PINGROUP(83, qup21, qup22, phase_flag, _, _, _, _),
	[84] = PINGROUP(84, qup21, qup22, pll_bist, pll_clk, _, _, _),
	[85] = PINGROUP(85, qup22, _, _, _, _, _, _),
	[86] = PINGROUP(86, qup22, _, pll_clk, _, _, _, _),
	[87] = PINGROUP(87, qup20, phase_flag, _, _, _, _, _),
	[88] = PINGROUP(88, qup20, phase_flag, _, _, _, _, _),
	[89] = PINGROUP(89, qup20, phase_flag, _, _, _, _, _),
	[90] = PINGROUP(90, qup20, phase_flag, _, _, _, _, _),
	[91] = PINGROUP(91, qup20, hs2_mi2s, phase_flag, _, _, _, _),
	[92] = PINGROUP(92, qup20, hs2_mi2s, phase_flag, _, _, _, _),
	[93] = PINGROUP(93, mi2s0_sck, emac1_ptp, emac1_ptp, phase_flag, _, _, _),
	[94] = PINGROUP(94, mi2s0_ws, emac1_ptp, emac1_ptp, phase_flag, _, _, _),
	[95] = PINGROUP(95, mi2s0_data0, emac1_ptp, emac1_ptp, phase_flag, _, _, _),
	[96] = PINGROUP(96, mi2s0_data1, emac1_ptp, emac1_ptp, qdss_gpio, _, _, _),
	[97] = PINGROUP(97, rgmii_1, atest_usb, _, _, _, _, _),
	[98] = PINGROUP(98, rgmii_1, atest_usb, _, _, _, _, _),
	[99] = PINGROUP(99, rgmii_1, _, _, _, _, _, _),
	[100] = PINGROUP(100, mdp_vsync, rgmii_1, _, _, _, _, _),
	[101] = PINGROUP(101, mdp_vsync, rgmii_1, tgu, atest_usb, _, _, _),
	[102] = PINGROUP(102, rgmii_1, tgu, atest_usb, _, _, _, _),
	[103] = PINGROUP(103, rgmii_1, tgu, atest_usb, _, _, _, _),
	[104] = PINGROUP(104, rgmii_1, tgu, atest_usb, _, _, _, _),
	[105] = PINGROUP(105, rgmii_1, tgu, atest_usb, _, _, _, _),
	[106] = PINGROUP(106, rgmii_1, tgu, _, _, _, _, _),
	[107] = PINGROUP(107, rgmii_1, tgu, _, _, _, _, _),
	[108] = PINGROUP(108, rgmii_1, tgu, _, _, _, _, _),
	[109] = PINGROUP(109, rgmii_1, _, _, _, _, _, _),
	[110] = PINGROUP(110, qup20, rgmii_1, atest_usb, _, _, _, _),
	[111] = PINGROUP(111, qup4, qup5, atest_usb, _, _, _, _),
	[112] = PINGROUP(112, qup4, qup5, atest_usb, _, _, _, _),
	[113] = PINGROUP(113, cci_i2c, atest_usb, _, _, _, _, _),
	[114] = PINGROUP(114, cci_i2c, gcc_gp2, atest_usb, _, _, _, _),
	[115] = PINGROUP(115, cci_i2c, gcc_gp3, qdss_gpio, _, _, _, _),
	[116] = PINGROUP(116, cci_i2c, qdss_gpio, _, _, _, _, _),
	[117] = PINGROUP(117, cci_i2c, _, qdss_gpio, _, _, _, _),
	[118] = PINGROUP(118, cci_i2c, _, qdss_gpio, _, _, _, _),
	[119] = PINGROUP(119, cam_mclk, cci_timer0, cci_async, gcc_gp1, qdss_gpio, _, _),
	[120] = PINGROUP(120, cam_mclk, cci_timer1, cci_async, gcc_gp2, qdss_gpio, ddr_pxi3, _),
	[121] = PINGROUP(121, qup2, qdss_gpio, _, atest_usb, ddr_pxi0, _, _),
	[122] = PINGROUP(122, qup2, qdss_gpio, atest_usb, _, _, _, _),
	[123] = PINGROUP(123, qup2, cci_i2c, cmu_rng, ddr_pxi2, _, _, _),
	[124] = PINGROUP(124, qup2, cci_i2c, cmu_rng, ddr_pxi1, _, _, _),
	[125] = PINGROUP(125, qup7, dbg_out, ddr_pxi1, _, _, _, _),
	[126] = PINGROUP(126, qup7, cmu_rng, ddr_pxi0, _, _, _, _),
	[127] = PINGROUP(127, emac0_phy, _, _, _, _, _, _),
	[128] = PINGROUP(128, qup7, ibi_i3c, _, _, _, _, _),
	[129] = PINGROUP(129, qup7, ibi_i3c, _, _, _, _, _),
	[130] = PINGROUP(130, emac0_ptp, emac0_ptp, sd_write, atest_usb, _, _, _),
	[131] = PINGROUP(131, emac0_ptp, emac0_ptp, atest_usb, _, _, _, _),
	[132] = PINGROUP(132, usb0_usb4, phase_flag, _, _, _, _, _),
	[133] = PINGROUP(133, usb2phy_ac, _, _, _, _, _, _),
	[134] = PINGROUP(134, usb2phy_ac, atest_char, _, _, _, _, _),
	[135] = PINGROUP(135, qup0, qup3, _, atest_usb, ddr_pxi7, _, _),
	[136] = PINGROUP(136, qup0, qup3, cmu_rng, ddr_pxi7, _, _, _),
	[137] = PINGROUP(137, qup3, qup0, _, atest_usb, ddr_pxi3, _, _),
	[138] = PINGROUP(138, qup3, qup0, _, atest_usb, ddr_pxi2, _, _),
	[139] = PINGROUP(139, cci_timer5, gcc_gp3, atest_char, _, _, _, _),
	[140] = PINGROUP(140, pcie4_clkreq, jitter_bist, atest_char, _, _, _, _),
	[141] = PINGROUP(141, _, _, _, _, _, _, _),
	[142] = PINGROUP(142, pcie2a_clkreq, atest_char, _, _, _, _, _),
	[143] = PINGROUP(143, _, atest_char, _, _, _, _, _),
	[144] = PINGROUP(144, pcie2b_clkreq, phase_flag, _, _, _, _, _),
	[145] = PINGROUP(145, qup5, cci_i2c, phase_flag, _, _, _, _),
	[146] = PINGROUP(146, qup5, cci_i2c, phase_flag, _, _, _, _),
	[147] = PINGROUP(147, _, phase_flag, _, _, _, _, _),
	[148] = PINGROUP(148, usb2phy_ac, _, atest_usb, _, _, _, _),
	[149] = PINGROUP(149, usb2phy_ac, gcc_gp1, atest_usb, _, _, _, _),
	[150] = PINGROUP(150, pcie3a_clkreq, _, _, _, _, _, _),
	[151] = PINGROUP(151, _, _, _, _, _, _, _),
	[152] = PINGROUP(152, pcie3b_clkreq, _, _, _, _, _, _),
	[153] = PINGROUP(153, _, tb_trig, _, _, _, _, _),
	[154] = PINGROUP(154, qup6, ibi_i3c, _, _, _, _, _),
	[155] = PINGROUP(155, qup6, ibi_i3c, _, _, _, _, _),
	[156] = PINGROUP(156, qup6, emac0_ptp, emac0_ptp, _, _, _, _),
	[157] = PINGROUP(157, qup6, emac0_ptp, emac0_ptp, tb_trig, _, _, _),
	[158] = PINGROUP(158, qup1, emac0_ptp, emac0_ptp, _, _, _, _),
	[159] = PINGROUP(159, qup1, emac0_ptp, emac0_ptp, _, _, _, _),
	[160] = PINGROUP(160, cci_timer9, qup1, cci_async, emac0_mcg0, gcc_gp4, _, _),
	[161] = PINGROUP(161, cci_timer4, cci_async, qup1, emac0_mcg1, qdss_gpio, _, _),
	[162] = PINGROUP(162, cci_timer6, emac0_mcg2, gcc_gp4, qdss_gpio, _, _, _),
	[163] = PINGROUP(163, cci_timer7, emac0_mcg3, _, _, _, _, _),
	[164] = PINGROUP(164, cci_i2c, _, _, _, _, _, _),
	[165] = PINGROUP(165, cci_i2c, _, _, _, _, _, _),
	[166] = PINGROUP(166, usb0_phy, _, _, _, _, _, _),
	[167] = PINGROUP(167, cci_timer8, cci_async, gcc_gp5, _, _, _, _),
	[168] = PINGROUP(168, usb0_sbtx, gcc_gp5, _, _, _, _, _),
	[169] = PINGROUP(169, usb0_sbtx, _, _, _, _, _, _),
	[170] = PINGROUP(170, usb0_sbrx, _, _, _, _, _, _),
	[171] = PINGROUP(171, qup4, _, _, _, _, _, _),
	[172] = PINGROUP(172, qup4, _, _, _, _, _, _),
	[173] = PINGROUP(173, qup4, _, _, _, _, _, _),
	[174] = PINGROUP(174, qup4, _, _, _, _, _, _),
	[175] = PINGROUP(175, qup4, rgmii_0, _, _, _, _, _),
	[176] = PINGROUP(176, rgmii_0, _, _, _, _, _, _),
	[177] = PINGROUP(177, rgmii_0, _, _, _, _, _, _),
	[178] = PINGROUP(178, rgmii_0, _, _, _, _, _, _),
	[179] = PINGROUP(179, rgmii_0, _, _, _, _, _, _),
	[180] = PINGROUP(180, rgmii_0, _, _, _, _, _, _),
	[181] = PINGROUP(181, rgmii_0, _, _, _, _, _, _),
	[182] = PINGROUP(182, rgmii_0, _, _, _, _, _, _),
	[183] = PINGROUP(183, rgmii_0, _, _, _, _, _, _),
	[184] = PINGROUP(184, rgmii_0, _, _, _, _, _, _),
	[185] = PINGROUP(185, rgmii_0, _, _, _, _, _, _),
	[186] = PINGROUP(186, rgmii_0, _, _, _, _, _, _),
	[187] = PINGROUP(187, rgmii_0, cri_trng0, _, _, _, _, _),
	[188] = PINGROUP(188, rgmii_0, cri_trng1, _, _, _, _, _),
	[189] = PINGROUP(189, prng_rosc0, _, _, _, _, _, egpio),
	[190] = PINGROUP(190, cri_trng, _, _, _, _, _, egpio),
	[191] = PINGROUP(191, prng_rosc1, _, _, _, _, _, egpio),
	[192] = PINGROUP(192, _, _, _, _, _, _, egpio),
	[193] = PINGROUP(193, prng_rosc2, _, _, _, _, _, egpio),
	[194] = PINGROUP(194, prng_rosc3, _, _, _, _, _, egpio),
	[195] = PINGROUP(195, phase_flag, _, qdss_gpio, _, _, _, egpio),
	[196] = PINGROUP(196, phase_flag, _, qdss_gpio, _, _, _, egpio),
	[197] = PINGROUP(197, phase_flag, _, qdss_gpio, _, _, _, egpio),
	[198] = PINGROUP(198, phase_flag, _, qdss_gpio, _, _, _, egpio),
	[199] = PINGROUP(199, _, _, _, _, _, _, egpio),
	[200] = PINGROUP(200, _, _, _, _, _, _, egpio),
	[201] = PINGROUP(201, qdss_gpio, _, _, _, _, _, egpio),
	[202] = PINGROUP(202, phase_flag, _, qdss_gpio, _, _, _, egpio),
	[203] = PINGROUP(203, _, _, _, _, _, _, egpio),
	[204] = PINGROUP(204, _, _, _, _, _, _, egpio),
	[205] = PINGROUP(205, _, _, _, _, _, _, egpio),
	[206] = PINGROUP(206, qdss_gpio, _, _, _, _, _, egpio),
	[207] = PINGROUP(207, qdss_gpio, _, _, _, _, _, egpio),
	[208] = PINGROUP(208, hs1_mi2s, _, _, _, _, _, egpio),
	[209] = PINGROUP(209, hs1_mi2s, _, _, _, _, _, egpio),
	[210] = PINGROUP(210, hs1_mi2s, _, _, _, _, _, egpio),
	[211] = PINGROUP(211, hs1_mi2s, _, _, _, _, _, egpio),
	[212] = PINGROUP(212, mi2s2_sck, qdss_gpio, _, _, _, _, egpio),
	[213] = PINGROUP(213, mi2s2_ws, qdss_gpio, _, _, _, _, egpio),
	[214] = PINGROUP(214, mi2s2_data0, qdss_gpio, ddr_pxi5, _, _, _, egpio),
	[215] = PINGROUP(215, mi2s2_data1, qdss_gpio, emac1_dll0, ddr_pxi5, _, _, egpio),
	[216] = PINGROUP(216, mi2s_mclk1, qdss_gpio, emac0_dll, ddr_pxi4, _, _, egpio),
	[217] = PINGROUP(217, mi2s_mclk2, qdss_gpio, emac0_dll, ddr_pxi4, _, _, egpio),
	[218] = PINGROUP(218, hs2_mi2s, emac1_dll1, ddr_pxi6, _, _, _, egpio),
	[219] = PINGROUP(219, hs2_mi2s, phase_flag, _, _, _, _, egpio),
	[220] = PINGROUP(220, lpass_slimbus, mi2s1_sck, phase_flag, _, _, _, egpio),
	[221] = PINGROUP(221, lpass_slimbus, mi2s1_ws, phase_flag, _, _, _, egpio),
	[222] = PINGROUP(222, mi2s1_data0, phase_flag, _, qdss_gpio, _, _, egpio),
	[223] = PINGROUP(223, mi2s1_data1, phase_flag, _, qdss_gpio, _, _, egpio),
	[224] = PINGROUP(224, hs3_mi2s, phase_flag, _, _, _, _, egpio),
	[225] = PINGROUP(225, hs3_mi2s, phase_flag, _, _, _, _, egpio),
	[226] = PINGROUP(226, hs3_mi2s, phase_flag, _, _, _, _, egpio),
	[227] = PINGROUP(227, hs3_mi2s, phase_flag, _, _, _, _, egpio),
	[228] = UFS_RESET(ufs_reset, 0xf1000),
	[229] = UFS_RESET(ufs1_reset, 0xf3000),
	[230] = SDC_QDSD_PINGROUP(sdc2_clk, 0xe8000, 14, 6),
	[231] = SDC_QDSD_PINGROUP(sdc2_cmd, 0xe8000, 11, 3),
	[232] = SDC_QDSD_PINGROUP(sdc2_data, 0xe8000, 9, 0),
};

static const struct msm_gpio_wakeirq_map sc8280xp_pdc_map[] = {
	{ 3, 245 }, { 4, 263 }, { 7, 254 }, { 21, 220 }, { 25, 244 },
	{ 26, 211 }, { 27, 172 }, { 29, 203 }, { 30, 169 }, { 31, 180 },
	{ 32, 181 }, { 33, 182 }, { 36, 206 }, { 39, 246 }, { 40, 183 },
	{ 42, 179 }, { 46, 247 }, { 53, 248 }, { 54, 190 }, { 55, 249 },
	{ 56, 250 }, { 58, 251 }, { 59, 207 }, { 62, 252 }, { 63, 191 },
	{ 64, 192 }, { 65, 193 }, { 69, 253 }, { 73, 255 }, { 84, 256 },
	{ 85, 208 }, { 90, 257 }, { 102, 214 }, { 103, 215 }, { 104, 216 },
	{ 107, 217 }, { 110, 218 }, { 124, 224 }, { 125, 189 },
	{ 126, 200 }, { 127, 225 }, { 128, 262 }, { 129, 201 },
	{ 130, 209 }, { 131, 173 }, { 132, 202 }, { 136, 210 },
	{ 138, 171 }, { 139, 226 }, { 140, 227 }, { 142, 228 },
	{ 144, 229 }, { 145, 230 }, { 146, 231 }, { 148, 232 },
	{ 149, 233 }, { 150, 234 }, { 152, 235 }, { 154, 212 },
	{ 157, 213 }, { 161, 219 }, { 170, 236 }, { 171, 221 },
	{ 174, 222 }, { 175, 237 }, { 176, 223 }, { 177, 170 },
	{ 180, 238 }, { 181, 239 }, { 182, 240 }, { 183, 241 },
	{ 184, 242 }, { 185, 243 }, { 190, 178 }, { 193, 184 },
	{ 196, 185 }, { 198, 186 }, { 200, 174 }, { 201, 175 },
	{ 205, 176 }, { 206, 177 }, { 208, 187 }, { 210, 198 },
	{ 211, 199 }, { 212, 204 }, { 215, 205 }, { 220, 188 },
	{ 221, 194 }, { 223, 195 }, { 225, 196 }, { 227, 197 },
};

static struct msm_pinctrl_soc_data sc8280xp_pinctrl = {
	.pins = sc8280xp_pins,
	.npins = ARRAY_SIZE(sc8280xp_pins),
	.functions = sc8280xp_functions,
	.nfunctions = ARRAY_SIZE(sc8280xp_functions),
	.groups = sc8280xp_groups,
	.ngroups = ARRAY_SIZE(sc8280xp_groups),
	.ngpios = 230,
	.wakeirq_map = sc8280xp_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(sc8280xp_pdc_map),
	.egpio_func = 7,
};

static int sc8280xp_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &sc8280xp_pinctrl);
}

static const struct of_device_id sc8280xp_pinctrl_of_match[] = {
	{ .compatible = "qcom,sc8280xp-tlmm", },
	{ },
};
MODULE_DEVICE_TABLE(of, sc8280xp_pinctrl_of_match);

static struct platform_driver sc8280xp_pinctrl_driver = {
	.driver = {
		.name = "sc8280xp-tlmm",
		.of_match_table = sc8280xp_pinctrl_of_match,
	},
	.probe = sc8280xp_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init sc8280xp_pinctrl_init(void)
{
	return platform_driver_register(&sc8280xp_pinctrl_driver);
}
arch_initcall(sc8280xp_pinctrl_init);

static void __exit sc8280xp_pinctrl_exit(void)
{
	platform_driver_unregister(&sc8280xp_pinctrl_driver);
}
module_exit(sc8280xp_pinctrl_exit);

MODULE_DESCRIPTION("Qualcomm SC8280XP TLMM pinctrl driver");
MODULE_LICENSE("GPL");
