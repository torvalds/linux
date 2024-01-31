// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#define NORTH	0x900000 /* dummy tile info */
#define SOUTH	0xD00000
#define SOUTH1	0xD1E000 /* dummy tile info */
#define WEST	0x100000
#define EAST	0x500000
#define DUMMY	0x0
#define REG_SIZE 0x1000
#define PINGROUP(id, base, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
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
		.ctl_reg = base + REG_SIZE * id,		\
		.io_reg = base + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = base + 0x8 + REG_SIZE * id,	\
		.intr_status_reg = base + 0xc + REG_SIZE * id,	\
		.intr_target_reg = base + 0x8 + REG_SIZE * id,	\
		.mux_bit = 2,			\
		.dir_conn_reg = (base == EAST) ? base + 0xcc000 : \
			((base == WEST) ? base + 0xcc000 : \
			((base == NORTH) ? EAST + 0xcc000 : base + 0xcd000)), \
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
		.dir_conn_en_bit = 8,       \
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
static const struct pinctrl_pin_desc sdmshrike_pins[] = {
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
	PINCTRL_PIN(190, "UFS_RESET"),
	PINCTRL_PIN(191, "UFS0_RESET"),
	PINCTRL_PIN(192, "SDC2_CLK"),
	PINCTRL_PIN(193, "SDC2_CMD"),
	PINCTRL_PIN(194, "SDC2_DATA"),
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

static const unsigned int sdc2_clk_pins[] = { 192 };
static const unsigned int sdc2_cmd_pins[] = { 193 };
static const unsigned int sdc2_data_pins[] = { 194 };
static const unsigned int ufs_reset_pins[] = { 190 };
static const unsigned int ufs0_reset_pins[] = { 191 };

enum sdmshrike_functions {
	msm_mux_GRFC2,
	msm_mux_atest_usb31,
	msm_mux_emac_pps,
	msm_mux_ddr_pxi4,
	msm_mux_GRFC3,
	msm_mux_tgu_ch4,
	msm_mux_atest_usb30,
	msm_mux_GRFC4,
	msm_mux_tgu_ch7,
	msm_mux_atest_usb4,
	msm_mux_ddr_pxi5,
	msm_mux_GRFC5,
	msm_mux_tgu_ch6,
	msm_mux_atest_usb43,
	msm_mux_GRFC6,
	msm_mux_tgu_ch5,
	msm_mux_atest_usb42,
	msm_mux_ddr_pxi6,
	msm_mux_GRFC7,
	msm_mux_atest_usb41,
	msm_mux_GRFC13,
	msm_mux_GRFC14,
	msm_mux_GRFC15,
	msm_mux_GRFC16,
	msm_mux_qup12,
	msm_mux_qup16,
	msm_mux_tsif1_clk,
	msm_mux_qup8,
	msm_mux_qspi0_cs,
	msm_mux_tgu_ch3,
	msm_mux_atest_usb40,
	msm_mux_ddr_pxi7,
	msm_mux_tsif1_en,
	msm_mux_qspi00,
	msm_mux_mdp_vsync0,
	msm_mux_mdp_vsync1,
	msm_mux_mdp_vsync2,
	msm_mux_mdp_vsync3,
	msm_mux_mdp_vsync4,
	msm_mux_mdp_vsync5,
	msm_mux_tgu_ch0,
	msm_mux_tsif1_data,
	msm_mux_qspi01,
	msm_mux_sdc4_cmd,
	msm_mux_tgu_ch1,
	msm_mux_atest_usb0,
	msm_mux_tsif1_sync,
	msm_mux_qspi02,
	msm_mux_sdc43,
	msm_mux_vfr_1,
	msm_mux_tgu_ch2,
	msm_mux_atest_usb03,
	msm_mux_tsif2_clk,
	msm_mux_qup11,
	msm_mux_qspi0_clk,
	msm_mux_sdc4_clk,
	msm_mux_atest_usb02,
	msm_mux_tsif2_en,
	msm_mux_qspi03,
	msm_mux_sdc42,
	msm_mux_atest_tsens,
	msm_mux_atest_usb01,
	msm_mux_tsif2_data,
	msm_mux_sdc41,
	msm_mux_atest_usb00,
	msm_mux_tsif2_sync,
	msm_mux_sdc40,
	msm_mux_atest_usb2,
	msm_mux_tsif2_error,
	msm_mux_sd_write,
	msm_mux_tsif1_error,
	msm_mux_qup7,
	msm_mux_ddr_bist,
	msm_mux_pll_bypassnl,
	msm_mux_pll_reset,
	msm_mux_pci_e1,
	msm_mux_phase_flag4,
	msm_mux_atest_usb23,
	msm_mux_uim2_data,
	msm_mux_uim2_clk,
	msm_mux_uim2_reset,
	msm_mux_uim2_present,
	msm_mux_uim1_data,
	msm_mux_uim1_clk,
	msm_mux_uim1_reset,
	msm_mux_uim1_present,
	msm_mux_uim_batt,
	msm_mux_aoss_cti,
	msm_mux_qup1,
	msm_mux_rgmii_txc,
	msm_mux_rgmii_rxc,
	msm_mux_adsp_ext,
	msm_mux_rgmii_rx,
	msm_mux_rgmii_rxd0,
	msm_mux_rgmii_rxd1,
	msm_mux_qup5,
	msm_mux_rgmii_rxd2,
	msm_mux_rgmii_rxd3,
	msm_mux_phase_flag17,
	msm_mux_rgmii_tx,
	msm_mux_phase_flag16,
	msm_mux_atest_usb21,
	msm_mux_rgmii_txd0,
	msm_mux_phase_flag15,
	msm_mux_atest_usb20,
	msm_mux_phase_flag14,
	msm_mux_emac_phy,
	msm_mux_hs3_mi2s,
	msm_mux_phase_flag22,
	msm_mux_sec_mi2s,
	msm_mux_qup2,
	msm_mux_jitter_bist,
	msm_mux_pll_bist,
	msm_mux_ter_mi2s,
	msm_mux_gcc_gp1,
	msm_mux_atest_char,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_qua_mi2s,
	msm_mux_gcc_gp4,
	msm_mux_gcc_gp5,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_pri_mi2s,
	msm_mux_qup3,
	msm_mux_pri_mi2s_ws,
	msm_mux_spkr_i2s,
	msm_mux_audio_ref,
	msm_mux_phase_flag21,
	msm_mux_lpass_slimbus,
	msm_mux_phase_flag20,
	msm_mux_phase_flag19,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_phase_flag18,
	msm_mux_phase_flag13,
	msm_mux_btfm_slimbus,
	msm_mux_hs1_mi2s,
	msm_mux_phase_flag12,
	msm_mux_phase_flag11,
	msm_mux_phase_flag10,
	msm_mux_phase_flag9,
	msm_mux_cri_trng0,
	msm_mux_phase_flag8,
	msm_mux_hs2_mi2s,
	msm_mux_cri_trng1,
	msm_mux_phase_flag7,
	msm_mux_qspi11,
	msm_mux_cri_trng,
	msm_mux_phase_flag24,
	msm_mux_qspi10,
	msm_mux_sp_cmu,
	msm_mux_phase_flag6,
	msm_mux_qspi1_clk,
	msm_mux_prng_rosc,
	msm_mux_phase_flag5,
	msm_mux_phase_flag31,
	msm_mux_phase_flag30,
	msm_mux_phase_flag27,
	msm_mux_phase_flag26,
	msm_mux_phase_flag25,
	msm_mux_pci_e2,
	msm_mux_pci_e3,
	msm_mux_cci_timer4,
	msm_mux_qup19,
	msm_mux_cci_timer5,
	msm_mux_cci_timer6,
	msm_mux_cci_timer7,
	msm_mux_cci_timer8,
	msm_mux_cci_timer9,
	msm_mux_dp_hot,
	msm_mux_egpio,
	msm_mux_qup0,
	msm_mux_gpio,
	msm_mux_cci_i2c,
	msm_mux_qup6,
	msm_mux_rgmii_txd1,
	msm_mux_phase_flag3,
	msm_mux_rgmii_txd2,
	msm_mux_phase_flag2,
	msm_mux_rgmii_txd3,
	msm_mux_qup_l6,
	msm_mux_phase_flag1,
	msm_mux_debug_hot,
	msm_mux_rgmii_mdc,
	msm_mux_qup_l5,
	msm_mux_phase_flag0,
	msm_mux_mdp_vsync,
	msm_mux_qup10,
	msm_mux_edp_hot,
	msm_mux_m_voc,
	msm_mux_edp_lcd,
	msm_mux_cam_mclk,
	msm_mux_qdss_gpio0,
	msm_mux_qdss_gpio1,
	msm_mux_qdss_gpio2,
	msm_mux_qdss_gpio3,
	msm_mux_qdss_gpio4,
	msm_mux_qdss_gpio5,
	msm_mux_qdss_gpio6,
	msm_mux_qdss_gpio7,
	msm_mux_cci_timer0,
	msm_mux_gcc_gp2,
	msm_mux_qdss_gpio8,
	msm_mux_cci_timer1,
	msm_mux_gcc_gp3,
	msm_mux_qdss_gpio,
	msm_mux_cci_timer2,
	msm_mux_qup18,
	msm_mux_qdss_gpio9,
	msm_mux_cci_timer3,
	msm_mux_cci_async,
	msm_mux_qdss_gpio10,
	msm_mux_qdss_gpio11,
	msm_mux_qdss_gpio12,
	msm_mux_qup15,
	msm_mux_qdss_gpio15,
	msm_mux_qdss_gpio13,
	msm_mux_qdss_gpio14,
	msm_mux_phase_flag23,
	msm_mux_dbg_out,
	msm_mux_atest_usb22,
	msm_mux_pci_e0,
	msm_mux_qup_l4,
	msm_mux_agera_pll,
	msm_mux_usb0_phy,
	msm_mux_qup9,
	msm_mux_qup13,
	msm_mux_qup14,
	msm_mux_usb2phy_ac,
	msm_mux_qdss_cti,
	msm_mux_qup4,
	msm_mux_phase_flag29,
	msm_mux_phase_flag28,
	msm_mux_qup17,
	msm_mux_qspi1_cs,
	msm_mux_qspi13,
	msm_mux_qspi12,
	msm_mux_usb1_phy,
	msm_mux_rgmii_mdio,
	msm_mux_GPS_TX,
	msm_mux_NAV_PPS,
	msm_mux_atest_usb1,
	msm_mux_ddr_pxi0,
	msm_mux_QLINK_REQUEST,
	msm_mux_QLINK_ENABLE,
	msm_mux_atest_tsens2,
	msm_mux_atest_usb13,
	msm_mux_vsense_trigger,
	msm_mux_wmss_reset,
	msm_mux_atest_usb12,
	msm_mux_ddr_pxi1,
	msm_mux_wlan1_adc1,
	msm_mux_GRFC8,
	msm_mux_atest_usb11,
	msm_mux_wlan1_adc0,
	msm_mux_GRFC9,
	msm_mux_atest_usb10,
	msm_mux_ddr_pxi2,
	msm_mux_wlan2_adc1,
	msm_mux_GRFC10,
	msm_mux_GRFC11,
	msm_mux_GRFC12,
	msm_mux_pa_indicator,
	msm_mux_atest_usb3,
	msm_mux_wlan2_adc0,
	msm_mux_mss_lte,
	msm_mux_GRFC0,
	msm_mux_atest_usb33,
	msm_mux_ddr_pxi3,
	msm_mux_GRFC1,
	msm_mux_atest_usb32,
	msm_mux_NA,
};

static const char * const GRFC2_groups[] = {
	"gpio73",
};
static const char * const atest_usb31_groups[] = {
	"gpio73",
};
static const char * const emac_pps_groups[] = {
	"gpio81",
};
static const char * const ddr_pxi4_groups[] = {
	"gpio73", "gpio74",
};
static const char * const GRFC3_groups[] = {
	"gpio74",
};
static const char * const tgu_ch4_groups[] = {
	"gpio74",
};
static const char * const atest_usb30_groups[] = {
	"gpio74",
};
static const char * const GRFC4_groups[] = {
	"gpio75",
};
static const char * const tgu_ch7_groups[] = {
	"gpio75",
};
static const char * const atest_usb4_groups[] = {
	"gpio75",
};
static const char * const ddr_pxi5_groups[] = {
	"gpio75", "gpio76",
};
static const char * const GRFC5_groups[] = {
	"gpio76",
};
static const char * const tgu_ch6_groups[] = {
	"gpio76",
};
static const char * const atest_usb43_groups[] = {
	"gpio76",
};
static const char * const GRFC6_groups[] = {
	"gpio77",
};
static const char * const tgu_ch5_groups[] = {
	"gpio77",
};
static const char * const atest_usb42_groups[] = {
	"gpio77",
};
static const char * const ddr_pxi6_groups[] = {
	"gpio77", "gpio78",
};
static const char * const GRFC7_groups[] = {
	"gpio78",
};
static const char * const atest_usb41_groups[] = {
	"gpio78",
};
static const char * const GRFC13_groups[] = {
	"gpio79",
};
static const char * const GRFC14_groups[] = {
	"gpio80",
};
static const char * const GRFC15_groups[] = {
	"gpio81",
};
static const char * const GRFC16_groups[] = {
	"gpio82",
};
static const char * const qup12_groups[] = {
	"gpio83", "gpio84", "gpio85", "gpio86",
};
static const char * const qup16_groups[] = {
	"gpio83", "gpio84", "gpio85", "gpio86",
};
static const char * const tsif1_clk_groups[] = {
	"gpio88",
};
static const char * const qup8_groups[] = {
	"gpio88", "gpio89", "gpio90", "gpio91",
};
static const char * const qspi0_cs_groups[] = {
	"gpio88", "gpio94",
};
static const char * const tgu_ch3_groups[] = {
	"gpio88",
};
static const char * const atest_usb40_groups[] = {
	"gpio88",
};
static const char * const ddr_pxi7_groups[] = {
	"gpio88", "gpio90",
};
static const char * const tsif1_en_groups[] = {
	"gpio89",
};
static const char * const qspi00_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync0_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync1_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync2_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync3_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync4_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync5_groups[] = {
	"gpio89",
};
static const char * const tgu_ch0_groups[] = {
	"gpio89",
};
static const char * const tsif1_data_groups[] = {
	"gpio90",
};
static const char * const qspi01_groups[] = {
	"gpio90",
};
static const char * const sdc4_cmd_groups[] = {
	"gpio90",
};
static const char * const tgu_ch1_groups[] = {
	"gpio90",
};
static const char * const atest_usb0_groups[] = {
	"gpio90",
};
static const char * const tsif1_sync_groups[] = {
	"gpio91",
};
static const char * const qspi02_groups[] = {
	"gpio91",
};
static const char * const sdc43_groups[] = {
	"gpio91",
};
static const char * const vfr_1_groups[] = {
	"gpio91",
};
static const char * const tgu_ch2_groups[] = {
	"gpio91",
};
static const char * const atest_usb03_groups[] = {
	"gpio91",
};
static const char * const tsif2_clk_groups[] = {
	"gpio92",
};
static const char * const qup11_groups[] = {
	"gpio92", "gpio93", "gpio94", "gpio95",
};
static const char * const qspi0_clk_groups[] = {
	"gpio92",
};
static const char * const sdc4_clk_groups[] = {
	"gpio92",
};
static const char * const atest_usb02_groups[] = {
	"gpio92",
};
static const char * const tsif2_en_groups[] = {
	"gpio93",
};
static const char * const qspi03_groups[] = {
	"gpio93",
};
static const char * const sdc42_groups[] = {
	"gpio93",
};
static const char * const atest_tsens_groups[] = {
	"gpio93",
};
static const char * const atest_usb01_groups[] = {
	"gpio93",
};
static const char * const tsif2_data_groups[] = {
	"gpio94",
};
static const char * const sdc41_groups[] = {
	"gpio94",
};
static const char * const atest_usb00_groups[] = {
	"gpio94",
};
static const char * const tsif2_sync_groups[] = {
	"gpio95",
};
static const char * const sdc40_groups[] = {
	"gpio95",
};
static const char * const atest_usb2_groups[] = {
	"gpio95",
};
static const char * const tsif2_error_groups[] = {
	"gpio96",
};
static const char * const sd_write_groups[] = {
	"gpio97",
};
static const char * const tsif1_error_groups[] = {
	"gpio97",
};
static const char * const qup7_groups[] = {
	"gpio98", "gpio99", "gpio100", "gpio101",
};
static const char * const ddr_bist_groups[] = {
	"gpio98", "gpio99", "gpio145", "gpio146",
};
static const char * const pll_bypassnl_groups[] = {
	"gpio100",
};
static const char * const pll_reset_groups[] = {
	"gpio101",
};
static const char * const pci_e1_groups[] = {
	"gpio102", "gpio103",
};
static const char * const phase_flag4_groups[] = {
	"gpio102",
};
static const char * const atest_usb23_groups[] = {
	"gpio102",
};
static const char * const uim2_data_groups[] = {
	"gpio105",
};
static const char * const uim2_clk_groups[] = {
	"gpio106",
};
static const char * const uim2_reset_groups[] = {
	"gpio107",
};
static const char * const uim2_present_groups[] = {
	"gpio108",
};
static const char * const uim1_data_groups[] = {
	"gpio109",
};
static const char * const uim1_clk_groups[] = {
	"gpio110",
};
static const char * const uim1_reset_groups[] = {
	"gpio111",
};
static const char * const uim1_present_groups[] = {
	"gpio112",
};
static const char * const uim_batt_groups[] = {
	"gpio113",
};
static const char * const aoss_cti_groups[] = {
	"gpio113",
};
static const char * const qup1_groups[] = {
	"gpio114", "gpio115", "gpio116", "gpio117",
};
static const char * const rgmii_txc_groups[] = {
	"gpio114",
};
static const char * const rgmii_rxc_groups[] = {
	"gpio115",
};
static const char * const adsp_ext_groups[] = {
	"gpio115",
};
static const char * const rgmii_rx_groups[] = {
	"gpio116",
};
static const char * const rgmii_rxd0_groups[] = {
	"gpio117",
};
static const char * const rgmii_rxd1_groups[] = {
	"gpio118",
};
static const char * const qup5_groups[] = {
	"gpio119", "gpio120", "gpio121", "gpio122",
};
static const char * const rgmii_rxd2_groups[] = {
	"gpio119",
};
static const char * const rgmii_rxd3_groups[] = {
	"gpio120",
};
static const char * const phase_flag17_groups[] = {
	"gpio120",
};
static const char * const rgmii_tx_groups[] = {
	"gpio121",
};
static const char * const phase_flag16_groups[] = {
	"gpio121",
};
static const char * const atest_usb21_groups[] = {
	"gpio121",
};
static const char * const rgmii_txd0_groups[] = {
	"gpio122",
};
static const char * const phase_flag15_groups[] = {
	"gpio122",
};
static const char * const atest_usb20_groups[] = {
	"gpio122",
};
static const char * const phase_flag14_groups[] = {
	"gpio123",
};
static const char * const emac_phy_groups[] = {
	"gpio124",
};
static const char * const hs3_mi2s_groups[] = {
	"gpio125", "gpio165", "gpio166", "gpio167", "gpio168",
};
static const char * const phase_flag22_groups[] = {
	"gpio125",
};
static const char * const sec_mi2s_groups[] = {
	"gpio126", "gpio127", "gpio128", "gpio129", "gpio130",
};
static const char * const qup2_groups[] = {
	"gpio126", "gpio127", "gpio128", "gpio129",
};
static const char * const jitter_bist_groups[] = {
	"gpio129",
};
static const char * const pll_bist_groups[] = {
	"gpio130",
};
static const char * const ter_mi2s_groups[] = {
	"gpio131", "gpio132", "gpio133", "gpio134", "gpio135",
};
static const char * const gcc_gp1_groups[] = {
	"gpio131", "gpio136",
};
static const char * const atest_char_groups[] = {
	"gpio133",
};
static const char * const atest_char0_groups[] = {
	"gpio134",
};
static const char * const atest_char1_groups[] = {
	"gpio135",
};
static const char * const qua_mi2s_groups[] = {
	"gpio136", "gpio137", "gpio138", "gpio139", "gpio140", "gpio141",
	"gpio142",
};
static const char * const gcc_gp4_groups[] = {
	"gpio139", "gpio182",
};
static const char * const gcc_gp5_groups[] = {
	"gpio140", "gpio183",
};
static const char * const atest_char2_groups[] = {
	"gpio140",
};
static const char * const atest_char3_groups[] = {
	"gpio142",
};
static const char * const pri_mi2s_groups[] = {
	"gpio143", "gpio144", "gpio146", "gpio147",
};
static const char * const qup3_groups[] = {
	"gpio144", "gpio145", "gpio146", "gpio147",
};
static const char * const pri_mi2s_ws_groups[] = {
	"gpio145",
};
static const char * const spkr_i2s_groups[] = {
	"gpio148", "gpio149", "gpio150", "gpio151", "gpio152",
};
static const char * const audio_ref_groups[] = {
	"gpio148",
};
static const char * const phase_flag21_groups[] = {
	"gpio148",
};
static const char * const lpass_slimbus_groups[] = {
	"gpio149", "gpio150", "gpio151", "gpio152",
};
static const char * const phase_flag20_groups[] = {
	"gpio149",
};
static const char * const phase_flag19_groups[] = {
	"gpio150",
};
static const char * const tsense_pwm1_groups[] = {
	"gpio150",
};
static const char * const tsense_pwm2_groups[] = {
	"gpio150",
};
static const char * const phase_flag18_groups[] = {
	"gpio151",
};
static const char * const phase_flag13_groups[] = {
	"gpio152",
};
static const char * const btfm_slimbus_groups[] = {
	"gpio153", "gpio154",
};
static const char * const hs1_mi2s_groups[] = {
	"gpio155", "gpio156", "gpio157", "gpio158", "gpio159",
};
static const char * const phase_flag12_groups[] = {
	"gpio155",
};
static const char * const phase_flag11_groups[] = {
	"gpio156",
};
static const char * const phase_flag10_groups[] = {
	"gpio157",
};
static const char * const phase_flag9_groups[] = {
	"gpio158",
};
static const char * const cri_trng0_groups[] = {
	"gpio159",
};
static const char * const phase_flag8_groups[] = {
	"gpio159",
};
static const char * const hs2_mi2s_groups[] = {
	"gpio160", "gpio161", "gpio162", "gpio163", "gpio164",
};
static const char * const cri_trng1_groups[] = {
	"gpio160",
};
static const char * const phase_flag7_groups[] = {
	"gpio160",
};
static const char * const qspi11_groups[] = {
	"gpio161",
};
static const char * const cri_trng_groups[] = {
	"gpio161",
};
static const char * const phase_flag24_groups[] = {
	"gpio161",
};
static const char * const qspi10_groups[] = {
	"gpio162",
};
static const char * const sp_cmu_groups[] = {
	"gpio162",
};
static const char * const phase_flag6_groups[] = {
	"gpio162",
};
static const char * const qspi1_clk_groups[] = {
	"gpio163",
};
static const char * const prng_rosc_groups[] = {
	"gpio163",
};
static const char * const phase_flag5_groups[] = {
	"gpio163",
};
static const char * const phase_flag31_groups[] = {
	"gpio164",
};
static const char * const phase_flag30_groups[] = {
	"gpio165",
};
static const char * const phase_flag27_groups[] = {
	"gpio166",
};
static const char * const phase_flag26_groups[] = {
	"gpio167",
};
static const char * const phase_flag25_groups[] = {
	"gpio168",
};
static const char * const pci_e2_groups[] = {
	"gpio175", "gpio176",
};
static const char * const pci_e3_groups[] = {
	"gpio178", "gpio179",
};
static const char * const cci_timer4_groups[] = {
	"gpio178",
};
static const char * const qup19_groups[] = {
	"gpio181", "gpio182", "gpio183", "gpio184",
};
static const char * const cci_timer5_groups[] = {
	"gpio182",
};
static const char * const cci_timer6_groups[] = {
	"gpio183",
};
static const char * const cci_timer7_groups[] = {
	"gpio184",
};
static const char * const cci_timer8_groups[] = {
	"gpio185",
};
static const char * const cci_timer9_groups[] = {
	"gpio186",
};
static const char * const dp_hot_groups[] = {
	"gpio189",
};
static const char * const qup0_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio12", "gpio13",
	"gpio14", "gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20",
	"gpio21", "gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio27",
	"gpio28", "gpio29", "gpio30", "gpio31", "gpio32", "gpio33", "gpio34",
	"gpio35", "gpio36", "gpio37", "gpio38", "gpio39", "gpio40", "gpio41",
	"gpio42", "gpio43", "gpio44", "gpio45", "gpio46", "gpio47", "gpio48",
	"gpio49", "gpio50", "gpio51", "gpio52", "gpio53", "gpio54", "gpio55",
	"gpio56", "gpio57", "gpio58", "gpio59", "gpio60", "gpio61", "gpio62",
	"gpio63", "gpio64", "gpio65", "gpio66", "gpio67", "gpio68", "gpio69",
	"gpio70", "gpio71", "gpio72", "gpio73", "gpio74", "gpio75", "gpio76",
	"gpio77", "gpio78", "gpio79", "gpio80", "gpio81", "gpio82", "gpio83",
	"gpio84", "gpio85", "gpio86", "gpio87", "gpio88", "gpio89", "gpio90",
	"gpio91", "gpio92", "gpio93", "gpio94", "gpio95", "gpio96", "gpio97",
	"gpio98", "gpio99", "gpio100", "gpio101", "gpio102", "gpio103",
	"gpio104", "gpio105", "gpio106", "gpio107", "gpio108", "gpio109",
	"gpio110", "gpio111", "gpio112", "gpio113", "gpio114", "gpio115",
	"gpio116", "gpio117", "gpio118", "gpio119", "gpio120", "gpio121",
	"gpio122", "gpio123", "gpio124", "gpio125", "gpio126", "gpio127",
	"gpio128", "gpio129", "gpio130", "gpio131", "gpio132", "gpio133",
	"gpio134", "gpio135", "gpio136", "gpio137", "gpio138", "gpio139",
	"gpio140", "gpio141", "gpio142", "gpio143", "gpio144", "gpio145",
	"gpio146", "gpio147", "gpio148", "gpio149", "gpio150", "gpio151",
	"gpio152", "gpio153", "gpio154", "gpio155", "gpio156", "gpio157",
	"gpio158", "gpio159", "gpio160", "gpio161", "gpio162", "gpio163",
	"gpio164", "gpio165", "gpio166", "gpio167", "gpio168", "gpio169",
	"gpio170", "gpio171", "gpio172", "gpio173", "gpio174", "gpio175",
	"gpio176", "gpio177", "gpio177", "gpio178", "gpio179", "gpio180",
	"gpio181", "gpio182", "gpio183", "gpio184", "gpio185", "gpio186",
	"gpio186", "gpio187", "gpio187", "gpio188", "gpio188", "gpio189",
};
static const char * const egpio_groups[] = {
	"gpio155", "gpio156", "gpio157", "gpio158",
	"gpio159", "gpio160", "gpio161", "gpio162",
	"gpio163", "gpio164", "gpio165", "gpio166",
	"gpio167", "gpio168",
	"gpio169", "gpio172", "gpio173", "gpio174",
};
static const char * const cci_i2c_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio17", "gpio18", "gpio19",
	"gpio20", "gpio31", "gpio32", "gpio33", "gpio34", "gpio39", "gpio40",
	"gpio41", "gpio42",
};
static const char * const qup6_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const rgmii_txd1_groups[] = {
	"gpio4",
};
static const char * const phase_flag3_groups[] = {
	"gpio4",
};
static const char * const rgmii_txd2_groups[] = {
	"gpio5",
};
static const char * const phase_flag2_groups[] = {
	"gpio5",
};
static const char * const rgmii_txd3_groups[] = {
	"gpio6",
};
static const char * const qup_l6_groups[] = {
	"gpio6", "gpio34", "gpio37", "gpio97",
};
static const char * const phase_flag1_groups[] = {
	"gpio6",
};
static const char * const debug_hot_groups[] = {
	"gpio7",
};
static const char * const rgmii_mdc_groups[] = {
	"gpio7",
};
static const char * const qup_l5_groups[] = {
	"gpio7", "gpio33", "gpio36", "gpio96",
};
static const char * const phase_flag0_groups[] = {
	"gpio7",
};
static const char * const mdp_vsync_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio60", "gpio82",
};
static const char * const qup10_groups[] = {
	"gpio9", "gpio10", "gpio11", "gpio12",
};
static const char * const edp_hot_groups[] = {
	"gpio10",
};
static const char * const m_voc_groups[] = {
	"gpio10",
};
static const char * const edp_lcd_groups[] = {
	"gpio11",
};
static const char * const cam_mclk_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16", "gpio25", "gpio179", "gpio180",
	"gpio181",
};
static const char * const qdss_gpio0_groups[] = {
	"gpio13", "gpio114",
};
static const char * const qdss_gpio1_groups[] = {
	"gpio14", "gpio115",
};
static const char * const qdss_gpio2_groups[] = {
	"gpio15", "gpio116",
};
static const char * const qdss_gpio3_groups[] = {
	"gpio16", "gpio117",
};
static const char * const qdss_gpio4_groups[] = {
	"gpio17", "gpio118",
};
static const char * const qdss_gpio5_groups[] = {
	"gpio18", "gpio119",
};
static const char * const qdss_gpio6_groups[] = {
	"gpio19", "gpio39",
};
static const char * const qdss_gpio7_groups[] = {
	"gpio20", "gpio40",
};
static const char * const cci_timer0_groups[] = {
	"gpio21",
};
static const char * const gcc_gp2_groups[] = {
	"gpio21", "gpio137",
};
static const char * const qdss_gpio8_groups[] = {
	"gpio21", "gpio133",
};
static const char * const cci_timer1_groups[] = {
	"gpio22",
};
static const char * const gcc_gp3_groups[] = {
	"gpio22", "gpio138",
};
static const char * const qdss_gpio_groups[] = {
	"gpio22", "gpio28", "gpio120", "gpio121",
};
static const char * const cci_timer2_groups[] = {
	"gpio23",
};
static const char * const qup18_groups[] = {
	"gpio23", "gpio24", "gpio25", "gpio26",
};
static const char * const qdss_gpio9_groups[] = {
	"gpio23", "gpio134",
};
static const char * const cci_timer3_groups[] = {
	"gpio24",
};
static const char * const cci_async_groups[] = {
	"gpio24", "gpio25", "gpio26", "gpio176", "gpio185", "gpio186",
};
static const char * const qdss_gpio10_groups[] = {
	"gpio24", "gpio135",
};
static const char * const qdss_gpio11_groups[] = {
	"gpio25", "gpio132",
};
static const char * const qdss_gpio12_groups[] = {
	"gpio26", "gpio130",
};
static const char * const qup15_groups[] = {
	"gpio27", "gpio28", "gpio29", "gpio30",
};
static const char * const qdss_gpio15_groups[] = {
	"gpio27", "gpio42",
};
static const char * const qdss_gpio13_groups[] = {
	"gpio29", "gpio92",
};
static const char * const qdss_gpio14_groups[] = {
	"gpio30", "gpio41",
};
static const char * const phase_flag23_groups[] = {
	"gpio33",
};
static const char * const dbg_out_groups[] = {
	"gpio34",
};
static const char * const atest_usb22_groups[] = {
	"gpio34",
};
static const char * const pci_e0_groups[] = {
	"gpio35", "gpio36",
};
static const char * const qup_l4_groups[] = {
	"gpio35", "gpio59", "gpio60", "gpio95",
};
static const char * const agera_pll_groups[] = {
	"gpio37",
};
static const char * const usb0_phy_groups[] = {
	"gpio38",
};
static const char * const qup9_groups[] = {
	"gpio39", "gpio40", "gpio41", "gpio42",
};
static const char * const qup13_groups[] = {
	"gpio43", "gpio44", "gpio45", "gpio46",
};
static const char * const qup14_groups[] = {
	"gpio47", "gpio48", "gpio49", "gpio50",
};
static const char * const usb2phy_ac_groups[] = {
	"gpio47", "gpio48", "gpio113", "gpio123",
};
static const char * const qdss_cti_groups[] = {
	"gpio49", "gpio50", "gpio81", "gpio82", "gpio89", "gpio90", "gpio141",
	"gpio142",
};
static const char * const qup4_groups[] = {
	"gpio51", "gpio52", "gpio53", "gpio54",
};
static const char * const phase_flag29_groups[] = {
	"gpio53",
};
static const char * const phase_flag28_groups[] = {
	"gpio54",
};
static const char * const qup17_groups[] = {
	"gpio55", "gpio56", "gpio57", "gpio58",
};
static const char * const qspi1_cs_groups[] = {
	"gpio55", "gpio164",
};
static const char * const qspi13_groups[] = {
	"gpio56",
};
static const char * const qspi12_groups[] = {
	"gpio57",
};
static const char * const usb1_phy_groups[] = {
	"gpio58",
};
static const char * const rgmii_mdio_groups[] = {
	"gpio59",
};
static const char * const GPS_TX_groups[] = {
	"gpio60", "gpio76", "gpio77", "gpio81", "gpio82",
};
static const char * const NAV_PPS_groups[] = {
	"gpio60", "gpio60", "gpio76", "gpio76", "gpio77", "gpio77", "gpio81",
	"gpio81", "gpio82", "gpio82",
};
static const char * const atest_usb1_groups[] = {
	"gpio60",
};
static const char * const ddr_pxi0_groups[] = {
	"gpio60", "gpio62",
};
static const char * const QLINK_REQUEST_groups[] = {
	"gpio61",
};
static const char * const QLINK_ENABLE_groups[] = {
	"gpio62",
};
static const char * const atest_tsens2_groups[] = {
	"gpio62",
};
static const char * const atest_usb13_groups[] = {
	"gpio62",
};
static const char * const vsense_trigger_groups[] = {
	"gpio62",
};
static const char * const wmss_reset_groups[] = {
	"gpio63",
};
static const char * const atest_usb12_groups[] = {
	"gpio63",
};
static const char * const ddr_pxi1_groups[] = {
	"gpio63", "gpio64",
};
static const char * const wlan1_adc1_groups[] = {
	"gpio63",
};
static const char * const GRFC8_groups[] = {
	"gpio64",
};
static const char * const atest_usb11_groups[] = {
	"gpio64",
};
static const char * const wlan1_adc0_groups[] = {
	"gpio64",
};
static const char * const GRFC9_groups[] = {
	"gpio65",
};
static const char * const atest_usb10_groups[] = {
	"gpio65",
};
static const char * const ddr_pxi2_groups[] = {
	"gpio65", "gpio68",
};
static const char * const wlan2_adc1_groups[] = {
	"gpio65",
};
static const char * const GRFC10_groups[] = {
	"gpio66",
};
static const char * const GRFC11_groups[] = {
	"gpio67",
};
static const char * const GRFC12_groups[] = {
	"gpio68",
};
static const char * const pa_indicator_groups[] = {
	"gpio68",
};
static const char * const atest_usb3_groups[] = {
	"gpio68",
};
static const char * const wlan2_adc0_groups[] = {
	"gpio68",
};
static const char * const mss_lte_groups[] = {
	"gpio69", "gpio70",
};
static const char * const GRFC0_groups[] = {
	"gpio71",
};
static const char * const atest_usb33_groups[] = {
	"gpio71",
};
static const char * const ddr_pxi3_groups[] = {
	"gpio71", "gpio72",
};
static const char * const GRFC1_groups[] = {
	"gpio72",
};
static const char * const atest_usb32_groups[] = {
	"gpio72",
};

static const struct msm_function sdmshrike_functions[] = {
	FUNCTION(GRFC2),
	FUNCTION(atest_usb31),
	FUNCTION(emac_pps),
	FUNCTION(ddr_pxi4),
	FUNCTION(GRFC3),
	FUNCTION(tgu_ch4),
	FUNCTION(atest_usb30),
	FUNCTION(GRFC4),
	FUNCTION(tgu_ch7),
	FUNCTION(atest_usb4),
	FUNCTION(ddr_pxi5),
	FUNCTION(GRFC5),
	FUNCTION(tgu_ch6),
	FUNCTION(atest_usb43),
	FUNCTION(GRFC6),
	FUNCTION(tgu_ch5),
	FUNCTION(atest_usb42),
	FUNCTION(ddr_pxi6),
	FUNCTION(GRFC7),
	FUNCTION(atest_usb41),
	FUNCTION(GRFC13),
	FUNCTION(GRFC14),
	FUNCTION(GRFC15),
	FUNCTION(GRFC16),
	FUNCTION(qup12),
	FUNCTION(qup16),
	FUNCTION(tsif1_clk),
	FUNCTION(qup8),
	FUNCTION(qspi0_cs),
	FUNCTION(tgu_ch3),
	FUNCTION(atest_usb40),
	FUNCTION(ddr_pxi7),
	FUNCTION(tsif1_en),
	FUNCTION(qspi00),
	FUNCTION(mdp_vsync0),
	FUNCTION(mdp_vsync1),
	FUNCTION(mdp_vsync2),
	FUNCTION(mdp_vsync3),
	FUNCTION(mdp_vsync4),
	FUNCTION(mdp_vsync5),
	FUNCTION(tgu_ch0),
	FUNCTION(tsif1_data),
	FUNCTION(qspi01),
	FUNCTION(sdc4_cmd),
	FUNCTION(tgu_ch1),
	FUNCTION(atest_usb0),
	FUNCTION(tsif1_sync),
	FUNCTION(qspi02),
	FUNCTION(sdc43),
	FUNCTION(vfr_1),
	FUNCTION(tgu_ch2),
	FUNCTION(atest_usb03),
	FUNCTION(tsif2_clk),
	FUNCTION(qup11),
	FUNCTION(qspi0_clk),
	FUNCTION(sdc4_clk),
	FUNCTION(atest_usb02),
	FUNCTION(tsif2_en),
	FUNCTION(qspi03),
	FUNCTION(sdc42),
	FUNCTION(atest_tsens),
	FUNCTION(atest_usb01),
	FUNCTION(tsif2_data),
	FUNCTION(sdc41),
	FUNCTION(atest_usb00),
	FUNCTION(tsif2_sync),
	FUNCTION(sdc40),
	FUNCTION(atest_usb2),
	FUNCTION(tsif2_error),
	FUNCTION(sd_write),
	FUNCTION(tsif1_error),
	FUNCTION(qup7),
	FUNCTION(ddr_bist),
	FUNCTION(pll_bypassnl),
	FUNCTION(pll_reset),
	FUNCTION(pci_e1),
	FUNCTION(phase_flag4),
	FUNCTION(atest_usb23),
	FUNCTION(uim2_data),
	FUNCTION(uim2_clk),
	FUNCTION(uim2_reset),
	FUNCTION(uim2_present),
	FUNCTION(uim1_data),
	FUNCTION(uim1_clk),
	FUNCTION(uim1_reset),
	FUNCTION(uim1_present),
	FUNCTION(uim_batt),
	FUNCTION(aoss_cti),
	FUNCTION(qup1),
	FUNCTION(rgmii_txc),
	FUNCTION(rgmii_rxc),
	FUNCTION(adsp_ext),
	FUNCTION(rgmii_rx),
	FUNCTION(rgmii_rxd0),
	FUNCTION(rgmii_rxd1),
	FUNCTION(qup5),
	FUNCTION(rgmii_rxd2),
	FUNCTION(rgmii_rxd3),
	FUNCTION(phase_flag17),
	FUNCTION(rgmii_tx),
	FUNCTION(phase_flag16),
	FUNCTION(atest_usb21),
	FUNCTION(rgmii_txd0),
	FUNCTION(phase_flag15),
	FUNCTION(atest_usb20),
	FUNCTION(phase_flag14),
	FUNCTION(emac_phy),
	FUNCTION(hs3_mi2s),
	FUNCTION(phase_flag22),
	FUNCTION(sec_mi2s),
	FUNCTION(qup2),
	FUNCTION(jitter_bist),
	FUNCTION(pll_bist),
	FUNCTION(ter_mi2s),
	FUNCTION(gcc_gp1),
	FUNCTION(atest_char),
	FUNCTION(atest_char0),
	FUNCTION(atest_char1),
	FUNCTION(qua_mi2s),
	FUNCTION(gcc_gp4),
	FUNCTION(gcc_gp5),
	FUNCTION(atest_char2),
	FUNCTION(atest_char3),
	FUNCTION(pri_mi2s),
	FUNCTION(qup3),
	FUNCTION(pri_mi2s_ws),
	FUNCTION(spkr_i2s),
	FUNCTION(audio_ref),
	FUNCTION(phase_flag21),
	FUNCTION(lpass_slimbus),
	FUNCTION(phase_flag20),
	FUNCTION(phase_flag19),
	FUNCTION(tsense_pwm1),
	FUNCTION(tsense_pwm2),
	FUNCTION(phase_flag18),
	FUNCTION(phase_flag13),
	FUNCTION(btfm_slimbus),
	FUNCTION(hs1_mi2s),
	FUNCTION(phase_flag12),
	FUNCTION(phase_flag11),
	FUNCTION(phase_flag10),
	FUNCTION(phase_flag9),
	FUNCTION(cri_trng0),
	FUNCTION(phase_flag8),
	FUNCTION(hs2_mi2s),
	FUNCTION(cri_trng1),
	FUNCTION(phase_flag7),
	FUNCTION(qspi11),
	FUNCTION(cri_trng),
	FUNCTION(phase_flag24),
	FUNCTION(qspi10),
	FUNCTION(sp_cmu),
	FUNCTION(phase_flag6),
	FUNCTION(qspi1_clk),
	FUNCTION(prng_rosc),
	FUNCTION(phase_flag5),
	FUNCTION(phase_flag31),
	FUNCTION(phase_flag30),
	FUNCTION(phase_flag27),
	FUNCTION(phase_flag26),
	FUNCTION(phase_flag25),
	FUNCTION(pci_e2),
	FUNCTION(pci_e3),
	FUNCTION(cci_timer4),
	FUNCTION(qup19),
	FUNCTION(cci_timer5),
	FUNCTION(cci_timer6),
	FUNCTION(cci_timer7),
	FUNCTION(cci_timer8),
	FUNCTION(cci_timer9),
	FUNCTION(dp_hot),
	FUNCTION(egpio),
	FUNCTION(qup0),
	FUNCTION(gpio),
	FUNCTION(cci_i2c),
	FUNCTION(qup6),
	FUNCTION(rgmii_txd1),
	FUNCTION(phase_flag3),
	FUNCTION(rgmii_txd2),
	FUNCTION(phase_flag2),
	FUNCTION(rgmii_txd3),
	FUNCTION(qup_l6),
	FUNCTION(phase_flag1),
	FUNCTION(debug_hot),
	FUNCTION(rgmii_mdc),
	FUNCTION(qup_l5),
	FUNCTION(phase_flag0),
	FUNCTION(mdp_vsync),
	FUNCTION(qup10),
	FUNCTION(edp_hot),
	FUNCTION(m_voc),
	FUNCTION(edp_lcd),
	FUNCTION(cam_mclk),
	FUNCTION(qdss_gpio0),
	FUNCTION(qdss_gpio1),
	FUNCTION(qdss_gpio2),
	FUNCTION(qdss_gpio3),
	FUNCTION(qdss_gpio4),
	FUNCTION(qdss_gpio5),
	FUNCTION(qdss_gpio6),
	FUNCTION(qdss_gpio7),
	FUNCTION(cci_timer0),
	FUNCTION(gcc_gp2),
	FUNCTION(qdss_gpio8),
	FUNCTION(cci_timer1),
	FUNCTION(gcc_gp3),
	FUNCTION(qdss_gpio),
	FUNCTION(cci_timer2),
	FUNCTION(qup18),
	FUNCTION(qdss_gpio9),
	FUNCTION(cci_timer3),
	FUNCTION(cci_async),
	FUNCTION(qdss_gpio10),
	FUNCTION(qdss_gpio11),
	FUNCTION(qdss_gpio12),
	FUNCTION(qup15),
	FUNCTION(qdss_gpio15),
	FUNCTION(qdss_gpio13),
	FUNCTION(qdss_gpio14),
	FUNCTION(phase_flag23),
	FUNCTION(dbg_out),
	FUNCTION(atest_usb22),
	FUNCTION(pci_e0),
	FUNCTION(qup_l4),
	FUNCTION(agera_pll),
	FUNCTION(usb0_phy),
	FUNCTION(qup9),
	FUNCTION(qup13),
	FUNCTION(qup14),
	FUNCTION(usb2phy_ac),
	FUNCTION(qdss_cti),
	FUNCTION(qup4),
	FUNCTION(phase_flag29),
	FUNCTION(phase_flag28),
	FUNCTION(qup17),
	FUNCTION(qspi1_cs),
	FUNCTION(qspi13),
	FUNCTION(qspi12),
	FUNCTION(usb1_phy),
	FUNCTION(rgmii_mdio),
	FUNCTION(GPS_TX),
	FUNCTION(NAV_PPS),
	FUNCTION(atest_usb1),
	FUNCTION(ddr_pxi0),
	FUNCTION(QLINK_REQUEST),
	FUNCTION(QLINK_ENABLE),
	FUNCTION(atest_tsens2),
	FUNCTION(atest_usb13),
	FUNCTION(vsense_trigger),
	FUNCTION(wmss_reset),
	FUNCTION(atest_usb12),
	FUNCTION(ddr_pxi1),
	FUNCTION(wlan1_adc1),
	FUNCTION(GRFC8),
	FUNCTION(atest_usb11),
	FUNCTION(wlan1_adc0),
	FUNCTION(GRFC9),
	FUNCTION(atest_usb10),
	FUNCTION(ddr_pxi2),
	FUNCTION(wlan2_adc1),
	FUNCTION(GRFC10),
	FUNCTION(GRFC11),
	FUNCTION(GRFC12),
	FUNCTION(pa_indicator),
	FUNCTION(atest_usb3),
	FUNCTION(wlan2_adc0),
	FUNCTION(mss_lte),
	FUNCTION(GRFC0),
	FUNCTION(atest_usb33),
	FUNCTION(ddr_pxi3),
	FUNCTION(GRFC1),
	FUNCTION(atest_usb32),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup sdmshrike_groups[] = {
	[0] = PINGROUP(0, WEST, qup0, cci_i2c, NA, NA, NA, NA, NA, NA, NA),
	[1] = PINGROUP(1, WEST, qup0, cci_i2c, NA, NA, NA, NA, NA, NA, NA),
	[2] = PINGROUP(2, WEST, qup0, cci_i2c, NA, NA, NA, NA, NA, NA, NA),
	[3] = PINGROUP(3, WEST, qup0, cci_i2c, NA, NA, NA, NA, NA, NA, NA),
	[4] = PINGROUP(4, WEST, qup6, rgmii_txd1, NA, phase_flag3, NA, NA, NA,
		       NA, NA),
	[5] = PINGROUP(5, WEST, qup6, rgmii_txd2, NA, phase_flag2, NA, NA, NA,
		       NA, NA),
	[6] = PINGROUP(6, WEST, qup6, rgmii_txd3, qup_l6, NA, phase_flag1, NA,
		       NA, NA, NA),
	[7] = PINGROUP(7, WEST, qup6, debug_hot, rgmii_mdc, qup_l5, NA,
		       phase_flag0, NA, NA, NA),
	[8] = PINGROUP(8, EAST, mdp_vsync, NA, NA, NA, NA, NA, NA, NA, NA),
	[9] = PINGROUP(9, EAST, mdp_vsync, qup10, NA, NA, NA, NA, NA, NA, NA),
	[10] = PINGROUP(10, EAST, edp_hot, m_voc, mdp_vsync, qup10, NA, NA, NA,
			NA, NA),
	[11] = PINGROUP(11, EAST, edp_lcd, qup10, NA, NA, NA, NA, NA, NA, NA),
	[12] = PINGROUP(12, EAST, qup10, NA, NA, NA, NA, NA, NA, NA, NA),
	[13] = PINGROUP(13, EAST, cam_mclk, qdss_gpio0, NA, NA, NA, NA, NA, NA,
			NA),
	[14] = PINGROUP(14, EAST, cam_mclk, qdss_gpio1, NA, NA, NA, NA, NA, NA,
			NA),
	[15] = PINGROUP(15, EAST, cam_mclk, qdss_gpio2, NA, NA, NA, NA, NA, NA,
			NA),
	[16] = PINGROUP(16, EAST, cam_mclk, qdss_gpio3, NA, NA, NA, NA, NA, NA,
			NA),
	[17] = PINGROUP(17, EAST, cci_i2c, qdss_gpio4, NA, NA, NA, NA, NA, NA,
			NA),
	[18] = PINGROUP(18, EAST, cci_i2c, qdss_gpio5, NA, NA, NA, NA, NA, NA,
			NA),
	[19] = PINGROUP(19, EAST, cci_i2c, qdss_gpio6, NA, NA, NA, NA, NA, NA,
			NA),
	[20] = PINGROUP(20, EAST, cci_i2c, qdss_gpio7, NA, NA, NA, NA, NA, NA,
			NA),
	[21] = PINGROUP(21, EAST, cci_timer0, gcc_gp2, qdss_gpio8, NA, NA, NA,
			NA, NA, NA),
	[22] = PINGROUP(22, EAST, cci_timer1, gcc_gp3, qdss_gpio, NA, NA, NA,
			NA, NA, NA),
	[23] = PINGROUP(23, EAST, cci_timer2, qup18, qdss_gpio9, NA, NA, NA,
			NA, NA, NA),
	[24] = PINGROUP(24, EAST, cci_timer3, cci_async, qup18, qdss_gpio10,
			NA, NA, NA, NA, NA),
	[25] = PINGROUP(25, EAST, cam_mclk, cci_async, qup18, qdss_gpio11, NA,
			NA, NA, NA, NA),
	[26] = PINGROUP(26, EAST, cci_async, qup18, qdss_gpio12, NA, NA, NA,
			NA, NA, NA),
	[27] = PINGROUP(27, EAST, qup15, NA, qdss_gpio15, NA, NA, NA, NA, NA,
			NA),
	[28] = PINGROUP(28, EAST, qup15, qdss_gpio, NA, NA, NA, NA, NA, NA, NA),
	[29] = PINGROUP(29, EAST, qup15, qdss_gpio13, NA, NA, NA, NA, NA, NA,
			NA),
	[30] = PINGROUP(30, EAST, qup15, qdss_gpio14, NA, NA, NA, NA, NA, NA,
			NA),
	[31] = PINGROUP(31, EAST, cci_i2c, NA, NA, NA, NA, NA, NA, NA, NA),
	[32] = PINGROUP(32, EAST, cci_i2c, NA, NA, NA, NA, NA, NA, NA, NA),
	[33] = PINGROUP(33, EAST, cci_i2c, qup_l5, NA, phase_flag23, NA, NA,
			NA, NA, NA),
	[34] = PINGROUP(34, EAST, cci_i2c, qup_l6, dbg_out, atest_usb22, NA,
			NA, NA, NA, NA),
	[35] = PINGROUP(35, SOUTH, pci_e0, qup_l4, NA, NA, NA, NA, NA, NA, NA),
	[36] = PINGROUP(36, SOUTH, pci_e0, qup_l5, NA, NA, NA, NA, NA, NA, NA),
	[37] = PINGROUP(37, SOUTH, qup_l6, agera_pll, NA, NA, NA, NA, NA, NA,
			NA),
	[38] = PINGROUP(38, SOUTH, usb0_phy, NA, NA, NA, NA, NA, NA, NA, NA),
	[39] = PINGROUP(39, EAST, qup9, cci_i2c, qdss_gpio6, NA, NA, NA, NA,
			NA, NA),
	[40] = PINGROUP(40, EAST, qup9, cci_i2c, qdss_gpio7, NA, NA, NA, NA,
			NA, NA),
	[41] = PINGROUP(41, EAST, qup9, cci_i2c, qdss_gpio14, NA, NA, NA, NA,
			NA, NA),
	[42] = PINGROUP(42, EAST, qup9, cci_i2c, qdss_gpio15, NA, NA, NA, NA,
			NA, NA),
	[43] = PINGROUP(43, EAST, qup13, NA, NA, NA, NA, NA, NA, NA, NA),
	[44] = PINGROUP(44, EAST, qup13, NA, NA, NA, NA, NA, NA, NA, NA),
	[45] = PINGROUP(45, EAST, qup13, NA, NA, NA, NA, NA, NA, NA, NA),
	[46] = PINGROUP(46, EAST, qup13, NA, NA, NA, NA, NA, NA, NA, NA),
	[47] = PINGROUP(47, EAST, qup14, usb2phy_ac, NA, NA, NA, NA, NA, NA,
			NA),
	[48] = PINGROUP(48, EAST, qup14, usb2phy_ac, NA, NA, NA, NA, NA, NA,
			NA),
	[49] = PINGROUP(49, EAST, qup14, qdss_cti, NA, NA, NA, NA, NA, NA, NA),
	[50] = PINGROUP(50, EAST, qup14, qdss_cti, NA, NA, NA, NA, NA, NA, NA),
	[51] = PINGROUP(51, WEST, qup4, NA, NA, NA, NA, NA, NA, NA, NA),
	[52] = PINGROUP(52, WEST, qup4, NA, NA, NA, NA, NA, NA, NA, NA),
	[53] = PINGROUP(53, WEST, qup4, NA, phase_flag29, NA, NA, NA, NA, NA,
			NA),
	[54] = PINGROUP(54, WEST, qup4, NA, NA, phase_flag28, NA, NA, NA, NA,
			NA),
	[55] = PINGROUP(55, WEST, qup17, qspi1_cs, NA, NA, NA, NA, NA, NA, NA),
	[56] = PINGROUP(56, WEST, qup17, qspi13, NA, NA, NA, NA, NA, NA, NA),
	[57] = PINGROUP(57, WEST, qup17, qspi12, NA, NA, NA, NA, NA, NA, NA),
	[58] = PINGROUP(58, WEST, usb1_phy, qup17, NA, NA, NA, NA, NA, NA, NA),
	[59] = PINGROUP(59, WEST, rgmii_mdio, qup_l4, NA, NA, NA, NA, NA, NA,
			NA),
	[60] = PINGROUP(60, EAST, GPS_TX, NAV_PPS, NAV_PPS, qup_l4, mdp_vsync,
			atest_usb1, ddr_pxi0, NA, NA),
	[61] = PINGROUP(61, EAST, QLINK_REQUEST, NA, NA, NA, NA, NA, NA, NA,
			NA),
	[62] = PINGROUP(62, EAST, QLINK_ENABLE, atest_tsens2, atest_usb13,
			ddr_pxi0, vsense_trigger, NA, NA, NA, NA),
	[63] = PINGROUP(63, EAST, wmss_reset, NA, atest_usb12, ddr_pxi1,
			wlan1_adc1, NA, NA, NA, NA),
	[64] = PINGROUP(64, EAST, GRFC8, NA, atest_usb11, ddr_pxi1, wlan1_adc0,
			NA, NA, NA, NA),
	[65] = PINGROUP(65, EAST, GRFC9, atest_usb10, ddr_pxi2, wlan2_adc1, NA,
			NA, NA, NA, NA),
	[66] = PINGROUP(66, EAST, GRFC10, NA, NA, NA, NA, NA, NA, NA, NA),
	[67] = PINGROUP(67, EAST, GRFC11, NA, NA, NA, NA, NA, NA, NA, NA),
	[68] = PINGROUP(68, EAST, GRFC12, pa_indicator, atest_usb3, ddr_pxi2,
			wlan2_adc0, NA, NA, NA, NA),
	[69] = PINGROUP(69, EAST, mss_lte, NA, NA, NA, NA, NA, NA, NA, NA),
	[70] = PINGROUP(70, EAST, mss_lte, NA, NA, NA, NA, NA, NA, NA, NA),
	[71] = PINGROUP(71, EAST, NA, GRFC0, atest_usb33, ddr_pxi3, NA, NA, NA,
			NA, NA),
	[72] = PINGROUP(72, EAST, NA, GRFC1, atest_usb32, ddr_pxi3, NA, NA, NA,
			NA, NA),
	[73] = PINGROUP(73, EAST, NA, GRFC2, atest_usb31, ddr_pxi4, NA, NA, NA,
			NA, NA),
	[74] = PINGROUP(74, EAST, NA, GRFC3, tgu_ch4, atest_usb30, ddr_pxi4,
			NA, NA, NA, NA),
	[75] = PINGROUP(75, EAST, NA, GRFC4, tgu_ch7, atest_usb4, ddr_pxi5, NA,
			NA, NA, NA),
	[76] = PINGROUP(76, EAST, NA, GRFC5, GPS_TX, NAV_PPS, NAV_PPS, tgu_ch6,
			atest_usb43, ddr_pxi5, NA),
	[77] = PINGROUP(77, EAST, NA, GRFC6, GPS_TX, NAV_PPS, NAV_PPS, tgu_ch5,
			atest_usb42, ddr_pxi6, NA),
	[78] = PINGROUP(78, EAST, NA, GRFC7, NA, atest_usb41, ddr_pxi6, NA, NA,
			NA, NA),
	[79] = PINGROUP(79, EAST, NA, GRFC13, NA, NA, NA, NA, NA, NA, NA),
	[80] = PINGROUP(80, EAST, NA, GRFC14, NA, NA, NA, NA, NA, NA, NA),
	[81] = PINGROUP(81, EAST, NA, GRFC15, GPS_TX, NAV_PPS, NAV_PPS,
			emac_pps, qdss_cti, NA, NA),
	[82] = PINGROUP(82, EAST, NA, GRFC16, GPS_TX, NAV_PPS, NAV_PPS,
			mdp_vsync, qdss_cti, NA, NA),
	[83] = PINGROUP(83, EAST, qup12, qup16, NA, NA, NA, NA, NA, NA, NA),
	[84] = PINGROUP(84, EAST, qup12, qup16, NA, NA, NA, NA, NA, NA, NA),
	[85] = PINGROUP(85, EAST, qup12, qup16, NA, NA, NA, NA, NA, NA, NA),
	[86] = PINGROUP(86, EAST, qup12, qup16, NA, NA, NA, NA, NA, NA, NA),
	[87] = PINGROUP(87, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[88] = PINGROUP(88, NORTH, tsif1_clk, qup8, qspi0_cs, tgu_ch3,
			atest_usb40, ddr_pxi7, NA, NA, NA),
	[89] = PINGROUP(89, NORTH, tsif1_en, qup8, qspi00, mdp_vsync0,
			mdp_vsync1, mdp_vsync2, mdp_vsync3, mdp_vsync4,
			mdp_vsync5),
	[90] = PINGROUP(90, NORTH, tsif1_data, qup8, qspi01, sdc4_cmd, tgu_ch1,
			qdss_cti, atest_usb0, ddr_pxi7, NA),
	[91] = PINGROUP(91, NORTH, tsif1_sync, qup8, qspi02, sdc43, vfr_1,
			tgu_ch2, atest_usb03, NA, NA),
	[92] = PINGROUP(92, NORTH, tsif2_clk, qup11, qspi0_clk, sdc4_clk,
			qdss_gpio13, atest_usb02, NA, NA, NA),
	[93] = PINGROUP(93, NORTH, tsif2_en, qup11, qspi03, sdc42, atest_tsens,
			atest_usb01, NA, NA, NA),
	[94] = PINGROUP(94, NORTH, tsif2_data, qup11, qspi0_cs, sdc41, NA,
			atest_usb00, NA, NA, NA),
	[95] = PINGROUP(95, EAST, tsif2_sync, qup11, sdc40, qup_l4, atest_usb2,
			NA, NA, NA, NA),
	[96] = PINGROUP(96, WEST, tsif2_error, qup_l5, NA, NA, NA, NA, NA, NA,
			NA),
	[97] = PINGROUP(97, WEST, sd_write, tsif1_error, qup_l6, NA, NA, NA,
			NA, NA, NA),
	[98] = PINGROUP(98, WEST, qup7, ddr_bist, NA, NA, NA, NA, NA, NA, NA),
	[99] = PINGROUP(99, WEST, qup7, ddr_bist, NA, NA, NA, NA, NA, NA, NA),
	[100] = PINGROUP(100, WEST, qup7, pll_bypassnl, NA, NA, NA, NA, NA, NA,
			 NA),
	[101] = PINGROUP(101, WEST, qup7, pll_reset, NA, NA, NA, NA, NA, NA,
			 NA),
	[102] = PINGROUP(102, SOUTH, pci_e1, NA, phase_flag4, atest_usb23, NA,
			 NA, NA, NA, NA),
	[103] = PINGROUP(103, SOUTH, pci_e1, NA, NA, NA, NA, NA, NA, NA, NA),
	[104] = PINGROUP(104, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[105] = PINGROUP(105, WEST, uim2_data, NA, NA, NA, NA, NA, NA, NA, NA),
	[106] = PINGROUP(106, WEST, uim2_clk, NA, NA, NA, NA, NA, NA, NA, NA),
	[107] = PINGROUP(107, WEST, uim2_reset, NA, NA, NA, NA, NA, NA, NA, NA),
	[108] = PINGROUP(108, WEST, uim2_present, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[109] = PINGROUP(109, WEST, uim1_data, NA, NA, NA, NA, NA, NA, NA, NA),
	[110] = PINGROUP(110, WEST, uim1_clk, NA, NA, NA, NA, NA, NA, NA, NA),
	[111] = PINGROUP(111, WEST, uim1_reset, NA, NA, NA, NA, NA, NA, NA, NA),
	[112] = PINGROUP(112, WEST, uim1_present, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[113] = PINGROUP(113, WEST, uim_batt, usb2phy_ac, aoss_cti, NA, NA, NA,
			 NA, NA, NA),
	[114] = PINGROUP(114, WEST, qup1, rgmii_txc, NA, qdss_gpio0, NA, NA,
			 NA, NA, NA),
	[115] = PINGROUP(115, WEST, qup1, rgmii_rxc, adsp_ext, NA, qdss_gpio1,
			 NA, NA, NA, NA),
	[116] = PINGROUP(116, WEST, qup1, rgmii_rx, NA, qdss_gpio2, NA, NA, NA,
			 NA, NA),
	[117] = PINGROUP(117, WEST, qup1, rgmii_rxd0, NA, qdss_gpio3, NA, NA,
			 NA, NA, NA),
	[118] = PINGROUP(118, WEST, rgmii_rxd1, NA, qdss_gpio4, NA, NA, NA, NA,
			 NA, NA),
	[119] = PINGROUP(119, WEST, qup5, rgmii_rxd2, NA, qdss_gpio5, NA, NA,
			 NA, NA, NA),
	[120] = PINGROUP(120, WEST, qup5, rgmii_rxd3, NA, phase_flag17,
			 qdss_gpio, NA, NA, NA, NA),
	[121] = PINGROUP(121, WEST, qup5, rgmii_tx, NA, phase_flag16,
			 qdss_gpio, atest_usb21, NA, NA, NA),
	[122] = PINGROUP(122, WEST, qup5, rgmii_txd0, NA, phase_flag15,
			 atest_usb20, NA, NA, NA, NA),
	[123] = PINGROUP(123, SOUTH, usb2phy_ac, NA, phase_flag14, NA, NA, NA,
			 NA, NA, NA),
	[124] = PINGROUP(124, SOUTH, emac_phy, NA, NA, NA, NA, NA, NA, NA, NA),
	[125] = PINGROUP(125, WEST, hs3_mi2s, NA, phase_flag22, NA, NA, NA, NA,
			 NA, NA),
	[126] = PINGROUP(126, WEST, sec_mi2s, qup2, NA, NA, NA, NA, NA, NA, NA),
	[127] = PINGROUP(127, WEST, sec_mi2s, qup2, NA, NA, NA, NA, NA, NA, NA),
	[128] = PINGROUP(128, WEST, sec_mi2s, qup2, NA, NA, NA, NA, NA, NA, NA),
	[129] = PINGROUP(129, WEST, sec_mi2s, qup2, jitter_bist, NA, NA, NA,
			 NA, NA, NA),
	[130] = PINGROUP(130, WEST, sec_mi2s, pll_bist, NA, qdss_gpio12, NA,
			 NA, NA, NA, NA),
	[131] = PINGROUP(131, WEST, ter_mi2s, gcc_gp1, NA, NA, NA, NA, NA, NA,
			 NA),
	[132] = PINGROUP(132, WEST, ter_mi2s, NA, qdss_gpio11, NA, NA, NA, NA,
			 NA, NA),
	[133] = PINGROUP(133, WEST, ter_mi2s, NA, qdss_gpio8, atest_char, NA,
			 NA, NA, NA, NA),
	[134] = PINGROUP(134, WEST, ter_mi2s, NA, qdss_gpio9, atest_char0, NA,
			 NA, NA, NA, NA),
	[135] = PINGROUP(135, WEST, ter_mi2s, NA, qdss_gpio10, atest_char1, NA,
			 NA, NA, NA, NA),
	[136] = PINGROUP(136, WEST, qua_mi2s, gcc_gp1, NA, NA, NA, NA, NA, NA,
			 NA),
	[137] = PINGROUP(137, WEST, qua_mi2s, gcc_gp2, NA, NA, NA, NA, NA, NA,
			 NA),
	[138] = PINGROUP(138, WEST, qua_mi2s, gcc_gp3, NA, NA, NA, NA, NA, NA,
			 NA),
	[139] = PINGROUP(139, WEST, qua_mi2s, gcc_gp4, NA, NA, NA, NA, NA, NA,
			 NA),
	[140] = PINGROUP(140, WEST, qua_mi2s, gcc_gp5, NA, atest_char2, NA, NA,
			 NA, NA, NA),
	[141] = PINGROUP(141, WEST, qua_mi2s, qdss_cti, NA, NA, NA, NA, NA, NA,
			 NA),
	[142] = PINGROUP(142, WEST, qua_mi2s, NA, NA, qdss_cti, atest_char3,
			 NA, NA, NA, NA),
	[143] = PINGROUP(143, WEST, pri_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	[144] = PINGROUP(144, WEST, pri_mi2s, qup3, NA, NA, NA, NA, NA, NA, NA),
	[145] = PINGROUP(145, WEST, pri_mi2s_ws, qup3, ddr_bist, NA, NA, NA,
			 NA, NA, NA),
	[146] = PINGROUP(146, WEST, pri_mi2s, qup3, ddr_bist, NA, NA, NA, NA,
			 NA, NA),
	[147] = PINGROUP(147, WEST, pri_mi2s, qup3, NA, NA, NA, NA, NA, NA, NA),
	[148] = PINGROUP(148, WEST, spkr_i2s, audio_ref, NA, phase_flag21, NA,
			 NA, NA, NA, NA),
	[149] = PINGROUP(149, WEST, lpass_slimbus, spkr_i2s, NA, phase_flag20,
			 NA, NA, NA, NA, NA),
	[150] = PINGROUP(150, WEST, lpass_slimbus, spkr_i2s, NA, phase_flag19,
			 tsense_pwm1, tsense_pwm2, NA, NA, NA),
	[151] = PINGROUP(151, WEST, lpass_slimbus, spkr_i2s, NA, phase_flag18,
			 NA, NA, NA, NA, NA),
	[152] = PINGROUP(152, WEST, lpass_slimbus, spkr_i2s, NA, phase_flag13,
			 NA, NA, NA, NA, NA),
	[153] = PINGROUP(153, WEST, btfm_slimbus, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[154] = PINGROUP(154, WEST, btfm_slimbus, NA, NA, NA, NA, NA, NA, NA,
			 NA),
	[155] = PINGROUP(155, WEST, hs1_mi2s, NA, phase_flag12, NA, NA, NA, NA,
			 NA, egpio),
	[156] = PINGROUP(156, WEST, hs1_mi2s, NA, phase_flag11, NA, NA, NA, NA,
			 NA, egpio),
	[157] = PINGROUP(157, WEST, hs1_mi2s, NA, phase_flag10, NA, NA, NA, NA,
			 NA, egpio),
	[158] = PINGROUP(158, WEST, hs1_mi2s, NA, phase_flag9, NA, NA, NA, NA,
			 NA, egpio),
	[159] = PINGROUP(159, WEST, hs1_mi2s, cri_trng0, NA, phase_flag8, NA,
			 NA, NA, NA, egpio),
	[160] = PINGROUP(160, WEST, hs2_mi2s, cri_trng1, NA, phase_flag7, NA,
			 NA, NA, NA, egpio),
	[161] = PINGROUP(161, WEST, hs2_mi2s, qspi11, cri_trng, NA,
			 phase_flag24, NA, NA, NA, egpio),
	[162] = PINGROUP(162, WEST, hs2_mi2s, qspi10, sp_cmu, NA, phase_flag6,
			 NA, NA, NA, egpio),
	[163] = PINGROUP(163, WEST, hs2_mi2s, qspi1_clk, prng_rosc, NA,
			 phase_flag5, NA, NA, NA, egpio),
	[164] = PINGROUP(164, WEST, hs2_mi2s, qspi1_cs, NA, phase_flag31, NA,
			 NA, NA, NA, egpio),
	[165] = PINGROUP(165, WEST, hs3_mi2s, NA, phase_flag30, NA, NA, NA, NA,
			 NA, egpio),
	[166] = PINGROUP(166, WEST, hs3_mi2s, NA, phase_flag27, NA, NA, NA, NA,
			 NA, egpio),
	[167] = PINGROUP(167, WEST, hs3_mi2s, NA, phase_flag26, NA, NA, NA, NA,
			 NA, egpio),
	[168] = PINGROUP(168, WEST, hs3_mi2s, NA, phase_flag25, NA, NA, NA, NA,
			 NA, egpio),
	[169] = PINGROUP(169, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, egpio),
	[170] = PINGROUP(170, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[171] = PINGROUP(171, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[172] = PINGROUP(172, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, egpio),
	[173] = PINGROUP(173, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, egpio),
	[174] = PINGROUP(174, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, egpio),
	[175] = PINGROUP(175, SOUTH, pci_e2, NA, NA, NA, NA, NA, NA, NA, NA),
	[176] = PINGROUP(176, SOUTH, pci_e2, cci_async, NA, NA, NA, NA, NA, NA,
			 NA),
	[177] = PINGROUP(177, SOUTH1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[178] = PINGROUP(178, SOUTH1, pci_e3, cci_timer4, NA, NA, NA, NA, NA,
			 NA, NA),
	[179] = PINGROUP(179, SOUTH1, pci_e3, cam_mclk, NA, NA, NA, NA, NA, NA,
			 NA),
	[180] = PINGROUP(180, SOUTH1, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA),
	[181] = PINGROUP(181, SOUTH1, qup19, cam_mclk, NA, NA, NA, NA, NA, NA,
			 NA),
	[182] = PINGROUP(182, SOUTH1, qup19, cci_timer5, gcc_gp4, NA, NA, NA,
			 NA, NA, NA),
	[183] = PINGROUP(183, SOUTH1, qup19, cci_timer6, gcc_gp5, NA, NA, NA,
			 NA, NA, NA),
	[184] = PINGROUP(184, SOUTH1, qup19, cci_timer7, NA, NA, NA, NA, NA, NA,
			 NA),
	[185] = PINGROUP(185, SOUTH1, cci_timer8, cci_async, NA, NA, NA, NA, NA,
			 NA, NA),
	[186] = PINGROUP(186, SOUTH1, cci_timer9, cci_async, NA, NA, NA, NA, NA,
			 NA, NA),
	[187] = PINGROUP(187, SOUTH1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[188] = PINGROUP(188, SOUTH1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[189] = PINGROUP(189, SOUTH1, dp_hot, NA, NA, NA, NA, NA, NA, NA, NA),
	[190] = UFS_RESET(ufs_reset, 0xdb6000),
	[191] = UFS_RESET(ufs0_reset, 0xdc7000),
	[192] = SDC_QDSD_PINGROUP(sdc2_clk, 0x9b2000, 14, 6),
	[193] = SDC_QDSD_PINGROUP(sdc2_cmd, 0x9b2000, 11, 3),
	[194] = SDC_QDSD_PINGROUP(sdc2_data, 0x9b2000, 9, 0),
};

static const struct msm_gpio_wakeirq_map sdmshrike_pdc_map[] = {
	{ 3, 31 }, { 5, 32 }, { 8, 33 }, { 9, 34 }, { 10, 100 },
	{ 12, 104 }, { 24, 37 }, { 26, 38 }, { 27, 41 }, { 28, 42 },
	{ 30, 39 }, { 36, 43 }, { 37, 44 }, { 38, 45 }, { 39, 118 },
	{ 41, 47 }, { 42, 48 }, { 46, 50 }, { 47, 49 }, { 48, 51 },
	{ 49, 53 }, { 50, 52 }, { 51, 116 }, { 53, 54 }, { 54, 55 },
	{ 55, 56 }, { 56, 57 }, { 58, 58 }, { 60, 60 }, { 61, 61 },
	{ 68, 62 }, { 70, 63 }, { 76, 86 }, { 77, 66 }, { 81, 64 },
	{ 83, 65 }, { 86, 67 }, { 87, 84 }, { 88, 117 }, { 90, 69 },
	{ 91, 70 }, {93, 75 }, { 95, 72 }, { 96, 73 }, { 97, 74 },
	{ 101, 76 }, { 103, 77 }, { 104, 78 }, { 108, 79 }, { 112, 80 },
	{ 113, 81 }, { 114, 82 }, { 117, 85 }, { 118, 101 }, { 119, 87 },
	{ 120, 88 }, { 121, 89 }, { 122, 90 }, { 123, 91 }, { 124, 92 },
	{ 125, 93 }, { 129, 94 }, { 132, 105 }, { 133, 35 }, { 134, 36 },
	{ 136, 97 }, { 142, 103 }, { 144, 115 }, { 147, 106 }, { 150, 107 },
	{ 152, 108 }, { 153, 109 }, { 177, 111 }, { 180, 112 }, { 184, 113 },
	{ 189, 114 },
};

static struct msm_dir_conn sdmshrike_dir_conn[] = {
	{-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0},
	{-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}
};

static struct msm_pinctrl_soc_data sdmshrike_pinctrl = {
	.pins = sdmshrike_pins,
	.npins = ARRAY_SIZE(sdmshrike_pins),
	.functions = sdmshrike_functions,
	.nfunctions = ARRAY_SIZE(sdmshrike_functions),
	.groups = sdmshrike_groups,
	.ngroups = ARRAY_SIZE(sdmshrike_groups),
	.ngpios = 192,
	.wakeirq_map = sdmshrike_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(sdmshrike_pdc_map),
	.wakeirq_dual_edge_errata = true,
	.dir_conn = sdmshrike_dir_conn,
	.egpio_func = 9,
};

static int sdmshrike_pinctrl_dirconn_list_probe(struct platform_device *pdev)
{
	int ret, n, dirconn_list_count, m;
	struct device_node *np = pdev->dev.of_node;

	n = of_property_count_elems_of_size(np, "qcom,dirconn-list",
					sizeof(u32));
	if (n <= 0 || n % 2)
		return -EINVAL;

	m = ARRAY_SIZE(sdmshrike_dir_conn) - 1;

	dirconn_list_count = n / 2;

	for (n = 0; n < dirconn_list_count; n++) {
		ret = of_property_read_u32_index(np, "qcom,dirconn-list",
						n * 2 + 0,
						&sdmshrike_dir_conn[m].gpio);
		if (ret)
			return ret;
		ret = of_property_read_u32_index(np, "qcom,dirconn-list",
						n * 2 + 1,
						&sdmshrike_dir_conn[m].irq);
		if (ret)
			return ret;
		m--;
	}

	return 0;
}

static int sdmshrike_pinctrl_probe(struct platform_device *pdev)
{
	int len, ret;

	if (of_find_property(pdev->dev.of_node, "qcom,dirconn-list", &len)) {
		ret = sdmshrike_pinctrl_dirconn_list_probe(pdev);
		if (ret) {
			dev_err(&pdev->dev,
					"Unable to parse Direct Connect List\n");
			return ret;
		}
	}

	return msm_pinctrl_probe(pdev, &sdmshrike_pinctrl);
}

static const struct of_device_id sdmshrike_pinctrl_of_match[] = {
	{ .compatible = "qcom,sdmshrike-pinctrl", },
	{ },
};

static struct platform_driver sdmshrike_pinctrl_driver = {
	.driver = {
		.name = "sdmshrike-pinctrl",
		.of_match_table = sdmshrike_pinctrl_of_match,
	},
	.probe = sdmshrike_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init sdmshrike_pinctrl_init(void)
{
	return platform_driver_register(&sdmshrike_pinctrl_driver);
}
arch_initcall(sdmshrike_pinctrl_init);

static void __exit sdmshrike_pinctrl_exit(void)
{
	platform_driver_unregister(&sdmshrike_pinctrl_driver);
}
module_exit(sdmshrike_pinctrl_exit);

MODULE_DESCRIPTION("QTI sdmshrike pinctrl driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, sdmshrike_pinctrl_of_match);
