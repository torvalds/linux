// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#define QUP_I3C(qup_mode, qup_offset)                  \
	{                                               \
		.mode = qup_mode,                       \
		.offset = qup_offset,                   \
	}

#define TLMM_NORTH_SPARE_OFFSET 0x1B3000
#define TLMM_NORTH_SPARE1_OFFSET 0x1B4000

#define SPARE_REG(sparereg, spare_offset)               \
	{                                               \
		.spare_reg = tlmm_##sparereg,            \
		.offset = spare_offset,                 \
	}

enum blair_tlmm_spare {
	tlmm_west_spare,
	tlmm_west_spare1,
	tlmm_north_spare,
	tlmm_north_spare1,
	tlmm_south_spare,
	tlmm_south_spare1,
};

static const struct pinctrl_pin_desc blair_pins[] = {
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
	PINCTRL_PIN(156, "UFS_RESET"),
	PINCTRL_PIN(157, "SDC1_RCLK"),
	PINCTRL_PIN(158, "SDC1_CLK"),
	PINCTRL_PIN(159, "SDC1_CMD"),
	PINCTRL_PIN(160, "SDC1_DATA"),
	PINCTRL_PIN(161, "SDC2_CLK"),
	PINCTRL_PIN(162, "SDC2_CMD"),
	PINCTRL_PIN(163, "SDC2_DATA"),
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


static const unsigned int sdc1_rclk_pins[] = { 157 };
static const unsigned int sdc1_clk_pins[] = { 158 };
static const unsigned int sdc1_cmd_pins[] = { 159 };
static const unsigned int sdc1_data_pins[] = { 160 };
static const unsigned int sdc2_clk_pins[] = { 161 };
static const unsigned int sdc2_cmd_pins[] = { 162 };
static const unsigned int sdc2_data_pins[] = { 163 };
static const unsigned int ufs_reset_pins[] = { 156 };

enum blair_functions {
	msm_mux_gpio,
	msm_mux_AGERA_PLL,
	msm_mux_CCI_ASYNC,
	msm_mux_CCI_I2C,
	msm_mux_GPS_TX,
	msm_mux_GP_PDM0,
	msm_mux_GP_PDM1,
	msm_mux_GP_PDM2,
	msm_mux_JITTER_BIST,
	msm_mux_MCLK,
	msm_mux_MDP_VSYNC,
	msm_mux_MSS_LTE,
	msm_mux_NAV_PPS,
	msm_mux_PLL_BIST,
	msm_mux_QLINK0_WMSS,
	msm_mux_QLINK1_WMSS,
	msm_mux_USB_PHY,
	msm_mux_adsp_ext,
	msm_mux_atest_char,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_atest_tsens,
	msm_mux_atest_tsens2,
	msm_mux_atest_usb1,
	msm_mux_atest_usb10,
	msm_mux_atest_usb11,
	msm_mux_atest_usb12,
	msm_mux_atest_usb13,
	msm_mux_atest_usb2,
	msm_mux_atest_usb20,
	msm_mux_atest_usb21,
	msm_mux_atest_usb22,
	msm_mux_atest_usb23,
	msm_mux_audio_ref,
	msm_mux_btfm_slimbus,
	msm_mux_cam_mclk,
	msm_mux_cci_timer0,
	msm_mux_cci_timer1,
	msm_mux_cci_timer2,
	msm_mux_cci_timer3,
	msm_mux_cci_timer4,
	msm_mux_cri_trng,
	msm_mux_dbg_out,
	msm_mux_ddr_bist,
	msm_mux_ddr_pxi0,
	msm_mux_ddr_pxi1,
	msm_mux_ddr_pxi2,
	msm_mux_ddr_pxi3,
	msm_mux_dp_hot,
	msm_mux_edp_lcd,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_ibi_i3c,
	msm_mux_ldo_en,
	msm_mux_ldo_update,
	msm_mux_lpass_ext,
	msm_mux_m_voc,
	msm_mux_mdp_vsync0,
	msm_mux_mdp_vsync1,
	msm_mux_mdp_vsync2,
	msm_mux_mdp_vsync3,
	msm_mux_mi2s_0,
	msm_mux_mi2s_1,
	msm_mux_mi2s_2,
	msm_mux_nav_gpio,
	msm_mux_pa_indicator,
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
	msm_mux_pll_bypassnl,
	msm_mux_pll_clk,
	msm_mux_pll_reset,
	msm_mux_prng_rosc0,
	msm_mux_prng_rosc1,
	msm_mux_prng_rosc2,
	msm_mux_prng_rosc3,
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
	msm_mux_qlink0_enable,
	msm_mux_qlink0_request,
	msm_mux_qlink1_enable,
	msm_mux_qlink1_request,
	msm_mux_qup00,
	msm_mux_qup01,
	msm_mux_qup02,
	msm_mux_qup10,
	msm_mux_qup11_f1,
	msm_mux_qup11_f2,
	msm_mux_qup12,
	msm_mux_qup13_f1,
	msm_mux_qup13_f2,
	msm_mux_qup14,
	msm_mux_sd_write,
	msm_mux_sdc1_tb,
	msm_mux_sdc2_tb,
	msm_mux_sp_cmu,
	msm_mux_tgu_ch0,
	msm_mux_tgu_ch1,
	msm_mux_tgu_ch2,
	msm_mux_tgu_ch3,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_uim1_clk,
	msm_mux_uim1_data,
	msm_mux_uim1_present,
	msm_mux_uim1_reset,
	msm_mux_uim2_clk,
	msm_mux_uim2_data,
	msm_mux_uim2_present,
	msm_mux_uim2_reset,
	msm_mux_usb2phy_ac,
	msm_mux_vfr_1,
	msm_mux_vsense_trigger,
	msm_mux_wlan1_adc0,
	msm_mux_wlan1_adc1,
	msm_mux_wlan2_adc0,
	msm_mux_wlan2_adc1,
	msm_mux_NA,
};

static const char * const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio11", "gpio12", "gpio13", "gpio14", "gpio15",
	"gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21", "gpio22",
	"gpio23", "gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio29",
	"gpio30", "gpio31", "gpio32", "gpio33", "gpio34", "gpio35", "gpio36",
	"gpio37", "gpio38", "gpio39", "gpio40", "gpio41", "gpio42", "gpio43",
	"gpio44", "gpio45", "gpio46", "gpio47", "gpio48", "gpio49", "gpio50",
	"gpio51", "gpio52", "gpio53", "gpio56", "gpio57", "gpio58", "gpio59",
	"gpio60", "gpio61", "gpio62", "gpio63", "gpio64", "gpio65", "gpio66",
	"gpio67", "gpio68", "gpio69", "gpio75", "gpio76", "gpio77", "gpio78",
	"gpio79", "gpio80", "gpio81", "gpio82", "gpio83", "gpio84", "gpio85",
	"gpio86", "gpio87", "gpio88", "gpio89", "gpio90", "gpio91", "gpio92",
	"gpio93", "gpio94", "gpio95", "gpio96", "gpio97", "gpio98", "gpio99",
	"gpio100", "gpio101", "gpio102", "gpio103", "gpio104", "gpio105",
	"gpio106", "gpio107", "gpio108", "gpio109", "gpio110", "gpio111",
	"gpio112", "gpio113", "gpio114", "gpio115", "gpio116", "gpio117",
	"gpio118", "gpio119", "gpio120", "gpio124", "gpio125", "gpio126",
	"gpio127", "gpio128", "gpio129", "gpio130", "gpio131", "gpio132",
	"gpio133", "gpio134", "gpio135", "gpio136", "gpio141", "gpio142",
	"gpio143", "gpio150", "gpio151", "gpio152", "gpio153", "gpio154",
	"gpio155",
};
static const char * const AGERA_PLL_groups[] = {
	"gpio89",
};
static const char * const CCI_ASYNC_groups[] = {
	"gpio35", "gpio36", "gpio48", "gpio52", "gpio53",
};
static const char * const CCI_I2C_groups[] = {
	"gpio2", "gpio3", "gpio39", "gpio40", "gpio41", "gpio42", "gpio43",
	"gpio44",
};
static const char * const GPS_TX_groups[] = {
	"gpio101", "gpio102", "gpio107", "gpio108",
};
static const char * const GP_PDM0_groups[] = {
	"gpio37", "gpio68",
};
static const char * const GP_PDM1_groups[] = {
	"gpio8", "gpio52",
};
static const char * const GP_PDM2_groups[] = {
	"gpio57",
};
static const char * const JITTER_BIST_groups[] = {
	"gpio90",
};
static const char * const MCLK_groups[] = {
	"gpio93",
};
static const char * const MDP_VSYNC_groups[] = {
	"gpio6", "gpio23", "gpio24", "gpio27", "gpio28",
};
static const char * const MSS_LTE_groups[] = {
	"gpio65", "gpio66",
};
static const char * const NAV_PPS_groups[] = {
	"gpio101", "gpio101", "gpio102", "gpio102",
};
static const char * const PLL_BIST_groups[] = {
	"gpio27",
};
static const char * const QLINK0_WMSS_groups[] = {
	"gpio103",
};
static const char * const QLINK1_WMSS_groups[] = {
	"gpio106",
};
static const char * const USB_PHY_groups[] = {
	"gpio124",
};
static const char * const adsp_ext_groups[] = {
	"gpio87",
};
static const char * const atest_char_groups[] = {
	"gpio95",
};
static const char * const atest_char0_groups[] = {
	"gpio96",
};
static const char * const atest_char1_groups[] = {
	"gpio97",
};
static const char * const atest_char2_groups[] = {
	"gpio98",
};
static const char * const atest_char3_groups[] = {
	"gpio99",
};
static const char * const atest_tsens_groups[] = {
	"gpio92",
};
static const char * const atest_tsens2_groups[] = {
	"gpio93",
};
static const char * const atest_usb1_groups[] = {
	"gpio83",
};
static const char * const atest_usb10_groups[] = {
	"gpio84",
};
static const char * const atest_usb11_groups[] = {
	"gpio85",
};
static const char * const atest_usb12_groups[] = {
	"gpio86",
};
static const char * const atest_usb13_groups[] = {
	"gpio87",
};
static const char * const atest_usb2_groups[] = {
	"gpio88",
};
static const char * const atest_usb20_groups[] = {
	"gpio89",
};
static const char * const atest_usb21_groups[] = {
	"gpio90",
};
static const char * const atest_usb22_groups[] = {
	"gpio91",
};
static const char * const atest_usb23_groups[] = {
	"gpio92",
};
static const char * const audio_ref_groups[] = {
	"gpio60",
};
static const char * const btfm_slimbus_groups[] = {
	"gpio67", "gpio68", "gpio86", "gpio87",
};
static const char * const cam_mclk_groups[] = {
	"gpio29", "gpio30", "gpio31", "gpio32", "gpio33",
};
static const char * const cci_timer0_groups[] = {
	"gpio34",
};
static const char * const cci_timer1_groups[] = {
	"gpio35",
};
static const char * const cci_timer2_groups[] = {
	"gpio36",
};
static const char * const cci_timer3_groups[] = {
	"gpio37",
};
static const char * const cci_timer4_groups[] = {
	"gpio38",
};
static const char * const cri_trng_groups[] = {
	"gpio0", "gpio1", "gpio2",
};
static const char * const dbg_out_groups[] = {
	"gpio3",
};
static const char * const ddr_bist_groups[] = {
	"gpio19", "gpio20", "gpio21", "gpio22",
};
static const char * const ddr_pxi0_groups[] = {
	"gpio86", "gpio90",
};
static const char * const ddr_pxi1_groups[] = {
	"gpio87", "gpio91",
};
static const char * const ddr_pxi2_groups[] = {
	"gpio88", "gpio92",
};
static const char * const ddr_pxi3_groups[] = {
	"gpio89", "gpio93",
};
static const char * const dp_hot_groups[] = {
	"gpio12", "gpio118",
};
static const char * const edp_lcd_groups[] = {
	"gpio23",
};
static const char * const gcc_gp1_groups[] = {
	"gpio48", "gpio58",
};
static const char * const gcc_gp2_groups[] = {
	"gpio21",
};
static const char * const gcc_gp3_groups[] = {
	"gpio22",
};
static const char * const ibi_i3c_groups[] = {
	"gpio0", "gpio1",
};
static const char * const ldo_en_groups[] = {
	"gpio95",
};
static const char * const ldo_update_groups[] = {
	"gpio96",
};
static const char * const lpass_ext_groups[] = {
	"gpio60", "gpio93",
};
static const char * const m_voc_groups[] = {
	"gpio12",
};
static const char * const mdp_vsync0_groups[] = {
	"gpio47",
};
static const char * const mdp_vsync1_groups[] = {
	"gpio48",
};
static const char * const mdp_vsync2_groups[] = {
	"gpio56",
};
static const char * const mdp_vsync3_groups[] = {
	"gpio57",
};
static const char * const mi2s_0_groups[] = {
	"gpio88", "gpio89", "gpio90", "gpio91",
};
static const char * const mi2s_1_groups[] = {
	"gpio67", "gpio68", "gpio86", "gpio87",
};
static const char * const mi2s_2_groups[] = {
	"gpio60",
};
static const char * const nav_gpio_groups[] = {
	"gpio101", "gpio102",
};
static const char * const pa_indicator_groups[] = {
	"gpio118",
};
static const char * const phase_flag0_groups[] = {
	"gpio12",
};
static const char * const phase_flag1_groups[] = {
	"gpio17",
};
static const char * const phase_flag10_groups[] = {
	"gpio41",
};
static const char * const phase_flag11_groups[] = {
	"gpio42",
};
static const char * const phase_flag12_groups[] = {
	"gpio43",
};
static const char * const phase_flag13_groups[] = {
	"gpio44",
};
static const char * const phase_flag14_groups[] = {
	"gpio45",
};
static const char * const phase_flag15_groups[] = {
	"gpio46",
};
static const char * const phase_flag16_groups[] = {
	"gpio47",
};
static const char * const phase_flag17_groups[] = {
	"gpio48",
};
static const char * const phase_flag18_groups[] = {
	"gpio49",
};
static const char * const phase_flag19_groups[] = {
	"gpio50",
};
static const char * const phase_flag2_groups[] = {
	"gpio18",
};
static const char * const phase_flag20_groups[] = {
	"gpio51",
};
static const char * const phase_flag21_groups[] = {
	"gpio52",
};
static const char * const phase_flag22_groups[] = {
	"gpio53",
};
static const char * const phase_flag23_groups[] = {
	"gpio56",
};
static const char * const phase_flag24_groups[] = {
	"gpio57",
};
static const char * const phase_flag25_groups[] = {
	"gpio60",
};
static const char * const phase_flag26_groups[] = {
	"gpio61",
};
static const char * const phase_flag27_groups[] = {
	"gpio62",
};
static const char * const phase_flag28_groups[] = {
	"gpio63",
};
static const char * const phase_flag29_groups[] = {
	"gpio64",
};
static const char * const phase_flag3_groups[] = {
	"gpio34",
};
static const char * const phase_flag30_groups[] = {
	"gpio67",
};
static const char * const phase_flag31_groups[] = {
	"gpio68",
};
static const char * const phase_flag4_groups[] = {
	"gpio35",
};
static const char * const phase_flag5_groups[] = {
	"gpio36",
};
static const char * const phase_flag6_groups[] = {
	"gpio37",
};
static const char * const phase_flag7_groups[] = {
	"gpio38",
};
static const char * const phase_flag8_groups[] = {
	"gpio39",
};
static const char * const phase_flag9_groups[] = {
	"gpio40",
};
static const char * const pll_bypassnl_groups[] = {
	"gpio13",
};
static const char * const pll_clk_groups[] = {
	"gpio98",
};
static const char * const pll_reset_groups[] = {
	"gpio14",
};
static const char * const prng_rosc0_groups[] = {
	"gpio97",
};
static const char * const prng_rosc1_groups[] = {
	"gpio98",
};
static const char * const prng_rosc2_groups[] = {
	"gpio99",
};
static const char * const prng_rosc3_groups[] = {
	"gpio100",
};
static const char * const qdss_cti_groups[] = {
	"gpio2", "gpio3", "gpio6", "gpio7", "gpio61", "gpio62", "gpio86",
	"gpio87",
};
static const char * const qdss_gpio_groups[] = {
	"gpio8", "gpio9", "gpio63", "gpio64",
};
static const char * const qdss_gpio0_groups[] = {
	"gpio39", "gpio65",
};
static const char * const qdss_gpio1_groups[] = {
	"gpio40", "gpio66",
};
static const char * const qdss_gpio10_groups[] = {
	"gpio50", "gpio56",
};
static const char * const qdss_gpio11_groups[] = {
	"gpio51", "gpio57",
};
static const char * const qdss_gpio12_groups[] = {
	"gpio34", "gpio52",
};
static const char * const qdss_gpio13_groups[] = {
	"gpio35", "gpio53",
};
static const char * const qdss_gpio14_groups[] = {
	"gpio27", "gpio36",
};
static const char * const qdss_gpio15_groups[] = {
	"gpio28", "gpio37",
};
static const char * const qdss_gpio2_groups[] = {
	"gpio38", "gpio41",
};
static const char * const qdss_gpio3_groups[] = {
	"gpio42", "gpio47",
};
static const char * const qdss_gpio4_groups[] = {
	"gpio43", "gpio88",
};
static const char * const qdss_gpio5_groups[] = {
	"gpio44", "gpio89",
};
static const char * const qdss_gpio6_groups[] = {
	"gpio45", "gpio90",
};
static const char * const qdss_gpio7_groups[] = {
	"gpio46", "gpio91",
};
static const char * const qdss_gpio8_groups[] = {
	"gpio48", "gpio92",
};
static const char * const qdss_gpio9_groups[] = {
	"gpio49", "gpio93",
};
static const char * const qlink0_enable_groups[] = {
	"gpio105",
};
static const char * const qlink0_request_groups[] = {
	"gpio104",
};
static const char * const qlink1_enable_groups[] = {
	"gpio108",
};
static const char * const qlink1_request_groups[] = {
	"gpio107",
};
static const char * const qup00_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const qup01_groups[] = {
	"gpio61", "gpio62", "gpio63", "gpio64",
};
static const char * const qup02_groups[] = {
	"gpio45", "gpio46", "gpio48", "gpio56", "gpio57",
};
static const char * const qup10_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16", "gpio17",
};
static const char * const qup11_f1_groups[] = {
	"gpio27", "gpio28",
};
static const char * const qup11_f2_groups[] = {
	"gpio27", "gpio28",
};

static const char * const qup12_groups[] = {
	"gpio19", "gpio19", "gpio20", "gpio20",
};
static const char * const qup13_f1_groups[] = {
	"gpio25", "gpio26",
};
static const char * const qup13_f2_groups[] = {
	"gpio25", "gpio26",
};
static const char * const qup14_groups[] = {
	"gpio4", "gpio4", "gpio5", "gpio5",
};
static const char * const sd_write_groups[] = {
	"gpio85",
};
static const char * const sdc1_tb_groups[] = {
	"gpio4",
};
static const char * const sdc2_tb_groups[] = {
	"gpio5",
};
static const char * const sp_cmu_groups[] = {
	"gpio3",
};
static const char * const tgu_ch0_groups[] = {
	"gpio61",
};
static const char * const tgu_ch1_groups[] = {
	"gpio62",
};
static const char * const tgu_ch2_groups[] = {
	"gpio63",
};
static const char * const tgu_ch3_groups[] = {
	"gpio64",
};
static const char * const tsense_pwm1_groups[] = {
	"gpio88",
};
static const char * const tsense_pwm2_groups[] = {
	"gpio88",
};
static const char * const uim1_clk_groups[] = {
	"gpio80",
};
static const char * const uim1_data_groups[] = {
	"gpio79",
};
static const char * const uim1_present_groups[] = {
	"gpio82",
};
static const char * const uim1_reset_groups[] = {
	"gpio81",
};
static const char * const uim2_clk_groups[] = {
	"gpio76",
};
static const char * const uim2_data_groups[] = {
	"gpio75",
};
static const char * const uim2_present_groups[] = {
	"gpio78",
};
static const char * const uim2_reset_groups[] = {
	"gpio77",
};
static const char * const usb2phy_ac_groups[] = {
	"gpio47",
};
static const char * const vfr_1_groups[] = {
	"gpio49",
};
static const char * const vsense_trigger_groups[] = {
	"gpio89",
};
static const char * const wlan1_adc0_groups[] = {
	"gpio90",
};
static const char * const wlan1_adc1_groups[] = {
	"gpio92",
};
static const char * const wlan2_adc0_groups[] = {
	"gpio91",
};
static const char * const wlan2_adc1_groups[] = {
	"gpio93",
};

static const struct msm_function blair_functions[] = {
	FUNCTION(gpio),
	FUNCTION(cri_trng),
	FUNCTION(qup00),
	FUNCTION(ibi_i3c),
	FUNCTION(CCI_I2C),
	FUNCTION(qdss_cti),
	FUNCTION(sp_cmu),
	FUNCTION(dbg_out),
	FUNCTION(qup14),
	FUNCTION(sdc1_tb),
	FUNCTION(sdc2_tb),
	FUNCTION(MDP_VSYNC),
	FUNCTION(GP_PDM1),
	FUNCTION(qdss_gpio),
	FUNCTION(m_voc),
	FUNCTION(dp_hot),
	FUNCTION(phase_flag0),
	FUNCTION(qup10),
	FUNCTION(pll_bypassnl),
	FUNCTION(pll_reset),
	FUNCTION(phase_flag1),
	FUNCTION(phase_flag2),
	FUNCTION(qup12),
	FUNCTION(ddr_bist),
	FUNCTION(gcc_gp2),
	FUNCTION(gcc_gp3),
	FUNCTION(edp_lcd),
	FUNCTION(qup13_f1),
	FUNCTION(qup13_f2),
	FUNCTION(qup11_f1),
	FUNCTION(qup11_f2),
	FUNCTION(PLL_BIST),
	FUNCTION(qdss_gpio14),
	FUNCTION(qdss_gpio15),
	FUNCTION(cam_mclk),
	FUNCTION(cci_timer0),
	FUNCTION(phase_flag3),
	FUNCTION(qdss_gpio12),
	FUNCTION(cci_timer1),
	FUNCTION(CCI_ASYNC),
	FUNCTION(phase_flag4),
	FUNCTION(qdss_gpio13),
	FUNCTION(cci_timer2),
	FUNCTION(phase_flag5),
	FUNCTION(cci_timer3),
	FUNCTION(GP_PDM0),
	FUNCTION(phase_flag6),
	FUNCTION(cci_timer4),
	FUNCTION(phase_flag7),
	FUNCTION(qdss_gpio2),
	FUNCTION(phase_flag8),
	FUNCTION(qdss_gpio0),
	FUNCTION(phase_flag9),
	FUNCTION(qdss_gpio1),
	FUNCTION(phase_flag10),
	FUNCTION(phase_flag11),
	FUNCTION(qdss_gpio3),
	FUNCTION(phase_flag12),
	FUNCTION(qdss_gpio4),
	FUNCTION(phase_flag13),
	FUNCTION(qdss_gpio5),
	FUNCTION(qup02),
	FUNCTION(phase_flag14),
	FUNCTION(qdss_gpio6),
	FUNCTION(phase_flag15),
	FUNCTION(qdss_gpio7),
	FUNCTION(mdp_vsync0),
	FUNCTION(phase_flag16),
	FUNCTION(usb2phy_ac),
	FUNCTION(mdp_vsync1),
	FUNCTION(gcc_gp1),
	FUNCTION(phase_flag17),
	FUNCTION(qdss_gpio8),
	FUNCTION(vfr_1),
	FUNCTION(phase_flag18),
	FUNCTION(qdss_gpio9),
	FUNCTION(phase_flag19),
	FUNCTION(qdss_gpio10),
	FUNCTION(phase_flag20),
	FUNCTION(qdss_gpio11),
	FUNCTION(phase_flag21),
	FUNCTION(phase_flag22),
	FUNCTION(mdp_vsync2),
	FUNCTION(phase_flag23),
	FUNCTION(mdp_vsync3),
	FUNCTION(GP_PDM2),
	FUNCTION(phase_flag24),
	FUNCTION(audio_ref),
	FUNCTION(lpass_ext),
	FUNCTION(mi2s_2),
	FUNCTION(phase_flag25),
	FUNCTION(qup01),
	FUNCTION(tgu_ch0),
	FUNCTION(phase_flag26),
	FUNCTION(tgu_ch1),
	FUNCTION(phase_flag27),
	FUNCTION(tgu_ch2),
	FUNCTION(phase_flag28),
	FUNCTION(tgu_ch3),
	FUNCTION(phase_flag29),
	FUNCTION(MSS_LTE),
	FUNCTION(btfm_slimbus),
	FUNCTION(mi2s_1),
	FUNCTION(phase_flag30),
	FUNCTION(phase_flag31),
	FUNCTION(uim2_data),
	FUNCTION(uim2_clk),
	FUNCTION(uim2_reset),
	FUNCTION(uim2_present),
	FUNCTION(uim1_data),
	FUNCTION(uim1_clk),
	FUNCTION(uim1_reset),
	FUNCTION(uim1_present),
	FUNCTION(atest_usb1),
	FUNCTION(atest_usb10),
	FUNCTION(sd_write),
	FUNCTION(atest_usb11),
	FUNCTION(atest_usb12),
	FUNCTION(ddr_pxi0),
	FUNCTION(adsp_ext),
	FUNCTION(atest_usb13),
	FUNCTION(ddr_pxi1),
	FUNCTION(mi2s_0),
	FUNCTION(atest_usb2),
	FUNCTION(ddr_pxi2),
	FUNCTION(tsense_pwm1),
	FUNCTION(tsense_pwm2),
	FUNCTION(AGERA_PLL),
	FUNCTION(vsense_trigger),
	FUNCTION(atest_usb20),
	FUNCTION(ddr_pxi3),
	FUNCTION(JITTER_BIST),
	FUNCTION(wlan1_adc0),
	FUNCTION(atest_usb21),
	FUNCTION(wlan2_adc0),
	FUNCTION(atest_usb22),
	FUNCTION(atest_tsens),
	FUNCTION(wlan1_adc1),
	FUNCTION(atest_usb23),
	FUNCTION(MCLK),
	FUNCTION(atest_tsens2),
	FUNCTION(wlan2_adc1),
	FUNCTION(ldo_en),
	FUNCTION(atest_char),
	FUNCTION(ldo_update),
	FUNCTION(atest_char0),
	FUNCTION(prng_rosc0),
	FUNCTION(atest_char1),
	FUNCTION(atest_char2),
	FUNCTION(prng_rosc1),
	FUNCTION(pll_clk),
	FUNCTION(atest_char3),
	FUNCTION(prng_rosc2),
	FUNCTION(prng_rosc3),
	FUNCTION(nav_gpio),
	FUNCTION(NAV_PPS),
	FUNCTION(GPS_TX),
	FUNCTION(QLINK0_WMSS),
	FUNCTION(qlink0_request),
	FUNCTION(qlink0_enable),
	FUNCTION(QLINK1_WMSS),
	FUNCTION(qlink1_request),
	FUNCTION(qlink1_enable),
	FUNCTION(pa_indicator),
	FUNCTION(USB_PHY),
};

static const struct msm_spare_tlmm blair_spare_regs[] = {
	SPARE_REG(west_spare, 0),
	SPARE_REG(west_spare1, 0),
	SPARE_REG(north_spare, TLMM_NORTH_SPARE_OFFSET),
	SPARE_REG(north_spare1, TLMM_NORTH_SPARE1_OFFSET),
	SPARE_REG(south_spare, 0),
	SPARE_REG(south_spare1, 0),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup blair_groups[] = {
	[0] = PINGROUP(0, ibi_i3c, qup00, cri_trng, NA, NA, NA, NA, NA, NA,
		       0x9C018, 0),
	[1] = PINGROUP(1, ibi_i3c, qup00, cri_trng, NA, NA, NA, NA, NA, NA,
		       0, -1),
	[2] = PINGROUP(2, qup00, CCI_I2C, cri_trng, qdss_cti, NA, NA, NA, NA,
		       NA, 0, -1),
	[3] = PINGROUP(3, qup00, CCI_I2C, sp_cmu, dbg_out, qdss_cti, NA, NA,
		       NA, NA, 0x9C018, 1),
	[4] = PINGROUP(4, qup14, qup14, sdc1_tb, NA, NA, NA, NA, NA, NA,
		       0x9C00C, 3),
	[5] = PINGROUP(5, qup14, qup14, sdc2_tb, NA, NA, NA, NA, NA, NA, 0, -1),
	[6] = PINGROUP(6, MDP_VSYNC, qdss_cti, NA, NA, NA, NA, NA, NA, NA,
		       0, -1),
	[7] = PINGROUP(7, qdss_cti, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C018, 2),
	[8] = PINGROUP(8, GP_PDM1, qdss_gpio, NA, NA, NA, NA, NA, NA, NA,
		       0x9C018, 3),
	[9] = PINGROUP(9, qdss_gpio, NA, NA, NA, NA, NA, NA, NA, NA,
		       0x9C018, 4),
	[10] = PINGROUP(10, NA, NA, NA, NA, NA, NA, NA, NA, NA,	0, -1),
	[11] = PINGROUP(11, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C00C, 4),
	[12] = PINGROUP(12, m_voc, dp_hot, NA, phase_flag0, NA, NA, NA, NA, NA,
			0x9C00C, 5),
	[13] = PINGROUP(13, qup10, pll_bypassnl, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 6),
	[14] = PINGROUP(14, qup10, pll_reset, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[15] = PINGROUP(15, qup10, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[16] = PINGROUP(16, qup10, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C00C, 7),
	[17] = PINGROUP(17, NA, phase_flag1, qup10, NA, NA, NA, NA, NA, NA,
			0x9C00C, 8),
	[18] = PINGROUP(18, NA, phase_flag2, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 9),
	[19] = PINGROUP(19, qup12, qup12, ddr_bist, NA, NA, NA, NA, NA, NA,
			0x9C00C, 10),
	[20] = PINGROUP(20, qup12, qup12, ddr_bist, NA, NA, NA, NA, NA, NA,
			0, -1),
	[21] = PINGROUP(21, gcc_gp2, ddr_bist, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 11),
	[22] = PINGROUP(22, gcc_gp3, ddr_bist, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 12),
	[23] = PINGROUP(23, MDP_VSYNC, edp_lcd, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 13),
	[24] = PINGROUP(24, MDP_VSYNC, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 14),
	[25] = PINGROUP(25, qup13_f1, qup13_f2, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 15),
	[26] = PINGROUP(26, qup13_f1, qup13_f2, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[27] = PINGROUP(27, qup11_f1, qup11_f2, MDP_VSYNC, PLL_BIST, NA, qdss_gpio14,
			NA, NA, NA, 0x9C010, 0),
	[28] = PINGROUP(28, qup11_f1, qup11_f2, MDP_VSYNC, NA, qdss_gpio15, NA, NA,
			NA, NA, 0x9C010, 1),
	[29] = PINGROUP(29, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[30] = PINGROUP(30, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[31] = PINGROUP(31, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[32] = PINGROUP(32, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[33] = PINGROUP(33, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[34] = PINGROUP(34, cci_timer0, NA, phase_flag3, qdss_gpio12, NA, NA,
			NA, NA, NA, 0x9C018, 5),
	[35] = PINGROUP(35, cci_timer1, CCI_ASYNC, NA, phase_flag4,
			qdss_gpio13, NA, NA, NA, NA, 0x9C018, 6),
	[36] = PINGROUP(36, cci_timer2, CCI_ASYNC, NA, phase_flag5,
			qdss_gpio14, NA, NA, NA, NA, 0x9C018, 7),
	[37] = PINGROUP(37, cci_timer3, GP_PDM0, NA, phase_flag6, qdss_gpio15,
			NA, NA, NA, NA, 0x9C018, 8),
	[38] = PINGROUP(38, cci_timer4, NA, phase_flag7, qdss_gpio2, NA, NA,
			NA, NA, NA, 0x9C018, 9),
	[39] = PINGROUP(39, CCI_I2C, NA, phase_flag8, qdss_gpio0, NA, NA, NA,
			NA, NA, 0, -1),
	[40] = PINGROUP(40, CCI_I2C, NA, phase_flag9, qdss_gpio1, NA, NA, NA,
			NA, NA, 0, -1),
	[41] = PINGROUP(41, CCI_I2C, NA, phase_flag10, qdss_gpio2, NA, NA, NA,
			NA, NA, 0, -1),
	[42] = PINGROUP(42, CCI_I2C, NA, phase_flag11, qdss_gpio3, NA, NA, NA,
			NA, NA, 0, -1),
	[43] = PINGROUP(43, CCI_I2C, NA, phase_flag12, qdss_gpio4, NA, NA, NA,
			NA, NA, 0, -1),
	[44] = PINGROUP(44, CCI_I2C, NA, phase_flag13, qdss_gpio5, NA, NA, NA,
			NA, NA, 0, -1),
	[45] = PINGROUP(45, qup02, NA, phase_flag14, qdss_gpio6, NA, NA, NA,
			NA, NA, 0, -1),
	[46] = PINGROUP(46, qup02, NA, phase_flag15, qdss_gpio7, NA, NA, NA,
			NA, NA, 0, -1),
	[47] = PINGROUP(47, mdp_vsync0, NA, phase_flag16, qdss_gpio3, NA, NA,
			usb2phy_ac, NA, NA, 0, -1),
	[48] = PINGROUP(48, CCI_ASYNC, mdp_vsync1, gcc_gp1, NA, phase_flag17,
			qdss_gpio8, qup02, NA, NA, 0x9C018, 10),
	[49] = PINGROUP(49, vfr_1, NA, phase_flag18, qdss_gpio9, NA, NA, NA,
			NA, NA, 0, -1),
	[50] = PINGROUP(50, NA, phase_flag19, qdss_gpio10, NA, NA, NA, NA, NA,
			NA, 0x9C010, 2),
	[51] = PINGROUP(51, NA, phase_flag20, qdss_gpio11, NA, NA, NA, NA, NA,
			NA, 0x9C010, 3),
	[52] = PINGROUP(52, CCI_ASYNC, GP_PDM1, NA, phase_flag21, qdss_gpio12,
			NA, NA, NA, NA, 0x9C010, 4),
	[53] = PINGROUP(53, CCI_ASYNC, NA, phase_flag22, qdss_gpio13, NA, NA,
			NA, NA, NA, 0x9C010, 5),
	[54] = PINGROUP(54, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[55] = PINGROUP(55, NA, NA, NA, NA, NA, NA, NA, NA, NA,	0, -1),
	[56] = PINGROUP(56, qup02, mdp_vsync2, NA, phase_flag23, qdss_gpio10,
			NA, NA, NA, NA, 0, -1),
	[57] = PINGROUP(57, qup02, mdp_vsync3, GP_PDM2, NA, phase_flag24,
			qdss_gpio11, NA, NA, NA, 0x9C018, 11),
	[58] = PINGROUP(58, gcc_gp1, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C000, 1),
	[59] = PINGROUP(59, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C018, 12),
	[60] = PINGROUP(60, audio_ref, lpass_ext, mi2s_2, NA, phase_flag25, NA,
			NA, NA, NA, 0x9C000, 2),
	[61] = PINGROUP(61, qup01, tgu_ch0, NA, phase_flag26, qdss_cti, NA, NA,
			NA, NA, 0x9C000, 3),
	[62] = PINGROUP(62, qup01, tgu_ch1, NA, phase_flag27, qdss_cti, NA, NA,
			NA, NA, 0x9C000, 4),
	[63] = PINGROUP(63, qup01, tgu_ch2, NA, phase_flag28, qdss_gpio, NA,
			NA, NA, NA, 0, -1),
	[64] = PINGROUP(64, qup01, tgu_ch3, NA, phase_flag29, qdss_gpio, NA,
			NA, NA, NA, 0x9C000, 5),
	[65] = PINGROUP(65, MSS_LTE, NA, qdss_gpio0, NA, NA, NA, NA, NA, NA,
			0, -1),
	[66] = PINGROUP(66, MSS_LTE, NA, qdss_gpio1, NA, NA, NA, NA, NA, NA,
			0x9C000, 6),
	[67] = PINGROUP(67, btfm_slimbus, mi2s_1, NA, phase_flag30, NA, NA, NA,
			NA, NA, 0x9C000, 7),
	[68] = PINGROUP(68, btfm_slimbus, mi2s_1, GP_PDM0, NA, phase_flag31,
			NA, NA, NA, NA, 0x9C000, 8),
	[69] = PINGROUP(69, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C010, 8),
	[70] = PINGROUP(70, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[71] = PINGROUP(71, NA, NA, NA, NA, NA, NA, NA, NA, NA,	0, -1),
	[72] = PINGROUP(72, NA, NA, NA, NA, NA, NA, NA, NA, NA,	0, -1),
	[73] = PINGROUP(73, NA, NA, NA, NA, NA, NA, NA, NA, NA,	0, -1),
	[74] = PINGROUP(74, NA, NA, NA, NA, NA, NA, NA, NA, NA,	0, -1),
	[75] = PINGROUP(75, uim2_data, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[76] = PINGROUP(76, uim2_clk, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[77] = PINGROUP(77, uim2_reset, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[78] = PINGROUP(78, uim2_present, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C010, 9),
	[79] = PINGROUP(79, uim1_data, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[80] = PINGROUP(80, uim1_clk, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[81] = PINGROUP(81, uim1_reset, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[82] = PINGROUP(82, uim1_present, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C010, 10),
	[83] = PINGROUP(83, atest_usb1, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C010, 11),
	[84] = PINGROUP(84, NA, atest_usb10, NA, NA, NA, NA, NA, NA, NA,
			0x9C010, 12),
	[85] = PINGROUP(85, sd_write, NA, atest_usb11, NA, NA, NA, NA, NA, NA,
			0x9C010, 13),
	[86] = PINGROUP(86, btfm_slimbus, mi2s_1, NA, qdss_cti, atest_usb12,
			ddr_pxi0, NA, NA, NA, 0, -1),
	[87] = PINGROUP(87, btfm_slimbus, mi2s_1, adsp_ext, NA, qdss_cti,
			atest_usb13, ddr_pxi1, NA, NA, 0x9C000, 10),
	[88] = PINGROUP(88, mi2s_0, NA, qdss_gpio4, NA, atest_usb2, ddr_pxi2,
			tsense_pwm1, tsense_pwm2, NA, 0x9C000, 11),
	[89] = PINGROUP(89, mi2s_0, AGERA_PLL, NA, qdss_gpio5, NA,
			vsense_trigger, atest_usb20, ddr_pxi3, NA, 0x9C000, 12),
	[90] = PINGROUP(90, mi2s_0, JITTER_BIST, NA, qdss_gpio6, NA,
			wlan1_adc0, atest_usb21, ddr_pxi0, NA, 0x9C000, 13),
	[91] = PINGROUP(91, mi2s_0, NA, qdss_gpio7, NA, wlan2_adc0,
			atest_usb22, ddr_pxi1, NA, NA, 0x9C000, 14),
	[92] = PINGROUP(92, NA, qdss_gpio8, atest_tsens, wlan1_adc1,
			atest_usb23, ddr_pxi2, NA, NA, NA, 0x9C000, 15),
	[93] = PINGROUP(93, MCLK, lpass_ext, NA, qdss_gpio9, atest_tsens2,
			wlan2_adc1, ddr_pxi3, NA, NA, 0x9C004, 0),
	[94] = PINGROUP(94, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C010, 14),
	[95] = PINGROUP(95, ldo_en, NA, atest_char, NA, NA, NA, NA, NA, NA,
			0x9C010, 15),
	[96] = PINGROUP(96, ldo_update, NA, atest_char0, NA, NA, NA, NA, NA,
			NA, 0x9C014, 0),
	[97] = PINGROUP(97, prng_rosc0, NA, atest_char1, NA, NA, NA, NA, NA,
			NA, 0x9C014, 1),
	[98] = PINGROUP(98, NA, atest_char2, NA, NA, prng_rosc1, pll_clk, NA,
			NA, NA, 0x9C014, 2),
	[99] = PINGROUP(99, NA, atest_char3, NA, NA, prng_rosc2, NA, NA, NA,
			NA, 0x9C014, 3),
	[100] = PINGROUP(100, NA, NA, prng_rosc3, NA, NA, NA, NA, NA, NA,
			 0x9C014, 4),
	[101] = PINGROUP(101, nav_gpio, NAV_PPS, NAV_PPS, GPS_TX, NA, NA, NA,
			 NA, NA, 0, -1),
	[102] = PINGROUP(102, nav_gpio, NAV_PPS, NAV_PPS, GPS_TX, NA, NA, NA,
			 NA, NA, 0, -1),
	[103] = PINGROUP(103, QLINK0_WMSS, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[104] = PINGROUP(104, qlink0_request, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C014, 5),
	[105] = PINGROUP(105, qlink0_enable, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[106] = PINGROUP(106, QLINK1_WMSS, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[107] = PINGROUP(107, qlink1_request, GPS_TX, NA, NA, NA, NA, NA, NA,
			 NA, 0x9C014, 6),
	[108] = PINGROUP(108, qlink1_enable, GPS_TX, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[109] = PINGROUP(109, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[110] = PINGROUP(110, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C014, 7),
	[111] = PINGROUP(111, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[112] = PINGROUP(112, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C014, 8),
	[113] = PINGROUP(113, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[114] = PINGROUP(114, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C014, 9),
	[115] = PINGROUP(115, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[116] = PINGROUP(116, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C014, 10),
	[117] = PINGROUP(117, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[118] = PINGROUP(118, NA, NA, pa_indicator, dp_hot, NA, NA, NA, NA, NA,
			 0x9C014, 11),
	[119] = PINGROUP(119, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[120] = PINGROUP(120, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[121] = PINGROUP(121, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[122] = PINGROUP(122, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[123] = PINGROUP(123, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[124] = PINGROUP(124, USB_PHY, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C014, 12),
	[125] = PINGROUP(125, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C014, 13),
	[126] = PINGROUP(126, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C014, 14),
	[127] = PINGROUP(127, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[128] = PINGROUP(128, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 1),
	[129] = PINGROUP(129, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 2),
	[130] = PINGROUP(130, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[131] = PINGROUP(131, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 3),
	[132] = PINGROUP(132, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[133] = PINGROUP(133, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 4),
	[134] = PINGROUP(134, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 5),
	[135] = PINGROUP(135, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[136] = PINGROUP(136, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 6),
	[137] = PINGROUP(137, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[138] = PINGROUP(138, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[139] = PINGROUP(139, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[140] = PINGROUP(140, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[141] = PINGROUP(141, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[142] = PINGROUP(142, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 11),
	[143] = PINGROUP(143, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[144] = PINGROUP(144, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[145] = PINGROUP(145, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[146] = PINGROUP(146, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[147] = PINGROUP(147, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[148] = PINGROUP(148, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[149] = PINGROUP(149, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[150] = PINGROUP(150, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 14),
	[151] = PINGROUP(151, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[152] = PINGROUP(152, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[153] = PINGROUP(153, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 15),
	[154] = PINGROUP(154, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[155] = PINGROUP(155, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C008, 0),
	[156] = UFS_RESET(ufs_reset, 0x1ae000),
	[157] = SDC_QDSD_PINGROUP(sdc1_rclk, 0x1a1000, 0, 0),
	[158] = SDC_QDSD_PINGROUP(sdc1_clk, 0x1a0000, 13, 6),
	[159] = SDC_QDSD_PINGROUP(sdc1_cmd, 0x1a0000, 11, 3),
	[160] = SDC_QDSD_PINGROUP(sdc1_data, 0x1a0000, 9, 0),
	[161] = SDC_QDSD_PINGROUP(sdc2_clk, 0x1a2000, 14, 6),
	[162] = SDC_QDSD_PINGROUP(sdc2_cmd, 0x1a2000, 11, 3),
	[163] = SDC_QDSD_PINGROUP(sdc2_data, 0x1a2000, 9, 0),
};

static const int blair_reserved_gpios[] = {
	13, 14, 15, 16, 17, 45, 46, 48, 56, 57, -1
};

static const struct msm_gpio_wakeirq_map blair_mpm_map[] = {
	{0, 84},
	{3, 6},
	{4, 7},
	{7, 8},
	{8, 9},
	{9, 10},
	{11, 11},
	{12, 13},
	{13, 14},
	{16, 16},
	{17, 17},
	{18, 18},
	{19, 19},
	{21, 20},
	{22, 21},
	{23, 23},
	{24, 24},
	{25, 25},
	{27, 26},
	{28, 27},
	{37, 28},
	{38, 29},
	{48, 30},
	{50, 31},
	{51, 32},
	{52, 33},
	{57, 34},
	{59, 35},
	{60, 37},
	{61, 38},
	{62, 39},
	{64, 40},
	{66, 41},
	{67, 42},
	{68, 43},
	{69, 44},
	{78, 45},
	{82, 36},
	{83, 47},
	{84, 48},
	{85, 49},
	{87, 50},
	{88, 51},
	{91, 52},
	{94, 53},
	{95, 54},
	{96, 55},
	{97, 56},
	{98, 57},
	{99, 58},
	{100, 59},
	{104, 60},
	{107, 61},
	{118, 62},
	{124, 63},
	{125, 64},
	{126, 65},
	{128, 66},
	{129, 67},
	{131, 69},
	{133, 70},
	{134, 71},
	{136, 73},
	{142, 74},
	{150, 75},
	{153, 76},
	{155, 77},
};

static const struct msm_pinctrl_soc_data blair_pinctrl = {
	.pins = blair_pins,
	.npins = ARRAY_SIZE(blair_pins),
	.functions = blair_functions,
	.nfunctions = ARRAY_SIZE(blair_functions),
	.groups = blair_groups,
	.ngroups = ARRAY_SIZE(blair_groups),
	.reserved_gpios = blair_reserved_gpios,
	.ngpios = 157,
	.wakeirq_map = blair_mpm_map,
	.nwakeirq_map = ARRAY_SIZE(blair_mpm_map),
	.spare_regs = blair_spare_regs,
	.nspare_regs = ARRAY_SIZE(blair_spare_regs),
};

static int blair_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &blair_pinctrl);
}

static const struct of_device_id blair_pinctrl_of_match[] = {
	{ .compatible = "qcom,blair-pinctrl", },
	{ },
};

static struct platform_driver blair_pinctrl_driver = {
	.driver = {
		.name = "blair-pinctrl",
		.of_match_table = blair_pinctrl_of_match,
	},
	.probe = blair_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init blair_pinctrl_init(void)
{
	return platform_driver_register(&blair_pinctrl_driver);
}
arch_initcall(blair_pinctrl_init);

static void __exit blair_pinctrl_exit(void)
{
	platform_driver_unregister(&blair_pinctrl_driver);
}
module_exit(blair_pinctrl_exit);

MODULE_DESCRIPTION("QTI blair pinctrl driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, blair_pinctrl_of_match);
