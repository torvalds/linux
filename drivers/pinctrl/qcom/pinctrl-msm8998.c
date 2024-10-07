// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define NORTH	0x500000
#define WEST	0x100000
#define EAST	0x900000

#define PINGROUP(id, base, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
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
		.ctl_reg = base + 0x1000 * id,	\
		.io_reg = base + 0x4 + 0x1000 * id,		\
		.intr_cfg_reg = base + 0x8 + 0x1000 * id,	\
		.intr_status_reg = base + 0xc + 0x1000 * id,	\
		.intr_target_reg = base + 0x8 + 0x1000 * id,	\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
		.oe_bit = 9,			\
		.in_bit = 0,			\
		.out_bit = 1,			\
		.intr_enable_bit = 0,		\
		.intr_status_bit = 0,		\
		.intr_target_bit = 5,		\
		.intr_target_kpss_val = 3,  \
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

static const struct pinctrl_pin_desc msm8998_pins[] = {
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
	PINCTRL_PIN(150, "SDC2_CLK"),
	PINCTRL_PIN(151, "SDC2_CMD"),
	PINCTRL_PIN(152, "SDC2_DATA"),
	PINCTRL_PIN(153, "UFS_RESET"),
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

static const unsigned int sdc2_clk_pins[] = { 150 };
static const unsigned int sdc2_cmd_pins[] = { 151 };
static const unsigned int sdc2_data_pins[] = { 152 };
static const unsigned int ufs_reset_pins[] = { 153 };

enum msm8998_functions {
	msm_mux_adsp_ext,
	msm_mux_agera_pll,
	msm_mux_atest_char,
	msm_mux_atest_gpsadc0,
	msm_mux_atest_gpsadc1,
	msm_mux_atest_tsens,
	msm_mux_atest_tsens2,
	msm_mux_atest_usb1,
	msm_mux_atest_usb10,
	msm_mux_atest_usb11,
	msm_mux_atest_usb12,
	msm_mux_atest_usb13,
	msm_mux_audio_ref,
	msm_mux_bimc_dte0,
	msm_mux_bimc_dte1,
	msm_mux_blsp10_spi,
	msm_mux_blsp10_spi_a,
	msm_mux_blsp10_spi_b,
	msm_mux_blsp11_i2c,
	msm_mux_blsp1_spi,
	msm_mux_blsp1_spi_a,
	msm_mux_blsp1_spi_b,
	msm_mux_blsp2_spi,
	msm_mux_blsp9_spi,
	msm_mux_blsp_i2c1,
	msm_mux_blsp_i2c10,
	msm_mux_blsp_i2c11,
	msm_mux_blsp_i2c12,
	msm_mux_blsp_i2c2,
	msm_mux_blsp_i2c3,
	msm_mux_blsp_i2c4,
	msm_mux_blsp_i2c5,
	msm_mux_blsp_i2c6,
	msm_mux_blsp_i2c7,
	msm_mux_blsp_i2c8,
	msm_mux_blsp_i2c9,
	msm_mux_blsp_spi1,
	msm_mux_blsp_spi10,
	msm_mux_blsp_spi11,
	msm_mux_blsp_spi12,
	msm_mux_blsp_spi2,
	msm_mux_blsp_spi3,
	msm_mux_blsp_spi4,
	msm_mux_blsp_spi5,
	msm_mux_blsp_spi6,
	msm_mux_blsp_spi7,
	msm_mux_blsp_spi8,
	msm_mux_blsp_spi9,
	msm_mux_blsp_uart1_a,
	msm_mux_blsp_uart1_b,
	msm_mux_blsp_uart2_a,
	msm_mux_blsp_uart2_b,
	msm_mux_blsp_uart3_a,
	msm_mux_blsp_uart3_b,
	msm_mux_blsp_uart7_a,
	msm_mux_blsp_uart7_b,
	msm_mux_blsp_uart8,
	msm_mux_blsp_uart8_a,
	msm_mux_blsp_uart8_b,
	msm_mux_blsp_uart9_a,
	msm_mux_blsp_uart9_b,
	msm_mux_blsp_uim1_a,
	msm_mux_blsp_uim1_b,
	msm_mux_blsp_uim2_a,
	msm_mux_blsp_uim2_b,
	msm_mux_blsp_uim3_a,
	msm_mux_blsp_uim3_b,
	msm_mux_blsp_uim7_a,
	msm_mux_blsp_uim7_b,
	msm_mux_blsp_uim8_a,
	msm_mux_blsp_uim8_b,
	msm_mux_blsp_uim9_a,
	msm_mux_blsp_uim9_b,
	msm_mux_bt_reset,
	msm_mux_btfm_slimbus,
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
	msm_mux_edp_hot,
	msm_mux_edp_lcd,
	msm_mux_gcc_gp1_a,
	msm_mux_gcc_gp1_b,
	msm_mux_gcc_gp2_a,
	msm_mux_gcc_gp2_b,
	msm_mux_gcc_gp3_a,
	msm_mux_gcc_gp3_b,
	msm_mux_gpio,
	msm_mux_hdmi_cec,
	msm_mux_hdmi_ddc,
	msm_mux_hdmi_hot,
	msm_mux_hdmi_rcv,
	msm_mux_isense_dbg,
	msm_mux_jitter_bist,
	msm_mux_ldo_en,
	msm_mux_ldo_update,
	msm_mux_lpass_slimbus,
	msm_mux_m_voc,
	msm_mux_mdp_vsync,
	msm_mux_mdp_vsync0,
	msm_mux_mdp_vsync1,
	msm_mux_mdp_vsync2,
	msm_mux_mdp_vsync3,
	msm_mux_mdp_vsync_a,
	msm_mux_mdp_vsync_b,
	msm_mux_modem_tsync,
	msm_mux_mss_lte,
	msm_mux_nav_dr,
	msm_mux_nav_pps,
	msm_mux_pa_indicator,
	msm_mux_pci_e0,
	msm_mux_phase_flag,
	msm_mux_pll_bypassnl,
	msm_mux_pll_reset,
	msm_mux_pri_mi2s,
	msm_mux_pri_mi2s_ws,
	msm_mux_prng_rosc,
	msm_mux_pwr_crypto,
	msm_mux_pwr_modem,
	msm_mux_pwr_nav,
	msm_mux_qdss_cti0_a,
	msm_mux_qdss_cti0_b,
	msm_mux_qdss_cti1_a,
	msm_mux_qdss_cti1_b,
	msm_mux_qdss,
	msm_mux_qlink_enable,
	msm_mux_qlink_request,
	msm_mux_qua_mi2s,
	msm_mux_sd_card,
	msm_mux_sd_write,
	msm_mux_sdc40,
	msm_mux_sdc41,
	msm_mux_sdc42,
	msm_mux_sdc43,
	msm_mux_sdc4_clk,
	msm_mux_sdc4_cmd,
	msm_mux_sec_mi2s,
	msm_mux_sp_cmu,
	msm_mux_spkr_i2s,
	msm_mux_ssbi1,
	msm_mux_ssc_irq,
	msm_mux_ter_mi2s,
	msm_mux_tgu_ch0,
	msm_mux_tgu_ch1,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_tsif0,
	msm_mux_tsif1,
	msm_mux_uim1_clk,
	msm_mux_uim1_data,
	msm_mux_uim1_present,
	msm_mux_uim1_reset,
	msm_mux_uim2_clk,
	msm_mux_uim2_data,
	msm_mux_uim2_present,
	msm_mux_uim2_reset,
	msm_mux_uim_batt,
	msm_mux_usb_phy,
	msm_mux_vfr_1,
	msm_mux_vsense_clkout,
	msm_mux_vsense_data0,
	msm_mux_vsense_data1,
	msm_mux_vsense_mode,
	msm_mux_wlan1_adc0,
	msm_mux_wlan1_adc1,
	msm_mux_wlan2_adc0,
	msm_mux_wlan2_adc1,
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
	"gpio99", "gpio100", "gpio101",	"gpio102", "gpio103", "gpio104",
	"gpio105", "gpio106", "gpio107", "gpio108", "gpio109", "gpio110",
	"gpio111", "gpio112", "gpio113", "gpio114", "gpio115", "gpio116",
	"gpio117", "gpio118", "gpio119", "gpio120", "gpio121", "gpio122",
	"gpio123", "gpio124", "gpio125", "gpio126", "gpio127", "gpio128",
	"gpio129", "gpio130", "gpio131", "gpio132", "gpio133", "gpio134",
	"gpio135", "gpio136", "gpio137", "gpio138", "gpio139", "gpio140",
	"gpio141", "gpio142", "gpio143", "gpio144", "gpio145", "gpio146",
	"gpio147", "gpio148", "gpio149",
};
static const char * const blsp_spi1_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const blsp_uim1_a_groups[] = {
	"gpio0", "gpio1",
};
static const char * const blsp_uart1_a_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const blsp_i2c1_groups[] = {
	"gpio2", "gpio3",
};
static const char * const blsp_spi8_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const blsp_uart8_a_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const blsp_uim8_a_groups[] = {
	"gpio4", "gpio5",
};
static const char * const qdss_cti0_b_groups[] = {
	"gpio4", "gpio5",
};
static const char * const blsp_i2c8_groups[] = {
	"gpio6", "gpio7",
};
static const char * const ddr_bist_groups[] = {
	"gpio7", "gpio8", "gpio9", "gpio10",
};
static const char * const atest_tsens2_groups[] = {
	"gpio7",
};
static const char * const atest_usb1_groups[] = {
	"gpio7",
};
static const char * const blsp_spi4_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};
static const char * const blsp_uart1_b_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};
static const char * const blsp_uim1_b_groups[] = {
	"gpio8", "gpio9",
};
static const char * const wlan1_adc1_groups[] = {
	"gpio8",
};
static const char * const atest_usb13_groups[] = {
	"gpio8",
};
static const char * const bimc_dte1_groups[] = {
	"gpio8", "gpio10",
};
static const char * const wlan1_adc0_groups[] = {
	"gpio9",
};
static const char * const atest_usb12_groups[] = {
	"gpio9",
};
static const char * const bimc_dte0_groups[] = {
	"gpio9", "gpio11",
};
static const char * const mdp_vsync_a_groups[] = {
	"gpio10", "gpio11",
};
static const char * const blsp_i2c4_groups[] = {
	"gpio10", "gpio11",
};
static const char * const atest_gpsadc1_groups[] = {
	"gpio10",
};
static const char * const wlan2_adc1_groups[] = {
	"gpio10",
};
static const char * const atest_usb11_groups[] = {
	"gpio10",
};
static const char * const edp_lcd_groups[] = {
	"gpio11",
};
static const char * const dbg_out_groups[] = {
	"gpio11",
};
static const char * const atest_gpsadc0_groups[] = {
	"gpio11",
};
static const char * const wlan2_adc0_groups[] = {
	"gpio11",
};
static const char * const atest_usb10_groups[] = {
	"gpio11",
};
static const char * const mdp_vsync_groups[] = {
	"gpio12",
};
static const char * const m_voc_groups[] = {
	"gpio12",
};
static const char * const cam_mclk_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16",
};
static const char * const pll_bypassnl_groups[] = {
	"gpio13",
};
static const char * const qdss_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16", "gpio17", "gpio18", "gpio19",
	"gpio20", "gpio21", "gpio22", "gpio23", "gpio24", "gpio25", "gpio26",
	"gpio27", "gpio28", "gpio29", "gpio30", "gpio41", "gpio42", "gpio43",
	"gpio44", "gpio75", "gpio76", "gpio77", "gpio79", "gpio80", "gpio93",
	"gpio117", "gpio118", "gpio119", "gpio120", "gpio121", "gpio122",
	"gpio123", "gpio124",
};
static const char * const pll_reset_groups[] = {
	"gpio14",
};
static const char * const cci_i2c_groups[] = {
	"gpio17", "gpio18", "gpio19", "gpio20",
};
static const char * const phase_flag_groups[] = {
	"gpio18", "gpio19", "gpio73", "gpio74", "gpio75", "gpio76", "gpio77",
	"gpio89", "gpio91", "gpio92", "gpio96", "gpio114", "gpio115",
	"gpio116", "gpio117", "gpio118", "gpio119", "gpio120", "gpio121",
	"gpio122", "gpio123", "gpio124", "gpio125", "gpio126", "gpio128",
	"gpio129", "gpio130", "gpio131", "gpio132", "gpio133", "gpio134",
};
static const char * const cci_timer4_groups[] = {
	"gpio25",
};
static const char * const blsp2_spi_groups[] = {
	"gpio25", "gpio29", "gpio30",
};
static const char * const cci_timer0_groups[] = {
	"gpio21",
};
static const char * const vsense_data0_groups[] = {
	"gpio21",
};
static const char * const cci_timer1_groups[] = {
	"gpio22",
};
static const char * const vsense_data1_groups[] = {
	"gpio22",
};
static const char * const cci_timer2_groups[] = {
	"gpio23",
};
static const char * const blsp1_spi_b_groups[] = {
	"gpio23", "gpio28",
};
static const char * const vsense_mode_groups[] = {
	"gpio23",
};
static const char * const cci_timer3_groups[] = {
	"gpio24",
};
static const char * const cci_async_groups[] = {
	"gpio24", "gpio25", "gpio26",
};
static const char * const blsp1_spi_a_groups[] = {
	"gpio24", "gpio27",
};
static const char * const vsense_clkout_groups[] = {
	"gpio24",
};
static const char * const hdmi_rcv_groups[] = {
	"gpio30",
};
static const char * const hdmi_cec_groups[] = {
	"gpio31",
};
static const char * const blsp_spi2_groups[] = {
	"gpio31", "gpio32", "gpio33", "gpio34",
};
static const char * const blsp_uart2_a_groups[] = {
	"gpio31", "gpio32", "gpio33", "gpio34",
};
static const char * const blsp_uim2_a_groups[] = {
	"gpio31", "gpio34",
};
static const char * const pwr_modem_groups[] = {
	"gpio31",
};
static const char * const hdmi_ddc_groups[] = {
	"gpio32", "gpio33",
};
static const char * const blsp_i2c2_groups[] = {
	"gpio32", "gpio33",
};
static const char * const pwr_nav_groups[] = {
	"gpio32",
};
static const char * const pwr_crypto_groups[] = {
	"gpio33",
};
static const char * const hdmi_hot_groups[] = {
	"gpio34",
};
static const char * const edp_hot_groups[] = {
	"gpio34",
};
static const char * const pci_e0_groups[] = {
	"gpio35", "gpio36", "gpio37",
};
static const char * const jitter_bist_groups[] = {
	"gpio35",
};
static const char * const agera_pll_groups[] = {
	"gpio36", "gpio37",
};
static const char * const atest_tsens_groups[] = {
	"gpio36",
};
static const char * const usb_phy_groups[] = {
	"gpio38",
};
static const char * const lpass_slimbus_groups[] = {
	"gpio39", "gpio70", "gpio71", "gpio72",
};
static const char * const sd_write_groups[] = {
	"gpio40",
};
static const char * const blsp_spi6_groups[] = {
	"gpio41", "gpio42", "gpio43", "gpio44",
};
static const char * const blsp_uart3_b_groups[] = {
	"gpio41", "gpio42", "gpio43", "gpio44",
};
static const char * const blsp_uim3_b_groups[] = {
	"gpio41", "gpio42",
};
static const char * const blsp_i2c6_groups[] = {
	"gpio43", "gpio44",
};
static const char * const bt_reset_groups[] = {
	"gpio45",
};
static const char * const blsp_spi3_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48",
};
static const char * const blsp_uart3_a_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48",
};
static const char * const blsp_uim3_a_groups[] = {
	"gpio45", "gpio46",
};
static const char * const blsp_i2c3_groups[] = {
	"gpio47", "gpio48",
};
static const char * const blsp_spi9_groups[] = {
	"gpio49", "gpio50", "gpio51", "gpio52",
};
static const char * const blsp_uart9_a_groups[] = {
	"gpio49", "gpio50", "gpio51", "gpio52",
};
static const char * const blsp_uim9_a_groups[] = {
	"gpio49", "gpio50",
};
static const char * const blsp10_spi_b_groups[] = {
	"gpio49", "gpio50",
};
static const char * const qdss_cti0_a_groups[] = {
	"gpio49", "gpio50",
};
static const char * const blsp_i2c9_groups[] = {
	"gpio51", "gpio52",
};
static const char * const blsp10_spi_a_groups[] = {
	"gpio51", "gpio52",
};
static const char * const blsp_spi7_groups[] = {
	"gpio53", "gpio54", "gpio55", "gpio56",
};
static const char * const blsp_uart7_a_groups[] = {
	"gpio53", "gpio54", "gpio55", "gpio56",
};
static const char * const blsp_uim7_a_groups[] = {
	"gpio53", "gpio54",
};
static const char * const blsp_i2c7_groups[] = {
	"gpio55", "gpio56",
};
static const char * const qua_mi2s_groups[] = {
	"gpio57", "gpio58", "gpio59", "gpio60", "gpio61", "gpio62", "gpio63",
};
static const char * const blsp10_spi_groups[] = {
	"gpio57",
};
static const char * const gcc_gp1_a_groups[] = {
	"gpio57",
};
static const char * const ssc_irq_groups[] = {
	"gpio58", "gpio59", "gpio60", "gpio61", "gpio62", "gpio63", "gpio78",
	"gpio79", "gpio80", "gpio117", "gpio118", "gpio119", "gpio120",
	"gpio121", "gpio122", "gpio123", "gpio124", "gpio125",
};
static const char * const blsp_spi11_groups[] = {
	"gpio58", "gpio59", "gpio60", "gpio61",
};
static const char * const blsp_uart8_b_groups[] = {
	"gpio58", "gpio59", "gpio60", "gpio61",
};
static const char * const blsp_uim8_b_groups[] = {
	"gpio58", "gpio59",
};
static const char * const gcc_gp2_a_groups[] = {
	"gpio58",
};
static const char * const qdss_cti1_a_groups[] = {
	"gpio58", "gpio59",
};
static const char * const gcc_gp3_a_groups[] = {
	"gpio59",
};
static const char * const blsp_i2c11_groups[] = {
	"gpio60", "gpio61",
};
static const char * const cri_trng0_groups[] = {
	"gpio60",
};
static const char * const cri_trng1_groups[] = {
	"gpio61",
};
static const char * const cri_trng_groups[] = {
	"gpio62",
};
static const char * const pri_mi2s_groups[] = {
	"gpio64", "gpio65", "gpio67", "gpio68",
};
static const char * const sp_cmu_groups[] = {
	"gpio64",
};
static const char * const blsp_spi10_groups[] = {
	"gpio65", "gpio66", "gpio67", "gpio68",
};
static const char * const blsp_uart7_b_groups[] = {
	"gpio65", "gpio66", "gpio67", "gpio68",
};
static const char * const blsp_uim7_b_groups[] = {
	"gpio65", "gpio66",
};
static const char * const pri_mi2s_ws_groups[] = {
	"gpio66",
};
static const char * const blsp_i2c10_groups[] = {
	"gpio67", "gpio68",
};
static const char * const spkr_i2s_groups[] = {
	"gpio69", "gpio70", "gpio71", "gpio72",
};
static const char * const audio_ref_groups[] = {
	"gpio69",
};
static const char * const blsp9_spi_groups[] = {
	"gpio70", "gpio71", "gpio72",
};
static const char * const tsense_pwm1_groups[] = {
	"gpio71",
};
static const char * const tsense_pwm2_groups[] = {
	"gpio71",
};
static const char * const btfm_slimbus_groups[] = {
	"gpio73", "gpio74",
};
static const char * const ter_mi2s_groups[] = {
	"gpio74", "gpio75", "gpio76", "gpio77", "gpio78",
};
static const char * const gcc_gp1_b_groups[] = {
	"gpio78",
};
static const char * const sec_mi2s_groups[] = {
	"gpio79", "gpio80", "gpio81", "gpio82", "gpio83",
};
static const char * const blsp_spi12_groups[] = {
	"gpio81", "gpio82", "gpio83", "gpio84",
};
static const char * const blsp_uart9_b_groups[] = {
	"gpio81", "gpio82", "gpio83", "gpio84",
};
static const char * const blsp_uim9_b_groups[] = {
	"gpio81", "gpio82",
};
static const char * const gcc_gp2_b_groups[] = {
	"gpio81",
};
static const char * const gcc_gp3_b_groups[] = {
	"gpio82",
};
static const char * const blsp_i2c12_groups[] = {
	"gpio83", "gpio84",
};
static const char * const blsp_spi5_groups[] = {
	"gpio85", "gpio86", "gpio87", "gpio88",
};
static const char * const blsp_uart2_b_groups[] = {
	"gpio85", "gpio86", "gpio87", "gpio88",
};
static const char * const blsp_uim2_b_groups[] = {
	"gpio85", "gpio86",
};
static const char * const blsp_i2c5_groups[] = {
	"gpio87", "gpio88",
};
static const char * const tsif0_groups[] = {
	"gpio9", "gpio40", "gpio89", "gpio90", "gpio91",
};
static const char * const mdp_vsync0_groups[] = {
	"gpio90",
};
static const char * const mdp_vsync1_groups[] = {
	"gpio90",
};
static const char * const mdp_vsync2_groups[] = {
	"gpio90",
};
static const char * const mdp_vsync3_groups[] = {
	"gpio90",
};
static const char * const blsp1_spi_groups[] = {
	"gpio90",
};
static const char * const tgu_ch0_groups[] = {
	"gpio90",
};
static const char * const qdss_cti1_b_groups[] = {
	"gpio90", "gpio91",
};
static const char * const sdc4_cmd_groups[] = {
	"gpio91",
};
static const char * const tgu_ch1_groups[] = {
	"gpio91",
};
static const char * const tsif1_groups[] = {
	"gpio92", "gpio93", "gpio94", "gpio95", "gpio96",
};
static const char * const sdc43_groups[] = {
	"gpio92",
};
static const char * const vfr_1_groups[] = {
	"gpio92",
};
static const char * const sdc4_clk_groups[] = {
	"gpio93",
};
static const char * const sdc42_groups[] = {
	"gpio94",
};
static const char * const sd_card_groups[] = {
	"gpio95",
};
static const char * const sdc41_groups[] = {
	"gpio95",
};
static const char * const sdc40_groups[] = {
	"gpio96",
};
static const char * const mdp_vsync_b_groups[] = {
	"gpio97", "gpio98",
};
static const char * const ldo_en_groups[] = {
	"gpio97",
};
static const char * const ldo_update_groups[] = {
	"gpio98",
};
static const char * const blsp_uart8_groups[] = {
	"gpio100", "gpio101",
};
static const char * const blsp11_i2c_groups[] = {
	"gpio102", "gpio103",
};
static const char * const prng_rosc_groups[] = {
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
static const char * const nav_dr_groups[] = {
	"gpio115",
};
static const char * const atest_char_groups[] = {
	"gpio117", "gpio118", "gpio119", "gpio120", "gpio121",
};
static const char * const adsp_ext_groups[] = {
	"gpio118",
};
static const char * const modem_tsync_groups[] = {
	"gpio128",
};
static const char * const nav_pps_groups[] = {
	"gpio128",
};
static const char * const qlink_request_groups[] = {
	"gpio130",
};
static const char * const qlink_enable_groups[] = {
	"gpio131",
};
static const char * const pa_indicator_groups[] = {
	"gpio135",
};
static const char * const ssbi1_groups[] = {
	"gpio142",
};
static const char * const isense_dbg_groups[] = {
	"gpio143",
};
static const char * const mss_lte_groups[] = {
	"gpio144", "gpio145",
};

static const struct pinfunction msm8998_functions[] = {
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(adsp_ext),
	MSM_PIN_FUNCTION(agera_pll),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_gpsadc0),
	MSM_PIN_FUNCTION(atest_gpsadc1),
	MSM_PIN_FUNCTION(atest_tsens),
	MSM_PIN_FUNCTION(atest_tsens2),
	MSM_PIN_FUNCTION(atest_usb1),
	MSM_PIN_FUNCTION(atest_usb10),
	MSM_PIN_FUNCTION(atest_usb11),
	MSM_PIN_FUNCTION(atest_usb12),
	MSM_PIN_FUNCTION(atest_usb13),
	MSM_PIN_FUNCTION(audio_ref),
	MSM_PIN_FUNCTION(bimc_dte0),
	MSM_PIN_FUNCTION(bimc_dte1),
	MSM_PIN_FUNCTION(blsp10_spi),
	MSM_PIN_FUNCTION(blsp10_spi_a),
	MSM_PIN_FUNCTION(blsp10_spi_b),
	MSM_PIN_FUNCTION(blsp11_i2c),
	MSM_PIN_FUNCTION(blsp1_spi),
	MSM_PIN_FUNCTION(blsp1_spi_a),
	MSM_PIN_FUNCTION(blsp1_spi_b),
	MSM_PIN_FUNCTION(blsp2_spi),
	MSM_PIN_FUNCTION(blsp9_spi),
	MSM_PIN_FUNCTION(blsp_i2c1),
	MSM_PIN_FUNCTION(blsp_i2c2),
	MSM_PIN_FUNCTION(blsp_i2c3),
	MSM_PIN_FUNCTION(blsp_i2c4),
	MSM_PIN_FUNCTION(blsp_i2c5),
	MSM_PIN_FUNCTION(blsp_i2c6),
	MSM_PIN_FUNCTION(blsp_i2c7),
	MSM_PIN_FUNCTION(blsp_i2c8),
	MSM_PIN_FUNCTION(blsp_i2c9),
	MSM_PIN_FUNCTION(blsp_i2c10),
	MSM_PIN_FUNCTION(blsp_i2c11),
	MSM_PIN_FUNCTION(blsp_i2c12),
	MSM_PIN_FUNCTION(blsp_spi1),
	MSM_PIN_FUNCTION(blsp_spi2),
	MSM_PIN_FUNCTION(blsp_spi3),
	MSM_PIN_FUNCTION(blsp_spi4),
	MSM_PIN_FUNCTION(blsp_spi5),
	MSM_PIN_FUNCTION(blsp_spi6),
	MSM_PIN_FUNCTION(blsp_spi7),
	MSM_PIN_FUNCTION(blsp_spi8),
	MSM_PIN_FUNCTION(blsp_spi9),
	MSM_PIN_FUNCTION(blsp_spi10),
	MSM_PIN_FUNCTION(blsp_spi11),
	MSM_PIN_FUNCTION(blsp_spi12),
	MSM_PIN_FUNCTION(blsp_uart1_a),
	MSM_PIN_FUNCTION(blsp_uart1_b),
	MSM_PIN_FUNCTION(blsp_uart2_a),
	MSM_PIN_FUNCTION(blsp_uart2_b),
	MSM_PIN_FUNCTION(blsp_uart3_a),
	MSM_PIN_FUNCTION(blsp_uart3_b),
	MSM_PIN_FUNCTION(blsp_uart7_a),
	MSM_PIN_FUNCTION(blsp_uart7_b),
	MSM_PIN_FUNCTION(blsp_uart8),
	MSM_PIN_FUNCTION(blsp_uart8_a),
	MSM_PIN_FUNCTION(blsp_uart8_b),
	MSM_PIN_FUNCTION(blsp_uart9_a),
	MSM_PIN_FUNCTION(blsp_uart9_b),
	MSM_PIN_FUNCTION(blsp_uim1_a),
	MSM_PIN_FUNCTION(blsp_uim1_b),
	MSM_PIN_FUNCTION(blsp_uim2_a),
	MSM_PIN_FUNCTION(blsp_uim2_b),
	MSM_PIN_FUNCTION(blsp_uim3_a),
	MSM_PIN_FUNCTION(blsp_uim3_b),
	MSM_PIN_FUNCTION(blsp_uim7_a),
	MSM_PIN_FUNCTION(blsp_uim7_b),
	MSM_PIN_FUNCTION(blsp_uim8_a),
	MSM_PIN_FUNCTION(blsp_uim8_b),
	MSM_PIN_FUNCTION(blsp_uim9_a),
	MSM_PIN_FUNCTION(blsp_uim9_b),
	MSM_PIN_FUNCTION(bt_reset),
	MSM_PIN_FUNCTION(btfm_slimbus),
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
	MSM_PIN_FUNCTION(edp_hot),
	MSM_PIN_FUNCTION(edp_lcd),
	MSM_PIN_FUNCTION(gcc_gp1_a),
	MSM_PIN_FUNCTION(gcc_gp1_b),
	MSM_PIN_FUNCTION(gcc_gp2_a),
	MSM_PIN_FUNCTION(gcc_gp2_b),
	MSM_PIN_FUNCTION(gcc_gp3_a),
	MSM_PIN_FUNCTION(gcc_gp3_b),
	MSM_PIN_FUNCTION(hdmi_cec),
	MSM_PIN_FUNCTION(hdmi_ddc),
	MSM_PIN_FUNCTION(hdmi_hot),
	MSM_PIN_FUNCTION(hdmi_rcv),
	MSM_PIN_FUNCTION(isense_dbg),
	MSM_PIN_FUNCTION(jitter_bist),
	MSM_PIN_FUNCTION(ldo_en),
	MSM_PIN_FUNCTION(ldo_update),
	MSM_PIN_FUNCTION(lpass_slimbus),
	MSM_PIN_FUNCTION(m_voc),
	MSM_PIN_FUNCTION(mdp_vsync),
	MSM_PIN_FUNCTION(mdp_vsync0),
	MSM_PIN_FUNCTION(mdp_vsync1),
	MSM_PIN_FUNCTION(mdp_vsync2),
	MSM_PIN_FUNCTION(mdp_vsync3),
	MSM_PIN_FUNCTION(mdp_vsync_a),
	MSM_PIN_FUNCTION(mdp_vsync_b),
	MSM_PIN_FUNCTION(modem_tsync),
	MSM_PIN_FUNCTION(mss_lte),
	MSM_PIN_FUNCTION(nav_dr),
	MSM_PIN_FUNCTION(nav_pps),
	MSM_PIN_FUNCTION(pa_indicator),
	MSM_PIN_FUNCTION(pci_e0),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(pll_bypassnl),
	MSM_PIN_FUNCTION(pll_reset),
	MSM_PIN_FUNCTION(pri_mi2s),
	MSM_PIN_FUNCTION(pri_mi2s_ws),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(pwr_crypto),
	MSM_PIN_FUNCTION(pwr_modem),
	MSM_PIN_FUNCTION(pwr_nav),
	MSM_PIN_FUNCTION(qdss_cti0_a),
	MSM_PIN_FUNCTION(qdss_cti0_b),
	MSM_PIN_FUNCTION(qdss_cti1_a),
	MSM_PIN_FUNCTION(qdss_cti1_b),
	MSM_PIN_FUNCTION(qdss),
	MSM_PIN_FUNCTION(qlink_enable),
	MSM_PIN_FUNCTION(qlink_request),
	MSM_PIN_FUNCTION(qua_mi2s),
	MSM_PIN_FUNCTION(sd_card),
	MSM_PIN_FUNCTION(sd_write),
	MSM_PIN_FUNCTION(sdc40),
	MSM_PIN_FUNCTION(sdc41),
	MSM_PIN_FUNCTION(sdc42),
	MSM_PIN_FUNCTION(sdc43),
	MSM_PIN_FUNCTION(sdc4_clk),
	MSM_PIN_FUNCTION(sdc4_cmd),
	MSM_PIN_FUNCTION(sec_mi2s),
	MSM_PIN_FUNCTION(sp_cmu),
	MSM_PIN_FUNCTION(spkr_i2s),
	MSM_PIN_FUNCTION(ssbi1),
	MSM_PIN_FUNCTION(ssc_irq),
	MSM_PIN_FUNCTION(ter_mi2s),
	MSM_PIN_FUNCTION(tgu_ch0),
	MSM_PIN_FUNCTION(tgu_ch1),
	MSM_PIN_FUNCTION(tsense_pwm1),
	MSM_PIN_FUNCTION(tsense_pwm2),
	MSM_PIN_FUNCTION(tsif0),
	MSM_PIN_FUNCTION(tsif1),
	MSM_PIN_FUNCTION(uim1_clk),
	MSM_PIN_FUNCTION(uim1_data),
	MSM_PIN_FUNCTION(uim1_present),
	MSM_PIN_FUNCTION(uim1_reset),
	MSM_PIN_FUNCTION(uim2_clk),
	MSM_PIN_FUNCTION(uim2_data),
	MSM_PIN_FUNCTION(uim2_present),
	MSM_PIN_FUNCTION(uim2_reset),
	MSM_PIN_FUNCTION(uim_batt),
	MSM_PIN_FUNCTION(usb_phy),
	MSM_PIN_FUNCTION(vfr_1),
	MSM_PIN_FUNCTION(vsense_clkout),
	MSM_PIN_FUNCTION(vsense_data0),
	MSM_PIN_FUNCTION(vsense_data1),
	MSM_PIN_FUNCTION(vsense_mode),
	MSM_PIN_FUNCTION(wlan1_adc0),
	MSM_PIN_FUNCTION(wlan1_adc1),
	MSM_PIN_FUNCTION(wlan2_adc0),
	MSM_PIN_FUNCTION(wlan2_adc1),
};

static const struct msm_pingroup msm8998_groups[] = {
	PINGROUP(0, EAST, blsp_spi1, blsp_uart1_a, blsp_uim1_a, _, _, _, _, _, _),
	PINGROUP(1, EAST, blsp_spi1, blsp_uart1_a, blsp_uim1_a, _, _, _, _, _, _),
	PINGROUP(2, EAST, blsp_spi1, blsp_uart1_a, blsp_i2c1, _, _, _, _, _, _),
	PINGROUP(3, EAST, blsp_spi1, blsp_uart1_a, blsp_i2c1, _, _, _, _, _, _),
	PINGROUP(4, WEST, blsp_spi8, blsp_uart8_a, blsp_uim8_a, _, qdss_cti0_b, _, _, _, _),
	PINGROUP(5, WEST, blsp_spi8, blsp_uart8_a, blsp_uim8_a, _, qdss_cti0_b, _, _, _, _),
	PINGROUP(6, WEST, blsp_spi8, blsp_uart8_a, blsp_i2c8, _, _, _, _, _, _),
	PINGROUP(7, WEST, blsp_spi8, blsp_uart8_a, blsp_i2c8, ddr_bist, _, atest_tsens2, atest_usb1, _, _),
	PINGROUP(8, EAST, blsp_spi4, blsp_uart1_b, blsp_uim1_b, _, ddr_bist, _, wlan1_adc1, atest_usb13, bimc_dte1),
	PINGROUP(9, EAST, blsp_spi4, blsp_uart1_b, blsp_uim1_b, tsif0, ddr_bist, _, wlan1_adc0, atest_usb12, bimc_dte0),
	PINGROUP(10, EAST, mdp_vsync_a, blsp_spi4, blsp_uart1_b, blsp_i2c4, ddr_bist, atest_gpsadc1, wlan2_adc1, atest_usb11, bimc_dte1),
	PINGROUP(11, EAST, mdp_vsync_a, edp_lcd, blsp_spi4, blsp_uart1_b, blsp_i2c4, dbg_out, atest_gpsadc0, wlan2_adc0, atest_usb10),
	PINGROUP(12, EAST, mdp_vsync, m_voc, _, _, _, _, _, _, _),
	PINGROUP(13, EAST, cam_mclk, pll_bypassnl, qdss, _, _, _, _, _, _),
	PINGROUP(14, EAST, cam_mclk, pll_reset, qdss, _, _, _, _, _, _),
	PINGROUP(15, EAST, cam_mclk, qdss, _, _, _, _, _, _, _),
	PINGROUP(16, EAST, cam_mclk, qdss, _, _, _, _, _, _, _),
	PINGROUP(17, EAST, cci_i2c, qdss, _, _, _, _, _, _, _),
	PINGROUP(18, EAST, cci_i2c, phase_flag, qdss, _, _, _, _, _, _),
	PINGROUP(19, EAST, cci_i2c, phase_flag, qdss, _, _, _, _, _, _),
	PINGROUP(20, EAST, cci_i2c, qdss, _, _, _, _, _, _, _),
	PINGROUP(21, EAST, cci_timer0, _, qdss, vsense_data0, _, _, _, _, _),
	PINGROUP(22, EAST, cci_timer1, _, qdss, vsense_data1, _, _, _, _, _),
	PINGROUP(23, EAST, cci_timer2, blsp1_spi_b, qdss, vsense_mode, _, _, _, _, _),
	PINGROUP(24, EAST, cci_timer3, cci_async, blsp1_spi_a, _, qdss, vsense_clkout, _, _, _),
	PINGROUP(25, EAST, cci_timer4, cci_async, blsp2_spi, _, qdss, _, _, _, _),
	PINGROUP(26, EAST, cci_async, qdss, _, _, _, _, _, _, _),
	PINGROUP(27, EAST, blsp1_spi_a, qdss, _, _, _, _, _, _, _),
	PINGROUP(28, EAST, blsp1_spi_b, qdss, _, _, _, _, _, _, _),
	PINGROUP(29, EAST, blsp2_spi, _, qdss, _, _, _, _, _, _),
	PINGROUP(30, EAST, hdmi_rcv, blsp2_spi, qdss, _, _, _, _, _, _),
	PINGROUP(31, EAST, hdmi_cec, blsp_spi2, blsp_uart2_a, blsp_uim2_a, pwr_modem, _, _, _, _),
	PINGROUP(32, EAST, hdmi_ddc, blsp_spi2, blsp_uart2_a, blsp_i2c2, pwr_nav, _, _, _, _),
	PINGROUP(33, EAST, hdmi_ddc, blsp_spi2, blsp_uart2_a, blsp_i2c2, pwr_crypto, _, _, _, _),
	PINGROUP(34, EAST, hdmi_hot, edp_hot, blsp_spi2, blsp_uart2_a, blsp_uim2_a, _, _, _, _),
	PINGROUP(35, NORTH, pci_e0, jitter_bist, _, _, _, _, _, _, _),
	PINGROUP(36, NORTH, pci_e0, agera_pll, _, atest_tsens, _, _, _, _, _),
	PINGROUP(37, NORTH, agera_pll, _, _, _, _, _, _, _, _),
	PINGROUP(38, WEST, usb_phy, _, _, _, _, _, _, _, _),
	PINGROUP(39, WEST, lpass_slimbus, _, _, _, _, _, _, _, _),
	PINGROUP(40, EAST, sd_write, tsif0, _, _, _, _, _, _, _),
	PINGROUP(41, EAST, blsp_spi6, blsp_uart3_b, blsp_uim3_b, _, qdss, _, _, _, _),
	PINGROUP(42, EAST, blsp_spi6, blsp_uart3_b, blsp_uim3_b, _, qdss, _, _, _, _),
	PINGROUP(43, EAST, blsp_spi6, blsp_uart3_b, blsp_i2c6, _, qdss, _, _, _, _),
	PINGROUP(44, EAST, blsp_spi6, blsp_uart3_b, blsp_i2c6, _, qdss, _, _, _, _),
	PINGROUP(45, EAST, blsp_spi3, blsp_uart3_a, blsp_uim3_a, _, _, _, _, _, _),
	PINGROUP(46, EAST, blsp_spi3, blsp_uart3_a, blsp_uim3_a, _, _, _, _, _, _),
	PINGROUP(47, EAST, blsp_spi3, blsp_uart3_a, blsp_i2c3, _, _, _, _, _, _),
	PINGROUP(48, EAST, blsp_spi3, blsp_uart3_a, blsp_i2c3, _, _, _, _, _, _),
	PINGROUP(49, NORTH, blsp_spi9, blsp_uart9_a, blsp_uim9_a, blsp10_spi_b, qdss_cti0_a, _, _, _, _),
	PINGROUP(50, NORTH, blsp_spi9, blsp_uart9_a, blsp_uim9_a, blsp10_spi_b, qdss_cti0_a, _, _, _, _),
	PINGROUP(51, NORTH, blsp_spi9, blsp_uart9_a, blsp_i2c9, blsp10_spi_a, _, _, _, _, _),
	PINGROUP(52, NORTH, blsp_spi9, blsp_uart9_a, blsp_i2c9, blsp10_spi_a, _, _, _, _, _),
	PINGROUP(53, WEST, blsp_spi7, blsp_uart7_a, blsp_uim7_a, _, _, _, _, _, _),
	PINGROUP(54, WEST, blsp_spi7, blsp_uart7_a, blsp_uim7_a, _, _, _, _, _, _),
	PINGROUP(55, WEST, blsp_spi7, blsp_uart7_a, blsp_i2c7, _, _, _, _, _, _),
	PINGROUP(56, WEST, blsp_spi7, blsp_uart7_a, blsp_i2c7, _, _, _, _, _, _),
	PINGROUP(57, WEST, qua_mi2s, blsp10_spi, gcc_gp1_a, _, _, _, _, _, _),
	PINGROUP(58, WEST, qua_mi2s, blsp_spi11, blsp_uart8_b, blsp_uim8_b, gcc_gp2_a, _, qdss_cti1_a, _, _),
	PINGROUP(59, WEST, qua_mi2s, blsp_spi11, blsp_uart8_b, blsp_uim8_b, gcc_gp3_a, _, qdss_cti1_a, _, _),
	PINGROUP(60, WEST, qua_mi2s, blsp_spi11, blsp_uart8_b, blsp_i2c11, cri_trng0, _, _, _, _),
	PINGROUP(61, WEST, qua_mi2s, blsp_spi11, blsp_uart8_b, blsp_i2c11, cri_trng1, _, _, _, _),
	PINGROUP(62, WEST, qua_mi2s, cri_trng, _, _, _, _, _, _, _),
	PINGROUP(63, WEST, qua_mi2s, _, _, _, _, _, _, _, _),
	PINGROUP(64, WEST, pri_mi2s, sp_cmu, _, _, _, _, _, _, _),
	PINGROUP(65, WEST, pri_mi2s, blsp_spi10, blsp_uart7_b, blsp_uim7_b, _, _, _, _, _),
	PINGROUP(66, WEST, pri_mi2s_ws, blsp_spi10, blsp_uart7_b, blsp_uim7_b, _, _, _, _, _),
	PINGROUP(67, WEST, pri_mi2s, blsp_spi10, blsp_uart7_b, blsp_i2c10, _, _, _, _, _),
	PINGROUP(68, WEST, pri_mi2s, blsp_spi10, blsp_uart7_b, blsp_i2c10, _, _, _, _, _),
	PINGROUP(69, WEST, spkr_i2s, audio_ref, _, _, _, _, _, _, _),
	PINGROUP(70, WEST, lpass_slimbus, spkr_i2s, blsp9_spi, _, _, _, _, _, _),
	PINGROUP(71, WEST, lpass_slimbus, spkr_i2s, blsp9_spi, tsense_pwm1, tsense_pwm2, _, _, _, _),
	PINGROUP(72, WEST, lpass_slimbus, spkr_i2s, blsp9_spi, _, _, _, _, _, _),
	PINGROUP(73, WEST, btfm_slimbus, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(74, WEST, btfm_slimbus, ter_mi2s, phase_flag, _, _, _, _, _, _),
	PINGROUP(75, WEST, ter_mi2s, phase_flag, qdss, _, _, _, _, _, _),
	PINGROUP(76, WEST, ter_mi2s, phase_flag, qdss, _, _, _, _, _, _),
	PINGROUP(77, WEST, ter_mi2s, phase_flag, qdss, _, _, _, _, _, _),
	PINGROUP(78, WEST, ter_mi2s, gcc_gp1_b, _, _, _, _, _, _, _),
	PINGROUP(79, WEST, sec_mi2s, _, qdss, _, _, _, _, _, _),
	PINGROUP(80, WEST, sec_mi2s, _, qdss, _, _, _, _, _, _),
	PINGROUP(81, WEST, sec_mi2s, blsp_spi12, blsp_uart9_b, blsp_uim9_b, gcc_gp2_b, _, _, _, _),
	PINGROUP(82, WEST, sec_mi2s, blsp_spi12, blsp_uart9_b, blsp_uim9_b, gcc_gp3_b, _, _, _, _),
	PINGROUP(83, WEST, sec_mi2s, blsp_spi12, blsp_uart9_b, blsp_i2c12, _, _, _, _, _),
	PINGROUP(84, WEST, blsp_spi12, blsp_uart9_b, blsp_i2c12, _, _, _, _, _, _),
	PINGROUP(85, EAST, blsp_spi5, blsp_uart2_b, blsp_uim2_b, _, _, _, _, _, _),
	PINGROUP(86, EAST, blsp_spi5, blsp_uart2_b, blsp_uim2_b, _, _, _, _, _, _),
	PINGROUP(87, EAST, blsp_spi5, blsp_uart2_b, blsp_i2c5, _, _, _, _, _, _),
	PINGROUP(88, EAST, blsp_spi5, blsp_uart2_b, blsp_i2c5, _, _, _, _, _, _),
	PINGROUP(89, EAST, tsif0, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(90, EAST, tsif0, mdp_vsync0, mdp_vsync1, mdp_vsync2, mdp_vsync3, blsp1_spi, tgu_ch0, qdss_cti1_b, _),
	PINGROUP(91, EAST, tsif0, sdc4_cmd, tgu_ch1, phase_flag, qdss_cti1_b, _, _, _, _),
	PINGROUP(92, EAST, tsif1, sdc43, vfr_1, phase_flag, _, _, _, _, _),
	PINGROUP(93, EAST, tsif1, sdc4_clk, _, qdss, _, _, _, _, _),
	PINGROUP(94, EAST, tsif1, sdc42, _, _, _, _, _, _, _),
	PINGROUP(95, EAST, tsif1, sdc41, _, _, _, _, _, _, _),
	PINGROUP(96, EAST, tsif1, sdc40, phase_flag, _, _, _, _, _, _),
	PINGROUP(97, WEST, _, mdp_vsync_b, ldo_en, _, _, _, _, _, _),
	PINGROUP(98, WEST, _, mdp_vsync_b, ldo_update, _, _, _, _, _, _),
	PINGROUP(99, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(100, WEST, _, _, blsp_uart8, _, _, _, _, _, _),
	PINGROUP(101, WEST, _, blsp_uart8, _, _, _, _, _, _, _),
	PINGROUP(102, WEST, _, blsp11_i2c, prng_rosc, _, _, _, _, _, _),
	PINGROUP(103, WEST, _, blsp11_i2c, phase_flag, _, _, _, _, _, _),
	PINGROUP(104, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(105, NORTH, uim2_data, _, _, _, _, _, _, _, _),
	PINGROUP(106, NORTH, uim2_clk, _, _, _, _, _, _, _, _),
	PINGROUP(107, NORTH, uim2_reset, _, _, _, _, _, _, _, _),
	PINGROUP(108, NORTH, uim2_present, _, _, _, _, _, _, _, _),
	PINGROUP(109, NORTH, uim1_data, _, _, _, _, _, _, _, _),
	PINGROUP(110, NORTH, uim1_clk, _, _, _, _, _, _, _, _),
	PINGROUP(111, NORTH, uim1_reset, _, _, _, _, _, _, _, _),
	PINGROUP(112, NORTH, uim1_present, _, _, _, _, _, _, _, _),
	PINGROUP(113, NORTH, uim_batt, _, _, _, _, _, _, _, _),
	PINGROUP(114, WEST, _, _, phase_flag, _, _, _, _, _, _),
	PINGROUP(115, WEST, _, nav_dr, phase_flag, _, _, _, _, _, _),
	PINGROUP(116, WEST, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(117, EAST, phase_flag, qdss, atest_char, _, _, _, _, _, _),
	PINGROUP(118, EAST, adsp_ext, phase_flag, qdss, atest_char, _, _, _, _, _),
	PINGROUP(119, EAST, phase_flag, qdss, atest_char, _, _, _, _, _, _),
	PINGROUP(120, EAST, phase_flag, qdss, atest_char, _, _, _, _, _, _),
	PINGROUP(121, EAST, phase_flag, qdss, atest_char, _, _, _, _, _, _),
	PINGROUP(122, EAST, phase_flag, qdss, _, _, _, _, _, _, _),
	PINGROUP(123, EAST, phase_flag, qdss, _, _, _, _, _, _, _),
	PINGROUP(124, EAST, phase_flag, qdss, _, _, _, _, _, _, _),
	PINGROUP(125, EAST, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(126, EAST, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(127, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(128, WEST, modem_tsync, nav_pps, phase_flag, _, _, _, _, _, _),
	PINGROUP(129, WEST, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(130, NORTH, qlink_request, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(131, NORTH, qlink_enable, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(132, WEST, _, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(133, WEST, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(134, WEST, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(135, WEST, _, pa_indicator, _, _, _, _, _, _, _),
	PINGROUP(136, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(137, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(138, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(139, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(140, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(141, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(142, WEST, _, ssbi1, _, _, _, _, _, _, _),
	PINGROUP(143, WEST, isense_dbg, _, _, _, _, _, _, _, _),
	PINGROUP(144, WEST, mss_lte, _, _, _, _, _, _, _, _),
	PINGROUP(145, WEST, mss_lte, _, _, _, _, _, _, _, _),
	PINGROUP(146, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(147, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(148, WEST, _, _, _, _, _, _, _, _, _),
	PINGROUP(149, WEST, _, _, _, _, _, _, _, _, _),
	SDC_QDSD_PINGROUP(sdc2_clk, 0x999000, 14, 6),
	SDC_QDSD_PINGROUP(sdc2_cmd, 0x999000, 11, 3),
	SDC_QDSD_PINGROUP(sdc2_data, 0x999000, 9, 0),
	UFS_RESET(ufs_reset, 0x19d000),
};

static const struct msm_gpio_wakeirq_map msm8998_mpm_map[] = {
	{ 1, 3 }, { 5, 4 }, { 9, 5 }, { 11, 6 }, { 22, 8 }, { 24, 9 }, { 26, 10 },
	{ 34, 11 }, { 36, 12 }, { 37, 13 }, { 38, 14 }, { 40, 15 }, { 42, 16 }, { 46, 17 },
	{ 50, 18 }, { 53, 19 }, { 54, 20 }, { 56, 21 }, { 57, 22 }, { 58, 23 }, { 59, 24 },
	{ 60, 25 }, { 61, 26 }, { 62, 27 }, { 63, 28 }, { 64, 29 }, { 66, 7 }, { 71, 30 },
	{ 73, 31 }, { 77, 32 }, { 78, 33 }, { 79, 34 }, { 80, 35 }, { 82, 36 }, { 86, 37 },
	{ 91, 38 }, { 92, 39 }, { 95, 40 }, { 97, 41 }, { 101, 42 }, { 104, 43 }, { 106, 44 },
	{ 108, 45 }, { 110, 48 }, { 112, 46 }, { 113, 47 }, { 115, 51 }, { 116, 54 }, { 117, 55 },
	{ 118, 56 }, { 119, 57 }, { 120, 58 }, { 121, 59 }, { 122, 60 }, { 123, 61 }, { 124, 62 },
	{ 125, 63 }, { 126, 64 }, { 127, 50 }, { 129, 65 }, { 131, 66 }, { 132, 67 }, { 133, 68 },
};

static const struct msm_pinctrl_soc_data msm8998_pinctrl = {
	.pins = msm8998_pins,
	.npins = ARRAY_SIZE(msm8998_pins),
	.functions = msm8998_functions,
	.nfunctions = ARRAY_SIZE(msm8998_functions),
	.groups = msm8998_groups,
	.ngroups = ARRAY_SIZE(msm8998_groups),
	.ngpios = 150,
	.wakeirq_map = msm8998_mpm_map,
	.nwakeirq_map = ARRAY_SIZE(msm8998_mpm_map),
};

static int msm8998_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &msm8998_pinctrl);
}

static const struct of_device_id msm8998_pinctrl_of_match[] = {
	{ .compatible = "qcom,msm8998-pinctrl", },
	{ },
};

static struct platform_driver msm8998_pinctrl_driver = {
	.driver = {
		.name = "msm8998-pinctrl",
		.of_match_table = msm8998_pinctrl_of_match,
	},
	.probe = msm8998_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init msm8998_pinctrl_init(void)
{
	return platform_driver_register(&msm8998_pinctrl_driver);
}
arch_initcall(msm8998_pinctrl_init);

static void __exit msm8998_pinctrl_exit(void)
{
	platform_driver_unregister(&msm8998_pinctrl_driver);
}
module_exit(msm8998_pinctrl_exit);

MODULE_DESCRIPTION("QTI msm8998 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, msm8998_pinctrl_of_match);
