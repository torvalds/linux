// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

static const char * const sm8250_tiles[] = {
	"west",
	"south",
	"north",
};

enum {
	WEST,
	SOUTH,
	NORTH,
};

#define REG_SIZE 0x1000
#define PINGROUP(id, _tile, f1, f2, f3, f4, f5, f6, f7, f8, f9) \
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
		.ctl_reg = REG_SIZE * id,		\
		.io_reg = REG_SIZE * id + 0x4,		\
		.intr_cfg_reg = REG_SIZE * id + 0x8,	\
		.intr_status_reg = REG_SIZE * id + 0xc,	\
		.intr_target_reg = REG_SIZE * id + 0x8,	\
		.tile = _tile,				\
		.mux_bit = 2,				\
		.pull_bit = 0,				\
		.drv_bit = 6,				\
		.egpio_enable = 12,			\
		.egpio_present = 11,			\
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

#define SDC_PINGROUP(pg_name, ctl, pull, drv)	\
	{						\
		.grp = PINCTRL_PINGROUP(#pg_name, 	\
			pg_name##_pins, 		\
			ARRAY_SIZE(pg_name##_pins)),	\
		.ctl_reg = ctl,				\
		.io_reg = 0,				\
		.intr_cfg_reg = 0,			\
		.intr_status_reg = 0,			\
		.intr_target_reg = 0,			\
		.tile = NORTH,				\
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

static const struct pinctrl_pin_desc sm8250_pins[] = {
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
	PINCTRL_PIN(180, "SDC2_CLK"),
	PINCTRL_PIN(181, "SDC2_CMD"),
	PINCTRL_PIN(182, "SDC2_DATA"),
	PINCTRL_PIN(183, "UFS_RESET"),
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

static const unsigned int ufs_reset_pins[] = { 180 };
static const unsigned int sdc2_clk_pins[] = { 181 };
static const unsigned int sdc2_cmd_pins[] = { 182 };
static const unsigned int sdc2_data_pins[] = { 183 };

enum sm8250_functions {
	msm_mux_aoss_cti,
	msm_mux_atest,
	msm_mux_audio_ref,
	msm_mux_cam_mclk,
	msm_mux_cci_async,
	msm_mux_cci_i2c,
	msm_mux_cci_timer0,
	msm_mux_cci_timer1,
	msm_mux_cci_timer2,
	msm_mux_cci_timer3,
	msm_mux_cci_timer4,
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
	msm_mux_egpio,
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
	msm_mux_pci_e0,
	msm_mux_pci_e1,
	msm_mux_pci_e2,
	msm_mux_phase_flag,
	msm_mux_pll_bist,
	msm_mux_pll_bypassnl,
	msm_mux_pll_clk,
	msm_mux_pll_reset,
	msm_mux_pri_mi2s,
	msm_mux_prng_rosc,
	msm_mux_qdss_cti,
	msm_mux_qdss_gpio,
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
	msm_mux_sp_cmu,
	msm_mux_tgu_ch0,
	msm_mux_tgu_ch1,
	msm_mux_tgu_ch2,
	msm_mux_tgu_ch3,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_tsif0_clk,
	msm_mux_tsif0_data,
	msm_mux_tsif0_en,
	msm_mux_tsif0_error,
	msm_mux_tsif0_sync,
	msm_mux_tsif1_clk,
	msm_mux_tsif1_data,
	msm_mux_tsif1_en,
	msm_mux_tsif1_error,
	msm_mux_tsif1_sync,
	msm_mux_usb2phy_ac,
	msm_mux_usb_phy,
	msm_mux_vsense_trigger,
	msm_mux__,
};

static const char * const tsif1_data_groups[] = {
	"gpio75",
};
static const char * const sdc41_groups[] = {
	"gpio75",
};
static const char * const tsif1_sync_groups[] = {
	"gpio76",
};
static const char * const sdc40_groups[] = {
	"gpio76",
};
static const char * const aoss_cti_groups[] = {
	"gpio77",
};
static const char * const phase_flag_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48", "gpio49", "gpio50", "gpio51",
	"gpio69", "gpio70", "gpio71", "gpio72", "gpio73", "gpio74", "gpio77",
	"gpio78", "gpio79", "gpio80", "gpio81", "gpio82", "gpio83", "gpio84",
	"gpio103", "gpio104", "gpio115", "gpio116", "gpio117", "gpio118",
	"gpio119", "gpio120", "gpio122", "gpio124", "gpio125",
};
static const char * const sd_write_groups[] = {
	"gpio78",
};
static const char * const pci_e0_groups[] = {
	"gpio79", "gpio80",
};
static const char * const pci_e1_groups[] = {
	"gpio82", "gpio83",
};
static const char * const pci_e2_groups[] = {
	"gpio85", "gpio86",
};
static const char * const tgu_ch0_groups[] = {
	"gpio85",
};
static const char * const atest_groups[] = {
	"gpio24", "gpio25", "gpio26", "gpio27", "gpio32", "gpio33", "gpio34",
	"gpio35", "gpio36", "gpio37", "gpio85", "gpio86", "gpio87", "gpio88",
	"gpio89",
};
static const char * const tgu_ch3_groups[] = {
	"gpio86",
};
static const char * const tsif1_error_groups[] = {
	"gpio90",
};
static const char * const tgu_ch1_groups[] = {
	"gpio90",
};
static const char * const tsif0_error_groups[] = {
	"gpio91",
};
static const char * const tgu_ch2_groups[] = {
	"gpio91",
};
static const char * const cam_mclk_groups[] = {
	"gpio94", "gpio95", "gpio96", "gpio97", "gpio98", "gpio99", "gpio100",
};
static const char * const ddr_bist_groups[] = {
	"gpio94", "gpio95", "gpio143", "gpio144",
};
static const char * const pll_bypassnl_groups[] = {
	"gpio96",
};
static const char * const pll_reset_groups[] = {
	"gpio97",
};
static const char * const cci_i2c_groups[] = {
	"gpio101", "gpio102", "gpio103", "gpio104", "gpio105", "gpio106",
	"gpio107", "gpio108",
};
static const char * const qdss_gpio_groups[] = {
	"gpio94", "gpio95", "gpio96", "gpio97", "gpio98", "gpio99", "gpio100",
	"gpio101", "gpio102", "gpio103", "gpio104", "gpio105", "gpio106",
	"gpio107", "gpio108", "gpio109", "gpio110", "gpio111", "gpio160",
	"gpio161", "gpio162", "gpio163", "gpio164", "gpio165", "gpio166",
	"gpio167", "gpio168", "gpio169", "gpio170", "gpio171", "gpio172",
	"gpio173", "gpio174", "gpio175", "gpio176", "gpio177",
};
static const char * const gcc_gp1_groups[] = {
	"gpio106", "gpio136",
};
static const char * const gcc_gp2_groups[] = {
	"gpio107", "gpio137",
};
static const char * const gcc_gp3_groups[] = {
	"gpio108", "gpio138",
};
static const char * const cci_timer0_groups[] = {
	"gpio109",
};
static const char * const cci_timer1_groups[] = {
	"gpio110",
};
static const char * const cci_timer2_groups[] = {
	"gpio111",
};
static const char * const cci_timer3_groups[] = {
	"gpio112",
};
static const char * const cci_async_groups[] = {
	"gpio112", "gpio113", "gpio114",
};
static const char * const cci_timer4_groups[] = {
	"gpio113",
};
static const char * const qup2_groups[] = {
	"gpio115", "gpio116", "gpio117", "gpio118",
};
static const char * const qup3_groups[] = {
	"gpio119", "gpio120", "gpio121", "gpio122",
};
static const char * const tsense_pwm1_groups[] = {
	"gpio123",
};
static const char * const tsense_pwm2_groups[] = {
	"gpio123",
};
static const char * const qup9_groups[] = {
	"gpio125", "gpio126", "gpio127", "gpio128",
};
static const char * const qup10_groups[] = {
	"gpio129", "gpio130", "gpio131", "gpio132",
};
static const char * const mi2s2_sck_groups[] = {
	"gpio133",
};
static const char * const mi2s2_data0_groups[] = {
	"gpio134",
};
static const char * const mi2s2_ws_groups[] = {
	"gpio135",
};
static const char * const pri_mi2s_groups[] = {
	"gpio136",
};
static const char * const sec_mi2s_groups[] = {
	"gpio137",
};
static const char * const audio_ref_groups[] = {
	"gpio137",
};
static const char * const mi2s2_data1_groups[] = {
	"gpio137",
};
static const char * const mi2s0_sck_groups[] = {
	"gpio138",
};
static const char * const mi2s0_data0_groups[] = {
	"gpio139",
};
static const char * const mi2s0_data1_groups[] = {
	"gpio140",
};
static const char * const mi2s0_ws_groups[] = {
	"gpio141",
};
static const char * const lpass_slimbus_groups[] = {
	"gpio142", "gpio143", "gpio144", "gpio145",
};
static const char * const mi2s1_sck_groups[] = {
	"gpio142",
};
static const char * const mi2s1_data0_groups[] = {
	"gpio143",
};
static const char * const mi2s1_data1_groups[] = {
	"gpio144",
};
static const char * const mi2s1_ws_groups[] = {
	"gpio145",
};
static const char * const cri_trng0_groups[] = {
	"gpio159",
};
static const char * const cri_trng1_groups[] = {
	"gpio160",
};
static const char * const cri_trng_groups[] = {
	"gpio161",
};
static const char * const sp_cmu_groups[] = {
	"gpio162",
};
static const char * const prng_rosc_groups[] = {
	"gpio163",
};
static const char * const qup19_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
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
	"gpio177", "gpio178", "gpio179",
};
static const char * const egpio_groups[] = {
	"gpio146", "gpio147", "gpio148", "gpio149", "gpio150", "gpio151",
	"gpio152", "gpio153", "gpio154", "gpio155", "gpio156", "gpio157",
	"gpio158", "gpio159", "gpio160", "gpio161", "gpio162", "gpio163",
	"gpio164", "gpio165", "gpio166", "gpio167", "gpio168", "gpio169",
	"gpio170", "gpio171", "gpio172", "gpio173", "gpio174", "gpio175",
	"gpio176", "gpio177", "gpio178", "gpio179",
};
static const char * const qdss_cti_groups[] = {
	"gpio0", "gpio2", "gpio2", "gpio44", "gpio45", "gpio46", "gpio92",
	"gpio93",
};
static const char * const qup1_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const ibi_i3c_groups[] = {
	"gpio4", "gpio5", "gpio24", "gpio25", "gpio28", "gpio29", "gpio40",
	"gpio41",
};
static const char * const qup_l4_groups[] = {
	"gpio6", "gpio14", "gpio46", "gpio123",
};
static const char * const qup_l5_groups[] = {
	"gpio7", "gpio15", "gpio47", "gpio124",
};
static const char * const qup4_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};
static const char * const qup5_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15",
};
static const char * const qup6_groups[] = {
	"gpio16", "gpio17", "gpio18", "gpio19",
};
static const char * const qup7_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23",
};
static const char * const qup8_groups[] = {
	"gpio24", "gpio25", "gpio26", "gpio27",
};
static const char * const qup0_groups[] = {
	"gpio28", "gpio29", "gpio30", "gpio31",
};
static const char * const qup12_groups[] = {
	"gpio32", "gpio33", "gpio34", "gpio35",
};
static const char * const qup13_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};
static const char * const qup14_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43",
};
static const char * const ddr_pxi3_groups[] = {
	"gpio40", "gpio43",
};
static const char * const ddr_pxi1_groups[] = {
	"gpio41", "gpio42",
};
static const char * const vsense_trigger_groups[] = {
	"gpio42",
};
static const char * const qup15_groups[] = {
	"gpio44", "gpio45", "gpio46", "gpio47",
};
static const char * const dbg_out_groups[] = {
	"gpio44",
};
static const char * const qup16_groups[] = {
	"gpio48", "gpio49", "gpio50", "gpio51",
};
static const char * const qup17_groups[] = {
	"gpio52", "gpio53", "gpio54", "gpio55",
};
static const char * const ddr_pxi0_groups[] = {
	"gpio52", "gpio53",
};
static const char * const jitter_bist_groups[] = {
	"gpio54",
};
static const char * const pll_bist_groups[] = {
	"gpio55",
};
static const char * const ddr_pxi2_groups[] = {
	"gpio55", "gpio56",
};
static const char * const qup18_groups[] = {
	"gpio56", "gpio57", "gpio58", "gpio59",
};
static const char * const qup11_groups[] = {
	"gpio60", "gpio61", "gpio62", "gpio63",
};
static const char * const usb2phy_ac_groups[] = {
	"gpio64", "gpio90",
};
static const char * const qup_l6_groups[] = {
	"gpio64", "gpio77", "gpio92", "gpio93",
};
static const char * const usb_phy_groups[] = {
	"gpio65",
};
static const char * const pll_clk_groups[] = {
	"gpio65",
};
static const char * const mdp_vsync_groups[] = {
	"gpio66", "gpio67", "gpio68", "gpio122", "gpio124",
};
static const char * const dp_lcd_groups[] = {
	"gpio67",
};
static const char * const dp_hot_groups[] = {
	"gpio68",
};
static const char * const qspi_cs_groups[] = {
	"gpio69", "gpio75",
};
static const char * const tsif0_clk_groups[] = {
	"gpio69",
};
static const char * const qspi0_groups[] = {
	"gpio70",
};
static const char * const tsif0_en_groups[] = {
	"gpio70",
};
static const char * const mdp_vsync0_groups[] = {
	"gpio70",
};
static const char * const mdp_vsync1_groups[] = {
	"gpio70",
};
static const char * const mdp_vsync2_groups[] = {
	"gpio70",
};
static const char * const mdp_vsync3_groups[] = {
	"gpio70",
};
static const char * const qspi1_groups[] = {
	"gpio71",
};
static const char * const tsif0_data_groups[] = {
	"gpio71",
};
static const char * const sdc4_cmd_groups[] = {
	"gpio71",
};
static const char * const qspi2_groups[] = {
	"gpio72",
};
static const char * const tsif0_sync_groups[] = {
	"gpio72",
};
static const char * const sdc43_groups[] = {
	"gpio72",
};
static const char * const qspi_clk_groups[] = {
	"gpio73",
};
static const char * const tsif1_clk_groups[] = {
	"gpio73",
};
static const char * const sdc4_clk_groups[] = {
	"gpio73",
};
static const char * const qspi3_groups[] = {
	"gpio74",
};
static const char * const tsif1_en_groups[] = {
	"gpio74",
};
static const char * const sdc42_groups[] = {
	"gpio74",
};

static const struct pinfunction sm8250_functions[] = {
	MSM_PIN_FUNCTION(aoss_cti),
	MSM_PIN_FUNCTION(atest),
	MSM_PIN_FUNCTION(audio_ref),
	MSM_PIN_FUNCTION(cam_mclk),
	MSM_PIN_FUNCTION(cci_async),
	MSM_PIN_FUNCTION(cci_i2c),
	MSM_PIN_FUNCTION(cci_timer0),
	MSM_PIN_FUNCTION(cci_timer1),
	MSM_PIN_FUNCTION(cci_timer2),
	MSM_PIN_FUNCTION(cci_timer3),
	MSM_PIN_FUNCTION(cci_timer4),
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
	MSM_PIN_FUNCTION(egpio),
	MSM_PIN_FUNCTION(gcc_gp1),
	MSM_PIN_FUNCTION(gcc_gp2),
	MSM_PIN_FUNCTION(gcc_gp3),
	MSM_GPIO_PIN_FUNCTION(gpio),
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
	MSM_PIN_FUNCTION(pci_e0),
	MSM_PIN_FUNCTION(pci_e1),
	MSM_PIN_FUNCTION(pci_e2),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(pll_bist),
	MSM_PIN_FUNCTION(pll_bypassnl),
	MSM_PIN_FUNCTION(pll_clk),
	MSM_PIN_FUNCTION(pll_reset),
	MSM_PIN_FUNCTION(pri_mi2s),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qdss_gpio),
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
	MSM_PIN_FUNCTION(sp_cmu),
	MSM_PIN_FUNCTION(tgu_ch0),
	MSM_PIN_FUNCTION(tgu_ch1),
	MSM_PIN_FUNCTION(tgu_ch2),
	MSM_PIN_FUNCTION(tgu_ch3),
	MSM_PIN_FUNCTION(tsense_pwm1),
	MSM_PIN_FUNCTION(tsense_pwm2),
	MSM_PIN_FUNCTION(tsif0_clk),
	MSM_PIN_FUNCTION(tsif0_data),
	MSM_PIN_FUNCTION(tsif0_en),
	MSM_PIN_FUNCTION(tsif0_error),
	MSM_PIN_FUNCTION(tsif0_sync),
	MSM_PIN_FUNCTION(tsif1_clk),
	MSM_PIN_FUNCTION(tsif1_data),
	MSM_PIN_FUNCTION(tsif1_en),
	MSM_PIN_FUNCTION(tsif1_error),
	MSM_PIN_FUNCTION(tsif1_sync),
	MSM_PIN_FUNCTION(usb2phy_ac),
	MSM_PIN_FUNCTION(usb_phy),
	MSM_PIN_FUNCTION(vsense_trigger),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup sm8250_groups[] = {
	[0] = PINGROUP(0, SOUTH, qup19, qdss_cti, _, _, _, _, _, _, _),
	[1] = PINGROUP(1, SOUTH, qup19, _, _, _, _, _, _, _, _),
	[2] = PINGROUP(2, SOUTH, qup19, qdss_cti, qdss_cti, _, _, _, _, _, _),
	[3] = PINGROUP(3, SOUTH, qup19, _, _, _, _, _, _, _, _),
	[4] = PINGROUP(4, NORTH, qup1, ibi_i3c, _, _, _, _, _, _, _),
	[5] = PINGROUP(5, NORTH, qup1, ibi_i3c, _, _, _, _, _, _, _),
	[6] = PINGROUP(6, NORTH, qup1, qup_l4, _, _, _, _, _, _, _),
	[7] = PINGROUP(7, NORTH, qup1, qup_l5, _, _, _, _, _, _, _),
	[8] = PINGROUP(8, NORTH, qup4, _, _, _, _, _, _, _, _),
	[9] = PINGROUP(9, NORTH, qup4, _, _, _, _, _, _, _, _),
	[10] = PINGROUP(10, NORTH, qup4, _, _, _, _, _, _, _, _),
	[11] = PINGROUP(11, NORTH, qup4, _, _, _, _, _, _, _, _),
	[12] = PINGROUP(12, NORTH, qup5, _, _, _, _, _, _, _, _),
	[13] = PINGROUP(13, NORTH, qup5, _, _, _, _, _, _, _, _),
	[14] = PINGROUP(14, NORTH, qup5, qup_l4, _, _, _, _, _, _, _),
	[15] = PINGROUP(15, NORTH, qup5, qup_l5, _, _, _, _, _, _, _),
	[16] = PINGROUP(16, NORTH, qup6, _, _, _, _, _, _, _, _),
	[17] = PINGROUP(17, NORTH, qup6, _, _, _, _, _, _, _, _),
	[18] = PINGROUP(18, NORTH, qup6, _, _, _, _, _, _, _, _),
	[19] = PINGROUP(19, NORTH, qup6, _, _, _, _, _, _, _, _),
	[20] = PINGROUP(20, NORTH, qup7, _, _, _, _, _, _, _, _),
	[21] = PINGROUP(21, NORTH, qup7, _, _, _, _, _, _, _, _),
	[22] = PINGROUP(22, NORTH, qup7, _, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, NORTH, qup7, _, _, _, _, _, _, _, _),
	[24] = PINGROUP(24, SOUTH, qup8, ibi_i3c, atest, _, _, _, _, _, _),
	[25] = PINGROUP(25, SOUTH, qup8, ibi_i3c, atest, _, _, _, _, _, _),
	[26] = PINGROUP(26, SOUTH, qup8, atest, _, _, _, _, _, _, _),
	[27] = PINGROUP(27, SOUTH, qup8, atest, _, _, _, _, _, _, _),
	[28] = PINGROUP(28, NORTH, qup0, ibi_i3c, _, _, _, _, _, _, _),
	[29] = PINGROUP(29, NORTH, qup0, ibi_i3c, _, _, _, _, _, _, _),
	[30] = PINGROUP(30, NORTH, qup0, _, _, _, _, _, _, _, _),
	[31] = PINGROUP(31, NORTH, qup0, _, _, _, _, _, _, _, _),
	[32] = PINGROUP(32, SOUTH, qup12, _, atest, _, _, _, _, _, _),
	[33] = PINGROUP(33, SOUTH, qup12, atest, _, _, _, _, _, _, _),
	[34] = PINGROUP(34, SOUTH, qup12, atest, _, _, _, _, _, _, _),
	[35] = PINGROUP(35, SOUTH, qup12, atest, _, _, _, _, _, _, _),
	[36] = PINGROUP(36, SOUTH, qup13, atest, _, _, _, _, _, _, _),
	[37] = PINGROUP(37, SOUTH, qup13, atest, _, _, _, _, _, _, _),
	[38] = PINGROUP(38, SOUTH, qup13, _, _, _, _, _, _, _, _),
	[39] = PINGROUP(39, SOUTH, qup13, _, _, _, _, _, _, _, _),
	[40] = PINGROUP(40, SOUTH, qup14, ibi_i3c, _, ddr_pxi3, _, _, _, _, _),
	[41] = PINGROUP(41, SOUTH, qup14, ibi_i3c, _, ddr_pxi1, _, _, _, _, _),
	[42] = PINGROUP(42, SOUTH, qup14, vsense_trigger, ddr_pxi1, _, _, _, _, _, _),
	[43] = PINGROUP(43, SOUTH, qup14, ddr_pxi3, _, _, _, _, _, _, _),
	[44] = PINGROUP(44, SOUTH, qup15, qdss_cti, dbg_out, _, _, _, _, _, _),
	[45] = PINGROUP(45, SOUTH, qup15, qdss_cti, phase_flag, _, _, _, _, _, _),
	[46] = PINGROUP(46, SOUTH, qup15, qup_l4, qdss_cti, phase_flag, _, _, _, _, _),
	[47] = PINGROUP(47, SOUTH, qup15, qup_l5, phase_flag, _, _, _, _, _, _),
	[48] = PINGROUP(48, SOUTH, qup16, phase_flag, _, _, _, _, _, _, _),
	[49] = PINGROUP(49, SOUTH, qup16, phase_flag, _, _, _, _, _, _, _),
	[50] = PINGROUP(50, SOUTH, qup16, phase_flag, _, _, _, _, _, _, _),
	[51] = PINGROUP(51, SOUTH, qup16, phase_flag, _, _, _, _, _, _, _),
	[52] = PINGROUP(52, SOUTH, qup17, ddr_pxi0, _, _, _, _, _, _, _),
	[53] = PINGROUP(53, SOUTH, qup17, ddr_pxi0, _, _, _, _, _, _, _),
	[54] = PINGROUP(54, SOUTH, qup17, jitter_bist, _, _, _, _, _, _, _),
	[55] = PINGROUP(55, SOUTH, qup17, pll_bist, ddr_pxi2, _, _, _, _, _, _),
	[56] = PINGROUP(56, SOUTH, qup18, ddr_pxi2, _, _, _, _, _, _, _),
	[57] = PINGROUP(57, SOUTH, qup18, _, _, _, _, _, _, _, _),
	[58] = PINGROUP(58, SOUTH, qup18, _, _, _, _, _, _, _, _),
	[59] = PINGROUP(59, SOUTH, qup18, _, _, _, _, _, _, _, _),
	[60] = PINGROUP(60, SOUTH, qup11, _, _, _, _, _, _, _, _),
	[61] = PINGROUP(61, SOUTH, qup11, _, _, _, _, _, _, _, _),
	[62] = PINGROUP(62, SOUTH, qup11, _, _, _, _, _, _, _, _),
	[63] = PINGROUP(63, SOUTH, qup11, _, _, _, _, _, _, _, _),
	[64] = PINGROUP(64, SOUTH, usb2phy_ac, qup_l6, _, _, _, _, _, _, _),
	[65] = PINGROUP(65, SOUTH, usb_phy, pll_clk, _, _, _, _, _, _, _),
	[66] = PINGROUP(66, NORTH, mdp_vsync, _, _, _, _, _, _, _, _),
	[67] = PINGROUP(67, NORTH, mdp_vsync, dp_lcd, _, _, _, _, _, _, _),
	[68] = PINGROUP(68, NORTH, mdp_vsync, dp_hot, _, _, _, _, _, _, _),
	[69] = PINGROUP(69, SOUTH, qspi_cs, tsif0_clk, phase_flag, _, _, _, _, _, _),
	[70] = PINGROUP(70, SOUTH, qspi0, tsif0_en, mdp_vsync0, mdp_vsync1, mdp_vsync2, mdp_vsync3, phase_flag, _, _),
	[71] = PINGROUP(71, SOUTH, qspi1, tsif0_data, sdc4_cmd, phase_flag, _, _, _, _, _),
	[72] = PINGROUP(72, SOUTH, qspi2, tsif0_sync, sdc43, phase_flag, _, _, _, _, _),
	[73] = PINGROUP(73, SOUTH, qspi_clk, tsif1_clk, sdc4_clk, phase_flag, _, _, _, _, _),
	[74] = PINGROUP(74, SOUTH, qspi3, tsif1_en, sdc42, phase_flag, _, _, _, _, _),
	[75] = PINGROUP(75, SOUTH, qspi_cs, tsif1_data, sdc41, _, _, _, _, _, _),
	[76] = PINGROUP(76, SOUTH, tsif1_sync, sdc40, _, _, _, _, _, _, _),
	[77] = PINGROUP(77, NORTH, qup_l6, aoss_cti, phase_flag, _, _, _, _, _, _),
	[78] = PINGROUP(78, NORTH, sd_write, phase_flag, _, _, _, _, _, _, _),
	[79] = PINGROUP(79, NORTH, pci_e0, phase_flag, _, _, _, _, _, _, _),
	[80] = PINGROUP(80, NORTH, pci_e0, phase_flag, _, _, _, _, _, _, _),
	[81] = PINGROUP(81, NORTH, phase_flag, _, _, _, _, _, _, _, _),
	[82] = PINGROUP(82, NORTH, pci_e1, phase_flag, _, _, _, _, _, _, _),
	[83] = PINGROUP(83, NORTH, pci_e1, phase_flag, _, _, _, _, _, _, _),
	[84] = PINGROUP(84, NORTH, phase_flag, _, _, _, _, _, _, _, _),
	[85] = PINGROUP(85, SOUTH, pci_e2, tgu_ch0, atest, _, _, _, _, _, _),
	[86] = PINGROUP(86, SOUTH, pci_e2, tgu_ch3, atest, _, _, _, _, _, _),
	[87] = PINGROUP(87, SOUTH, atest, _, _, _, _, _, _, _, _),
	[88] = PINGROUP(88, SOUTH, _, atest, _, _, _, _, _, _, _),
	[89] = PINGROUP(89, SOUTH, _, atest, _, _, _, _, _, _, _),
	[90] = PINGROUP(90, SOUTH, tsif1_error, usb2phy_ac, tgu_ch1, _, _, _, _, _, _),
	[91] = PINGROUP(91, SOUTH, tsif0_error, tgu_ch2, _, _, _, _, _, _, _),
	[92] = PINGROUP(92, NORTH, qup_l6, qdss_cti, _, _, _, _, _, _, _),
	[93] = PINGROUP(93, NORTH, qup_l6, qdss_cti, _, _, _, _, _, _, _),
	[94] = PINGROUP(94, NORTH, cam_mclk, ddr_bist, qdss_gpio, _, _, _, _, _, _),
	[95] = PINGROUP(95, NORTH, cam_mclk, ddr_bist, qdss_gpio, _, _, _, _, _, _),
	[96] = PINGROUP(96, NORTH, cam_mclk, pll_bypassnl, qdss_gpio, _, _, _, _, _, _),
	[97] = PINGROUP(97, NORTH, cam_mclk, pll_reset, qdss_gpio, _, _, _, _, _, _),
	[98] = PINGROUP(98, NORTH, cam_mclk, qdss_gpio, _, _, _, _, _, _, _),
	[99] = PINGROUP(99, NORTH, cam_mclk, qdss_gpio, _, _, _, _, _, _, _),
	[100] = PINGROUP(100, NORTH, cam_mclk, qdss_gpio, _, _, _, _, _, _, _),
	[101] = PINGROUP(101, NORTH, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[102] = PINGROUP(102, NORTH, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[103] = PINGROUP(103, NORTH, cci_i2c, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[104] = PINGROUP(104, NORTH, cci_i2c, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[105] = PINGROUP(105, NORTH, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[106] = PINGROUP(106, NORTH, cci_i2c, gcc_gp1, qdss_gpio, _, _, _, _, _, _),
	[107] = PINGROUP(107, NORTH, cci_i2c, gcc_gp2, qdss_gpio, _, _, _, _, _, _),
	[108] = PINGROUP(108, NORTH, cci_i2c, gcc_gp3, qdss_gpio, _, _, _, _, _, _),
	[109] = PINGROUP(109, NORTH, cci_timer0, qdss_gpio, _, _, _, _, _, _, _),
	[110] = PINGROUP(110, NORTH, cci_timer1, qdss_gpio, _, _, _, _, _, _, _),
	[111] = PINGROUP(111, NORTH, cci_timer2, qdss_gpio, _, _, _, _, _, _, _),
	[112] = PINGROUP(112, NORTH, cci_timer3, cci_async, _, _, _, _, _, _, _),
	[113] = PINGROUP(113, NORTH, cci_timer4, cci_async, _, _, _, _, _, _, _),
	[114] = PINGROUP(114, NORTH, cci_async, _, _, _, _, _, _, _, _),
	[115] = PINGROUP(115, NORTH, qup2, phase_flag, _, _, _, _, _, _, _),
	[116] = PINGROUP(116, NORTH, qup2, phase_flag, _, _, _, _, _, _, _),
	[117] = PINGROUP(117, NORTH, qup2, phase_flag, _, _, _, _, _, _, _),
	[118] = PINGROUP(118, NORTH, qup2, phase_flag, _, _, _, _, _, _, _),
	[119] = PINGROUP(119, NORTH, qup3, phase_flag, _, _, _, _, _, _, _),
	[120] = PINGROUP(120, NORTH, qup3, phase_flag, _, _, _, _, _, _, _),
	[121] = PINGROUP(121, NORTH, qup3, _, _, _, _, _, _, _, _),
	[122] = PINGROUP(122, NORTH, qup3, mdp_vsync, phase_flag, _, _, _, _, _, _),
	[123] = PINGROUP(123, NORTH, qup_l4, tsense_pwm1, tsense_pwm2, _, _, _, _, _, _),
	[124] = PINGROUP(124, NORTH, qup_l5, mdp_vsync, phase_flag, _, _, _, _, _, _),
	[125] = PINGROUP(125, SOUTH, qup9, phase_flag, _, _, _, _, _, _, _),
	[126] = PINGROUP(126, SOUTH, qup9, _, _, _, _, _, _, _, _),
	[127] = PINGROUP(127, SOUTH, qup9, _, _, _, _, _, _, _, _),
	[128] = PINGROUP(128, SOUTH, qup9, _, _, _, _, _, _, _, _),
	[129] = PINGROUP(129, SOUTH, qup10, _, _, _, _, _, _, _, _),
	[130] = PINGROUP(130, SOUTH, qup10, _, _, _, _, _, _, _, _),
	[131] = PINGROUP(131, SOUTH, qup10, _, _, _, _, _, _, _, _),
	[132] = PINGROUP(132, SOUTH, qup10, _, _, _, _, _, _, _, _),
	[133] = PINGROUP(133, WEST, mi2s2_sck, _, _, _, _, _, _, _, _),
	[134] = PINGROUP(134, WEST, mi2s2_data0, _, _, _, _, _, _, _, _),
	[135] = PINGROUP(135, WEST, mi2s2_ws, _, _, _, _, _, _, _, _),
	[136] = PINGROUP(136, WEST, pri_mi2s, gcc_gp1, _, _, _, _, _, _, _),
	[137] = PINGROUP(137, WEST, sec_mi2s, audio_ref, mi2s2_data1, gcc_gp2, _, _, _, _, _),
	[138] = PINGROUP(138, WEST, mi2s0_sck, gcc_gp3, _, _, _, _, _, _, _),
	[139] = PINGROUP(139, WEST, mi2s0_data0, _, _, _, _, _, _, _, _),
	[140] = PINGROUP(140, WEST, mi2s0_data1, _, _, _, _, _, _, _, _),
	[141] = PINGROUP(141, WEST, mi2s0_ws, _, _, _, _, _, _, _, _),
	[142] = PINGROUP(142, WEST, lpass_slimbus, mi2s1_sck, _, _, _, _, _, _, _),
	[143] = PINGROUP(143, WEST, lpass_slimbus, mi2s1_data0, ddr_bist, _, _, _, _, _, _),
	[144] = PINGROUP(144, WEST, lpass_slimbus, mi2s1_data1, ddr_bist, _, _, _, _, _, _),
	[145] = PINGROUP(145, WEST, lpass_slimbus, mi2s1_ws, _, _, _, _, _, _, _),
	[146] = PINGROUP(146, WEST, _, _, _, _, _, _, _, _, egpio),
	[147] = PINGROUP(147, WEST, _, _, _, _, _, _, _, _, egpio),
	[148] = PINGROUP(148, WEST, _, _, _, _, _, _, _, _, egpio),
	[149] = PINGROUP(149, WEST, _, _, _, _, _, _, _, _, egpio),
	[150] = PINGROUP(150, WEST, _, _, _, _, _, _, _, _, egpio),
	[151] = PINGROUP(151, WEST, _, _, _, _, _, _, _, _, egpio),
	[152] = PINGROUP(152, WEST, _, _, _, _, _, _, _, _, egpio),
	[153] = PINGROUP(153, WEST, _, _, _, _, _, _, _, _, egpio),
	[154] = PINGROUP(154, WEST, _, _, _, _, _, _, _, _, egpio),
	[155] = PINGROUP(155, WEST, _, _, _, _, _, _, _, _, egpio),
	[156] = PINGROUP(156, WEST, _, _, _, _, _, _, _, _, egpio),
	[157] = PINGROUP(157, WEST, _, _, _, _, _, _, _, _, egpio),
	[158] = PINGROUP(158, WEST, _, _, _, _, _, _, _, _, egpio),
	[159] = PINGROUP(159, WEST, cri_trng0, _, _, _, _, _, _, _, egpio),
	[160] = PINGROUP(160, WEST, cri_trng1, qdss_gpio, _, _, _, _, _, _, egpio),
	[161] = PINGROUP(161, WEST, cri_trng, qdss_gpio, _, _, _, _, _, _, egpio),
	[162] = PINGROUP(162, WEST, sp_cmu, qdss_gpio, _, _, _, _, _, _, egpio),
	[163] = PINGROUP(163, WEST, prng_rosc, qdss_gpio, _, _, _, _, _, _, egpio),
	[164] = PINGROUP(164, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[165] = PINGROUP(165, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[166] = PINGROUP(166, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[167] = PINGROUP(167, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[168] = PINGROUP(168, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[169] = PINGROUP(169, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[170] = PINGROUP(170, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[171] = PINGROUP(171, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[172] = PINGROUP(172, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[173] = PINGROUP(173, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[174] = PINGROUP(174, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[175] = PINGROUP(175, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[176] = PINGROUP(176, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[177] = PINGROUP(177, WEST, qdss_gpio, _, _, _, _, _, _, _, egpio),
	[178] = PINGROUP(178, WEST, _, _, _, _, _, _, _, _, egpio),
	[179] = PINGROUP(179, WEST, _, _, _, _, _, _, _, _, egpio),
	[180] = UFS_RESET(ufs_reset, 0xb8000),
	[181] = SDC_PINGROUP(sdc2_clk, 0xb7000, 14, 6),
	[182] = SDC_PINGROUP(sdc2_cmd, 0xb7000, 11, 3),
	[183] = SDC_PINGROUP(sdc2_data, 0xb7000, 9, 0),
};

static const struct msm_gpio_wakeirq_map sm8250_pdc_map[] = {
	{ 0, 79 }, { 1, 84 }, { 2, 80 }, { 3, 82 }, { 4, 107 }, { 7, 43 },
	{ 11, 42 }, { 14, 44 }, { 15, 52 }, { 19, 67 }, { 23, 68 }, { 24, 105 },
	{ 27, 92 }, { 28, 106 }, { 31, 69 }, { 35, 70 }, { 39, 73 },
	{ 40, 108 }, { 43, 71 }, { 45, 72 }, { 47, 83 }, { 51, 74 }, { 55, 77 },
	{ 59, 78 }, { 63, 75 }, { 64, 81 }, { 65, 87 }, { 66, 88 }, { 67, 89 },
	{ 68, 54 }, { 70, 85 }, { 77, 46 }, { 80, 90 }, { 81, 91 }, { 83, 97 },
	{ 84, 98 }, { 86, 99 }, { 87, 100 }, { 88, 101 }, { 89, 102 },
	{ 92, 103 }, { 93, 104 }, { 100, 53 }, { 103, 47 }, { 104, 48 },
	{ 108, 49 }, { 109, 94 }, { 110, 95 }, { 111, 96 }, { 112, 55 },
	{ 113, 56 }, { 118, 50 }, { 121, 51 }, { 122, 57 }, { 123, 58 },
	{ 124, 45 }, { 126, 59 }, { 128, 76 }, { 129, 86 }, { 132, 93 },
	{ 133, 65 }, { 134, 66 }, { 136, 62 }, { 137, 63 }, { 138, 64 },
	{ 142, 60 }, { 143, 61 }
};

static const struct msm_pinctrl_soc_data sm8250_pinctrl = {
	.pins = sm8250_pins,
	.npins = ARRAY_SIZE(sm8250_pins),
	.functions = sm8250_functions,
	.nfunctions = ARRAY_SIZE(sm8250_functions),
	.groups = sm8250_groups,
	.ngroups = ARRAY_SIZE(sm8250_groups),
	.ngpios = 181,
	.tiles = sm8250_tiles,
	.ntiles = ARRAY_SIZE(sm8250_tiles),
	.wakeirq_map = sm8250_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(sm8250_pdc_map),
	.egpio_func = 9,
};

static int sm8250_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &sm8250_pinctrl);
}

static const struct of_device_id sm8250_pinctrl_of_match[] = {
	{ .compatible = "qcom,sm8250-pinctrl", },
	{ },
};

static struct platform_driver sm8250_pinctrl_driver = {
	.driver = {
		.name = "sm8250-pinctrl",
		.of_match_table = sm8250_pinctrl_of_match,
	},
	.probe = sm8250_pinctrl_probe,
};

static int __init sm8250_pinctrl_init(void)
{
	return platform_driver_register(&sm8250_pinctrl_driver);
}
arch_initcall(sm8250_pinctrl_init);

static void __exit sm8250_pinctrl_exit(void)
{
	platform_driver_unregister(&sm8250_pinctrl_driver);
}
module_exit(sm8250_pinctrl_exit);

MODULE_DESCRIPTION("QTI sm8250 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, sm8250_pinctrl_of_match);
