/*
 * Copyright (c) 2015, Sony Mobile Communications AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

static const struct pinctrl_pin_desc msm8660_pins[] = {
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

	PINCTRL_PIN(173, "SDC4_CLK"),
	PINCTRL_PIN(174, "SDC4_CMD"),
	PINCTRL_PIN(175, "SDC4_DATA"),
	PINCTRL_PIN(176, "SDC3_CLK"),
	PINCTRL_PIN(177, "SDC3_CMD"),
	PINCTRL_PIN(178, "SDC3_DATA"),
};

#define DECLARE_MSM_GPIO_PIN(pin) static const unsigned int gpio##pin##_pins[] = { pin }
DECLARE_MSM_GPIO_PIN(0);
DECLARE_MSM_GPIO_PIN(1);
DECLARE_MSM_GPIO_PIN(2);
DECLARE_MSM_GPIO_PIN(3);
DECLARE_MSM_GPIO_PIN(4);
DECLARE_MSM_GPIO_PIN(5);
DECLARE_MSM_GPIO_PIN(6);
DECLARE_MSM_GPIO_PIN(7);
DECLARE_MSM_GPIO_PIN(8);
DECLARE_MSM_GPIO_PIN(9);
DECLARE_MSM_GPIO_PIN(10);
DECLARE_MSM_GPIO_PIN(11);
DECLARE_MSM_GPIO_PIN(12);
DECLARE_MSM_GPIO_PIN(13);
DECLARE_MSM_GPIO_PIN(14);
DECLARE_MSM_GPIO_PIN(15);
DECLARE_MSM_GPIO_PIN(16);
DECLARE_MSM_GPIO_PIN(17);
DECLARE_MSM_GPIO_PIN(18);
DECLARE_MSM_GPIO_PIN(19);
DECLARE_MSM_GPIO_PIN(20);
DECLARE_MSM_GPIO_PIN(21);
DECLARE_MSM_GPIO_PIN(22);
DECLARE_MSM_GPIO_PIN(23);
DECLARE_MSM_GPIO_PIN(24);
DECLARE_MSM_GPIO_PIN(25);
DECLARE_MSM_GPIO_PIN(26);
DECLARE_MSM_GPIO_PIN(27);
DECLARE_MSM_GPIO_PIN(28);
DECLARE_MSM_GPIO_PIN(29);
DECLARE_MSM_GPIO_PIN(30);
DECLARE_MSM_GPIO_PIN(31);
DECLARE_MSM_GPIO_PIN(32);
DECLARE_MSM_GPIO_PIN(33);
DECLARE_MSM_GPIO_PIN(34);
DECLARE_MSM_GPIO_PIN(35);
DECLARE_MSM_GPIO_PIN(36);
DECLARE_MSM_GPIO_PIN(37);
DECLARE_MSM_GPIO_PIN(38);
DECLARE_MSM_GPIO_PIN(39);
DECLARE_MSM_GPIO_PIN(40);
DECLARE_MSM_GPIO_PIN(41);
DECLARE_MSM_GPIO_PIN(42);
DECLARE_MSM_GPIO_PIN(43);
DECLARE_MSM_GPIO_PIN(44);
DECLARE_MSM_GPIO_PIN(45);
DECLARE_MSM_GPIO_PIN(46);
DECLARE_MSM_GPIO_PIN(47);
DECLARE_MSM_GPIO_PIN(48);
DECLARE_MSM_GPIO_PIN(49);
DECLARE_MSM_GPIO_PIN(50);
DECLARE_MSM_GPIO_PIN(51);
DECLARE_MSM_GPIO_PIN(52);
DECLARE_MSM_GPIO_PIN(53);
DECLARE_MSM_GPIO_PIN(54);
DECLARE_MSM_GPIO_PIN(55);
DECLARE_MSM_GPIO_PIN(56);
DECLARE_MSM_GPIO_PIN(57);
DECLARE_MSM_GPIO_PIN(58);
DECLARE_MSM_GPIO_PIN(59);
DECLARE_MSM_GPIO_PIN(60);
DECLARE_MSM_GPIO_PIN(61);
DECLARE_MSM_GPIO_PIN(62);
DECLARE_MSM_GPIO_PIN(63);
DECLARE_MSM_GPIO_PIN(64);
DECLARE_MSM_GPIO_PIN(65);
DECLARE_MSM_GPIO_PIN(66);
DECLARE_MSM_GPIO_PIN(67);
DECLARE_MSM_GPIO_PIN(68);
DECLARE_MSM_GPIO_PIN(69);
DECLARE_MSM_GPIO_PIN(70);
DECLARE_MSM_GPIO_PIN(71);
DECLARE_MSM_GPIO_PIN(72);
DECLARE_MSM_GPIO_PIN(73);
DECLARE_MSM_GPIO_PIN(74);
DECLARE_MSM_GPIO_PIN(75);
DECLARE_MSM_GPIO_PIN(76);
DECLARE_MSM_GPIO_PIN(77);
DECLARE_MSM_GPIO_PIN(78);
DECLARE_MSM_GPIO_PIN(79);
DECLARE_MSM_GPIO_PIN(80);
DECLARE_MSM_GPIO_PIN(81);
DECLARE_MSM_GPIO_PIN(82);
DECLARE_MSM_GPIO_PIN(83);
DECLARE_MSM_GPIO_PIN(84);
DECLARE_MSM_GPIO_PIN(85);
DECLARE_MSM_GPIO_PIN(86);
DECLARE_MSM_GPIO_PIN(87);
DECLARE_MSM_GPIO_PIN(88);
DECLARE_MSM_GPIO_PIN(89);
DECLARE_MSM_GPIO_PIN(90);
DECLARE_MSM_GPIO_PIN(91);
DECLARE_MSM_GPIO_PIN(92);
DECLARE_MSM_GPIO_PIN(93);
DECLARE_MSM_GPIO_PIN(94);
DECLARE_MSM_GPIO_PIN(95);
DECLARE_MSM_GPIO_PIN(96);
DECLARE_MSM_GPIO_PIN(97);
DECLARE_MSM_GPIO_PIN(98);
DECLARE_MSM_GPIO_PIN(99);
DECLARE_MSM_GPIO_PIN(100);
DECLARE_MSM_GPIO_PIN(101);
DECLARE_MSM_GPIO_PIN(102);
DECLARE_MSM_GPIO_PIN(103);
DECLARE_MSM_GPIO_PIN(104);
DECLARE_MSM_GPIO_PIN(105);
DECLARE_MSM_GPIO_PIN(106);
DECLARE_MSM_GPIO_PIN(107);
DECLARE_MSM_GPIO_PIN(108);
DECLARE_MSM_GPIO_PIN(109);
DECLARE_MSM_GPIO_PIN(110);
DECLARE_MSM_GPIO_PIN(111);
DECLARE_MSM_GPIO_PIN(112);
DECLARE_MSM_GPIO_PIN(113);
DECLARE_MSM_GPIO_PIN(114);
DECLARE_MSM_GPIO_PIN(115);
DECLARE_MSM_GPIO_PIN(116);
DECLARE_MSM_GPIO_PIN(117);
DECLARE_MSM_GPIO_PIN(118);
DECLARE_MSM_GPIO_PIN(119);
DECLARE_MSM_GPIO_PIN(120);
DECLARE_MSM_GPIO_PIN(121);
DECLARE_MSM_GPIO_PIN(122);
DECLARE_MSM_GPIO_PIN(123);
DECLARE_MSM_GPIO_PIN(124);
DECLARE_MSM_GPIO_PIN(125);
DECLARE_MSM_GPIO_PIN(126);
DECLARE_MSM_GPIO_PIN(127);
DECLARE_MSM_GPIO_PIN(128);
DECLARE_MSM_GPIO_PIN(129);
DECLARE_MSM_GPIO_PIN(130);
DECLARE_MSM_GPIO_PIN(131);
DECLARE_MSM_GPIO_PIN(132);
DECLARE_MSM_GPIO_PIN(133);
DECLARE_MSM_GPIO_PIN(134);
DECLARE_MSM_GPIO_PIN(135);
DECLARE_MSM_GPIO_PIN(136);
DECLARE_MSM_GPIO_PIN(137);
DECLARE_MSM_GPIO_PIN(138);
DECLARE_MSM_GPIO_PIN(139);
DECLARE_MSM_GPIO_PIN(140);
DECLARE_MSM_GPIO_PIN(141);
DECLARE_MSM_GPIO_PIN(142);
DECLARE_MSM_GPIO_PIN(143);
DECLARE_MSM_GPIO_PIN(144);
DECLARE_MSM_GPIO_PIN(145);
DECLARE_MSM_GPIO_PIN(146);
DECLARE_MSM_GPIO_PIN(147);
DECLARE_MSM_GPIO_PIN(148);
DECLARE_MSM_GPIO_PIN(149);
DECLARE_MSM_GPIO_PIN(150);
DECLARE_MSM_GPIO_PIN(151);
DECLARE_MSM_GPIO_PIN(152);
DECLARE_MSM_GPIO_PIN(153);
DECLARE_MSM_GPIO_PIN(154);
DECLARE_MSM_GPIO_PIN(155);
DECLARE_MSM_GPIO_PIN(156);
DECLARE_MSM_GPIO_PIN(157);
DECLARE_MSM_GPIO_PIN(158);
DECLARE_MSM_GPIO_PIN(159);
DECLARE_MSM_GPIO_PIN(160);
DECLARE_MSM_GPIO_PIN(161);
DECLARE_MSM_GPIO_PIN(162);
DECLARE_MSM_GPIO_PIN(163);
DECLARE_MSM_GPIO_PIN(164);
DECLARE_MSM_GPIO_PIN(165);
DECLARE_MSM_GPIO_PIN(166);
DECLARE_MSM_GPIO_PIN(167);
DECLARE_MSM_GPIO_PIN(168);
DECLARE_MSM_GPIO_PIN(169);
DECLARE_MSM_GPIO_PIN(170);
DECLARE_MSM_GPIO_PIN(171);
DECLARE_MSM_GPIO_PIN(172);

static const unsigned int sdc4_clk_pins[] = { 173 };
static const unsigned int sdc4_cmd_pins[] = { 174 };
static const unsigned int sdc4_data_pins[] = { 175 };
static const unsigned int sdc3_clk_pins[] = { 176 };
static const unsigned int sdc3_cmd_pins[] = { 177 };
static const unsigned int sdc3_data_pins[] = { 178 };

#define FUNCTION(fname)					\
	[MSM_MUX_##fname] = {				\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7) \
	{						\
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = ARRAY_SIZE(gpio##id##_pins),	\
		.funcs = (int[]){			\
			MSM_MUX_gpio,			\
			MSM_MUX_##f1,			\
			MSM_MUX_##f2,			\
			MSM_MUX_##f3,			\
			MSM_MUX_##f4,			\
			MSM_MUX_##f5,			\
			MSM_MUX_##f6,			\
			MSM_MUX_##f7,			\
		},					\
		.nfuncs = 8,				\
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

enum msm8660_functions {
	MSM_MUX_gpio,
	MSM_MUX_cam_mclk,
	MSM_MUX_dsub,
	MSM_MUX_ext_gps,
	MSM_MUX_gp_clk_0a,
	MSM_MUX_gp_clk_0b,
	MSM_MUX_gp_clk_1a,
	MSM_MUX_gp_clk_1b,
	MSM_MUX_gp_clk_2a,
	MSM_MUX_gp_clk_2b,
	MSM_MUX_gp_mn,
	MSM_MUX_gsbi1,
	MSM_MUX_gsbi1_spi_cs1_n,
	MSM_MUX_gsbi1_spi_cs2a_n,
	MSM_MUX_gsbi1_spi_cs2b_n,
	MSM_MUX_gsbi1_spi_cs3_n,
	MSM_MUX_gsbi2,
	MSM_MUX_gsbi2_spi_cs1_n,
	MSM_MUX_gsbi2_spi_cs2_n,
	MSM_MUX_gsbi2_spi_cs3_n,
	MSM_MUX_gsbi3,
	MSM_MUX_gsbi3_spi_cs1_n,
	MSM_MUX_gsbi3_spi_cs2_n,
	MSM_MUX_gsbi3_spi_cs3_n,
	MSM_MUX_gsbi4,
	MSM_MUX_gsbi5,
	MSM_MUX_gsbi6,
	MSM_MUX_gsbi7,
	MSM_MUX_gsbi8,
	MSM_MUX_gsbi9,
	MSM_MUX_gsbi10,
	MSM_MUX_gsbi11,
	MSM_MUX_gsbi12,
	MSM_MUX_hdmi,
	MSM_MUX_i2s,
	MSM_MUX_lcdc,
	MSM_MUX_mdp_vsync,
	MSM_MUX_mi2s,
	MSM_MUX_pcm,
	MSM_MUX_ps_hold,
	MSM_MUX_sdc1,
	MSM_MUX_sdc2,
	MSM_MUX_sdc5,
	MSM_MUX_tsif1,
	MSM_MUX_tsif2,
	MSM_MUX_usb_fs1,
	MSM_MUX_usb_fs1_oe_n,
	MSM_MUX_usb_fs2,
	MSM_MUX_usb_fs2_oe_n,
	MSM_MUX_vfe,
	MSM_MUX_vsens_alarm,
	MSM_MUX_ebi2cs,
	MSM_MUX_ebi2,
	MSM_MUX__,
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
	"gpio171", "gpio172"
};

static const char * const cam_mclk_groups[] = {
	"gpio32"
};
static const char * const dsub_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio13", "gpio14",
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21",
	"gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio27"
};
static const char * const ext_gps_groups[] = {
	"gpio66", "gpio67", "gpio68", "gpio69"
};
static const char * const gp_clk_0a_groups[] = {
	"gpio30"
};
static const char * const gp_clk_0b_groups[] = {
	"gpio115"
};
static const char * const gp_clk_1a_groups[] = {
	"gpio31"
};
static const char * const gp_clk_1b_groups[] = {
	"gpio122"
};
static const char * const gp_clk_2a_groups[] = {
	"gpio103"
};
static const char * const gp_clk_2b_groups[] = {
	"gpio70"
};
static const char * const gp_mn_groups[] = {
	"gpio29"
};
static const char * const gsbi1_groups[] = {
	"gpio33", "gpio34", "gpio35", "gpio36"
};
static const char * const gsbi1_spi_cs1_n_groups[] = {
};
static const char * const gsbi1_spi_cs2a_n_groups[] = {
};
static const char * const gsbi1_spi_cs2b_n_groups[] = {
};
static const char * const gsbi1_spi_cs3_n_groups[] = {
};
static const char * const gsbi2_groups[] = {
	"gpio37", "gpio38", "gpio39", "gpio40"
};
static const char * const gsbi2_spi_cs1_n_groups[] = {
	"gpio123"
};
static const char * const gsbi2_spi_cs2_n_groups[] = {
	"gpio124"
};
static const char * const gsbi2_spi_cs3_n_groups[] = {
	"gpio125"
};
static const char * const gsbi3_groups[] = {
	"gpio41", "gpio42", "gpio43", "gpio44"
};
static const char * const gsbi3_spi_cs1_n_groups[] = {
	"gpio62"
};
static const char * const gsbi3_spi_cs2_n_groups[] = {
	"gpio45"
};
static const char * const gsbi3_spi_cs3_n_groups[] = {
	"gpio46"
};
static const char * const gsbi4_groups[] = {
	"gpio45", "gpio56", "gpio47", "gpio48"
};
static const char * const gsbi5_groups[] = {
	"gpio49", "gpio50", "gpio51", "gpio52"
};
static const char * const gsbi6_groups[] = {
	"gpio53", "gpio54", "gpio55", "gpio56"
};
static const char * const gsbi7_groups[] = {
	"gpio57", "gpio58", "gpio59", "gpio60"
};
static const char * const gsbi8_groups[] = {
	"gpio62", "gpio63", "gpio64", "gpio65"
};
static const char * const gsbi9_groups[] = {
	"gpio66", "gpio67", "gpio68", "gpio69"
};
static const char * const gsbi10_groups[] = {
	"gpio70", "gpio71", "gpio72", "gpio73"
};
static const char * const gsbi11_groups[] = {
	"gpio103", "gpio104", "gpio105", "gpio106"
};
static const char * const gsbi12_groups[] = {
	"gpio115", "gpio116", "gpio117", "gpio118"
};
static const char * const hdmi_groups[] = {
	"gpio169", "gpio170", "gpio171", "gpio172"
};
static const char * const i2s_groups[] = {
	"gpio108", "gpio109", "gpio110", "gpio115", "gpio116", "gpio117",
	"gpio118", "gpio119", "gpio120", "gpio121", "gpio122"
};
static const char * const lcdc_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio13", "gpio14",
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21",
	"gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio27"
};
static const char * const mdp_vsync_groups[] = {
	"gpio28", "gpio39", "gpio41"
};
static const char * const mi2s_groups[] = {
	"gpio101", "gpio102", "gpio103", "gpio104", "gpio105", "gpio106",
	"gpio107"
};
static const char * const pcm_groups[] = {
	"gpio111", "gpio112", "gpio113", "gpio114"
};
static const char * const ps_hold_groups[] = {
	"gpio92"
};
static const char * const sdc1_groups[] = {
	"gpio159", "gpio160", "gpio161", "gpio162", "gpio163", "gpio164",
	"gpio165", "gpio166", "gpio167", "gpio168"
};
static const char * const sdc2_groups[] = {
	"gpio143", "gpio144", "gpio145", "gpio146", "gpio147", "gpio148",
	"gpio149", "gpio150", "gpio151", "gpio152"
};
static const char * const sdc5_groups[] = {
	"gpio95", "gpio96", "gpio97", "gpio98", "gpio99", "gpio100"
};
static const char * const tsif1_groups[] = {
	"gpio93", "gpio94", "gpio95", "gpio96"
};
static const char * const tsif2_groups[] = {
	"gpio97", "gpio98", "gpio99", "gpio100"
};
static const char * const usb_fs1_groups[] = {
	"gpio49", "gpio50", "gpio51"
};
static const char * const usb_fs1_oe_n_groups[] = {
	"gpio51"
};
static const char * const usb_fs2_groups[] = {
	"gpio71", "gpio72", "gpio73"
};
static const char * const usb_fs2_oe_n_groups[] = {
	"gpio73"
};
static const char * const vfe_groups[] = {
	"gpio29", "gpio30", "gpio31", "gpio42", "gpio46", "gpio105", "gpio106",
	"gpio117"
};
static const char * const vsens_alarm_groups[] = {
	"gpio127"
};
static const char * const ebi2cs_groups[] = {
	"gpio39", /* CS1A */
	"gpio40", /* CS2A */
	"gpio123", /* CS1B */
	"gpio124", /* CS2B */
	"gpio131", /* CS5 */
	"gpio132", /* CS4 */
	"gpio133", /* CS3 */
	"gpio134", /* CS0 */
};
static const char * const ebi2_groups[] = {
	/* ADDR9 & ADDR8 */
	"gpio37", "gpio38",
	/* ADDR7 - ADDR 0 */
	"gpio123", "gpio124", "gpio125", "gpio126",
	"gpio127", "gpio128", "gpio129", "gpio130",
	/* (muxed address+data) AD15 - AD0 */
	"gpio135", "gpio136", "gpio137", "gpio138", "gpio139",
	"gpio140", "gpio141", "gpio142", "gpio143", "gpio144",
	"gpio145", "gpio146", "gpio147", "gpio148", "gpio149",
	"gpio150",
	"gpio151", /* OE output enable */
	"gpio152", /* clock */
	"gpio153", /* ADV */
	"gpio154", /* WAIT (input) */
	"gpio155", /* UB Upper Byte Enable */
	"gpio156", /* LB Lower Byte Enable */
	"gpio157", /* WE Write Enable */
	"gpio158", /* busy */
};

static const struct msm_function msm8660_functions[] = {
	FUNCTION(gpio),
	FUNCTION(cam_mclk),
	FUNCTION(dsub),
	FUNCTION(ext_gps),
	FUNCTION(gp_clk_0a),
	FUNCTION(gp_clk_0b),
	FUNCTION(gp_clk_1a),
	FUNCTION(gp_clk_1b),
	FUNCTION(gp_clk_2a),
	FUNCTION(gp_clk_2b),
	FUNCTION(gp_mn),
	FUNCTION(gsbi1),
	FUNCTION(gsbi1_spi_cs1_n),
	FUNCTION(gsbi1_spi_cs2a_n),
	FUNCTION(gsbi1_spi_cs2b_n),
	FUNCTION(gsbi1_spi_cs3_n),
	FUNCTION(gsbi2),
	FUNCTION(gsbi2_spi_cs1_n),
	FUNCTION(gsbi2_spi_cs2_n),
	FUNCTION(gsbi2_spi_cs3_n),
	FUNCTION(gsbi3),
	FUNCTION(gsbi3_spi_cs1_n),
	FUNCTION(gsbi3_spi_cs2_n),
	FUNCTION(gsbi3_spi_cs3_n),
	FUNCTION(gsbi4),
	FUNCTION(gsbi5),
	FUNCTION(gsbi6),
	FUNCTION(gsbi7),
	FUNCTION(gsbi8),
	FUNCTION(gsbi9),
	FUNCTION(gsbi10),
	FUNCTION(gsbi11),
	FUNCTION(gsbi12),
	FUNCTION(hdmi),
	FUNCTION(i2s),
	FUNCTION(lcdc),
	FUNCTION(mdp_vsync),
	FUNCTION(mi2s),
	FUNCTION(pcm),
	FUNCTION(ps_hold),
	FUNCTION(sdc1),
	FUNCTION(sdc2),
	FUNCTION(sdc5),
	FUNCTION(tsif1),
	FUNCTION(tsif2),
	FUNCTION(usb_fs1),
	FUNCTION(usb_fs1_oe_n),
	FUNCTION(usb_fs2),
	FUNCTION(usb_fs2_oe_n),
	FUNCTION(vfe),
	FUNCTION(vsens_alarm),
	FUNCTION(ebi2cs), /* for EBI2 chip selects */
	FUNCTION(ebi2), /* for general EBI2 pins */
};

static const struct msm_pingroup msm8660_groups[] = {
	PINGROUP(0, lcdc, dsub, _, _, _, _, _),
	PINGROUP(1, lcdc, dsub, _, _, _, _, _),
	PINGROUP(2, lcdc, dsub, _, _, _, _, _),
	PINGROUP(3, lcdc, dsub, _, _, _, _, _),
	PINGROUP(4, lcdc, dsub, _, _, _, _, _),
	PINGROUP(5, lcdc, dsub, _, _, _, _, _),
	PINGROUP(6, lcdc, dsub, _, _, _, _, _),
	PINGROUP(7, lcdc, dsub, _, _, _, _, _),
	PINGROUP(8, lcdc, dsub, _, _, _, _, _),
	PINGROUP(9, lcdc, dsub, _, _, _, _, _),
	PINGROUP(10, lcdc, dsub, _, _, _, _, _),
	PINGROUP(11, lcdc, dsub, _, _, _, _, _),
	PINGROUP(12, lcdc, dsub, _, _, _, _, _),
	PINGROUP(13, lcdc, dsub, _, _, _, _, _),
	PINGROUP(14, lcdc, dsub, _, _, _, _, _),
	PINGROUP(15, lcdc, dsub, _, _, _, _, _),
	PINGROUP(16, lcdc, dsub, _, _, _, _, _),
	PINGROUP(17, lcdc, dsub, _, _, _, _, _),
	PINGROUP(18, lcdc, dsub, _, _, _, _, _),
	PINGROUP(19, lcdc, dsub, _, _, _, _, _),
	PINGROUP(20, lcdc, dsub, _, _, _, _, _),
	PINGROUP(21, lcdc, dsub, _, _, _, _, _),
	PINGROUP(22, lcdc, dsub, _, _, _, _, _),
	PINGROUP(23, lcdc, dsub, _, _, _, _, _),
	PINGROUP(24, lcdc, dsub, _, _, _, _, _),
	PINGROUP(25, lcdc, dsub, _, _, _, _, _),
	PINGROUP(26, lcdc, dsub, _, _, _, _, _),
	PINGROUP(27, lcdc, dsub, _, _, _, _, _),
	PINGROUP(28, mdp_vsync, _, _, _, _, _, _),
	PINGROUP(29, vfe, gp_mn, _, _, _, _, _),
	PINGROUP(30, vfe, gp_clk_0a, _, _, _, _, _),
	PINGROUP(31, vfe, gp_clk_1a, _, _, _, _, _),
	PINGROUP(32, cam_mclk, _, _, _, _, _, _),
	PINGROUP(33, gsbi1, _, _, _, _, _, _),
	PINGROUP(34, gsbi1, _, _, _, _, _, _),
	PINGROUP(35, gsbi1, _, _, _, _, _, _),
	PINGROUP(36, gsbi1, _, _, _, _, _, _),
	PINGROUP(37, gsbi2, ebi2, _, _, _, _, _),
	PINGROUP(38, gsbi2, ebi2, _, _, _, _, _),
	PINGROUP(39, gsbi2, ebi2cs, mdp_vsync, _, _, _, _),
	PINGROUP(40, gsbi2, ebi2cs, _, _, _, _, _),
	PINGROUP(41, gsbi3, mdp_vsync, _, _, _, _, _),
	PINGROUP(42, gsbi3, vfe, _, _, _, _, _),
	PINGROUP(43, gsbi3, _, _, _, _, _, _),
	PINGROUP(44, gsbi3, _, _, _, _, _, _),
	PINGROUP(45, gsbi4, gsbi3_spi_cs2_n, _, _, _, _, _),
	PINGROUP(46, gsbi4, gsbi3_spi_cs3_n, vfe, _, _, _, _),
	PINGROUP(47, gsbi4, _, _, _, _, _, _),
	PINGROUP(48, gsbi4, _, _, _, _, _, _),
	PINGROUP(49, gsbi5, usb_fs1, _, _, _, _, _),
	PINGROUP(50, gsbi5, usb_fs1, _, _, _, _, _),
	PINGROUP(51, gsbi5, usb_fs1, usb_fs1_oe_n, _, _, _, _),
	PINGROUP(52, gsbi5, _, _, _, _, _, _),
	PINGROUP(53, gsbi6, _, _, _, _, _, _),
	PINGROUP(54, gsbi6, _, _, _, _, _, _),
	PINGROUP(55, gsbi6, _, _, _, _, _, _),
	PINGROUP(56, gsbi6, _, _, _, _, _, _),
	PINGROUP(57, gsbi7, _, _, _, _, _, _),
	PINGROUP(58, gsbi7, _, _, _, _, _, _),
	PINGROUP(59, gsbi7, _, _, _, _, _, _),
	PINGROUP(60, gsbi7, _, _, _, _, _, _),
	PINGROUP(61, _, _, _, _, _, _, _),
	PINGROUP(62, gsbi8, gsbi3_spi_cs1_n, gsbi1_spi_cs2a_n, _, _, _, _),
	PINGROUP(63, gsbi8, gsbi1_spi_cs1_n, _, _, _, _, _),
	PINGROUP(64, gsbi8, gsbi1_spi_cs2b_n, _, _, _, _, _),
	PINGROUP(65, gsbi8, gsbi1_spi_cs3_n, _, _, _, _, _),
	PINGROUP(66, gsbi9, ext_gps, _, _, _, _, _),
	PINGROUP(67, gsbi9, ext_gps, _, _, _, _, _),
	PINGROUP(68, gsbi9, ext_gps, _, _, _, _, _),
	PINGROUP(69, gsbi9, ext_gps, _, _, _, _, _),
	PINGROUP(70, gsbi10, gp_clk_2b, _, _, _, _, _),
	PINGROUP(71, gsbi10, usb_fs2, _, _, _, _, _),
	PINGROUP(72, gsbi10, usb_fs2, _, _, _, _, _),
	PINGROUP(73, gsbi10, usb_fs2, usb_fs2_oe_n, _, _, _, _),
	PINGROUP(74, _, _, _, _, _, _, _),
	PINGROUP(75, _, _, _, _, _, _, _),
	PINGROUP(76, _, _, _, _, _, _, _),
	PINGROUP(77, _, _, _, _, _, _, _),
	PINGROUP(78, _, _, _, _, _, _, _),
	PINGROUP(79, _, _, _, _, _, _, _),
	PINGROUP(80, _, _, _, _, _, _, _),
	PINGROUP(81, _, _, _, _, _, _, _),
	PINGROUP(82, _, _, _, _, _, _, _),
	PINGROUP(83, _, _, _, _, _, _, _),
	PINGROUP(84, _, _, _, _, _, _, _),
	PINGROUP(85, _, _, _, _, _, _, _),
	PINGROUP(86, _, _, _, _, _, _, _),
	PINGROUP(87, _, _, _, _, _, _, _),
	PINGROUP(88, _, _, _, _, _, _, _),
	PINGROUP(89, _, _, _, _, _, _, _),
	PINGROUP(90, _, _, _, _, _, _, _),
	PINGROUP(91, _, _, _, _, _, _, _),
	PINGROUP(92, ps_hold, _, _, _, _, _, _),
	PINGROUP(93, tsif1, _, _, _, _, _, _),
	PINGROUP(94, tsif1, _, _, _, _, _, _),
	PINGROUP(95, tsif1, sdc5, _, _, _, _, _),
	PINGROUP(96, tsif1, sdc5, _, _, _, _, _),
	PINGROUP(97, tsif2, sdc5, _, _, _, _, _),
	PINGROUP(98, tsif2, sdc5, _, _, _, _, _),
	PINGROUP(99, tsif2, sdc5, _, _, _, _, _),
	PINGROUP(100, tsif2, sdc5, _, _, _, _, _),
	PINGROUP(101, mi2s, _, _, _, _, _, _),
	PINGROUP(102, mi2s, _, _, _, _, _, _),
	PINGROUP(103, mi2s, gsbi11, gp_clk_2a, _, _, _, _),
	PINGROUP(104, mi2s, gsbi11, _, _, _, _, _),
	PINGROUP(105, mi2s, gsbi11, vfe, _, _, _, _),
	PINGROUP(106, mi2s, gsbi11, vfe, _, _, _, _),
	PINGROUP(107, mi2s, _, _, _, _, _, _),
	PINGROUP(108, i2s, _, _, _, _, _, _),
	PINGROUP(109, i2s, _, _, _, _, _, _),
	PINGROUP(110, i2s, _, _, _, _, _, _),
	PINGROUP(111, pcm, _, _, _, _, _, _),
	PINGROUP(112, pcm, _, _, _, _, _, _),
	PINGROUP(113, pcm, _, _, _, _, _, _),
	PINGROUP(114, pcm, _, _, _, _, _, _),
	PINGROUP(115, i2s, gsbi12, gp_clk_0b, _, _, _, _),
	PINGROUP(116, i2s, gsbi12, _, _, _, _, _),
	PINGROUP(117, i2s, gsbi12, vfe, _, _, _, _),
	PINGROUP(118, i2s, gsbi12, _, _, _, _, _),
	PINGROUP(119, i2s, _, _, _, _, _, _),
	PINGROUP(120, i2s, _, _, _, _, _, _),
	PINGROUP(121, i2s, _, _, _, _, _, _),
	PINGROUP(122, i2s, gp_clk_1b, _, _, _, _, _),
	PINGROUP(123, ebi2, gsbi2_spi_cs1_n, ebi2cs, _, _, _, _),
	PINGROUP(124, ebi2, gsbi2_spi_cs2_n, ebi2cs, _, _, _, _),
	PINGROUP(125, ebi2, gsbi2_spi_cs3_n, _, _, _, _, _),
	PINGROUP(126, ebi2, _, _, _, _, _, _),
	PINGROUP(127, ebi2, vsens_alarm, _, _, _, _, _),
	PINGROUP(128, ebi2, _, _, _, _, _, _),
	PINGROUP(129, ebi2, _, _, _, _, _, _),
	PINGROUP(130, ebi2, _, _, _, _, _, _),
	PINGROUP(131, ebi2cs, _, _, _, _, _, _),
	PINGROUP(132, ebi2cs, _, _, _, _, _, _),
	PINGROUP(133, ebi2cs, _, _, _, _, _, _),
	PINGROUP(134, ebi2cs, _, _, _, _, _, _),
	PINGROUP(135, ebi2, _, _, _, _, _, _),
	PINGROUP(136, ebi2, _, _, _, _, _, _),
	PINGROUP(137, ebi2, _, _, _, _, _, _),
	PINGROUP(138, ebi2, _, _, _, _, _, _),
	PINGROUP(139, ebi2, _, _, _, _, _, _),
	PINGROUP(140, ebi2, _, _, _, _, _, _),
	PINGROUP(141, ebi2, _, _, _, _, _, _),
	PINGROUP(142, ebi2, _, _, _, _, _, _),
	PINGROUP(143, ebi2, sdc2, _, _, _, _, _),
	PINGROUP(144, ebi2, sdc2, _, _, _, _, _),
	PINGROUP(145, ebi2, sdc2, _, _, _, _, _),
	PINGROUP(146, ebi2, sdc2, _, _, _, _, _),
	PINGROUP(147, ebi2, sdc2, _, _, _, _, _),
	PINGROUP(148, ebi2, sdc2, _, _, _, _, _),
	PINGROUP(149, ebi2, sdc2, _, _, _, _, _),
	PINGROUP(150, ebi2, sdc2, _, _, _, _, _),
	PINGROUP(151, ebi2, sdc2, _, _, _, _, _),
	PINGROUP(152, ebi2, sdc2, _, _, _, _, _),
	PINGROUP(153, ebi2, _, _, _, _, _, _),
	PINGROUP(154, ebi2, _, _, _, _, _, _),
	PINGROUP(155, ebi2, _, _, _, _, _, _),
	PINGROUP(156, ebi2, _, _, _, _, _, _),
	PINGROUP(157, ebi2, _, _, _, _, _, _),
	PINGROUP(158, ebi2, _, _, _, _, _, _),
	PINGROUP(159, sdc1, _, _, _, _, _, _),
	PINGROUP(160, sdc1, _, _, _, _, _, _),
	PINGROUP(161, sdc1, _, _, _, _, _, _),
	PINGROUP(162, sdc1, _, _, _, _, _, _),
	PINGROUP(163, sdc1, _, _, _, _, _, _),
	PINGROUP(164, sdc1, _, _, _, _, _, _),
	PINGROUP(165, sdc1, _, _, _, _, _, _),
	PINGROUP(166, sdc1, _, _, _, _, _, _),
	PINGROUP(167, sdc1, _, _, _, _, _, _),
	PINGROUP(168, sdc1, _, _, _, _, _, _),
	PINGROUP(169, hdmi, _, _, _, _, _, _),
	PINGROUP(170, hdmi, _, _, _, _, _, _),
	PINGROUP(171, hdmi, _, _, _, _, _, _),
	PINGROUP(172, hdmi, _, _, _, _, _, _),

	SDC_PINGROUP(sdc4_clk, 0x20a0, -1, 6),
	SDC_PINGROUP(sdc4_cmd, 0x20a0, 11, 3),
	SDC_PINGROUP(sdc4_data, 0x20a0, 9, 0),

	SDC_PINGROUP(sdc3_clk, 0x20a4, -1, 6),
	SDC_PINGROUP(sdc3_cmd, 0x20a4, 11, 3),
	SDC_PINGROUP(sdc3_data, 0x20a4, 9, 0),
};

#define NUM_GPIO_PINGROUPS 173

static const struct msm_pinctrl_soc_data msm8660_pinctrl = {
	.pins = msm8660_pins,
	.npins = ARRAY_SIZE(msm8660_pins),
	.functions = msm8660_functions,
	.nfunctions = ARRAY_SIZE(msm8660_functions),
	.groups = msm8660_groups,
	.ngroups = ARRAY_SIZE(msm8660_groups),
	.ngpios = NUM_GPIO_PINGROUPS,
};

static int msm8660_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &msm8660_pinctrl);
}

static const struct of_device_id msm8660_pinctrl_of_match[] = {
	{ .compatible = "qcom,msm8660-pinctrl", },
	{ },
};

static struct platform_driver msm8660_pinctrl_driver = {
	.driver = {
		.name = "msm8660-pinctrl",
		.of_match_table = msm8660_pinctrl_of_match,
	},
	.probe = msm8660_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init msm8660_pinctrl_init(void)
{
	return platform_driver_register(&msm8660_pinctrl_driver);
}
arch_initcall(msm8660_pinctrl_init);

static void __exit msm8660_pinctrl_exit(void)
{
	platform_driver_unregister(&msm8660_pinctrl_driver);
}
module_exit(msm8660_pinctrl_exit);

MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@sonymobile.com>");
MODULE_DESCRIPTION("Qualcomm MSM8660 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, msm8660_pinctrl_of_match);
