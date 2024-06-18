// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
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
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, wake_off, bit)	\
	{					        \
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = (unsigned int)ARRAY_SIZE(gpio##id##_pins),	\
		.ctl_reg = REG_BASE + REG_SIZE * id,			\
		.io_reg = REG_BASE + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = REG_BASE + 0x8 + REG_SIZE * id,		\
		.intr_status_reg = REG_BASE + 0xc + REG_SIZE * id,	\
		.intr_target_reg = REG_BASE + 0x8 + REG_SIZE * id,	\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
		.egpio_enable = 12,		\
		.egpio_present = 11,	\
		.oe_bit = 9,			\
		.in_bit = 0,			\
		.out_bit = 1,			\
		.intr_enable_bit = 0,		\
		.intr_status_bit = 0,		\
		.intr_target_bit = 8,		\
		.intr_wakeup_enable_bit = 7,	\
		.intr_wakeup_present_bit = 6,	\
		.intr_target_kpss_val = 3,	\
		.intr_raw_status_bit = 4,	\
		.intr_polarity_bit = 1,		\
		.intr_detection_bit = 2,	\
		.intr_detection_width = 2,	\
		.wake_reg = REG_BASE + wake_off,	\
		.wake_bit = bit,		\
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
			msm_mux_##f9,			\
			msm_mux_##f10,			\
			msm_mux_##f11 /* egpio mode */	\
		},					        \
		.nfuncs = 12,				\
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

#define UFS_RESET(pg_name, offset, io)				\
	{					        \
		.name = #pg_name,			\
		.pins = pg_name##_pins,			\
		.npins = (unsigned int)ARRAY_SIZE(pg_name##_pins),	\
		.ctl_reg = offset,			\
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

#define QUP_I3C(qup_mode, qup_offset)			\
	{						\
		.mode = qup_mode,			\
		.offset = REG_BASE + qup_offset,			\
	}

#define QUP_2_I3C_0_MODE_OFFSET	0xE0000
#define QUP_2_I3C_1_MODE_OFFSET	0xE1000
#define QUP_3_I3C_0_MODE_OFFSET	0xE2000
#define QUP_1_I3C_0_MODE_OFFSET	0xE3000

static const struct pinctrl_pin_desc niobe_pins[] = {
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
	PINCTRL_PIN(200, "UFS_RESET"),
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

static const unsigned int ufs_reset_pins[] = { 200 };

enum niobe_functions {
	msm_mux_gpio,
	msm_mux_RESOUT_GPIO_N,
	msm_mux_SYS_THROTTLE_MIRA,
	msm_mux_SYS_THROTTLE_MIRB,
	msm_mux_USB0_PHY,
	msm_mux_USB1_PHY,
	msm_mux_aoss_cti,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_atest_char_start,
	msm_mux_atest_usb0,
	msm_mux_atest_usb00,
	msm_mux_atest_usb01,
	msm_mux_atest_usb02,
	msm_mux_atest_usb03,
	msm_mux_atest_usb1,
	msm_mux_atest_usb10,
	msm_mux_atest_usb11,
	msm_mux_atest_usb12,
	msm_mux_atest_usb13,
	msm_mux_audio_ref_clk,
	msm_mux_cam_mclk,
	msm_mux_cam_mclk10_mirb,
	msm_mux_cam_mclk4_mirb,
	msm_mux_cci01_async_in0,
	msm_mux_cci01_async_in1,
	msm_mux_cci01_async_in2,
	msm_mux_cci01_timer0,
	msm_mux_cci01_timer1,
	msm_mux_cci01_timer2,
	msm_mux_cci01_timer3,
	msm_mux_cci01_timer4,
	msm_mux_cci0_i2c,
	msm_mux_cci0_i2c_scl0,
	msm_mux_cci0_i2c_sda0,
	msm_mux_cci1_i2c,
	msm_mux_cci1_i2c_scl2,
	msm_mux_cci1_i2c_sda2,
	msm_mux_cci23_async_in0,
	msm_mux_cci23_async_in1,
	msm_mux_cci23_async_in2,
	msm_mux_cci23_timer0,
	msm_mux_cci23_timer1,
	msm_mux_cci23_timer2,
	msm_mux_cci23_timer3,
	msm_mux_cci23_timer4,
	msm_mux_cci2_i2c_scl4,
	msm_mux_cci2_i2c_scl5,
	msm_mux_cci2_i2c_sda4,
	msm_mux_cci2_i2c_sda5,
	msm_mux_cci3_i2c_scl6,
	msm_mux_cci3_i2c_scl7,
	msm_mux_cci3_i2c_sda6,
	msm_mux_cci3_i2c_sda7,
	msm_mux_cci45_async,
	msm_mux_cci45_timer0,
	msm_mux_cci45_timer1,
	msm_mux_cci45_timer2,
	msm_mux_cci45_timer3_mira,
	msm_mux_cci45_timer3_mirb,
	msm_mux_cci45_timer4_mira,
	msm_mux_cci45_timer4_mirb,
	msm_mux_cci4_i2c,
	msm_mux_cci4_i2c_scl8,
	msm_mux_cci4_i2c_sda8,
	msm_mux_cci5_i2c,
	msm_mux_cci5_i2c_scl10,
	msm_mux_cci5_i2c_sda10,
	msm_mux_cmu_rng0,
	msm_mux_cmu_rng1,
	msm_mux_cmu_rng2,
	msm_mux_cmu_rng3,
	msm_mux_cri_trng,
	msm_mux_dbg_out_clk,
	msm_mux_ddr_bist_complete,
	msm_mux_ddr_bist_fail,
	msm_mux_ddr_bist_start,
	msm_mux_ddr_bist_stop,
	msm_mux_ddr_pxi0,
	msm_mux_ddr_pxi1,
	msm_mux_ddr_pxi2,
	msm_mux_ddr_pxi3,
	msm_mux_dp0_hot,
	msm_mux_edp0_hot,
	msm_mux_edp0_lcd,
	msm_mux_edp1_dpu0,
	msm_mux_edp1_dpu1,
	msm_mux_edp1_hot,
	msm_mux_egpio,
	msm_mux_ext_mclk0,
	msm_mux_ext_mclk1,
	msm_mux_gcc_gp,
	msm_mux_gcc_gp_clk10,
	msm_mux_gcc_gp_clk11,
	msm_mux_gcc_gp_clk4,
	msm_mux_gcc_gp_clk5,
	msm_mux_gcc_gp_clk6,
	msm_mux_gcc_gp_clk7,
	msm_mux_gcc_gp_clk8,
	msm_mux_gcc_gp_clk9,
	msm_mux_i2s0_data0,
	msm_mux_i2s0_data1,
	msm_mux_i2s0_sck,
	msm_mux_i2s0_ws,
	msm_mux_i2s2_data0,
	msm_mux_i2s2_data1,
	msm_mux_i2s2_sck,
	msm_mux_i2s2_ws,
	msm_mux_ibi_i3c,
	msm_mux_jitter_bist,
	msm_mux_mdp0_vsync0_mira,
	msm_mux_mdp0_vsync0_mirb,
	msm_mux_mdp0_vsync0_out,
	msm_mux_mdp0_vsync1_mira,
	msm_mux_mdp0_vsync1_mirb,
	msm_mux_mdp0_vsync1_out,
	msm_mux_mdp0_vsync2_out,
	msm_mux_mdp0_vsync3_out,
	msm_mux_mdp0_vsync4_out,
	msm_mux_mdp0_vsync5_out,
	msm_mux_mdp0_vsync6_out,
	msm_mux_mdp0_vsync7_out,
	msm_mux_mdp0_vsync8_out,
	msm_mux_mdp1_vsync0_mira,
	msm_mux_mdp1_vsync0_mirb,
	msm_mux_mdp1_vsync0_out,
	msm_mux_mdp1_vsync1_mira,
	msm_mux_mdp1_vsync1_mirb,
	msm_mux_mdp1_vsync1_out,
	msm_mux_mdp1_vsync2_out,
	msm_mux_mdp1_vsync3_out,
	msm_mux_mdp1_vsync4_out,
	msm_mux_mdp1_vsync5_out,
	msm_mux_mdp1_vsync6_out,
	msm_mux_mdp1_vsync7_out,
	msm_mux_mdp1_vsync8_out,
	msm_mux_pcie0_clk_req_n,
	msm_mux_pcie1_clk_req_n,
	msm_mux_pcie2_clk_req_n,
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
	msm_mux_pll_bist_sync,
	msm_mux_pll_clk_aux,
	msm_mux_prng_rosc0,
	msm_mux_prng_rosc1,
	msm_mux_prng_rosc2,
	msm_mux_prng_rosc3,
	msm_mux_pwm_0,
	msm_mux_pwm_1,
	msm_mux_pwm_10,
	msm_mux_pwm_11,
	msm_mux_pwm_12,
	msm_mux_pwm_13,
	msm_mux_pwm_14,
	msm_mux_pwm_15,
	msm_mux_pwm_16,
	msm_mux_pwm_17,
	msm_mux_pwm_18,
	msm_mux_pwm_19,
	msm_mux_pwm_2,
	msm_mux_pwm_3,
	msm_mux_pwm_4,
	msm_mux_pwm_5,
	msm_mux_pwm_6,
	msm_mux_pwm_7,
	msm_mux_pwm_8,
	msm_mux_pwm_9,
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
	msm_mux_qup1_se0_l0,
	msm_mux_qup1_se0_l1,
	msm_mux_qup1_se0_l2,
	msm_mux_qup1_se0_l3,
	msm_mux_qup1_se1_l0,
	msm_mux_qup1_se1_l1,
	msm_mux_qup1_se1_l2,
	msm_mux_qup1_se1_l3,
	msm_mux_qup1_se1_l4,
	msm_mux_qup1_se1_l5,
	msm_mux_qup1_se1_l6,
	msm_mux_qup1_se2_l0,
	msm_mux_qup1_se2_l1,
	msm_mux_qup1_se2_l2,
	msm_mux_qup1_se2_l3,
	msm_mux_qup1_se3_l0,
	msm_mux_qup1_se3_l1,
	msm_mux_qup1_se3_l2,
	msm_mux_qup1_se3_l3,
	msm_mux_qup1_se3_l4,
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
	msm_mux_qup2_se2_l3_mira,
	msm_mux_qup2_se2_l3_mirb,
	msm_mux_qup2_se3_l0,
	msm_mux_qup2_se3_l1,
	msm_mux_qup2_se3_l2,
	msm_mux_qup2_se3_l3,
	msm_mux_qup2_se4_l0,
	msm_mux_qup2_se4_l1,
	msm_mux_qup2_se4_l2,
	msm_mux_qup2_se4_l3,
	msm_mux_qup2_se4_l4,
	msm_mux_qup2_se4_l5,
	msm_mux_qup2_se4_l6,
	msm_mux_qup2_se5_l0,
	msm_mux_qup2_se5_l1,
	msm_mux_qup2_se5_l2,
	msm_mux_qup2_se5_l3_mira,
	msm_mux_qup2_se5_l3_mirb,
	msm_mux_qup2_se6_l0,
	msm_mux_qup2_se6_l1,
	msm_mux_qup2_se6_l2,
	msm_mux_qup2_se6_l3,
	msm_mux_qup2_se7_l0,
	msm_mux_qup2_se7_l1,
	msm_mux_qup2_se7_l2,
	msm_mux_qup2_se7_l3,
	msm_mux_qup2_se7_l4,
	msm_mux_qup3_se0_l0,
	msm_mux_qup3_se0_l1,
	msm_mux_qup3_se0_l2,
	msm_mux_qup3_se0_l3,
	msm_mux_qup3_se0_l6,
	msm_mux_qup3_se1_l0,
	msm_mux_qup3_se1_l1,
	msm_mux_qup3_se1_l2,
	msm_mux_qup3_se1_l3,
	msm_mux_qup3_se2_l0,
	msm_mux_qup3_se2_l1,
	msm_mux_qup3_se2_l2,
	msm_mux_qup3_se2_l3,
	msm_mux_qup3_se3_l0,
	msm_mux_qup3_se3_l1,
	msm_mux_qup3_se3_l2,
	msm_mux_qup3_se3_l3,
	msm_mux_sd_write_protect,
	msm_mux_sdc2_data,
	msm_mux_sdc2_clk,
	msm_mux_sdc2_cmd,
	msm_mux_sdc2_fb_clk,
	msm_mux_sdcc5_vdd2_on,
	msm_mux_tb_trig_sdc2,
	msm_mux_tgu_ch0_trigout,
	msm_mux_tgu_ch1_trigout,
	msm_mux_tgu_ch2_trigout,
	msm_mux_tgu_ch3_trigout,
	msm_mux_tmess_prng0,
	msm_mux_tmess_prng1,
	msm_mux_tmess_prng2,
	msm_mux_tmess_prng3,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_tsense_pwm3,
	msm_mux_usb0_hs,
	msm_mux_usb1_hs,
	msm_mux_vsense_trigger_mirnat,
	msm_mux_NA,
};

static const char *const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5",
	"gpio6", "gpio7", "gpio8", "gpio9", "gpio10", "gpio11",
	"gpio12", "gpio13", "gpio14", "gpio15", "gpio16", "gpio17",
	"gpio18", "gpio19", "gpio20", "gpio21", "gpio22", "gpio23",
	"gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio29",
	"gpio30", "gpio31", "gpio32", "gpio33", "gpio34", "gpio35",
	"gpio36", "gpio37", "gpio38", "gpio39", "gpio40", "gpio41",
	"gpio42", "gpio43", "gpio44", "gpio45", "gpio46", "gpio47",
	"gpio48", "gpio49", "gpio50", "gpio51", "gpio52", "gpio53",
	"gpio54", "gpio55", "gpio56", "gpio57", "gpio58", "gpio59",
	"gpio60", "gpio61", "gpio62", "gpio63", "gpio64", "gpio65",
	"gpio66", "gpio67", "gpio68", "gpio69", "gpio70", "gpio71",
	"gpio72", "gpio73", "gpio74", "gpio75", "gpio76", "gpio77",
	"gpio78", "gpio79", "gpio80", "gpio81", "gpio82", "gpio83",
	"gpio84", "gpio85", "gpio86", "gpio87", "gpio88", "gpio89",
	"gpio90", "gpio91", "gpio92", "gpio93", "gpio94", "gpio95",
	"gpio96", "gpio97", "gpio98", "gpio99", "gpio100", "gpio101",
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
	"gpio198", "gpio199",
};
static const char *const RESOUT_GPIO_N_groups[] = {
	"gpio129",
};
static const char *const SYS_THROTTLE_MIRA_groups[] = {
	"gpio129",
};
static const char *const SYS_THROTTLE_MIRB_groups[] = {
	"gpio135",
};
static const char *const USB0_PHY_groups[] = {
	"gpio80", "gpio153",
};
static const char *const USB1_PHY_groups[] = {
	"gpio151", "gpio154",
};
static const char *const aoss_cti_groups[] = {
	"gpio72", "gpio73", "gpio74", "gpio75",
};
static const char *const atest_char0_groups[] = {
	"gpio113",
};
static const char *const atest_char1_groups[] = {
	"gpio112",
};
static const char *const atest_char2_groups[] = {
	"gpio108",
};
static const char *const atest_char3_groups[] = {
	"gpio107",
};
static const char *const atest_char_start_groups[] = {
	"gpio121",
};
static const char *const atest_usb0_groups[] = {
	"gpio156",
};
static const char *const atest_usb00_groups[] = {
	"gpio65",
};
static const char *const atest_usb01_groups[] = {
	"gpio58",
};
static const char *const atest_usb02_groups[] = {
	"gpio60",
};
static const char *const atest_usb03_groups[] = {
	"gpio61",
};
static const char *const atest_usb1_groups[] = {
	"gpio157",
};
static const char *const atest_usb10_groups[] = {
	"gpio56",
};
static const char *const atest_usb11_groups[] = {
	"gpio57",
};
static const char *const atest_usb12_groups[] = {
	"gpio62",
};
static const char *const atest_usb13_groups[] = {
	"gpio59",
};
static const char *const audio_ref_clk_groups[] = {
	"gpio78",
};
static const char *const cam_mclk_groups[] = {
	"gpio94", "gpio95", "gpio96", "gpio97", "gpio98", "gpio99",
	"gpio100", "gpio101", "gpio102", "gpio103", "gpio104", "gpio105",
	"gpio114", "gpio120",
};
static const char *const cam_mclk10_mirb_groups[] = {
	"gpio114",
};
static const char *const cam_mclk4_mirb_groups[] = {
	"gpio114",
};
static const char *const cci01_async_in0_groups[] = {
	"gpio114",
};
static const char *const cci01_async_in1_groups[] = {
	"gpio119",
};
static const char *const cci01_async_in2_groups[] = {
	"gpio120",
};
static const char *const cci01_timer0_groups[] = {
	"gpio106",
};
static const char *const cci01_timer1_groups[] = {
	"gpio107",
};
static const char *const cci01_timer2_groups[] = {
	"gpio108",
};
static const char *const cci01_timer3_groups[] = {
	"gpio119",
};
static const char *const cci01_timer4_groups[] = {
	"gpio120",
};
static const char *const cci0_i2c_groups[] = {
	"gpio38", "gpio39", "gpio135", "gpio136",
};
static const char *const cci0_i2c_scl0_groups[] = {
	"gpio83",
};
static const char *const cci0_i2c_sda0_groups[] = {
	"gpio82",
};
static const char *const cci1_i2c_groups[] = {
	"gpio69", "gpio70", "gpio137", "gpio138",
};
static const char *const cci1_i2c_scl2_groups[] = {
	"gpio85",
};
static const char *const cci1_i2c_sda2_groups[] = {
	"gpio84",
};
static const char *const cci23_async_in0_groups[] = {
	"gpio115",
};
static const char *const cci23_async_in1_groups[] = {
	"gpio116",
};
static const char *const cci23_async_in2_groups[] = {
	"gpio121",
};
static const char *const cci23_timer0_groups[] = {
	"gpio109",
};
static const char *const cci23_timer1_groups[] = {
	"gpio110",
};
static const char *const cci23_timer2_groups[] = {
	"gpio121",
};
static const char *const cci23_timer3_groups[] = {
	"gpio122",
};
static const char *const cci23_timer4_groups[] = {
	"gpio123",
};
static const char *const cci2_i2c_scl4_groups[] = {
	"gpio87",
};
static const char *const cci2_i2c_scl5_groups[] = {
	"gpio122",
};
static const char *const cci2_i2c_sda4_groups[] = {
	"gpio86",
};
static const char *const cci2_i2c_sda5_groups[] = {
	"gpio123",
};
static const char *const cci3_i2c_scl6_groups[] = {
	"gpio89",
};
static const char *const cci3_i2c_scl7_groups[] = {
	"gpio51",
};
static const char *const cci3_i2c_sda6_groups[] = {
	"gpio88",
};
static const char *const cci3_i2c_sda7_groups[] = {
	"gpio50",
};
static const char *const cci45_async_groups[] = {
	"gpio73", "gpio75", "gpio77", "gpio117", "gpio118", "gpio124",
};
static const char *const cci45_timer0_groups[] = {
	"gpio111",
};
static const char *const cci45_timer1_groups[] = {
	"gpio112",
};
static const char *const cci45_timer2_groups[] = {
	"gpio113",
};
static const char *const cci45_timer3_mira_groups[] = {
	"gpio124",
};
static const char *const cci45_timer3_mirb_groups[] = {
	"gpio77",
};
static const char *const cci45_timer4_mira_groups[] = {
	"gpio125",
};
static const char *const cci45_timer4_mirb_groups[] = {
	"gpio76",
};
static const char *const cci4_i2c_groups[] = {
	"gpio40", "gpio41", "gpio117", "gpio118",
};
static const char *const cci4_i2c_scl8_groups[] = {
	"gpio91",
};
static const char *const cci4_i2c_sda8_groups[] = {
	"gpio90",
};
static const char *const cci5_i2c_groups[] = {
	"gpio42", "gpio43", "gpio124", "gpio125",
};
static const char *const cci5_i2c_scl10_groups[] = {
	"gpio93",
};
static const char *const cci5_i2c_sda10_groups[] = {
	"gpio92",
};
static const char *const cmu_rng0_groups[] = {
	"gpio59",
};
static const char *const cmu_rng1_groups[] = {
	"gpio58",
};
static const char *const cmu_rng2_groups[] = {
	"gpio57",
};
static const char *const cmu_rng3_groups[] = {
	"gpio56",
};
static const char *const cri_trng_groups[] = {
	"gpio105",
};
static const char *const dbg_out_clk_groups[] = {
	"gpio59",
};
static const char *const ddr_bist_complete_groups[] = {
	"gpio87",
};
static const char *const ddr_bist_fail_groups[] = {
	"gpio84",
};
static const char *const ddr_bist_start_groups[] = {
	"gpio85",
};
static const char *const ddr_bist_stop_groups[] = {
	"gpio86",
};
static const char *const ddr_pxi0_groups[] = {
	"gpio61", "gpio62",
};
static const char *const ddr_pxi1_groups[] = {
	"gpio60", "gpio65",
};
static const char *const ddr_pxi2_groups[] = {
	"gpio156", "gpio157",
};
static const char *const ddr_pxi3_groups[] = {
	"gpio58", "gpio59",
};
static const char *const dp0_hot_groups[] = {
	"gpio50", "gpio160",
};
static const char *const edp0_hot_groups[] = {
	"gpio137", "gpio156",
};
static const char *const edp0_lcd_groups[] = {
	"gpio38", "gpio158",
};
static const char *const edp1_dpu0_groups[] = {
	"gpio39", "gpio159",
};
static const char *const edp1_dpu1_groups[] = {
	"gpio39", "gpio159",
};
static const char *const edp1_hot_groups[] = {
	"gpio138", "gpio157",
};
static const char *const egpio_groups[] = {
	"gpio161", "gpio162", "gpio163", "gpio164", "gpio165", "gpio166",
	"gpio167", "gpio168", "gpio169", "gpio170", "gpio171", "gpio172",
	"gpio173", "gpio174", "gpio175", "gpio176", "gpio177", "gpio178",
	"gpio179", "gpio180", "gpio181", "gpio182", "gpio183", "gpio184",
	"gpio185", "gpio186", "gpio187", "gpio188", "gpio189", "gpio190",
	"gpio191", "gpio192", "gpio193", "gpio194", "gpio195", "gpio196",
	"gpio197", "gpio198", "gpio199",
};
static const char *const ext_mclk0_groups[] = {
	"gpio130",
};
static const char *const ext_mclk1_groups[] = {
	"gpio78",
};
static const char *const gcc_gp_groups[] = {
	"gpio60", "gpio61", "gpio62", "gpio120", "gpio122", "gpio123",
};
static const char *const gcc_gp_clk10_groups[] = {
	"gpio128",
};
static const char *const gcc_gp_clk11_groups[] = {
	"gpio127",
};
static const char *const gcc_gp_clk4_groups[] = {
	"gpio72",
};
static const char *const gcc_gp_clk5_groups[] = {
	"gpio74",
};
static const char *const gcc_gp_clk6_groups[] = {
	"gpio136",
};
static const char *const gcc_gp_clk7_groups[] = {
	"gpio76",
};
static const char *const gcc_gp_clk8_groups[] = {
	"gpio22",
};
static const char *const gcc_gp_clk9_groups[] = {
	"gpio65",
};
static const char *const i2s0_data0_groups[] = {
	"gpio132",
};
static const char *const i2s0_data1_groups[] = {
	"gpio131",
};
static const char *const i2s0_sck_groups[] = {
	"gpio133",
};
static const char *const i2s0_ws_groups[] = {
	"gpio134",
};
static const char *const i2s2_data0_groups[] = {
	"gpio79",
};
static const char *const i2s2_data1_groups[] = {
	"gpio81",
};
static const char *const i2s2_sck_groups[] = {
	"gpio152",
};
static const char *const i2s2_ws_groups[] = {
	"gpio150",
};
static const char *const ibi_i3c_groups[] = {
	"gpio0", "gpio1", "gpio4", "gpio5", "gpio40", "gpio41",
	"gpio52", "gpio53",
};
static const char *const jitter_bist_groups[] = {
	"gpio56",
};
static const char *const mdp0_vsync0_mira_groups[] = {
	"gpio137",
};
static const char *const mdp0_vsync0_mirb_groups[] = {
	"gpio156",
};
static const char *const mdp0_vsync0_out_groups[] = {
	"gpio15",
};
static const char *const mdp0_vsync1_mira_groups[] = {
	"gpio138",
};
static const char *const mdp0_vsync1_mirb_groups[] = {
	"gpio157",
};
static const char *const mdp0_vsync1_out_groups[] = {
	"gpio76",
};
static const char *const mdp0_vsync2_out_groups[] = {
	"gpio77",
};
static const char *const mdp0_vsync3_out_groups[] = {
	"gpio19",
};
static const char *const mdp0_vsync4_out_groups[] = {
	"gpio21",
};
static const char *const mdp0_vsync5_out_groups[] = {
	"gpio18",
};
static const char *const mdp0_vsync6_out_groups[] = {
	"gpio12",
};
static const char *const mdp0_vsync7_out_groups[] = {
	"gpio13",
};
static const char *const mdp0_vsync8_out_groups[] = {
	"gpio14",
};
static const char *const mdp1_vsync0_mira_groups[] = {
	"gpio38",
};
static const char *const mdp1_vsync0_mirb_groups[] = {
	"gpio158",
};
static const char *const mdp1_vsync0_out_groups[] = {
	"gpio73",
};
static const char *const mdp1_vsync1_mira_groups[] = {
	"gpio39",
};
static const char *const mdp1_vsync1_mirb_groups[] = {
	"gpio159",
};
static const char *const mdp1_vsync1_out_groups[] = {
	"gpio11",
};
static const char *const mdp1_vsync2_out_groups[] = {
	"gpio72",
};
static const char *const mdp1_vsync3_out_groups[] = {
	"gpio23",
};
static const char *const mdp1_vsync4_out_groups[] = {
	"gpio24",
};
static const char *const mdp1_vsync5_out_groups[] = {
	"gpio25",
};
static const char *const mdp1_vsync6_out_groups[] = {
	"gpio26",
};
static const char *const mdp1_vsync7_out_groups[] = {
	"gpio75",
};
static const char *const mdp1_vsync8_out_groups[] = {
	"gpio74",
};
static const char *const pcie0_clk_req_n_groups[] = {
	"gpio126",
};
static const char *const pcie1_clk_req_n_groups[] = {
	"gpio139",
};
static const char *const pcie2_clk_req_n_groups[] = {
	"gpio149",
};
static const char *const phase_flag0_groups[] = {
	"gpio13",
};
static const char *const phase_flag1_groups[] = {
	"gpio55",
};
static const char *const phase_flag10_groups[] = {
	"gpio64",
};
static const char *const phase_flag11_groups[] = {
	"gpio107",
};
static const char *const phase_flag12_groups[] = {
	"gpio139",
};
static const char *const phase_flag13_groups[] = {
	"gpio149",
};
static const char *const phase_flag14_groups[] = {
	"gpio15",
};
static const char *const phase_flag15_groups[] = {
	"gpio158",
};
static const char *const phase_flag16_groups[] = {
	"gpio106",
};
static const char *const phase_flag17_groups[] = {
	"gpio93",
};
static const char *const phase_flag18_groups[] = {
	"gpio92",
};
static const char *const phase_flag19_groups[] = {
	"gpio21",
};
static const char *const phase_flag2_groups[] = {
	"gpio69",
};
static const char *const phase_flag20_groups[] = {
	"gpio19",
};
static const char *const phase_flag21_groups[] = {
	"gpio91",
};
static const char *const phase_flag22_groups[] = {
	"gpio90",
};
static const char *const phase_flag23_groups[] = {
	"gpio51",
};
static const char *const phase_flag24_groups[] = {
	"gpio14",
};
static const char *const phase_flag25_groups[] = {
	"gpio66",
};
static const char *const phase_flag26_groups[] = {
	"gpio71",
};
static const char *const phase_flag27_groups[] = {
	"gpio121",
};
static const char *const phase_flag28_groups[] = {
	"gpio112",
};
static const char *const phase_flag29_groups[] = {
	"gpio108",
};
static const char *const phase_flag3_groups[] = {
	"gpio63",
};
static const char *const phase_flag30_groups[] = {
	"gpio140",
};
static const char *const phase_flag31_groups[] = {
	"gpio54",
};
static const char *const phase_flag4_groups[] = {
	"gpio160",
};
static const char *const phase_flag5_groups[] = {
	"gpio117",
};
static const char *const phase_flag6_groups[] = {
	"gpio70",
};
static const char *const phase_flag7_groups[] = {
	"gpio111",
};
static const char *const phase_flag8_groups[] = {
	"gpio67",
};
static const char *const phase_flag9_groups[] = {
	"gpio12",
};
static const char *const pll_bist_sync_groups[] = {
	"gpio159",
};
static const char *const pll_clk_aux_groups[] = {
	"gpio57",
};
static const char *const prng_rosc0_groups[] = {
	"gpio103",
};
static const char *const prng_rosc1_groups[] = {
	"gpio104",
};
static const char *const prng_rosc2_groups[] = {
	"gpio102",
};
static const char *const prng_rosc3_groups[] = {
	"gpio101",
};
static const char *const pwm_0_groups[] = {
	"gpio135",
};
static const char *const pwm_1_groups[] = {
	"gpio136",
};
static const char *const pwm_10_groups[] = {
	"gpio20",
};
static const char *const pwm_11_groups[] = {
	"gpio36",
};
static const char *const pwm_12_groups[] = {
	"gpio51",
};
static const char *const pwm_13_groups[] = {
	"gpio4",
};
static const char *const pwm_14_groups[] = {
	"gpio160",
};
static const char *const pwm_15_groups[] = {
	"gpio5",
};
static const char *const pwm_16_groups[] = {
	"gpio128",
};
static const char *const pwm_17_groups[] = {
	"gpio151",
};
static const char *const pwm_18_groups[] = {
	"gpio80",
};
static const char *const pwm_19_groups[] = {
	"gpio127",
};
static const char *const pwm_2_groups[] = {
	"gpio73",
};
static const char *const pwm_3_groups[] = {
	"gpio11",
};
static const char *const pwm_4_groups[] = {
	"gpio26",
};
static const char *const pwm_5_groups[] = {
	"gpio75",
};
static const char *const pwm_6_groups[] = {
	"gpio153",
};
static const char *const pwm_7_groups[] = {
	"gpio154",
};
static const char *const pwm_8_groups[] = {
	"gpio76",
};
static const char *const pwm_9_groups[] = {
	"gpio77",
};
static const char *const qdss_cti_groups[] = {
	"gpio6", "gpio7", "gpio29", "gpio30", "gpio66", "gpio68",
	"gpio131", "gpio133",
};
static const char *const qdss_gpio_groups[] = {
	"gpio85", "gpio124", "gpio182", "gpio183",
};
static const char *const qdss_gpio0_groups[] = {
	"gpio82", "gpio193",
};
static const char *const qdss_gpio1_groups[] = {
	"gpio83", "gpio190",
};
static const char *const qdss_gpio10_groups[] = {
	"gpio115", "gpio178",
};
static const char *const qdss_gpio11_groups[] = {
	"gpio116", "gpio191",
};
static const char *const qdss_gpio12_groups[] = {
	"gpio119", "gpio128",
};
static const char *const qdss_gpio13_groups[] = {
	"gpio118", "gpio127",
};
static const char *const qdss_gpio14_groups[] = {
	"gpio122", "gpio192",
};
static const char *const qdss_gpio15_groups[] = {
	"gpio123", "gpio179",
};
static const char *const qdss_gpio2_groups[] = {
	"gpio84", "gpio167",
};
static const char *const qdss_gpio3_groups[] = {
	"gpio86", "gpio168",
};
static const char *const qdss_gpio4_groups[] = {
	"gpio87", "gpio169",
};
static const char *const qdss_gpio5_groups[] = {
	"gpio88", "gpio170",
};
static const char *const qdss_gpio6_groups[] = {
	"gpio89", "gpio173",
};
static const char *const qdss_gpio7_groups[] = {
	"gpio109", "gpio174",
};
static const char *const qdss_gpio8_groups[] = {
	"gpio110", "gpio194",
};
static const char *const qdss_gpio9_groups[] = {
	"gpio113", "gpio195",
};
static const char *const qup1_se0_l0_groups[] = {
	"gpio52",
};
static const char *const qup1_se0_l1_groups[] = {
	"gpio53",
};
static const char *const qup1_se0_l2_groups[] = {
	"gpio54",
};
static const char *const qup1_se0_l3_groups[] = {
	"gpio55",
};
static const char *const qup1_se1_l0_groups[] = {
	"gpio56",
};
static const char *const qup1_se1_l1_groups[] = {
	"gpio57",
};
static const char *const qup1_se1_l2_groups[] = {
	"gpio58",
};
static const char *const qup1_se1_l3_groups[] = {
	"gpio59",
};
static const char *const qup1_se1_l4_groups[] = {
	"gpio60",
};
static const char *const qup1_se1_l5_groups[] = {
	"gpio61",
};
static const char *const qup1_se1_l6_groups[] = {
	"gpio62",
};
static const char *const qup1_se2_l0_groups[] = {
	"gpio63",
};
static const char *const qup1_se2_l1_groups[] = {
	"gpio64",
};
static const char *const qup1_se2_l2_groups[] = {
	"gpio65",
};
static const char *const qup1_se2_l3_groups[] = {
	"gpio66",
};
static const char *const qup1_se3_l0_groups[] = {
	"gpio67",
};
static const char *const qup1_se3_l1_groups[] = {
	"gpio68",
};
static const char *const qup1_se3_l2_groups[] = {
	"gpio69",
};
static const char *const qup1_se3_l3_groups[] = {
	"gpio70",
};
static const char *const qup1_se3_l4_groups[] = {
	"gpio71",
};
static const char *const qup2_se0_l0_groups[] = {
	"gpio0",
};
static const char *const qup2_se0_l1_groups[] = {
	"gpio1",
};
static const char *const qup2_se0_l2_groups[] = {
	"gpio2",
};
static const char *const qup2_se0_l3_groups[] = {
	"gpio3",
};
static const char *const qup2_se1_l0_groups[] = {
	"gpio4",
};
static const char *const qup2_se1_l1_groups[] = {
	"gpio5",
};
static const char *const qup2_se1_l2_groups[] = {
	"gpio6",
};
static const char *const qup2_se1_l3_groups[] = {
	"gpio7",
};
static const char *const qup2_se2_l0_groups[] = {
	"gpio8",
};
static const char *const qup2_se2_l1_groups[] = {
	"gpio9",
};
static const char *const qup2_se2_l2_groups[] = {
	"gpio10",
};
static const char *const qup2_se2_l3_mira_groups[] = {
	"gpio11",
};
static const char *const qup2_se2_l3_mirb_groups[] = {
	"gpio72",
};
static const char *const qup2_se3_l0_groups[] = {
	"gpio12",
};
static const char *const qup2_se3_l1_groups[] = {
	"gpio13",
};
static const char *const qup2_se3_l2_groups[] = {
	"gpio14",
};
static const char *const qup2_se3_l3_groups[] = {
	"gpio15",
};
static const char *const qup2_se4_l0_groups[] = {
	"gpio16",
};
static const char *const qup2_se4_l1_groups[] = {
	"gpio17",
};
static const char *const qup2_se4_l2_groups[] = {
	"gpio18",
};
static const char *const qup2_se4_l3_groups[] = {
	"gpio19",
};
static const char *const qup2_se4_l4_groups[] = {
	"gpio20",
};
static const char *const qup2_se4_l5_groups[] = {
	"gpio21",
};
static const char *const qup2_se4_l6_groups[] = {
	"gpio22",
};
static const char *const qup2_se5_l0_groups[] = {
	"gpio23",
};
static const char *const qup2_se5_l1_groups[] = {
	"gpio24",
};
static const char *const qup2_se5_l2_groups[] = {
	"gpio25",
};
static const char *const qup2_se5_l3_mira_groups[] = {
	"gpio26",
};
static const char *const qup2_se5_l3_mirb_groups[] = {
	"gpio74",
};
static const char *const qup2_se6_l0_groups[] = {
	"gpio27",
};
static const char *const qup2_se6_l1_groups[] = {
	"gpio28",
};
static const char *const qup2_se6_l2_groups[] = {
	"gpio29",
};
static const char *const qup2_se6_l3_groups[] = {
	"gpio30",
};
static const char *const qup2_se7_l0_groups[] = {
	"gpio31",
};
static const char *const qup2_se7_l1_groups[] = {
	"gpio32",
};
static const char *const qup2_se7_l2_groups[] = {
	"gpio33",
};
static const char *const qup2_se7_l3_groups[] = {
	"gpio34",
};
static const char *const qup2_se7_l4_groups[] = {
	"gpio35",
};
static const char *const qup3_se0_l0_groups[] = {
	"gpio40",
};
static const char *const qup3_se0_l1_groups[] = {
	"gpio41",
};
static const char *const qup3_se0_l2_groups[] = {
	"gpio42",
};
static const char *const qup3_se0_l3_groups[] = {
	"gpio43",
};
static const char *const qup3_se0_l6_groups[] = {
	"gpio43",
};
static const char *const qup3_se1_l0_groups[] = {
	"gpio36",
};
static const char *const qup3_se1_l1_groups[] = {
	"gpio37",
};
static const char *const qup3_se1_l2_groups[] = {
	"gpio38",
};
static const char *const qup3_se1_l3_groups[] = {
	"gpio39",
};
static const char *const qup3_se2_l0_groups[] = {
	"gpio44",
};
static const char *const qup3_se2_l1_groups[] = {
	"gpio45",
};
static const char *const qup3_se2_l2_groups[] = {
	"gpio46",
};
static const char *const qup3_se2_l3_groups[] = {
	"gpio47",
};
static const char *const qup3_se3_l0_groups[] = {
	"gpio48",
};
static const char *const qup3_se3_l1_groups[] = {
	"gpio49",
};
static const char *const qup3_se3_l2_groups[] = {
	"gpio50",
};
static const char *const qup3_se3_l3_groups[] = {
	"gpio51",
};
static const char *const sd_write_protect_groups[] = {
	"gpio130",
};
static const char *const sdc2_data_groups[] = {
	"gpio141", "gpio142", "gpio143", "gpio144",
};
static const char *const sdc2_clk_groups[] = {
	"gpio146",
};
static const char *const sdc2_cmd_groups[] = {
	"gpio145",
};
static const char *const sdc2_fb_clk_groups[] = {
	"gpio147",
};
static const char *const sdcc5_vdd2_on_groups[] = {
	"gpio81",
};
static const char *const tb_trig_sdc2_groups[] = {
	"gpio150",
};
static const char *const tgu_ch0_trigout_groups[] = {
	"gpio79",
};
static const char *const tgu_ch1_trigout_groups[] = {
	"gpio152",
};
static const char *const tgu_ch2_trigout_groups[] = {
	"gpio81",
};
static const char *const tgu_ch3_trigout_groups[] = {
	"gpio150",
};
static const char *const tmess_prng0_groups[] = {
	"gpio100",
};
static const char *const tmess_prng1_groups[] = {
	"gpio99",
};
static const char *const tmess_prng2_groups[] = {
	"gpio98",
};
static const char *const tmess_prng3_groups[] = {
	"gpio97",
};
static const char *const tsense_pwm1_groups[] = {
	"gpio7",
};
static const char *const tsense_pwm2_groups[] = {
	"gpio7",
};
static const char *const tsense_pwm3_groups[] = {
	"gpio7",
};
static const char *const usb0_hs_groups[] = {
	"gpio30",
};
static const char *const usb1_hs_groups[] = {
	"gpio29",
};
static const char *const vsense_trigger_mirnat_groups[] = {
	"gpio56",
};

static const struct msm_function niobe_functions[] = {
	FUNCTION(gpio),
	FUNCTION(RESOUT_GPIO_N),
	FUNCTION(SYS_THROTTLE_MIRA),
	FUNCTION(SYS_THROTTLE_MIRB),
	FUNCTION(USB0_PHY),
	FUNCTION(USB1_PHY),
	FUNCTION(aoss_cti),
	FUNCTION(atest_char0),
	FUNCTION(atest_char1),
	FUNCTION(atest_char2),
	FUNCTION(atest_char3),
	FUNCTION(atest_char_start),
	FUNCTION(atest_usb0),
	FUNCTION(atest_usb00),
	FUNCTION(atest_usb01),
	FUNCTION(atest_usb02),
	FUNCTION(atest_usb03),
	FUNCTION(atest_usb1),
	FUNCTION(atest_usb10),
	FUNCTION(atest_usb11),
	FUNCTION(atest_usb12),
	FUNCTION(atest_usb13),
	FUNCTION(audio_ref_clk),
	FUNCTION(cam_mclk),
	FUNCTION(cam_mclk10_mirb),
	FUNCTION(cam_mclk4_mirb),
	FUNCTION(cci01_async_in0),
	FUNCTION(cci01_async_in1),
	FUNCTION(cci01_async_in2),
	FUNCTION(cci01_timer0),
	FUNCTION(cci01_timer1),
	FUNCTION(cci01_timer2),
	FUNCTION(cci01_timer3),
	FUNCTION(cci01_timer4),
	FUNCTION(cci0_i2c),
	FUNCTION(cci0_i2c_scl0),
	FUNCTION(cci0_i2c_sda0),
	FUNCTION(cci1_i2c),
	FUNCTION(cci1_i2c_scl2),
	FUNCTION(cci1_i2c_sda2),
	FUNCTION(cci23_async_in0),
	FUNCTION(cci23_async_in1),
	FUNCTION(cci23_async_in2),
	FUNCTION(cci23_timer0),
	FUNCTION(cci23_timer1),
	FUNCTION(cci23_timer2),
	FUNCTION(cci23_timer3),
	FUNCTION(cci23_timer4),
	FUNCTION(cci2_i2c_scl4),
	FUNCTION(cci2_i2c_scl5),
	FUNCTION(cci2_i2c_sda4),
	FUNCTION(cci2_i2c_sda5),
	FUNCTION(cci3_i2c_scl6),
	FUNCTION(cci3_i2c_scl7),
	FUNCTION(cci3_i2c_sda6),
	FUNCTION(cci3_i2c_sda7),
	FUNCTION(cci45_async),
	FUNCTION(cci45_timer0),
	FUNCTION(cci45_timer1),
	FUNCTION(cci45_timer2),
	FUNCTION(cci45_timer3_mira),
	FUNCTION(cci45_timer3_mirb),
	FUNCTION(cci45_timer4_mira),
	FUNCTION(cci45_timer4_mirb),
	FUNCTION(cci4_i2c),
	FUNCTION(cci4_i2c_scl8),
	FUNCTION(cci4_i2c_sda8),
	FUNCTION(cci5_i2c),
	FUNCTION(cci5_i2c_scl10),
	FUNCTION(cci5_i2c_sda10),
	FUNCTION(cmu_rng0),
	FUNCTION(cmu_rng1),
	FUNCTION(cmu_rng2),
	FUNCTION(cmu_rng3),
	FUNCTION(cri_trng),
	FUNCTION(dbg_out_clk),
	FUNCTION(ddr_bist_complete),
	FUNCTION(ddr_bist_fail),
	FUNCTION(ddr_bist_start),
	FUNCTION(ddr_bist_stop),
	FUNCTION(ddr_pxi0),
	FUNCTION(ddr_pxi1),
	FUNCTION(ddr_pxi2),
	FUNCTION(ddr_pxi3),
	FUNCTION(dp0_hot),
	FUNCTION(edp0_hot),
	FUNCTION(edp0_lcd),
	FUNCTION(edp1_dpu0),
	FUNCTION(edp1_dpu1),
	FUNCTION(edp1_hot),
	FUNCTION(egpio),
	FUNCTION(ext_mclk0),
	FUNCTION(ext_mclk1),
	FUNCTION(gcc_gp),
	FUNCTION(gcc_gp_clk10),
	FUNCTION(gcc_gp_clk11),
	FUNCTION(gcc_gp_clk4),
	FUNCTION(gcc_gp_clk5),
	FUNCTION(gcc_gp_clk6),
	FUNCTION(gcc_gp_clk7),
	FUNCTION(gcc_gp_clk8),
	FUNCTION(gcc_gp_clk9),
	FUNCTION(i2s0_data0),
	FUNCTION(i2s0_data1),
	FUNCTION(i2s0_sck),
	FUNCTION(i2s0_ws),
	FUNCTION(i2s2_data0),
	FUNCTION(i2s2_data1),
	FUNCTION(i2s2_sck),
	FUNCTION(i2s2_ws),
	FUNCTION(ibi_i3c),
	FUNCTION(jitter_bist),
	FUNCTION(mdp0_vsync0_mira),
	FUNCTION(mdp0_vsync0_mirb),
	FUNCTION(mdp0_vsync0_out),
	FUNCTION(mdp0_vsync1_mira),
	FUNCTION(mdp0_vsync1_mirb),
	FUNCTION(mdp0_vsync1_out),
	FUNCTION(mdp0_vsync2_out),
	FUNCTION(mdp0_vsync3_out),
	FUNCTION(mdp0_vsync4_out),
	FUNCTION(mdp0_vsync5_out),
	FUNCTION(mdp0_vsync6_out),
	FUNCTION(mdp0_vsync7_out),
	FUNCTION(mdp0_vsync8_out),
	FUNCTION(mdp1_vsync0_mira),
	FUNCTION(mdp1_vsync0_mirb),
	FUNCTION(mdp1_vsync0_out),
	FUNCTION(mdp1_vsync1_mira),
	FUNCTION(mdp1_vsync1_mirb),
	FUNCTION(mdp1_vsync1_out),
	FUNCTION(mdp1_vsync2_out),
	FUNCTION(mdp1_vsync3_out),
	FUNCTION(mdp1_vsync4_out),
	FUNCTION(mdp1_vsync5_out),
	FUNCTION(mdp1_vsync6_out),
	FUNCTION(mdp1_vsync7_out),
	FUNCTION(mdp1_vsync8_out),
	FUNCTION(pcie0_clk_req_n),
	FUNCTION(pcie1_clk_req_n),
	FUNCTION(pcie2_clk_req_n),
	FUNCTION(phase_flag0),
	FUNCTION(phase_flag1),
	FUNCTION(phase_flag10),
	FUNCTION(phase_flag11),
	FUNCTION(phase_flag12),
	FUNCTION(phase_flag13),
	FUNCTION(phase_flag14),
	FUNCTION(phase_flag15),
	FUNCTION(phase_flag16),
	FUNCTION(phase_flag17),
	FUNCTION(phase_flag18),
	FUNCTION(phase_flag19),
	FUNCTION(phase_flag2),
	FUNCTION(phase_flag20),
	FUNCTION(phase_flag21),
	FUNCTION(phase_flag22),
	FUNCTION(phase_flag23),
	FUNCTION(phase_flag24),
	FUNCTION(phase_flag25),
	FUNCTION(phase_flag26),
	FUNCTION(phase_flag27),
	FUNCTION(phase_flag28),
	FUNCTION(phase_flag29),
	FUNCTION(phase_flag3),
	FUNCTION(phase_flag30),
	FUNCTION(phase_flag31),
	FUNCTION(phase_flag4),
	FUNCTION(phase_flag5),
	FUNCTION(phase_flag6),
	FUNCTION(phase_flag7),
	FUNCTION(phase_flag8),
	FUNCTION(phase_flag9),
	FUNCTION(pll_bist_sync),
	FUNCTION(pll_clk_aux),
	FUNCTION(prng_rosc0),
	FUNCTION(prng_rosc1),
	FUNCTION(prng_rosc2),
	FUNCTION(prng_rosc3),
	FUNCTION(pwm_0),
	FUNCTION(pwm_1),
	FUNCTION(pwm_10),
	FUNCTION(pwm_11),
	FUNCTION(pwm_12),
	FUNCTION(pwm_13),
	FUNCTION(pwm_14),
	FUNCTION(pwm_15),
	FUNCTION(pwm_16),
	FUNCTION(pwm_17),
	FUNCTION(pwm_18),
	FUNCTION(pwm_19),
	FUNCTION(pwm_2),
	FUNCTION(pwm_3),
	FUNCTION(pwm_4),
	FUNCTION(pwm_5),
	FUNCTION(pwm_6),
	FUNCTION(pwm_7),
	FUNCTION(pwm_8),
	FUNCTION(pwm_9),
	FUNCTION(qdss_cti),
	FUNCTION(qdss_gpio),
	FUNCTION(qdss_gpio0),
	FUNCTION(qdss_gpio1),
	FUNCTION(qdss_gpio10),
	FUNCTION(qdss_gpio11),
	FUNCTION(qdss_gpio12),
	FUNCTION(qdss_gpio13),
	FUNCTION(qdss_gpio14),
	FUNCTION(qdss_gpio15),
	FUNCTION(qdss_gpio2),
	FUNCTION(qdss_gpio3),
	FUNCTION(qdss_gpio4),
	FUNCTION(qdss_gpio5),
	FUNCTION(qdss_gpio6),
	FUNCTION(qdss_gpio7),
	FUNCTION(qdss_gpio8),
	FUNCTION(qdss_gpio9),
	FUNCTION(qup1_se0_l0),
	FUNCTION(qup1_se0_l1),
	FUNCTION(qup1_se0_l2),
	FUNCTION(qup1_se0_l3),
	FUNCTION(qup1_se1_l0),
	FUNCTION(qup1_se1_l1),
	FUNCTION(qup1_se1_l2),
	FUNCTION(qup1_se1_l3),
	FUNCTION(qup1_se1_l4),
	FUNCTION(qup1_se1_l5),
	FUNCTION(qup1_se1_l6),
	FUNCTION(qup1_se2_l0),
	FUNCTION(qup1_se2_l1),
	FUNCTION(qup1_se2_l2),
	FUNCTION(qup1_se2_l3),
	FUNCTION(qup1_se3_l0),
	FUNCTION(qup1_se3_l1),
	FUNCTION(qup1_se3_l2),
	FUNCTION(qup1_se3_l3),
	FUNCTION(qup1_se3_l4),
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
	FUNCTION(qup2_se2_l3_mira),
	FUNCTION(qup2_se2_l3_mirb),
	FUNCTION(qup2_se3_l0),
	FUNCTION(qup2_se3_l1),
	FUNCTION(qup2_se3_l2),
	FUNCTION(qup2_se3_l3),
	FUNCTION(qup2_se4_l0),
	FUNCTION(qup2_se4_l1),
	FUNCTION(qup2_se4_l2),
	FUNCTION(qup2_se4_l3),
	FUNCTION(qup2_se4_l4),
	FUNCTION(qup2_se4_l5),
	FUNCTION(qup2_se4_l6),
	FUNCTION(qup2_se5_l0),
	FUNCTION(qup2_se5_l1),
	FUNCTION(qup2_se5_l2),
	FUNCTION(qup2_se5_l3_mira),
	FUNCTION(qup2_se5_l3_mirb),
	FUNCTION(qup2_se6_l0),
	FUNCTION(qup2_se6_l1),
	FUNCTION(qup2_se6_l2),
	FUNCTION(qup2_se6_l3),
	FUNCTION(qup2_se7_l0),
	FUNCTION(qup2_se7_l1),
	FUNCTION(qup2_se7_l2),
	FUNCTION(qup2_se7_l3),
	FUNCTION(qup2_se7_l4),
	FUNCTION(qup3_se0_l0),
	FUNCTION(qup3_se0_l1),
	FUNCTION(qup3_se0_l2),
	FUNCTION(qup3_se0_l3),
	FUNCTION(qup3_se0_l6),
	FUNCTION(qup3_se1_l0),
	FUNCTION(qup3_se1_l1),
	FUNCTION(qup3_se1_l2),
	FUNCTION(qup3_se1_l3),
	FUNCTION(qup3_se2_l0),
	FUNCTION(qup3_se2_l1),
	FUNCTION(qup3_se2_l2),
	FUNCTION(qup3_se2_l3),
	FUNCTION(qup3_se3_l0),
	FUNCTION(qup3_se3_l1),
	FUNCTION(qup3_se3_l2),
	FUNCTION(qup3_se3_l3),
	FUNCTION(sd_write_protect),
	FUNCTION(sdc2_data),
	FUNCTION(sdc2_clk),
	FUNCTION(sdc2_cmd),
	FUNCTION(sdc2_fb_clk),
	FUNCTION(sdcc5_vdd2_on),
	FUNCTION(tb_trig_sdc2),
	FUNCTION(tgu_ch0_trigout),
	FUNCTION(tgu_ch1_trigout),
	FUNCTION(tgu_ch2_trigout),
	FUNCTION(tgu_ch3_trigout),
	FUNCTION(tmess_prng0),
	FUNCTION(tmess_prng1),
	FUNCTION(tmess_prng2),
	FUNCTION(tmess_prng3),
	FUNCTION(tsense_pwm1),
	FUNCTION(tsense_pwm2),
	FUNCTION(tsense_pwm3),
	FUNCTION(usb0_hs),
	FUNCTION(usb1_hs),
	FUNCTION(vsense_trigger_mirnat),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup niobe_groups[] = {
	[0] = PINGROUP(0, qup2_se0_l0, ibi_i3c, NA, NA, NA, NA, NA, NA, NA, NA,
		       NA, 0, -1),
	[1] = PINGROUP(1, qup2_se0_l1, ibi_i3c, NA, NA, NA, NA, NA, NA, NA, NA,
		       NA, 0, -1),
	[2] = PINGROUP(2, qup2_se0_l2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		       0, -1),
	[3] = PINGROUP(3, qup2_se0_l3, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		       0, -1),
	[4] = PINGROUP(4, qup2_se1_l0, ibi_i3c, pwm_13, NA, NA, NA, NA, NA, NA,
		       NA, NA, 0, -1),
	[5] = PINGROUP(5, qup2_se1_l1, ibi_i3c, pwm_15, NA, NA, NA, NA, NA, NA,
		       NA, NA, 0, -1),
	[6] = PINGROUP(6, qup2_se1_l2, qdss_cti, NA, NA, NA, NA, NA, NA, NA, NA,
		       NA, 0, -1),
	[7] = PINGROUP(7, qup2_se1_l3, qdss_cti, tsense_pwm1, tsense_pwm2,
		       tsense_pwm3, NA, NA, NA, NA, NA, NA, 0, -1),
	[8] = PINGROUP(8, qup2_se2_l0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		       0, -1),
	[9] = PINGROUP(9, qup2_se2_l1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		       0, -1),
	[10] = PINGROUP(10, qup2_se2_l2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[11] = PINGROUP(11, qup2_se2_l3_mira, pwm_3, mdp1_vsync1_out, NA, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[12] = PINGROUP(12, qup2_se3_l0, mdp0_vsync6_out, NA, phase_flag9, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[13] = PINGROUP(13, qup2_se3_l1, mdp0_vsync7_out, NA, phase_flag0, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[14] = PINGROUP(14, qup2_se3_l2, mdp0_vsync8_out, NA, phase_flag24, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[15] = PINGROUP(15, qup2_se3_l3, mdp0_vsync0_out, NA, phase_flag14, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[16] = PINGROUP(16, qup2_se4_l0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[17] = PINGROUP(17, qup2_se4_l1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[18] = PINGROUP(18, qup2_se4_l2, mdp0_vsync5_out, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[19] = PINGROUP(19, qup2_se4_l3, mdp0_vsync3_out, NA, phase_flag20, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[20] = PINGROUP(20, qup2_se4_l4, pwm_10, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[21] = PINGROUP(21, qup2_se4_l5, mdp0_vsync4_out, NA, phase_flag19, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[22] = PINGROUP(22, gcc_gp_clk8, qup2_se4_l6, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[23] = PINGROUP(23, qup2_se5_l0, mdp1_vsync3_out, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[24] = PINGROUP(24, qup2_se5_l1, mdp1_vsync4_out, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[25] = PINGROUP(25, qup2_se5_l2, mdp1_vsync5_out, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[26] = PINGROUP(26, qup2_se5_l3_mira, pwm_4, mdp1_vsync6_out, NA, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[27] = PINGROUP(27, qup2_se6_l0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[28] = PINGROUP(28, qup2_se6_l1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[29] = PINGROUP(29, qup2_se6_l2, usb1_hs, qdss_cti, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[30] = PINGROUP(30, qup2_se6_l3, usb0_hs, qdss_cti, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[31] = PINGROUP(31, qup2_se7_l0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[32] = PINGROUP(32, qup2_se7_l1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[33] = PINGROUP(33, qup2_se7_l2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[34] = PINGROUP(34, qup2_se7_l3, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[35] = PINGROUP(35, qup2_se7_l4, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[36] = PINGROUP(36, qup3_se1_l0, pwm_11, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[37] = PINGROUP(37, qup3_se1_l1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[38] = PINGROUP(38, mdp1_vsync0_mira, qup3_se1_l2, cci0_i2c, edp0_lcd,
			NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[39] = PINGROUP(39, mdp1_vsync1_mira, qup3_se1_l3, cci0_i2c, edp1_dpu1,
			edp1_dpu0, NA, NA, NA, NA, NA, NA, 0, -1),
	[40] = PINGROUP(40, qup3_se0_l0, ibi_i3c, cci4_i2c, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[41] = PINGROUP(41, qup3_se0_l1, ibi_i3c, cci4_i2c, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[42] = PINGROUP(42, qup3_se0_l2, cci5_i2c, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[43] = PINGROUP(43, qup3_se0_l3, qup3_se0_l6, cci5_i2c, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[44] = PINGROUP(44, qup3_se2_l0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[45] = PINGROUP(45, qup3_se2_l1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[46] = PINGROUP(46, qup3_se2_l2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[47] = PINGROUP(47, qup3_se2_l3, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[48] = PINGROUP(48, qup3_se3_l0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[49] = PINGROUP(49, qup3_se3_l1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[50] = PINGROUP(50, qup3_se3_l2, cci3_i2c_sda7, dp0_hot, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[51] = PINGROUP(51, qup3_se3_l3, cci3_i2c_scl7, pwm_12, NA,
			phase_flag23, NA, NA, NA, NA, NA, NA, 0, -1),
	[52] = PINGROUP(52, qup1_se0_l0, ibi_i3c, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[53] = PINGROUP(53, qup1_se0_l1, ibi_i3c, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[54] = PINGROUP(54, qup1_se0_l2, NA, phase_flag31, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[55] = PINGROUP(55, qup1_se0_l3, NA, phase_flag1, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[56] = PINGROUP(56, qup1_se1_l0, jitter_bist, cmu_rng3, NA,
			vsense_trigger_mirnat, atest_usb10, NA, NA, NA, NA, NA,
			0, -1),
	[57] = PINGROUP(57, qup1_se1_l1, pll_clk_aux, cmu_rng2, NA, atest_usb11,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[58] = PINGROUP(58, qup1_se1_l2, cmu_rng1, NA, atest_usb01, ddr_pxi3,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[59] = PINGROUP(59, qup1_se1_l3, cmu_rng0, dbg_out_clk, atest_usb13,
			ddr_pxi3, NA, NA, NA, NA, NA, NA, 0, -1),
	[60] = PINGROUP(60, qup1_se1_l4, gcc_gp, atest_usb02, ddr_pxi1, NA, NA,
			NA, NA, NA, NA, NA, 0, -1),
	[61] = PINGROUP(61, qup1_se1_l5, gcc_gp, atest_usb03, ddr_pxi0, NA, NA,
			NA, NA, NA, NA, NA, 0, -1),
	[62] = PINGROUP(62, qup1_se1_l6, gcc_gp, atest_usb12, ddr_pxi0, NA, NA,
			NA, NA, NA, NA, NA, 0, -1),
	[63] = PINGROUP(63, qup1_se2_l0, NA, phase_flag3, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[64] = PINGROUP(64, qup1_se2_l1, NA, phase_flag10, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[65] = PINGROUP(65, qup1_se2_l2, gcc_gp_clk9, NA, atest_usb00, ddr_pxi1,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[66] = PINGROUP(66, qup1_se2_l3, NA, phase_flag25, qdss_cti, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[67] = PINGROUP(67, qup1_se3_l0, NA, phase_flag8, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[68] = PINGROUP(68, qup1_se3_l1, qdss_cti, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[69] = PINGROUP(69, qup1_se3_l2, cci1_i2c, NA, phase_flag2, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[70] = PINGROUP(70, qup1_se3_l3, cci1_i2c, NA, phase_flag6, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[71] = PINGROUP(71, qup1_se3_l4, NA, phase_flag26, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[72] = PINGROUP(72, gcc_gp_clk4, aoss_cti, qup2_se2_l3_mirb,
			mdp1_vsync2_out, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[73] = PINGROUP(73, aoss_cti, cci45_async, pwm_2, mdp1_vsync0_out, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[74] = PINGROUP(74, gcc_gp_clk5, aoss_cti, qup2_se5_l3_mirb,
			mdp1_vsync8_out, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[75] = PINGROUP(75, aoss_cti, cci45_async, pwm_5, mdp1_vsync7_out, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[76] = PINGROUP(76, gcc_gp_clk7, cci45_timer4_mirb, pwm_8,
			mdp0_vsync1_out, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[77] = PINGROUP(77, cci45_async, cci45_timer3_mirb, pwm_9,
			mdp0_vsync2_out, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[78] = PINGROUP(78, audio_ref_clk, ext_mclk1, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[79] = PINGROUP(79, i2s2_data0, tgu_ch0_trigout, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[80] = PINGROUP(80, USB0_PHY, pwm_18, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[81] = PINGROUP(81, sdcc5_vdd2_on, i2s2_data1, tgu_ch2_trigout, NA, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[82] = PINGROUP(82, cci0_i2c_sda0, qdss_gpio0, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[83] = PINGROUP(83, cci0_i2c_scl0, qdss_gpio1, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[84] = PINGROUP(84, cci1_i2c_sda2, ddr_bist_fail, qdss_gpio2, NA, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[85] = PINGROUP(85, cci1_i2c_scl2, ddr_bist_start, qdss_gpio, NA, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[86] = PINGROUP(86, cci2_i2c_sda4, ddr_bist_stop, qdss_gpio3, NA, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[87] = PINGROUP(87, cci2_i2c_scl4, ddr_bist_complete, qdss_gpio4, NA,
			NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[88] = PINGROUP(88, cci3_i2c_sda6, qdss_gpio5, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[89] = PINGROUP(89, cci3_i2c_scl6, qdss_gpio6, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[90] = PINGROUP(90, cci4_i2c_sda8, NA, phase_flag22, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[91] = PINGROUP(91, cci4_i2c_scl8, NA, phase_flag21, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[92] = PINGROUP(92, cci5_i2c_sda10, NA, phase_flag18, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[93] = PINGROUP(93, cci5_i2c_scl10, NA, phase_flag17, NA, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[94] = PINGROUP(94, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			-1),
	[95] = PINGROUP(95, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			-1),
	[96] = PINGROUP(96, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			-1),
	[97] = PINGROUP(97, cam_mclk, tmess_prng3, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[98] = PINGROUP(98, cam_mclk, tmess_prng2, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[99] = PINGROUP(99, cam_mclk, tmess_prng1, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[100] = PINGROUP(100, cam_mclk, tmess_prng0, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[101] = PINGROUP(101, cam_mclk, prng_rosc3, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[102] = PINGROUP(102, cam_mclk, prng_rosc2, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[103] = PINGROUP(103, cam_mclk, prng_rosc0, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[104] = PINGROUP(104, cam_mclk, prng_rosc1, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[105] = PINGROUP(105, cam_mclk, cri_trng, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[106] = PINGROUP(106, cci01_timer0, NA, phase_flag16, NA, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[107] = PINGROUP(107, cci01_timer1, NA, phase_flag11, atest_char3, NA,
			 NA, NA, NA, NA, NA, NA, 0, -1),
	[108] = PINGROUP(108, cci01_timer2, NA, phase_flag29, atest_char2, NA,
			 NA, NA, NA, NA, NA, NA, 0, -1),
	[109] = PINGROUP(109, cci23_timer0, NA, qdss_gpio7, NA, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[110] = PINGROUP(110, cci23_timer1, NA, qdss_gpio8, NA, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[111] = PINGROUP(111, cci45_timer0, NA, NA, phase_flag7, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[112] = PINGROUP(112, cci45_timer1, NA, phase_flag28, atest_char1, NA,
			 NA, NA, NA, NA, NA, NA, 0, -1),
	[113] = PINGROUP(113, cci45_timer2, qdss_gpio9, atest_char0, NA, NA, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[114] = PINGROUP(114, cci01_async_in0, cam_mclk10_mirb, cam_mclk4_mirb, NA,
			 NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[115] = PINGROUP(115, cci23_async_in0, qdss_gpio10, NA, NA, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[116] = PINGROUP(116, cci23_async_in1, qdss_gpio11, NA, NA, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[117] = PINGROUP(117, cci45_async, cci4_i2c, NA, phase_flag5, NA, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[118] = PINGROUP(118, cci45_async, cci4_i2c, qdss_gpio13, NA, NA, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[119] = PINGROUP(119, cci01_timer3, cci01_async_in1, qdss_gpio12, NA,
			 NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[120] = PINGROUP(120, cci01_timer4, cci01_async_in2, cam_mclk, gcc_gp,
			 cam_mclk, NA, NA, NA, NA, NA, NA, 0, -1),
	[121] = PINGROUP(121, cci23_timer2, cci23_async_in2, NA, phase_flag27,
			 atest_char_start, NA, NA, NA, NA, NA, NA, 0, -1),
	[122] = PINGROUP(122, cci23_timer3, cci2_i2c_scl5, gcc_gp, qdss_gpio14,
			 NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[123] = PINGROUP(123, cci23_timer4, cci2_i2c_sda5, gcc_gp, qdss_gpio15,
			 NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[124] = PINGROUP(124, cci45_timer3_mira, cci5_i2c, cci45_async,
			 qdss_gpio, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[125] = PINGROUP(125, cci45_timer4_mira, cci5_i2c, NA, NA, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[126] = PINGROUP(126, pcie0_clk_req_n, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[127] = PINGROUP(127, gcc_gp_clk11, pwm_19, qdss_gpio13, NA, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[128] = PINGROUP(128, gcc_gp_clk10, pwm_16, qdss_gpio12, NA, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[129] = PINGROUP(129, RESOUT_GPIO_N, SYS_THROTTLE_MIRA, NA, NA, NA, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[130] = PINGROUP(130, ext_mclk0, sd_write_protect, NA, NA, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[131] = PINGROUP(131, i2s0_data1, qdss_cti, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[132] = PINGROUP(132, i2s0_data0, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[133] = PINGROUP(133, i2s0_sck, qdss_cti, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[134] = PINGROUP(134, i2s0_ws, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[135] = PINGROUP(135, pwm_0, SYS_THROTTLE_MIRB, cci0_i2c, NA, NA, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[136] = PINGROUP(136, pwm_1, gcc_gp_clk6, cci0_i2c, NA, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[137] = PINGROUP(137, mdp0_vsync0_mira, cci1_i2c, edp0_hot, NA, NA, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[138] = PINGROUP(138, mdp0_vsync1_mira, cci1_i2c, edp1_hot, NA, NA, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[139] = PINGROUP(139, pcie1_clk_req_n, NA, phase_flag12, NA, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[140] = PINGROUP(140, NA, phase_flag30, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[141] = PINGROUP(141, sdc2_data, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[142] = PINGROUP(142, sdc2_data, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[143] = PINGROUP(143, sdc2_data, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[144] = PINGROUP(144, sdc2_data, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[145] = PINGROUP(145, sdc2_cmd, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[146] = PINGROUP(146, sdc2_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[147] = PINGROUP(147, sdc2_fb_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[148] = PINGROUP(148, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			 -1),
	[149] = PINGROUP(149, pcie2_clk_req_n, NA, phase_flag13, NA, NA, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[150] = PINGROUP(150, tb_trig_sdc2, i2s2_ws, tgu_ch3_trigout, NA, NA,
			 NA, NA, NA, NA, NA, NA, 0, -1),
	[151] = PINGROUP(151, USB1_PHY, pwm_17, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[152] = PINGROUP(152, i2s2_sck, tgu_ch1_trigout, NA, NA, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[153] = PINGROUP(153, USB0_PHY, pwm_6, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[154] = PINGROUP(154, USB1_PHY, pwm_7, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[155] = PINGROUP(155, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			 -1),
	[156] = PINGROUP(156, mdp0_vsync0_mirb, edp0_hot, NA, atest_usb0,
			 ddr_pxi2, NA, NA, NA, NA, NA, NA, 0, -1),
	[157] = PINGROUP(157, mdp0_vsync1_mirb, edp1_hot, NA, atest_usb1,
			 ddr_pxi2, NA, NA, NA, NA, NA, NA, 0, -1),
	[158] = PINGROUP(158, mdp1_vsync0_mirb, edp0_lcd, NA, phase_flag15, NA,
			 NA, NA, NA, NA, NA, NA, 0, -1),
	[159] = PINGROUP(159, mdp1_vsync1_mirb, edp1_dpu1, edp1_dpu0,
			 pll_bist_sync, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[160] = PINGROUP(160, dp0_hot, pwm_14, NA, phase_flag4, NA, NA, NA, NA,
			 NA, NA, NA, 0, -1),
	[161] = PINGROUP(161, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[162] = PINGROUP(162, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[163] = PINGROUP(163, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[164] = PINGROUP(164, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[165] = PINGROUP(165, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[166] = PINGROUP(166, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[167] = PINGROUP(167, qdss_gpio2, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[168] = PINGROUP(168, qdss_gpio3, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[169] = PINGROUP(169, qdss_gpio4, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[170] = PINGROUP(170, qdss_gpio5, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[171] = PINGROUP(171, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[172] = PINGROUP(172, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[173] = PINGROUP(173, qdss_gpio6, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[174] = PINGROUP(174, qdss_gpio7, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[175] = PINGROUP(175, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[176] = PINGROUP(176, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[177] = PINGROUP(177, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[178] = PINGROUP(178, qdss_gpio10, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[179] = PINGROUP(179, qdss_gpio15, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[180] = PINGROUP(180, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[181] = PINGROUP(181, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[182] = PINGROUP(182, qdss_gpio, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[183] = PINGROUP(183, qdss_gpio, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[184] = PINGROUP(184, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[185] = PINGROUP(185, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[186] = PINGROUP(186, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[187] = PINGROUP(187, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[188] = PINGROUP(188, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[189] = PINGROUP(189, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[190] = PINGROUP(190, qdss_gpio1, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[191] = PINGROUP(191, qdss_gpio11, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[192] = PINGROUP(192, qdss_gpio14, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[193] = PINGROUP(193, qdss_gpio0, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[194] = PINGROUP(194, qdss_gpio8, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[195] = PINGROUP(195, qdss_gpio9, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 egpio, 0, -1),
	[196] = PINGROUP(196, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[197] = PINGROUP(197, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[198] = PINGROUP(198, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[199] = PINGROUP(199, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, egpio, 0,
			 -1),
	[200] = UFS_RESET(ufs_reset, 0x1D2004, 0x1D3000),
};

static struct pinctrl_qup niobe_qup_regs[] = {
	QUP_I3C(1, QUP_2_I3C_0_MODE_OFFSET),
	QUP_I3C(2, QUP_2_I3C_1_MODE_OFFSET),
	QUP_I3C(3, QUP_3_I3C_0_MODE_OFFSET),
	QUP_I3C(4, QUP_1_I3C_0_MODE_OFFSET),
};

static const struct msm_gpio_wakeirq_map niobe_pdc_map[] = {
	{ 0, 76 }, { 1, 77 }, { 3, 88 }, { 4, 64 }, { 5, 63 }, { 6, 65 },
	{ 7, 89 }, { 8, 75 }, { 11, 128 }, { 12, 85 }, { 15, 129 }, { 19, 66 },
	{ 21, 123 }, { 22, 124 }, { 26, 94 }, { 29, 61 }, { 30, 72 }, { 32, 54 },
	{ 34, 53 }, { 36, 90 }, { 38, 143 }, { 39, 144 }, { 40, 84 }, { 41, 83 },
	{ 43, 99 }, { 47, 140 }, { 50, 71 }, { 51, 82 }, { 52, 104 }, { 53, 103 },
	{ 55, 137 }, { 59, 70 }, { 62, 115 }, { 63, 117 }, { 64, 116 }, { 66, 125 },
	{ 67, 119 }, { 69, 95 }, { 70, 105 }, { 71, 102 }, { 72, 93 }, { 74, 78 },
	{ 78, 101 }, { 79, 91 }, { 80, 92 }, { 81, 118 }, { 115, 106 }, { 116, 100 },
	{ 117, 87 }, { 125, 108 }, { 126, 109 }, { 129, 126 }, { 130, 127 }, { 131, 130 },
	{ 132, 131 }, { 133, 132 }, { 135, 74 }, { 136, 73 }, { 137, 133 }, { 138, 138 },
	{ 139, 60 }, { 140, 56 }, { 141, 59 }, { 142, 79 }, { 143, 112 }, { 144, 113 },
	{ 145, 114 }, { 147, 86 }, { 148, 55 }, { 149, 122 }, { 150, 58 }, { 151, 80 },
	{ 152, 69 }, { 153, 139 }, { 154, 141 }, { 156, 96 }, { 157, 97 }, { 158, 98 },
	{ 159, 107 }, { 160, 110 }, { 162, 142 }, { 165, 145 }, { 170, 146 }, { 172, 111 },
	{ 175, 81 }, { 177, 57 }, { 181, 67 }, { 183, 51 }, { 184, 52 }, { 186, 62 },
	{ 188, 134 }, { 191, 68 }, { 194, 135 }, { 195, 136 }, { 197, 121 }, { 199, 120 },
};

static const struct msm_pinctrl_soc_data niobe_pinctrl = {
	.pins = niobe_pins,
	.npins = ARRAY_SIZE(niobe_pins),
	.functions = niobe_functions,
	.nfunctions = ARRAY_SIZE(niobe_functions),
	.groups = niobe_groups,
	.ngroups = ARRAY_SIZE(niobe_groups),
	.ngpios = 201,
	.qup_regs = niobe_qup_regs,
	.nqup_regs = ARRAY_SIZE(niobe_qup_regs),
	.wakeirq_map = niobe_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(niobe_pdc_map),
	.egpio_func = 11,
};

static const struct msm_pinctrl_soc_data niobe_vm_pinctrl = {
	.pins = niobe_pins,
	.npins = ARRAY_SIZE(niobe_pins),
	.functions = niobe_functions,
	.nfunctions = ARRAY_SIZE(niobe_functions),
	.groups = niobe_groups,
	.ngroups = ARRAY_SIZE(niobe_groups),
	.ngpios = 201,
	.egpio_func = 11,
};

static const struct of_device_id niobe_pinctrl_of_match[] = {
	{ .compatible = "qcom,niobe-pinctrl", .data = &niobe_pinctrl},
	{ .compatible = "qcom,niobe-vm-pinctrl", .data = &niobe_vm_pinctrl},
	{},
};

static int niobe_pinctrl_probe(struct platform_device *pdev)
{
	const struct msm_pinctrl_soc_data *pinctrl_data;
	struct device *dev = &pdev->dev;

	pinctrl_data = of_device_get_match_data(dev);
	if (!pinctrl_data)
		return -EINVAL;

	return msm_pinctrl_probe(pdev, pinctrl_data);
}

static struct platform_driver niobe_pinctrl_driver = {
	.driver = {
		.name = "niobe-pinctrl",
		.of_match_table = niobe_pinctrl_of_match,
	},
	.probe = niobe_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init niobe_pinctrl_init(void)
{
	return platform_driver_register(&niobe_pinctrl_driver);
}
arch_initcall(niobe_pinctrl_init);

static void __exit niobe_pinctrl_exit(void)
{
	platform_driver_unregister(&niobe_pinctrl_driver);
}
module_exit(niobe_pinctrl_exit);

MODULE_DESCRIPTION("QTI niobe pinctrl driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, niobe_pinctrl_of_match);
MODULE_SOFTDEP("pre: qcom_tlmm_vm_irqchip");
