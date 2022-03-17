// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

#define FUNCTION(fname)			                \
	[msm_mux_##fname] = {		                \
		.name = #fname,				\
		.groups = fname##_groups,               \
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define REG_BASE 0x100000
#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, wake_off, bit)	\
	{					        \
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
		},				        \
		.nfuncs = 10,				\
		.ctl_reg = REG_BASE + REG_SIZE * id,			\
		.io_reg = REG_BASE + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = REG_BASE + 0x8 + REG_SIZE * id,		\
		.intr_status_reg = REG_BASE + 0xc + REG_SIZE * id,	\
		.intr_target_reg = REG_BASE + 0x8 + REG_SIZE * id,	\
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
		.wake_reg = REG_BASE + wake_off,	\
		.wake_bit = bit,		\
	}

#define SDC_QDSD_PINGROUP(pg_name, ctl, pull, drv)	\
	{					        \
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
	{					        \
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

#define QUP_I3C(qup_mode, qup_offset)			\
	{						\
		.mode = qup_mode,			\
		.offset = qup_offset,			\
	}

#define QUP_1_I3C_0_MODE_OFFSET	0xE9000
#define QUP_1_I3C_1_MODE_OFFSET	0xEA000
#define QUP_1_I3C_4_MODE_OFFSET	0xEB000
#define QUP_1_I3C_6_MODE_OFFSET	0xEC000
#define QUP_2_I3C_0_MODE_OFFSET	0xED000
#define QUP_2_I3C_1_MODE_OFFSET	0xEE000
#define QUP_2_I3C_2_MODE_OFFSET	0xEF000
#define QUP_2_I3C_3_MODE_OFFSET	0xF0000

static const struct pinctrl_pin_desc pineapple_pins[] = {
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
	PINCTRL_PIN(210, "UFS_RESET"),
	PINCTRL_PIN(211, "SDC2_CLK"),
	PINCTRL_PIN(212, "SDC2_CMD"),
	PINCTRL_PIN(213, "SDC2_DATA"),
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

static const unsigned int ufs_reset_pins[] = { 210 };
static const unsigned int sdc2_clk_pins[] = { 211 };
static const unsigned int sdc2_cmd_pins[] = { 212 };
static const unsigned int sdc2_data_pins[] = { 213 };

enum pineapple_functions {
	msm_mux_gpio,
	msm_mux_aoss_cti_debug,
	msm_mux_atest_char_start,
	msm_mux_atest_char_status0,
	msm_mux_atest_char_status1,
	msm_mux_atest_char_status2,
	msm_mux_atest_char_status3,
	msm_mux_atest_usb0_atereset,
	msm_mux_atest_usb0_testdataout00,
	msm_mux_atest_usb0_testdataout01,
	msm_mux_atest_usb0_testdataout02,
	msm_mux_atest_usb0_testdataout03,
	msm_mux_audio_ext_mclk0,
	msm_mux_audio_ext_mclk1,
	msm_mux_audio_ref_clk,
	msm_mux_cam_aon_mclk4,
	msm_mux_cam_mclk,
	msm_mux_cci_async_in0,
	msm_mux_cci_async_in1,
	msm_mux_cci_async_in2,
	msm_mux_cci_i2c_scl0,
	msm_mux_cci_i2c_scl1,
	msm_mux_cci_i2c_scl2,
	msm_mux_cci_i2c_scl3,
	msm_mux_cci_i2c_scl4,
	msm_mux_cci_i2c_scl5,
	msm_mux_cci_i2c_sda0,
	msm_mux_cci_i2c_sda1,
	msm_mux_cci_i2c_sda2,
	msm_mux_cci_i2c_sda3,
	msm_mux_cci_i2c_sda4,
	msm_mux_cci_i2c_sda5,
	msm_mux_cci_timer0,
	msm_mux_cci_timer1,
	msm_mux_cci_timer2,
	msm_mux_cci_timer3,
	msm_mux_cci_timer4,
	msm_mux_cmu_rng_entropy0,
	msm_mux_cmu_rng_entropy1,
	msm_mux_cmu_rng_entropy2,
	msm_mux_cmu_rng_entropy3,
	msm_mux_coex_uart1_rx,
	msm_mux_coex_uart1_tx,
	msm_mux_coex_uart2_rx,
	msm_mux_coex_uart2_tx,
	msm_mux_cri_trng_rosc,
	msm_mux_dbg_out_clk,
	msm_mux_ddr_bist_complete,
	msm_mux_ddr_bist_fail,
	msm_mux_ddr_bist_start,
	msm_mux_ddr_bist_stop,
	msm_mux_ddr_pxi0_test,
	msm_mux_ddr_pxi1_test,
	msm_mux_ddr_pxi2_test,
	msm_mux_ddr_pxi3_test,
	msm_mux_do_not_expose,
	msm_mux_do_not_expose0,
	msm_mux_do_not_expose1,
	msm_mux_do_not_expose2,
	msm_mux_do_not_expose3,
	msm_mux_dp_hot_plug,
	msm_mux_gcc_gp1_clk,
	msm_mux_gcc_gp2_clk,
	msm_mux_gcc_gp3_clk,
	msm_mux_gnss_adc_dtest0,
	msm_mux_gnss_adc_dtest1,
	msm_mux_i2chub0_se0_l0,
	msm_mux_i2chub0_se0_l1,
	msm_mux_i2chub0_se1_l0,
	msm_mux_i2chub0_se1_l1,
	msm_mux_i2chub0_se2_l0,
	msm_mux_i2chub0_se2_l1,
	msm_mux_i2chub0_se3_l0,
	msm_mux_i2chub0_se3_l1,
	msm_mux_i2chub0_se4_l0,
	msm_mux_i2chub0_se4_l1,
	msm_mux_i2chub0_se5_l0,
	msm_mux_i2chub0_se5_l1,
	msm_mux_i2chub0_se6_l0,
	msm_mux_i2chub0_se6_l1,
	msm_mux_i2chub0_se7_l0,
	msm_mux_i2chub0_se7_l1,
	msm_mux_i2chub0_se8_l0,
	msm_mux_i2chub0_se8_l1,
	msm_mux_i2chub0_se9_l0,
	msm_mux_i2chub0_se9_l1,
	msm_mux_i2s0_data0,
	msm_mux_i2s0_data1,
	msm_mux_i2s0_sck,
	msm_mux_i2s0_ws,
	msm_mux_i2s1_data0,
	msm_mux_i2s1_data1,
	msm_mux_i2s1_sck,
	msm_mux_i2s1_ws,
	msm_mux_ibi_i3c_qup1,
	msm_mux_ibi_i3c_qup2,
	msm_mux_jitter_bist_ref,
	msm_mux_mdp_vsync0_out,
	msm_mux_mdp_vsync1_out,
	msm_mux_mdp_vsync2_out,
	msm_mux_mdp_vsync3_out,
	msm_mux_mdp_vsync_e,
	msm_mux_mdp_vsync_p,
	msm_mux_mdp_vsync_s,
	msm_mux_nav_gpio0,
	msm_mux_nav_gpio1,
	msm_mux_nav_gpio2,
	msm_mux_nav_gpio3,
	msm_mux_pcie0_clk_req,
	msm_mux_pcie1_clk_req,
	msm_mux_phase_flag_status0,
	msm_mux_phase_flag_status1,
	msm_mux_phase_flag_status10,
	msm_mux_phase_flag_status11,
	msm_mux_phase_flag_status12,
	msm_mux_phase_flag_status13,
	msm_mux_phase_flag_status14,
	msm_mux_phase_flag_status15,
	msm_mux_phase_flag_status16,
	msm_mux_phase_flag_status17,
	msm_mux_phase_flag_status18,
	msm_mux_phase_flag_status19,
	msm_mux_phase_flag_status2,
	msm_mux_phase_flag_status20,
	msm_mux_phase_flag_status21,
	msm_mux_phase_flag_status22,
	msm_mux_phase_flag_status23,
	msm_mux_phase_flag_status24,
	msm_mux_phase_flag_status25,
	msm_mux_phase_flag_status26,
	msm_mux_phase_flag_status27,
	msm_mux_phase_flag_status28,
	msm_mux_phase_flag_status29,
	msm_mux_phase_flag_status3,
	msm_mux_phase_flag_status30,
	msm_mux_phase_flag_status31,
	msm_mux_phase_flag_status4,
	msm_mux_phase_flag_status5,
	msm_mux_phase_flag_status6,
	msm_mux_phase_flag_status7,
	msm_mux_phase_flag_status8,
	msm_mux_phase_flag_status9,
	msm_mux_pll_bist_sync,
	msm_mux_pll_clk_aux,
	msm_mux_prng_rosc_test0,
	msm_mux_prng_rosc_test1,
	msm_mux_prng_rosc_test2,
	msm_mux_prng_rosc_test3,
	msm_mux_qdss_cti_trig0,
	msm_mux_qdss_cti_trig1,
	msm_mux_qdss_gpio_traceclk,
	msm_mux_qdss_gpio_tracectl,
	msm_mux_qdss_gpio_tracedata0,
	msm_mux_qdss_gpio_tracedata1,
	msm_mux_qdss_gpio_tracedata10,
	msm_mux_qdss_gpio_tracedata11,
	msm_mux_qdss_gpio_tracedata12,
	msm_mux_qdss_gpio_tracedata13,
	msm_mux_qdss_gpio_tracedata14,
	msm_mux_qdss_gpio_tracedata15,
	msm_mux_qdss_gpio_tracedata2,
	msm_mux_qdss_gpio_tracedata3,
	msm_mux_qdss_gpio_tracedata4,
	msm_mux_qdss_gpio_tracedata5,
	msm_mux_qdss_gpio_tracedata6,
	msm_mux_qdss_gpio_tracedata7,
	msm_mux_qdss_gpio_tracedata8,
	msm_mux_qdss_gpio_tracedata9,
	msm_mux_qlink_big_enable,
	msm_mux_qlink_big_request,
	msm_mux_qlink_little_enable,
	msm_mux_qlink_little_request,
	msm_mux_qlink_wmss_reset,
	msm_mux_qspi0,
	msm_mux_qspi1,
	msm_mux_qspi2,
	msm_mux_qspi3,
	msm_mux_qspi_clk,
	msm_mux_qspi_cs_n,
	msm_mux_qup1_se0_l0,
	msm_mux_qup1_se0_l1,
	msm_mux_qup1_se0_l2,
	msm_mux_qup1_se0_l3,
	msm_mux_qup1_se1_l0,
	msm_mux_qup1_se1_l1,
	msm_mux_qup1_se1_l2,
	msm_mux_qup1_se1_l3,
	msm_mux_qup1_se2_l0,
	msm_mux_qup1_se2_l1,
	msm_mux_qup1_se2_l2,
	msm_mux_qup1_se2_l3,
	msm_mux_qup1_se2_l4,
	msm_mux_qup1_se2_l5,
	msm_mux_qup1_se2_l6,
	msm_mux_qup1_se3_l0,
	msm_mux_qup1_se3_l1,
	msm_mux_qup1_se3_l2,
	msm_mux_qup1_se3_l3,
	msm_mux_qup1_se4_l0,
	msm_mux_qup1_se4_l1,
	msm_mux_qup1_se4_l2,
	msm_mux_qup1_se4_l3,
	msm_mux_qup1_se5_l0,
	msm_mux_qup1_se5_l1,
	msm_mux_qup1_se5_l2,
	msm_mux_qup1_se5_l3,
	msm_mux_qup1_se6_l0,
	msm_mux_qup1_se6_l1,
	msm_mux_qup1_se6_l2,
	msm_mux_qup1_se6_l3,
	msm_mux_qup1_se7_l0,
	msm_mux_qup1_se7_l1,
	msm_mux_qup1_se7_l2,
	msm_mux_qup1_se7_l3,
	msm_mux_qup2_se0_l0,
	msm_mux_qup2_se0_l1,
	msm_mux_qup2_se0_l2,
	msm_mux_qup2_se0_l3,
	msm_mux_qup2_se1_l0,
	msm_mux_qup2_se1_l1,
	msm_mux_qup2_se1_l2,
	msm_mux_qup2_se1_l3,
	msm_mux_qup2_se2_l0,
	msm_mux_qup2_se2_l1,
	msm_mux_qup2_se2_l2,
	msm_mux_qup2_se2_l3,
	msm_mux_qup2_se2_l4,
	msm_mux_qup2_se2_l5,
	msm_mux_qup2_se2_l6,
	msm_mux_qup2_se3_l0,
	msm_mux_qup2_se3_l1,
	msm_mux_qup2_se3_l2,
	msm_mux_qup2_se3_l3,
	msm_mux_qup2_se4_l0,
	msm_mux_qup2_se4_l1,
	msm_mux_qup2_se4_l2,
	msm_mux_qup2_se4_l3,
	msm_mux_qup2_se5_l0,
	msm_mux_qup2_se5_l1,
	msm_mux_qup2_se5_l2,
	msm_mux_qup2_se5_l3,
	msm_mux_qup2_se5_l6,
	msm_mux_qup2_se6_l0,
	msm_mux_qup2_se6_l1,
	msm_mux_qup2_se6_l2,
	msm_mux_qup2_se6_l3,
	msm_mux_qup2_se7_l0,
	msm_mux_qup2_se7_l1,
	msm_mux_qup2_se7_l2,
	msm_mux_qup2_se7_l3,
	msm_mux_sd_write_protect,
	msm_mux_sdc40,
	msm_mux_sdc41,
	msm_mux_sdc42,
	msm_mux_sdc43,
	msm_mux_sdc4_clk,
	msm_mux_sdc4_cmd,
	msm_mux_tb_trig_sdc2,
	msm_mux_tb_trig_sdc4,
	msm_mux_tgu_ch0_trigout,
	msm_mux_tgu_ch1_trigout,
	msm_mux_tgu_ch2_trigout,
	msm_mux_tgu_ch3_trigout,
	msm_mux_tmess_prng_rosc0,
	msm_mux_tmess_prng_rosc1,
	msm_mux_tmess_prng_rosc2,
	msm_mux_tmess_prng_rosc3,
	msm_mux_tsense_pwm1_out,
	msm_mux_tsense_pwm2_out,
	msm_mux_tsense_pwm3_out,
	msm_mux_uim0_clk,
	msm_mux_uim0_data,
	msm_mux_uim0_present,
	msm_mux_uim0_reset,
	msm_mux_uim1_clk,
	msm_mux_uim1_data,
	msm_mux_uim1_present,
	msm_mux_uim1_reset,
	msm_mux_usb1_hs_ac,
	msm_mux_usb_phy_ps,
	msm_mux_vfr_0,
	msm_mux_vfr_1,
	msm_mux_vsense_trigger_mirnat,
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
	"gpio201", "gpio202", "gpio203", "gpio204", "gpio205", "gpio206",
	"gpio207", "gpio208", "gpio209",
};
static const char * const aoss_cti_debug_groups[] = {
	"gpio50", "gpio51", "gpio60", "gpio61",
};
static const char * const atest_char_start_groups[] = {
	"gpio137",
};
static const char * const atest_char_status0_groups[] = {
	"gpio133",
};
static const char * const atest_char_status1_groups[] = {
	"gpio132",
};
static const char * const atest_char_status2_groups[] = {
	"gpio131",
};
static const char * const atest_char_status3_groups[] = {
	"gpio130",
};
static const char * const atest_usb0_atereset_groups[] = {
	"gpio74",
};
static const char * const atest_usb0_testdataout00_groups[] = {
	"gpio131",
};
static const char * const atest_usb0_testdataout01_groups[] = {
	"gpio130",
};
static const char * const atest_usb0_testdataout02_groups[] = {
	"gpio71",
};
static const char * const atest_usb0_testdataout03_groups[] = {
	"gpio72",
};
static const char * const audio_ext_mclk0_groups[] = {
	"gpio125",
};
static const char * const audio_ext_mclk1_groups[] = {
	"gpio124",
};
static const char * const audio_ref_clk_groups[] = {
	"gpio124",
};
static const char * const cam_aon_mclk4_groups[] = {
	"gpio104",
};
static const char * const cam_mclk_groups[] = {
	"gpio100", "gpio101", "gpio102", "gpio103", "gpio105", "gpio106",
	"gpio108",
};
static const char * const cci_async_in0_groups[] = {
	"gpio15",
};
static const char * const cci_async_in1_groups[] = {
	"gpio163",
};
static const char * const cci_async_in2_groups[] = {
	"gpio164",
};
static const char * const cci_i2c_scl0_groups[] = {
	"gpio114",
};
static const char * const cci_i2c_scl1_groups[] = {
	"gpio116",
};
static const char * const cci_i2c_scl2_groups[] = {
	"gpio118",
};
static const char * const cci_i2c_scl3_groups[] = {
	"gpio13",
};
static const char * const cci_i2c_scl4_groups[] = {
	"gpio153",
};
static const char * const cci_i2c_scl5_groups[] = {
	"gpio120",
};
static const char * const cci_i2c_sda0_groups[] = {
	"gpio113",
};
static const char * const cci_i2c_sda1_groups[] = {
	"gpio115",
};
static const char * const cci_i2c_sda2_groups[] = {
	"gpio117",
};
static const char * const cci_i2c_sda3_groups[] = {
	"gpio12",
};
static const char * const cci_i2c_sda4_groups[] = {
	"gpio112",
};
static const char * const cci_i2c_sda5_groups[] = {
	"gpio119",
};
static const char * const cci_timer0_groups[] = {
	"gpio109",
};
static const char * const cci_timer1_groups[] = {
	"gpio110",
};
static const char * const cci_timer2_groups[] = {
	"gpio10",
};
static const char * const cci_timer3_groups[] = {
	"gpio11",
};
static const char * const cci_timer4_groups[] = {
	"gpio111",
};
static const char * const cmu_rng_entropy0_groups[] = {
	"gpio94", "gpio129",
};
static const char * const cmu_rng_entropy1_groups[] = {
	"gpio95", "gpio128",
};
static const char * const cmu_rng_entropy2_groups[] = {
	"gpio96", "gpio127",
};
static const char * const cmu_rng_entropy3_groups[] = {
	"gpio112", "gpio122",
};
static const char * const coex_uart1_rx_groups[] = {
	"gpio148",
};
static const char * const coex_uart1_tx_groups[] = {
	"gpio149",
};
static const char * const coex_uart2_rx_groups[] = {
	"gpio150",
};
static const char * const coex_uart2_tx_groups[] = {
	"gpio151",
};
static const char * const cri_trng_rosc_groups[] = {
	"gpio187",
};
static const char * const dbg_out_clk_groups[] = {
	"gpio92",
};
static const char * const ddr_bist_complete_groups[] = {
	"gpio44",
};
static const char * const ddr_bist_fail_groups[] = {
	"gpio40",
};
static const char * const ddr_bist_start_groups[] = {
	"gpio41",
};
static const char * const ddr_bist_stop_groups[] = {
	"gpio45",
};
static const char * const ddr_pxi0_test_groups[] = {
	"gpio75", "gpio76",
};
static const char * const ddr_pxi1_test_groups[] = {
	"gpio44", "gpio45",
};
static const char * const ddr_pxi2_test_groups[] = {
	"gpio51", "gpio62",
};
static const char * const ddr_pxi3_test_groups[] = {
	"gpio46", "gpio47",
};
static const char * const do_not_expose_groups[] = {
	"gpio134", "gpio135", "gpio136",
};
static const char * const do_not_expose0_groups[] = {
	"gpio36",
};
static const char * const do_not_expose1_groups[] = {
	"gpio37",
};
static const char * const do_not_expose2_groups[] = {
	"gpio38",
};
static const char * const do_not_expose3_groups[] = {
	"gpio39",
};
static const char * const dp_hot_plug_groups[] = {
	"gpio47",
};
static const char * const gcc_gp1_clk_groups[] = {
	"gpio86", "gpio134",
};
static const char * const gcc_gp2_clk_groups[] = {
	"gpio87", "gpio135",
};
static const char * const gcc_gp3_clk_groups[] = {
	"gpio88", "gpio136",
};
static const char * const gnss_adc_dtest0_groups[] = {
	"gpio89", "gpio91",
};
static const char * const gnss_adc_dtest1_groups[] = {
	"gpio90", "gpio92",
};
static const char * const i2chub0_se0_l0_groups[] = {
	"gpio64",
};
static const char * const i2chub0_se0_l1_groups[] = {
	"gpio65",
};
static const char * const i2chub0_se1_l0_groups[] = {
	"gpio66",
};
static const char * const i2chub0_se1_l1_groups[] = {
	"gpio67",
};
static const char * const i2chub0_se2_l0_groups[] = {
	"gpio68",
};
static const char * const i2chub0_se2_l1_groups[] = {
	"gpio69",
};
static const char * const i2chub0_se3_l0_groups[] = {
	"gpio70",
};
static const char * const i2chub0_se3_l1_groups[] = {
	"gpio71",
};
static const char * const i2chub0_se4_l0_groups[] = {
	"gpio72",
};
static const char * const i2chub0_se4_l1_groups[] = {
	"gpio73",
};
static const char * const i2chub0_se5_l0_groups[] = {
	"gpio74",
};
static const char * const i2chub0_se5_l1_groups[] = {
	"gpio75",
};
static const char * const i2chub0_se6_l0_groups[] = {
	"gpio76",
};
static const char * const i2chub0_se6_l1_groups[] = {
	"gpio77",
};
static const char * const i2chub0_se7_l0_groups[] = {
	"gpio78",
};
static const char * const i2chub0_se7_l1_groups[] = {
	"gpio79",
};
static const char * const i2chub0_se8_l0_groups[] = {
	"gpio206",
};
static const char * const i2chub0_se8_l1_groups[] = {
	"gpio207",
};
static const char * const i2chub0_se9_l0_groups[] = {
	"gpio80",
};
static const char * const i2chub0_se9_l1_groups[] = {
	"gpio81",
};
static const char * const i2s0_data0_groups[] = {
	"gpio127",
};
static const char * const i2s0_data1_groups[] = {
	"gpio128",
};
static const char * const i2s0_sck_groups[] = {
	"gpio126",
};
static const char * const i2s0_ws_groups[] = {
	"gpio129",
};
static const char * const i2s1_data0_groups[] = {
	"gpio122",
};
static const char * const i2s1_data1_groups[] = {
	"gpio124",
};
static const char * const i2s1_sck_groups[] = {
	"gpio121",
};
static const char * const i2s1_ws_groups[] = {
	"gpio123",
};
static const char * const ibi_i3c_qup1_groups[] = {
	"gpio32", "gpio33", "gpio36", "gpio37", "gpio48", "gpio49", "gpio56",
	"gpio57",
};
static const char * const ibi_i3c_qup2_groups[] = {
	"gpio0", "gpio1", "gpio4", "gpio5", "gpio8", "gpio9", "gpio12",
	"gpio13",
};
static const char * const jitter_bist_ref_groups[] = {
	"gpio73",
};
static const char * const mdp_vsync0_out_groups[] = {
	"gpio86",
};
static const char * const mdp_vsync1_out_groups[] = {
	"gpio86",
};
static const char * const mdp_vsync2_out_groups[] = {
	"gpio87",
};
static const char * const mdp_vsync3_out_groups[] = {
	"gpio87",
};
static const char * const mdp_vsync_e_groups[] = {
	"gpio88",
};
static const char * const mdp_vsync_p_groups[] = {
	"gpio86", "gpio137",
};
static const char * const mdp_vsync_s_groups[] = {
	"gpio87", "gpio133",
};
static const char * const nav_gpio0_groups[] = {
	"gpio154",
};
static const char * const nav_gpio1_groups[] = {
	"gpio155",
};
static const char * const nav_gpio2_groups[] = {
	"gpio152",
};
static const char * const nav_gpio3_groups[] = {
	"gpio154",
};
static const char * const pcie0_clk_req_groups[] = {
	"gpio95",
};
static const char * const pcie1_clk_req_groups[] = {
	"gpio98",
};
static const char * const phase_flag_status0_groups[] = {
	"gpio0",
};
static const char * const phase_flag_status1_groups[] = {
	"gpio1",
};
static const char * const phase_flag_status10_groups[] = {
	"gpio15",
};
static const char * const phase_flag_status11_groups[] = {
	"gpio16",
};
static const char * const phase_flag_status12_groups[] = {
	"gpio17",
};
static const char * const phase_flag_status13_groups[] = {
	"gpio19",
};
static const char * const phase_flag_status14_groups[] = {
	"gpio94",
};
static const char * const phase_flag_status15_groups[] = {
	"gpio95",
};
static const char * const phase_flag_status16_groups[] = {
	"gpio96",
};
static const char * const phase_flag_status17_groups[] = {
	"gpio109",
};
static const char * const phase_flag_status18_groups[] = {
	"gpio13",
};
static const char * const phase_flag_status19_groups[] = {
	"gpio111",
};
static const char * const phase_flag_status2_groups[] = {
	"gpio3",
};
static const char * const phase_flag_status20_groups[] = {
	"gpio112",
};
static const char * const phase_flag_status21_groups[] = {
	"gpio113",
};
static const char * const phase_flag_status22_groups[] = {
	"gpio114",
};
static const char * const phase_flag_status23_groups[] = {
	"gpio115",
};
static const char * const phase_flag_status24_groups[] = {
	"gpio116",
};
static const char * const phase_flag_status25_groups[] = {
	"gpio117",
};
static const char * const phase_flag_status26_groups[] = {
	"gpio118",
};
static const char * const phase_flag_status27_groups[] = {
	"gpio119",
};
static const char * const phase_flag_status28_groups[] = {
	"gpio120",
};
static const char * const phase_flag_status29_groups[] = {
	"gpio153",
};
static const char * const phase_flag_status3_groups[] = {
	"gpio4",
};
static const char * const phase_flag_status30_groups[] = {
	"gpio163",
};
static const char * const phase_flag_status31_groups[] = {
	"gpio164",
};
static const char * const phase_flag_status4_groups[] = {
	"gpio5",
};
static const char * const phase_flag_status5_groups[] = {
	"gpio7",
};
static const char * const phase_flag_status6_groups[] = {
	"gpio8",
};
static const char * const phase_flag_status7_groups[] = {
	"gpio9",
};
static const char * const phase_flag_status8_groups[] = {
	"gpio11",
};
static const char * const phase_flag_status9_groups[] = {
	"gpio12",
};
static const char * const pll_bist_sync_groups[] = {
	"gpio68",
};
static const char * const pll_clk_aux_groups[] = {
	"gpio106",
};
static const char * const prng_rosc_test0_groups[] = {
	"gpio186",
};
static const char * const prng_rosc_test1_groups[] = {
	"gpio183",
};
static const char * const prng_rosc_test2_groups[] = {
	"gpio182",
};
static const char * const prng_rosc_test3_groups[] = {
	"gpio181",
};
static const char * const qdss_cti_trig0_groups[] = {
	"gpio31", "gpio78", "gpio82", "gpio162",
};
static const char * const qdss_cti_trig1_groups[] = {
	"gpio27", "gpio79", "gpio83", "gpio159",
};
static const char * const qdss_gpio_traceclk_groups[] = {
	"gpio115", "gpio149",
};
static const char * const qdss_gpio_tracectl_groups[] = {
	"gpio116", "gpio148",
};
static const char * const qdss_gpio_tracedata0_groups[] = {
	"gpio102", "gpio140",
};
static const char * const qdss_gpio_tracedata1_groups[] = {
	"gpio103", "gpio141",
};
static const char * const qdss_gpio_tracedata10_groups[] = {
	"gpio118", "gpio152",
};
static const char * const qdss_gpio_tracedata11_groups[] = {
	"gpio100", "gpio154",
};
static const char * const qdss_gpio_tracedata12_groups[] = {
	"gpio101", "gpio155",
};
static const char * const qdss_gpio_tracedata13_groups[] = {
	"gpio13", "gpio156",
};
static const char * const qdss_gpio_tracedata14_groups[] = {
	"gpio15", "gpio157",
};
static const char * const qdss_gpio_tracedata15_groups[] = {
	"gpio7", "gpio158",
};
static const char * const qdss_gpio_tracedata2_groups[] = {
	"gpio104", "gpio142",
};
static const char * const qdss_gpio_tracedata3_groups[] = {
	"gpio105", "gpio143",
};
static const char * const qdss_gpio_tracedata4_groups[] = {
	"gpio8", "gpio144",
};
static const char * const qdss_gpio_tracedata5_groups[] = {
	"gpio145", "gpio153",
};
static const char * const qdss_gpio_tracedata6_groups[] = {
	"gpio3", "gpio146",
};
static const char * const qdss_gpio_tracedata7_groups[] = {
	"gpio113", "gpio147",
};
static const char * const qdss_gpio_tracedata8_groups[] = {
	"gpio114", "gpio150",
};
static const char * const qdss_gpio_tracedata9_groups[] = {
	"gpio117", "gpio151",
};
static const char * const qlink_big_enable_groups[] = {
	"gpio160",
};
static const char * const qlink_big_request_groups[] = {
	"gpio159",
};
static const char * const qlink_little_enable_groups[] = {
	"gpio157",
};
static const char * const qlink_little_request_groups[] = {
	"gpio156",
};
static const char * const qlink_wmss_reset_groups[] = {
	"gpio158",
};
static const char * const qspi0_groups[] = {
	"gpio134",
};
static const char * const qspi1_groups[] = {
	"gpio136",
};
static const char * const qspi2_groups[] = {
	"gpio56",
};
static const char * const qspi3_groups[] = {
	"gpio57",
};
static const char * const qspi_clk_groups[] = {
	"gpio135",
};
static const char * const qspi_cs_n_groups[] = {
	"gpio58", "gpio59",
};
static const char * const qup1_se0_l0_groups[] = {
	"gpio32",
};
static const char * const qup1_se0_l1_groups[] = {
	"gpio33",
};
static const char * const qup1_se0_l2_groups[] = {
	"gpio34",
};
static const char * const qup1_se0_l3_groups[] = {
	"gpio35",
};
static const char * const qup1_se1_l0_groups[] = {
	"gpio36",
};
static const char * const qup1_se1_l1_groups[] = {
	"gpio37",
};
static const char * const qup1_se1_l2_groups[] = {
	"gpio38",
};
static const char * const qup1_se1_l3_groups[] = {
	"gpio39",
};
static const char * const qup1_se2_l0_groups[] = {
	"gpio40",
};
static const char * const qup1_se2_l1_groups[] = {
	"gpio41",
};
static const char * const qup1_se2_l2_groups[] = {
	"gpio42",
};
static const char * const qup1_se2_l3_groups[] = {
	"gpio43",
};
static const char * const qup1_se2_l4_groups[] = {
	"gpio44",
};
static const char * const qup1_se2_l5_groups[] = {
	"gpio45",
};
static const char * const qup1_se2_l6_groups[] = {
	"gpio46",
};
static const char * const qup1_se3_l0_groups[] = {
	"gpio44",
};
static const char * const qup1_se3_l1_groups[] = {
	"gpio45",
};
static const char * const qup1_se3_l2_groups[] = {
	"gpio46",
};
static const char * const qup1_se3_l3_groups[] = {
	"gpio47",
};
static const char * const qup1_se4_l0_groups[] = {
	"gpio48",
};
static const char * const qup1_se4_l1_groups[] = {
	"gpio49",
};
static const char * const qup1_se4_l2_groups[] = {
	"gpio50",
};
static const char * const qup1_se4_l3_groups[] = {
	"gpio51",
};
static const char * const qup1_se5_l0_groups[] = {
	"gpio52",
};
static const char * const qup1_se5_l1_groups[] = {
	"gpio53",
};
static const char * const qup1_se5_l2_groups[] = {
	"gpio54",
};
static const char * const qup1_se5_l3_groups[] = {
	"gpio55",
};
static const char * const qup1_se6_l0_groups[] = {
	"gpio56",
};
static const char * const qup1_se6_l1_groups[] = {
	"gpio57",
};
static const char * const qup1_se6_l2_groups[] = {
	"gpio58",
};
static const char * const qup1_se6_l3_groups[] = {
	"gpio59",
};
static const char * const qup1_se7_l0_groups[] = {
	"gpio60",
};
static const char * const qup1_se7_l1_groups[] = {
	"gpio61",
};
static const char * const qup1_se7_l2_groups[] = {
	"gpio62",
};
static const char * const qup1_se7_l3_groups[] = {
	"gpio63",
};
static const char * const qup2_se0_l0_groups[] = {
	"gpio0",
};
static const char * const qup2_se0_l1_groups[] = {
	"gpio1",
};
static const char * const qup2_se0_l2_groups[] = {
	"gpio2",
};
static const char * const qup2_se0_l3_groups[] = {
	"gpio3",
};
static const char * const qup2_se1_l0_groups[] = {
	"gpio4",
};
static const char * const qup2_se1_l1_groups[] = {
	"gpio5",
};
static const char * const qup2_se1_l2_groups[] = {
	"gpio6",
};
static const char * const qup2_se1_l3_groups[] = {
	"gpio7",
};
static const char * const qup2_se2_l0_groups[] = {
	"gpio8",
};
static const char * const qup2_se2_l1_groups[] = {
	"gpio9",
};
static const char * const qup2_se2_l2_groups[] = {
	"gpio10",
};
static const char * const qup2_se2_l3_groups[] = {
	"gpio11",
};
static const char * const qup2_se2_l4_groups[] = {
	"gpio13",
};
static const char * const qup2_se2_l5_groups[] = {
	"gpio15",
};
static const char * const qup2_se2_l6_groups[] = {
	"gpio12",
};
static const char * const qup2_se3_l0_groups[] = {
	"gpio12",
};
static const char * const qup2_se3_l1_groups[] = {
	"gpio13",
};
static const char * const qup2_se3_l2_groups[] = {
	"gpio14",
};
static const char * const qup2_se3_l3_groups[] = {
	"gpio15",
};
static const char * const qup2_se4_l0_groups[] = {
	"gpio16",
};
static const char * const qup2_se4_l1_groups[] = {
	"gpio17",
};
static const char * const qup2_se4_l2_groups[] = {
	"gpio18",
};
static const char * const qup2_se4_l3_groups[] = {
	"gpio19",
};
static const char * const qup2_se5_l0_groups[] = {
	"gpio20",
};
static const char * const qup2_se5_l1_groups[] = {
	"gpio21",
};
static const char * const qup2_se5_l2_groups[] = {
	"gpio22",
};
static const char * const qup2_se5_l3_groups[] = {
	"gpio23",
};
static const char * const qup2_se5_l6_groups[] = {
	"gpio23",
};
static const char * const qup2_se6_l0_groups[] = {
	"gpio24",
};
static const char * const qup2_se6_l1_groups[] = {
	"gpio25",
};
static const char * const qup2_se6_l2_groups[] = {
	"gpio26",
};
static const char * const qup2_se6_l3_groups[] = {
	"gpio27",
};
static const char * const qup2_se7_l0_groups[] = {
	"gpio28",
};
static const char * const qup2_se7_l1_groups[] = {
	"gpio29",
};
static const char * const qup2_se7_l2_groups[] = {
	"gpio30",
};
static const char * const qup2_se7_l3_groups[] = {
	"gpio31",
};
static const char * const sd_write_protect_groups[] = {
	"gpio93",
};
static const char * const sdc40_groups[] = {
	"gpio134",
};
static const char * const sdc41_groups[] = {
	"gpio136",
};
static const char * const sdc42_groups[] = {
	"gpio56",
};
static const char * const sdc43_groups[] = {
	"gpio57",
};
static const char * const sdc4_clk_groups[] = {
	"gpio135",
};
static const char * const sdc4_cmd_groups[] = {
	"gpio59",
};
static const char * const tb_trig_sdc2_groups[] = {
	"gpio8",
};
static const char * const tb_trig_sdc4_groups[] = {
	"gpio58",
};
static const char * const tgu_ch0_trigout_groups[] = {
	"gpio8",
};
static const char * const tgu_ch1_trigout_groups[] = {
	"gpio9",
};
static const char * const tgu_ch2_trigout_groups[] = {
	"gpio10",
};
static const char * const tgu_ch3_trigout_groups[] = {
	"gpio11",
};
static const char * const tmess_prng_rosc0_groups[] = {
	"gpio94",
};
static const char * const tmess_prng_rosc1_groups[] = {
	"gpio95",
};
static const char * const tmess_prng_rosc2_groups[] = {
	"gpio96",
};
static const char * const tmess_prng_rosc3_groups[] = {
	"gpio109",
};
static const char * const tsense_pwm1_out_groups[] = {
	"gpio58",
};
static const char * const tsense_pwm2_out_groups[] = {
	"gpio58",
};
static const char * const tsense_pwm3_out_groups[] = {
	"gpio58",
};
static const char * const uim0_clk_groups[] = {
	"gpio131",
};
static const char * const uim0_data_groups[] = {
	"gpio130",
};
static const char * const uim0_present_groups[] = {
	"gpio47",
};
static const char * const uim0_reset_groups[] = {
	"gpio132",
};
static const char * const uim1_clk_groups[] = {
	"gpio135",
};
static const char * const uim1_data_groups[] = {
	"gpio134",
};
static const char * const uim1_present_groups[] = {
	"gpio59",
};
static const char * const uim1_reset_groups[] = {
	"gpio136",
};
static const char * const usb1_hs_ac_groups[] = {
	"gpio89",
};
static const char * const usb_phy_ps_groups[] = {
	"gpio29", "gpio54",
};
static const char * const vfr_0_groups[] = {
	"gpio150",
};
static const char * const vfr_1_groups[] = {
	"gpio155",
};
static const char * const vsense_trigger_mirnat_groups[] = {
	"gpio60",
};

static const struct msm_function pineapple_functions[] = {
	FUNCTION(gpio),
	FUNCTION(aoss_cti_debug),
	FUNCTION(atest_char_start),
	FUNCTION(atest_char_status0),
	FUNCTION(atest_char_status1),
	FUNCTION(atest_char_status2),
	FUNCTION(atest_char_status3),
	FUNCTION(atest_usb0_atereset),
	FUNCTION(atest_usb0_testdataout00),
	FUNCTION(atest_usb0_testdataout01),
	FUNCTION(atest_usb0_testdataout02),
	FUNCTION(atest_usb0_testdataout03),
	FUNCTION(audio_ext_mclk0),
	FUNCTION(audio_ext_mclk1),
	FUNCTION(audio_ref_clk),
	FUNCTION(cam_aon_mclk4),
	FUNCTION(cam_mclk),
	FUNCTION(cci_async_in0),
	FUNCTION(cci_async_in1),
	FUNCTION(cci_async_in2),
	FUNCTION(cci_i2c_scl0),
	FUNCTION(cci_i2c_scl1),
	FUNCTION(cci_i2c_scl2),
	FUNCTION(cci_i2c_scl3),
	FUNCTION(cci_i2c_scl4),
	FUNCTION(cci_i2c_scl5),
	FUNCTION(cci_i2c_sda0),
	FUNCTION(cci_i2c_sda1),
	FUNCTION(cci_i2c_sda2),
	FUNCTION(cci_i2c_sda3),
	FUNCTION(cci_i2c_sda4),
	FUNCTION(cci_i2c_sda5),
	FUNCTION(cci_timer0),
	FUNCTION(cci_timer1),
	FUNCTION(cci_timer2),
	FUNCTION(cci_timer3),
	FUNCTION(cci_timer4),
	FUNCTION(cmu_rng_entropy0),
	FUNCTION(cmu_rng_entropy1),
	FUNCTION(cmu_rng_entropy2),
	FUNCTION(cmu_rng_entropy3),
	FUNCTION(coex_uart1_rx),
	FUNCTION(coex_uart1_tx),
	FUNCTION(coex_uart2_rx),
	FUNCTION(coex_uart2_tx),
	FUNCTION(cri_trng_rosc),
	FUNCTION(dbg_out_clk),
	FUNCTION(ddr_bist_complete),
	FUNCTION(ddr_bist_fail),
	FUNCTION(ddr_bist_start),
	FUNCTION(ddr_bist_stop),
	FUNCTION(ddr_pxi0_test),
	FUNCTION(ddr_pxi1_test),
	FUNCTION(ddr_pxi2_test),
	FUNCTION(ddr_pxi3_test),
	FUNCTION(do_not_expose),
	FUNCTION(do_not_expose0),
	FUNCTION(do_not_expose1),
	FUNCTION(do_not_expose2),
	FUNCTION(do_not_expose3),
	FUNCTION(dp_hot_plug),
	FUNCTION(gcc_gp1_clk),
	FUNCTION(gcc_gp2_clk),
	FUNCTION(gcc_gp3_clk),
	FUNCTION(gnss_adc_dtest0),
	FUNCTION(gnss_adc_dtest1),
	FUNCTION(i2chub0_se0_l0),
	FUNCTION(i2chub0_se0_l1),
	FUNCTION(i2chub0_se1_l0),
	FUNCTION(i2chub0_se1_l1),
	FUNCTION(i2chub0_se2_l0),
	FUNCTION(i2chub0_se2_l1),
	FUNCTION(i2chub0_se3_l0),
	FUNCTION(i2chub0_se3_l1),
	FUNCTION(i2chub0_se4_l0),
	FUNCTION(i2chub0_se4_l1),
	FUNCTION(i2chub0_se5_l0),
	FUNCTION(i2chub0_se5_l1),
	FUNCTION(i2chub0_se6_l0),
	FUNCTION(i2chub0_se6_l1),
	FUNCTION(i2chub0_se7_l0),
	FUNCTION(i2chub0_se7_l1),
	FUNCTION(i2chub0_se8_l0),
	FUNCTION(i2chub0_se8_l1),
	FUNCTION(i2chub0_se9_l0),
	FUNCTION(i2chub0_se9_l1),
	FUNCTION(i2s0_data0),
	FUNCTION(i2s0_data1),
	FUNCTION(i2s0_sck),
	FUNCTION(i2s0_ws),
	FUNCTION(i2s1_data0),
	FUNCTION(i2s1_data1),
	FUNCTION(i2s1_sck),
	FUNCTION(i2s1_ws),
	FUNCTION(ibi_i3c_qup1),
	FUNCTION(ibi_i3c_qup2),
	FUNCTION(jitter_bist_ref),
	FUNCTION(mdp_vsync0_out),
	FUNCTION(mdp_vsync1_out),
	FUNCTION(mdp_vsync2_out),
	FUNCTION(mdp_vsync3_out),
	FUNCTION(mdp_vsync_e),
	FUNCTION(mdp_vsync_p),
	FUNCTION(mdp_vsync_s),
	FUNCTION(nav_gpio0),
	FUNCTION(nav_gpio1),
	FUNCTION(nav_gpio2),
	FUNCTION(nav_gpio3),
	FUNCTION(pcie0_clk_req),
	FUNCTION(pcie1_clk_req),
	FUNCTION(phase_flag_status0),
	FUNCTION(phase_flag_status1),
	FUNCTION(phase_flag_status10),
	FUNCTION(phase_flag_status11),
	FUNCTION(phase_flag_status12),
	FUNCTION(phase_flag_status13),
	FUNCTION(phase_flag_status14),
	FUNCTION(phase_flag_status15),
	FUNCTION(phase_flag_status16),
	FUNCTION(phase_flag_status17),
	FUNCTION(phase_flag_status18),
	FUNCTION(phase_flag_status19),
	FUNCTION(phase_flag_status2),
	FUNCTION(phase_flag_status20),
	FUNCTION(phase_flag_status21),
	FUNCTION(phase_flag_status22),
	FUNCTION(phase_flag_status23),
	FUNCTION(phase_flag_status24),
	FUNCTION(phase_flag_status25),
	FUNCTION(phase_flag_status26),
	FUNCTION(phase_flag_status27),
	FUNCTION(phase_flag_status28),
	FUNCTION(phase_flag_status29),
	FUNCTION(phase_flag_status3),
	FUNCTION(phase_flag_status30),
	FUNCTION(phase_flag_status31),
	FUNCTION(phase_flag_status4),
	FUNCTION(phase_flag_status5),
	FUNCTION(phase_flag_status6),
	FUNCTION(phase_flag_status7),
	FUNCTION(phase_flag_status8),
	FUNCTION(phase_flag_status9),
	FUNCTION(pll_bist_sync),
	FUNCTION(pll_clk_aux),
	FUNCTION(prng_rosc_test0),
	FUNCTION(prng_rosc_test1),
	FUNCTION(prng_rosc_test2),
	FUNCTION(prng_rosc_test3),
	FUNCTION(qdss_cti_trig0),
	FUNCTION(qdss_cti_trig1),
	FUNCTION(qdss_gpio_traceclk),
	FUNCTION(qdss_gpio_tracectl),
	FUNCTION(qdss_gpio_tracedata0),
	FUNCTION(qdss_gpio_tracedata1),
	FUNCTION(qdss_gpio_tracedata10),
	FUNCTION(qdss_gpio_tracedata11),
	FUNCTION(qdss_gpio_tracedata12),
	FUNCTION(qdss_gpio_tracedata13),
	FUNCTION(qdss_gpio_tracedata14),
	FUNCTION(qdss_gpio_tracedata15),
	FUNCTION(qdss_gpio_tracedata2),
	FUNCTION(qdss_gpio_tracedata3),
	FUNCTION(qdss_gpio_tracedata4),
	FUNCTION(qdss_gpio_tracedata5),
	FUNCTION(qdss_gpio_tracedata6),
	FUNCTION(qdss_gpio_tracedata7),
	FUNCTION(qdss_gpio_tracedata8),
	FUNCTION(qdss_gpio_tracedata9),
	FUNCTION(qlink_big_enable),
	FUNCTION(qlink_big_request),
	FUNCTION(qlink_little_enable),
	FUNCTION(qlink_little_request),
	FUNCTION(qlink_wmss_reset),
	FUNCTION(qspi0),
	FUNCTION(qspi1),
	FUNCTION(qspi2),
	FUNCTION(qspi3),
	FUNCTION(qspi_clk),
	FUNCTION(qspi_cs_n),
	FUNCTION(qup1_se0_l0),
	FUNCTION(qup1_se0_l1),
	FUNCTION(qup1_se0_l2),
	FUNCTION(qup1_se0_l3),
	FUNCTION(qup1_se1_l0),
	FUNCTION(qup1_se1_l1),
	FUNCTION(qup1_se1_l2),
	FUNCTION(qup1_se1_l3),
	FUNCTION(qup1_se2_l0),
	FUNCTION(qup1_se2_l1),
	FUNCTION(qup1_se2_l2),
	FUNCTION(qup1_se2_l3),
	FUNCTION(qup1_se2_l4),
	FUNCTION(qup1_se2_l5),
	FUNCTION(qup1_se2_l6),
	FUNCTION(qup1_se3_l0),
	FUNCTION(qup1_se3_l1),
	FUNCTION(qup1_se3_l2),
	FUNCTION(qup1_se3_l3),
	FUNCTION(qup1_se4_l0),
	FUNCTION(qup1_se4_l1),
	FUNCTION(qup1_se4_l2),
	FUNCTION(qup1_se4_l3),
	FUNCTION(qup1_se5_l0),
	FUNCTION(qup1_se5_l1),
	FUNCTION(qup1_se5_l2),
	FUNCTION(qup1_se5_l3),
	FUNCTION(qup1_se6_l0),
	FUNCTION(qup1_se6_l1),
	FUNCTION(qup1_se6_l2),
	FUNCTION(qup1_se6_l3),
	FUNCTION(qup1_se7_l0),
	FUNCTION(qup1_se7_l1),
	FUNCTION(qup1_se7_l2),
	FUNCTION(qup1_se7_l3),
	FUNCTION(qup2_se0_l0),
	FUNCTION(qup2_se0_l1),
	FUNCTION(qup2_se0_l2),
	FUNCTION(qup2_se0_l3),
	FUNCTION(qup2_se1_l0),
	FUNCTION(qup2_se1_l1),
	FUNCTION(qup2_se1_l2),
	FUNCTION(qup2_se1_l3),
	FUNCTION(qup2_se2_l0),
	FUNCTION(qup2_se2_l1),
	FUNCTION(qup2_se2_l2),
	FUNCTION(qup2_se2_l3),
	FUNCTION(qup2_se2_l4),
	FUNCTION(qup2_se2_l5),
	FUNCTION(qup2_se2_l6),
	FUNCTION(qup2_se3_l0),
	FUNCTION(qup2_se3_l1),
	FUNCTION(qup2_se3_l2),
	FUNCTION(qup2_se3_l3),
	FUNCTION(qup2_se4_l0),
	FUNCTION(qup2_se4_l1),
	FUNCTION(qup2_se4_l2),
	FUNCTION(qup2_se4_l3),
	FUNCTION(qup2_se5_l0),
	FUNCTION(qup2_se5_l1),
	FUNCTION(qup2_se5_l2),
	FUNCTION(qup2_se5_l3),
	FUNCTION(qup2_se5_l6),
	FUNCTION(qup2_se6_l0),
	FUNCTION(qup2_se6_l1),
	FUNCTION(qup2_se6_l2),
	FUNCTION(qup2_se6_l3),
	FUNCTION(qup2_se7_l0),
	FUNCTION(qup2_se7_l1),
	FUNCTION(qup2_se7_l2),
	FUNCTION(qup2_se7_l3),
	FUNCTION(sd_write_protect),
	FUNCTION(sdc40),
	FUNCTION(sdc41),
	FUNCTION(sdc42),
	FUNCTION(sdc43),
	FUNCTION(sdc4_clk),
	FUNCTION(sdc4_cmd),
	FUNCTION(tb_trig_sdc2),
	FUNCTION(tb_trig_sdc4),
	FUNCTION(tgu_ch0_trigout),
	FUNCTION(tgu_ch1_trigout),
	FUNCTION(tgu_ch2_trigout),
	FUNCTION(tgu_ch3_trigout),
	FUNCTION(tmess_prng_rosc0),
	FUNCTION(tmess_prng_rosc1),
	FUNCTION(tmess_prng_rosc2),
	FUNCTION(tmess_prng_rosc3),
	FUNCTION(tsense_pwm1_out),
	FUNCTION(tsense_pwm2_out),
	FUNCTION(tsense_pwm3_out),
	FUNCTION(uim0_clk),
	FUNCTION(uim0_data),
	FUNCTION(uim0_present),
	FUNCTION(uim0_reset),
	FUNCTION(uim1_clk),
	FUNCTION(uim1_data),
	FUNCTION(uim1_present),
	FUNCTION(uim1_reset),
	FUNCTION(usb1_hs_ac),
	FUNCTION(usb_phy_ps),
	FUNCTION(vfr_0),
	FUNCTION(vfr_1),
	FUNCTION(vsense_trigger_mirnat),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup pineapple_groups[] = {
	[0] = PINGROUP(0, qup2_se0_l0, ibi_i3c_qup2, phase_flag_status0, NA,
		       NA, NA, NA, NA, NA, 0, -1),
	[1] = PINGROUP(1, qup2_se0_l1, ibi_i3c_qup2, phase_flag_status1, NA,
		       NA, NA, NA, NA, NA, 0, -1),
	[2] = PINGROUP(2, qup2_se0_l2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[3] = PINGROUP(3, qup2_se0_l3, phase_flag_status2, NA,
		       qdss_gpio_tracedata6, NA, NA, NA, NA, NA, 0, -1),
	[4] = PINGROUP(4, qup2_se1_l0, ibi_i3c_qup2, phase_flag_status3, NA,
		       NA, NA, NA, NA, NA, 0, -1),
	[5] = PINGROUP(5, qup2_se1_l1, ibi_i3c_qup2, phase_flag_status4, NA,
		       NA, NA, NA, NA, NA, 0, -1),
	[6] = PINGROUP(6, qup2_se1_l2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[7] = PINGROUP(7, qup2_se1_l3, phase_flag_status5, NA,
		       qdss_gpio_tracedata15, NA, NA, NA, NA, NA, 0, -1),
	[8] = PINGROUP(8, qup2_se2_l0, ibi_i3c_qup2, tb_trig_sdc2,
		       phase_flag_status6, tgu_ch0_trigout, NA,
		       qdss_gpio_tracedata4, NA, NA, 0, -1),
	[9] = PINGROUP(9, qup2_se2_l1, ibi_i3c_qup2, phase_flag_status7,
		       tgu_ch1_trigout, NA, NA, NA, NA, NA, 0, -1),
	[10] = PINGROUP(10, qup2_se2_l2, cci_timer2, tgu_ch2_trigout, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[11] = PINGROUP(11, qup2_se2_l3, cci_timer3, phase_flag_status8,
			tgu_ch3_trigout, NA, NA, NA, NA, NA, 0, -1),
	[12] = PINGROUP(12, qup2_se3_l0, cci_i2c_sda3, ibi_i3c_qup2,
			qup2_se2_l6, phase_flag_status9, NA, NA, NA, NA, 0, -1),
	[13] = PINGROUP(13, qup2_se3_l1, cci_i2c_scl3, ibi_i3c_qup2,
			qup2_se2_l4, phase_flag_status18, NA,
			qdss_gpio_tracedata13, NA, NA, 0, -1),
	[14] = PINGROUP(14, qup2_se3_l2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[15] = PINGROUP(15, qup2_se3_l3, cci_async_in0, qup2_se2_l5,
			phase_flag_status10, NA, qdss_gpio_tracedata14, NA, NA,
			NA, 0, -1),
	[16] = PINGROUP(16, qup2_se4_l0, phase_flag_status11, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[17] = PINGROUP(17, qup2_se4_l1, phase_flag_status12, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[18] = PINGROUP(18, qup2_se4_l2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[19] = PINGROUP(19, qup2_se4_l3, phase_flag_status13, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[20] = PINGROUP(20, qup2_se5_l0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[21] = PINGROUP(21, qup2_se5_l1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[22] = PINGROUP(22, qup2_se5_l2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[23] = PINGROUP(23, qup2_se5_l3, qup2_se5_l6, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[24] = PINGROUP(24, qup2_se6_l0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[25] = PINGROUP(25, qup2_se6_l1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[26] = PINGROUP(26, qup2_se6_l2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[27] = PINGROUP(27, qup2_se6_l3, qdss_cti_trig1, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[28] = PINGROUP(28, qup2_se7_l0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[29] = PINGROUP(29, qup2_se7_l1, usb_phy_ps, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[30] = PINGROUP(30, qup2_se7_l2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[31] = PINGROUP(31, qup2_se7_l3, qdss_cti_trig0, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[32] = PINGROUP(32, qup1_se0_l0, ibi_i3c_qup1, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[33] = PINGROUP(33, qup1_se0_l1, ibi_i3c_qup1, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[34] = PINGROUP(34, qup1_se0_l2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[35] = PINGROUP(35, qup1_se0_l3, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[36] = PINGROUP(36, qup1_se1_l0, do_not_expose0, ibi_i3c_qup1, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[37] = PINGROUP(37, qup1_se1_l1, do_not_expose1, ibi_i3c_qup1, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[38] = PINGROUP(38, qup1_se1_l2, do_not_expose2, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[39] = PINGROUP(39, qup1_se1_l3, do_not_expose3, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[40] = PINGROUP(40, qup1_se2_l0, ddr_bist_fail, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[41] = PINGROUP(41, qup1_se2_l1, ddr_bist_start, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[42] = PINGROUP(42, qup1_se2_l2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[43] = PINGROUP(43, qup1_se2_l3, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[44] = PINGROUP(44, qup1_se3_l0, qup1_se2_l4, ddr_bist_complete,
			ddr_pxi1_test, NA, NA, NA, NA, NA, 0, -1),
	[45] = PINGROUP(45, qup1_se3_l1, qup1_se2_l5, ddr_bist_stop,
			ddr_pxi1_test, NA, NA, NA, NA, NA, 0, -1),
	[46] = PINGROUP(46, qup1_se3_l2, qup1_se2_l6, ddr_pxi3_test, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[47] = PINGROUP(47, qup1_se3_l3, uim0_present, dp_hot_plug,
			ddr_pxi3_test, NA, NA, NA, NA, NA, 0, -1),
	[48] = PINGROUP(48, qup1_se4_l0, ibi_i3c_qup1, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[49] = PINGROUP(49, qup1_se4_l1, ibi_i3c_qup1, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[50] = PINGROUP(50, qup1_se4_l2, aoss_cti_debug, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[51] = PINGROUP(51, qup1_se4_l3, aoss_cti_debug, ddr_pxi2_test, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[52] = PINGROUP(52, qup1_se5_l0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[53] = PINGROUP(53, qup1_se5_l1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[54] = PINGROUP(54, qup1_se5_l2, usb_phy_ps, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[55] = PINGROUP(55, qup1_se5_l3, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[56] = PINGROUP(56, qup1_se6_l0, ibi_i3c_qup1, qspi2, sdc42, NA, NA,
			NA, NA, NA, 0, -1),
	[57] = PINGROUP(57, qup1_se6_l1, ibi_i3c_qup1, qspi3, sdc43, NA, NA,
			NA, NA, NA, 0, -1),
	[58] = PINGROUP(58, qup1_se6_l2, qspi_cs_n, tb_trig_sdc4,
			tsense_pwm1_out, tsense_pwm2_out, tsense_pwm3_out, NA,
			NA, NA, 0, -1),
	[59] = PINGROUP(59, qup1_se6_l3, uim1_present, qspi_cs_n, sdc4_cmd, NA,
			NA, NA, NA, NA, 0, -1),
	[60] = PINGROUP(60, qup1_se7_l0, aoss_cti_debug, vsense_trigger_mirnat,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[61] = PINGROUP(61, qup1_se7_l1, aoss_cti_debug, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[62] = PINGROUP(62, qup1_se7_l2, ddr_pxi2_test, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[63] = PINGROUP(63, qup1_se7_l3, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[64] = PINGROUP(64, i2chub0_se0_l0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[65] = PINGROUP(65, i2chub0_se0_l1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[66] = PINGROUP(66, i2chub0_se1_l0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[67] = PINGROUP(67, i2chub0_se1_l1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[68] = PINGROUP(68, i2chub0_se2_l0, pll_bist_sync, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[69] = PINGROUP(69, i2chub0_se2_l1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[70] = PINGROUP(70, i2chub0_se3_l0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[71] = PINGROUP(71, i2chub0_se3_l1, NA, atest_usb0_testdataout02, NA,
			NA, NA, NA, NA, NA, 0, -1),
	[72] = PINGROUP(72, i2chub0_se4_l0, NA, atest_usb0_testdataout03, NA,
			NA, NA, NA, NA, NA, 0, -1),
	[73] = PINGROUP(73, i2chub0_se4_l1, jitter_bist_ref, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[74] = PINGROUP(74, i2chub0_se5_l0, atest_usb0_atereset, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[75] = PINGROUP(75, i2chub0_se5_l1, ddr_pxi0_test, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[76] = PINGROUP(76, i2chub0_se6_l0, ddr_pxi0_test, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[77] = PINGROUP(77, i2chub0_se6_l1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[78] = PINGROUP(78, i2chub0_se7_l0, qdss_cti_trig0, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[79] = PINGROUP(79, i2chub0_se7_l1, qdss_cti_trig1, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[80] = PINGROUP(80, i2chub0_se9_l0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[81] = PINGROUP(81, i2chub0_se9_l1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[82] = PINGROUP(82, qdss_cti_trig0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[83] = PINGROUP(83, qdss_cti_trig1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[84] = PINGROUP(84, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[85] = PINGROUP(85, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[86] = PINGROUP(86, mdp_vsync_p, mdp_vsync0_out, mdp_vsync1_out,
			gcc_gp1_clk, NA, NA, NA, NA, NA, 0, -1),
	[87] = PINGROUP(87, mdp_vsync_s, mdp_vsync2_out, mdp_vsync3_out,
			gcc_gp2_clk, NA, NA, NA, NA, NA, 0, -1),
	[88] = PINGROUP(88, mdp_vsync_e, gcc_gp3_clk, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[89] = PINGROUP(89, usb1_hs_ac, gnss_adc_dtest0, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[90] = PINGROUP(90, gnss_adc_dtest1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[91] = PINGROUP(91, NA, gnss_adc_dtest0, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[92] = PINGROUP(92, dbg_out_clk, gnss_adc_dtest1, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[93] = PINGROUP(93, sd_write_protect, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[94] = PINGROUP(94, cmu_rng_entropy0, phase_flag_status14,
			tmess_prng_rosc0, NA, NA, NA, NA, NA, NA, 0, -1),
	[95] = PINGROUP(95, pcie0_clk_req, cmu_rng_entropy1,
			phase_flag_status15, tmess_prng_rosc1, NA, NA, NA, NA,
			NA, 0, -1),
	[96] = PINGROUP(96, cmu_rng_entropy2, phase_flag_status16,
			tmess_prng_rosc2, NA, NA, NA, NA, NA, NA, 0, -1),
	[97] = PINGROUP(97, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[98] = PINGROUP(98, pcie1_clk_req, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[99] = PINGROUP(99, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[100] = PINGROUP(100, cam_mclk, qdss_gpio_tracedata11, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[101] = PINGROUP(101, cam_mclk, qdss_gpio_tracedata12, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[102] = PINGROUP(102, cam_mclk, qdss_gpio_tracedata0, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[103] = PINGROUP(103, cam_mclk, qdss_gpio_tracedata1, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[104] = PINGROUP(104, cam_aon_mclk4, qdss_gpio_tracedata2, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[105] = PINGROUP(105, cam_mclk, qdss_gpio_tracedata3, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[106] = PINGROUP(106, cam_mclk, pll_clk_aux, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[107] = PINGROUP(107, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[108] = PINGROUP(108, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[109] = PINGROUP(109, cci_timer0, phase_flag_status17,
			 tmess_prng_rosc3, NA, NA, NA, NA, NA, NA, 0, -1),
	[110] = PINGROUP(110, cci_timer1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[111] = PINGROUP(111, cci_timer4, phase_flag_status19, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[112] = PINGROUP(112, cci_i2c_sda4, cmu_rng_entropy3,
			 phase_flag_status20, NA, NA, NA, NA, NA, NA, 0, -1),
	[113] = PINGROUP(113, cci_i2c_sda0, phase_flag_status21, NA,
			 qdss_gpio_tracedata7, NA, NA, NA, NA, NA, 0, -1),
	[114] = PINGROUP(114, cci_i2c_scl0, phase_flag_status22, NA,
			 qdss_gpio_tracedata8, NA, NA, NA, NA, NA, 0, -1),
	[115] = PINGROUP(115, cci_i2c_sda1, phase_flag_status23, NA,
			 qdss_gpio_traceclk, NA, NA, NA, NA, NA, 0, -1),
	[116] = PINGROUP(116, cci_i2c_scl1, phase_flag_status24, NA,
			 qdss_gpio_tracectl, NA, NA, NA, NA, NA, 0, -1),
	[117] = PINGROUP(117, cci_i2c_sda2, phase_flag_status25, NA,
			 qdss_gpio_tracedata9, NA, NA, NA, NA, NA, 0, -1),
	[118] = PINGROUP(118, cci_i2c_scl2, phase_flag_status26, NA,
			 qdss_gpio_tracedata10, NA, NA, NA, NA, NA, 0, -1),
	[119] = PINGROUP(119, cci_i2c_sda5, phase_flag_status27, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[120] = PINGROUP(120, cci_i2c_scl5, phase_flag_status28, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[121] = PINGROUP(121, i2s1_sck, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[122] = PINGROUP(122, i2s1_data0, cmu_rng_entropy3, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[123] = PINGROUP(123, i2s1_ws, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[124] = PINGROUP(124, i2s1_data1, audio_ext_mclk1, audio_ref_clk, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[125] = PINGROUP(125, audio_ext_mclk0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[126] = PINGROUP(126, i2s0_sck, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[127] = PINGROUP(127, i2s0_data0, cmu_rng_entropy2, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[128] = PINGROUP(128, i2s0_data1, cmu_rng_entropy1, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[129] = PINGROUP(129, i2s0_ws, cmu_rng_entropy0, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[130] = PINGROUP(130, uim0_data, atest_usb0_testdataout01,
			 atest_char_status3, NA, NA, NA, NA, NA, NA, 0, -1),
	[131] = PINGROUP(131, uim0_clk, atest_usb0_testdataout00,
			 atest_char_status2, NA, NA, NA, NA, NA, NA, 0, -1),
	[132] = PINGROUP(132, uim0_reset, atest_char_status1, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[133] = PINGROUP(133, mdp_vsync_s, atest_char_status0, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[134] = PINGROUP(134, uim1_data, do_not_expose, qspi0, sdc40,
			 gcc_gp1_clk, NA, NA, NA, NA, 0, -1),
	[135] = PINGROUP(135, uim1_clk, do_not_expose, qspi_clk, sdc4_clk,
			 gcc_gp2_clk, NA, NA, NA, NA, 0, -1),
	[136] = PINGROUP(136, uim1_reset, do_not_expose, qspi1, sdc41,
			 gcc_gp3_clk, NA, NA, NA, NA, 0, -1),
	[137] = PINGROUP(137, mdp_vsync_p, atest_char_start, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[138] = PINGROUP(138, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[139] = PINGROUP(139, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[140] = PINGROUP(140, NA, NA, qdss_gpio_tracedata0, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[141] = PINGROUP(141, NA, NA, qdss_gpio_tracedata1, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[142] = PINGROUP(142, NA, NA, qdss_gpio_tracedata2, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[143] = PINGROUP(143, NA, NA, qdss_gpio_tracedata3, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[144] = PINGROUP(144, NA, qdss_gpio_tracedata4, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[145] = PINGROUP(145, NA, qdss_gpio_tracedata5, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[146] = PINGROUP(146, NA, qdss_gpio_tracedata6, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[147] = PINGROUP(147, NA, qdss_gpio_tracedata7, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[148] = PINGROUP(148, coex_uart1_rx, qdss_gpio_tracectl, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[149] = PINGROUP(149, coex_uart1_tx, qdss_gpio_traceclk, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[150] = PINGROUP(150, NA, vfr_0, coex_uart2_rx, qdss_gpio_tracedata8,
			 NA, NA, NA, NA, NA, 0, -1),
	[151] = PINGROUP(151, NA, coex_uart2_tx, qdss_gpio_tracedata9, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[152] = PINGROUP(152, nav_gpio2, NA, qdss_gpio_tracedata10, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[153] = PINGROUP(153, cci_i2c_scl4, phase_flag_status29, NA,
			 qdss_gpio_tracedata5, NA, NA, NA, NA, NA, 0, -1),
	[154] = PINGROUP(154, nav_gpio0, nav_gpio3, qdss_gpio_tracedata11, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[155] = PINGROUP(155, nav_gpio1, vfr_1, qdss_gpio_tracedata12, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[156] = PINGROUP(156, qlink_little_request, qdss_gpio_tracedata13, NA,
			 NA, NA, NA, NA, NA, NA, 0, -1),
	[157] = PINGROUP(157, qlink_little_enable, qdss_gpio_tracedata14, NA,
			 NA, NA, NA, NA, NA, NA, 0, -1),
	[158] = PINGROUP(158, qlink_wmss_reset, qdss_gpio_tracedata15, NA, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[159] = PINGROUP(159, qlink_big_request, qdss_cti_trig1, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[160] = PINGROUP(160, qlink_big_enable, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[161] = PINGROUP(161, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[162] = PINGROUP(162, qdss_cti_trig0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[163] = PINGROUP(163, cci_async_in1, phase_flag_status30, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[164] = PINGROUP(164, cci_async_in2, phase_flag_status31, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[165] = PINGROUP(165, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[166] = PINGROUP(166, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[167] = PINGROUP(167, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[168] = PINGROUP(168, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[169] = PINGROUP(169, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[170] = PINGROUP(170, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[171] = PINGROUP(171, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[172] = PINGROUP(172, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[173] = PINGROUP(173, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[174] = PINGROUP(174, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[175] = PINGROUP(175, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[176] = PINGROUP(176, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[177] = PINGROUP(177, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[178] = PINGROUP(178, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[179] = PINGROUP(179, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[180] = PINGROUP(180, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[181] = PINGROUP(181, prng_rosc_test3, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[182] = PINGROUP(182, prng_rosc_test2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[183] = PINGROUP(183, prng_rosc_test1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[184] = PINGROUP(184, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[185] = PINGROUP(185, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[186] = PINGROUP(186, prng_rosc_test0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[187] = PINGROUP(187, cri_trng_rosc, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[188] = PINGROUP(188, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[189] = PINGROUP(189, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[190] = PINGROUP(190, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[191] = PINGROUP(191, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[192] = PINGROUP(192, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[193] = PINGROUP(193, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[194] = PINGROUP(194, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[195] = PINGROUP(195, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[196] = PINGROUP(196, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[197] = PINGROUP(197, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[198] = PINGROUP(198, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[199] = PINGROUP(199, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[200] = PINGROUP(200, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[201] = PINGROUP(201, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[202] = PINGROUP(202, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[203] = PINGROUP(203, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[204] = PINGROUP(204, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[205] = PINGROUP(205, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[206] = PINGROUP(206, i2chub0_se8_l0, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[207] = PINGROUP(207, i2chub0_se8_l1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[208] = PINGROUP(208, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[209] = PINGROUP(209, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[210] = UFS_RESET(ufs_reset, 0x1de004),
	[211] = SDC_QDSD_PINGROUP(sdc2_clk, 0x1d6000, 14, 6),
	[212] = SDC_QDSD_PINGROUP(sdc2_cmd, 0x1d6000, 11, 3),
	[213] = SDC_QDSD_PINGROUP(sdc2_data, 0x1d6000, 9, 0),
};

/*
 * The first arguments to QUP_I3C below have been chosen to be monotonically
 * increasing integers so as to be able to cater to two QUP blocks.
 */
static struct pinctrl_qup pineapple_qup_regs[] = {
	QUP_I3C(1, QUP_1_I3C_0_MODE_OFFSET),
	QUP_I3C(2, QUP_1_I3C_1_MODE_OFFSET),
	QUP_I3C(3, QUP_1_I3C_4_MODE_OFFSET),
	QUP_I3C(4, QUP_1_I3C_6_MODE_OFFSET),
	QUP_I3C(5, QUP_2_I3C_0_MODE_OFFSET),
	QUP_I3C(6, QUP_2_I3C_1_MODE_OFFSET),
	QUP_I3C(7, QUP_2_I3C_2_MODE_OFFSET),
	QUP_I3C(8, QUP_2_I3C_3_MODE_OFFSET),
};

static const struct msm_pinctrl_soc_data pineapple_pinctrl = {
	.pins = pineapple_pins,
	.npins = ARRAY_SIZE(pineapple_pins),
	.functions = pineapple_functions,
	.nfunctions = ARRAY_SIZE(pineapple_functions),
	.groups = pineapple_groups,
	.ngroups = ARRAY_SIZE(pineapple_groups),
	.ngpios = 211,
	.qup_regs = pineapple_qup_regs,
	.nqup_regs = ARRAY_SIZE(pineapple_qup_regs),
};

static int pineapple_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &pineapple_pinctrl);
}

static const struct of_device_id pineapple_pinctrl_of_match[] = {
	{ .compatible = "qcom,pineapple-pinctrl", },
	{ },
};

static struct platform_driver pineapple_pinctrl_driver = {
	.driver = {
		.name = "pineapple-pinctrl",
		.of_match_table = pineapple_pinctrl_of_match,
	},
	.probe = pineapple_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init pineapple_pinctrl_init(void)
{
	return platform_driver_register(&pineapple_pinctrl_driver);
}
arch_initcall(pineapple_pinctrl_init);

static void __exit pineapple_pinctrl_exit(void)
{
	platform_driver_unregister(&pineapple_pinctrl_driver);
}
module_exit(pineapple_pinctrl_exit);

MODULE_DESCRIPTION("QTI pineapple pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, pineapple_pinctrl_of_match);
