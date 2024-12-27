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
		.intr_target_bit = 5,		\
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


static const struct pinctrl_pin_desc seraph_pins[] = {
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
	PINCTRL_PIN(132, "SDC1_RCLK"),
	PINCTRL_PIN(133, "SDC1_CLK"),
	PINCTRL_PIN(134, "SDC1_CMD"),
	PINCTRL_PIN(135, "SDC1_DATA"),

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

static const unsigned int sdc1_rclk_pins[] = { 132 };
static const unsigned int sdc1_clk_pins[] = { 133 };
static const unsigned int sdc1_cmd_pins[] = { 134 };
static const unsigned int sdc1_data_pins[] = { 135 };

enum seraph_functions {
	msm_mux_gpio,
	msm_mux_RESOUT_GPIO_N,
	msm_mux_aoss_cti,
	msm_mux_aoss_ts,
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
	msm_mux_audio_ext_mclk0,
	msm_mux_audio_ext_mclk1,
	msm_mux_audio_ref_clk,
	msm_mux_cam_asc_mclk4,
	msm_mux_cam_mclk,
	msm_mux_cci0_async_in0,
	msm_mux_cci0_async_in1,
	msm_mux_cci0_async_in2,
	msm_mux_cci0_i2c_scl0,
	msm_mux_cci0_i2c_scl1,
	msm_mux_cci0_i2c_scl2,
	msm_mux_cci0_i2c_scl3,
	msm_mux_cci0_i2c_sda0,
	msm_mux_cci0_i2c_sda1,
	msm_mux_cci0_i2c_sda2,
	msm_mux_cci0_i2c_sda3,
	msm_mux_cci0_timer0,
	msm_mux_cci0_timer1,
	msm_mux_cci0_timer2,
	msm_mux_cci0_timer3_mira,
	msm_mux_cci0_timer3_mirb,
	msm_mux_cci0_timer4_mira,
	msm_mux_cci0_timer4_mirb,
	msm_mux_cci1_async_in0,
	msm_mux_cci1_async_in1,
	msm_mux_cci1_async_in2,
	msm_mux_cci1_i2c_scl4,
	msm_mux_cci1_i2c_scl5,
	msm_mux_cci1_i2c_scl6,
	msm_mux_cci1_i2c_scl7,
	msm_mux_cci1_i2c_sda4,
	msm_mux_cci1_i2c_sda5,
	msm_mux_cci1_i2c_sda6,
	msm_mux_cci1_i2c_sda7,
	msm_mux_cci1_timer0,
	msm_mux_cci1_timer1,
	msm_mux_cci1_timer2,
	msm_mux_cci1_timer3,
	msm_mux_cci1_timer4,
	msm_mux_dbg_out_clk,
	msm_mux_ddr_bist_complete,
	msm_mux_ddr_bist_fail,
	msm_mux_ddr_bist_start,
	msm_mux_ddr_bist_stop,
	msm_mux_ddr_pxi0,
	msm_mux_dp0_hot,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp10_clk,
	msm_mux_gcc_gp11_clk,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_gcc_gp4_clk,
	msm_mux_gcc_gp5_clk,
	msm_mux_gcc_gp6_clk,
	msm_mux_gcc_gp7_clk,
	msm_mux_gcc_gp8_clk,
	msm_mux_gcc_gp9_clk,
	msm_mux_host2wlan_sol_mira,
	msm_mux_host2wlan_sol_mirb,
	msm_mux_i2s0_data0,
	msm_mux_i2s0_data1,
	msm_mux_i2s0_sck,
	msm_mux_i2s0_ws,
	msm_mux_i3c_s_scl,
	msm_mux_i3c_s_sda,
	msm_mux_ibi_i3c,
	msm_mux_jitter_bist,
	msm_mux_mdp_vsync,
	msm_mux_mdp_vsync0_out,
	msm_mux_mdp_vsync1_out,
	msm_mux_mdp_vsync2_out,
	msm_mux_mdp_vsync3_out,
	msm_mux_mdp_vsync_e,
	msm_mux_pcie0_clk_req_n,
	msm_mux_pcie1_clk_req_n,
	msm_mux_pcie1_rst_n,
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
	msm_mux_pwm_2,
	msm_mux_pwm_3,
	msm_mux_pwm_4,
	msm_mux_pwm_5,
	msm_mux_pwm_6,
	msm_mux_pwm_7,
	msm_mux_pwm_8,
	msm_mux_pwm_9,
	msm_mux_qdss_cti,
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
	msm_mux_qspi0_clk,
	msm_mux_qspi0_cs0_n,
	msm_mux_qspi0_cs1_n,
	msm_mux_qspi0_data0,
	msm_mux_qspi0_data1,
	msm_mux_qspi0_data2,
	msm_mux_qspi0_data3,
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
	msm_mux_qup0_se2_l4,
	msm_mux_qup0_se3_l0,
	msm_mux_qup0_se3_l1,
	msm_mux_qup0_se3_l2,
	msm_mux_qup0_se3_l3,
	msm_mux_qup0_se3_l4,
	msm_mux_qup0_se4_l0,
	msm_mux_qup0_se4_l1,
	msm_mux_qup0_se4_l2,
	msm_mux_qup0_se4_l3,
	msm_mux_qup0_se4_l4_mira,
	msm_mux_qup0_se4_l4_mirb,
	msm_mux_qup0_se5_l0,
	msm_mux_qup0_se5_l1,
	msm_mux_qup0_se5_l2,
	msm_mux_qup0_se5_l3,
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
	msm_mux_qup1_se3_l0,
	msm_mux_qup1_se3_l1,
	msm_mux_qup1_se3_l2,
	msm_mux_qup1_se3_l3,
	msm_mux_qup1_se4_l0,
	msm_mux_qup1_se4_l1,
	msm_mux_qup1_se4_l2_mira,
	msm_mux_qup1_se4_l2_mirb,
	msm_mux_qup1_se4_l3_mira,
	msm_mux_qup1_se4_l3_mirb,
	msm_mux_qup1_se5_l0_mira,
	msm_mux_qup1_se5_l0_mirb,
	msm_mux_qup1_se5_l1_mira,
	msm_mux_qup1_se5_l1_mirb,
	msm_mux_qup1_se5_l2,
	msm_mux_qup1_se5_l3,
	msm_mux_sdc2_clk,
	msm_mux_sdc2_cmd,
	msm_mux_sdc2_fb_clk,
	msm_mux_tb_trig_sdc1,
	msm_mux_tb_trig_sdc2,
	msm_mux_tgu_ch0_trigout,
	msm_mux_tmess_prng0,
	msm_mux_tmess_prng1,
	msm_mux_tmess_prng2,
	msm_mux_tmess_prng3,
	msm_mux_tsense_mirnat_RESERVED,
	msm_mux_tsense_pwm1,
	msm_mux_usb0_hs,
	msm_mux_usb0_phy_ps,
	msm_mux_vsense_trigger_mirnat,
	msm_mux_wcn_sw,
	msm_mux_wcn_sw_ctrl,
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
};

static const char *const RESOUT_GPIO_N_groups[] = {
	"gpio63",
};

static const char *const aoss_cti_groups[] = {
	"gpio61", "gpio62", "gpio67", "gpio68",
};

static const char *const aoss_ts_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio28", "gpio29",
	"gpio30", "gpio69", "gpio70", "gpio71", "gpio72",
};

static const char *const atest_char0_groups[] = {
	"gpio57",
};

static const char *const atest_char1_groups[] = {
	"gpio58",
};

static const char *const atest_char2_groups[] = {
	"gpio59",
};

static const char *const atest_char3_groups[] = {
	"gpio60",
};

static const char *const atest_char_start_groups[] = {
	"gpio40",
};

static const char *const atest_usb0_groups[] = {
	"gpio94",
};

static const char *const atest_usb00_groups[] = {
	"gpio88",
};

static const char *const atest_usb01_groups[] = {
	"gpio93",
};

static const char *const atest_usb02_groups[] = {
	"gpio90",
};

static const char *const atest_usb03_groups[] = {
	"gpio91",
};

static const char *const audio_ext_mclk0_groups[] = {
	"gpio30",
};

static const char *const audio_ext_mclk1_groups[] = {
	"gpio70",
};

static const char *const audio_ref_clk_groups[] = {
	"gpio70",
};

static const char *const cam_asc_mclk4_groups[] = {
	"gpio49",
};

static const char *const cam_mclk_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48", "gpio50", "gpio125",
	"gpio126",
};

static const char *const cci0_async_in0_groups[] = {
	"gpio56",
};

static const char *const cci0_async_in1_groups[] = {
	"gpio60",
};

static const char *const cci0_async_in2_groups[] = {
	"gpio59",
};

static const char *const cci0_i2c_scl0_groups[] = {
	"gpio58",
};

static const char *const cci0_i2c_scl1_groups[] = {
	"gpio60",
};

static const char *const cci0_i2c_scl2_groups[] = {
	"gpio128",
};

static const char *const cci0_i2c_scl3_groups[] = {
	"gpio130",
};

static const char *const cci0_i2c_sda0_groups[] = {
	"gpio57",
};

static const char *const cci0_i2c_sda1_groups[] = {
	"gpio59",
};

static const char *const cci0_i2c_sda2_groups[] = {
	"gpio127",
};

static const char *const cci0_i2c_sda3_groups[] = {
	"gpio129",
};

static const char *const cci0_timer0_groups[] = {
	"gpio51",
};

static const char *const cci0_timer1_groups[] = {
	"gpio52",
};

static const char *const cci0_timer2_groups[] = {
	"gpio53",
};

static const char *const cci0_timer3_mira_groups[] = {
	"gpio54",
};

static const char *const cci0_timer3_mirb_groups[] = {
	"gpio42",
};

static const char *const cci0_timer4_mira_groups[] = {
	"gpio55",
};

static const char *const cci0_timer4_mirb_groups[] = {
	"gpio43",
};

static const char *const cci1_async_in0_groups[] = {
	"gpio65",
};

static const char *const cci1_async_in1_groups[] = {
	"gpio50",
};

static const char *const cci1_async_in2_groups[] = {
	"gpio51",
};

static const char *const cci1_i2c_scl4_groups[] = {
	"gpio62",
};

static const char *const cci1_i2c_scl5_groups[] = {
	"gpio113",
};

static const char *const cci1_i2c_scl6_groups[] = {
	"gpio53",
};

static const char *const cci1_i2c_scl7_groups[] = {
	"gpio55",
};

static const char *const cci1_i2c_sda4_groups[] = {
	"gpio61",
};

static const char *const cci1_i2c_sda5_groups[] = {
	"gpio112",
};

static const char *const cci1_i2c_sda6_groups[] = {
	"gpio52",
};

static const char *const cci1_i2c_sda7_groups[] = {
	"gpio54",
};

static const char *const cci1_timer0_groups[] = {
	"gpio56",
};

static const char *const cci1_timer1_groups[] = {
	"gpio65",
};

static const char *const cci1_timer2_groups[] = {
	"gpio41",
};

static const char *const cci1_timer3_groups[] = {
	"gpio123",
};

static const char *const cci1_timer4_groups[] = {
	"gpio44",
};

static const char *const dbg_out_clk_groups[] = {
	"gpio80",
};

static const char *const ddr_bist_complete_groups[] = {
	"gpio43",
};

static const char *const ddr_bist_fail_groups[] = {
	"gpio41",
};

static const char *const ddr_bist_start_groups[] = {
	"gpio42",
};

static const char *const ddr_bist_stop_groups[] = {
	"gpio44",
};

static const char *const ddr_pxi0_groups[] = {
	"gpio34", "gpio35",
};

static const char *const dp0_hot_groups[] = {
	"gpio45", "gpio63",
};

static const char *const gcc_gp1_groups[] = {
	"gpio61", "gpio98",
};

static const char *const gcc_gp10_clk_groups[] = {
	"gpio96",
};

static const char *const gcc_gp11_clk_groups[] = {
	"gpio97",
};

static const char *const gcc_gp2_groups[] = {
	"gpio62", "gpio99",
};

static const char *const gcc_gp3_groups[] = {
	"gpio39", "gpio100",
};

static const char *const gcc_gp4_clk_groups[] = {
	"gpio102",
};

static const char *const gcc_gp5_clk_groups[] = {
	"gpio103",
};

static const char *const gcc_gp6_clk_groups[] = {
	"gpio104",
};

static const char *const gcc_gp7_clk_groups[] = {
	"gpio106",
};

static const char *const gcc_gp8_clk_groups[] = {
	"gpio107",
};

static const char *const gcc_gp9_clk_groups[] = {
	"gpio95",
};

static const char *const host2wlan_sol_mira_groups[] = {
	"gpio73",
};

static const char *const host2wlan_sol_mirb_groups[] = {
	"gpio55",
};

static const char *const i2s0_data0_groups[] = {
	"gpio16",
};

static const char *const i2s0_data1_groups[] = {
	"gpio17",
};

static const char *const i2s0_sck_groups[] = {
	"gpio18",
};

static const char *const i2s0_ws_groups[] = {
	"gpio19",
};

static const char *const i3c_s_scl_groups[] = {
	"gpio5",
};

static const char *const i3c_s_sda_groups[] = {
	"gpio4",
};

static const char *const ibi_i3c_groups[] = {
	"gpio4", "gpio5", "gpio20", "gpio21", "gpio22", "gpio23",
	"gpio61", "gpio62",
};

static const char *const jitter_bist_groups[] = {
	"gpio64",
};

static const char *const mdp_vsync_groups[] = {
	"gpio12", "gpio13", "gpio34", "gpio35",
};

static const char *const mdp_vsync0_out_groups[] = {
	"gpio34",
};

static const char *const mdp_vsync1_out_groups[] = {
	"gpio34",
};

static const char *const mdp_vsync2_out_groups[] = {
	"gpio35",
};

static const char *const mdp_vsync3_out_groups[] = {
	"gpio35",
};

static const char *const mdp_vsync_e_groups[] = {
	"gpio30",
};

static const char *const pcie0_clk_req_n_groups[] = {
	"gpio38",
};

static const char *const pcie1_clk_req_n_groups[] = {
	"gpio40",
};

static const char *const pcie1_rst_n_groups[] = {
	"gpio7",
};

static const char *const phase_flag0_groups[] = {
	"gpio92",
};

static const char *const phase_flag1_groups[] = {
	"gpio89",
};

static const char *const phase_flag10_groups[] = {
	"gpio51",
};

static const char *const phase_flag11_groups[] = {
	"gpio56",
};

static const char *const phase_flag12_groups[] = {
	"gpio33",
};

static const char *const phase_flag13_groups[] = {
	"gpio34",
};

static const char *const phase_flag14_groups[] = {
	"gpio35",
};

static const char *const phase_flag15_groups[] = {
	"gpio24",
};

static const char *const phase_flag16_groups[] = {
	"gpio25",
};

static const char *const phase_flag17_groups[] = {
	"gpio26",
};

static const char *const phase_flag18_groups[] = {
	"gpio40",
};

static const char *const phase_flag19_groups[] = {
	"gpio43",
};

static const char *const phase_flag2_groups[] = {
	"gpio8",
};

static const char *const phase_flag20_groups[] = {
	"gpio44",
};

static const char *const phase_flag21_groups[] = {
	"gpio57",
};

static const char *const phase_flag22_groups[] = {
	"gpio58",
};

static const char *const phase_flag23_groups[] = {
	"gpio59",
};

static const char *const phase_flag24_groups[] = {
	"gpio60",
};

static const char *const phase_flag25_groups[] = {
	"gpio71",
};

static const char *const phase_flag26_groups[] = {
	"gpio72",
};

static const char *const phase_flag27_groups[] = {
	"gpio27",
};

static const char *const phase_flag28_groups[] = {
	"gpio52",
};

static const char *const phase_flag29_groups[] = {
	"gpio75",
};

static const char *const phase_flag3_groups[] = {
	"gpio9",
};

static const char *const phase_flag30_groups[] = {
	"gpio76",
};

static const char *const phase_flag31_groups[] = {
	"gpio77",
};

static const char *const phase_flag4_groups[] = {
	"gpio10",
};

static const char *const phase_flag5_groups[] = {
	"gpio11",
};

static const char *const phase_flag6_groups[] = {
	"gpio14",
};

static const char *const phase_flag7_groups[] = {
	"gpio15",
};

static const char *const phase_flag8_groups[] = {
	"gpio19",
};

static const char *const phase_flag9_groups[] = {
	"gpio105",
};

static const char *const pll_bist_sync_groups[] = {
	"gpio104",
};

static const char *const pll_clk_aux_groups[] = {
	"gpio4",
};

static const char *const prng_rosc0_groups[] = {
	"gpio85",
};

static const char *const prng_rosc1_groups[] = {
	"gpio73",
};

static const char *const prng_rosc2_groups[] = {
	"gpio65",
};

static const char *const prng_rosc3_groups[] = {
	"gpio66",
};

static const char *const pwm_0_groups[] = {
	"gpio2",
};

static const char *const pwm_1_groups[] = {
	"gpio3",
};

static const char *const pwm_2_groups[] = {
	"gpio12",
};

static const char *const pwm_3_groups[] = {
	"gpio13",
};

static const char *const pwm_4_groups[] = {
	"gpio16",
};

static const char *const pwm_5_groups[] = {
	"gpio17",
};

static const char *const pwm_6_groups[] = {
	"gpio28",
};

static const char *const pwm_7_groups[] = {
	"gpio29",
};

static const char *const pwm_8_groups[] = {
	"gpio18",
};

static const char *const pwm_9_groups[] = {
	"gpio19",
};

static const char *const qdss_cti_groups[] = {
	"gpio43", "gpio44", "gpio61", "gpio62", "gpio65", "gpio66",
	"gpio69", "gpio70", "gpio116", "gpio117",
};

static const char *const qdss_gpio_traceclk_groups[] = {
	"gpio73",
};

static const char *const qdss_gpio_tracectl_groups[] = {
	"gpio74",
};

static const char *const qdss_gpio_tracedata0_groups[] = {
	"gpio33",
};

static const char *const qdss_gpio_tracedata1_groups[] = {
	"gpio32",
};

static const char *const qdss_gpio_tracedata10_groups[] = {
	"gpio24",
};

static const char *const qdss_gpio_tracedata11_groups[] = {
	"gpio39",
};

static const char *const qdss_gpio_tracedata12_groups[] = {
	"gpio38",
};

static const char *const qdss_gpio_tracedata13_groups[] = {
	"gpio37",
};

static const char *const qdss_gpio_tracedata14_groups[] = {
	"gpio15",
};

static const char *const qdss_gpio_tracedata15_groups[] = {
	"gpio14",
};

static const char *const qdss_gpio_tracedata2_groups[] = {
	"gpio31",
};

static const char *const qdss_gpio_tracedata3_groups[] = {
	"gpio53",
};

static const char *const qdss_gpio_tracedata4_groups[] = {
	"gpio52",
};

static const char *const qdss_gpio_tracedata5_groups[] = {
	"gpio60",
};

static const char *const qdss_gpio_tracedata6_groups[] = {
	"gpio59",
};

static const char *const qdss_gpio_tracedata7_groups[] = {
	"gpio27",
};

static const char *const qdss_gpio_tracedata8_groups[] = {
	"gpio26",
};

static const char *const qdss_gpio_tracedata9_groups[] = {
	"gpio25",
};

static const char *const qspi0_clk_groups[] = {
	"gpio2",
};

static const char *const qspi0_cs0_n_groups[] = {
	"gpio3",
};

static const char *const qspi0_cs1_n_groups[] = {
	"gpio68",
};

static const char *const qspi0_data0_groups[] = {
	"gpio41",
};

static const char *const qspi0_data1_groups[] = {
	"gpio42",
};

static const char *const qspi0_data2_groups[] = {
	"gpio63",
};

static const char *const qspi0_data3_groups[] = {
	"gpio67",
};

static const char *const qup0_se0_l0_groups[] = {
	"gpio22",
};

static const char *const qup0_se0_l1_groups[] = {
	"gpio23",
};

static const char *const qup0_se0_l2_groups[] = {
	"gpio12",
};

static const char *const qup0_se0_l3_groups[] = {
	"gpio13",
};

static const char *const qup0_se1_l0_groups[] = {
	"gpio67",
};

static const char *const qup0_se1_l1_groups[] = {
	"gpio68",
};

static const char *const qup0_se1_l2_groups[] = {
	"gpio41",
};

static const char *const qup0_se1_l3_groups[] = {
	"gpio42",
};

static const char *const qup0_se2_l0_groups[] = {
	"gpio0",
};

static const char *const qup0_se2_l1_groups[] = {
	"gpio1",
};

static const char *const qup0_se2_l2_groups[] = {
	"gpio2",
};

static const char *const qup0_se2_l3_groups[] = {
	"gpio3",
};

static const char *const qup0_se2_l4_groups[] = {
	"gpio63",
};

static const char *const qup0_se3_l0_groups[] = {
	"gpio16",
};

static const char *const qup0_se3_l1_groups[] = {
	"gpio17",
};

static const char *const qup0_se3_l2_groups[] = {
	"gpio18",
};

static const char *const qup0_se3_l3_groups[] = {
	"gpio19",
};

static const char *const qup0_se3_l4_groups[] = {
	"gpio30",
};

static const char *const qup0_se4_l0_groups[] = {
	"gpio20",
};

static const char *const qup0_se4_l1_groups[] = {
	"gpio21",
};

static const char *const qup0_se4_l2_groups[] = {
	"gpio22",
};

static const char *const qup0_se4_l3_groups[] = {
	"gpio23",
};

static const char *const qup0_se4_l4_mira_groups[] = {
	"gpio64",
};

static const char *const qup0_se4_l4_mirb_groups[] = {
	"gpio12",
};

static const char *const qup0_se5_l0_groups[] = {
	"gpio65",
};

static const char *const qup0_se5_l1_groups[] = {
	"gpio66",
};

static const char *const qup0_se5_l2_groups[] = {
	"gpio67",
};

static const char *const qup0_se5_l3_groups[] = {
	"gpio68",
};

static const char *const qup1_se0_l0_groups[] = {
	"gpio61",
};

static const char *const qup1_se0_l1_groups[] = {
	"gpio62",
};

static const char *const qup1_se0_l2_groups[] = {
	"gpio43",
};

static const char *const qup1_se0_l3_groups[] = {
	"gpio44",
};

static const char *const qup1_se1_l0_groups[] = {
	"gpio24",
};

static const char *const qup1_se1_l1_groups[] = {
	"gpio25",
};

static const char *const qup1_se1_l2_groups[] = {
	"gpio26",
};

static const char *const qup1_se1_l3_groups[] = {
	"gpio27",
};

static const char *const qup1_se2_l0_groups[] = {
	"gpio8",
};

static const char *const qup1_se2_l1_groups[] = {
	"gpio9",
};

static const char *const qup1_se2_l2_groups[] = {
	"gpio10",
};

static const char *const qup1_se2_l3_groups[] = {
	"gpio11",
};

static const char *const qup1_se3_l0_groups[] = {
	"gpio71",
};

static const char *const qup1_se3_l1_groups[] = {
	"gpio72",
};

static const char *const qup1_se3_l2_groups[] = {
	"gpio28",
};

static const char *const qup1_se3_l3_groups[] = {
	"gpio29",
};

static const char *const qup1_se4_l0_groups[] = {
	"gpio4",
};

static const char *const qup1_se4_l1_groups[] = {
	"gpio5",
};

static const char *const qup1_se4_l2_mira_groups[] = {
	"gpio6",
};

static const char *const qup1_se4_l2_mirb_groups[] = {
	"gpio119",
};

static const char *const qup1_se4_l3_mira_groups[] = {
	"gpio7",
};

static const char *const qup1_se4_l3_mirb_groups[] = {
	"gpio120",
};

static const char *const qup1_se5_l0_mira_groups[] = {
	"gpio108",
};

static const char *const qup1_se5_l0_mirb_groups[] = {
	"gpio10",
};

static const char *const qup1_se5_l1_mira_groups[] = {
	"gpio109",
};

static const char *const qup1_se5_l1_mirb_groups[] = {
	"gpio11",
};

static const char *const qup1_se5_l2_groups[] = {
	"gpio14",
};

static const char *const qup1_se5_l3_groups[] = {
	"gpio15",
};

static const char *const sdc2_clk_groups[] = {
	"gpio73",
};

static const char *const sdc2_cmd_groups[] = {
	"gpio32",
};

static const char *const sdc2_fb_clk_groups[] = {
	"gpio74",
};

static const char *const tb_trig_sdc1_groups[] = {
	"gpio88",
};

static const char *const tb_trig_sdc2_groups[] = {
	"gpio90",
};

static const char *const tgu_ch0_trigout_groups[] = {
	"gpio51",
};

static const char *const tmess_prng0_groups[] = {
	"gpio85",
};

static const char *const tmess_prng1_groups[] = {
	"gpio62",
};

static const char *const tmess_prng2_groups[] = {
	"gpio65",
};

static const char *const tmess_prng3_groups[] = {
	"gpio66",
};

static const char *const tsense_mirnat_RESERVED_groups[] = {
	"gpio53",
};

static const char *const tsense_pwm1_groups[] = {
	"gpio74",
};

static const char *const usb0_hs_groups[] = {
	"gpio76",
};

static const char *const usb0_phy_ps_groups[] = {
	"gpio69",
};

static const char *const vsense_trigger_mirnat_groups[] = {
	"gpio80",
};

static const char *const wcn_sw_groups[] = {
	"gpio70",
};

static const char *const wcn_sw_ctrl_groups[] = {
	"gpio33",
};


static const struct msm_function seraph_functions[] = {
	FUNCTION(gpio),
	FUNCTION(RESOUT_GPIO_N),
	FUNCTION(aoss_cti),
	FUNCTION(aoss_ts),
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
	FUNCTION(audio_ext_mclk0),
	FUNCTION(audio_ext_mclk1),
	FUNCTION(audio_ref_clk),
	FUNCTION(cam_asc_mclk4),
	FUNCTION(cam_mclk),
	FUNCTION(cci0_async_in0),
	FUNCTION(cci0_async_in1),
	FUNCTION(cci0_async_in2),
	FUNCTION(cci0_i2c_scl0),
	FUNCTION(cci0_i2c_scl1),
	FUNCTION(cci0_i2c_scl2),
	FUNCTION(cci0_i2c_scl3),
	FUNCTION(cci0_i2c_sda0),
	FUNCTION(cci0_i2c_sda1),
	FUNCTION(cci0_i2c_sda2),
	FUNCTION(cci0_i2c_sda3),
	FUNCTION(cci0_timer0),
	FUNCTION(cci0_timer1),
	FUNCTION(cci0_timer2),
	FUNCTION(cci0_timer3_mira),
	FUNCTION(cci0_timer3_mirb),
	FUNCTION(cci0_timer4_mira),
	FUNCTION(cci0_timer4_mirb),
	FUNCTION(cci1_async_in0),
	FUNCTION(cci1_async_in1),
	FUNCTION(cci1_async_in2),
	FUNCTION(cci1_i2c_scl4),
	FUNCTION(cci1_i2c_scl5),
	FUNCTION(cci1_i2c_scl6),
	FUNCTION(cci1_i2c_scl7),
	FUNCTION(cci1_i2c_sda4),
	FUNCTION(cci1_i2c_sda5),
	FUNCTION(cci1_i2c_sda6),
	FUNCTION(cci1_i2c_sda7),
	FUNCTION(cci1_timer0),
	FUNCTION(cci1_timer1),
	FUNCTION(cci1_timer2),
	FUNCTION(cci1_timer3),
	FUNCTION(cci1_timer4),
	FUNCTION(dbg_out_clk),
	FUNCTION(ddr_bist_complete),
	FUNCTION(ddr_bist_fail),
	FUNCTION(ddr_bist_start),
	FUNCTION(ddr_bist_stop),
	FUNCTION(ddr_pxi0),
	FUNCTION(dp0_hot),
	FUNCTION(gcc_gp1),
	FUNCTION(gcc_gp10_clk),
	FUNCTION(gcc_gp11_clk),
	FUNCTION(gcc_gp2),
	FUNCTION(gcc_gp3),
	FUNCTION(gcc_gp4_clk),
	FUNCTION(gcc_gp5_clk),
	FUNCTION(gcc_gp6_clk),
	FUNCTION(gcc_gp7_clk),
	FUNCTION(gcc_gp8_clk),
	FUNCTION(gcc_gp9_clk),
	FUNCTION(host2wlan_sol_mira),
	FUNCTION(host2wlan_sol_mirb),
	FUNCTION(i2s0_data0),
	FUNCTION(i2s0_data1),
	FUNCTION(i2s0_sck),
	FUNCTION(i2s0_ws),
	FUNCTION(i3c_s_scl),
	FUNCTION(i3c_s_sda),
	FUNCTION(ibi_i3c),
	FUNCTION(jitter_bist),
	FUNCTION(mdp_vsync),
	FUNCTION(mdp_vsync0_out),
	FUNCTION(mdp_vsync1_out),
	FUNCTION(mdp_vsync2_out),
	FUNCTION(mdp_vsync3_out),
	FUNCTION(mdp_vsync_e),
	FUNCTION(pcie0_clk_req_n),
	FUNCTION(pcie1_clk_req_n),
	FUNCTION(pcie1_rst_n),
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
	FUNCTION(pwm_2),
	FUNCTION(pwm_3),
	FUNCTION(pwm_4),
	FUNCTION(pwm_5),
	FUNCTION(pwm_6),
	FUNCTION(pwm_7),
	FUNCTION(pwm_8),
	FUNCTION(pwm_9),
	FUNCTION(qdss_cti),
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
	FUNCTION(qspi0_clk),
	FUNCTION(qspi0_cs0_n),
	FUNCTION(qspi0_cs1_n),
	FUNCTION(qspi0_data0),
	FUNCTION(qspi0_data1),
	FUNCTION(qspi0_data2),
	FUNCTION(qspi0_data3),
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
	FUNCTION(qup0_se2_l4),
	FUNCTION(qup0_se3_l0),
	FUNCTION(qup0_se3_l1),
	FUNCTION(qup0_se3_l2),
	FUNCTION(qup0_se3_l3),
	FUNCTION(qup0_se3_l4),
	FUNCTION(qup0_se4_l0),
	FUNCTION(qup0_se4_l1),
	FUNCTION(qup0_se4_l2),
	FUNCTION(qup0_se4_l3),
	FUNCTION(qup0_se4_l4_mira),
	FUNCTION(qup0_se4_l4_mirb),
	FUNCTION(qup0_se5_l0),
	FUNCTION(qup0_se5_l1),
	FUNCTION(qup0_se5_l2),
	FUNCTION(qup0_se5_l3),
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
	FUNCTION(qup1_se3_l0),
	FUNCTION(qup1_se3_l1),
	FUNCTION(qup1_se3_l2),
	FUNCTION(qup1_se3_l3),
	FUNCTION(qup1_se4_l0),
	FUNCTION(qup1_se4_l1),
	FUNCTION(qup1_se4_l2_mira),
	FUNCTION(qup1_se4_l2_mirb),
	FUNCTION(qup1_se4_l3_mira),
	FUNCTION(qup1_se4_l3_mirb),
	FUNCTION(qup1_se5_l0_mira),
	FUNCTION(qup1_se5_l0_mirb),
	FUNCTION(qup1_se5_l1_mira),
	FUNCTION(qup1_se5_l1_mirb),
	FUNCTION(qup1_se5_l2),
	FUNCTION(qup1_se5_l3),
	FUNCTION(sdc2_clk),
	FUNCTION(sdc2_cmd),
	FUNCTION(sdc2_fb_clk),
	FUNCTION(tb_trig_sdc1),
	FUNCTION(tb_trig_sdc2),
	FUNCTION(tgu_ch0_trigout),
	FUNCTION(tmess_prng0),
	FUNCTION(tmess_prng1),
	FUNCTION(tmess_prng2),
	FUNCTION(tmess_prng3),
	FUNCTION(tsense_mirnat_RESERVED),
	FUNCTION(tsense_pwm1),
	FUNCTION(usb0_hs),
	FUNCTION(usb0_phy_ps),
	FUNCTION(vsense_trigger_mirnat),
	FUNCTION(wcn_sw),
	FUNCTION(wcn_sw_ctrl),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup seraph_groups[] = {
	[0] = PINGROUP(0, qup0_se2_l0, aoss_ts, NA, NA, NA, NA, NA, NA, NA, NA,
		       NA, 0x84010, 0),
	[1] = PINGROUP(1, qup0_se2_l1, aoss_ts, NA, NA, NA, NA, NA, NA, NA, NA,
		       NA, 0x84010, 1),
	[2] = PINGROUP(2, qup0_se2_l2, qspi0_clk, aoss_ts, pwm_0, NA, NA, NA,
		       NA, NA, NA, NA, 0x84010, 2),
	[3] = PINGROUP(3, qup0_se2_l3, qspi0_cs0_n, aoss_ts, pwm_1, NA, NA, NA,
		       NA, NA, NA, NA, 0x84010, 3),
	[4] = PINGROUP(4, qup1_se4_l0, ibi_i3c, i3c_s_sda, pll_clk_aux, NA, NA,
		       NA, NA, NA, NA, NA, 0x84000, 11),
	[5] = PINGROUP(5, qup1_se4_l1, ibi_i3c, i3c_s_scl, NA, NA, NA, NA, NA,
		       NA, NA, NA, 0x84000, 12),
	[6] = PINGROUP(6, qup1_se4_l2_mira, NA, NA, NA, NA, NA, NA, NA, NA, NA,
		       NA, 0x84000, 13),
	[7] = PINGROUP(7, qup1_se4_l3_mira, pcie1_rst_n, NA, NA, NA, NA, NA, NA,
		       NA, NA, NA, 0x84000, 14),
	[8] = PINGROUP(8, qup1_se2_l0, phase_flag2, NA, NA, NA, NA, NA, NA, NA,
		       NA, NA, 0, -1),
	[9] = PINGROUP(9, qup1_se2_l1, phase_flag3, NA, NA, NA, NA, NA, NA, NA,
		       NA, NA, 0, -1),
	[10] = PINGROUP(10, qup1_se2_l2, qup1_se5_l0_mirb, phase_flag4, NA, NA,
			NA, NA, NA, NA, NA, NA, 0, -1),
	[11] = PINGROUP(11, qup1_se2_l3, qup1_se5_l1_mirb, phase_flag5, NA, NA,
			NA, NA, NA, NA, NA, NA, 0x84000, 15),
	[12] = PINGROUP(12, qup0_se0_l2, qup0_se4_l4_mirb, mdp_vsync, pwm_2, NA,
			NA, NA, NA, NA, NA, NA, 0x84010, 4),
	[13] = PINGROUP(13, qup0_se0_l3, mdp_vsync, pwm_3, NA, NA, NA, NA, NA,
			NA, NA, NA, 0x84010, 5),
	[14] = PINGROUP(14, qup1_se5_l2, phase_flag6, NA, qdss_gpio_tracedata15,
			NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[15] = PINGROUP(15, qup1_se5_l3, phase_flag7, NA, qdss_gpio_tracedata14,
			NA, NA, NA, NA, NA, NA, NA, 0x84004, 0),
	[16] = PINGROUP(16, qup0_se3_l0, i2s0_data0, pwm_4, NA, NA, NA, NA, NA,
			NA, NA, NA, 0x84010, 6),
	[17] = PINGROUP(17, qup0_se3_l1, i2s0_data1, pwm_5, NA, NA, NA, NA, NA,
			NA, NA, NA, 0x84010, 7),
	[18] = PINGROUP(18, qup0_se3_l2, i2s0_sck, pwm_8, NA, NA, NA, NA, NA,
			NA, NA, NA, 0x84010, 8),
	[19] = PINGROUP(19, qup0_se3_l3, i2s0_ws, pwm_9, phase_flag8, NA, NA,
			NA, NA, NA, NA, NA, 0x84010, 9),
	[20] = PINGROUP(20, qup0_se4_l0, ibi_i3c, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x84010, 10),
	[21] = PINGROUP(21, qup0_se4_l1, ibi_i3c, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x84010, 11),
	[22] = PINGROUP(22, qup0_se4_l2, qup0_se0_l0, ibi_i3c, NA, NA, NA, NA,
			NA, NA, NA, NA, 0x84010, 12),
	[23] = PINGROUP(23, qup0_se4_l3, qup0_se0_l1, ibi_i3c, NA, NA, NA, NA,
			NA, NA, NA, NA, 0x84010, 13),
	[24] = PINGROUP(24, qup1_se1_l0, phase_flag15, NA,
			qdss_gpio_tracedata10, NA, NA, NA, NA, NA, NA, NA,
			0x84004, 1),
	[25] = PINGROUP(25, qup1_se1_l1, phase_flag16, NA, qdss_gpio_tracedata9,
			NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[26] = PINGROUP(26, qup1_se1_l2, phase_flag17, NA, qdss_gpio_tracedata8,
			NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[27] = PINGROUP(27, qup1_se1_l3, phase_flag27, NA, qdss_gpio_tracedata7,
			NA, NA, NA, NA, NA, NA, NA, 0x84004, 2),
	[28] = PINGROUP(28, qup1_se3_l2, aoss_ts, pwm_6, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x84004, 3),
	[29] = PINGROUP(29, qup1_se3_l3, aoss_ts, pwm_7, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x84004, 4),
	[30] = PINGROUP(30, qup0_se3_l4, mdp_vsync_e, aoss_ts, audio_ext_mclk0,
			aoss_ts, NA, NA, NA, NA, NA, NA, 0x84010, 14),
	[31] = PINGROUP(31, NA, qdss_gpio_tracedata2, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0x84004, 5),
	[32] = PINGROUP(32, sdc2_cmd, qdss_gpio_tracedata1, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0x84004, 6),
	[33] = PINGROUP(33, wcn_sw_ctrl, phase_flag12, NA, qdss_gpio_tracedata0,
			NA, NA, NA, NA, NA, NA, NA, 0x84004, 7),
	[34] = PINGROUP(34, mdp_vsync, mdp_vsync0_out, mdp_vsync1_out,
			phase_flag13, NA, ddr_pxi0, NA, NA, NA, NA, NA, 0x84010,
			15),
	[35] = PINGROUP(35, mdp_vsync, mdp_vsync2_out, mdp_vsync3_out,
			phase_flag14, NA, ddr_pxi0, NA, NA, NA, NA, NA, 0x84014,
			0),
	[36] = PINGROUP(36, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[37] = PINGROUP(37, NA, qdss_gpio_tracedata13, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0x84004, 8),
	[38] = PINGROUP(38, pcie0_clk_req_n, NA, qdss_gpio_tracedata12, NA, NA,
			NA, NA, NA, NA, NA, NA, 0x84004, 9),
	[39] = PINGROUP(39, NA, gcc_gp3, qdss_gpio_tracedata11, NA, NA, NA, NA,
			NA, NA, NA, NA, 0x84004, 10),
	[40] = PINGROUP(40, pcie1_clk_req_n, NA, phase_flag18, NA,
			atest_char_start, NA, NA, NA, NA, NA, NA, 0x84004, 11),
	[41] = PINGROUP(41, qup0_se1_l2, qspi0_data0, cci1_timer2,
			ddr_bist_fail, NA, NA, NA, NA, NA, NA, NA, 0x84014, 1),
	[42] = PINGROUP(42, qup0_se1_l3, qspi0_data1, cci0_timer3_mirb,
			ddr_bist_start, NA, NA, NA, NA, NA, NA, NA, 0x84014, 2),
	[43] = PINGROUP(43, qup1_se0_l2, cci0_timer4_mirb, ddr_bist_complete,
			phase_flag19, NA, qdss_cti, NA, NA, NA, NA, NA, 0x84004,
			12),
	[44] = PINGROUP(44, qup1_se0_l3, cci1_timer4, ddr_bist_stop,
			phase_flag20, NA, qdss_cti, NA, NA, NA, NA, NA, 0x84004,
			13),
	[45] = PINGROUP(45, cam_mclk, dp0_hot, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x84004, 14),
	[46] = PINGROUP(46, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			-1),
	[47] = PINGROUP(47, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x84004, 15),
	[48] = PINGROUP(48, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x84008, 0),
	[49] = PINGROUP(49, cam_asc_mclk4, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[50] = PINGROUP(50, cam_mclk, cci1_async_in1, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0x84008, 1),
	[51] = PINGROUP(51, cci0_timer0, cci1_async_in2, phase_flag10, NA,
			tgu_ch0_trigout, NA, NA, NA, NA, NA, NA, 0x84008, 2),
	[52] = PINGROUP(52, cci0_timer1, cci1_i2c_sda6, phase_flag28, NA,
			qdss_gpio_tracedata4, NA, NA, NA, NA, NA, NA, 0, -1),
	[53] = PINGROUP(53, cci0_timer2, cci1_i2c_scl6, NA,
			qdss_gpio_tracedata3, tsense_mirnat_RESERVED, NA, NA,
			NA, NA, NA, NA, 0, -1),
	[54] = PINGROUP(54, cci0_timer3_mira, cci1_i2c_sda7, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0x84008, 3),
	[55] = PINGROUP(55, cci0_timer4_mira, cci1_i2c_scl7, host2wlan_sol_mirb,
			NA, NA, NA, NA, NA, NA, NA, NA, 0x84008, 4),
	[56] = PINGROUP(56, cci1_timer0, cci0_async_in0, phase_flag11, NA, NA,
			NA, NA, NA, NA, NA, NA, 0x84008, 5),
	[57] = PINGROUP(57, cci0_i2c_sda0, phase_flag21, NA, atest_char0, NA,
			NA, NA, NA, NA, NA, NA, 0x84008, 6),
	[58] = PINGROUP(58, cci0_i2c_scl0, phase_flag22, NA, atest_char1, NA,
			NA, NA, NA, NA, NA, NA, 0x84008, 7),
	[59] = PINGROUP(59, cci0_i2c_sda1, cci0_async_in2, phase_flag23, NA,
			qdss_gpio_tracedata6, atest_char2, NA, NA, NA, NA, NA,
			0x84008, 8),
	[60] = PINGROUP(60, cci0_i2c_scl1, cci0_async_in1, phase_flag24, NA,
			qdss_gpio_tracedata5, atest_char3, NA, NA, NA, NA, NA,
			0x84008, 9),
	[61] = PINGROUP(61, cci1_i2c_sda4, qup1_se0_l0, ibi_i3c, gcc_gp1,
			aoss_cti, NA, qdss_cti, NA, NA, NA, NA, 0x84008, 10),
	[62] = PINGROUP(62, cci1_i2c_scl4, qup1_se0_l1, ibi_i3c, tmess_prng1,
			gcc_gp2, aoss_cti, NA, qdss_cti, NA, NA, NA, 0, -1),
	[63] = PINGROUP(63, qup0_se2_l4, qspi0_data2, RESOUT_GPIO_N, dp0_hot,
			NA, NA, NA, NA, NA, NA, NA, 0x84014, 3),
	[64] = PINGROUP(64, qup0_se4_l4_mira, jitter_bist, NA, NA, NA, NA, NA,
			NA, NA, NA, NA, 0x84014, 4),
	[65] = PINGROUP(65, qup0_se5_l0, cci1_async_in0, prng_rosc2,
			tmess_prng2, cci1_timer1, NA, qdss_cti, NA, NA, NA, NA,
			0x84014, 5),
	[66] = PINGROUP(66, qup0_se5_l1, prng_rosc3, tmess_prng3, NA, qdss_cti,
			NA, NA, NA, NA, NA, NA, 0x84014, 6),
	[67] = PINGROUP(67, qup0_se5_l2, qup0_se1_l0, qspi0_data3, NA, aoss_cti,
			NA, NA, NA, NA, NA, NA, 0x84014, 7),
	[68] = PINGROUP(68, qup0_se5_l3, qup0_se1_l1, qspi0_cs1_n, aoss_cti, NA,
			NA, NA, NA, NA, NA, NA, 0x84014, 8),
	[69] = PINGROUP(69, usb0_phy_ps, aoss_ts, qdss_cti, NA, NA, NA, NA, NA,
			NA, NA, NA, 0x84014, 9),
	[70] = PINGROUP(70, audio_ext_mclk1, audio_ref_clk, aoss_ts, wcn_sw,
			qdss_cti, NA, NA, NA, NA, NA, NA, 0x84014, 10),
	[71] = PINGROUP(71, qup1_se3_l0, aoss_ts, phase_flag25, NA, NA, NA, NA,
			NA, NA, NA, NA, 0x84008, 11),
	[72] = PINGROUP(72, qup1_se3_l1, aoss_ts, aoss_ts, phase_flag26, NA, NA,
			NA, NA, NA, NA, NA, 0x84008, 12),
	[73] = PINGROUP(73, host2wlan_sol_mira, sdc2_clk, prng_rosc1,
			qdss_gpio_traceclk, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[74] = PINGROUP(74, sdc2_fb_clk, qdss_gpio_tracectl, tsense_pwm1, NA,
			NA, NA, NA, NA, NA, NA, NA, 0x84008, 13),
	[75] = PINGROUP(75, phase_flag29, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x84014, 11),
	[76] = PINGROUP(76, usb0_hs, phase_flag30, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0x84014, 12),
	[77] = PINGROUP(77, phase_flag31, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x84014, 13),
	[78] = PINGROUP(78, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x84014,
			14),
	[79] = PINGROUP(79, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x84014,
			15),
	[80] = PINGROUP(80, dbg_out_clk, vsense_trigger_mirnat, NA, NA, NA, NA,
			NA, NA, NA, NA, NA, 0, -1),
	[81] = PINGROUP(81, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x84018,
			0),
	[82] = PINGROUP(82, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x84018,
			1),
	[83] = PINGROUP(83, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[84] = PINGROUP(84, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x84018,
			2),
	[85] = PINGROUP(85, prng_rosc0, tmess_prng0, NA, NA, NA, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[86] = PINGROUP(86, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x84018,
			3),
	[87] = PINGROUP(87, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x84018,
			4),
	[88] = PINGROUP(88, tb_trig_sdc1, NA, atest_usb00, NA, NA, NA, NA, NA,
			NA, NA, NA, 0x84018, 5),
	[89] = PINGROUP(89, phase_flag1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[90] = PINGROUP(90, tb_trig_sdc2, atest_usb02, NA, NA, NA, NA, NA, NA,
			NA, NA, NA, 0, -1),
	[91] = PINGROUP(91, atest_usb03, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x84018, 6),
	[92] = PINGROUP(92, phase_flag0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x84018, 7),
	[93] = PINGROUP(93, NA, atest_usb01, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[94] = PINGROUP(94, atest_usb0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[95] = PINGROUP(95, gcc_gp9_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x84018, 8),
	[96] = PINGROUP(96, gcc_gp10_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x84018, 9),
	[97] = PINGROUP(97, gcc_gp11_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			NA, 0x84018, 10),
	[98] = PINGROUP(98, gcc_gp1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			0x84018, 11),
	[99] = PINGROUP(99, gcc_gp2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			-1),
	[100] = PINGROUP(100, gcc_gp3, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x84018, 12),
	[101] = PINGROUP(101, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			 -1),
	[102] = PINGROUP(102, gcc_gp4_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0x84018, 13),
	[103] = PINGROUP(103, gcc_gp5_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0x84018, 14),
	[104] = PINGROUP(104, pll_bist_sync, gcc_gp6_clk, NA, NA, NA, NA, NA,
			 NA, NA, NA, NA, 0x84018, 15),
	[105] = PINGROUP(105, phase_flag9, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[106] = PINGROUP(106, gcc_gp7_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0x8401C, 0),
	[107] = PINGROUP(107, gcc_gp8_clk, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0x8401C, 1),
	[108] = PINGROUP(108, qup1_se5_l0_mira, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[109] = PINGROUP(109, qup1_se5_l1_mira, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[110] = PINGROUP(110, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			 -1),
	[111] = PINGROUP(111, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			 -1),
	[112] = PINGROUP(112, cci1_i2c_sda5, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0x84008, 14),
	[113] = PINGROUP(113, cci1_i2c_scl5, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[114] = PINGROUP(114, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			 -1),
	[115] = PINGROUP(115, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			 -1),
	[116] = PINGROUP(116, qdss_cti, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[117] = PINGROUP(117, qdss_cti, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[118] = PINGROUP(118, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x8401C, 2),
	[119] = PINGROUP(119, qup1_se4_l2_mirb, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0x84008, 15),
	[120] = PINGROUP(120, qup1_se4_l3_mirb, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, NA, 0x8400C, 0),
	[121] = PINGROUP(121, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			 -1),
	[122] = PINGROUP(122, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			 -1),
	[123] = PINGROUP(123, cci1_timer3, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[124] = PINGROUP(124, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0,
			 -1),
	[125] = PINGROUP(125, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x8400C, 1),
	[126] = PINGROUP(126, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[127] = PINGROUP(127, cci0_i2c_sda2, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[128] = PINGROUP(128, cci0_i2c_scl2, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[129] = PINGROUP(129, cci0_i2c_sda3, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0x8400C, 2),
	[130] = PINGROUP(130, cci0_i2c_scl3, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 NA, 0, -1),
	[131] = PINGROUP(131, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x8401C, 3),
	[132] = SDC_QDSD_PINGROUP(sdc1_rclk, 0x189000, 0, 0),
	[133] = SDC_QDSD_PINGROUP(sdc1_clk, 0x188000, 13, 6),
	[134] = SDC_QDSD_PINGROUP(sdc1_cmd, 0x188000, 11, 3),
	[135] = SDC_QDSD_PINGROUP(sdc1_data, 0x188000, 9, 0),

};

static struct pinctrl_qup seraph_qup_regs[] = {
};

static const struct msm_gpio_wakeirq_map seraph_pdc_map[] = {
	{ 0, 70 }, { 1, 147 }, { 2, 154 }, { 3, 94 }, { 4, 101 }, { 5, 86 },
	{ 6, 159 }, { 7, 124 }, { 11, 75 }, { 12, 151 }, { 13, 80 }, { 15, 84 },
	{ 16, 152 }, { 17, 153 }, { 18, 142 }, { 19, 88 }, { 20, 89 }, { 21, 143 },
	{ 22, 71 }, { 23, 90 }, { 24, 144 }, { 27, 91 }, { 28, 92 }, { 29, 93 },
	{ 30, 95 }, { 31, 96 }, { 32, 97 }, { 33, 155 }, { 34, 156 }, { 35, 98 },
	{ 37, 157 }, { 38, 81 }, { 39, 100 }, { 40, 82 }, { 41, 158 }, { 42, 83 },
	{ 43, 102 }, { 44, 85 }, { 45, 103 }, { 47, 72 }, { 48, 146 }, { 50, 161 },
	{ 51, 107 }, { 54, 108 }, { 55, 109 }, { 56, 110 }, { 57, 111 }, { 58, 112 },
	{ 59, 113 }, { 60, 114 }, { 61, 115 }, { 63, 117 }, { 64, 118 }, { 65, 119 },
	{ 66, 120 }, { 67, 121 }, { 68, 122 }, { 69, 123 }, { 70, 87 }, { 71, 104 },
	{ 72, 105 }, { 74, 106 }, { 75, 125 }, { 76, 126 }, { 77, 162 }, { 78, 163 },
	{ 79, 128 }, { 81, 130 }, { 82, 127 }, { 84, 131 }, { 86, 164 }, { 87, 134 },
	{ 88, 165 }, { 91, 132 }, { 92, 133 }, { 95, 136 }, { 96, 149 }, { 97, 99 },
	{ 98, 139 }, { 100, 148 }, { 102, 116 }, { 103, 73 }, { 104, 129 }, { 106, 74 },
	{ 107, 145 }, { 112, 76 }, { 118, 135 }, { 119, 150 }, { 120, 77 }, { 125, 78 },
	{ 129, 79 }, { 131, 140 },
};

static const struct msm_pinctrl_soc_data seraph_pinctrl = {
	.pins = seraph_pins,
	.npins = ARRAY_SIZE(seraph_pins),
	.functions = seraph_functions,
	.nfunctions = ARRAY_SIZE(seraph_functions),
	.groups = seraph_groups,
	.ngroups = ARRAY_SIZE(seraph_groups),
	.ngpios = 132,
	.qup_regs = seraph_qup_regs,
	.nqup_regs = ARRAY_SIZE(seraph_qup_regs),
	.wakeirq_map = seraph_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(seraph_pdc_map),
	.egpio_func = 11,
};

static const struct of_device_id seraph_pinctrl_of_match[] = {
	{ .compatible = "qcom,seraph-pinctrl", .data = &seraph_pinctrl},
	{},
};

static int seraph_pinctrl_probe(struct platform_device *pdev)
{
	const struct msm_pinctrl_soc_data *pinctrl_data;
	struct device *dev = &pdev->dev;

	pinctrl_data = of_device_get_match_data(dev);
	if (!pinctrl_data)
		return -EINVAL;

	return msm_pinctrl_probe(pdev, pinctrl_data);
}

static struct platform_driver seraph_pinctrl_driver = {
	.driver = {
		.name = "seraph-pinctrl",
		.of_match_table = seraph_pinctrl_of_match,
	},
	.probe = seraph_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init seraph_pinctrl_init(void)
{
	return platform_driver_register(&seraph_pinctrl_driver);
}
arch_initcall(seraph_pinctrl_init);

static void __exit seraph_pinctrl_exit(void)
{
	platform_driver_unregister(&seraph_pinctrl_driver);
}
module_exit(seraph_pinctrl_exit);

MODULE_DESCRIPTION("QTI seraph pinctrl driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, seraph_pinctrl_of_match);
