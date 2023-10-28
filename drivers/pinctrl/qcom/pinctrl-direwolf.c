// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

#define FUNCTION(fname)					\
	[msm_mux_##fname] = {				\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define REG_BASE 0x100000
#define REG_SIZE 0x1000
#define REG_DIRCONN 0x110000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{						\
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = (unsigned int)ARRAY_SIZE(gpio##id##_pins),	\
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
		.ctl_reg = REG_BASE + REG_SIZE * id,		\
		.io_reg = REG_BASE + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = REG_BASE + 0x8 + REG_SIZE * id,	\
		.intr_status_reg = REG_BASE + 0xc + REG_SIZE * id,	\
		.intr_target_reg = REG_BASE + 0x8 + REG_SIZE * id,	\
		.dir_conn_reg = REG_BASE + REG_DIRCONN, \
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
		.dir_conn_en_bit = 8,		\
	}

#define SDC_QDSD_PINGROUP(pg_name, ctl, pull, drv)	\
	{						\
		.name = #pg_name,			\
		.pins = pg_name##_pins,			\
		.npins = (unsigned int)ARRAY_SIZE(pg_name##_pins),	\
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
		.name = #pg_name,			\
		.pins = pg_name##_pins,			\
		.npins = (unsigned int)ARRAY_SIZE(pg_name##_pins),	\
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
static const struct pinctrl_pin_desc direwolf_pins[] = {
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

enum direwolf_functions {
	msm_mux_gpio,
	msm_mux_qup12,
	msm_mux_mdp0_vsync0,
	msm_mux_edp_hot,
	msm_mux_mdp0_vsync1,
	msm_mux_qdss_cti,
	msm_mux_qup14,
	msm_mux_ibi_i3c,
	msm_mux_cam_mclk,
	msm_mux_mdp_vsync,
	msm_mux_mdp0_vsync2,
	msm_mux_usb1_dp,
	msm_mux_mdp0_vsync3,
	msm_mux_cci_i2c,
	msm_mux_mdp0_vsync4,
	msm_mux_qdss_gpio0,
	msm_mux_mdp0_vsync5,
	msm_mux_qdss_gpio1,
	msm_mux_mdp0_vsync6,
	msm_mux_qdss_gpio2,
	msm_mux_mdp0_vsync7,
	msm_mux_qdss_gpio3,
	msm_mux_cci_timer2,
	msm_mux_qdss_gpio4,
	msm_mux_cci_timer3,
	msm_mux_cci_async,
	msm_mux_qdss_gpio5,
	msm_mux_mdp0_vsync8,
	msm_mux_qdss_gpio6,
	msm_mux_mdp1_vsync0,
	msm_mux_qdss_gpio7,
	msm_mux_qup11,
	msm_mux_mdp1_vsync1,
	msm_mux_mdp1_vsync2,
	msm_mux_dp2_hot,
	msm_mux_mdp1_vsync3,
	msm_mux_usb0_dp,
	msm_mux_qup10,
	msm_mux_usb2phy_ac,
	msm_mux_qup13,
	msm_mux_edp0_lcd,
	msm_mux_edp1_lcd,
	msm_mux_edp2_lcd,
	msm_mux_edp3_lcd,
	msm_mux_usb1_usb4,
	msm_mux_qup15,
	msm_mux_mdp1_vsync4,
	msm_mux_mdp1_vsync5,
	msm_mux_mdp1_vsync6,
	msm_mux_mdp1_vsync7,
	msm_mux_mdp1_vsync8,
	msm_mux_qup9,
	msm_mux_ddr_bist,
	msm_mux_qup8,
	msm_mux_dp3_hot,
	msm_mux_usb1_phy,
	msm_mux_usb1_sbtx,
	msm_mux_usb1_sbrx,
	msm_mux_emac1_phy,
	msm_mux_emac1_ptp,
	msm_mux_qup19,
	msm_mux_emac1_mcg0,
	msm_mux_emac1_mcg1,
	msm_mux_qup23,
	msm_mux_qup17,
	msm_mux_tsense_pwm4,
	msm_mux_qup18,
	msm_mux_tsense_pwm3,
	msm_mux_emac1_mcg2,
	msm_mux_emac1_mcg3,
	msm_mux_tsense_pwm2,
	msm_mux_qup16,
	msm_mux_tsense_pwm1,
	msm_mux_atest_usb3,
	msm_mux_pcie3b_clkreq,
	msm_mux_tb_trig,
	msm_mux_qup6,
	msm_mux_qup1,
	msm_mux_cci_timer9,
	msm_mux_emac0_mcg0,
	msm_mux_gcc_gp4,
	msm_mux_cci_timer4,
	msm_mux_emac0_mcg1,
	msm_mux_qdss_gpio14,
	msm_mux_cci_timer6,
	msm_mux_emac0_mcg2,
	msm_mux_qdss_gpio15,
	msm_mux_cci_timer7,
	msm_mux_emac0_mcg3,
	msm_mux_usb0_phy,
	msm_mux_cci_timer8,
	msm_mux_gcc_gp5,
	msm_mux_usb0_sbtx,
	msm_mux_usb0_sbrx,
	msm_mux_rgmii_0,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_prng_rosc0,
	msm_mux_cri_trng,
	msm_mux_prng_rosc1,
	msm_mux_prng_rosc2,
	msm_mux_prng_rosc3,
	msm_mux_phase_flag12,
	msm_mux_phase_flag11,
	msm_mux_phase_flag10,
	msm_mux_phase_flag9,
	msm_mux_phase_flag19,
	msm_mux_hs1_mi2s,
	msm_mux_mi2s2_sck,
	msm_mux_mi2s2_ws,
	msm_mux_mi2s2_data0,
	msm_mux_ddr_pxi5,
	msm_mux_mi2s2_data1,
	msm_mux_emac1_dll0,
	msm_mux_emac0_dll,
	msm_mux_ddr_pxi4,
	msm_mux_mi2s_mclk2,
	msm_mux_emac1_dll1,
	msm_mux_phase_flag8,
	msm_mux_lpass_slimbus,
	msm_mux_mi2s1_sck,
	msm_mux_phase_flag7,
	msm_mux_mi2s1_ws,
	msm_mux_phase_flag6,
	msm_mux_mi2s1_data0,
	msm_mux_phase_flag5,
	msm_mux_mi2s1_data1,
	msm_mux_phase_flag4,
	msm_mux_hs3_mi2s,
	msm_mux_phase_flag3,
	msm_mux_phase_flag2,
	msm_mux_phase_flag1,
	msm_mux_phase_flag0,
	msm_mux_atest_usb33,
	msm_mux_atest_usb32,
	msm_mux_qspi0_clk,
	msm_mux_sdc4_clk,
	msm_mux_atest_usb23,
	msm_mux_qspi0_cs,
	msm_mux_sdc4_cmd,
	msm_mux_atest_usb13,
	msm_mux_qspi00,
	msm_mux_sdc40,
	msm_mux_atest_usb11,
	msm_mux_qspi02,
	msm_mux_sdc42,
	msm_mux_atest_usb02,
	msm_mux_qspi03,
	msm_mux_sdc43,
	msm_mux_atest_usb01,
	msm_mux_ddr_pxi6,
	msm_mux_mi2s_mclk1,
	msm_mux_audio_ref,
	msm_mux_phase_flag23,
	msm_mux_qdss_gpio12,
	msm_mux_qup20,
	msm_mux_phase_flag22,
	msm_mux_vsense_trigger,
	msm_mux_qup21,
	msm_mux_phase_flag21,
	msm_mux_qup22,
	msm_mux_phase_flag20,
	msm_mux_pll_bist,
	msm_mux_pll_clk,
	msm_mux_phase_flag18,
	msm_mux_phase_flag17,
	msm_mux_phase_flag16,
	msm_mux_phase_flag15,
	msm_mux_hs2_mi2s,
	msm_mux_phase_flag14,
	msm_mux_phase_flag13,
	msm_mux_mi2s0_sck,
	msm_mux_phase_flag31,
	msm_mux_mi2s0_ws,
	msm_mux_phase_flag30,
	msm_mux_mi2s0_data0,
	msm_mux_phase_flag29,
	msm_mux_mi2s0_data1,
	msm_mux_qdss_gpio13,
	msm_mux_rgmii_1,
	msm_mux_atest_usb31,
	msm_mux_atest_usb30,
	msm_mux_tgu_ch0,
	msm_mux_atest_usb4,
	msm_mux_tgu_ch1,
	msm_mux_atest_usb43,
	msm_mux_tgu_ch2,
	msm_mux_atest_usb42,
	msm_mux_tgu_ch3,
	msm_mux_atest_usb41,
	msm_mux_tgu_ch4,
	msm_mux_atest_usb40,
	msm_mux_tgu_ch5,
	msm_mux_tgu_ch6,
	msm_mux_tgu_ch7,
	msm_mux_atest_usb5,
	msm_mux_qup4,
	msm_mux_qup5,
	msm_mux_atest_usb53,
	msm_mux_atest_usb52,
	msm_mux_atest_usb51,
	msm_mux_gcc_gp2,
	msm_mux_atest_usb50,
	msm_mux_gcc_gp3,
	msm_mux_qdss_gpio,
	msm_mux_qdss_gpio8,
	msm_mux_qdss_gpio9,
	msm_mux_cci_timer0,
	msm_mux_gcc_gp1,
	msm_mux_qdss_gpio10,
	msm_mux_cci_timer1,
	msm_mux_qdss_gpio11,
	msm_mux_ddr_pxi3,
	msm_mux_qup2,
	msm_mux_atest_usb20,
	msm_mux_ddr_pxi0,
	msm_mux_atest_usb00,
	msm_mux_cmu_rng,
	msm_mux_ddr_pxi2,
	msm_mux_ddr_pxi1,
	msm_mux_qup7,
	msm_mux_dbg_out,
	msm_mux_emac0_phy,
	msm_mux_emac0_ptp,
	msm_mux_sd_write,
	msm_mux_atest_usb1,
	msm_mux_atest_usb0,
	msm_mux_usb0_usb4,
	msm_mux_phase_flag28,
	msm_mux_atest_char0,
	msm_mux_qup0,
	msm_mux_qup3,
	msm_mux_atest_usb12,
	msm_mux_ddr_pxi7,
	msm_mux_atest_usb22,
	msm_mux_atest_usb21,
	msm_mux_cci_timer5,
	msm_mux_atest_char2,
	msm_mux_pcie4_clkreq,
	msm_mux_jitter_bist,
	msm_mux_atest_char1,
	msm_mux_pcie2a_clkreq,
	msm_mux_atest_char3,
	msm_mux_atest_char,
	msm_mux_pcie2b_clkreq,
	msm_mux_phase_flag27,
	msm_mux_phase_flag26,
	msm_mux_phase_flag25,
	msm_mux_phase_flag24,
	msm_mux_atest_usb2,
	msm_mux_atest_usb03,
	msm_mux_pcie3a_clkreq,
	msm_mux_NA,
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
static const char * const qup12_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const mdp0_vsync0_groups[] = {
	"gpio1",
};
static const char * const edp_hot_groups[] = {
	"gpio2", "gpio3", "gpio6", "gpio7",
};
static const char * const mdp0_vsync1_groups[] = {
	"gpio2",
};
static const char * const qdss_cti_groups[] = {
	"gpio3", "gpio4", "gpio7", "gpio21", "gpio30", "gpio30", "gpio31",
	"gpio31",
};
static const char * const qup14_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const ibi_i3c_groups[] = {
	"gpio4", "gpio5", "gpio36", "gpio37", "gpio128", "gpio129", "gpio154",
	"gpio155",
};
static const char * const cam_mclk_groups[] = {
	"gpio6", "gpio7", "gpio16", "gpio17", "gpio33", "gpio34", "gpio119",
	"gpio120",
};
static const char * const mdp_vsync_groups[] = {
	"gpio8", "gpio100", "gpio101",
};
static const char * const mdp0_vsync2_groups[] = {
	"gpio8",
};
static const char * const usb1_dp_groups[] = {
	"gpio9",
};
static const char * const mdp0_vsync3_groups[] = {
	"gpio9",
};
static const char * const cci_i2c_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13", "gpio113", "gpio114",
	"gpio115", "gpio116", "gpio117", "gpio118", "gpio123", "gpio124",
	"gpio145", "gpio146", "gpio164", "gpio165",
};
static const char * const mdp0_vsync4_groups[] = {
	"gpio10",
};
static const char * const qdss_gpio0_groups[] = {
	"gpio10", "gpio195",
};
static const char * const mdp0_vsync5_groups[] = {
	"gpio11",
};
static const char * const qdss_gpio1_groups[] = {
	"gpio11", "gpio196",
};
static const char * const mdp0_vsync6_groups[] = {
	"gpio12",
};
static const char * const qdss_gpio2_groups[] = {
	"gpio12", "gpio197",
};
static const char * const mdp0_vsync7_groups[] = {
	"gpio13",
};
static const char * const qdss_gpio3_groups[] = {
	"gpio13", "gpio198",
};
static const char * const cci_timer2_groups[] = {
	"gpio14",
};
static const char * const qdss_gpio4_groups[] = {
	"gpio14", "gpio201",
};
static const char * const cci_timer3_groups[] = {
	"gpio15",
};
static const char * const cci_async_groups[] = {
	"gpio15", "gpio119", "gpio120", "gpio160", "gpio161", "gpio167",
};
static const char * const qdss_gpio5_groups[] = {
	"gpio15", "gpio202",
};
static const char * const mdp0_vsync8_groups[] = {
	"gpio16",
};
static const char * const qdss_gpio6_groups[] = {
	"gpio16", "gpio206",
};
static const char * const mdp1_vsync0_groups[] = {
	"gpio17",
};
static const char * const qdss_gpio7_groups[] = {
	"gpio17", "gpio207",
};
static const char * const qup11_groups[] = {
	"gpio18", "gpio19", "gpio20", "gpio21",
};
static const char * const mdp1_vsync1_groups[] = {
	"gpio18",
};
static const char * const mdp1_vsync2_groups[] = {
	"gpio19",
};
static const char * const dp2_hot_groups[] = {
	"gpio20",
};
static const char * const mdp1_vsync3_groups[] = {
	"gpio20",
};
static const char * const usb0_dp_groups[] = {
	"gpio21",
};
static const char * const qup10_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25",
};
static const char * const usb2phy_ac_groups[] = {
	"gpio24", "gpio25", "gpio133", "gpio134", "gpio148", "gpio149",
};
static const char * const qup13_groups[] = {
	"gpio26", "gpio27", "gpio28", "gpio29",
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
static const char * const usb1_usb4_groups[] = {
	"gpio32",
};
static const char * const qup15_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
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
static const char * const qup9_groups[] = {
	"gpio41", "gpio42", "gpio43", "gpio44",
};
static const char * const ddr_bist_groups[] = {
	"gpio42", "gpio45", "gpio46", "gpio47",
};
static const char * const qup8_groups[] = {
	"gpio43", "gpio44", "gpio45", "gpio46",
};
static const char * const dp3_hot_groups[] = {
	"gpio45",
};
static const char * const usb1_phy_groups[] = {
	"gpio49",
};
static const char * const usb1_sbtx_groups[] = {
	"gpio51", "gpio52",
};
static const char * const usb1_sbrx_groups[] = {
	"gpio53",
};
static const char * const emac1_phy_groups[] = {
	"gpio54",
};
static const char * const emac1_ptp_groups[] = {
	"gpio55", "gpio55", "gpio56", "gpio56", "gpio93", "gpio93", "gpio94",
	"gpio94", "gpio95", "gpio95", "gpio96", "gpio96",
};
static const char * const qup19_groups[] = {
	"gpio55", "gpio56", "gpio57", "gpio58",
};
static const char * const emac1_mcg0_groups[] = {
	"gpio57",
};
static const char * const emac1_mcg1_groups[] = {
	"gpio58",
};
static const char * const qup23_groups[] = {
	"gpio59", "gpio60", "gpio61", "gpio62",
};
static const char * const qup17_groups[] = {
	"gpio61", "gpio62", "gpio63", "gpio64",
};
static const char * const tsense_pwm4_groups[] = {
	"gpio65",
};
static const char * const qup18_groups[] = {
	"gpio66", "gpio67", "gpio68", "gpio69",
};
static const char * const tsense_pwm3_groups[] = {
	"gpio67",
};
static const char * const emac1_mcg2_groups[] = {
	"gpio68",
};
static const char * const emac1_mcg3_groups[] = {
	"gpio69",
};
static const char * const tsense_pwm2_groups[] = {
	"gpio69",
};
static const char * const qup16_groups[] = {
	"gpio70", "gpio71", "gpio72", "gpio73",
};
static const char * const tsense_pwm1_groups[] = {
	"gpio70",
};
static const char * const atest_usb3_groups[] = {
	"gpio71",
};
static const char * const pcie3b_clkreq_groups[] = {
	"gpio152",
};
static const char * const tb_trig_groups[] = {
	"gpio153", "gpio157",
};
static const char * const qup6_groups[] = {
	"gpio154", "gpio155", "gpio156", "gpio157",
};
static const char * const qup1_groups[] = {
	"gpio158", "gpio159", "gpio160", "gpio161",
};
static const char * const cci_timer9_groups[] = {
	"gpio160",
};
static const char * const emac0_mcg0_groups[] = {
	"gpio160",
};
static const char * const gcc_gp4_groups[] = {
	"gpio160", "gpio162",
};
static const char * const cci_timer4_groups[] = {
	"gpio161",
};
static const char * const emac0_mcg1_groups[] = {
	"gpio161",
};
static const char * const qdss_gpio14_groups[] = {
	"gpio161", "gpio222",
};
static const char * const cci_timer6_groups[] = {
	"gpio162",
};
static const char * const emac0_mcg2_groups[] = {
	"gpio162",
};
static const char * const qdss_gpio15_groups[] = {
	"gpio162", "gpio223",
};
static const char * const cci_timer7_groups[] = {
	"gpio163",
};
static const char * const emac0_mcg3_groups[] = {
	"gpio163",
};
static const char * const usb0_phy_groups[] = {
	"gpio166",
};
static const char * const cci_timer8_groups[] = {
	"gpio167",
};
static const char * const gcc_gp5_groups[] = {
	"gpio167", "gpio168",
};
static const char * const usb0_sbtx_groups[] = {
	"gpio168", "gpio169",
};
static const char * const usb0_sbrx_groups[] = {
	"gpio170",
};
static const char * const rgmii_0_groups[] = {
	"gpio175", "gpio176", "gpio177", "gpio178", "gpio179", "gpio180",
	"gpio181", "gpio182", "gpio183", "gpio184", "gpio185", "gpio186",
	"gpio187", "gpio188",
};
static const char * const cri_trng0_groups[] = {
	"gpio187",
};
static const char * const cri_trng1_groups[] = {
	"gpio188",
};
static const char * const prng_rosc0_groups[] = {
	"gpio189",
};
static const char * const cri_trng_groups[] = {
	"gpio190",
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
static const char * const phase_flag12_groups[] = {
	"gpio195",
};
static const char * const phase_flag11_groups[] = {
	"gpio196",
};
static const char * const phase_flag10_groups[] = {
	"gpio197",
};
static const char * const phase_flag9_groups[] = {
	"gpio198",
};
static const char * const phase_flag19_groups[] = {
	"gpio202",
};
static const char * const hs1_mi2s_groups[] = {
	"gpio208", "gpio209", "gpio210", "gpio211",
};
static const char * const mi2s2_sck_groups[] = {
	"gpio212",
};
static const char * const mi2s2_ws_groups[] = {
	"gpio213",
};
static const char * const mi2s2_data0_groups[] = {
	"gpio214",
};
static const char * const ddr_pxi5_groups[] = {
	"gpio214", "gpio215",
};
static const char * const mi2s2_data1_groups[] = {
	"gpio215",
};
static const char * const emac1_dll0_groups[] = {
	"gpio215",
};
static const char * const emac0_dll_groups[] = {
	"gpio216", "gpio217",
};
static const char * const ddr_pxi4_groups[] = {
	"gpio216", "gpio217",
};
static const char * const mi2s_mclk2_groups[] = {
	"gpio217",
};
static const char * const emac1_dll1_groups[] = {
	"gpio218",
};
static const char * const phase_flag8_groups[] = {
	"gpio219",
};
static const char * const lpass_slimbus_groups[] = {
	"gpio220", "gpio221",
};
static const char * const mi2s1_sck_groups[] = {
	"gpio220",
};
static const char * const phase_flag7_groups[] = {
	"gpio220",
};
static const char * const mi2s1_ws_groups[] = {
	"gpio221",
};
static const char * const phase_flag6_groups[] = {
	"gpio221",
};
static const char * const mi2s1_data0_groups[] = {
	"gpio222",
};
static const char * const phase_flag5_groups[] = {
	"gpio222",
};
static const char * const mi2s1_data1_groups[] = {
	"gpio223",
};
static const char * const phase_flag4_groups[] = {
	"gpio223",
};
static const char * const hs3_mi2s_groups[] = {
	"gpio224", "gpio225", "gpio226", "gpio227",
};
static const char * const phase_flag3_groups[] = {
	"gpio224",
};
static const char * const phase_flag2_groups[] = {
	"gpio225",
};
static const char * const phase_flag1_groups[] = {
	"gpio226",
};
static const char * const phase_flag0_groups[] = {
	"gpio227",
};
static const char * const atest_usb33_groups[] = {
	"gpio72",
};
static const char * const atest_usb32_groups[] = {
	"gpio73",
};
static const char * const qspi0_clk_groups[] = {
	"gpio74",
};
static const char * const sdc4_clk_groups[] = {
	"gpio74",
};
static const char * const atest_usb23_groups[] = {
	"gpio74",
};
static const char * const qspi0_cs_groups[] = {
	"gpio75", "gpio81",
};
static const char * const sdc4_cmd_groups[] = {
	"gpio75",
};
static const char * const atest_usb13_groups[] = {
	"gpio75",
};
static const char * const qspi00_groups[] = {
	"gpio76",
};
static const char * const sdc40_groups[] = {
	"gpio76",
};
static const char * const atest_usb11_groups[] = {
	"gpio76",
};
static const char * const qspi02_groups[] = {
	"gpio78",
};
static const char * const sdc42_groups[] = {
	"gpio78",
};
static const char * const atest_usb02_groups[] = {
	"gpio78",
};
static const char * const qspi03_groups[] = {
	"gpio79",
};
static const char * const sdc43_groups[] = {
	"gpio79",
};
static const char * const atest_usb01_groups[] = {
	"gpio79",
};
static const char * const ddr_pxi6_groups[] = {
	"gpio79", "gpio218",
};
static const char * const mi2s_mclk1_groups[] = {
	"gpio80", "gpio216",
};
static const char * const audio_ref_groups[] = {
	"gpio80",
};
static const char * const phase_flag23_groups[] = {
	"gpio80",
};
static const char * const qdss_gpio12_groups[] = {
	"gpio80", "gpio121",
};
static const char * const qup20_groups[] = {
	"gpio87", "gpio88", "gpio89", "gpio90", "gpio91",
	"gpio92", "gpio110",
};
static const char * const phase_flag22_groups[] = {
	"gpio81",
};
static const char * const vsense_trigger_groups[] = {
	"gpio81",
};
static const char * const qup21_groups[] = {
	"gpio81", "gpio82", "gpio83", "gpio84",
};
static const char * const phase_flag21_groups[] = {
	"gpio82",
};
static const char * const qup22_groups[] = {
	"gpio83", "gpio84", "gpio85", "gpio86",
};
static const char * const phase_flag20_groups[] = {
	"gpio83",
};
static const char * const pll_bist_groups[] = {
	"gpio84",
};
static const char * const pll_clk_groups[] = {
	"gpio84", "gpio86",
};
static const char * const phase_flag18_groups[] = {
	"gpio87",
};
static const char * const phase_flag17_groups[] = {
	"gpio88",
};
static const char * const phase_flag16_groups[] = {
	"gpio89",
};
static const char * const phase_flag15_groups[] = {
	"gpio90",
};
static const char * const hs2_mi2s_groups[] = {
	"gpio91", "gpio92", "gpio218", "gpio219",
};
static const char * const phase_flag14_groups[] = {
	"gpio91",
};
static const char * const phase_flag13_groups[] = {
	"gpio92",
};
static const char * const mi2s0_sck_groups[] = {
	"gpio93",
};
static const char * const phase_flag31_groups[] = {
	"gpio93",
};
static const char * const mi2s0_ws_groups[] = {
	"gpio94",
};
static const char * const phase_flag30_groups[] = {
	"gpio94",
};
static const char * const mi2s0_data0_groups[] = {
	"gpio95",
};
static const char * const phase_flag29_groups[] = {
	"gpio95",
};
static const char * const mi2s0_data1_groups[] = {
	"gpio96",
};
static const char * const qdss_gpio13_groups[] = {
	"gpio96", "gpio122",
};
static const char * const rgmii_1_groups[] = {
	"gpio97", "gpio98", "gpio99", "gpio100", "gpio101", "gpio102",
	"gpio103", "gpio104", "gpio105", "gpio106", "gpio107", "gpio108",
	"gpio109", "gpio110",
};
static const char * const atest_usb31_groups[] = {
	"gpio97",
};
static const char * const atest_usb30_groups[] = {
	"gpio98",
};
static const char * const tgu_ch0_groups[] = {
	"gpio101",
};
static const char * const atest_usb4_groups[] = {
	"gpio101",
};
static const char * const tgu_ch1_groups[] = {
	"gpio102",
};
static const char * const atest_usb43_groups[] = {
	"gpio102",
};
static const char * const tgu_ch2_groups[] = {
	"gpio103",
};
static const char * const atest_usb42_groups[] = {
	"gpio103",
};
static const char * const tgu_ch3_groups[] = {
	"gpio104",
};
static const char * const atest_usb41_groups[] = {
	"gpio104",
};
static const char * const tgu_ch4_groups[] = {
	"gpio105",
};
static const char * const atest_usb40_groups[] = {
	"gpio105",
};
static const char * const tgu_ch5_groups[] = {
	"gpio106",
};
static const char * const tgu_ch6_groups[] = {
	"gpio107",
};
static const char * const tgu_ch7_groups[] = {
	"gpio108",
};
static const char * const atest_usb5_groups[] = {
	"gpio110",
};
static const char * const qup4_groups[] = {
	"gpio111", "gpio112", "gpio171", "gpio172", "gpio173", "gpio174",
	"gpio175",
};
static const char * const qup5_groups[] = {
	"gpio111", "gpio112", "gpio145", "gpio146",
};
static const char * const atest_usb53_groups[] = {
	"gpio111",
};
static const char * const atest_usb52_groups[] = {
	"gpio112",
};
static const char * const atest_usb51_groups[] = {
	"gpio113",
};
static const char * const gcc_gp2_groups[] = {
	"gpio114", "gpio120",
};
static const char * const atest_usb50_groups[] = {
	"gpio114",
};
static const char * const gcc_gp3_groups[] = {
	"gpio115", "gpio139",
};
static const char * const qdss_gpio_groups[] = {
	"gpio115", "gpio116", "gpio216", "gpio217",
};
static const char * const qdss_gpio8_groups[] = {
	"gpio117", "gpio212",
};
static const char * const qdss_gpio9_groups[] = {
	"gpio118", "gpio213",
};
static const char * const cci_timer0_groups[] = {
	"gpio119",
};
static const char * const gcc_gp1_groups[] = {
	"gpio119", "gpio149",
};
static const char * const qdss_gpio10_groups[] = {
	"gpio119", "gpio214",
};
static const char * const cci_timer1_groups[] = {
	"gpio120",
};
static const char * const qdss_gpio11_groups[] = {
	"gpio120", "gpio215",
};
static const char * const ddr_pxi3_groups[] = {
	"gpio120", "gpio137",
};
static const char * const qup2_groups[] = {
	"gpio121", "gpio122", "gpio123", "gpio124",
};
static const char * const atest_usb20_groups[] = {
	"gpio121",
};
static const char * const ddr_pxi0_groups[] = {
	"gpio121", "gpio126",
};
static const char * const atest_usb00_groups[] = {
	"gpio122",
};
static const char * const cmu_rng_groups[] = {
	"gpio123", "gpio124", "gpio126", "gpio136",
};
static const char * const ddr_pxi2_groups[] = {
	"gpio123", "gpio138",
};
static const char * const ddr_pxi1_groups[] = {
	"gpio124", "gpio125",
};
static const char * const qup7_groups[] = {
	"gpio125", "gpio126", "gpio128", "gpio129",
};
static const char * const dbg_out_groups[] = {
	"gpio125",
};
static const char * const emac0_phy_groups[] = {
	"gpio127",
};
static const char * const emac0_ptp_groups[] = {
	"gpio130", "gpio130", "gpio131", "gpio131", "gpio156", "gpio156",
	"gpio157", "gpio157", "gpio158", "gpio158", "gpio159", "gpio159",
};
static const char * const sd_write_groups[] = {
	"gpio130",
};
static const char * const atest_usb1_groups[] = {
	"gpio130",
};
static const char * const atest_usb0_groups[] = {
	"gpio131",
};
static const char * const usb0_usb4_groups[] = {
	"gpio132",
};
static const char * const phase_flag28_groups[] = {
	"gpio132",
};
static const char * const atest_char0_groups[] = {
	"gpio134",
};
static const char * const qup0_groups[] = {
	"gpio135", "gpio136", "gpio137", "gpio138",
};
static const char * const qup3_groups[] = {
	"gpio135", "gpio136", "gpio137", "gpio138",
};
static const char * const atest_usb12_groups[] = {
	"gpio135",
};
static const char * const ddr_pxi7_groups[] = {
	"gpio135", "gpio136",
};
static const char * const atest_usb22_groups[] = {
	"gpio137",
};
static const char * const atest_usb21_groups[] = {
	"gpio138",
};
static const char * const cci_timer5_groups[] = {
	"gpio139",
};
static const char * const atest_char2_groups[] = {
	"gpio139",
};
static const char * const pcie4_clkreq_groups[] = {
	"gpio140",
};
static const char * const jitter_bist_groups[] = {
	"gpio140",
};
static const char * const atest_char1_groups[] = {
	"gpio140",
};
static const char * const pcie2a_clkreq_groups[] = {
	"gpio142",
};
static const char * const atest_char3_groups[] = {
	"gpio142",
};
static const char * const atest_char_groups[] = {
	"gpio143",
};
static const char * const pcie2b_clkreq_groups[] = {
	"gpio144",
};
static const char * const phase_flag27_groups[] = {
	"gpio144",
};
static const char * const phase_flag26_groups[] = {
	"gpio145",
};
static const char * const phase_flag25_groups[] = {
	"gpio146",
};
static const char * const phase_flag24_groups[] = {
	"gpio147",
};
static const char * const atest_usb2_groups[] = {
	"gpio148",
};
static const char * const atest_usb03_groups[] = {
	"gpio149",
};
static const char * const pcie3a_clkreq_groups[] = {
	"gpio150",
};

static const struct msm_function direwolf_functions[] = {
	FUNCTION(gpio),
	FUNCTION(qup12),
	FUNCTION(mdp0_vsync0),
	FUNCTION(edp_hot),
	FUNCTION(mdp0_vsync1),
	FUNCTION(qdss_cti),
	FUNCTION(qup14),
	FUNCTION(ibi_i3c),
	FUNCTION(cam_mclk),
	FUNCTION(mdp_vsync),
	FUNCTION(mdp0_vsync2),
	FUNCTION(usb1_dp),
	FUNCTION(mdp0_vsync3),
	FUNCTION(cci_i2c),
	FUNCTION(mdp0_vsync4),
	FUNCTION(qdss_gpio0),
	FUNCTION(mdp0_vsync5),
	FUNCTION(qdss_gpio1),
	FUNCTION(mdp0_vsync6),
	FUNCTION(qdss_gpio2),
	FUNCTION(mdp0_vsync7),
	FUNCTION(qdss_gpio3),
	FUNCTION(cci_timer2),
	FUNCTION(qdss_gpio4),
	FUNCTION(cci_timer3),
	FUNCTION(cci_async),
	FUNCTION(qdss_gpio5),
	FUNCTION(mdp0_vsync8),
	FUNCTION(qdss_gpio6),
	FUNCTION(mdp1_vsync0),
	FUNCTION(qdss_gpio7),
	FUNCTION(qup11),
	FUNCTION(mdp1_vsync1),
	FUNCTION(mdp1_vsync2),
	FUNCTION(dp2_hot),
	FUNCTION(mdp1_vsync3),
	FUNCTION(usb0_dp),
	FUNCTION(qup10),
	FUNCTION(usb2phy_ac),
	FUNCTION(qup13),
	FUNCTION(edp0_lcd),
	FUNCTION(edp1_lcd),
	FUNCTION(edp2_lcd),
	FUNCTION(edp3_lcd),
	FUNCTION(usb1_usb4),
	FUNCTION(qup15),
	FUNCTION(mdp1_vsync4),
	FUNCTION(mdp1_vsync5),
	FUNCTION(mdp1_vsync6),
	FUNCTION(mdp1_vsync7),
	FUNCTION(mdp1_vsync8),
	FUNCTION(qup9),
	FUNCTION(ddr_bist),
	FUNCTION(qup8),
	FUNCTION(dp3_hot),
	FUNCTION(usb1_phy),
	FUNCTION(usb1_sbtx),
	FUNCTION(usb1_sbrx),
	FUNCTION(emac1_phy),
	FUNCTION(emac1_ptp),
	FUNCTION(qup19),
	FUNCTION(emac1_mcg0),
	FUNCTION(emac1_mcg1),
	FUNCTION(qup23),
	FUNCTION(qup17),
	FUNCTION(tsense_pwm4),
	FUNCTION(qup18),
	FUNCTION(tsense_pwm3),
	FUNCTION(emac1_mcg2),
	FUNCTION(emac1_mcg3),
	FUNCTION(tsense_pwm2),
	FUNCTION(qup16),
	FUNCTION(tsense_pwm1),
	FUNCTION(atest_usb3),
	FUNCTION(pcie3b_clkreq),
	FUNCTION(tb_trig),
	FUNCTION(qup6),
	FUNCTION(qup1),
	FUNCTION(cci_timer9),
	FUNCTION(emac0_mcg0),
	FUNCTION(gcc_gp4),
	FUNCTION(cci_timer4),
	FUNCTION(emac0_mcg1),
	FUNCTION(qdss_gpio14),
	FUNCTION(cci_timer6),
	FUNCTION(emac0_mcg2),
	FUNCTION(qdss_gpio15),
	FUNCTION(cci_timer7),
	FUNCTION(emac0_mcg3),
	FUNCTION(usb0_phy),
	FUNCTION(cci_timer8),
	FUNCTION(gcc_gp5),
	FUNCTION(usb0_sbtx),
	FUNCTION(usb0_sbrx),
	FUNCTION(rgmii_0),
	FUNCTION(cri_trng0),
	FUNCTION(cri_trng1),
	FUNCTION(prng_rosc0),
	FUNCTION(cri_trng),
	FUNCTION(prng_rosc1),
	FUNCTION(prng_rosc2),
	FUNCTION(prng_rosc3),
	FUNCTION(phase_flag12),
	FUNCTION(phase_flag11),
	FUNCTION(phase_flag10),
	FUNCTION(phase_flag9),
	FUNCTION(phase_flag19),
	FUNCTION(hs1_mi2s),
	FUNCTION(mi2s2_sck),
	FUNCTION(mi2s2_ws),
	FUNCTION(mi2s2_data0),
	FUNCTION(ddr_pxi5),
	FUNCTION(mi2s2_data1),
	FUNCTION(emac1_dll0),
	FUNCTION(emac0_dll),
	FUNCTION(ddr_pxi4),
	FUNCTION(mi2s_mclk2),
	FUNCTION(emac1_dll1),
	FUNCTION(phase_flag8),
	FUNCTION(lpass_slimbus),
	FUNCTION(mi2s1_sck),
	FUNCTION(phase_flag7),
	FUNCTION(mi2s1_ws),
	FUNCTION(phase_flag6),
	FUNCTION(mi2s1_data0),
	FUNCTION(phase_flag5),
	FUNCTION(mi2s1_data1),
	FUNCTION(phase_flag4),
	FUNCTION(hs3_mi2s),
	FUNCTION(phase_flag3),
	FUNCTION(phase_flag2),
	FUNCTION(phase_flag1),
	FUNCTION(phase_flag0),
	FUNCTION(atest_usb33),
	FUNCTION(atest_usb32),
	FUNCTION(qspi0_clk),
	FUNCTION(sdc4_clk),
	FUNCTION(atest_usb23),
	FUNCTION(qspi0_cs),
	FUNCTION(sdc4_cmd),
	FUNCTION(atest_usb13),
	FUNCTION(qspi00),
	FUNCTION(sdc40),
	FUNCTION(atest_usb11),
	FUNCTION(qspi02),
	FUNCTION(sdc42),
	FUNCTION(atest_usb02),
	FUNCTION(qspi03),
	FUNCTION(sdc43),
	FUNCTION(atest_usb01),
	FUNCTION(ddr_pxi6),
	FUNCTION(mi2s_mclk1),
	FUNCTION(audio_ref),
	FUNCTION(phase_flag23),
	FUNCTION(qdss_gpio12),
	FUNCTION(qup20),
	FUNCTION(phase_flag22),
	FUNCTION(vsense_trigger),
	FUNCTION(qup21),
	FUNCTION(phase_flag21),
	FUNCTION(qup22),
	FUNCTION(phase_flag20),
	FUNCTION(pll_bist),
	FUNCTION(pll_clk),
	FUNCTION(phase_flag18),
	FUNCTION(phase_flag17),
	FUNCTION(phase_flag16),
	FUNCTION(phase_flag15),
	FUNCTION(hs2_mi2s),
	FUNCTION(phase_flag14),
	FUNCTION(phase_flag13),
	FUNCTION(mi2s0_sck),
	FUNCTION(phase_flag31),
	FUNCTION(mi2s0_ws),
	FUNCTION(phase_flag30),
	FUNCTION(mi2s0_data0),
	FUNCTION(phase_flag29),
	FUNCTION(mi2s0_data1),
	FUNCTION(qdss_gpio13),
	FUNCTION(rgmii_1),
	FUNCTION(atest_usb31),
	FUNCTION(atest_usb30),
	FUNCTION(tgu_ch0),
	FUNCTION(atest_usb4),
	FUNCTION(tgu_ch1),
	FUNCTION(atest_usb43),
	FUNCTION(tgu_ch2),
	FUNCTION(atest_usb42),
	FUNCTION(tgu_ch3),
	FUNCTION(atest_usb41),
	FUNCTION(tgu_ch4),
	FUNCTION(atest_usb40),
	FUNCTION(tgu_ch5),
	FUNCTION(tgu_ch6),
	FUNCTION(tgu_ch7),
	FUNCTION(atest_usb5),
	FUNCTION(qup4),
	FUNCTION(qup5),
	FUNCTION(atest_usb53),
	FUNCTION(atest_usb52),
	FUNCTION(atest_usb51),
	FUNCTION(gcc_gp2),
	FUNCTION(atest_usb50),
	FUNCTION(gcc_gp3),
	FUNCTION(qdss_gpio),
	FUNCTION(qdss_gpio8),
	FUNCTION(qdss_gpio9),
	FUNCTION(cci_timer0),
	FUNCTION(gcc_gp1),
	FUNCTION(qdss_gpio10),
	FUNCTION(cci_timer1),
	FUNCTION(qdss_gpio11),
	FUNCTION(ddr_pxi3),
	FUNCTION(qup2),
	FUNCTION(atest_usb20),
	FUNCTION(ddr_pxi0),
	FUNCTION(atest_usb00),
	FUNCTION(cmu_rng),
	FUNCTION(ddr_pxi2),
	FUNCTION(ddr_pxi1),
	FUNCTION(qup7),
	FUNCTION(dbg_out),
	FUNCTION(emac0_phy),
	FUNCTION(emac0_ptp),
	FUNCTION(sd_write),
	FUNCTION(atest_usb1),
	FUNCTION(atest_usb0),
	FUNCTION(usb0_usb4),
	FUNCTION(phase_flag28),
	FUNCTION(atest_char0),
	FUNCTION(qup0),
	FUNCTION(qup3),
	FUNCTION(atest_usb12),
	FUNCTION(ddr_pxi7),
	FUNCTION(atest_usb22),
	FUNCTION(atest_usb21),
	FUNCTION(cci_timer5),
	FUNCTION(atest_char2),
	FUNCTION(pcie4_clkreq),
	FUNCTION(jitter_bist),
	FUNCTION(atest_char1),
	FUNCTION(pcie2a_clkreq),
	FUNCTION(atest_char3),
	FUNCTION(atest_char),
	FUNCTION(pcie2b_clkreq),
	FUNCTION(phase_flag27),
	FUNCTION(phase_flag26),
	FUNCTION(phase_flag25),
	FUNCTION(phase_flag24),
	FUNCTION(atest_usb2),
	FUNCTION(atest_usb03),
	FUNCTION(pcie3a_clkreq),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup direwolf_groups[] = {
	[0] = PINGROUP(0, qup12, NA, NA, NA, NA, NA, NA, NA, NA),
	[1] = PINGROUP(1, qup12, mdp0_vsync0, NA, NA, NA, NA, NA, NA,
		       NA),
	[2] = PINGROUP(2, edp_hot, qup12, mdp0_vsync1, NA, NA, NA, NA,
		       NA, NA),
	[3] = PINGROUP(3, edp_hot, qup12, qdss_cti, NA, NA, NA, NA, NA,
		       NA),
	[4] = PINGROUP(4, qup14, ibi_i3c, qdss_cti, NA, NA, NA, NA, NA,
		       NA),
	[5] = PINGROUP(5, qup14, ibi_i3c, NA, NA, NA, NA, NA, NA, NA),
	[6] = PINGROUP(6, edp_hot, qup14, cam_mclk, NA, NA, NA, NA, NA,
		       NA),
	[7] = PINGROUP(7, edp_hot, qup14, qdss_cti, cam_mclk, NA, NA,
		       NA, NA, NA),
	[8] = PINGROUP(8, mdp_vsync, mdp0_vsync2, NA, NA, NA, NA, NA,
		       NA, NA),
	[9] = PINGROUP(9, usb1_dp, mdp0_vsync3, NA, NA, NA, NA, NA, NA,
		       NA),
	[10] = PINGROUP(10, cci_i2c, mdp0_vsync4, NA, qdss_gpio0, NA,
			NA, NA, NA, NA),
	[11] = PINGROUP(11, cci_i2c, mdp0_vsync5, NA, qdss_gpio1, NA,
			NA, NA, NA, NA),
	[12] = PINGROUP(12, cci_i2c, mdp0_vsync6, NA, qdss_gpio2, NA,
			NA, NA, NA, NA),
	[13] = PINGROUP(13, cci_i2c, mdp0_vsync7, NA, qdss_gpio3, NA,
			NA, NA, NA, NA),
	[14] = PINGROUP(14, cci_timer2, qdss_gpio4, NA, NA, NA, NA, NA,
			NA, NA),
	[15] = PINGROUP(15, cci_timer3, cci_async, NA, qdss_gpio5, NA,
			NA, NA, NA, NA),
	[16] = PINGROUP(16, cam_mclk, mdp0_vsync8, NA, qdss_gpio6, NA,
			NA, NA, NA, NA),
	[17] = PINGROUP(17, cam_mclk, mdp1_vsync0, NA, qdss_gpio7, NA,
			NA, NA, NA, NA),
	[18] = PINGROUP(18, qup11, mdp1_vsync1, NA, NA, NA, NA, NA, NA,
			NA),
	[19] = PINGROUP(19, qup11, mdp1_vsync2, NA, NA, NA, NA, NA, NA,
			NA),
	[20] = PINGROUP(20, qup11, dp2_hot, mdp1_vsync3, NA, NA, NA, NA,
			NA, NA),
	[21] = PINGROUP(21, qup11, usb0_dp, qdss_cti, NA, NA, NA, NA,
			NA, NA),
	[22] = PINGROUP(22, qup10, NA, NA, NA, NA, NA, NA, NA, NA),
	[23] = PINGROUP(23, qup10, NA, NA, NA, NA, NA, NA, NA, NA),
	[24] = PINGROUP(24, qup10, usb2phy_ac, NA, NA, NA, NA, NA, NA,
			NA),
	[25] = PINGROUP(25, qup10, usb2phy_ac, NA, NA, NA, NA, NA, NA,
			NA),
	[26] = PINGROUP(26, qup13, edp0_lcd, NA, NA, NA, NA, NA, NA, NA),
	[27] = PINGROUP(27, qup13, edp1_lcd, NA, NA, NA, NA, NA, NA, NA),
	[28] = PINGROUP(28, qup13, edp2_lcd, NA, NA, NA, NA, NA, NA, NA),
	[29] = PINGROUP(29, qup13, edp3_lcd, NA, NA, NA, NA, NA, NA, NA),
	[30] = PINGROUP(30, qdss_cti, qdss_cti, NA, NA, NA, NA, NA, NA,
			NA),
	[31] = PINGROUP(31, qdss_cti, qdss_cti, NA, NA, NA, NA, NA, NA,
			NA),
	[32] = PINGROUP(32, usb1_usb4, NA, NA, NA, NA, NA, NA, NA, NA),
	[33] = PINGROUP(33, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA),
	[34] = PINGROUP(34, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA),
	[35] = PINGROUP(35, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[36] = PINGROUP(36, qup15, ibi_i3c, mdp1_vsync4, NA, NA, NA, NA,
			NA, NA),
	[37] = PINGROUP(37, qup15, ibi_i3c, mdp1_vsync5, NA, NA, NA, NA,
			NA, NA),
	[38] = PINGROUP(38, qup15, mdp1_vsync6, NA, NA, NA, NA, NA, NA,
			NA),
	[39] = PINGROUP(39, qup15, mdp1_vsync7, NA, NA, NA, NA, NA, NA,
			NA),
	[40] = PINGROUP(40, mdp1_vsync8, NA, NA, NA, NA, NA, NA, NA, NA),
	[41] = PINGROUP(41, qup9, NA, NA, NA, NA, NA, NA, NA, NA),
	[42] = PINGROUP(42, qup9, ddr_bist, NA, NA, NA, NA, NA, NA, NA),
	[43] = PINGROUP(43, qup8, qup9, NA, NA, NA, NA, NA, NA, NA),
	[44] = PINGROUP(44, qup8, qup9, NA, NA, NA, NA, NA, NA, NA),
	[45] = PINGROUP(45, qup8, dp3_hot, ddr_bist, NA, NA, NA, NA, NA,
			NA),
	[46] = PINGROUP(46, qup8, ddr_bist, NA, NA, NA, NA, NA, NA, NA),
	[47] = PINGROUP(47, ddr_bist, NA, NA, NA, NA, NA, NA, NA, NA),
	[48] = PINGROUP(48, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[49] = PINGROUP(49, usb1_phy, NA, NA, NA, NA, NA, NA, NA, NA),
	[50] = PINGROUP(50, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[51] = PINGROUP(51, usb1_sbtx, NA, NA, NA, NA, NA, NA, NA, NA),
	[52] = PINGROUP(52, usb1_sbtx, NA, NA, NA, NA, NA, NA, NA, NA),
	[53] = PINGROUP(53, usb1_sbrx, NA, NA, NA, NA, NA, NA, NA, NA),
	[54] = PINGROUP(54, emac1_phy, NA, NA, NA, NA, NA, NA, NA, NA),
	[55] = PINGROUP(55, emac1_ptp, emac1_ptp, qup19, NA, NA, NA, NA,
			NA, NA),
	[56] = PINGROUP(56, emac1_ptp, emac1_ptp, qup19, NA, NA, NA, NA,
			NA, NA),
	[57] = PINGROUP(57, qup19, emac1_mcg0, NA, NA, NA, NA, NA, NA,
			NA),
	[58] = PINGROUP(58, qup19, emac1_mcg1, NA, NA, NA, NA, NA, NA,
			NA),
	[59] = PINGROUP(59, qup23, NA, NA, NA, NA, NA, NA, NA, NA),
	[60] = PINGROUP(60, qup23, NA, NA, NA, NA, NA, NA, NA, NA),
	[61] = PINGROUP(61, qup23, qup17, NA, NA, NA, NA, NA, NA, NA),
	[62] = PINGROUP(62, qup23, qup17, NA, NA, NA, NA, NA, NA, NA),
	[63] = PINGROUP(63, qup17, NA, NA, NA, NA, NA, NA, NA, NA),
	[64] = PINGROUP(64, qup17, NA, NA, NA, NA, NA, NA, NA, NA),
	[65] = PINGROUP(65, tsense_pwm4, NA, NA, NA, NA, NA, NA, NA, NA),
	[66] = PINGROUP(66, qup18, NA, NA, NA, NA, NA, NA, NA, NA),
	[67] = PINGROUP(67, qup18, tsense_pwm3, NA, NA, NA, NA, NA, NA,
			NA),
	[68] = PINGROUP(68, qup18, emac1_mcg2, NA, NA, NA, NA, NA, NA,
			NA),
	[69] = PINGROUP(69, qup18, emac1_mcg3, tsense_pwm2, NA, NA, NA,
			NA, NA, NA),
	[70] = PINGROUP(70, qup16, tsense_pwm1, NA, NA, NA, NA, NA, NA,
			NA),
	[71] = PINGROUP(71, qup16, atest_usb3, NA, NA, NA, NA, NA, NA,
			NA),
	[72] = PINGROUP(72, qup16, atest_usb33, NA, NA, NA, NA, NA, NA,
			NA),
	[73] = PINGROUP(73, qup16, atest_usb32, NA, NA, NA, NA, NA, NA,
			NA),
	[74] = PINGROUP(74, qspi0_clk, sdc4_clk, atest_usb23, NA, NA,
			NA, NA, NA, NA),
	[75] = PINGROUP(75, qspi0_cs, sdc4_cmd, atest_usb13, NA, NA, NA,
			NA, NA, NA),
	[76] = PINGROUP(76, qspi00, sdc40, atest_usb11, NA, NA, NA, NA,
			NA, NA),
	[77] = PINGROUP(77, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[78] = PINGROUP(78, qspi02, sdc42, atest_usb02, NA, NA, NA, NA,
			NA, NA),
	[79] = PINGROUP(79, qspi03, sdc43, atest_usb01, ddr_pxi6, NA,
			NA, NA, NA, NA),
	[80] = PINGROUP(80, mi2s_mclk1, audio_ref, phase_flag23, NA,
			qdss_gpio12, NA, NA, NA, NA),
	[81] = PINGROUP(81, qup21, qspi0_cs, phase_flag22, NA,
			vsense_trigger, NA, NA, NA, NA),
	[82] = PINGROUP(82, qup21, phase_flag21, NA, NA, NA, NA, NA,
			NA, NA),
	[83] = PINGROUP(83, qup21, qup22, phase_flag20, NA, NA, NA,
			NA, NA, NA),
	[84] = PINGROUP(84, qup21, qup22, pll_bist, pll_clk, NA, NA,
			NA, NA, NA),
	[85] = PINGROUP(85, qup22, NA, NA, NA, NA, NA, NA, NA, NA),
	[86] = PINGROUP(86, qup22, NA, pll_clk, NA, NA, NA, NA, NA, NA),
	[87] = PINGROUP(87, qup20, phase_flag18, NA, NA, NA, NA, NA,
			NA, NA),
	[88] = PINGROUP(88, qup20, phase_flag17, NA, NA, NA, NA, NA,
			NA, NA),
	[89] = PINGROUP(89, qup20, phase_flag16, NA, NA, NA, NA, NA,
			NA, NA),
	[90] = PINGROUP(90, qup20, phase_flag15, NA, NA, NA, NA, NA,
			NA, NA),
	[91] = PINGROUP(91, qup20, hs2_mi2s, phase_flag14, NA, NA, NA,
			NA, NA, NA),
	[92] = PINGROUP(92, qup20, hs2_mi2s, phase_flag13, NA, NA, NA,
			NA, NA, NA),
	[93] = PINGROUP(93, mi2s0_sck, emac1_ptp, emac1_ptp,
			phase_flag31, NA, NA, NA, NA, NA),
	[94] = PINGROUP(94, mi2s0_ws, emac1_ptp, emac1_ptp,
			phase_flag30, NA, NA, NA, NA, NA),
	[95] = PINGROUP(95, mi2s0_data0, emac1_ptp, emac1_ptp,
			phase_flag29, NA, NA, NA, NA, NA),
	[96] = PINGROUP(96, mi2s0_data1, emac1_ptp, emac1_ptp,
			qdss_gpio13, NA, NA, NA, NA, NA),
	[97] = PINGROUP(97, rgmii_1, atest_usb31, NA, NA, NA, NA, NA,
			NA, NA),
	[98] = PINGROUP(98, rgmii_1, atest_usb30, NA, NA, NA, NA, NA,
			NA, NA),
	[99] = PINGROUP(99, rgmii_1, NA, NA, NA, NA, NA, NA, NA, NA),
	[100] = PINGROUP(100, mdp_vsync, rgmii_1, NA, NA, NA, NA, NA,
			 NA, NA),
	[101] = PINGROUP(101, mdp_vsync, rgmii_1, tgu_ch0, atest_usb4,
			 NA, NA, NA, NA, NA),
	[102] = PINGROUP(102, rgmii_1, tgu_ch1, atest_usb43, NA, NA, NA,
			 NA, NA, NA),
	[103] = PINGROUP(103, rgmii_1, tgu_ch2, atest_usb42, NA, NA, NA,
			 NA, NA, NA),
	[104] = PINGROUP(104, rgmii_1, tgu_ch3, atest_usb41, NA, NA, NA,
			 NA, NA, NA),
	[105] = PINGROUP(105, rgmii_1, tgu_ch4, atest_usb40, NA, NA, NA,
			 NA, NA, NA),
	[106] = PINGROUP(106, rgmii_1, tgu_ch5, NA, NA, NA, NA, NA, NA,
			 NA),
	[107] = PINGROUP(107, rgmii_1, tgu_ch6, NA, NA, NA, NA, NA, NA,
			 NA),
	[108] = PINGROUP(108, rgmii_1, tgu_ch7, NA, NA, NA, NA, NA, NA,
			 NA),
	[109] = PINGROUP(109, rgmii_1, NA, NA, NA, NA, NA, NA, NA, NA),
	[110] = PINGROUP(110, qup20, rgmii_1, atest_usb5, NA, NA, NA,
			 NA, NA, NA),
	[111] = PINGROUP(111, qup4, qup5, atest_usb53, NA, NA, NA, NA,
			 NA, NA),
	[112] = PINGROUP(112, qup4, qup5, atest_usb52, NA, NA, NA, NA,
			 NA, NA),
	[113] = PINGROUP(113, cci_i2c, atest_usb51, NA, NA, NA, NA, NA,
			 NA, NA),
	[114] = PINGROUP(114, cci_i2c, gcc_gp2, atest_usb50, NA, NA, NA,
			 NA, NA, NA),
	[115] = PINGROUP(115, cci_i2c, gcc_gp3, qdss_gpio, NA, NA, NA,
			 NA, NA, NA),
	[116] = PINGROUP(116, cci_i2c, qdss_gpio, NA, NA, NA, NA, NA,
			 NA, NA),
	[117] = PINGROUP(117, cci_i2c, NA, qdss_gpio8, NA, NA, NA, NA,
			 NA, NA),
	[118] = PINGROUP(118, cci_i2c, NA, qdss_gpio9, NA, NA, NA, NA,
			 NA, NA),
	[119] = PINGROUP(119, cam_mclk, cci_timer0, cci_async, gcc_gp1,
			 qdss_gpio10, NA, NA, NA, NA),
	[120] = PINGROUP(120, cam_mclk, cci_timer1, cci_async, gcc_gp2,
			 qdss_gpio11, ddr_pxi3, NA, NA, NA),
	[121] = PINGROUP(121, qup2, qdss_gpio12, NA, atest_usb20,
			 ddr_pxi0, NA, NA, NA, NA),
	[122] = PINGROUP(122, qup2, qdss_gpio13, atest_usb00, NA, NA,
			 NA, NA, NA, NA),
	[123] = PINGROUP(123, qup2, cci_i2c, cmu_rng, ddr_pxi2, NA, NA,
			 NA, NA, NA),
	[124] = PINGROUP(124, qup2, cci_i2c, cmu_rng, ddr_pxi1, NA, NA,
			 NA, NA, NA),
	[125] = PINGROUP(125, qup7, dbg_out, ddr_pxi1, NA, NA, NA, NA,
			 NA, NA),
	[126] = PINGROUP(126, qup7, cmu_rng, ddr_pxi0, NA, NA, NA, NA,
			 NA, NA),
	[127] = PINGROUP(127, emac0_phy, NA, NA, NA, NA, NA, NA, NA, NA),
	[128] = PINGROUP(128, qup7, ibi_i3c, NA, NA, NA, NA, NA, NA, NA),
	[129] = PINGROUP(129, qup7, ibi_i3c, NA, NA, NA, NA, NA, NA, NA),
	[130] = PINGROUP(130, emac0_ptp, emac0_ptp, sd_write,
			 atest_usb1, NA, NA, NA, NA, NA),
	[131] = PINGROUP(131, emac0_ptp, emac0_ptp, atest_usb0, NA, NA,
			 NA, NA, NA, NA),
	[132] = PINGROUP(132, usb0_usb4, phase_flag28, NA, NA, NA, NA,
			 NA, NA, NA),
	[133] = PINGROUP(133, usb2phy_ac, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[134] = PINGROUP(134, usb2phy_ac, atest_char0, NA, NA, NA, NA,
			 NA, NA, NA),
	[135] = PINGROUP(135, qup0, qup3, NA, atest_usb12, ddr_pxi7, NA,
			 NA, NA, NA),
	[136] = PINGROUP(136, qup0, qup3, cmu_rng, ddr_pxi7, NA, NA, NA,
			 NA, NA),
	[137] = PINGROUP(137, qup3, qup0, NA, atest_usb22, ddr_pxi3, NA,
			 NA, NA, NA),
	[138] = PINGROUP(138, qup3, qup0, NA, atest_usb21, ddr_pxi2, NA,
			 NA, NA, NA),
	[139] = PINGROUP(139, cci_timer5, gcc_gp3, atest_char2, NA, NA,
			 NA, NA, NA, NA),
	[140] = PINGROUP(140, pcie4_clkreq, jitter_bist, atest_char1,
			 NA, NA, NA, NA, NA, NA),
	[141] = PINGROUP(141, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[142] = PINGROUP(142, pcie2a_clkreq, atest_char3, NA, NA, NA,
			 NA, NA, NA, NA),
	[143] = PINGROUP(143, NA, atest_char, NA, NA, NA, NA, NA, NA,
			 NA),
	[144] = PINGROUP(144, pcie2b_clkreq, phase_flag27, NA, NA, NA,
			 NA, NA, NA, NA),
	[145] = PINGROUP(145, qup5, cci_i2c, phase_flag26, NA, NA, NA,
			 NA, NA, NA),
	[146] = PINGROUP(146, qup5, cci_i2c, phase_flag25, NA, NA, NA,
			 NA, NA, NA),
	[147] = PINGROUP(147, NA, phase_flag24, NA, NA, NA, NA, NA, NA,
			 NA),
	[148] = PINGROUP(148, usb2phy_ac, NA, atest_usb2, NA, NA, NA,
			 NA, NA, NA),
	[149] = PINGROUP(149, usb2phy_ac, gcc_gp1, atest_usb03, NA, NA,
			 NA, NA, NA, NA),
	[150] = PINGROUP(150, pcie3a_clkreq, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[151] = PINGROUP(151, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[152] = PINGROUP(152, pcie3b_clkreq, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[153] = PINGROUP(153, NA, tb_trig, NA, NA, NA, NA, NA, NA, NA),
	[154] = PINGROUP(154, qup6, ibi_i3c, NA, NA, NA, NA, NA, NA, NA),
	[155] = PINGROUP(155, qup6, ibi_i3c, NA, NA, NA, NA, NA, NA, NA),
	[156] = PINGROUP(156, qup6, emac0_ptp, emac0_ptp, NA, NA, NA,
			 NA, NA, NA),
	[157] = PINGROUP(157, qup6, emac0_ptp, emac0_ptp, tb_trig, NA,
			 NA, NA, NA, NA),
	[158] = PINGROUP(158, qup1, emac0_ptp, emac0_ptp, NA, NA, NA,
			 NA, NA, NA),
	[159] = PINGROUP(159, qup1, emac0_ptp, emac0_ptp, NA, NA, NA,
			 NA, NA, NA),
	[160] = PINGROUP(160, cci_timer9, qup1, cci_async, emac0_mcg0,
			 gcc_gp4, NA, NA, NA, NA),
	[161] = PINGROUP(161, cci_timer4, cci_async, qup1, emac0_mcg1,
			 qdss_gpio14, NA, NA, NA, NA),
	[162] = PINGROUP(162, cci_timer6, emac0_mcg2, gcc_gp4,
			 qdss_gpio15, NA, NA, NA, NA, NA),
	[163] = PINGROUP(163, cci_timer7, emac0_mcg3, NA, NA, NA, NA,
			 NA, NA, NA),
	[164] = PINGROUP(164, cci_i2c, NA, NA, NA, NA, NA, NA, NA, NA),
	[165] = PINGROUP(165, cci_i2c, NA, NA, NA, NA, NA, NA, NA, NA),
	[166] = PINGROUP(166, usb0_phy, NA, NA, NA, NA, NA, NA, NA, NA),
	[167] = PINGROUP(167, cci_timer8, cci_async, gcc_gp5, NA, NA,
			 NA, NA, NA, NA),
	[168] = PINGROUP(168, usb0_sbtx, gcc_gp5, NA, NA, NA, NA, NA,
			 NA, NA),
	[169] = PINGROUP(169, usb0_sbtx, NA, NA, NA, NA, NA, NA, NA, NA),
	[170] = PINGROUP(170, usb0_sbrx, NA, NA, NA, NA, NA, NA, NA, NA),
	[171] = PINGROUP(171, qup4, NA, NA, NA, NA, NA, NA, NA, NA),
	[172] = PINGROUP(172, qup4, NA, NA, NA, NA, NA, NA, NA, NA),
	[173] = PINGROUP(173, qup4, NA, NA, NA, NA, NA, NA, NA, NA),
	[174] = PINGROUP(174, qup4, NA, NA, NA, NA, NA, NA, NA, NA),
	[175] = PINGROUP(175, qup4, rgmii_0, NA, NA, NA, NA, NA, NA, NA),
	[176] = PINGROUP(176, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[177] = PINGROUP(177, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[178] = PINGROUP(178, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[179] = PINGROUP(179, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[180] = PINGROUP(180, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[181] = PINGROUP(181, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[182] = PINGROUP(182, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[183] = PINGROUP(183, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[184] = PINGROUP(184, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[185] = PINGROUP(185, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[186] = PINGROUP(186, rgmii_0, NA, NA, NA, NA, NA, NA, NA, NA),
	[187] = PINGROUP(187, rgmii_0, cri_trng0, NA, NA, NA, NA, NA,
			 NA, NA),
	[188] = PINGROUP(188, rgmii_0, cri_trng1, NA, NA, NA, NA, NA,
			 NA, NA),
	[189] = PINGROUP(189, prng_rosc0, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[190] = PINGROUP(190, cri_trng, NA, NA, NA, NA, NA, NA, NA, NA),
	[191] = PINGROUP(191, prng_rosc1, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[192] = PINGROUP(192, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[193] = PINGROUP(193, prng_rosc2, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[194] = PINGROUP(194, prng_rosc3, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[195] = PINGROUP(195, phase_flag12, NA, qdss_gpio0, NA, NA, NA,
			 NA, NA, NA),
	[196] = PINGROUP(196, phase_flag11, NA, qdss_gpio1, NA, NA, NA,
			 NA, NA, NA),
	[197] = PINGROUP(197, phase_flag10, NA, qdss_gpio2, NA, NA, NA,
			 NA, NA, NA),
	[198] = PINGROUP(198, phase_flag9, NA, qdss_gpio3, NA, NA, NA,
			 NA, NA, NA),
	[199] = PINGROUP(199, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[200] = PINGROUP(200, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[201] = PINGROUP(201, qdss_gpio4, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[202] = PINGROUP(202, phase_flag19, NA, qdss_gpio5, NA, NA, NA,
			 NA, NA, NA),
	[203] = PINGROUP(203, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[204] = PINGROUP(204, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[205] = PINGROUP(205, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[206] = PINGROUP(206, qdss_gpio6, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[207] = PINGROUP(207, qdss_gpio7, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[208] = PINGROUP(208, hs1_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	[209] = PINGROUP(209, hs1_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	[210] = PINGROUP(210, hs1_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	[211] = PINGROUP(211, hs1_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	[212] = PINGROUP(212, mi2s2_sck, qdss_gpio8, NA, NA, NA, NA, NA,
			 NA, NA),
	[213] = PINGROUP(213, mi2s2_ws, qdss_gpio9, NA, NA, NA, NA, NA,
			 NA, NA),
	[214] = PINGROUP(214, mi2s2_data0, qdss_gpio10, ddr_pxi5, NA,
			 NA, NA, NA, NA, NA),
	[215] = PINGROUP(215, mi2s2_data1, qdss_gpio11, emac1_dll0,
			 ddr_pxi5, NA, NA, NA, NA, NA),
	[216] = PINGROUP(216, mi2s_mclk1, qdss_gpio, emac0_dll,
			 ddr_pxi4, NA, NA, NA, NA, NA),
	[217] = PINGROUP(217, mi2s_mclk2, qdss_gpio, emac0_dll,
			 ddr_pxi4, NA, NA, NA, NA, NA),
	[218] = PINGROUP(218, hs2_mi2s, emac1_dll1, ddr_pxi6, NA, NA,
			 NA, NA, NA, NA),
	[219] = PINGROUP(219, hs2_mi2s, phase_flag8, NA, NA, NA, NA, NA,
			 NA, NA),
	[220] = PINGROUP(220, lpass_slimbus, mi2s1_sck, phase_flag7, NA,
			 NA, NA, NA, NA, NA),
	[221] = PINGROUP(221, lpass_slimbus, mi2s1_ws, phase_flag6, NA,
			 NA, NA, NA, NA, NA),
	[222] = PINGROUP(222, mi2s1_data0, phase_flag5, NA, qdss_gpio14,
			 NA, NA, NA, NA, NA),
	[223] = PINGROUP(223, mi2s1_data1, phase_flag4, NA, qdss_gpio15,
			 NA, NA, NA, NA, NA),
	[224] = PINGROUP(224, hs3_mi2s, phase_flag3, NA, NA, NA, NA, NA,
			 NA, NA),
	[225] = PINGROUP(225, hs3_mi2s, phase_flag2, NA, NA, NA, NA, NA,
			 NA, NA),
	[226] = PINGROUP(226, hs3_mi2s, phase_flag1, NA, NA, NA, NA, NA,
			 NA, NA),
	[227] = PINGROUP(227, hs3_mi2s, phase_flag0, NA, NA, NA, NA, NA,
			 NA, NA),
	[228] = UFS_RESET(ufs_reset, 0x1f1000),
	[229] = UFS_RESET(ufs1_reset, 0x1f3000),
	[230] = SDC_QDSD_PINGROUP(sdc2_clk, 0x1e8000, 14, 6),
	[231] = SDC_QDSD_PINGROUP(sdc2_cmd, 0x1e8000, 11, 3),
	[232] = SDC_QDSD_PINGROUP(sdc2_data, 0x1e8000, 9, 0),
};

static struct msm_dir_conn direwolf_dir_conn[] = {
	{-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0},
	{-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}
};

static const struct msm_pinctrl_soc_data direwolf_pinctrl = {
	.pins = direwolf_pins,
	.npins = ARRAY_SIZE(direwolf_pins),
	.functions = direwolf_functions,
	.nfunctions = ARRAY_SIZE(direwolf_functions),
	.groups = direwolf_groups,
	.ngroups = ARRAY_SIZE(direwolf_groups),
	.ngpios = 230,
	.dir_conn = direwolf_dir_conn,
};

static int direwolf_pinctrl_dirconn_list_probe(struct platform_device *pdev)
{
	int ret, n, dirconn_list_count, m;
	struct device_node *np = pdev->dev.of_node;

	n = of_property_count_elems_of_size(np, "qcom,dirconn-list",
						sizeof(u32));

	if (n <= 0 || n % 2)
		return -EINVAL;

	m = ARRAY_SIZE(direwolf_dir_conn) - 1;

	dirconn_list_count = n / 2;

	for (n = 0; n < dirconn_list_count; n++) {
		ret = of_property_read_u32_index(np, "qcom,dirconn-list",
						n * 2 + 0,
						&direwolf_dir_conn[m].gpio);
		if (ret)
			return ret;
		ret = of_property_read_u32_index(np, "qcom,dirconn-list",
						n * 2 + 1,
						&direwolf_dir_conn[m].irq);
		if (ret)
			return ret;
		m--;
	}

	return 0;
}

static int direwolf_pinctrl_probe(struct platform_device *pdev)
{
	int len, ret;

	if (of_find_property(pdev->dev.of_node, "qcom,dirconn-list", &len)) {
		ret = direwolf_pinctrl_dirconn_list_probe(pdev);
		if (ret) {
			dev_err(&pdev->dev,
				"Unable to parse Direct Connect List\n");
			return ret;
		}
	}

	return msm_pinctrl_probe(pdev, &direwolf_pinctrl);
}

static const struct of_device_id direwolf_pinctrl_of_match[] = {
	{ .compatible = "qcom,direwolf-pinctrl", },
	{ },
};

static struct platform_driver direwolf_pinctrl_driver = {
	.driver = {
		.name = "direwolf-pinctrl",
		.of_match_table = direwolf_pinctrl_of_match,
	},
	.probe = direwolf_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init direwolf_pinctrl_init(void)
{
	return platform_driver_register(&direwolf_pinctrl_driver);
}
arch_initcall(direwolf_pinctrl_init);

static void __exit direwolf_pinctrl_exit(void)
{
	platform_driver_unregister(&direwolf_pinctrl_driver);
}
module_exit(direwolf_pinctrl_exit);

MODULE_DESCRIPTION("QTI Direwolf pinctrl driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, direwolf_pinctrl_of_match);
