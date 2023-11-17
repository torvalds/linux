// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
#define REG_DIRCONN 0xA9000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, wake_off, bit)	\
	{					        \
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = (unsigned int)ARRAY_SIZE(gpio##id##_pins),	\
		.ctl_reg = REG_BASE + REG_SIZE * id,			\
		.io_reg = REG_BASE + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = REG_BASE + 0x8 + REG_SIZE * id,		\
		.intr_status_reg = REG_BASE + 0xc + REG_SIZE * id,	\
		.intr_target_reg = REG_BASE + 0x8 + REG_SIZE * id,	\
		.dir_conn_reg = REG_BASE + REG_DIRCONN, \
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
		.intr_target_bit = 5,		\
		.intr_target_kpss_val = 3,	\
		.intr_raw_status_bit = 4,	\
		.intr_polarity_bit = 1,		\
		.intr_detection_bit = 2,	\
		.intr_detection_width = 2,	\
		.dir_conn_en_bit = 9,		\
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
			msm_mux_##f10			\
		},					        \
		.nfuncs = 11,				\
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
		.offset = REG_BASE + qup_offset,			\
	}


static const struct pinctrl_pin_desc monaco_auto_pins[] = {
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
	PINCTRL_PIN(133, "UFS_RESET"),
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

static const unsigned int ufs_reset_pins[] = { 133 };

enum monaco_auto_functions {
	msm_mux_gpio,
	msm_mux_aoss_cti,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_atest_char_start,
	msm_mux_atest_usb2,
	msm_mux_atest_usb20,
	msm_mux_atest_usb21,
	msm_mux_atest_usb22,
	msm_mux_atest_usb23,
	msm_mux_audio_ref_clk,
	msm_mux_cam_mclk,
	msm_mux_cci_async_in0,
	msm_mux_cci_async_in1,
	msm_mux_cci_async_in2,
	msm_mux_cci_async_in3,
	msm_mux_cci_async_in4,
	msm_mux_cci_async_in5,
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
	msm_mux_cci_timer5,
	msm_mux_cci_timer6,
	msm_mux_cci_timer7,
	msm_mux_cci_timer8,
	msm_mux_cci_timer9,
	msm_mux_cri_trng,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_dbg_out_clk,
	msm_mux_ddr_bist_complete,
	msm_mux_ddr_bist_fail,
	msm_mux_ddr_bist_start,
	msm_mux_ddr_bist_stop,
	msm_mux_ddr_pxi0,
	msm_mux_ddr_pxi1,
	msm_mux_ddr_pxi2,
	msm_mux_ddr_pxi3,
	msm_mux_edp0_hot,
	msm_mux_edp0_lcd,
	msm_mux_edp1_lcd,
	msm_mux_emac0_mcg0,
	msm_mux_emac0_mcg1,
	msm_mux_emac0_mcg2,
	msm_mux_emac0_mcg3,
	msm_mux_emac0_mdc,
	msm_mux_emac0_mdio,
	msm_mux_emac0_ptp,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_gcc_gp4,
	msm_mux_gcc_gp5,
	msm_mux_hs0_mi2s_data0,
	msm_mux_hs0_mi2s_data1,
	msm_mux_hs0_mi2s_sck,
	msm_mux_hs0_mi2s_ws,
	msm_mux_hs1_mi2s,
	msm_mux_hs2_mi2s,
	msm_mux_ibi_i3c,
	msm_mux_jitter_bist,
	msm_mux_mdp0_vsync0_out,
	msm_mux_mdp0_vsync1_out,
	msm_mux_mdp0_vsync3_out,
	msm_mux_mdp0_vsync6_out,
	msm_mux_mdp0_vsync7_out,
	msm_mux_mdp_vsync_e,
	msm_mux_mdp_vsync_p,
	msm_mux_mdp_vsync_s,
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
	msm_mux_pcie0_clkreq_n,
	msm_mux_pcie1_clkreq_n,
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
	msm_mux_qup0_se0_l0,
	msm_mux_qup0_se0_l1,
	msm_mux_qup0_se0_l2,
	msm_mux_qup0_se0_l3,
	msm_mux_qup0_se1_l0,
	msm_mux_qup0_se1_l1,
	msm_mux_qup0_se1_l2,
	msm_mux_qup0_se1_l3,
	msm_mux_qup0_se2_l0,
	msm_mux_qup0_se2_l1,
	msm_mux_qup0_se2_l2,
	msm_mux_qup0_se2_l3,
	msm_mux_qup0_se3_l0,
	msm_mux_qup0_se3_l1,
	msm_mux_qup0_se3_l2,
	msm_mux_qup0_se3_l3,
	msm_mux_qup0_se4_l0,
	msm_mux_qup0_se4_l1,
	msm_mux_qup0_se4_l2,
	msm_mux_qup0_se4_l3,
	msm_mux_qup0_se5_l0,
	msm_mux_qup0_se5_l1,
	msm_mux_qup0_se5_l2,
	msm_mux_qup0_se5_l3,
	msm_mux_qup0_se6_l0,
	msm_mux_qup0_se6_l1,
	msm_mux_qup0_se6_l2,
	msm_mux_qup0_se6_l3,
	msm_mux_qup0_se7_l0,
	msm_mux_qup0_se7_l1,
	msm_mux_qup0_se7_l2,
	msm_mux_qup0_se7_l3,
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
	msm_mux_qup2_se0_l4,
	msm_mux_qup2_se0_l5,
	msm_mux_qup2_se0_l6,
	msm_mux_sailss_emac0,
	msm_mux_sailss_ospi,
	msm_mux_sgmii_phy_intr0_n,
	msm_mux_tb_trig_sdc1,
	msm_mux_tb_trig_sdc4,
	msm_mux_tgu_ch0_trigout,
	msm_mux_tgu_ch1_trigout,
	msm_mux_tgu_ch2_trigout,
	msm_mux_tgu_ch3_trigout,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_tsense_pwm3,
	msm_mux_tsense_pwm4,
	msm_mux_usb2phy_ac_en0,
	msm_mux_usb2phy_ac_en2,
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
	"gpio132",
};
static const char *const aoss_cti_groups[] = {
	"gpio37", "gpio38", "gpio39", "gpio40",
};
static const char *const atest_char0_groups[] = {
	"gpio70",
};
static const char *const atest_char1_groups[] = {
	"gpio71",
};
static const char *const atest_char2_groups[] = {
	"gpio72",
};
static const char *const atest_char3_groups[] = {
	"gpio93",
};
static const char *const atest_char_start_groups[] = {
	"gpio66",
};
static const char *const atest_usb2_groups[] = {
	"gpio63", "gpio83", "gpio92",
};
static const char *const atest_usb20_groups[] = {
	"gpio74", "gpio84", "gpio87",
};
static const char *const atest_usb21_groups[] = {
	"gpio67", "gpio75", "gpio85",
};
static const char *const atest_usb22_groups[] = {
	"gpio65", "gpio68", "gpio80",
};
static const char *const atest_usb23_groups[] = {
	"gpio64", "gpio69", "gpio81",
};
static const char *const audio_ref_clk_groups[] = {
	"gpio105",
};
static const char *const cam_mclk_groups[] = {
	"gpio67", "gpio68", "gpio69",
};
static const char *const cci_async_in0_groups[] = {
	"gpio63",
};
static const char *const cci_async_in1_groups[] = {
	"gpio64",
};
static const char *const cci_async_in2_groups[] = {
	"gpio65",
};
static const char *const cci_async_in3_groups[] = {
	"gpio29",
};
static const char *const cci_async_in4_groups[] = {
	"gpio30",
};
static const char *const cci_async_in5_groups[] = {
	"gpio31",
};
static const char *const cci_i2c_scl0_groups[] = {
	"gpio58",
};
static const char *const cci_i2c_scl1_groups[] = {
	"gpio30",
};
static const char *const cci_i2c_scl2_groups[] = {
	"gpio60",
};
static const char *const cci_i2c_scl3_groups[] = {
	"gpio32",
};
static const char *const cci_i2c_scl4_groups[] = {
	"gpio62",
};
static const char *const cci_i2c_scl5_groups[] = {
	"gpio55",
};
static const char *const cci_i2c_sda0_groups[] = {
	"gpio57",
};
static const char *const cci_i2c_sda1_groups[] = {
	"gpio29",
};
static const char *const cci_i2c_sda2_groups[] = {
	"gpio59",
};
static const char *const cci_i2c_sda3_groups[] = {
	"gpio31",
};
static const char *const cci_i2c_sda4_groups[] = {
	"gpio61",
};
static const char *const cci_i2c_sda5_groups[] = {
	"gpio54",
};
static const char *const cci_timer0_groups[] = {
	"gpio63",
};
static const char *const cci_timer1_groups[] = {
	"gpio64",
};
static const char *const cci_timer2_groups[] = {
	"gpio65",
};
static const char *const cci_timer3_groups[] = {
	"gpio49",
};
static const char *const cci_timer4_groups[] = {
	"gpio50",
};
static const char *const cci_timer5_groups[] = {
	"gpio19",
};
static const char *const cci_timer6_groups[] = {
	"gpio20",
};
static const char *const cci_timer7_groups[] = {
	"gpio21",
};
static const char *const cci_timer8_groups[] = {
	"gpio22",
};
static const char *const cci_timer9_groups[] = {
	"gpio23",
};
static const char *const cri_trng_groups[] = {
	"gpio92",
};
static const char *const cri_trng0_groups[] = {
	"gpio90",
};
static const char *const cri_trng1_groups[] = {
	"gpio91",
};
static const char *const dbg_out_clk_groups[] = {
	"gpio75",
};
static const char *const ddr_bist_complete_groups[] = {
	"gpio54",
};
static const char *const ddr_bist_fail_groups[] = {
	"gpio56",
};
static const char *const ddr_bist_start_groups[] = {
	"gpio53",
};
static const char *const ddr_bist_stop_groups[] = {
	"gpio55",
};
static const char *const ddr_pxi0_groups[] = {
	"gpio68", "gpio69",
};
static const char *const ddr_pxi1_groups[] = {
	"gpio49", "gpio50",
};
static const char *const ddr_pxi2_groups[] = {
	"gpio52", "gpio83",
};
static const char *const ddr_pxi3_groups[] = {
	"gpio80", "gpio81",
};
static const char *const edp0_hot_groups[] = {
	"gpio94",
};
static const char *const edp0_lcd_groups[] = {
	"gpio48",
};
static const char *const edp1_lcd_groups[] = {
	"gpio49",
};
static const char *const emac0_mcg0_groups[] = {
	"gpio10",
};
static const char *const emac0_mcg1_groups[] = {
	"gpio11",
};
static const char *const emac0_mcg2_groups[] = {
	"gpio24",
};
static const char *const emac0_mcg3_groups[] = {
	"gpio79",
};
static const char *const emac0_mdc_groups[] = {
	"gpio5",
};
static const char *const emac0_mdio_groups[] = {
	"gpio6",
};
static const char *const emac0_ptp_groups[] = {
	"gpio24", "gpio29", "gpio30", "gpio31", "gpio32", "gpio79",
};
static const char *const gcc_gp1_groups[] = {
	"gpio35", "gpio84",
};
static const char *const gcc_gp2_groups[] = {
	"gpio36", "gpio81",
};
static const char *const gcc_gp3_groups[] = {
	"gpio69", "gpio82",
};
static const char *const gcc_gp4_groups[] = {
	"gpio68", "gpio83",
};
static const char *const gcc_gp5_groups[] = {
	"gpio76", "gpio77",
};
static const char *const hs0_mi2s_data0_groups[] = {
	"gpio108",
};
static const char *const hs0_mi2s_data1_groups[] = {
	"gpio109",
};
static const char *const hs0_mi2s_sck_groups[] = {
	"gpio106",
};
static const char *const hs0_mi2s_ws_groups[] = {
	"gpio107",
};
static const char *const hs1_mi2s_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48",
};
static const char *const hs2_mi2s_groups[] = {
	"gpio49", "gpio50", "gpio51", "gpio52",
};
static const char *const ibi_i3c_groups[] = {
	"gpio17", "gpio18", "gpio19", "gpio20", "gpio37", "gpio38",
	"gpio39", "gpio40",
};
static const char *const jitter_bist_groups[] = {
	"gpio97",
};
static const char *const mdp0_vsync0_out_groups[] = {
	"gpio89",
};
static const char *const mdp0_vsync1_out_groups[] = {
	"gpio90",
};
static const char *const mdp0_vsync3_out_groups[] = {
	"gpio91",
};
static const char *const mdp0_vsync6_out_groups[] = {
	"gpio80",
};
static const char *const mdp0_vsync7_out_groups[] = {
	"gpio81",
};
static const char *const mdp_vsync_e_groups[] = {
	"gpio42",
};
static const char *const mdp_vsync_p_groups[] = {
	"gpio52",
};
static const char *const mdp_vsync_s_groups[] = {
	"gpio32",
};
static const char *const mi2s1_data0_groups[] = {
	"gpio100",
};
static const char *const mi2s1_data1_groups[] = {
	"gpio101",
};
static const char *const mi2s1_sck_groups[] = {
	"gpio98",
};
static const char *const mi2s1_ws_groups[] = {
	"gpio99",
};
static const char *const mi2s2_data0_groups[] = {
	"gpio104",
};
static const char *const mi2s2_data1_groups[] = {
	"gpio105",
};
static const char *const mi2s2_sck_groups[] = {
	"gpio102",
};
static const char *const mi2s2_ws_groups[] = {
	"gpio103",
};
static const char *const mi2s_mclk0_groups[] = {
	"gpio97",
};
static const char *const mi2s_mclk1_groups[] = {
	"gpio109",
};
static const char *const pcie0_clkreq_n_groups[] = {
	"gpio1",
};
static const char *const pcie1_clkreq_n_groups[] = {
	"gpio22",
};
static const char *const phase_flag0_groups[] = {
	"gpio66",
};
static const char *const phase_flag1_groups[] = {
	"gpio56",
};
static const char *const phase_flag10_groups[] = {
	"gpio118",
};
static const char *const phase_flag11_groups[] = {
	"gpio117",
};
static const char *const phase_flag12_groups[] = {
	"gpio116",
};
static const char *const phase_flag13_groups[] = {
	"gpio3",
};
static const char *const phase_flag14_groups[] = {
	"gpio114",
};
static const char *const phase_flag15_groups[] = {
	"gpio113",
};
static const char *const phase_flag16_groups[] = {
	"gpio112",
};
static const char *const phase_flag17_groups[] = {
	"gpio111",
};
static const char *const phase_flag18_groups[] = {
	"gpio110",
};
static const char *const phase_flag19_groups[] = {
	"gpio28",
};
static const char *const phase_flag2_groups[] = {
	"gpio55",
};
static const char *const phase_flag20_groups[] = {
	"gpio108",
};
static const char *const phase_flag21_groups[] = {
	"gpio107",
};
static const char *const phase_flag22_groups[] = {
	"gpio106",
};
static const char *const phase_flag23_groups[] = {
	"gpio105",
};
static const char *const phase_flag24_groups[] = {
	"gpio104",
};
static const char *const phase_flag25_groups[] = {
	"gpio103",
};
static const char *const phase_flag26_groups[] = {
	"gpio102",
};
static const char *const phase_flag27_groups[] = {
	"gpio101",
};
static const char *const phase_flag28_groups[] = {
	"gpio100",
};
static const char *const phase_flag29_groups[] = {
	"gpio99",
};
static const char *const phase_flag3_groups[] = {
	"gpio125",
};
static const char *const phase_flag30_groups[] = {
	"gpio98",
};
static const char *const phase_flag31_groups[] = {
	"gpio54",
};
static const char *const phase_flag4_groups[] = {
	"gpio25",
};
static const char *const phase_flag5_groups[] = {
	"gpio26",
};
static const char *const phase_flag6_groups[] = {
	"gpio122",
};
static const char *const phase_flag7_groups[] = {
	"gpio121",
};
static const char *const phase_flag8_groups[] = {
	"gpio120",
};
static const char *const phase_flag9_groups[] = {
	"gpio9",
};
static const char *const pll_bist_sync_groups[] = {
	"gpio107",
};
static const char *const pll_clk_aux_groups[] = {
	"gpio74",
};
static const char *const prng_rosc0_groups[] = {
	"gpio57",
};
static const char *const prng_rosc1_groups[] = {
	"gpio58",
};
static const char *const prng_rosc2_groups[] = {
	"gpio59",
};
static const char *const prng_rosc3_groups[] = {
	"gpio60",
};
static const char *const qdss_cti_groups[] = {
	"gpio4", "gpio5", "gpio23", "gpio24", "gpio49", "gpio50",
	"gpio51", "gpio52",
};
static const char *const qdss_gpio_groups[] = {
	"gpio57", "gpio58", "gpio97", "gpio106",
};
static const char *const qdss_gpio0_groups[] = {
	"gpio59", "gpio107",
};
static const char *const qdss_gpio1_groups[] = {
	"gpio60", "gpio108",
};
static const char *const qdss_gpio10_groups[] = {
	"gpio36", "gpio100",
};
static const char *const qdss_gpio11_groups[] = {
	"gpio61", "gpio101",
};
static const char *const qdss_gpio12_groups[] = {
	"gpio62", "gpio102",
};
static const char *const qdss_gpio13_groups[] = {
	"gpio33", "gpio103",
};
static const char *const qdss_gpio14_groups[] = {
	"gpio34", "gpio104",
};
static const char *const qdss_gpio15_groups[] = {
	"gpio75", "gpio105",
};
static const char *const qdss_gpio2_groups[] = {
	"gpio72", "gpio109",
};
static const char *const qdss_gpio3_groups[] = {
	"gpio71", "gpio110",
};
static const char *const qdss_gpio4_groups[] = {
	"gpio70", "gpio111",
};
static const char *const qdss_gpio5_groups[] = {
	"gpio63", "gpio112",
};
static const char *const qdss_gpio6_groups[] = {
	"gpio64", "gpio113",
};
static const char *const qdss_gpio7_groups[] = {
	"gpio65", "gpio114",
};
static const char *const qdss_gpio8_groups[] = {
	"gpio73", "gpio98",
};
static const char *const qdss_gpio9_groups[] = {
	"gpio74", "gpio99",
};
static const char *const qup0_se0_l0_groups[] = {
	"gpio17",
};
static const char *const qup0_se0_l1_groups[] = {
	"gpio18",
};
static const char *const qup0_se0_l2_groups[] = {
	"gpio19",
};
static const char *const qup0_se0_l3_groups[] = {
	"gpio20",
};
static const char *const qup0_se1_l0_groups[] = {
	"gpio19",
};
static const char *const qup0_se1_l1_groups[] = {
	"gpio20",
};
static const char *const qup0_se1_l2_groups[] = {
	"gpio17",
};
static const char *const qup0_se1_l3_groups[] = {
	"gpio18",
};
static const char *const qup0_se2_l0_groups[] = {
	"gpio33",
};
static const char *const qup0_se2_l1_groups[] = {
	"gpio34",
};
static const char *const qup0_se2_l2_groups[] = {
	"gpio35",
};
static const char *const qup0_se2_l3_groups[] = {
	"gpio36",
};
static const char *const qup0_se3_l0_groups[] = {
	"gpio25",
};
static const char *const qup0_se3_l1_groups[] = {
	"gpio26",
};
static const char *const qup0_se3_l2_groups[] = {
	"gpio27",
};
static const char *const qup0_se3_l3_groups[] = {
	"gpio28",
};
static const char *const qup0_se4_l0_groups[] = {
	"gpio29",
};
static const char *const qup0_se4_l1_groups[] = {
	"gpio30",
};
static const char *const qup0_se4_l2_groups[] = {
	"gpio31",
};
static const char *const qup0_se4_l3_groups[] = {
	"gpio32",
};
static const char *const qup0_se5_l0_groups[] = {
	"gpio21",
};
static const char *const qup0_se5_l1_groups[] = {
	"gpio22",
};
static const char *const qup0_se5_l2_groups[] = {
	"gpio23",
};
static const char *const qup0_se5_l3_groups[] = {
	"gpio24",
};
static const char *const qup0_se6_l0_groups[] = {
	"gpio80",
};
static const char *const qup0_se6_l1_groups[] = {
	"gpio81",
};
static const char *const qup0_se6_l2_groups[] = {
	"gpio82",
};
static const char *const qup0_se6_l3_groups[] = {
	"gpio83",
};
static const char *const qup0_se7_l0_groups[] = {
	"gpio43",
};
static const char *const qup0_se7_l1_groups[] = {
	"gpio44",
};
static const char *const qup0_se7_l2_groups[] = {
	"gpio43",
};
static const char *const qup0_se7_l3_groups[] = {
	"gpio44",
};
static const char *const qup1_se0_l0_groups[] = {
	"gpio37",
};
static const char *const qup1_se0_l1_groups[] = {
	"gpio38",
};
static const char *const qup1_se0_l2_groups[] = {
	"gpio39",
};
static const char *const qup1_se0_l3_groups[] = {
	"gpio40",
};
static const char *const qup1_se1_l0_groups[] = {
	"gpio39",
};
static const char *const qup1_se1_l1_groups[] = {
	"gpio40",
};
static const char *const qup1_se1_l2_groups[] = {
	"gpio37",
};
static const char *const qup1_se1_l3_groups[] = {
	"gpio38",
};
static const char *const qup1_se2_l0_groups[] = {
	"gpio84",
};
static const char *const qup1_se2_l1_groups[] = {
	"gpio85",
};
static const char *const qup1_se2_l2_groups[] = {
	"gpio86",
};
static const char *const qup1_se2_l3_groups[] = {
	"gpio87",
};
static const char *const qup1_se2_l4_groups[] = {
	"gpio88",
};
static const char *const qup1_se3_l0_groups[] = {
	"gpio41",
};
static const char *const qup1_se3_l1_groups[] = {
	"gpio42",
};
static const char *const qup1_se3_l2_groups[] = {
	"gpio41",
};
static const char *const qup1_se3_l3_groups[] = {
	"gpio42",
};
static const char *const qup1_se4_l0_groups[] = {
	"gpio45",
};
static const char *const qup1_se4_l1_groups[] = {
	"gpio46",
};
static const char *const qup1_se4_l2_groups[] = {
	"gpio47",
};
static const char *const qup1_se4_l3_groups[] = {
	"gpio48",
};
static const char *const qup1_se5_l0_groups[] = {
	"gpio49",
};
static const char *const qup1_se5_l1_groups[] = {
	"gpio50",
};
static const char *const qup1_se5_l2_groups[] = {
	"gpio51",
};
static const char *const qup1_se5_l3_groups[] = {
	"gpio52",
};
static const char *const qup1_se6_l0_groups[] = {
	"gpio89",
};
static const char *const qup1_se6_l1_groups[] = {
	"gpio90",
};
static const char *const qup1_se6_l2_groups[] = {
	"gpio91",
};
static const char *const qup1_se6_l3_groups[] = {
	"gpio92",
};
static const char *const qup1_se7_l0_groups[] = {
	"gpio91",
};
static const char *const qup1_se7_l1_groups[] = {
	"gpio92",
};
static const char *const qup1_se7_l2_groups[] = {
	"gpio89",
};
static const char *const qup1_se7_l3_groups[] = {
	"gpio90",
};
static const char *const qup2_se0_l0_groups[] = {
	"gpio10",
};
static const char *const qup2_se0_l1_groups[] = {
	"gpio11",
};
static const char *const qup2_se0_l2_groups[] = {
	"gpio12",
};
static const char *const qup2_se0_l3_groups[] = {
	"gpio13",
};
static const char *const qup2_se0_l4_groups[] = {
	"gpio14",
};
static const char *const qup2_se0_l5_groups[] = {
	"gpio15",
};
static const char *const qup2_se0_l6_groups[] = {
	"gpio16",
};
static const char *const sailss_emac0_groups[] = {
	"gpio15", "gpio16",
};
static const char *const sailss_ospi_groups[] = {
	"gpio15", "gpio16",
};
static const char *const sgmii_phy_intr0_n_groups[] = {
	"gpio4",
};
static const char *const tb_trig_sdc1_groups[] = {
	"gpio14",
};
static const char *const tb_trig_sdc4_groups[] = {
	"gpio14",
};
static const char *const tgu_ch0_trigout_groups[] = {
	"gpio43",
};
static const char *const tgu_ch1_trigout_groups[] = {
	"gpio44",
};
static const char *const tgu_ch2_trigout_groups[] = {
	"gpio29",
};
static const char *const tgu_ch3_trigout_groups[] = {
	"gpio30",
};
static const char *const tsense_pwm1_groups[] = {
	"gpio79",
};
static const char *const tsense_pwm2_groups[] = {
	"gpio78",
};
static const char *const tsense_pwm3_groups[] = {
	"gpio77",
};
static const char *const tsense_pwm4_groups[] = {
	"gpio76",
};
static const char *const usb2phy_ac_en0_groups[] = {
	"gpio7",
};
static const char *const usb2phy_ac_en2_groups[] = {
	"gpio8",
};
static const char *const vsense_trigger_mirnat_groups[] = {
	"gpio67",
};

static const struct msm_function monaco_auto_functions[] = {
	FUNCTION(gpio),
	FUNCTION(aoss_cti),
	FUNCTION(atest_char0),
	FUNCTION(atest_char1),
	FUNCTION(atest_char2),
	FUNCTION(atest_char3),
	FUNCTION(atest_char_start),
	FUNCTION(atest_usb2),
	FUNCTION(atest_usb20),
	FUNCTION(atest_usb21),
	FUNCTION(atest_usb22),
	FUNCTION(atest_usb23),
	FUNCTION(audio_ref_clk),
	FUNCTION(cam_mclk),
	FUNCTION(cci_async_in0),
	FUNCTION(cci_async_in1),
	FUNCTION(cci_async_in2),
	FUNCTION(cci_async_in3),
	FUNCTION(cci_async_in4),
	FUNCTION(cci_async_in5),
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
	FUNCTION(cci_timer5),
	FUNCTION(cci_timer6),
	FUNCTION(cci_timer7),
	FUNCTION(cci_timer8),
	FUNCTION(cci_timer9),
	FUNCTION(cri_trng),
	FUNCTION(cri_trng0),
	FUNCTION(cri_trng1),
	FUNCTION(dbg_out_clk),
	FUNCTION(ddr_bist_complete),
	FUNCTION(ddr_bist_fail),
	FUNCTION(ddr_bist_start),
	FUNCTION(ddr_bist_stop),
	FUNCTION(ddr_pxi0),
	FUNCTION(ddr_pxi1),
	FUNCTION(ddr_pxi2),
	FUNCTION(ddr_pxi3),
	FUNCTION(edp0_hot),
	FUNCTION(edp0_lcd),
	FUNCTION(edp1_lcd),
	FUNCTION(emac0_mcg0),
	FUNCTION(emac0_mcg1),
	FUNCTION(emac0_mcg2),
	FUNCTION(emac0_mcg3),
	FUNCTION(emac0_mdc),
	FUNCTION(emac0_mdio),
	FUNCTION(emac0_ptp),
	FUNCTION(gcc_gp1),
	FUNCTION(gcc_gp2),
	FUNCTION(gcc_gp3),
	FUNCTION(gcc_gp4),
	FUNCTION(gcc_gp5),
	FUNCTION(hs0_mi2s_data0),
	FUNCTION(hs0_mi2s_data1),
	FUNCTION(hs0_mi2s_sck),
	FUNCTION(hs0_mi2s_ws),
	FUNCTION(hs1_mi2s),
	FUNCTION(hs2_mi2s),
	FUNCTION(ibi_i3c),
	FUNCTION(jitter_bist),
	FUNCTION(mdp0_vsync0_out),
	FUNCTION(mdp0_vsync1_out),
	FUNCTION(mdp0_vsync3_out),
	FUNCTION(mdp0_vsync6_out),
	FUNCTION(mdp0_vsync7_out),
	FUNCTION(mdp_vsync_e),
	FUNCTION(mdp_vsync_p),
	FUNCTION(mdp_vsync_s),
	FUNCTION(mi2s1_data0),
	FUNCTION(mi2s1_data1),
	FUNCTION(mi2s1_sck),
	FUNCTION(mi2s1_ws),
	FUNCTION(mi2s2_data0),
	FUNCTION(mi2s2_data1),
	FUNCTION(mi2s2_sck),
	FUNCTION(mi2s2_ws),
	FUNCTION(mi2s_mclk0),
	FUNCTION(mi2s_mclk1),
	FUNCTION(pcie0_clkreq_n),
	FUNCTION(pcie1_clkreq_n),
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
	FUNCTION(qup0_se0_l0),
	FUNCTION(qup0_se0_l1),
	FUNCTION(qup0_se0_l2),
	FUNCTION(qup0_se0_l3),
	FUNCTION(qup0_se1_l0),
	FUNCTION(qup0_se1_l1),
	FUNCTION(qup0_se1_l2),
	FUNCTION(qup0_se1_l3),
	FUNCTION(qup0_se2_l0),
	FUNCTION(qup0_se2_l1),
	FUNCTION(qup0_se2_l2),
	FUNCTION(qup0_se2_l3),
	FUNCTION(qup0_se3_l0),
	FUNCTION(qup0_se3_l1),
	FUNCTION(qup0_se3_l2),
	FUNCTION(qup0_se3_l3),
	FUNCTION(qup0_se4_l0),
	FUNCTION(qup0_se4_l1),
	FUNCTION(qup0_se4_l2),
	FUNCTION(qup0_se4_l3),
	FUNCTION(qup0_se5_l0),
	FUNCTION(qup0_se5_l1),
	FUNCTION(qup0_se5_l2),
	FUNCTION(qup0_se5_l3),
	FUNCTION(qup0_se6_l0),
	FUNCTION(qup0_se6_l1),
	FUNCTION(qup0_se6_l2),
	FUNCTION(qup0_se6_l3),
	FUNCTION(qup0_se7_l0),
	FUNCTION(qup0_se7_l1),
	FUNCTION(qup0_se7_l2),
	FUNCTION(qup0_se7_l3),
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
	FUNCTION(qup2_se0_l4),
	FUNCTION(qup2_se0_l5),
	FUNCTION(qup2_se0_l6),
	FUNCTION(sailss_emac0),
	FUNCTION(sailss_ospi),
	FUNCTION(sgmii_phy_intr0_n),
	FUNCTION(tb_trig_sdc1),
	FUNCTION(tb_trig_sdc4),
	FUNCTION(tgu_ch0_trigout),
	FUNCTION(tgu_ch1_trigout),
	FUNCTION(tgu_ch2_trigout),
	FUNCTION(tgu_ch3_trigout),
	FUNCTION(tsense_pwm1),
	FUNCTION(tsense_pwm2),
	FUNCTION(tsense_pwm3),
	FUNCTION(tsense_pwm4),
	FUNCTION(usb2phy_ac_en0),
	FUNCTION(usb2phy_ac_en2),
	FUNCTION(vsense_trigger_mirnat),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup monaco_auto_groups[] = {
	[0] = PINGROUP(0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x8500C, 0),
	[1] = PINGROUP(1, pcie0_clkreq_n, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		       0x8500C, 1),
	[2] = PINGROUP(2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x8500C, 2),
	[3] = PINGROUP(3, phase_flag13, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		       0x85000, 0),
	[4] = PINGROUP(4, sgmii_phy_intr0_n, qdss_cti, NA, NA, NA, NA, NA, NA,
		       NA, NA, 0x85018, 1),
	[5] = PINGROUP(5, emac0_mdc, qdss_cti, NA, NA, NA, NA, NA, NA, NA, NA,
		       0, -1),
	[6] = PINGROUP(6, emac0_mdio, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
		       -1),
	[7] = PINGROUP(7, usb2phy_ac_en0, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
		       -1),
	[8] = PINGROUP(8, usb2phy_ac_en2, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
		       -1),
	[9] = PINGROUP(9, phase_flag9, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		       0x85000, 1),
	[10] = PINGROUP(10, qup2_se0_l0, emac0_mcg0, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x85018, 2),
	[11] = PINGROUP(11, qup2_se0_l1, emac0_mcg1, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x85018, 3),
	[12] = PINGROUP(12, qup2_se0_l2, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			-1),
	[13] = PINGROUP(13, qup2_se0_l3, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x85018, 4),
	[14] = PINGROUP(14, qup2_se0_l4, tb_trig_sdc1, tb_trig_sdc4, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[15] = PINGROUP(15, qup2_se0_l5, NA, sailss_ospi, sailss_emac0, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[16] = PINGROUP(16, qup2_se0_l6, NA, NA, sailss_ospi, sailss_emac0, NA,
			NA, NA, NA, NA, 0x85018, 5),
	[17] = PINGROUP(17, qup0_se0_l0, qup0_se1_l2, ibi_i3c, NA, NA, NA, NA,
			NA, NA, NA, 0x85010, 0),
	[18] = PINGROUP(18, qup0_se0_l1, qup0_se1_l3, ibi_i3c, NA, NA, NA, NA,
			NA, NA, NA, 0x85010, 1),
	[19] = PINGROUP(19, qup0_se1_l0, qup0_se0_l2, cci_timer5, ibi_i3c, NA,
			NA, NA, NA, NA, NA, 0x85010, 2),
	[20] = PINGROUP(20, qup0_se1_l1, qup0_se0_l3, cci_timer6, ibi_i3c, NA,
			NA, NA, NA, NA, NA, 0x85010, 3),
	[21] = PINGROUP(21, qup0_se5_l0, cci_timer7, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x85000, 2),
	[22] = PINGROUP(22, pcie1_clkreq_n, qup0_se5_l1, cci_timer8, NA, NA, NA,
			NA, NA, NA, NA, 0x85000, 3),
	[23] = PINGROUP(23, qup0_se5_l2, cci_timer9, qdss_cti, NA, NA, NA, NA,
			NA, NA, NA, 0x85000, 4),
	[24] = PINGROUP(24, qup0_se5_l3, emac0_ptp, emac0_ptp, qdss_cti,
			emac0_mcg2, NA, NA, NA, NA, NA, 0x85000, 5),
	[25] = PINGROUP(25, qup0_se3_l0, phase_flag4, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x85000, 6),
	[26] = PINGROUP(26, qup0_se3_l1, phase_flag5, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[27] = PINGROUP(27, qup0_se3_l2, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			-1),
	[28] = PINGROUP(28, qup0_se3_l3, phase_flag19, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x85000, 7),
	[29] = PINGROUP(29, qup0_se4_l0, cci_i2c_sda1, cci_async_in3, emac0_ptp,
			tgu_ch2_trigout, NA, NA, NA, NA, NA, 0x85000, 8),
	[30] = PINGROUP(30, qup0_se4_l1, cci_i2c_scl1, cci_async_in4, emac0_ptp,
			tgu_ch3_trigout, NA, NA, NA, NA, NA, 0, -1),
	[31] = PINGROUP(31, qup0_se4_l2, cci_i2c_sda3, cci_async_in5, emac0_ptp,
			NA, NA, NA, NA, NA, NA, 0x85000, 9),
	[32] = PINGROUP(32, qup0_se4_l3, cci_i2c_scl3, emac0_ptp, mdp_vsync_s,
			NA, NA, NA, NA, NA, NA, 0x85000, 10),
	[33] = PINGROUP(33, qup0_se2_l0, qdss_gpio13, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x85000, 11),
	[34] = PINGROUP(34, qup0_se2_l1, qdss_gpio14, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[35] = PINGROUP(35, qup0_se2_l2, gcc_gp1, NA, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[36] = PINGROUP(36, qup0_se2_l3, gcc_gp2, qdss_gpio10, NA, NA, NA, NA,
			NA, NA, NA, 0x85000, 12),
	[37] = PINGROUP(37, qup1_se0_l0, ibi_i3c, qup1_se1_l2, aoss_cti, NA, NA,
			NA, NA, NA, NA, 0x85010, 4),
	[38] = PINGROUP(38, qup1_se0_l1, ibi_i3c, qup1_se1_l3, aoss_cti, NA, NA,
			NA, NA, NA, NA, 0x85010, 5),
	[39] = PINGROUP(39, qup1_se1_l0, ibi_i3c, qup1_se0_l2, aoss_cti, NA, NA,
			NA, NA, NA, NA, 0x85010, 6),
	[40] = PINGROUP(40, qup1_se1_l1, ibi_i3c, qup1_se0_l3, aoss_cti, NA, NA,
			NA, NA, NA, NA, 0x85010, 7),
	[41] = PINGROUP(41, qup1_se3_l2, qup1_se3_l0, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[42] = PINGROUP(42, qup1_se3_l3, qup1_se3_l1, mdp_vsync_e, NA, NA, NA,
			NA, NA, NA, NA, 0x85000, 13),
	[43] = PINGROUP(43, qup0_se7_l2, qup0_se7_l0, tgu_ch0_trigout, NA, NA,
			NA, NA, NA, NA, NA, 0, -1),
	[44] = PINGROUP(44, qup0_se7_l3, qup0_se7_l1, tgu_ch1_trigout, NA, NA,
			NA, NA, NA, NA, NA, 0x85000, 14),
	[45] = PINGROUP(45, qup1_se4_l0, hs1_mi2s, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x85000, 15),
	[46] = PINGROUP(46, qup1_se4_l1, hs1_mi2s, NA, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[47] = PINGROUP(47, qup1_se4_l2, hs1_mi2s, NA, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[48] = PINGROUP(48, qup1_se4_l3, hs1_mi2s, edp0_lcd, NA, NA, NA, NA, NA,
			NA, NA, 0x85004, 0),
	[49] = PINGROUP(49, qup1_se5_l0, hs2_mi2s, cci_timer3, qdss_cti,
			edp1_lcd, ddr_pxi1, NA, NA, NA, NA, 0, -1),
	[50] = PINGROUP(50, qup1_se5_l1, hs2_mi2s, cci_timer4, qdss_cti, NA,
			ddr_pxi1, NA, NA, NA, NA, 0, -1),
	[51] = PINGROUP(51, qup1_se5_l2, hs2_mi2s, qdss_cti, NA, NA, NA, NA, NA,
			NA, NA, 0x85004, 1),
	[52] = PINGROUP(52, qup1_se5_l3, hs2_mi2s, qdss_cti, mdp_vsync_p,
			ddr_pxi2, NA, NA, NA, NA, NA, 0x85004, 2),
	[53] = PINGROUP(53, ddr_bist_start, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x8500C, 3),
	[54] = PINGROUP(54, cci_i2c_sda5, phase_flag31, ddr_bist_complete, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[55] = PINGROUP(55, cci_i2c_scl5, phase_flag2, ddr_bist_stop, NA, NA,
			NA, NA, NA, NA, NA, 0, -1),
	[56] = PINGROUP(56, phase_flag1, ddr_bist_fail, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x85004, 3),
	[57] = PINGROUP(57, cci_i2c_sda0, prng_rosc0, qdss_gpio, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[58] = PINGROUP(58, cci_i2c_scl0, prng_rosc1, qdss_gpio, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[59] = PINGROUP(59, cci_i2c_sda2, prng_rosc2, qdss_gpio0, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[60] = PINGROUP(60, cci_i2c_scl2, prng_rosc3, qdss_gpio1, NA, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[61] = PINGROUP(61, cci_i2c_sda4, qdss_gpio11, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[62] = PINGROUP(62, cci_i2c_scl4, qdss_gpio12, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[63] = PINGROUP(63, cci_timer0, cci_async_in0, qdss_gpio5, atest_usb2,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[64] = PINGROUP(64, cci_timer1, cci_async_in1, qdss_gpio6, atest_usb23,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[65] = PINGROUP(65, cci_timer2, cci_async_in2, qdss_gpio7, atest_usb22,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[66] = PINGROUP(66, phase_flag0, NA, atest_char_start, NA, NA, NA, NA,
			NA, NA, NA, 0x85004, 4),
	[67] = PINGROUP(67, cam_mclk, vsense_trigger_mirnat, atest_usb21, NA,
			NA, NA, NA, NA, NA, NA, 0x85004, 5),
	[68] = PINGROUP(68, cam_mclk, gcc_gp4, atest_usb22, ddr_pxi0, NA, NA,
			NA, NA, NA, NA, 0x85004, 6),
	[69] = PINGROUP(69, cam_mclk, gcc_gp3, atest_usb23, ddr_pxi0, NA, NA,
			NA, NA, NA, NA, 0x85004, 7),
	[70] = PINGROUP(70, qdss_gpio4, atest_char0, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x85004, 8),
	[71] = PINGROUP(71, qdss_gpio3, atest_char1, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x85004, 9),
	[72] = PINGROUP(72, qdss_gpio2, atest_char2, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x85004, 10),
	[73] = PINGROUP(73, NA, qdss_gpio8, NA, NA, NA, NA, NA, NA, NA, NA,
			0x85004, 11),
	[74] = PINGROUP(74, pll_clk_aux, qdss_gpio9, atest_usb20, NA, NA, NA,
			NA, NA, NA, NA, 0x85004, 12),
	[75] = PINGROUP(75, NA, dbg_out_clk, qdss_gpio15, atest_usb21, NA, NA,
			NA, NA, NA, NA, 0x85004, 13),
	[76] = PINGROUP(76, gcc_gp5, tsense_pwm4, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x85004, 14),
	[77] = PINGROUP(77, gcc_gp5, tsense_pwm3, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x85010, 8),
	[78] = PINGROUP(78, tsense_pwm2, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x8500C, 4),
	[79] = PINGROUP(79, emac0_ptp, emac0_ptp, emac0_mcg3, NA, tsense_pwm1,
			NA, NA, NA, NA, NA, 0x8500C, 5),
	[80] = PINGROUP(80, qup0_se6_l0, mdp0_vsync6_out, NA, atest_usb22,
			ddr_pxi3, NA, NA, NA, NA, NA, 0x85004, 15),
	[81] = PINGROUP(81, qup0_se6_l1, mdp0_vsync7_out, gcc_gp2, NA,
			atest_usb23, ddr_pxi3, NA, NA, NA, NA, 0, -1),
	[82] = PINGROUP(82, qup0_se6_l2, gcc_gp3, NA, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[83] = PINGROUP(83, qup0_se6_l3, gcc_gp4, NA, atest_usb2, ddr_pxi2, NA,
			NA, NA, NA, NA, 0x85008, 0),
	[84] = PINGROUP(84, qup1_se2_l0, gcc_gp1, NA, atest_usb20, NA, NA, NA,
			NA, NA, NA, 0x85008, 1),
	[85] = PINGROUP(85, qup1_se2_l1, NA, atest_usb21, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[86] = PINGROUP(86, qup1_se2_l2, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			-1),
	[87] = PINGROUP(87, qup1_se2_l3, NA, atest_usb20, NA, NA, NA, NA, NA,
			NA, NA, 0x85008, 2),
	[88] = PINGROUP(88, qup1_se2_l4, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			-1),
	[89] = PINGROUP(89, qup1_se6_l0, qup1_se7_l2, mdp0_vsync0_out, NA, NA,
			NA, NA, NA, NA, NA, 0x85008, 3),
	[90] = PINGROUP(90, qup1_se6_l1, qup1_se7_l3, mdp0_vsync1_out,
			cri_trng0, NA, NA, NA, NA, NA, NA, 0x85008, 4),
	[91] = PINGROUP(91, qup1_se7_l0, qup1_se6_l2, mdp0_vsync3_out,
			cri_trng1, NA, NA, NA, NA, NA, NA, 0x85008, 5),
	[92] = PINGROUP(92, qup1_se7_l1, qup1_se6_l3, cri_trng, NA, atest_usb2,
			NA, NA, NA, NA, NA, 0x85008, 6),
	[93] = PINGROUP(93, atest_char3, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x85008, 7),
	[94] = PINGROUP(94, edp0_hot, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x85008, 8),
	[95] = PINGROUP(95, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x85010, 9),
	[96] = PINGROUP(96, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[97] = PINGROUP(97, mi2s_mclk0, jitter_bist, qdss_gpio, NA, NA, NA, NA,
			NA, NA, NA, 0x85010, 10),
	[98] = PINGROUP(98, mi2s1_sck, phase_flag30, NA, qdss_gpio8, NA, NA, NA,
			NA, NA, NA, 0x85010, 11),
	[99] = PINGROUP(99, mi2s1_ws, phase_flag29, NA, qdss_gpio9, NA, NA, NA,
			NA, NA, NA, 0x85010, 12),
	[100] = PINGROUP(100, mi2s1_data0, phase_flag28, NA, qdss_gpio10, NA,
			 NA, NA, NA, NA, NA, 0x85010, 13),
	[101] = PINGROUP(101, mi2s1_data1, phase_flag27, NA, qdss_gpio11, NA,
			 NA, NA, NA, NA, NA, 0x85010, 14),
	[102] = PINGROUP(102, mi2s2_sck, phase_flag26, NA, qdss_gpio12, NA, NA,
			 NA, NA, NA, NA, 0x85010, 15),
	[103] = PINGROUP(103, mi2s2_ws, phase_flag25, NA, qdss_gpio13, NA, NA,
			 NA, NA, NA, NA, 0x85014, 0),
	[104] = PINGROUP(104, mi2s2_data0, phase_flag24, NA, qdss_gpio14, NA,
			 NA, NA, NA, NA, NA, 0x85014, 1),
	[105] = PINGROUP(105, mi2s2_data1, audio_ref_clk, phase_flag23, NA,
			 qdss_gpio15, NA, NA, NA, NA, NA, 0x85014, 2),
	[106] = PINGROUP(106, hs0_mi2s_sck, phase_flag22, NA, qdss_gpio, NA, NA,
			 NA, NA, NA, NA, 0, -1),
	[107] = PINGROUP(107, hs0_mi2s_ws, pll_bist_sync, phase_flag21, NA,
			 qdss_gpio0, NA, NA, NA, NA, NA, 0, -1),
	[108] = PINGROUP(108, hs0_mi2s_data0, phase_flag20, NA, qdss_gpio1, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[109] = PINGROUP(109, hs0_mi2s_data1, mi2s_mclk1, qdss_gpio2, NA, NA,
			 NA, NA, NA, NA, NA, 0, -1),
	[110] = PINGROUP(110, phase_flag18, NA, qdss_gpio3, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[111] = PINGROUP(111, phase_flag17, NA, qdss_gpio4, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[112] = PINGROUP(112, phase_flag16, NA, qdss_gpio5, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[113] = PINGROUP(113, phase_flag15, NA, qdss_gpio6, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[114] = PINGROUP(114, phase_flag14, NA, qdss_gpio7, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[115] = PINGROUP(115, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[116] = PINGROUP(116, phase_flag12, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[117] = PINGROUP(117, phase_flag11, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[118] = PINGROUP(118, phase_flag10, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[119] = PINGROUP(119, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[120] = PINGROUP(120, phase_flag8, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[121] = PINGROUP(121, phase_flag7, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[122] = PINGROUP(122, phase_flag6, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[123] = PINGROUP(123, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[124] = PINGROUP(124, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[125] = PINGROUP(125, phase_flag3, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[126] = PINGROUP(126, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[127] = PINGROUP(127, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[128] = PINGROUP(128, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[129] = PINGROUP(129, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x85014,
			 3),
	[130] = PINGROUP(130, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x85014,
			 4),
	[131] = PINGROUP(131, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[132] = PINGROUP(132, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[133] = UFS_RESET(ufs_reset, 0x192000),
};

static struct pinctrl_qup monaco_auto_qup_regs[] = {
};

static const struct msm_gpio_wakeirq_map monaco_auto_pdc_map[] = {
	{ 0, 169 }, { 1, 174 }, { 2, 221 }, { 3, 176 }, { 4, 171 }, { 9, 198 },
	{ 10, 187 }, { 11, 188 }, { 13, 211 }, { 16, 203 }, { 17, 213 }, { 18, 209 },
	{ 19, 201 }, { 20, 230 }, { 21, 231 }, { 22, 175 }, { 23, 170 }, { 24, 232 },
	{ 28, 235 }, { 29, 216 }, { 31, 208 }, { 32, 200 }, { 36, 212 }, { 37, 177 },
	{ 38, 178 }, { 39, 184 }, { 40, 185 }, { 42, 186 }, { 44, 194 }, { 45, 173 },
	{ 48, 195 }, { 51, 215 }, { 52, 197 }, { 53, 192 }, { 56, 193 }, { 66, 238 },
	{ 67, 172 }, { 68, 182 }, { 69, 179 }, { 70, 181 }, { 71, 202 }, { 72, 183 },
	{ 73, 189 }, { 74, 196 }, { 75, 190 }, { 76, 191 }, { 77, 204 }, { 78, 206 },
	{ 79, 207 }, { 83, 214 }, { 84, 205 }, { 87, 237 }, { 89, 225 }, { 90, 217 },
	{ 91, 218 }, { 92, 226 }, { 93, 227 }, { 94, 228 }, { 95, 236 }, { 97, 199 },
	{ 98, 229 }, { 99, 180 }, { 100, 220 }, { 101, 239 }, { 102, 219 }, { 103, 233 },
	{ 104, 234 }, { 105, 223 }, { 129, 210 }, { 130, 222 },
};

static struct msm_dir_conn monaco_dir_conn[] = {
	{-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0},
	{-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}
};

static const struct msm_pinctrl_soc_data monaco_auto_pinctrl = {
	.pins = monaco_auto_pins,
	.npins = ARRAY_SIZE(monaco_auto_pins),
	.functions = monaco_auto_functions,
	.nfunctions = ARRAY_SIZE(monaco_auto_functions),
	.groups = monaco_auto_groups,
	.ngroups = ARRAY_SIZE(monaco_auto_groups),
	.ngpios = 134,
	.qup_regs = monaco_auto_qup_regs,
	.nqup_regs = ARRAY_SIZE(monaco_auto_qup_regs),
	.wakeirq_map = monaco_auto_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(monaco_auto_pdc_map),
	.dir_conn = monaco_dir_conn,
};

static int monaco_pinctrl_dirconn_list_probe(struct platform_device *pdev)
{
	int ret, n, dirconn_list_count, m;
	struct device_node *np = pdev->dev.of_node;

	n = of_property_count_elems_of_size(np, "qcom,dirconn-list", sizeof(u32));

	if (n <= 0 || n % 2)
		return -EINVAL;

	m = ARRAY_SIZE(monaco_dir_conn) - 1;
	dirconn_list_count = n / 2;

	for (n = 0; n < dirconn_list_count; n++) {
		ret = of_property_read_u32_index(np, "qcom,dirconn-list",
				n * 2 + 0, &monaco_dir_conn[m].gpio);
		if (ret)
			return ret;
		ret = of_property_read_u32_index(np, "qcom,dirconn-list",
				n * 2 + 1, &monaco_dir_conn[m].irq);
		if (ret)
			return ret;
		m--;
	}
	return 0;
}

static const struct of_device_id monaco_auto_pinctrl_of_match[] = {
	{ .compatible = "qcom,monaco_auto-pinctrl", .data = &monaco_auto_pinctrl},
	{},
};

static int monaco_auto_pinctrl_probe(struct platform_device *pdev)
{
	const struct msm_pinctrl_soc_data *pinctrl_data;
	struct device *dev = &pdev->dev;
	int len, ret;

	if (of_find_property(pdev->dev.of_node, "qcom,dirconn-list", &len)) {
		ret = monaco_pinctrl_dirconn_list_probe(pdev);
		if (ret) {
			dev_err(&pdev->dev,
				"Unable to parse Direct Connect List\n");
			return ret;
		}
	}

	pinctrl_data = of_device_get_match_data(dev);
	if (!pinctrl_data)
		return -EINVAL;

	return msm_pinctrl_probe(pdev, pinctrl_data);
}

static struct platform_driver monaco_auto_pinctrl_driver = {
	.driver = {
		.name = "monaco_auto-pinctrl",
		.of_match_table = monaco_auto_pinctrl_of_match,
	},
	.probe = monaco_auto_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init monaco_auto_pinctrl_init(void)
{
	return platform_driver_register(&monaco_auto_pinctrl_driver);
}
arch_initcall(monaco_auto_pinctrl_init);

static void __exit monaco_auto_pinctrl_exit(void)
{
	platform_driver_unregister(&monaco_auto_pinctrl_driver);
}
module_exit(monaco_auto_pinctrl_exit);

MODULE_DESCRIPTION("QTI monaco_auto pinctrl driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, monaco_auto_pinctrl_of_match);
