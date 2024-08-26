/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

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
		.intr_target_kpss_val = 4,	\
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

static const struct pinctrl_pin_desc neo_pins[] = {
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
	PINCTRL_PIN(156, "SDC1_RCLK"),
	PINCTRL_PIN(157, "SDC1_CLK"),
	PINCTRL_PIN(158, "SDC1_CMD"),
	PINCTRL_PIN(159, "SDC1_DATA"),
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

static const unsigned int sdc1_rclk_pins[] = { 156 };
static const unsigned int sdc1_clk_pins[] = { 157 };
static const unsigned int sdc1_cmd_pins[] = { 158 };
static const unsigned int sdc1_data_pins[] = { 159 };

enum neo_functions {
	msm_mux_gpio,
	msm_mux_PCIE0_CLK_REQ_N,
	msm_mux_PCIE1_CLK_REQ_N,
	msm_mux_aoss_cti,
	msm_mux_atest_char,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_atest_usb0,
	msm_mux_atest_usb00,
	msm_mux_atest_usb01,
	msm_mux_atest_usb02,
	msm_mux_atest_usb03,
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
	msm_mux_dp0_hot,
	msm_mux_ext_mclk0,
	msm_mux_ext_mclk1,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_host2wlan_sol,
	msm_mux_i2s0_data0,
	msm_mux_i2s0_data1,
	msm_mux_i2s0_sck,
	msm_mux_i2s0_ws,
	msm_mux_ibi_i3c,
	msm_mux_jitter_bist,
	msm_mux_mdp_vsync,
	msm_mux_mdp_vsync0,
	msm_mux_mdp_vsync1,
	msm_mux_mdp_vsync2,
	msm_mux_mdp_vsync3,
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
	msm_mux_pll_bist,
	msm_mux_pll_clk,
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
	msm_mux_qspi00,
	msm_mux_qspi01,
	msm_mux_qspi02,
	msm_mux_qspi03,
	msm_mux_qspi0_clk,
	msm_mux_qspi0_cs0,
	msm_mux_qspi0_cs1,
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
	msm_mux_tb_trig,
	msm_mux_tgu_ch0,
	msm_mux_tgu_ch1,
	msm_mux_tgu_ch2,
	msm_mux_tgu_ch3,
	msm_mux_tmess_prng0,
	msm_mux_tmess_prng1,
	msm_mux_tmess_prng2,
	msm_mux_tmess_prng3,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_usb0_phy,
	msm_mux_vsense_trigger,
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
	"gpio153", "gpio154", "gpio155",
};
static const char * const PCIE0_CLK_REQ_N_groups[] = {
	"gpio56",
};
static const char * const PCIE1_CLK_REQ_N_groups[] = {
	"gpio59",
};
static const char * const aoss_cti_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23",
};
static const char * const atest_char_groups[] = {
	"gpio45",
};
static const char * const atest_char0_groups[] = {
	"gpio90",
};
static const char * const atest_char1_groups[] = {
	"gpio89",
};
static const char * const atest_char2_groups[] = {
	"gpio88",
};
static const char * const atest_char3_groups[] = {
	"gpio87",
};
static const char * const atest_usb0_groups[] = {
	"gpio26",
};
static const char * const atest_usb00_groups[] = {
	"gpio110",
};
static const char * const atest_usb01_groups[] = {
	"gpio109",
};
static const char * const atest_usb02_groups[] = {
	"gpio27",
};
static const char * const atest_usb03_groups[] = {
	"gpio60",
};
static const char * const audio_ref_groups[] = {
	"gpio103",
};
static const char * const cam_mclk_groups[] = {
	"gpio69", "gpio70", "gpio71", "gpio72", "gpio73", "gpio74", "gpio75",
	"gpio76",
};
static const char * const cci_async_groups[] = {
	"gpio80", "gpio81", "gpio82",
};
static const char * const cci_i2c_groups[] = {
	"gpio67", "gpio68", "gpio78", "gpio79", "gpio80", "gpio81", "gpio83",
	"gpio84", "gpio85", "gpio86", "gpio87", "gpio88", "gpio89", "gpio90",
	"gpio91", "gpio92",
};
static const char * const cci_timer0_groups[] = {
	"gpio77",
};
static const char * const cci_timer1_groups[] = {
	"gpio78",
};
static const char * const cci_timer2_groups[] = {
	"gpio79",
};
static const char * const cci_timer3_groups[] = {
	"gpio80",
};
static const char * const cci_timer4_groups[] = {
	"gpio81",
};
static const char * const cri_trng_groups[] = {
	"gpio60",
};
static const char * const cri_trng0_groups[] = {
	"gpio70",
};
static const char * const cri_trng1_groups[] = {
	"gpio71",
};
static const char * const dbg_out_groups[] = {
	"gpio59",
};
static const char * const ddr_bist_groups[] = {
	"gpio4", "gpio5", "gpio100", "gpio103",
};
static const char * const ddr_pxi0_groups[] = {
	"gpio56", "gpio57",
};
static const char * const ddr_pxi1_groups[] = {
	"gpio41", "gpio45",
};
static const char * const ddr_pxi2_groups[] = {
	"gpio48", "gpio55",
};
static const char * const ddr_pxi3_groups[] = {
	"gpio46", "gpio47",
};
static const char * const dp0_hot_groups[] = {
	"gpio35", "gpio103",
};
static const char * const ext_mclk0_groups[] = {
	"gpio104",
};
static const char * const ext_mclk1_groups[] = {
	"gpio103",
};
static const char * const gcc_gp1_groups[] = {
	"gpio129", "gpio132",
};
static const char * const gcc_gp2_groups[] = {
	"gpio130", "gpio135",
};
static const char * const gcc_gp3_groups[] = {
	"gpio131", "gpio136",
};
static const char * const host2wlan_sol_groups[] = {
	"gpio111",
};
static const char * const i2s0_data0_groups[] = {
	"gpio106",
};
static const char * const i2s0_data1_groups[] = {
	"gpio107",
};
static const char * const i2s0_sck_groups[] = {
	"gpio105",
};
static const char * const i2s0_ws_groups[] = {
	"gpio108",
};
static const char * const ibi_i3c_groups[] = {
	"gpio0", "gpio1", "gpio91", "gpio92",
};
static const char * const jitter_bist_groups[] = {
	"gpio0",
};
static const char * const mdp_vsync_groups[] = {
	"gpio12", "gpio13", "gpio41", "gpio49", "gpio50",
};
static const char * const mdp_vsync0_groups[] = {
	"gpio49",
};
static const char * const mdp_vsync1_groups[] = {
	"gpio49",
};
static const char * const mdp_vsync2_groups[] = {
	"gpio50",
};
static const char * const mdp_vsync3_groups[] = {
	"gpio50",
};
static const char * const phase_flag0_groups[] = {
	"gpio133",
};
static const char * const phase_flag1_groups[] = {
	"gpio128",
};
static const char * const phase_flag10_groups[] = {
	"gpio94",
};
static const char * const phase_flag11_groups[] = {
	"gpio93",
};
static const char * const phase_flag12_groups[] = {
	"gpio134",
};
static const char * const phase_flag13_groups[] = {
	"gpio139",
};
static const char * const phase_flag14_groups[] = {
	"gpio138",
};
static const char * const phase_flag15_groups[] = {
	"gpio137",
};
static const char * const phase_flag16_groups[] = {
	"gpio62",
};
static const char * const phase_flag17_groups[] = {
	"gpio61",
};
static const char * const phase_flag18_groups[] = {
	"gpio41",
};
static const char * const phase_flag19_groups[] = {
	"gpio23",
};
static const char * const phase_flag2_groups[] = {
	"gpio127",
};
static const char * const phase_flag20_groups[] = {
	"gpio22",
};
static const char * const phase_flag21_groups[] = {
	"gpio21",
};
static const char * const phase_flag22_groups[] = {
	"gpio19",
};
static const char * const phase_flag23_groups[] = {
	"gpio18",
};
static const char * const phase_flag24_groups[] = {
	"gpio17",
};
static const char * const phase_flag25_groups[] = {
	"gpio16",
};
static const char * const phase_flag26_groups[] = {
	"gpio13",
};
static const char * const phase_flag27_groups[] = {
	"gpio12",
};
static const char * const phase_flag28_groups[] = {
	"gpio3",
};
static const char * const phase_flag29_groups[] = {
	"gpio2",
};
static const char * const phase_flag3_groups[] = {
	"gpio126",
};
static const char * const phase_flag30_groups[] = {
	"gpio149",
};
static const char * const phase_flag31_groups[] = {
	"gpio148",
};
static const char * const phase_flag4_groups[] = {
	"gpio151",
};
static const char * const phase_flag5_groups[] = {
	"gpio150",
};
static const char * const phase_flag6_groups[] = {
	"gpio98",
};
static const char * const phase_flag7_groups[] = {
	"gpio97",
};
static const char * const phase_flag8_groups[] = {
	"gpio96",
};
static const char * const phase_flag9_groups[] = {
	"gpio95",
};
static const char * const pll_bist_groups[] = {
	"gpio8",
};
static const char * const pll_clk_groups[] = {
	"gpio54",
};
static const char * const prng_rosc0_groups[] = {
	"gpio72",
};
static const char * const prng_rosc1_groups[] = {
	"gpio73",
};
static const char * const prng_rosc2_groups[] = {
	"gpio74",
};
static const char * const prng_rosc3_groups[] = {
	"gpio75",
};
static const char * const qdss_cti_groups[] = {
	"gpio28", "gpio29", "gpio36", "gpio37", "gpio38", "gpio38", "gpio47",
	"gpio48", "gpio53", "gpio53", "gpio105", "gpio106", "gpio154",
	"gpio155",
};
static const char * const qdss_gpio_groups[] = {
	"gpio89", "gpio90", "gpio109", "gpio110",
};
static const char * const qdss_gpio0_groups[] = {
	"gpio24", "gpio65",
};
static const char * const qdss_gpio1_groups[] = {
	"gpio25", "gpio66",
};
static const char * const qdss_gpio10_groups[] = {
	"gpio63", "gpio83",
};
static const char * const qdss_gpio11_groups[] = {
	"gpio64", "gpio84",
};
static const char * const qdss_gpio12_groups[] = {
	"gpio39", "gpio85",
};
static const char * const qdss_gpio13_groups[] = {
	"gpio10", "gpio86",
};
static const char * const qdss_gpio14_groups[] = {
	"gpio45", "gpio87",
};
static const char * const qdss_gpio15_groups[] = {
	"gpio11", "gpio88",
};
static const char * const qdss_gpio2_groups[] = {
	"gpio26", "gpio67",
};
static const char * const qdss_gpio3_groups[] = {
	"gpio27", "gpio68",
};
static const char * const qdss_gpio4_groups[] = {
	"gpio30", "gpio77",
};
static const char * const qdss_gpio5_groups[] = {
	"gpio31", "gpio78",
};
static const char * const qdss_gpio6_groups[] = {
	"gpio4", "gpio79",
};
static const char * const qdss_gpio7_groups[] = {
	"gpio5", "gpio80",
};
static const char * const qdss_gpio8_groups[] = {
	"gpio6", "gpio81",
};
static const char * const qdss_gpio9_groups[] = {
	"gpio7", "gpio82",
};
static const char * const qspi00_groups[] = {
	"gpio32",
};
static const char * const qspi01_groups[] = {
	"gpio33",
};
static const char * const qspi02_groups[] = {
	"gpio36",
};
static const char * const qspi03_groups[] = {
	"gpio37",
};
static const char * const qspi0_clk_groups[] = {
	"gpio34",
};
static const char * const qspi0_cs0_groups[] = {
	"gpio35",
};
static const char * const qspi0_cs1_groups[] = {
	"gpio38",
};
static const char * const qup0_se0_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio93",
};
static const char * const qup0_se1_groups[] = {
	"gpio2", "gpio3", "gpio61", "gpio62",
};
static const char * const qup0_se2_groups[] = {
	"gpio12", "gpio13", "gpio22", "gpio23",
};
static const char * const qup0_se3_groups[] = {
	"gpio16", "gpio17", "gpio18", "gpio19", "gpio41",
};
static const char * const qup0_se4_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23", "gpio94",
};
static const char * const qup0_se5_groups[] = {
	"gpio95", "gpio96", "gpio97", "gpio98",
};
static const char * const qup1_se0_groups[] = {
	"gpio63", "gpio64", "gpio91", "gpio92",
};
static const char * const qup1_se1_groups[] = {
	"gpio24", "gpio25", "gpio26", "gpio27",
};
static const char * const qup1_se2_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};
static const char * const qup1_se3_groups[] = {
	"gpio34", "gpio35", "gpio109", "gpio110",
};
static const char * const qup1_se4_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const qup1_se5_groups[] = {
	"gpio14", "gpio15", "gpio28", "gpio30",
};
static const char * const tb_trig_groups[] = {
	"gpio69",
};
static const char * const tgu_ch0_groups[] = {
	"gpio20",
};
static const char * const tgu_ch1_groups[] = {
	"gpio21",
};
static const char * const tgu_ch2_groups[] = {
	"gpio22",
};
static const char * const tgu_ch3_groups[] = {
	"gpio23",
};
static const char * const tmess_prng0_groups[] = {
	"gpio80",
};
static const char * const tmess_prng1_groups[] = {
	"gpio79",
};
static const char * const tmess_prng2_groups[] = {
	"gpio83",
};
static const char * const tmess_prng3_groups[] = {
	"gpio81",
};
static const char * const tsense_pwm1_groups[] = {
	"gpio86",
};
static const char * const tsense_pwm2_groups[] = {
	"gpio86",
};
static const char * const usb0_phy_groups[] = {
	"gpio100",
};
static const char * const vsense_trigger_groups[] = {
	"gpio36",
};

static const struct msm_function neo_functions[] = {
	FUNCTION(gpio),
	FUNCTION(qup0_se0),
	FUNCTION(ibi_i3c),
	FUNCTION(jitter_bist),
	FUNCTION(qup0_se1),
	FUNCTION(phase_flag29),
	FUNCTION(phase_flag28),
	FUNCTION(qup1_se4),
	FUNCTION(ddr_bist),
	FUNCTION(qdss_gpio6),
	FUNCTION(qdss_gpio7),
	FUNCTION(qdss_gpio8),
	FUNCTION(qdss_gpio9),
	FUNCTION(qup1_se2),
	FUNCTION(pll_bist),
	FUNCTION(qdss_gpio13),
	FUNCTION(qdss_gpio15),
	FUNCTION(qup0_se2),
	FUNCTION(mdp_vsync),
	FUNCTION(phase_flag27),
	FUNCTION(phase_flag26),
	FUNCTION(qup1_se5),
	FUNCTION(qup0_se3),
	FUNCTION(phase_flag25),
	FUNCTION(phase_flag24),
	FUNCTION(phase_flag23),
	FUNCTION(phase_flag22),
	FUNCTION(qup0_se4),
	FUNCTION(aoss_cti),
	FUNCTION(tgu_ch0),
	FUNCTION(phase_flag21),
	FUNCTION(tgu_ch1),
	FUNCTION(phase_flag20),
	FUNCTION(tgu_ch2),
	FUNCTION(phase_flag19),
	FUNCTION(tgu_ch3),
	FUNCTION(qup1_se1),
	FUNCTION(qdss_gpio0),
	FUNCTION(qdss_gpio1),
	FUNCTION(qdss_gpio2),
	FUNCTION(atest_usb0),
	FUNCTION(qdss_gpio3),
	FUNCTION(atest_usb02),
	FUNCTION(qdss_cti),
	FUNCTION(qdss_gpio4),
	FUNCTION(qdss_gpio5),
	FUNCTION(qspi00),
	FUNCTION(qspi01),
	FUNCTION(qspi0_clk),
	FUNCTION(qup1_se3),
	FUNCTION(qspi0_cs0),
	FUNCTION(dp0_hot),
	FUNCTION(qspi02),
	FUNCTION(vsense_trigger),
	FUNCTION(qspi03),
	FUNCTION(qspi0_cs1),
	FUNCTION(qdss_gpio12),
	FUNCTION(phase_flag18),
	FUNCTION(ddr_pxi1),
	FUNCTION(qdss_gpio14),
	FUNCTION(atest_char),
	FUNCTION(ddr_pxi3),
	FUNCTION(ddr_pxi2),
	FUNCTION(mdp_vsync0),
	FUNCTION(mdp_vsync1),
	FUNCTION(mdp_vsync2),
	FUNCTION(mdp_vsync3),
	FUNCTION(pll_clk),
	FUNCTION(PCIE0_CLK_REQ_N),
	FUNCTION(ddr_pxi0),
	FUNCTION(PCIE1_CLK_REQ_N),
	FUNCTION(dbg_out),
	FUNCTION(cri_trng),
	FUNCTION(atest_usb03),
	FUNCTION(phase_flag17),
	FUNCTION(phase_flag16),
	FUNCTION(qup1_se0),
	FUNCTION(qdss_gpio10),
	FUNCTION(qdss_gpio11),
	FUNCTION(cci_i2c),
	FUNCTION(cam_mclk),
	FUNCTION(tb_trig),
	FUNCTION(cri_trng0),
	FUNCTION(cri_trng1),
	FUNCTION(prng_rosc0),
	FUNCTION(prng_rosc1),
	FUNCTION(prng_rosc2),
	FUNCTION(prng_rosc3),
	FUNCTION(cci_timer0),
	FUNCTION(cci_timer1),
	FUNCTION(cci_timer2),
	FUNCTION(tmess_prng1),
	FUNCTION(cci_timer3),
	FUNCTION(cci_async),
	FUNCTION(tmess_prng0),
	FUNCTION(cci_timer4),
	FUNCTION(tmess_prng3),
	FUNCTION(tmess_prng2),
	FUNCTION(tsense_pwm1),
	FUNCTION(tsense_pwm2),
	FUNCTION(atest_char3),
	FUNCTION(atest_char2),
	FUNCTION(qdss_gpio),
	FUNCTION(atest_char1),
	FUNCTION(atest_char0),
	FUNCTION(phase_flag11),
	FUNCTION(phase_flag10),
	FUNCTION(qup0_se5),
	FUNCTION(phase_flag9),
	FUNCTION(phase_flag8),
	FUNCTION(phase_flag7),
	FUNCTION(phase_flag6),
	FUNCTION(usb0_phy),
	FUNCTION(ext_mclk1),
	FUNCTION(audio_ref),
	FUNCTION(ext_mclk0),
	FUNCTION(i2s0_sck),
	FUNCTION(i2s0_data0),
	FUNCTION(i2s0_data1),
	FUNCTION(i2s0_ws),
	FUNCTION(atest_usb01),
	FUNCTION(atest_usb00),
	FUNCTION(host2wlan_sol),
	FUNCTION(phase_flag3),
	FUNCTION(phase_flag2),
	FUNCTION(phase_flag1),
	FUNCTION(gcc_gp1),
	FUNCTION(gcc_gp2),
	FUNCTION(gcc_gp3),
	FUNCTION(phase_flag0),
	FUNCTION(phase_flag12),
	FUNCTION(phase_flag15),
	FUNCTION(phase_flag14),
	FUNCTION(phase_flag13),
	FUNCTION(phase_flag31),
	FUNCTION(phase_flag30),
	FUNCTION(phase_flag5),
	FUNCTION(phase_flag4),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup neo_groups[] = {
	[0] = PINGROUP(0, qup0_se0, ibi_i3c, jitter_bist, NA, NA, NA, NA, NA,
		       NA, 0x9C014, 0),
	[1] = PINGROUP(1, qup0_se0, ibi_i3c, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[2] = PINGROUP(2, qup0_se0, qup0_se1, phase_flag29, NA, NA, NA, NA, NA,
		       NA, 0, -1),
	[3] = PINGROUP(3, qup0_se0, qup0_se1, phase_flag28, NA, NA, NA, NA, NA,
		       NA, 0x9C014, 1),
	[4] = PINGROUP(4, qup1_se4, ddr_bist, qdss_gpio6, NA, NA, NA, NA, NA,
		       NA, 0, -1),
	[5] = PINGROUP(5, qup1_se4, ddr_bist, qdss_gpio7, NA, NA, NA, NA, NA,
		       NA, 0, -1),
	[6] = PINGROUP(6, qup1_se4, qdss_gpio8, NA, NA, NA, NA, NA, NA, NA,
		       0x9C008, 3),
	[7] = PINGROUP(7, qup1_se4, qdss_gpio9, NA, NA, NA, NA, NA, NA, NA,
		       0x9C008, 4),
	[8] = PINGROUP(8, qup1_se2, pll_bist, NA, NA, NA, NA, NA, NA, NA,
		       0, -1),
	[9] = PINGROUP(9, qup1_se2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[10] = PINGROUP(10, qup1_se2, qdss_gpio13, NA, NA, NA, NA, NA, NA, NA,
			0x9C008, 5),
	[11] = PINGROUP(11, qup1_se2, qdss_gpio15, NA, NA, NA, NA, NA, NA, NA,
			0x9C008, 6),
	[12] = PINGROUP(12, qup0_se2, mdp_vsync, phase_flag27, NA, NA, NA, NA,
			NA, NA, 0x9C014, 2),
	[13] = PINGROUP(13, qup0_se2, mdp_vsync, phase_flag26, NA, NA, NA, NA,
			NA, NA, 0x9C014, 3),
	[14] = PINGROUP(14, qup1_se5, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C008, 7),
	[15] = PINGROUP(15, qup1_se5, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C008, 8),
	[16] = PINGROUP(16, qup0_se3, phase_flag25, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[17] = PINGROUP(17, qup0_se3, phase_flag24, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[18] = PINGROUP(18, qup0_se3, phase_flag23, NA, NA, NA, NA, NA, NA, NA,
			0x9C014, 4),
	[19] = PINGROUP(19, qup0_se3, phase_flag22, NA, NA, NA, NA, NA, NA, NA,
			0x9C014, 5),
	[20] = PINGROUP(20, qup0_se4, aoss_cti, tgu_ch0, NA, NA, NA, NA, NA,
			NA, 0, -1),
	[21] = PINGROUP(21, qup0_se4, aoss_cti, phase_flag21, tgu_ch1, NA, NA,
			NA, NA, NA, 0, -1),
	[22] = PINGROUP(22, qup0_se4, qup0_se2, aoss_cti, phase_flag20,
			tgu_ch2, NA, NA, NA, NA, 0, -1),
	[23] = PINGROUP(23, qup0_se4, qup0_se2, aoss_cti, phase_flag19,
			tgu_ch3, NA, NA, NA, NA, 0x9C014, 6),
	[24] = PINGROUP(24, qup1_se1, qdss_gpio0, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[25] = PINGROUP(25, qup1_se1, qdss_gpio1, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[26] = PINGROUP(26, qup1_se1, qdss_gpio2, atest_usb0, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[27] = PINGROUP(27, qup1_se1, qdss_gpio3, atest_usb02, NA, NA, NA, NA,
			NA, NA, 0x9C008, 9),
	[28] = PINGROUP(28, qup1_se5, qdss_cti, NA, NA, NA, NA, NA, NA, NA,
			0x9C008, 10),
	[29] = PINGROUP(29, qdss_cti, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C000, 0),
	[30] = PINGROUP(30, qup1_se5, qdss_gpio4, NA, NA, NA, NA, NA, NA, NA,
			0x9C008, 11),
	[31] = PINGROUP(31, qdss_gpio5, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C008, 12),
	[32] = PINGROUP(32, qspi00, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C008, 13),
	[33] = PINGROUP(33, qspi01, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C008, 14),
	[34] = PINGROUP(34, qspi0_clk, qup1_se3, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[35] = PINGROUP(35, qspi0_cs0, qup1_se3, dp0_hot, NA, NA, NA, NA, NA,
			NA, 0x9C008, 15),
	[36] = PINGROUP(36, qspi02, qdss_cti, vsense_trigger, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[37] = PINGROUP(37, qspi03, qdss_cti, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 0),
	[38] = PINGROUP(38, qspi0_cs1, qdss_cti, qdss_cti, NA, NA, NA, NA, NA,
			NA, 0x9C00C, 1),
	[39] = PINGROUP(39, qdss_gpio12, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 2),
	[40] = PINGROUP(40, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 1),
	[41] = PINGROUP(41, qup0_se3, mdp_vsync, phase_flag18, NA, ddr_pxi1,
			NA, NA, NA, NA, 0x9C014, 7),
	[42] = PINGROUP(42, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C00C, 3),
	[43] = PINGROUP(43, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 2),
	[44] = PINGROUP(44, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 3),
	[45] = PINGROUP(45, qdss_gpio14, ddr_pxi1, atest_char, NA, NA, NA, NA,
			NA, NA, 0x9C00C, 4),
	[46] = PINGROUP(46, ddr_pxi3, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 5),
	[47] = PINGROUP(47, qdss_cti, ddr_pxi3, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 6),
	[48] = PINGROUP(48, qdss_cti, ddr_pxi2, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 7),
	[49] = PINGROUP(49, mdp_vsync, mdp_vsync0, mdp_vsync1, NA, NA, NA, NA,
			NA, NA, 0x9C014, 8),
	[50] = PINGROUP(50, mdp_vsync, mdp_vsync2, mdp_vsync3, NA, NA, NA, NA,
			NA, NA, 0x9C014, 9),
	[51] = PINGROUP(51, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[52] = PINGROUP(52, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[53] = PINGROUP(53, qdss_cti, qdss_cti, NA, NA, NA, NA, NA, NA, NA,
			0x9C000, 4),
	[54] = PINGROUP(54, pll_clk, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C000, 5),
	[55] = PINGROUP(55, NA, ddr_pxi2, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 8),
	[56] = PINGROUP(56, PCIE0_CLK_REQ_N, NA, ddr_pxi0, NA, NA, NA, NA, NA, NA,
			0x9C00C, 9),
	[57] = PINGROUP(57, ddr_pxi0, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 10),
	[58] = PINGROUP(58, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C00C, 11),
	[59] = PINGROUP(59, PCIE1_CLK_REQ_N, dbg_out, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 12),
	[60] = PINGROUP(60, cri_trng, atest_usb03, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 13),
	[61] = PINGROUP(61, qup0_se1, phase_flag17, NA, NA, NA, NA, NA, NA, NA,
			0x9C014, 10),
	[62] = PINGROUP(62, qup0_se1, phase_flag16, NA, NA, NA, NA, NA, NA, NA,
			0x9C014, 11),
	[63] = PINGROUP(63, qup1_se0, qdss_gpio10, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 14),
	[64] = PINGROUP(64, qup1_se0, qdss_gpio11, NA, NA, NA, NA, NA, NA, NA,
			0x9C00C, 15),
	[65] = PINGROUP(65, qdss_gpio0, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C014, 12),
	[66] = PINGROUP(66, qdss_gpio1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[67] = PINGROUP(67, cci_i2c, qdss_gpio2, NA, NA, NA, NA, NA, NA, NA,
			0x9C014, 13),
	[68] = PINGROUP(68, cci_i2c, qdss_gpio3, NA, NA, NA, NA, NA, NA, NA,
			0x9C014, 14),
	[69] = PINGROUP(69, cam_mclk, tb_trig, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[70] = PINGROUP(70, cam_mclk, cri_trng0, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[71] = PINGROUP(71, cam_mclk, cri_trng1, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[72] = PINGROUP(72, cam_mclk, prng_rosc0, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[73] = PINGROUP(73, cam_mclk, prng_rosc1, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[74] = PINGROUP(74, cam_mclk, prng_rosc2, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[75] = PINGROUP(75, cam_mclk, prng_rosc3, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[76] = PINGROUP(76, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA,
			0x9C010, 0),
	[77] = PINGROUP(77, cci_timer0, qdss_gpio4, NA, NA, NA, NA, NA, NA, NA,
			0x9C014, 15),
	[78] = PINGROUP(78, cci_timer1, cci_i2c, qdss_gpio5, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[79] = PINGROUP(79, cci_timer2, cci_i2c, tmess_prng1, qdss_gpio6, NA,
			NA, NA, NA, NA, 0, -1),
	[80] = PINGROUP(80, cci_timer3, cci_i2c, cci_async, tmess_prng0,
			qdss_gpio7, NA, NA, NA, NA, 0, -1),
	[81] = PINGROUP(81, cci_timer4, cci_i2c, cci_async, tmess_prng3,
			qdss_gpio8, NA, NA, NA, NA, 0, -1),
	[82] = PINGROUP(82, cci_async, qdss_gpio9, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[83] = PINGROUP(83, cci_i2c, tmess_prng2, qdss_gpio10, NA, NA, NA, NA,
			NA, NA, 0x9C018, 0),
	[84] = PINGROUP(84, cci_i2c, qdss_gpio11, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[85] = PINGROUP(85, cci_i2c, qdss_gpio12, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[86] = PINGROUP(86, cci_i2c, qdss_gpio13, tsense_pwm1, tsense_pwm2, NA,
			NA, NA, NA, NA, 0, -1),
	[87] = PINGROUP(87, cci_i2c, qdss_gpio14, atest_char3, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[88] = PINGROUP(88, cci_i2c, qdss_gpio15, atest_char2, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[89] = PINGROUP(89, cci_i2c, qdss_gpio, atest_char1, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[90] = PINGROUP(90, cci_i2c, qdss_gpio, atest_char0, NA, NA, NA, NA,
			NA, NA, 0, -1),
	[91] = PINGROUP(91, cci_i2c, qup1_se0, ibi_i3c, NA, NA, NA, NA, NA, NA,
			0x9C010, 1),
	[92] = PINGROUP(92, cci_i2c, qup1_se0, ibi_i3c, NA, NA, NA, NA, NA, NA,
			0, -1),
	[93] = PINGROUP(93, qup0_se0, phase_flag11, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[94] = PINGROUP(94, qup0_se4, phase_flag10, NA, NA, NA, NA, NA, NA, NA,
			0x9C018, 1),
	[95] = PINGROUP(95, qup0_se5, phase_flag9, NA, NA, NA, NA, NA, NA, NA,
			0x9C018, 2),
	[96] = PINGROUP(96, qup0_se5, phase_flag8, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[97] = PINGROUP(97, qup0_se5, phase_flag7, NA, NA, NA, NA, NA, NA, NA,
			0, -1),
	[98] = PINGROUP(98, qup0_se5, phase_flag6, NA, NA, NA, NA, NA, NA, NA,
			0x9C018, 3),
	[99] = PINGROUP(99, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[100] = PINGROUP(100, usb0_phy, ddr_bist, NA, NA, NA, NA, NA, NA, NA,
			 0x9C000, 6),
	[101] = PINGROUP(101, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 7),
	[102] = PINGROUP(102, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 8),
	[103] = PINGROUP(103, ext_mclk1, audio_ref, dp0_hot, ddr_bist, NA, NA,
			 NA, NA, NA, 0x9C018, 4),
	[104] = PINGROUP(104, ext_mclk0, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C018, 5),
	[105] = PINGROUP(105, i2s0_sck, NA, qdss_cti, NA, NA, NA, NA, NA, NA,
			 0x9C018, 6),
	[106] = PINGROUP(106, i2s0_data0, NA, qdss_cti, NA, NA, NA, NA, NA, NA,
			 0x9C018, 7),
	[107] = PINGROUP(107, i2s0_data1, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C018, 8),
	[108] = PINGROUP(108, i2s0_ws, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C018, 9),
	[109] = PINGROUP(109, qup1_se3, qdss_gpio, atest_usb01, NA, NA, NA, NA,
			 NA, NA, 0x9C010, 2),
	[110] = PINGROUP(110, qup1_se3, qdss_gpio, atest_usb00, NA, NA, NA, NA,
			 NA, NA, 0, -1),
	[111] = PINGROUP(111, host2wlan_sol, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C000, 9),
	[112] = PINGROUP(112, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 10),
	[113] = PINGROUP(113, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C010, 3),
	[114] = PINGROUP(114, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 11),
	[115] = PINGROUP(115, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 12),
	[116] = PINGROUP(116, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 13),
	[117] = PINGROUP(117, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 14),
	[118] = PINGROUP(118, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C000, 15),
	[119] = PINGROUP(119, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[120] = PINGROUP(120, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[121] = PINGROUP(121, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 0),
	[122] = PINGROUP(122, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C004, 1),
	[123] = PINGROUP(123, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[124] = PINGROUP(124, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C018, 10),
	[125] = PINGROUP(125, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[126] = PINGROUP(126, phase_flag3, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[127] = PINGROUP(127, phase_flag2, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C018, 11),
	[128] = PINGROUP(128, phase_flag1, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[129] = PINGROUP(129, gcc_gp1, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[130] = PINGROUP(130, gcc_gp2, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[131] = PINGROUP(131, gcc_gp3, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[132] = PINGROUP(132, gcc_gp1, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C018, 12),
	[133] = PINGROUP(133, phase_flag0, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[134] = PINGROUP(134, phase_flag12, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C018, 13),
	[135] = PINGROUP(135, gcc_gp2, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C018, 14),
	[136] = PINGROUP(136, gcc_gp3, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C018, 15),
	[137] = PINGROUP(137, phase_flag15, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[138] = PINGROUP(138, phase_flag14, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[139] = PINGROUP(139, phase_flag13, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C01C, 0),
	[140] = PINGROUP(140, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C01C, 1),
	[141] = PINGROUP(141, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C01C, 2),
	[142] = PINGROUP(142, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[143] = PINGROUP(143, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C01C, 3),
	[144] = PINGROUP(144, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C01C, 4),
	[145] = PINGROUP(145, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C01C, 5),
	[146] = PINGROUP(146, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C01C, 6),
	[147] = PINGROUP(147, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[148] = PINGROUP(148, phase_flag31, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C01C, 7),
	[149] = PINGROUP(149, phase_flag30, NA, NA, NA, NA, NA, NA, NA, NA,
			 0, -1),
	[150] = PINGROUP(150, phase_flag5, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C01C, 8),
	[151] = PINGROUP(151, phase_flag4, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C01C, 9),
	[152] = PINGROUP(152, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[153] = PINGROUP(153, NA, NA, NA, NA, NA, NA, NA, NA, NA, 0x9C01C, 10),
	[154] = PINGROUP(154, qdss_cti, NA, NA, NA, NA, NA, NA, NA, NA, 0, -1),
	[155] = PINGROUP(155, qdss_cti, NA, NA, NA, NA, NA, NA, NA, NA,
			 0x9C01C, 11),
	[156] = SDC_QDSD_PINGROUP(sdc1_rclk, 0x1a1000, 0, 0),
	[157] = SDC_QDSD_PINGROUP(sdc1_clk, 0x1a0000, 13, 6),
	[158] = SDC_QDSD_PINGROUP(sdc1_cmd, 0x1a0000, 11, 3),
	[159] = SDC_QDSD_PINGROUP(sdc1_data, 0x1a0000, 9, 0),
};

static const struct msm_gpio_wakeirq_map neo_pdc_map[] = {
	{ 0, 50 }, { 3, 68 }, { 6, 88 }, { 7, 55 }, { 10, 66 }, { 11, 96 }, { 12, 48 },
	{ 13, 49 }, { 15, 62 }, { 18, 57 }, { 19, 59 }, { 23, 51 }, { 27, 74 },
	{ 28, 67 }, { 29, 84 }, { 30, 58 }, { 31, 94 }, { 32, 60 }, { 33, 61 },
	{ 35, 69 }, { 37, 70 }, { 38, 64 }, { 39, 65 }, { 40, 63 }, { 41, 92 },
	{ 42, 82 }, { 44, 83 }, { 45, 43 }, { 46, 72 }, { 47, 45 }, { 48, 44 },
	{ 49, 71 }, { 50, 87 }, { 53, 77 }, { 54, 78 }, { 55, 106 }, { 56, 79 },
	{ 57, 80 }, { 58, 107 }, { 59, 81 }, { 60, 89 }, { 61, 54 }, { 62, 73 },
	{ 63, 93 }, { 64, 86 }, { 65, 75 }, { 67, 42 }, { 68, 76 }, { 76, 116 },
	{ 77, 12 }, { 83, 13 }, { 91, 90 }, { 94, 95 }, { 95, 91 }, { 98, 47 },
	{ 100, 85 }, { 101, 52 }, { 102, 53 }, { 103, 97 }, { 104, 98 }, { 105, 99 },
	{ 106, 100 }, { 107, 101 }, { 108, 102 }, { 109, 103 }, { 111, 104 }, { 113, 46 },
	{ 114, 56 }, { 115, 108 }, { 116, 109 }, { 117, 110 }, { 118, 111 }, { 121, 112 },
	{ 122, 113 }, { 124, 114 }, { 127, 115 }, { 132, 118 }, { 134, 119 }, { 135, 120 },
	{ 136, 121 }, { 139, 122 }, { 140, 123 }, { 141, 124 }, { 143, 128 }, { 144, 129 },
	{ 145, 130 }, { 146, 131 }, { 148, 132 }, { 150, 133 }, { 151, 134 }, { 153, 135 },
	{ 155, 137 },
};

static struct pinctrl_qup neo_qup_regs[] = {
};
