// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define REG_BASE 0x100000
#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)\
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
		.ctl_reg = REG_BASE + REG_SIZE * id,			\
		.io_reg = REG_BASE + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = REG_BASE + 0x8 + REG_SIZE * id,		\
		.intr_status_reg = REG_BASE + 0xc + REG_SIZE * id,	\
		.intr_target_reg = REG_BASE + 0x8 + REG_SIZE * id,	\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
		.egpio_enable = 12,             \
		.egpio_present = 11,            \
		.oe_bit = 9,			\
		.in_bit = 0,			\
		.out_bit = 1,			\
		.intr_enable_bit = 0,		\
		.intr_status_bit = 0,		\
		.intr_target_bit = 5,		\
		.intr_target_width = 4,		\
		.intr_target_kpss_val = 3,	\
		.intr_raw_status_bit = 4,	\
		.intr_polarity_bit = 1,		\
		.intr_detection_bit = 2,	\
		.intr_detection_width = 2,	\
	}

#define SDC_QDSD_PINGROUP(pg_name, ctl, pull, drv)	\
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

#define QUP_I3C(qup_mode, qup_offset)			\
	{						\
		.mode = qup_mode,			\
		.offset = qup_offset,			\
	}

#define QUP_I3C_6_MODE_OFFSET	0xAF000
#define QUP_I3C_7_MODE_OFFSET	0xB0000
#define QUP_I3C_13_MODE_OFFSET	0xB1000
#define QUP_I3C_14_MODE_OFFSET	0xB2000

static const struct pinctrl_pin_desc sa8775p_pins[] = {
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
	PINCTRL_PIN(149, "UFS_RESET"),
	PINCTRL_PIN(150, "SDC1_RCLK"),
	PINCTRL_PIN(151, "SDC1_CLK"),
	PINCTRL_PIN(152, "SDC1_CMD"),
	PINCTRL_PIN(153, "SDC1_DATA"),
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

static const unsigned int ufs_reset_pins[] = { 149 };
static const unsigned int sdc1_rclk_pins[] = { 150 };
static const unsigned int sdc1_clk_pins[] = { 151 };
static const unsigned int sdc1_cmd_pins[] = { 152 };
static const unsigned int sdc1_data_pins[] = { 153 };

enum sa8775p_functions {
	msm_mux_gpio,
	msm_mux_atest_char,
	msm_mux_atest_usb2,
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
	msm_mux_edp0_hot,
	msm_mux_edp0_lcd,
	msm_mux_edp1_hot,
	msm_mux_edp1_lcd,
	msm_mux_edp2_hot,
	msm_mux_edp2_lcd,
	msm_mux_edp3_hot,
	msm_mux_edp3_lcd,
	msm_mux_emac0_mcg0,
	msm_mux_emac0_mcg1,
	msm_mux_emac0_mcg2,
	msm_mux_emac0_mcg3,
	msm_mux_emac0_mdc,
	msm_mux_emac0_mdio,
	msm_mux_emac0_ptp_aux,
	msm_mux_emac0_ptp_pps,
	msm_mux_emac1_mcg0,
	msm_mux_emac1_mcg1,
	msm_mux_emac1_mcg2,
	msm_mux_emac1_mcg3,
	msm_mux_emac1_mdc,
	msm_mux_emac1_mdio,
	msm_mux_emac1_ptp_aux,
	msm_mux_emac1_ptp_pps,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_gcc_gp4,
	msm_mux_gcc_gp5,
	msm_mux_hs0_mi2s,
	msm_mux_hs1_mi2s,
	msm_mux_hs2_mi2s,
	msm_mux_ibi_i3c,
	msm_mux_jitter_bist,
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
	msm_mux_mi2s1_data0,
	msm_mux_mi2s1_data1,
	msm_mux_mi2s1_sck,
	msm_mux_mi2s1_ws,
	msm_mux_mi2s2_data0,
	msm_mux_mi2s2_data1,
	msm_mux_mi2s2_sck,
	msm_mux_mi2s2_ws,
	msm_mux_mi2s_mclk0,
	msm_mux_mi2s_mclk1,
	msm_mux_pcie0_clkreq,
	msm_mux_pcie1_clkreq,
	msm_mux_phase_flag,
	msm_mux_pll_bist,
	msm_mux_pll_clk,
	msm_mux_prng_rosc0,
	msm_mux_prng_rosc1,
	msm_mux_prng_rosc2,
	msm_mux_prng_rosc3,
	msm_mux_qdss_cti,
	msm_mux_qdss_gpio,
	msm_mux_qup0_se0,
	msm_mux_qup0_se1,
	msm_mux_qup0_se2,
	msm_mux_qup0_se3,
	msm_mux_qup0_se4,
	msm_mux_qup0_se5,
	msm_mux_qup1_se0,
	msm_mux_qup1_se1,
	msm_mux_qup1_se2,
	msm_mux_qup1_se3,
	msm_mux_qup1_se4,
	msm_mux_qup1_se5,
	msm_mux_qup1_se6,
	msm_mux_qup2_se0,
	msm_mux_qup2_se1,
	msm_mux_qup2_se2,
	msm_mux_qup2_se3,
	msm_mux_qup2_se4,
	msm_mux_qup2_se5,
	msm_mux_qup2_se6,
	msm_mux_qup3_se0,
	msm_mux_sail_top,
	msm_mux_sailss_emac0,
	msm_mux_sailss_ospi,
	msm_mux_sgmii_phy,
	msm_mux_tb_trig,
	msm_mux_tgu_ch0,
	msm_mux_tgu_ch1,
	msm_mux_tgu_ch2,
	msm_mux_tgu_ch3,
	msm_mux_tgu_ch4,
	msm_mux_tgu_ch5,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_tsense_pwm3,
	msm_mux_tsense_pwm4,
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
	"gpio147", "gpio148",
};

static const char * const atest_char_groups[] = {
	"gpio27", "gpio58", "gpio59", "gpio89", "gpio90",
};

static const char * const atest_usb2_groups[] = {
	"gpio58", "gpio59", "gpio86", "gpio87", "gpio88", "gpio89", "gpio90",
	"gpio91", "gpio92", "gpio93", "gpio94", "gpio95", "gpio96", "gpio97",
	"gpio105",
};

static const char * const audio_ref_groups[] = {
	"gpio113",
};

static const char * const cam_mclk_groups[] = {
	"gpio72", "gpio73", "gpio74", "gpio75",
};

static const char * const cci_async_groups[] = {
	"gpio50", "gpio66", "gpio68", "gpio69", "gpio70", "gpio71",
};

static const char * const cci_i2c_groups[] = {
	"gpio52", "gpio53", "gpio54", "gpio55", "gpio56", "gpio57", "gpio58",
	"gpio59", "gpio60", "gpio61", "gpio62", "gpio63", "gpio64", "gpio65",
	"gpio66", "gpio67",
};

static const char * const cci_timer0_groups[] = {
	"gpio68",
};

static const char * const cci_timer1_groups[] = {
	"gpio69",
};

static const char * const cci_timer2_groups[] = {
	"gpio70",
};

static const char * const cci_timer3_groups[] = {
	"gpio71",
};

static const char * const cci_timer4_groups[] = {
	"gpio52",
};

static const char * const cci_timer5_groups[] = {
	"gpio53",
};

static const char * const cci_timer6_groups[] = {
	"gpio54",
};

static const char * const cci_timer7_groups[] = {
	"gpio55",
};

static const char * const cci_timer8_groups[] = {
	"gpio56",
};

static const char * const cci_timer9_groups[] = {
	"gpio57",
};

static const char * const cri_trng_groups[] = {
	"gpio99",
};

static const char * const cri_trng0_groups[] = {
	"gpio97",
};

static const char * const cri_trng1_groups[] = {
	"gpio98",
};

static const char * const dbg_out_groups[] = {
	"gpio144",
};

static const char * const ddr_bist_groups[] = {
	"gpio56", "gpio57", "gpio58", "gpio59",
};

static const char * const ddr_pxi0_groups[] = {
	"gpio33", "gpio34",
};

static const char * const ddr_pxi1_groups[] = {
	"gpio52", "gpio53",
};

static const char * const ddr_pxi2_groups[] = {
	"gpio55", "gpio86",
};

static const char * const ddr_pxi3_groups[] = {
	"gpio87", "gpio88",
};

static const char * const ddr_pxi4_groups[] = {
	"gpio89", "gpio90",
};

static const char * const ddr_pxi5_groups[] = {
	"gpio118", "gpio119",
};

static const char * const edp0_hot_groups[] = {
	"gpio101",
};

static const char * const edp0_lcd_groups[] = {
	"gpio44",
};

static const char * const edp1_hot_groups[] = {
	"gpio102",
};

static const char * const edp1_lcd_groups[] = {
	"gpio45",
};

static const char * const edp2_hot_groups[] = {
	"gpio104",
};

static const char * const edp2_lcd_groups[] = {
	"gpio48",
};

static const char * const edp3_hot_groups[] = {
	"gpio103",
};

static const char * const edp3_lcd_groups[] = {
	"gpio49",
};

static const char * const emac0_mcg0_groups[] = {
	"gpio12",
};

static const char * const emac0_mcg1_groups[] = {
	"gpio13",
};

static const char * const emac0_mcg2_groups[] = {
	"gpio14",
};

static const char * const emac0_mcg3_groups[] = {
	"gpio15",
};

static const char * const emac0_mdc_groups[] = {
	"gpio8",
};

static const char * const emac0_mdio_groups[] = {
	"gpio9",
};

static const char * const emac0_ptp_aux_groups[] = {
	"gpio6", "gpio10", "gpio11", "gpio12",
};

static const char * const emac0_ptp_pps_groups[] = {
	"gpio6", "gpio10", "gpio11", "gpio12",
};

static const char * const emac1_mcg0_groups[] = {
	"gpio16",

};

static const char * const emac1_mcg1_groups[] = {
	"gpio17",
};

static const char * const emac1_mcg2_groups[] = {
	"gpio18",
};

static const char * const emac1_mcg3_groups[] = {
	"gpio19",
};

static const char * const emac1_mdc_groups[] = {
	"gpio20",
};

static const char * const emac1_mdio_groups[] = {
	"gpio21",
};

static const char * const emac1_ptp_aux_groups[] = {
	"gpio6", "gpio10", "gpio11", "gpio12",
};

static const char * const emac1_ptp_pps_groups[] = {
	"gpio6", "gpio10", "gpio11", "gpio12",
};

static const char * const gcc_gp1_groups[] = {
	"gpio51", "gpio82",
};

static const char * const gcc_gp2_groups[] = {
	"gpio52", "gpio83",
};

static const char * const gcc_gp3_groups[] = {
	"gpio53", "gpio84",
};

static const char * const gcc_gp4_groups[] = {
	"gpio33", "gpio55",
};

static const char * const gcc_gp5_groups[] = {
	"gpio34", "gpio42",
};

static const char * const hs0_mi2s_groups[] = {
	"gpio114", "gpio115", "gpio116", "gpio117",
};

static const char * const hs1_mi2s_groups[] = {
	"gpio118", "gpio119", "gpio120", "gpio121",
};

static const char * const hs2_mi2s_groups[] = {
	"gpio122", "gpio123", "gpio124", "gpio125",
};

static const char * const ibi_i3c_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43", "gpio80", "gpio81", "gpio84",
	"gpio85",
};

static const char * const jitter_bist_groups[] = {
	"gpio86",
};

static const char * const mdp0_vsync0_groups[] = {
	"gpio57",
};

static const char * const mdp0_vsync1_groups[] = {
	"gpio58",
};

static const char * const mdp0_vsync2_groups[] = {
	"gpio59",
};

static const char * const mdp0_vsync3_groups[] = {
	"gpio80",
};

static const char * const mdp0_vsync4_groups[] = {
	"gpio81",
};

static const char * const mdp0_vsync5_groups[] = {
	"gpio91",
};

static const char * const mdp0_vsync6_groups[] = {
	"gpio92",
};

static const char * const mdp0_vsync7_groups[] = {
	"gpio93",
};

static const char * const mdp0_vsync8_groups[] = {
	"gpio94",
};

static const char * const mdp1_vsync0_groups[] = {
	"gpio40",
};

static const char * const mdp1_vsync1_groups[] = {
	"gpio41",
};

static const char * const mdp1_vsync2_groups[] = {
	"gpio42",
};

static const char * const mdp1_vsync3_groups[] = {
	"gpio43",
};

static const char * const mdp1_vsync4_groups[] = {
	"gpio46",
};

static const char * const mdp1_vsync5_groups[] = {
	"gpio47",
};

static const char * const mdp1_vsync6_groups[] = {
	"gpio51",
};

static const char * const mdp1_vsync7_groups[] = {
	"gpio52",
};

static const char * const mdp1_vsync8_groups[] = {
	"gpio50",
};

static const char * const mdp_vsync_groups[] = {
	"gpio82", "gpio83", "gpio84",
};

static const char * const mi2s1_data0_groups[] = {
	"gpio108",
};

static const char * const mi2s1_data1_groups[] = {
	"gpio109",
};

static const char * const mi2s1_sck_groups[] = {
	"gpio106",
};

static const char * const mi2s1_ws_groups[] = {
	"gpio107",
};

static const char * const mi2s2_data0_groups[] = {
	"gpio112",
};

static const char * const mi2s2_data1_groups[] = {
	"gpio113",
};

static const char * const mi2s2_sck_groups[] = {
	"gpio110",
};

static const char * const mi2s2_ws_groups[] = {
	"gpio111",
};

static const char * const mi2s_mclk0_groups[] = {
	"gpio105",
};

static const char * const mi2s_mclk1_groups[] = {
	"gpio117",
};

static const char * const pcie0_clkreq_groups[] = {
	"gpio1",
};

static const char * const pcie1_clkreq_groups[] = {
	"gpio3",
};

static const char * const phase_flag_groups[] = {
	"gpio25", "gpio26", "gpio27", "gpio28", "gpio29", "gpio30", "gpio31",
	"gpio32", "gpio35", "gpio36", "gpio37", "gpio38", "gpio39", "gpio56",
	"gpio57", "gpio98", "gpio99", "gpio106", "gpio107", "gpio108",
	"gpio109", "gpio110", "gpio111", "gpio112", "gpio113", "gpio114",
	"gpio120", "gpio121", "gpio122", "gpio123", "gpio124", "gpio125",
};

static const char * const pll_bist_groups[] = {
	"gpio114",
};

static const char * const pll_clk_groups[] = {
	"gpio87",
};

static const char * const prng_rosc0_groups[] = {
	"gpio101",
};

static const char * const prng_rosc1_groups[] = {
	"gpio102",
};

static const char * const prng_rosc2_groups[] = {
	"gpio103",
};

static const char * const prng_rosc3_groups[] = {
	"gpio104",
};

static const char * const qdss_cti_groups[] = {
	"gpio26", "gpio27", "gpio38", "gpio39", "gpio48", "gpio49", "gpio50",
	"gpio51",
};

static const char * const qdss_gpio_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23", "gpio24", "gpio25", "gpio28",
	"gpio29", "gpio30", "gpio31", "gpio60", "gpio61", "gpio62", "gpio63",
	"gpio64", "gpio65", "gpio66", "gpio67", "gpio105", "gpio106", "gpio107",
	"gpio108", "gpio109", "gpio110", "gpio111", "gpio112", "gpio113",
	"gpio114", "gpio115", "gpio116", "gpio117", "gpio118", "gpio119",
	"gpio120", "gpio121", "gpio122",
};

static const char * const qup0_se0_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23",
};

static const char * const qup0_se1_groups[] = {
	"gpio24", "gpio25", "gpio26", "gpio27",
};

static const char * const qup0_se2_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};

static const char * const qup0_se3_groups[] = {
	"gpio28", "gpio29", "gpio30", "gpio31",
};

static const char * const qup0_se4_groups[] = {
	"gpio32", "gpio33", "gpio34", "gpio35",
};

static const char * const qup0_se5_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};

static const char * const qup1_se0_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43",
};

static const char * const qup1_se1_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43",
};

static const char * const qup1_se2_groups[] = {
	"gpio44", "gpio45", "gpio46", "gpio47",
};

static const char * const qup1_se3_groups[] = {
	"gpio44", "gpio45", "gpio46", "gpio47",
};

static const char * const qup1_se4_groups[] = {
	"gpio48", "gpio49", "gpio50", "gpio51",
};

static const char * const qup1_se5_groups[] = {
	"gpio52", "gpio53", "gpio54", "gpio55",
};

static const char * const qup1_se6_groups[] = {
	"gpio56", "gpio56", "gpio57", "gpio57",
};

static const char * const qup2_se0_groups[] = {
	"gpio80", "gpio81", "gpio82", "gpio83",
};

static const char * const qup2_se1_groups[] = {
	"gpio84", "gpio85", "gpio99", "gpio100",
};

static const char * const qup2_se2_groups[] = {
	"gpio86", "gpio87", "gpio88", "gpio89", "gpio90",
};

static const char * const qup2_se3_groups[] = {
	"gpio91", "gpio92", "gpio93", "gpio94",
};

static const char * const qup2_se4_groups[] = {
	"gpio95", "gpio96", "gpio97", "gpio98",
};

static const char * const qup2_se5_groups[] = {
	"gpio84", "gpio85", "gpio99", "gpio100",
};

static const char * const qup2_se6_groups[] = {
	"gpio95", "gpio96", "gpio97", "gpio98",
};

static const char * const qup3_se0_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16", "gpio17", "gpio18", "gpio19",
};

static const char * const sail_top_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16",
};

static const char * const sailss_emac0_groups[] = {
	"gpio18", "gpio19",
};

static const char * const sailss_ospi_groups[] = {
	"gpio18", "gpio19",
};

static const char * const sgmii_phy_groups[] = {
	"gpio7", "gpio26",
};

static const char * const tb_trig_groups[] = {
	"gpio17", "gpio17",
};

static const char * const tgu_ch0_groups[] = {
	"gpio46",
};

static const char * const tgu_ch1_groups[] = {
	"gpio47",
};

static const char * const tgu_ch2_groups[] = {
	"gpio36",
};

static const char * const tgu_ch3_groups[] = {
	"gpio37",
};

static const char * const tgu_ch4_groups[] = {
	"gpio38",
};

static const char * const tgu_ch5_groups[] = {
	"gpio39",
};

static const char * const tsense_pwm1_groups[] = {
	"gpio104",
};

static const char * const tsense_pwm2_groups[] = {
	"gpio103",
};

static const char * const tsense_pwm3_groups[] = {
	"gpio102",
};

static const char * const tsense_pwm4_groups[] = {
	"gpio101",
};

static const char * const usb2phy_ac_groups[] = {
	"gpio10", "gpio11", "gpio12",
};

static const char * const vsense_trigger_groups[] = {
	"gpio111",
};

static const struct pinfunction sa8775p_functions[] = {
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_usb2),
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
	MSM_PIN_FUNCTION(edp0_hot),
	MSM_PIN_FUNCTION(edp0_lcd),
	MSM_PIN_FUNCTION(edp1_hot),
	MSM_PIN_FUNCTION(edp1_lcd),
	MSM_PIN_FUNCTION(edp2_hot),
	MSM_PIN_FUNCTION(edp2_lcd),
	MSM_PIN_FUNCTION(edp3_hot),
	MSM_PIN_FUNCTION(edp3_lcd),
	MSM_PIN_FUNCTION(emac0_mcg0),
	MSM_PIN_FUNCTION(emac0_mcg1),
	MSM_PIN_FUNCTION(emac0_mcg2),
	MSM_PIN_FUNCTION(emac0_mcg3),
	MSM_PIN_FUNCTION(emac0_mdc),
	MSM_PIN_FUNCTION(emac0_mdio),
	MSM_PIN_FUNCTION(emac0_ptp_aux),
	MSM_PIN_FUNCTION(emac0_ptp_pps),
	MSM_PIN_FUNCTION(emac1_mcg0),
	MSM_PIN_FUNCTION(emac1_mcg1),
	MSM_PIN_FUNCTION(emac1_mcg2),
	MSM_PIN_FUNCTION(emac1_mcg3),
	MSM_PIN_FUNCTION(emac1_mdc),
	MSM_PIN_FUNCTION(emac1_mdio),
	MSM_PIN_FUNCTION(emac1_ptp_aux),
	MSM_PIN_FUNCTION(emac1_ptp_pps),
	MSM_PIN_FUNCTION(gcc_gp1),
	MSM_PIN_FUNCTION(gcc_gp2),
	MSM_PIN_FUNCTION(gcc_gp3),
	MSM_PIN_FUNCTION(gcc_gp4),
	MSM_PIN_FUNCTION(gcc_gp5),
	MSM_PIN_FUNCTION(hs0_mi2s),
	MSM_PIN_FUNCTION(hs1_mi2s),
	MSM_PIN_FUNCTION(hs2_mi2s),
	MSM_PIN_FUNCTION(ibi_i3c),
	MSM_PIN_FUNCTION(jitter_bist),
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
	MSM_PIN_FUNCTION(mi2s1_data0),
	MSM_PIN_FUNCTION(mi2s1_data1),
	MSM_PIN_FUNCTION(mi2s1_sck),
	MSM_PIN_FUNCTION(mi2s1_ws),
	MSM_PIN_FUNCTION(mi2s2_data0),
	MSM_PIN_FUNCTION(mi2s2_data1),
	MSM_PIN_FUNCTION(mi2s2_sck),
	MSM_PIN_FUNCTION(mi2s2_ws),
	MSM_PIN_FUNCTION(mi2s_mclk0),
	MSM_PIN_FUNCTION(mi2s_mclk1),
	MSM_PIN_FUNCTION(pcie0_clkreq),
	MSM_PIN_FUNCTION(pcie1_clkreq),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(pll_bist),
	MSM_PIN_FUNCTION(pll_clk),
	MSM_PIN_FUNCTION(prng_rosc0),
	MSM_PIN_FUNCTION(prng_rosc1),
	MSM_PIN_FUNCTION(prng_rosc2),
	MSM_PIN_FUNCTION(prng_rosc3),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qdss_gpio),
	MSM_PIN_FUNCTION(qup0_se0),
	MSM_PIN_FUNCTION(qup0_se1),
	MSM_PIN_FUNCTION(qup0_se2),
	MSM_PIN_FUNCTION(qup0_se3),
	MSM_PIN_FUNCTION(qup0_se4),
	MSM_PIN_FUNCTION(qup0_se5),
	MSM_PIN_FUNCTION(qup1_se0),
	MSM_PIN_FUNCTION(qup1_se1),
	MSM_PIN_FUNCTION(qup1_se2),
	MSM_PIN_FUNCTION(qup1_se3),
	MSM_PIN_FUNCTION(qup1_se4),
	MSM_PIN_FUNCTION(qup1_se5),
	MSM_PIN_FUNCTION(qup1_se6),
	MSM_PIN_FUNCTION(qup2_se0),
	MSM_PIN_FUNCTION(qup2_se1),
	MSM_PIN_FUNCTION(qup2_se2),
	MSM_PIN_FUNCTION(qup2_se3),
	MSM_PIN_FUNCTION(qup2_se4),
	MSM_PIN_FUNCTION(qup2_se5),
	MSM_PIN_FUNCTION(qup2_se6),
	MSM_PIN_FUNCTION(qup3_se0),
	MSM_PIN_FUNCTION(sail_top),
	MSM_PIN_FUNCTION(sailss_emac0),
	MSM_PIN_FUNCTION(sailss_ospi),
	MSM_PIN_FUNCTION(sgmii_phy),
	MSM_PIN_FUNCTION(tb_trig),
	MSM_PIN_FUNCTION(tgu_ch0),
	MSM_PIN_FUNCTION(tgu_ch1),
	MSM_PIN_FUNCTION(tgu_ch2),
	MSM_PIN_FUNCTION(tgu_ch3),
	MSM_PIN_FUNCTION(tgu_ch4),
	MSM_PIN_FUNCTION(tgu_ch5),
	MSM_PIN_FUNCTION(tsense_pwm1),
	MSM_PIN_FUNCTION(tsense_pwm2),
	MSM_PIN_FUNCTION(tsense_pwm3),
	MSM_PIN_FUNCTION(tsense_pwm4),
	MSM_PIN_FUNCTION(usb2phy_ac),
	MSM_PIN_FUNCTION(vsense_trigger),
};

/*
 * Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup sa8775p_groups[] = {
	[0] = PINGROUP(0, _, _, _, _, _, _, _, _, _),
	[1] = PINGROUP(1, pcie0_clkreq, _, _, _, _, _, _, _, _),
	[2] = PINGROUP(2, _, _, _, _, _, _, _, _, _),
	[3] = PINGROUP(3, pcie1_clkreq, _, _, _, _, _, _, _, _),
	[4] = PINGROUP(4, _, _, _, _, _, _, _, _, _),
	[5] = PINGROUP(5, _, _, _, _, _, _, _, _, _),
	[6] = PINGROUP(6, emac0_ptp_aux, emac0_ptp_pps, emac1_ptp_aux, emac1_ptp_pps,
		       _, _, _, _, _),
	[7] = PINGROUP(7, sgmii_phy, _, _, _, _, _, _, _, _),
	[8] = PINGROUP(8, emac0_mdc, _, _, _, _, _, _, _, _),
	[9] = PINGROUP(9, emac0_mdio, _, _, _, _, _, _, _, _),
	[10] = PINGROUP(10, usb2phy_ac, emac0_ptp_aux, emac0_ptp_pps, emac1_ptp_aux, emac1_ptp_pps,
			_, _, _, _),
	[11] = PINGROUP(11, usb2phy_ac, emac0_ptp_aux, emac0_ptp_pps, emac1_ptp_aux, emac1_ptp_pps,
			_, _, _, _),
	[12] = PINGROUP(12, usb2phy_ac, emac0_ptp_aux, emac0_ptp_pps, emac1_ptp_aux, emac1_ptp_pps,
			emac0_mcg0, _, _, _),
	[13] = PINGROUP(13, qup3_se0, emac0_mcg1, _, _, sail_top, _, _, _, _),
	[14] = PINGROUP(14, qup3_se0, emac0_mcg2, _, _, sail_top, _, _, _, _),
	[15] = PINGROUP(15, qup3_se0, emac0_mcg3, _, _, sail_top, _, _, _, _),
	[16] = PINGROUP(16, qup3_se0, emac1_mcg0, _, _, sail_top, _, _, _, _),
	[17] = PINGROUP(17, qup3_se0, tb_trig, tb_trig, emac1_mcg1, _, _, _, _, _),
	[18] = PINGROUP(18, qup3_se0, emac1_mcg2, _, _, sailss_ospi, sailss_emac0, _, _, _),
	[19] = PINGROUP(19, qup3_se0, emac1_mcg3, _, _, sailss_ospi, sailss_emac0, _, _, _),
	[20] = PINGROUP(20, qup0_se0, emac1_mdc, qdss_gpio, _, _, _, _, _, _),
	[21] = PINGROUP(21, qup0_se0, emac1_mdio, qdss_gpio, _, _, _, _, _, _),
	[22] = PINGROUP(22, qup0_se0, qdss_gpio, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, qup0_se0, qdss_gpio, _, _, _, _, _, _, _),
	[24] = PINGROUP(24, qup0_se1, qdss_gpio, _, _, _, _, _, _, _),
	[25] = PINGROUP(25, qup0_se1, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[26] = PINGROUP(26, sgmii_phy, qup0_se1, qdss_cti, phase_flag, _, _, _, _, _),
	[27] = PINGROUP(27, qup0_se1, qdss_cti, phase_flag, _, atest_char, _, _, _, _),
	[28] = PINGROUP(28, qup0_se3, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[29] = PINGROUP(29, qup0_se3, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[30] = PINGROUP(30, qup0_se3, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[31] = PINGROUP(31, qup0_se3, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[32] = PINGROUP(32, qup0_se4, phase_flag, _, _, _, _, _, _, _),
	[33] = PINGROUP(33, qup0_se4, gcc_gp4, _, ddr_pxi0, _, _, _, _,	_),
	[34] = PINGROUP(34, qup0_se4, gcc_gp5, _, ddr_pxi0, _, _, _, _,	_),
	[35] = PINGROUP(35, qup0_se4, phase_flag, _, _, _, _, _, _, _),
	[36] = PINGROUP(36, qup0_se2, qup0_se5, phase_flag, tgu_ch2, _, _, _, _, _),
	[37] = PINGROUP(37, qup0_se2, qup0_se5, phase_flag, tgu_ch3, _, _, _, _, _),
	[38] = PINGROUP(38, qup0_se5, qup0_se2, qdss_cti, phase_flag, tgu_ch4, _, _, _, _),
	[39] = PINGROUP(39, qup0_se5, qup0_se2, qdss_cti, phase_flag, tgu_ch5, _, _, _, _),
	[40] = PINGROUP(40, qup1_se0, qup1_se1, ibi_i3c, mdp1_vsync0, _, _, _, _, _),
	[41] = PINGROUP(41, qup1_se0, qup1_se1, ibi_i3c, mdp1_vsync1, _, _, _, _, _),
	[42] = PINGROUP(42, qup1_se1, qup1_se0, ibi_i3c, mdp1_vsync2, gcc_gp5, _, _, _, _),
	[43] = PINGROUP(43, qup1_se1, qup1_se0, ibi_i3c, mdp1_vsync3, _, _, _, _, _),
	[44] = PINGROUP(44, qup1_se2, qup1_se3, edp0_lcd, _, _, _, _, _, _),
	[45] = PINGROUP(45, qup1_se2, qup1_se3, edp1_lcd, _, _, _, _, _, _),
	[46] = PINGROUP(46, qup1_se3, qup1_se2, mdp1_vsync4, tgu_ch0, _, _, _, _, _),
	[47] = PINGROUP(47, qup1_se3, qup1_se2, mdp1_vsync5, tgu_ch1, _, _, _, _, _),
	[48] = PINGROUP(48, qup1_se4, qdss_cti, edp2_lcd, _, _, _, _, _, _),
	[49] = PINGROUP(49, qup1_se4, qdss_cti, edp3_lcd, _, _, _, _, _, _),
	[50] = PINGROUP(50, qup1_se4, cci_async, qdss_cti, mdp1_vsync8, _, _, _, _, _),
	[51] = PINGROUP(51, qup1_se4, qdss_cti, mdp1_vsync6, gcc_gp1, _, _, _, _, _),
	[52] = PINGROUP(52, qup1_se5, cci_timer4, cci_i2c, mdp1_vsync7,	gcc_gp2, _, ddr_pxi1, _, _),
	[53] = PINGROUP(53, qup1_se5, cci_timer5, cci_i2c, gcc_gp3, _, ddr_pxi1, _, _, _),
	[54] = PINGROUP(54, qup1_se5, cci_timer6, cci_i2c, _, _, _, _, _, _),
	[55] = PINGROUP(55, qup1_se5, cci_timer7, cci_i2c, gcc_gp4, _, ddr_pxi2, _, _, _),
	[56] = PINGROUP(56, qup1_se6, qup1_se6, cci_timer8, cci_i2c, phase_flag,
			ddr_bist, _, _, _),
	[57] = PINGROUP(57, qup1_se6, qup1_se6, cci_timer9, cci_i2c, mdp0_vsync0,
			phase_flag, ddr_bist, _, _),
	[58] = PINGROUP(58, cci_i2c, mdp0_vsync1, ddr_bist, _, atest_usb2, atest_char, _, _, _),
	[59] = PINGROUP(59, cci_i2c, mdp0_vsync2, ddr_bist, _, atest_usb2, atest_char, _, _, _),
	[60] = PINGROUP(60, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[61] = PINGROUP(61, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[62] = PINGROUP(62, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[63] = PINGROUP(63, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[64] = PINGROUP(64, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[65] = PINGROUP(65, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[66] = PINGROUP(66, cci_i2c, cci_async, qdss_gpio, _, _, _, _, _, _),
	[67] = PINGROUP(67, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[68] = PINGROUP(68, cci_timer0, cci_async, _, _, _, _, _, _, _),
	[69] = PINGROUP(69, cci_timer1, cci_async, _, _, _, _, _, _, _),
	[70] = PINGROUP(70, cci_timer2, cci_async, _, _, _, _, _, _, _),
	[71] = PINGROUP(71, cci_timer3, cci_async, _, _, _, _, _, _, _),
	[72] = PINGROUP(72, cam_mclk, _, _, _, _, _, _, _, _),
	[73] = PINGROUP(73, cam_mclk, _, _, _, _, _, _, _, _),
	[74] = PINGROUP(74, cam_mclk, _, _, _, _, _, _, _, _),
	[75] = PINGROUP(75, cam_mclk, _, _, _, _, _, _, _, _),
	[76] = PINGROUP(76, _, _, _, _, _, _, _, _, _),
	[77] = PINGROUP(77, _, _, _, _, _, _, _, _, _),
	[78] = PINGROUP(78, _, _, _, _, _, _, _, _, _),
	[79] = PINGROUP(79, _, _, _, _, _, _, _, _, _),
	[80] = PINGROUP(80, qup2_se0, ibi_i3c, mdp0_vsync3, _, _, _, _, _, _),
	[81] = PINGROUP(81, qup2_se0, ibi_i3c, mdp0_vsync4, _, _, _, _, _, _),
	[82] = PINGROUP(82, qup2_se0, mdp_vsync, gcc_gp1, _, _, _, _, _, _),
	[83] = PINGROUP(83, qup2_se0, mdp_vsync, gcc_gp2, _, _, _, _, _, _),
	[84] = PINGROUP(84, qup2_se1, qup2_se5, ibi_i3c, mdp_vsync, gcc_gp3, _, _, _, _),
	[85] = PINGROUP(85, qup2_se1, qup2_se5, ibi_i3c, _, _, _, _, _, _),
	[86] = PINGROUP(86, qup2_se2, jitter_bist, atest_usb2, ddr_pxi2, _, _, _, _, _),
	[87] = PINGROUP(87, qup2_se2, pll_clk, atest_usb2, ddr_pxi3, _, _, _, _, _),
	[88] = PINGROUP(88, qup2_se2, _, atest_usb2, ddr_pxi3, _, _, _, _, _),
	[89] = PINGROUP(89, qup2_se2, _, atest_usb2, ddr_pxi4, atest_char, _, _, _, _),
	[90] = PINGROUP(90, qup2_se2, _, atest_usb2, ddr_pxi4, atest_char, _, _, _, _),
	[91] = PINGROUP(91, qup2_se3, mdp0_vsync5, _, atest_usb2, _, _, _, _, _),
	[92] = PINGROUP(92, qup2_se3, mdp0_vsync6, _, atest_usb2, _, _, _, _, _),
	[93] = PINGROUP(93, qup2_se3, mdp0_vsync7, _, atest_usb2, _, _, _, _, _),
	[94] = PINGROUP(94, qup2_se3, mdp0_vsync8, _, atest_usb2, _, _, _, _, _),
	[95] = PINGROUP(95, qup2_se4, qup2_se6, _, atest_usb2, _, _, _, _, _),
	[96] = PINGROUP(96, qup2_se4, qup2_se6, _, atest_usb2, _, _, _, _, _),
	[97] = PINGROUP(97, qup2_se6, qup2_se4, cri_trng0, _, atest_usb2, _, _, _, _),
	[98] = PINGROUP(98, qup2_se6, qup2_se4, phase_flag, cri_trng1, _, _, _, _, _),
	[99] = PINGROUP(99, qup2_se5, qup2_se1, phase_flag, cri_trng, _, _, _, _, _),
	[100] = PINGROUP(100, qup2_se5, qup2_se1, _, _, _, _, _, _, _),
	[101] = PINGROUP(101, edp0_hot, prng_rosc0, tsense_pwm4, _, _, _, _, _, _),
	[102] = PINGROUP(102, edp1_hot, prng_rosc1, tsense_pwm3, _, _, _, _, _, _),
	[103] = PINGROUP(103, edp3_hot, prng_rosc2, tsense_pwm2, _, _, _, _, _, _),
	[104] = PINGROUP(104, edp2_hot, prng_rosc3, tsense_pwm1, _, _, _, _, _, _),
	[105] = PINGROUP(105, mi2s_mclk0, _, qdss_gpio, atest_usb2, _, _, _, _, _),
	[106] = PINGROUP(106, mi2s1_sck, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[107] = PINGROUP(107, mi2s1_ws, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[108] = PINGROUP(108, mi2s1_data0, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[109] = PINGROUP(109, mi2s1_data1, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[110] = PINGROUP(110, mi2s2_sck, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[111] = PINGROUP(111, mi2s2_ws, phase_flag, _, qdss_gpio, vsense_trigger, _, _, _, _),
	[112] = PINGROUP(112, mi2s2_data0, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[113] = PINGROUP(113, mi2s2_data1, audio_ref, phase_flag, _, qdss_gpio, _, _, _, _),
	[114] = PINGROUP(114, hs0_mi2s, pll_bist, phase_flag, _, qdss_gpio, _, _, _, _),
	[115] = PINGROUP(115, hs0_mi2s, _, qdss_gpio, _, _, _, _, _, _),
	[116] = PINGROUP(116, hs0_mi2s, _, qdss_gpio, _, _, _, _, _, _),
	[117] = PINGROUP(117, hs0_mi2s, mi2s_mclk1, _, qdss_gpio, _, _, _, _, _),
	[118] = PINGROUP(118, hs1_mi2s, _, qdss_gpio, ddr_pxi5, _, _, _, _, _),
	[119] = PINGROUP(119, hs1_mi2s, _, qdss_gpio, ddr_pxi5, _, _, _, _, _),
	[120] = PINGROUP(120, hs1_mi2s, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[121] = PINGROUP(121, hs1_mi2s, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[122] = PINGROUP(122, hs2_mi2s, phase_flag, _, qdss_gpio, _, _, _, _, _),
	[123] = PINGROUP(123, hs2_mi2s, phase_flag, _, _, _, _, _, _, _),
	[124] = PINGROUP(124, hs2_mi2s, phase_flag, _, _, _, _, _, _, _),
	[125] = PINGROUP(125, hs2_mi2s, phase_flag, _, _, _, _, _, _, _),
	[126] = PINGROUP(126, _, _, _, _, _, _, _, _, _),
	[127] = PINGROUP(127, _, _, _, _, _, _, _, _, _),
	[128] = PINGROUP(128, _, _, _, _, _, _, _, _, _),
	[129] = PINGROUP(129, _, _, _, _, _, _, _, _, _),
	[130] = PINGROUP(130, _, _, _, _, _, _, _, _, _),
	[131] = PINGROUP(131, _, _, _, _, _, _, _, _, _),
	[132] = PINGROUP(132, _, _, _, _, _, _, _, _, _),
	[133] = PINGROUP(133, _, _, _, _, _, _, _, _, _),
	[134] = PINGROUP(134, _, _, _, _, _, _, _, _, _),
	[135] = PINGROUP(135, _, _, _, _, _, _, _, _, _),
	[136] = PINGROUP(136, _, _, _, _, _, _, _, _, _),
	[137] = PINGROUP(137, _, _, _, _, _, _, _, _, _),
	[138] = PINGROUP(138, _, _, _, _, _, _, _, _, _),
	[139] = PINGROUP(139, _, _, _, _, _, _, _, _, _),
	[140] = PINGROUP(140, _, _, _, _, _, _, _, _, _),
	[141] = PINGROUP(141, _, _, _, _, _, _, _, _, _),
	[142] = PINGROUP(142, _, _, _, _, _, _, _, _, _),
	[143] = PINGROUP(143, _, _, _, _, _, _, _, _, _),
	[144] = PINGROUP(144, dbg_out, _, _, _, _, _, _, _, _),
	[145] = PINGROUP(145, _, _, _, _, _, _, _, _, _),
	[146] = PINGROUP(146, _, _, _, _, _, _, _, _, _),
	[147] = PINGROUP(147, _, _, _, _, _, _, _, _, _),
	[148] = PINGROUP(148, _, _, _, _, _, _, _, _, _),
	[149] = UFS_RESET(ufs_reset, 0x1a2000),
	[150] = SDC_QDSD_PINGROUP(sdc1_rclk, 0x199000, 15, 0),
	[151] = SDC_QDSD_PINGROUP(sdc1_clk, 0x199000, 13, 6),
	[152] = SDC_QDSD_PINGROUP(sdc1_cmd, 0x199000, 11, 3),
	[153] = SDC_QDSD_PINGROUP(sdc1_data, 0x199000, 9, 0),
};

static const struct msm_gpio_wakeirq_map sa8775p_pdc_map[] = {
	{ 0, 169 }, { 1, 174 }, { 2, 170 }, { 3, 175 }, { 4, 171 }, { 5, 173 },
	{ 6, 172 }, { 7, 182 }, { 10, 220 }, { 11, 213 }, { 12, 221 },
	{ 16, 230 }, { 19, 231 }, { 20, 232 }, { 23, 233 }, { 24, 234 },
	{ 26, 223 }, { 27, 235 }, { 28, 209 }, { 29, 176 }, { 30, 200 },
	{ 31, 201 }, { 32, 212 }, { 35, 177 }, { 36, 178 }, { 39, 184 },
	{ 40, 185 }, { 41, 227 }, { 42, 186 }, { 43, 228 }, { 45, 187 },
	{ 47, 188 }, { 48, 194 }, { 51, 195 }, { 52, 196 }, { 55, 197 },
	{ 56, 198 }, { 57, 236 }, { 58, 192 }, { 59, 193 }, { 72, 179 },
	{ 73, 180 }, { 74, 181 }, { 75, 202 }, { 76, 183 }, { 77, 189 },
	{ 78, 190 }, { 79, 191 }, { 80, 199 }, { 83, 204 }, { 84, 205 },
	{ 85, 229 }, { 86, 206 }, { 89, 207 }, { 91, 208 }, { 94, 214 },
	{ 95, 215 }, { 96, 237 }, { 97, 216 }, { 98, 238 }, { 99, 217 },
	{ 100, 239 }, { 105, 219 }, { 106, 210 }, { 107, 211 }, { 108, 222 },
	{ 109, 203 }, { 145, 225 }, { 146, 226 },
};

static const struct msm_pinctrl_soc_data sa8775p_pinctrl = {
	.pins = sa8775p_pins,
	.npins = ARRAY_SIZE(sa8775p_pins),
	.functions = sa8775p_functions,
	.nfunctions = ARRAY_SIZE(sa8775p_functions),
	.groups = sa8775p_groups,
	.ngroups = ARRAY_SIZE(sa8775p_groups),
	.ngpios = 150,
	.wakeirq_map = sa8775p_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(sa8775p_pdc_map),
};

static int sa8775p_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &sa8775p_pinctrl);
}

static const struct of_device_id sa8775p_pinctrl_of_match[] = {
	{ .compatible = "qcom,sa8775p-tlmm", },
	{ },
};
MODULE_DEVICE_TABLE(of, sa8775p_pinctrl_of_match);

static struct platform_driver sa8775p_pinctrl_driver = {
	.driver = {
		.name = "sa8775p-tlmm",
		.of_match_table = sa8775p_pinctrl_of_match,
	},
	.probe = sa8775p_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init sa8775p_pinctrl_init(void)
{
	return platform_driver_register(&sa8775p_pinctrl_driver);
}
arch_initcall(sa8775p_pinctrl_init);

static void __exit sa8775p_pinctrl_exit(void)
{
	platform_driver_unregister(&sa8775p_pinctrl_driver);
}
module_exit(sa8775p_pinctrl_exit);

MODULE_DESCRIPTION("QTI SA8775P pinctrl driver");
MODULE_LICENSE("GPL");
