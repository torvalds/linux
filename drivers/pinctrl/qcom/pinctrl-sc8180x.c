// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020-2021, Linaro Ltd.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

static const char * const sc8180x_tiles[] = {
	"south",
	"east",
	"west"
};

enum {
	SOUTH,
	EAST,
	WEST
};

/*
 * ACPI DSDT has one single memory resource for TLMM.  The offsets below are
 * used to locate different tiles for ACPI probe.
 */
struct tile_info {
	u32 offset;
	u32 size;
};

static const struct tile_info sc8180x_tile_info[] = {
	{ 0x00d00000, 0x00300000, },
	{ 0x00500000, 0x00700000, },
	{ 0x00100000, 0x00300000, },
};

#define REG_SIZE 0x1000
#define PINGROUP_OFFSET(id, _tile, offset, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
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
			msm_mux_##f8,			\
			msm_mux_##f9			\
		},					\
		.nfuncs = 10,				\
		.ctl_reg = REG_SIZE * id + offset,	\
		.io_reg = REG_SIZE * id + 0x4 + offset,	\
		.intr_cfg_reg = REG_SIZE * id + 0x8 + offset,	\
		.intr_status_reg = REG_SIZE * id + 0xc + offset,\
		.intr_target_reg = REG_SIZE * id + 0x8 + offset,\
		.tile = _tile,				\
		.mux_bit = 2,				\
		.pull_bit = 0,				\
		.drv_bit = 6,				\
		.oe_bit = 9,				\
		.in_bit = 0,				\
		.out_bit = 1,				\
		.intr_enable_bit = 0,			\
		.intr_status_bit = 0,			\
		.intr_target_bit = 5,			\
		.intr_target_kpss_val = 3,		\
		.intr_raw_status_bit = 4,		\
		.intr_polarity_bit = 1,			\
		.intr_detection_bit = 2,		\
		.intr_detection_width = 2,		\
	}

#define PINGROUP(id, _tile, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	PINGROUP_OFFSET(id, _tile, 0x0, f1, f2, f3, f4, f5, f6, f7, f8, f9)

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
		.tile = EAST,				\
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

#define UFS_RESET(pg_name)				\
	{						\
		.grp = PINCTRL_PINGROUP(#pg_name, 	\
			pg_name##_pins, 		\
			ARRAY_SIZE(pg_name##_pins)),	\
		.ctl_reg = 0xb6000,			\
		.io_reg = 0xb6004,			\
		.intr_cfg_reg = 0,			\
		.intr_status_reg = 0,			\
		.intr_target_reg = 0,			\
		.tile = SOUTH,				\
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
static const struct pinctrl_pin_desc sc8180x_pins[] = {
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
	PINCTRL_PIN(191, "SDC2_CLK"),
	PINCTRL_PIN(192, "SDC2_CMD"),
	PINCTRL_PIN(193, "SDC2_DATA"),
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

static const unsigned int ufs_reset_pins[] = { 190 };
static const unsigned int sdc2_clk_pins[] = { 191 };
static const unsigned int sdc2_cmd_pins[] = { 192 };
static const unsigned int sdc2_data_pins[] = { 193 };

enum sc8180x_functions {
	msm_mux_adsp_ext,
	msm_mux_agera_pll,
	msm_mux_aoss_cti,
	msm_mux_atest_char,
	msm_mux_atest_tsens,
	msm_mux_atest_tsens2,
	msm_mux_atest_usb0,
	msm_mux_atest_usb1,
	msm_mux_atest_usb2,
	msm_mux_atest_usb3,
	msm_mux_atest_usb4,
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
	msm_mux_cci_timer5,
	msm_mux_cci_timer6,
	msm_mux_cci_timer7,
	msm_mux_cci_timer8,
	msm_mux_cci_timer9,
	msm_mux_cri_trng,
	msm_mux_dbg_out,
	msm_mux_ddr_bist,
	msm_mux_ddr_pxi,
	msm_mux_debug_hot,
	msm_mux_dp_hot,
	msm_mux_edp_hot,
	msm_mux_edp_lcd,
	msm_mux_emac_phy,
	msm_mux_emac_pps,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_gcc_gp4,
	msm_mux_gcc_gp5,
	msm_mux_gpio,
	msm_mux_gps,
	msm_mux_grfc,
	msm_mux_hs1_mi2s,
	msm_mux_hs2_mi2s,
	msm_mux_hs3_mi2s,
	msm_mux_jitter_bist,
	msm_mux_lpass_slimbus,
	msm_mux_m_voc,
	msm_mux_mdp_vsync,
	msm_mux_mdp_vsync0,
	msm_mux_mdp_vsync1,
	msm_mux_mdp_vsync2,
	msm_mux_mdp_vsync3,
	msm_mux_mdp_vsync4,
	msm_mux_mdp_vsync5,
	msm_mux_mss_lte,
	msm_mux_nav_pps,
	msm_mux_pa_indicator,
	msm_mux_pci_e0,
	msm_mux_pci_e1,
	msm_mux_pci_e2,
	msm_mux_pci_e3,
	msm_mux_phase_flag,
	msm_mux_pll_bist,
	msm_mux_pll_bypassnl,
	msm_mux_pll_reset,
	msm_mux_pri_mi2s,
	msm_mux_pri_mi2s_ws,
	msm_mux_prng_rosc,
	msm_mux_qdss_cti,
	msm_mux_qdss_gpio,
	msm_mux_qlink,
	msm_mux_qspi0,
	msm_mux_qspi0_clk,
	msm_mux_qspi0_cs,
	msm_mux_qspi1,
	msm_mux_qspi1_clk,
	msm_mux_qspi1_cs,
	msm_mux_qua_mi2s,
	msm_mux_qup0,
	msm_mux_qup1,
	msm_mux_qup2,
	msm_mux_qup3,
	msm_mux_qup4,
	msm_mux_qup5,
	msm_mux_qup6,
	msm_mux_qup7,
	msm_mux_qup8,
	msm_mux_qup9,
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
	msm_mux_qup_l4,
	msm_mux_qup_l5,
	msm_mux_qup_l6,
	msm_mux_rgmii,
	msm_mux_sd_write,
	msm_mux_sdc4,
	msm_mux_sdc4_clk,
	msm_mux_sdc4_cmd,
	msm_mux_sec_mi2s,
	msm_mux_sp_cmu,
	msm_mux_spkr_i2s,
	msm_mux_ter_mi2s,
	msm_mux_tgu,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_tsif1,
	msm_mux_tsif2,
	msm_mux_uim1,
	msm_mux_uim2,
	msm_mux_uim_batt,
	msm_mux_usb0_phy,
	msm_mux_usb1_phy,
	msm_mux_usb2phy_ac,
	msm_mux_vfr_1,
	msm_mux_vsense_trigger,
	msm_mux_wlan1_adc,
	msm_mux_wlan2_adc,
	msm_mux_wmss_reset,
	msm_mux__,
};

static const char * const adsp_ext_groups[] = {
	"gpio115",
};

static const char * const agera_pll_groups[] = {
	"gpio37",
};

static const char * const aoss_cti_groups[] = {
	"gpio113",
};

static const char * const atest_char_groups[] = {
	"gpio133", "gpio134", "gpio135", "gpio140", "gpio142",
};

static const char * const atest_tsens2_groups[] = {
	"gpio62",
};

static const char * const atest_tsens_groups[] = {
	"gpio93",
};

static const char * const atest_usb0_groups[] = {
	"gpio90", "gpio91", "gpio92", "gpio93", "gpio94",
};

static const char * const atest_usb1_groups[] = {
	"gpio60", "gpio62", "gpio63", "gpio64", "gpio65",
};

static const char * const atest_usb2_groups[] = {
	"gpio34", "gpio95", "gpio102", "gpio121", "gpio122",
};

static const char * const atest_usb3_groups[] = {
	"gpio68", "gpio71", "gpio72", "gpio73", "gpio74",
};

static const char * const atest_usb4_groups[] = {
	"gpio75", "gpio76", "gpio77", "gpio78", "gpio88",
};

static const char * const audio_ref_groups[] = {
	"gpio148",
};

static const char * const btfm_slimbus_groups[] = {
	"gpio153", "gpio154",
};

static const char * const cam_mclk_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16", "gpio25", "gpio179", "gpio180",
	"gpio181",
};

static const char * const cci_async_groups[] = {
	"gpio24", "gpio25", "gpio26", "gpio176", "gpio185", "gpio186",
};

static const char * const cci_i2c_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio17", "gpio18", "gpio19",
	"gpio20", "gpio31", "gpio32", "gpio33", "gpio34", "gpio39", "gpio40",
	"gpio41", "gpio42",
};

static const char * const cci_timer0_groups[] = {
	"gpio21",
};

static const char * const cci_timer1_groups[] = {
	"gpio22",
};

static const char * const cci_timer2_groups[] = {
	"gpio23",
};

static const char * const cci_timer3_groups[] = {
	"gpio24",
};

static const char * const cci_timer4_groups[] = {
	"gpio178",
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

static const char * const cri_trng_groups[] = {
	"gpio159",
	"gpio160",
	"gpio161",
};

static const char * const dbg_out_groups[] = {
	"gpio34",
};

static const char * const ddr_bist_groups[] = {
	"gpio98", "gpio99", "gpio145", "gpio146",
};

static const char * const ddr_pxi_groups[] = {
	"gpio60", "gpio62", "gpio63", "gpio64", "gpio65", "gpio68", "gpio71",
	"gpio72", "gpio73", "gpio74", "gpio75", "gpio76", "gpio77", "gpio78",
	"gpio88", "gpio90",
};

static const char * const debug_hot_groups[] = {
	"gpio7",
};

static const char * const dp_hot_groups[] = {
	"gpio189",
};

static const char * const edp_hot_groups[] = {
	"gpio10",
};

static const char * const edp_lcd_groups[] = {
	"gpio11",
};

static const char * const emac_phy_groups[] = {
	"gpio124",
};

static const char * const emac_pps_groups[] = {
	"gpio81",
};

static const char * const gcc_gp1_groups[] = {
	"gpio131", "gpio136",
};

static const char * const gcc_gp2_groups[] = {
	"gpio21", "gpio137",
};

static const char * const gcc_gp3_groups[] = {
	"gpio22", "gpio138",
};

static const char * const gcc_gp4_groups[] = {
	"gpio139", "gpio182",
};

static const char * const gcc_gp5_groups[] = {
	"gpio140", "gpio183",
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

static const char * const gps_groups[] = {
	"gpio60", "gpio76", "gpio77", "gpio81", "gpio82",
};

static const char * const grfc_groups[] = {
	"gpio64", "gpio65", "gpio66", "gpio67", "gpio68", "gpio71", "gpio72",
	"gpio73", "gpio74", "gpio75", "gpio76", "gpio77", "gpio78", "gpio79",
	"gpio80", "gpio81", "gpio82",
};

static const char * const hs1_mi2s_groups[] = {
	"gpio155", "gpio156", "gpio157", "gpio158", "gpio159",
};

static const char * const hs2_mi2s_groups[] = {
	"gpio160", "gpio161", "gpio162", "gpio163", "gpio164",
};

static const char * const hs3_mi2s_groups[] = {
	"gpio125", "gpio165", "gpio166", "gpio167", "gpio168",
};

static const char * const jitter_bist_groups[] = {
	"gpio129",
};

static const char * const lpass_slimbus_groups[] = {
	"gpio149", "gpio150", "gpio151", "gpio152",
};

static const char * const m_voc_groups[] = {
	"gpio10",
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

static const char * const mdp_vsync_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio60", "gpio82",
};

static const char * const mss_lte_groups[] = {
	"gpio69", "gpio70",
};

static const char * const nav_pps_groups[] = {
	"gpio60", "gpio60", "gpio76", "gpio76", "gpio77", "gpio77", "gpio81",
	"gpio81", "gpio82", "gpio82",
};

static const char * const pa_indicator_groups[] = {
	"gpio68",
};

static const char * const pci_e0_groups[] = {
	"gpio35", "gpio36",
};

static const char * const pci_e1_groups[] = {
	"gpio102", "gpio103",
};

static const char * const pci_e2_groups[] = {
	"gpio175", "gpio176",
};

static const char * const pci_e3_groups[] = {
	"gpio178", "gpio179",
};

static const char * const phase_flag_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7", "gpio33", "gpio53", "gpio54",
	"gpio102", "gpio120", "gpio121", "gpio122", "gpio123", "gpio125",
	"gpio148", "gpio149", "gpio150", "gpio151", "gpio152", "gpio155",
	"gpio156", "gpio157", "gpio158", "gpio159", "gpio160", "gpio161",
	"gpio162", "gpio163", "gpio164", "gpio165", "gpio166", "gpio167",
	"gpio168",
};

static const char * const pll_bist_groups[] = {
	"gpio130",
};

static const char * const pll_bypassnl_groups[] = {
	"gpio100",
};

static const char * const pll_reset_groups[] = {
	"gpio101",
};

static const char * const pri_mi2s_groups[] = {
	"gpio143", "gpio144", "gpio146", "gpio147",
};

static const char * const pri_mi2s_ws_groups[] = {
	"gpio145",
};

static const char * const prng_rosc_groups[] = {
	"gpio163",
};

static const char * const qdss_cti_groups[] = {
	"gpio49", "gpio50", "gpio81", "gpio82", "gpio89", "gpio90", "gpio141",
	"gpio142",
};

static const char * const qdss_gpio_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16", "gpio17", "gpio18", "gpio19",
	"gpio20", "gpio21", "gpio22", "gpio23", "gpio24", "gpio25", "gpio26",
	"gpio27", "gpio28", "gpio29", "gpio30", "gpio39", "gpio40", "gpio41",
	"gpio42", "gpio92", "gpio114", "gpio115", "gpio116", "gpio117",
	"gpio118", "gpio119", "gpio120", "gpio121", "gpio130", "gpio132",
	"gpio133", "gpio134", "gpio135",
};

static const char * const qlink_groups[] = {
	"gpio61", "gpio62",
};

static const char * const qspi0_groups[] = {
	"gpio89", "gpio90", "gpio91", "gpio93",
};

static const char * const qspi0_clk_groups[] = {
	"gpio92",
};

static const char * const qspi0_cs_groups[] = {
	"gpio88", "gpio94",
};

static const char * const qspi1_groups[] = {
	"gpio56", "gpio57", "gpio161", "gpio162",
};

static const char * const qspi1_clk_groups[] = {
	"gpio163",
};

static const char * const qspi1_cs_groups[] = {
	"gpio55", "gpio164",
};

static const char * const qua_mi2s_groups[] = {
	"gpio136", "gpio137", "gpio138", "gpio139", "gpio140", "gpio141",
	"gpio142",
};

static const char * const qup0_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};

static const char * const qup10_groups[] = {
	"gpio9", "gpio10", "gpio11", "gpio12",
};

static const char * const qup11_groups[] = {
	"gpio92", "gpio93", "gpio94", "gpio95",
};

static const char * const qup12_groups[] = {
	"gpio83", "gpio84", "gpio85", "gpio86",
};

static const char * const qup13_groups[] = {
	"gpio43", "gpio44", "gpio45", "gpio46",
};

static const char * const qup14_groups[] = {
	"gpio47", "gpio48", "gpio49", "gpio50",
};

static const char * const qup15_groups[] = {
	"gpio27", "gpio28", "gpio29", "gpio30",
};

static const char * const qup16_groups[] = {
	"gpio83", "gpio84", "gpio85", "gpio86",
};

static const char * const qup17_groups[] = {
	"gpio55", "gpio56", "gpio57", "gpio58",
};

static const char * const qup18_groups[] = {
	"gpio23", "gpio24", "gpio25", "gpio26",
};

static const char * const qup19_groups[] = {
	"gpio181", "gpio182", "gpio183", "gpio184",
};

static const char * const qup1_groups[] = {
	"gpio114", "gpio115", "gpio116", "gpio117",
};

static const char * const qup2_groups[] = {
	"gpio126", "gpio127", "gpio128", "gpio129",
};

static const char * const qup3_groups[] = {
	"gpio144", "gpio145", "gpio146", "gpio147",
};

static const char * const qup4_groups[] = {
	"gpio51", "gpio52", "gpio53", "gpio54",
};

static const char * const qup5_groups[] = {
	"gpio119", "gpio120", "gpio121", "gpio122",
};

static const char * const qup6_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};

static const char * const qup7_groups[] = {
	"gpio98", "gpio99", "gpio100", "gpio101",
};

static const char * const qup8_groups[] = {
	"gpio88", "gpio89", "gpio90", "gpio91",
};

static const char * const qup9_groups[] = {
	"gpio39", "gpio40", "gpio41", "gpio42",
};

static const char * const qup_l4_groups[] = {
	"gpio35", "gpio59", "gpio60", "gpio95",
};

static const char * const qup_l5_groups[] = {
	"gpio7", "gpio33", "gpio36", "gpio96",
};

static const char * const qup_l6_groups[] = {
	"gpio6", "gpio34", "gpio37", "gpio97",
};

static const char * const rgmii_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7", "gpio59", "gpio114", "gpio115",
	"gpio116", "gpio117", "gpio118", "gpio119", "gpio120", "gpio121",
	"gpio122",
};

static const char * const sd_write_groups[] = {
	"gpio97",
};

static const char * const sdc4_groups[] = {
	"gpio91", "gpio93", "gpio94", "gpio95",
};

static const char * const sdc4_clk_groups[] = {
	"gpio92",
};

static const char * const sdc4_cmd_groups[] = {
	"gpio90",
};

static const char * const sec_mi2s_groups[] = {
	"gpio126", "gpio127", "gpio128", "gpio129", "gpio130",
};

static const char * const sp_cmu_groups[] = {
	"gpio162",
};

static const char * const spkr_i2s_groups[] = {
	"gpio148", "gpio149", "gpio150", "gpio151", "gpio152",
};

static const char * const ter_mi2s_groups[] = {
	"gpio131", "gpio132", "gpio133", "gpio134", "gpio135",
};

static const char * const tgu_groups[] = {
	"gpio89", "gpio90", "gpio91", "gpio88", "gpio74", "gpio77", "gpio76",
	"gpio75",
};

static const char * const tsense_pwm1_groups[] = {
	"gpio150",
};

static const char * const tsense_pwm2_groups[] = {
	"gpio150",
};

static const char * const tsif1_groups[] = {
	"gpio88", "gpio89", "gpio90", "gpio91", "gpio97",
};

static const char * const tsif2_groups[] = {
	"gpio92", "gpio93", "gpio94", "gpio95", "gpio96",
};

static const char * const uim1_groups[] = {
	"gpio109", "gpio110", "gpio111", "gpio112",
};

static const char * const uim2_groups[] = {
	"gpio105", "gpio106", "gpio107", "gpio108",
};

static const char * const uim_batt_groups[] = {
	"gpio113",
};

static const char * const usb0_phy_groups[] = {
	"gpio38",
};

static const char * const usb1_phy_groups[] = {
	"gpio58",
};

static const char * const usb2phy_ac_groups[] = {
	"gpio47", "gpio48", "gpio113", "gpio123",
};

static const char * const vfr_1_groups[] = {
	"gpio91",
};

static const char * const vsense_trigger_groups[] = {
	"gpio62",
};

static const char * const wlan1_adc_groups[] = {
	"gpio64", "gpio63",
};

static const char * const wlan2_adc_groups[] = {
	"gpio68", "gpio65",
};

static const char * const wmss_reset_groups[] = {
	"gpio63",
};

static const struct pinfunction sc8180x_functions[] = {
	MSM_PIN_FUNCTION(adsp_ext),
	MSM_PIN_FUNCTION(agera_pll),
	MSM_PIN_FUNCTION(aoss_cti),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_tsens),
	MSM_PIN_FUNCTION(atest_tsens2),
	MSM_PIN_FUNCTION(atest_usb0),
	MSM_PIN_FUNCTION(atest_usb1),
	MSM_PIN_FUNCTION(atest_usb2),
	MSM_PIN_FUNCTION(atest_usb3),
	MSM_PIN_FUNCTION(atest_usb4),
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
	MSM_PIN_FUNCTION(cci_timer5),
	MSM_PIN_FUNCTION(cci_timer6),
	MSM_PIN_FUNCTION(cci_timer7),
	MSM_PIN_FUNCTION(cci_timer8),
	MSM_PIN_FUNCTION(cci_timer9),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(ddr_bist),
	MSM_PIN_FUNCTION(ddr_pxi),
	MSM_PIN_FUNCTION(debug_hot),
	MSM_PIN_FUNCTION(dp_hot),
	MSM_PIN_FUNCTION(edp_hot),
	MSM_PIN_FUNCTION(edp_lcd),
	MSM_PIN_FUNCTION(emac_phy),
	MSM_PIN_FUNCTION(emac_pps),
	MSM_PIN_FUNCTION(gcc_gp1),
	MSM_PIN_FUNCTION(gcc_gp2),
	MSM_PIN_FUNCTION(gcc_gp3),
	MSM_PIN_FUNCTION(gcc_gp4),
	MSM_PIN_FUNCTION(gcc_gp5),
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(gps),
	MSM_PIN_FUNCTION(grfc),
	MSM_PIN_FUNCTION(hs1_mi2s),
	MSM_PIN_FUNCTION(hs2_mi2s),
	MSM_PIN_FUNCTION(hs3_mi2s),
	MSM_PIN_FUNCTION(jitter_bist),
	MSM_PIN_FUNCTION(lpass_slimbus),
	MSM_PIN_FUNCTION(m_voc),
	MSM_PIN_FUNCTION(mdp_vsync),
	MSM_PIN_FUNCTION(mdp_vsync0),
	MSM_PIN_FUNCTION(mdp_vsync1),
	MSM_PIN_FUNCTION(mdp_vsync2),
	MSM_PIN_FUNCTION(mdp_vsync3),
	MSM_PIN_FUNCTION(mdp_vsync4),
	MSM_PIN_FUNCTION(mdp_vsync5),
	MSM_PIN_FUNCTION(mss_lte),
	MSM_PIN_FUNCTION(nav_pps),
	MSM_PIN_FUNCTION(pa_indicator),
	MSM_PIN_FUNCTION(pci_e0),
	MSM_PIN_FUNCTION(pci_e1),
	MSM_PIN_FUNCTION(pci_e2),
	MSM_PIN_FUNCTION(pci_e3),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(pll_bist),
	MSM_PIN_FUNCTION(pll_bypassnl),
	MSM_PIN_FUNCTION(pll_reset),
	MSM_PIN_FUNCTION(pri_mi2s),
	MSM_PIN_FUNCTION(pri_mi2s_ws),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qdss_gpio),
	MSM_PIN_FUNCTION(qlink),
	MSM_PIN_FUNCTION(qspi0),
	MSM_PIN_FUNCTION(qspi0_clk),
	MSM_PIN_FUNCTION(qspi0_cs),
	MSM_PIN_FUNCTION(qspi1),
	MSM_PIN_FUNCTION(qspi1_clk),
	MSM_PIN_FUNCTION(qspi1_cs),
	MSM_PIN_FUNCTION(qua_mi2s),
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
	MSM_PIN_FUNCTION(qup_l4),
	MSM_PIN_FUNCTION(qup_l5),
	MSM_PIN_FUNCTION(qup_l6),
	MSM_PIN_FUNCTION(rgmii),
	MSM_PIN_FUNCTION(sd_write),
	MSM_PIN_FUNCTION(sdc4),
	MSM_PIN_FUNCTION(sdc4_clk),
	MSM_PIN_FUNCTION(sdc4_cmd),
	MSM_PIN_FUNCTION(sec_mi2s),
	MSM_PIN_FUNCTION(sp_cmu),
	MSM_PIN_FUNCTION(spkr_i2s),
	MSM_PIN_FUNCTION(ter_mi2s),
	MSM_PIN_FUNCTION(tgu),
	MSM_PIN_FUNCTION(tsense_pwm1),
	MSM_PIN_FUNCTION(tsense_pwm2),
	MSM_PIN_FUNCTION(tsif1),
	MSM_PIN_FUNCTION(tsif2),
	MSM_PIN_FUNCTION(uim1),
	MSM_PIN_FUNCTION(uim2),
	MSM_PIN_FUNCTION(uim_batt),
	MSM_PIN_FUNCTION(usb0_phy),
	MSM_PIN_FUNCTION(usb1_phy),
	MSM_PIN_FUNCTION(usb2phy_ac),
	MSM_PIN_FUNCTION(vfr_1),
	MSM_PIN_FUNCTION(vsense_trigger),
	MSM_PIN_FUNCTION(wlan1_adc),
	MSM_PIN_FUNCTION(wlan2_adc),
	MSM_PIN_FUNCTION(wmss_reset),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup sc8180x_groups[] = {
	[0] = PINGROUP(0, WEST, qup0, cci_i2c, _, _, _, _, _, _, _),
	[1] = PINGROUP(1, WEST, qup0, cci_i2c, _, _, _, _, _, _, _),
	[2] = PINGROUP(2, WEST, qup0, cci_i2c, _, _, _, _, _, _, _),
	[3] = PINGROUP(3, WEST, qup0, cci_i2c, _, _, _, _, _, _, _),
	[4] = PINGROUP(4, WEST, qup6, rgmii, _, phase_flag, _, _, _, _, _),
	[5] = PINGROUP(5, WEST, qup6, rgmii, _, phase_flag, _, _, _, _, _),
	[6] = PINGROUP(6, WEST, qup6, rgmii, qup_l6, _, phase_flag, _, _, _, _),
	[7] = PINGROUP(7, WEST, qup6, debug_hot, rgmii, qup_l5, _, phase_flag, _, _, _),
	[8] = PINGROUP(8, EAST, mdp_vsync, _, _, _, _, _, _, _, _),
	[9] = PINGROUP(9, EAST, mdp_vsync, qup10, _, _, _, _, _, _, _),
	[10] = PINGROUP(10, EAST, edp_hot, m_voc, mdp_vsync, qup10, _, _, _, _, _),
	[11] = PINGROUP(11, EAST, edp_lcd, qup10, _, _, _, _, _, _, _),
	[12] = PINGROUP(12, EAST, qup10, _, _, _, _, _, _, _, _),
	[13] = PINGROUP(13, EAST, cam_mclk, qdss_gpio, _, _, _, _, _, _, _),
	[14] = PINGROUP(14, EAST, cam_mclk, qdss_gpio, _, _, _, _, _, _, _),
	[15] = PINGROUP(15, EAST, cam_mclk, qdss_gpio, _, _, _, _, _, _, _),
	[16] = PINGROUP(16, EAST, cam_mclk, qdss_gpio, _, _, _, _, _, _, _),
	[17] = PINGROUP(17, EAST, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[18] = PINGROUP(18, EAST, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[19] = PINGROUP(19, EAST, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[20] = PINGROUP(20, EAST, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[21] = PINGROUP(21, EAST, cci_timer0, gcc_gp2, qdss_gpio, _, _, _, _, _, _),
	[22] = PINGROUP(22, EAST, cci_timer1, gcc_gp3, qdss_gpio, _, _, _, _, _, _),
	[23] = PINGROUP(23, EAST, cci_timer2, qup18, qdss_gpio, _, _, _, _, _, _),
	[24] = PINGROUP(24, EAST, cci_timer3, cci_async, qup18, qdss_gpio, _, _, _, _, _),
	[25] = PINGROUP(25, EAST, cam_mclk, cci_async, qup18, qdss_gpio, _, _, _, _, _),
	[26] = PINGROUP(26, EAST, cci_async, qup18, qdss_gpio, _, _, _, _, _, _),
	[27] = PINGROUP(27, EAST, qup15, _, qdss_gpio, _, _, _, _, _, _),
	[28] = PINGROUP(28, EAST, qup15, qdss_gpio, _, _, _, _, _, _, _),
	[29] = PINGROUP(29, EAST, qup15, qdss_gpio, _, _, _, _, _, _, _),
	[30] = PINGROUP(30, EAST, qup15, qdss_gpio, _, _, _, _, _, _, _),
	[31] = PINGROUP(31, EAST, cci_i2c, _, _, _, _, _, _, _, _),
	[32] = PINGROUP(32, EAST, cci_i2c, _, _, _, _, _, _, _, _),
	[33] = PINGROUP(33, EAST, cci_i2c, qup_l5, _, phase_flag, _, _, _, _, _),
	[34] = PINGROUP(34, EAST, cci_i2c, qup_l6, dbg_out, atest_usb2, _, _, _, _, _),
	[35] = PINGROUP(35, SOUTH, pci_e0, qup_l4, _, _, _, _, _, _, _),
	[36] = PINGROUP(36, SOUTH, pci_e0, qup_l5, _, _, _, _, _, _, _),
	[37] = PINGROUP(37, SOUTH, qup_l6, agera_pll, _, _, _, _, _, _, _),
	[38] = PINGROUP(38, SOUTH, usb0_phy, _, _, _, _, _, _, _, _),
	[39] = PINGROUP(39, EAST, qup9, cci_i2c, qdss_gpio, _, _, _, _, _, _),
	[40] = PINGROUP(40, EAST, qup9, cci_i2c, qdss_gpio, _, _, _, _, _, _),
	[41] = PINGROUP(41, EAST, qup9, cci_i2c, qdss_gpio, _, _, _, _, _, _),
	[42] = PINGROUP(42, EAST, qup9, cci_i2c, qdss_gpio, _, _, _, _, _, _),
	[43] = PINGROUP(43, EAST, qup13, _, _, _, _, _, _, _, _),
	[44] = PINGROUP(44, EAST, qup13, _, _, _, _, _, _, _, _),
	[45] = PINGROUP(45, EAST, qup13, _, _, _, _, _, _, _, _),
	[46] = PINGROUP(46, EAST, qup13, _, _, _, _, _, _, _, _),
	[47] = PINGROUP(47, EAST, qup14, usb2phy_ac, _, _, _, _, _, _, _),
	[48] = PINGROUP(48, EAST, qup14, usb2phy_ac, _, _, _, _, _, _, _),
	[49] = PINGROUP(49, EAST, qup14, qdss_cti, _, _, _, _, _, _, _),
	[50] = PINGROUP(50, EAST, qup14, qdss_cti, _, _, _, _, _, _, _),
	[51] = PINGROUP(51, WEST, qup4, _, _, _, _, _, _, _, _),
	[52] = PINGROUP(52, WEST, qup4, _, _, _, _, _, _, _, _),
	[53] = PINGROUP(53, WEST, qup4, _, phase_flag, _, _, _, _, _, _),
	[54] = PINGROUP(54, WEST, qup4, _, _, phase_flag, _, _, _, _, _),
	[55] = PINGROUP(55, WEST, qup17, qspi1_cs, _, _, _, _, _, _, _),
	[56] = PINGROUP(56, WEST, qup17, qspi1, _, _, _, _, _, _, _),
	[57] = PINGROUP(57, WEST, qup17, qspi1, _, _, _, _, _, _, _),
	[58] = PINGROUP(58, WEST, usb1_phy, qup17, _, _, _, _, _, _, _),
	[59] = PINGROUP(59, WEST, rgmii, qup_l4, _, _, _, _, _, _, _),
	[60] = PINGROUP(60, EAST, gps, nav_pps, nav_pps, qup_l4, mdp_vsync, atest_usb1, ddr_pxi, _, _),
	[61] = PINGROUP(61, EAST, qlink, _, _, _, _, _, _, _, _),
	[62] = PINGROUP(62, EAST, qlink, atest_tsens2, atest_usb1, ddr_pxi, vsense_trigger, _, _, _, _),
	[63] = PINGROUP(63, EAST, wmss_reset, _, atest_usb1, ddr_pxi, wlan1_adc, _, _, _, _),
	[64] = PINGROUP(64, EAST, grfc, _, atest_usb1, ddr_pxi, wlan1_adc, _, _, _, _),
	[65] = PINGROUP(65, EAST, grfc, atest_usb1, ddr_pxi, wlan2_adc, _, _, _, _, _),
	[66] = PINGROUP(66, EAST, grfc, _, _, _, _, _, _, _, _),
	[67] = PINGROUP(67, EAST, grfc, _, _, _, _, _, _, _, _),
	[68] = PINGROUP(68, EAST, grfc, pa_indicator, atest_usb3, ddr_pxi, wlan2_adc, _, _, _, _),
	[69] = PINGROUP(69, EAST, mss_lte, _, _, _, _, _, _, _, _),
	[70] = PINGROUP(70, EAST, mss_lte, _, _, _, _, _, _, _, _),
	[71] = PINGROUP(71, EAST, _, grfc, atest_usb3, ddr_pxi, _, _, _, _, _),
	[72] = PINGROUP(72, EAST, _, grfc, atest_usb3, ddr_pxi, _, _, _, _, _),
	[73] = PINGROUP(73, EAST, _, grfc, atest_usb3, ddr_pxi, _, _, _, _, _),
	[74] = PINGROUP(74, EAST, _, grfc, tgu, atest_usb3, ddr_pxi, _, _, _, _),
	[75] = PINGROUP(75, EAST, _, grfc, tgu, atest_usb4, ddr_pxi, _, _, _, _),
	[76] = PINGROUP(76, EAST, _, grfc, gps, nav_pps, nav_pps, tgu, atest_usb4, ddr_pxi, _),
	[77] = PINGROUP(77, EAST, _, grfc, gps, nav_pps, nav_pps, tgu, atest_usb4, ddr_pxi, _),
	[78] = PINGROUP(78, EAST, _, grfc, _, atest_usb4, ddr_pxi, _, _, _, _),
	[79] = PINGROUP(79, EAST, _, grfc, _, _, _, _, _, _, _),
	[80] = PINGROUP(80, EAST, _, grfc, _, _, _, _, _, _, _),
	[81] = PINGROUP(81, EAST, _, grfc, gps, nav_pps, nav_pps, qdss_cti, _, emac_pps, _),
	[82] = PINGROUP(82, EAST, _, grfc, gps, nav_pps, nav_pps, mdp_vsync, qdss_cti, _, _),
	[83] = PINGROUP(83, EAST, qup12, qup16, _, _, _, _, _, _, _),
	[84] = PINGROUP(84, EAST, qup12, qup16, _, _, _, _, _, _, _),
	[85] = PINGROUP(85, EAST, qup12, qup16, _, _, _, _, _, _, _),
	[86] = PINGROUP(86, EAST, qup12, qup16, _, _, _, _, _, _, _),
	[87] = PINGROUP(87, SOUTH, _, _, _, _, _, _, _, _, _),
	[88] = PINGROUP(88, EAST, tsif1, qup8, qspi0_cs, tgu, atest_usb4, ddr_pxi, _, _, _),
	[89] = PINGROUP(89, EAST, tsif1, qup8, qspi0, mdp_vsync0, mdp_vsync1, mdp_vsync2, mdp_vsync3, mdp_vsync4, mdp_vsync5),
	[90] = PINGROUP(90, EAST, tsif1, qup8, qspi0, sdc4_cmd, tgu, qdss_cti, atest_usb0, ddr_pxi, _),
	[91] = PINGROUP(91, EAST, tsif1, qup8, qspi0, sdc4, vfr_1, tgu, atest_usb0, _, _),
	[92] = PINGROUP(92, EAST, tsif2, qup11, qspi0_clk, sdc4_clk, qdss_gpio, atest_usb0, _, _, _),
	[93] = PINGROUP(93, EAST, tsif2, qup11, qspi0, sdc4, atest_tsens, atest_usb0, _, _, _),
	[94] = PINGROUP(94, EAST, tsif2, qup11, qspi0_cs, sdc4, _, atest_usb0, _, _, _),
	[95] = PINGROUP(95, EAST, tsif2, qup11, sdc4, qup_l4, atest_usb2, _, _, _, _),
	[96] = PINGROUP(96, WEST, tsif2, qup_l5, _, _, _, _, _, _, _),
	[97] = PINGROUP(97, WEST, sd_write, tsif1, qup_l6, _, _, _, _, _, _),
	[98] = PINGROUP(98, WEST, qup7, ddr_bist, _, _, _, _, _, _, _),
	[99] = PINGROUP(99, WEST, qup7, ddr_bist, _, _, _, _, _, _, _),
	[100] = PINGROUP(100, WEST, qup7, pll_bypassnl, _, _, _, _, _, _, _),
	[101] = PINGROUP(101, WEST, qup7, pll_reset, _, _, _, _, _, _, _),
	[102] = PINGROUP(102, SOUTH, pci_e1, _, phase_flag, atest_usb2, _, _, _, _, _),
	[103] = PINGROUP(103, SOUTH, pci_e1, _, _, _, _, _, _, _, _),
	[104] = PINGROUP(104, SOUTH, _, _, _, _, _, _, _, _, _),
	[105] = PINGROUP(105, WEST, uim2, _, _, _, _, _, _, _, _),
	[106] = PINGROUP(106, WEST, uim2, _, _, _, _, _, _, _, _),
	[107] = PINGROUP(107, WEST, uim2, _, _, _, _, _, _, _, _),
	[108] = PINGROUP(108, WEST, uim2, _, _, _, _, _, _, _, _),
	[109] = PINGROUP(109, WEST, uim1, _, _, _, _, _, _, _, _),
	[110] = PINGROUP(110, WEST, uim1, _, _, _, _, _, _, _, _),
	[111] = PINGROUP(111, WEST, uim1, _, _, _, _, _, _, _, _),
	[112] = PINGROUP(112, WEST, uim1, _, _, _, _, _, _, _, _),
	[113] = PINGROUP(113, WEST, uim_batt, usb2phy_ac, aoss_cti, _, _, _, _, _, _),
	[114] = PINGROUP(114, WEST, qup1, rgmii, _, qdss_gpio, _, _, _, _, _),
	[115] = PINGROUP(115, WEST, qup1, rgmii, adsp_ext, _, qdss_gpio, _, _, _, _),
	[116] = PINGROUP(116, WEST, qup1, rgmii, _, qdss_gpio, _, _, _, _, _),
	[117] = PINGROUP(117, WEST, qup1, rgmii, _, qdss_gpio, _, _, _, _, _),
	[118] = PINGROUP(118, WEST, rgmii, _, qdss_gpio, _, _, _, _, _, _),
	[119] = PINGROUP(119, WEST, qup5, rgmii, _, qdss_gpio, _, _, _, _, _),
	[120] = PINGROUP(120, WEST, qup5, rgmii, _, phase_flag, qdss_gpio, _, _, _, _),
	[121] = PINGROUP(121, WEST, qup5, rgmii, _, phase_flag, qdss_gpio, atest_usb2, _, _, _),
	[122] = PINGROUP(122, WEST, qup5, rgmii, _, phase_flag, atest_usb2, _, _, _, _),
	[123] = PINGROUP(123, SOUTH, usb2phy_ac, _, phase_flag, _, _, _, _, _, _),
	[124] = PINGROUP(124, SOUTH, emac_phy, _, _, _, _, _, _, _, _),
	[125] = PINGROUP(125, WEST, hs3_mi2s, _, phase_flag, _, _, _, _, _, _),
	[126] = PINGROUP(126, WEST, sec_mi2s, qup2, _, _, _, _, _, _, _),
	[127] = PINGROUP(127, WEST, sec_mi2s, qup2, _, _, _, _, _, _, _),
	[128] = PINGROUP(128, WEST, sec_mi2s, qup2, _, _, _, _, _, _, _),
	[129] = PINGROUP(129, WEST, sec_mi2s, qup2, jitter_bist, _, _, _, _, _, _),
	[130] = PINGROUP(130, WEST, sec_mi2s, pll_bist, _, qdss_gpio, _, _, _, _, _),
	[131] = PINGROUP(131, WEST, ter_mi2s, gcc_gp1, _, _, _, _, _, _, _),
	[132] = PINGROUP(132, WEST, ter_mi2s, _, qdss_gpio, _, _, _, _, _, _),
	[133] = PINGROUP(133, WEST, ter_mi2s, _, qdss_gpio, atest_char, _, _, _, _, _),
	[134] = PINGROUP(134, WEST, ter_mi2s, _, qdss_gpio, atest_char, _, _, _, _, _),
	[135] = PINGROUP(135, WEST, ter_mi2s, _, qdss_gpio, atest_char, _, _, _, _, _),
	[136] = PINGROUP(136, WEST, qua_mi2s, gcc_gp1, _, _, _, _, _, _, _),
	[137] = PINGROUP(137, WEST, qua_mi2s, gcc_gp2, _, _, _, _, _, _, _),
	[138] = PINGROUP(138, WEST, qua_mi2s, gcc_gp3, _, _, _, _, _, _, _),
	[139] = PINGROUP(139, WEST, qua_mi2s, gcc_gp4, _, _, _, _, _, _, _),
	[140] = PINGROUP(140, WEST, qua_mi2s, gcc_gp5, _, atest_char, _, _, _, _, _),
	[141] = PINGROUP(141, WEST, qua_mi2s, qdss_cti, _, _, _, _, _, _, _),
	[142] = PINGROUP(142, WEST, qua_mi2s, _, _, qdss_cti, atest_char, _, _, _, _),
	[143] = PINGROUP(143, WEST, pri_mi2s, _, _, _, _, _, _, _, _),
	[144] = PINGROUP(144, WEST, pri_mi2s, qup3, _, _, _, _, _, _, _),
	[145] = PINGROUP(145, WEST, pri_mi2s_ws, qup3, ddr_bist, _, _, _, _, _, _),
	[146] = PINGROUP(146, WEST, pri_mi2s, qup3, ddr_bist, _, _, _, _, _, _),
	[147] = PINGROUP(147, WEST, pri_mi2s, qup3, _, _, _, _, _, _, _),
	[148] = PINGROUP(148, WEST, spkr_i2s, audio_ref, _, phase_flag, _, _, _, _, _),
	[149] = PINGROUP(149, WEST, lpass_slimbus, spkr_i2s, _, phase_flag, _, _, _, _, _),
	[150] = PINGROUP(150, WEST, lpass_slimbus, spkr_i2s, _, phase_flag, tsense_pwm1, tsense_pwm2, _, _, _),
	[151] = PINGROUP(151, WEST, lpass_slimbus, spkr_i2s, _, phase_flag, _, _, _, _, _),
	[152] = PINGROUP(152, WEST, lpass_slimbus, spkr_i2s, _, phase_flag, _, _, _, _, _),
	[153] = PINGROUP(153, WEST, btfm_slimbus, _, _, _, _, _, _, _, _),
	[154] = PINGROUP(154, WEST, btfm_slimbus, _, _, _, _, _, _, _, _),
	[155] = PINGROUP(155, WEST, hs1_mi2s, _, phase_flag, _, _, _, _, _, _),
	[156] = PINGROUP(156, WEST, hs1_mi2s, _, phase_flag, _, _, _, _, _, _),
	[157] = PINGROUP(157, WEST, hs1_mi2s, _, phase_flag, _, _, _, _, _, _),
	[158] = PINGROUP(158, WEST, hs1_mi2s, _, phase_flag, _, _, _, _, _, _),
	[159] = PINGROUP(159, WEST, hs1_mi2s, cri_trng, _, phase_flag, _, _, _, _, _),
	[160] = PINGROUP(160, WEST, hs2_mi2s, cri_trng, _, phase_flag, _, _, _, _, _),
	[161] = PINGROUP(161, WEST, hs2_mi2s, qspi1, cri_trng, _, phase_flag, _, _, _, _),
	[162] = PINGROUP(162, WEST, hs2_mi2s, qspi1, sp_cmu, _, phase_flag, _, _, _, _),
	[163] = PINGROUP(163, WEST, hs2_mi2s, qspi1_clk, prng_rosc, _, phase_flag, _, _, _, _),
	[164] = PINGROUP(164, WEST, hs2_mi2s, qspi1_cs, _, phase_flag, _, _, _, _, _),
	[165] = PINGROUP(165, WEST, hs3_mi2s, _, phase_flag, _, _, _, _, _, _),
	[166] = PINGROUP(166, WEST, hs3_mi2s, _, phase_flag, _, _, _, _, _, _),
	[167] = PINGROUP(167, WEST, hs3_mi2s, _, phase_flag, _, _, _, _, _, _),
	[168] = PINGROUP(168, WEST, hs3_mi2s, _, phase_flag, _, _, _, _, _, _),
	[169] = PINGROUP(169, SOUTH, _, _, _, _, _, _, _, _, _),
	[170] = PINGROUP(170, SOUTH, _, _, _, _, _, _, _, _, _),
	[171] = PINGROUP(171, SOUTH, _, _, _, _, _, _, _, _, _),
	[172] = PINGROUP(172, SOUTH, _, _, _, _, _, _, _, _, _),
	[173] = PINGROUP(173, SOUTH, _, _, _, _, _, _, _, _, _),
	[174] = PINGROUP(174, SOUTH, _, _, _, _, _, _, _, _, _),
	[175] = PINGROUP(175, SOUTH, pci_e2, _, _, _, _, _, _, _, _),
	[176] = PINGROUP(176, SOUTH, pci_e2, cci_async, _, _, _, _, _, _, _),
	[177] = PINGROUP_OFFSET(177, SOUTH, 0x1e000, _, _, _, _, _, _, _, _, _),
	[178] = PINGROUP_OFFSET(178, SOUTH, 0x1e000, pci_e3, cci_timer4, _, _, _, _, _, _, _),
	[179] = PINGROUP_OFFSET(179, SOUTH, 0x1e000, pci_e3, cam_mclk, _, _, _, _, _, _, _),
	[180] = PINGROUP_OFFSET(180, SOUTH, 0x1e000, cam_mclk, _, _, _, _, _, _, _, _),
	[181] = PINGROUP_OFFSET(181, SOUTH, 0x1e000, qup19, cam_mclk, _, _, _, _, _, _, _),
	[182] = PINGROUP_OFFSET(182, SOUTH, 0x1e000, qup19, cci_timer5, gcc_gp4, _, _, _, _, _, _),
	[183] = PINGROUP_OFFSET(183, SOUTH, 0x1e000, qup19, cci_timer6, gcc_gp5, _, _, _, _, _, _),
	[184] = PINGROUP_OFFSET(184, SOUTH, 0x1e000, qup19, cci_timer7, _, _, _, _, _, _, _),
	[185] = PINGROUP_OFFSET(185, SOUTH, 0x1e000, cci_timer8, cci_async, _, _, _, _, _, _, _),
	[186] = PINGROUP_OFFSET(186, SOUTH, 0x1e000, cci_timer9, cci_async, _, _, _, _, _, _, _),
	[187] = PINGROUP_OFFSET(187, SOUTH, 0x1e000, _, _, _, _, _, _, _, _, _),
	[188] = PINGROUP_OFFSET(188, SOUTH, 0x1e000, _, _, _, _, _, _, _, _, _),
	[189] = PINGROUP_OFFSET(189, SOUTH, 0x1e000, dp_hot, _, _, _, _, _, _, _, _),
	[190] = UFS_RESET(ufs_reset),
	[191] = SDC_QDSD_PINGROUP(sdc2_clk, 0x4b2000, 14, 6),
	[192] = SDC_QDSD_PINGROUP(sdc2_cmd, 0x4b2000, 11, 3),
	[193] = SDC_QDSD_PINGROUP(sdc2_data, 0x4b2000, 9, 0),
};

static const int sc8180x_acpi_reserved_gpios[] = {
	0, 1, 2, 3,
	47, 48, 49, 50,
	126, 127, 128, 129,
	-1 /* terminator */
};

static const struct msm_gpio_wakeirq_map sc8180x_pdc_map[] = {
	{ 3, 31 }, { 5, 32 }, { 8, 33 }, { 9, 34 }, { 10, 100 }, { 12, 104 },
	{ 24, 37 }, { 26, 38 }, { 27, 41 }, { 28, 42 }, { 30, 39 }, { 36, 43 },
	{ 37, 44 }, { 38, 45 }, { 39, 118 }, { 39, 125 }, { 41, 47 },
	{ 42, 48 }, { 46, 50 }, { 47, 49 }, { 48, 51 }, { 49, 53 }, { 50, 52 },
	{ 51, 116 }, { 51, 123 }, { 53, 54 }, { 54, 55 }, { 55, 56 },
	{ 56, 57 }, { 58, 58 }, { 60, 60 }, { 68, 62 }, { 70, 63 }, { 76, 86 },
	{ 77, 36 }, { 81, 64 }, { 83, 65 }, { 86, 67 }, { 87, 84 }, { 88, 117 },
	{ 88, 124 }, { 90, 69 }, { 91, 70 }, { 93, 75 }, { 95, 72 }, { 97, 74 },
	{ 101, 76 }, { 103, 77 }, { 104, 78 }, { 114, 82 }, { 117, 85 },
	{ 118, 101 }, { 119, 87 }, { 120, 88 }, { 121, 89 }, { 122, 90 },
	{ 123, 91 }, { 124, 92 }, { 125, 93 }, { 129, 94 }, { 132, 105 },
	{ 133, 35 }, { 134, 36 }, { 136, 97 }, { 142, 103 }, { 144, 115 },
	{ 144, 122 }, { 147, 106 }, { 150, 107 }, { 152, 108 }, { 153, 109 },
	{ 177, 111 }, { 180, 112 }, { 184, 113 }, { 189, 114 }
};

static struct msm_pinctrl_soc_data sc8180x_pinctrl = {
	.tiles = sc8180x_tiles,
	.ntiles = ARRAY_SIZE(sc8180x_tiles),
	.pins = sc8180x_pins,
	.npins = ARRAY_SIZE(sc8180x_pins),
	.functions = sc8180x_functions,
	.nfunctions = ARRAY_SIZE(sc8180x_functions),
	.groups = sc8180x_groups,
	.ngroups = ARRAY_SIZE(sc8180x_groups),
	.ngpios = 191,
	.wakeirq_map = sc8180x_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(sc8180x_pdc_map),
};

static const struct msm_pinctrl_soc_data sc8180x_acpi_pinctrl = {
	.tiles = sc8180x_tiles,
	.ntiles = ARRAY_SIZE(sc8180x_tiles),
	.pins = sc8180x_pins,
	.npins = ARRAY_SIZE(sc8180x_pins),
	.groups = sc8180x_groups,
	.ngroups = ARRAY_SIZE(sc8180x_groups),
	.reserved_gpios = sc8180x_acpi_reserved_gpios,
	.ngpios = 190,
};

/*
 * ACPI DSDT has one single memory resource for TLMM, which violates the
 * hardware layout of 3 separate tiles.  Let's split the memory resource into
 * 3 named ones, so that msm_pinctrl_probe() can map memory for ACPI in the
 * same way as for DT probe.
 */
static int sc8180x_pinctrl_add_tile_resources(struct platform_device *pdev)
{
	int nres_num = pdev->num_resources + ARRAY_SIZE(sc8180x_tiles) - 1;
	struct resource *mres = NULL;
	struct resource *nres, *res;
	int i, ret;

	/*
	 * DT already has tiles defined properly, so nothing needs to be done
	 * for DT probe.
	 */
	if (pdev->dev.of_node)
		return 0;

	/* Allocate for new resources */
	nres = devm_kzalloc(&pdev->dev, sizeof(*nres) * nres_num, GFP_KERNEL);
	if (!nres)
		return -ENOMEM;

	res = nres;

	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *r = &pdev->resource[i];

		/* Save memory resource and copy others */
		if (resource_type(r) == IORESOURCE_MEM)
			mres = r;
		else
			*res++ = *r;
	}

	if (!mres)
		return -EINVAL;

	/* Append tile memory resources */
	for (i = 0; i < ARRAY_SIZE(sc8180x_tiles); i++, res++) {
		const struct tile_info *info = &sc8180x_tile_info[i];

		res->start = mres->start + info->offset;
		res->end = mres->start + info->offset + info->size - 1;
		res->flags = mres->flags;
		res->name = sc8180x_tiles[i];

		/* Add new MEM to resource tree */
		insert_resource(mres->parent, res);
	}

	/* Remove old MEM from resource tree */
	remove_resource(mres);

	/* Free old resources and install new ones */
	ret = platform_device_add_resources(pdev, nres, nres_num);
	if (ret) {
		dev_err(&pdev->dev, "failed to add new resources: %d\n", ret);
		return ret;
	}

	return 0;
}

static int sc8180x_pinctrl_probe(struct platform_device *pdev)
{
	const struct msm_pinctrl_soc_data *soc_data;
	int ret;

	soc_data = device_get_match_data(&pdev->dev);
	if (!soc_data)
		return -EINVAL;

	ret = sc8180x_pinctrl_add_tile_resources(pdev);
	if (ret)
		return ret;

	return msm_pinctrl_probe(pdev, soc_data);
}

static const struct acpi_device_id sc8180x_pinctrl_acpi_match[] = {
	{
		.id = "QCOM040D",
		.driver_data = (kernel_ulong_t) &sc8180x_acpi_pinctrl,
	},
	{ }
};
MODULE_DEVICE_TABLE(acpi, sc8180x_pinctrl_acpi_match);

static const struct of_device_id sc8180x_pinctrl_of_match[] = {
	{
		.compatible = "qcom,sc8180x-tlmm",
		.data = &sc8180x_pinctrl,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sc8180x_pinctrl_of_match);

static struct platform_driver sc8180x_pinctrl_driver = {
	.driver = {
		.name = "sc8180x-pinctrl",
		.of_match_table = sc8180x_pinctrl_of_match,
		.acpi_match_table = sc8180x_pinctrl_acpi_match,
	},
	.probe = sc8180x_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init sc8180x_pinctrl_init(void)
{
	return platform_driver_register(&sc8180x_pinctrl_driver);
}
arch_initcall(sc8180x_pinctrl_init);

static void __exit sc8180x_pinctrl_exit(void)
{
	platform_driver_unregister(&sc8180x_pinctrl_driver);
}
module_exit(sc8180x_pinctrl_exit);

MODULE_DESCRIPTION("QTI SC8180x pinctrl driver");
MODULE_LICENSE("GPL v2");
