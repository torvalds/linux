// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define REG_BASE 0x0
#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
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
		.ctl_reg = REG_BASE + REG_SIZE * id,	\
		.io_reg = REG_BASE + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = REG_BASE + 0x8 + REG_SIZE * id,		\
		.intr_status_reg = REG_BASE + 0xc + REG_SIZE * id,	\
		.intr_target_reg = REG_BASE + 0x8 + REG_SIZE * id,	\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
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
static const struct pinctrl_pin_desc msm8996_pins[] = {
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
	PINCTRL_PIN(150, "SDC1_CLK"),
	PINCTRL_PIN(151, "SDC1_CMD"),
	PINCTRL_PIN(152, "SDC1_DATA"),
	PINCTRL_PIN(153, "SDC2_CLK"),
	PINCTRL_PIN(154, "SDC2_CMD"),
	PINCTRL_PIN(155, "SDC2_DATA"),
	PINCTRL_PIN(156, "SDC1_RCLK"),
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

static const unsigned int sdc1_clk_pins[] = { 150 };
static const unsigned int sdc1_cmd_pins[] = { 151 };
static const unsigned int sdc1_data_pins[] = { 152 };
static const unsigned int sdc2_clk_pins[] = { 153 };
static const unsigned int sdc2_cmd_pins[] = { 154 };
static const unsigned int sdc2_data_pins[] = { 155 };
static const unsigned int sdc1_rclk_pins[] = { 156 };

enum msm8996_functions {
	msm_mux_adsp_ext,
	msm_mux_atest_bbrx0,
	msm_mux_atest_bbrx1,
	msm_mux_atest_char,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_atest_gpsadc0,
	msm_mux_atest_gpsadc1,
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
	msm_mux_bimc_dte0,
	msm_mux_bimc_dte1,
	msm_mux_blsp10_spi,
	msm_mux_blsp11_i2c_scl_b,
	msm_mux_blsp11_i2c_sda_b,
	msm_mux_blsp11_uart_rx_b,
	msm_mux_blsp11_uart_tx_b,
	msm_mux_blsp1_spi,
	msm_mux_blsp2_spi,
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
	msm_mux_blsp_uart1,
	msm_mux_blsp_uart10,
	msm_mux_blsp_uart11,
	msm_mux_blsp_uart12,
	msm_mux_blsp_uart2,
	msm_mux_blsp_uart3,
	msm_mux_blsp_uart4,
	msm_mux_blsp_uart5,
	msm_mux_blsp_uart6,
	msm_mux_blsp_uart7,
	msm_mux_blsp_uart8,
	msm_mux_blsp_uart9,
	msm_mux_blsp_uim1,
	msm_mux_blsp_uim10,
	msm_mux_blsp_uim11,
	msm_mux_blsp_uim12,
	msm_mux_blsp_uim2,
	msm_mux_blsp_uim3,
	msm_mux_blsp_uim4,
	msm_mux_blsp_uim5,
	msm_mux_blsp_uim6,
	msm_mux_blsp_uim7,
	msm_mux_blsp_uim8,
	msm_mux_blsp_uim9,
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
	msm_mux_dac_calib0,
	msm_mux_dac_calib1,
	msm_mux_dac_calib10,
	msm_mux_dac_calib11,
	msm_mux_dac_calib12,
	msm_mux_dac_calib13,
	msm_mux_dac_calib14,
	msm_mux_dac_calib15,
	msm_mux_dac_calib16,
	msm_mux_dac_calib17,
	msm_mux_dac_calib18,
	msm_mux_dac_calib19,
	msm_mux_dac_calib2,
	msm_mux_dac_calib20,
	msm_mux_dac_calib21,
	msm_mux_dac_calib22,
	msm_mux_dac_calib23,
	msm_mux_dac_calib24,
	msm_mux_dac_calib25,
	msm_mux_dac_calib26,
	msm_mux_dac_calib3,
	msm_mux_dac_calib4,
	msm_mux_dac_calib5,
	msm_mux_dac_calib6,
	msm_mux_dac_calib7,
	msm_mux_dac_calib8,
	msm_mux_dac_calib9,
	msm_mux_dac_gpio,
	msm_mux_dbg_out,
	msm_mux_ddr_bist,
	msm_mux_edp_hot,
	msm_mux_edp_lcd,
	msm_mux_gcc_gp1_clk_a,
	msm_mux_gcc_gp1_clk_b,
	msm_mux_gcc_gp2_clk_a,
	msm_mux_gcc_gp2_clk_b,
	msm_mux_gcc_gp3_clk_a,
	msm_mux_gcc_gp3_clk_b,
	msm_mux_gsm_tx,
	msm_mux_hdmi_cec,
	msm_mux_hdmi_ddc,
	msm_mux_hdmi_hot,
	msm_mux_hdmi_rcv,
	msm_mux_isense_dbg,
	msm_mux_ldo_en,
	msm_mux_ldo_update,
	msm_mux_lpass_slimbus,
	msm_mux_m_voc,
	msm_mux_mdp_vsync,
	msm_mux_mdp_vsync_p_b,
	msm_mux_mdp_vsync_s_b,
	msm_mux_modem_tsync,
	msm_mux_mss_lte,
	msm_mux_nav_dr,
	msm_mux_nav_pps,
	msm_mux_pa_indicator,
	msm_mux_pci_e0,
	msm_mux_pci_e1,
	msm_mux_pci_e2,
	msm_mux_pll_bypassnl,
	msm_mux_pll_reset,
	msm_mux_pri_mi2s,
	msm_mux_prng_rosc,
	msm_mux_pwr_crypto,
	msm_mux_pwr_modem,
	msm_mux_pwr_nav,
	msm_mux_qdss_cti,
	msm_mux_qdss_cti_trig_in_a,
	msm_mux_qdss_cti_trig_in_b,
	msm_mux_qdss_cti_trig_out_a,
	msm_mux_qdss_cti_trig_out_b,
	msm_mux_qdss_stm0,
	msm_mux_qdss_stm1,
	msm_mux_qdss_stm10,
	msm_mux_qdss_stm11,
	msm_mux_qdss_stm12,
	msm_mux_qdss_stm13,
	msm_mux_qdss_stm14,
	msm_mux_qdss_stm15,
	msm_mux_qdss_stm16,
	msm_mux_qdss_stm17,
	msm_mux_qdss_stm18,
	msm_mux_qdss_stm19,
	msm_mux_qdss_stm2,
	msm_mux_qdss_stm20,
	msm_mux_qdss_stm21,
	msm_mux_qdss_stm22,
	msm_mux_qdss_stm23,
	msm_mux_qdss_stm24,
	msm_mux_qdss_stm25,
	msm_mux_qdss_stm26,
	msm_mux_qdss_stm27,
	msm_mux_qdss_stm28,
	msm_mux_qdss_stm29,
	msm_mux_qdss_stm3,
	msm_mux_qdss_stm30,
	msm_mux_qdss_stm31,
	msm_mux_qdss_stm4,
	msm_mux_qdss_stm5,
	msm_mux_qdss_stm6,
	msm_mux_qdss_stm7,
	msm_mux_qdss_stm8,
	msm_mux_qdss_stm9,
	msm_mux_qdss_traceclk_a,
	msm_mux_qdss_traceclk_b,
	msm_mux_qdss_tracectl_a,
	msm_mux_qdss_tracectl_b,
	msm_mux_qdss_tracedata_11,
	msm_mux_qdss_tracedata_12,
	msm_mux_qdss_tracedata_a,
	msm_mux_qdss_tracedata_b,
	msm_mux_qspi0,
	msm_mux_qspi1,
	msm_mux_qspi2,
	msm_mux_qspi3,
	msm_mux_qspi_clk,
	msm_mux_qspi_cs,
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
	msm_mux_spkr_i2s,
	msm_mux_ssbi1,
	msm_mux_ssbi2,
	msm_mux_ssc_irq,
	msm_mux_ter_mi2s,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_tsif1_clk,
	msm_mux_tsif1_data,
	msm_mux_tsif1_en,
	msm_mux_tsif1_error,
	msm_mux_tsif1_sync,
	msm_mux_tsif2_clk,
	msm_mux_tsif2_data,
	msm_mux_tsif2_en,
	msm_mux_tsif2_error,
	msm_mux_tsif2_sync,
	msm_mux_uim1,
	msm_mux_uim2,
	msm_mux_uim3,
	msm_mux_uim4,
	msm_mux_uim_batt,
	msm_mux_vfr_1,
	msm_mux_gpio,
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
	"gpio147", "gpio148", "gpio149"
};


static const char * const blsp_uart1_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const blsp_spi1_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const blsp_i2c1_groups[] = {
	"gpio2", "gpio3",
};
static const char * const blsp_uim1_groups[] = {
	"gpio0", "gpio1",
};
static const char * const atest_tsens_groups[] = {
	"gpio3",
};
static const char * const bimc_dte1_groups[] = {
	"gpio3", "gpio5",
};
static const char * const blsp_spi8_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const blsp_uart8_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const blsp_uim8_groups[] = {
	"gpio4", "gpio5",
};
static const char * const qdss_cti_trig_out_b_groups[] = {
	"gpio4",
};
static const char * const dac_calib0_groups[] = {
	"gpio4", "gpio41",
};
static const char * const bimc_dte0_groups[] = {
	"gpio4", "gpio6",
};
static const char * const qdss_cti_trig_in_b_groups[] = {
	"gpio5",
};
static const char * const dac_calib1_groups[] = {
	"gpio5", "gpio42",
};
static const char * const dac_calib2_groups[] = {
	"gpio6", "gpio43",
};
static const char * const atest_tsens2_groups[] = {
	"gpio7",
};
static const char * const blsp_spi10_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};
static const char * const blsp_uart10_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};
static const char * const blsp_uim10_groups[] = {
	"gpio8", "gpio9",
};
static const char * const atest_bbrx1_groups[] = {
	"gpio8",
};
static const char * const atest_usb12_groups[] = {
	"gpio9",
};
static const char * const mdp_vsync_groups[] = {
	"gpio10", "gpio11", "gpio12",
};
static const char * const edp_lcd_groups[] = {
	"gpio10",
};
static const char * const blsp_i2c10_groups[] = {
	"gpio10", "gpio11",
};
static const char * const atest_usb11_groups[] = {
	"gpio10",
};
static const char * const atest_gpsadc0_groups[] = {
	"gpio11",
};
static const char * const edp_hot_groups[] = {
	"gpio11",
};
static const char * const atest_usb10_groups[] = {
	"gpio11",
};
static const char * const m_voc_groups[] = {
	"gpio12",
};
static const char * const dac_gpio_groups[] = {
	"gpio12",
};
static const char * const atest_char_groups[] = {
	"gpio12",
};
static const char * const cam_mclk_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16",
};
static const char * const pll_bypassnl_groups[] = {
	"gpio13",
};
static const char * const qdss_stm7_groups[] = {
	"gpio13",
};
static const char * const blsp_i2c8_groups[] = {
	"gpio6", "gpio7",
};
static const char * const atest_usb1_groups[] = {
	"gpio7",
};
static const char * const atest_usb13_groups[] = {
	"gpio8",
};
static const char * const atest_bbrx0_groups[] = {
	"gpio9",
};
static const char * const atest_gpsadc1_groups[] = {
	"gpio10",
};
static const char * const qdss_tracedata_b_groups[] = {
	"gpio13", "gpio14", "gpio15", "gpio16", "gpio17", "gpio18", "gpio19",
	"gpio21", "gpio22", "gpio23", "gpio26", "gpio29", "gpio57", "gpio58",
	"gpio92", "gpio93",
};
static const char * const pll_reset_groups[] = {
	"gpio14",
};
static const char * const qdss_stm6_groups[] = {
	"gpio14",
};
static const char * const qdss_stm5_groups[] = {
	"gpio15",
};
static const char * const qdss_stm4_groups[] = {
	"gpio16",
};
static const char * const atest_usb2_groups[] = {
	"gpio16",
};
static const char * const dac_calib3_groups[] = {
	"gpio17", "gpio44",
};
static const char * const cci_i2c_groups[] = {
	"gpio17", "gpio18", "gpio19", "gpio20",
};
static const char * const qdss_stm3_groups[] = {
	"gpio17",
};
static const char * const atest_usb23_groups[] = {
	"gpio17",
};
static const char * const atest_char3_groups[] = {
	"gpio17",
};
static const char * const dac_calib4_groups[] = {
	"gpio18", "gpio45",
};
static const char * const qdss_stm2_groups[] = {
	"gpio18",
};
static const char * const atest_usb22_groups[] = {
	"gpio18",
};
static const char * const atest_char2_groups[] = {
	"gpio18",
};
static const char * const dac_calib5_groups[] = {
	"gpio19", "gpio46",
};
static const char * const qdss_stm1_groups[] = {
	"gpio19",
};
static const char * const atest_usb21_groups[] = {
	"gpio19",
};
static const char * const atest_char1_groups[] = {
	"gpio19",
};
static const char * const dac_calib6_groups[] = {
	"gpio20", "gpio47",
};
static const char * const dbg_out_groups[] = {
	"gpio20",
};
static const char * const qdss_stm0_groups[] = {
	"gpio20",
};
static const char * const atest_usb20_groups[] = {
	"gpio20",
};
static const char * const atest_char0_groups[] = {
	"gpio20",
};
static const char * const dac_calib7_groups[] = {
	"gpio21", "gpio48",
};
static const char * const cci_timer0_groups[] = {
	"gpio21",
};
static const char * const qdss_stm13_groups[] = {
	"gpio21",
};
static const char * const dac_calib8_groups[] = {
	"gpio22", "gpio49",
};
static const char * const cci_timer1_groups[] = {
	"gpio22",
};
static const char * const qdss_stm12_groups[] = {
	"gpio22",
};
static const char * const dac_calib9_groups[] = {
	"gpio23", "gpio50",
};
static const char * const cci_timer2_groups[] = {
	"gpio23",
};
static const char * const qdss_stm11_groups[] = {
	"gpio23",
};
static const char * const dac_calib10_groups[] = {
	"gpio24", "gpio51",
};
static const char * const cci_timer3_groups[] = {
	"gpio24",
};
static const char * const cci_async_groups[] = {
	"gpio24", "gpio25", "gpio26",
};
static const char * const blsp1_spi_groups[] = {
	"gpio24", "gpio27", "gpio28", "gpio90",
};
static const char * const qdss_stm10_groups[] = {
	"gpio24",
};
static const char * const qdss_cti_trig_in_a_groups[] = {
	"gpio24",
};
static const char * const dac_calib11_groups[] = {
	"gpio25", "gpio52",
};
static const char * const cci_timer4_groups[] = {
	"gpio25",
};
static const char * const blsp_spi6_groups[] = {
	"gpio25", "gpio26", "gpio27", "gpio28",
};
static const char * const blsp_uart6_groups[] = {
	"gpio25", "gpio26", "gpio27", "gpio28",
};
static const char * const blsp_uim6_groups[] = {
	"gpio25", "gpio26",
};
static const char * const blsp2_spi_groups[] = {
	"gpio25", "gpio29", "gpio30",
};
static const char * const qdss_stm9_groups[] = {
	"gpio25",
};
static const char * const qdss_cti_trig_out_a_groups[] = {
	"gpio25",
};
static const char * const dac_calib12_groups[] = {
	"gpio26", "gpio53",
};
static const char * const qdss_stm8_groups[] = {
	"gpio26",
};
static const char * const dac_calib13_groups[] = {
	"gpio27", "gpio54",
};
static const char * const blsp_i2c6_groups[] = {
	"gpio27", "gpio28",
};
static const char * const qdss_tracectl_a_groups[] = {
	"gpio27",
};
static const char * const dac_calib14_groups[] = {
	"gpio28", "gpio55",
};
static const char * const qdss_traceclk_a_groups[] = {
	"gpio28",
};
static const char * const dac_calib15_groups[] = {
	"gpio29", "gpio56",
};
static const char * const dac_calib16_groups[] = {
	"gpio30", "gpio57",
};
static const char * const hdmi_rcv_groups[] = {
	"gpio30",
};
static const char * const dac_calib17_groups[] = {
	"gpio31", "gpio58",
};
static const char * const pwr_modem_groups[] = {
	"gpio31",
};
static const char * const hdmi_cec_groups[] = {
	"gpio31",
};
static const char * const pwr_nav_groups[] = {
	"gpio32",
};
static const char * const dac_calib18_groups[] = {
	"gpio32", "gpio59",
};
static const char * const hdmi_ddc_groups[] = {
	"gpio32", "gpio33",
};
static const char * const pwr_crypto_groups[] = {
	"gpio33",
};
static const char * const dac_calib19_groups[] = {
	"gpio33", "gpio60",
};
static const char * const dac_calib20_groups[] = {
	"gpio34", "gpio61",
};
static const char * const hdmi_hot_groups[] = {
	"gpio34",
};
static const char * const dac_calib21_groups[] = {
	"gpio35", "gpio62",
};
static const char * const pci_e0_groups[] = {
	"gpio35", "gpio36",
};
static const char * const dac_calib22_groups[] = {
	"gpio36", "gpio63",
};
static const char * const dac_calib23_groups[] = {
	"gpio37", "gpio64",
};
static const char * const blsp_i2c2_groups[] = {
	"gpio43", "gpio44",
};
static const char * const blsp_spi3_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48",
};
static const char * const blsp_uart3_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48",
};
static const char * const blsp_uim3_groups[] = {
	"gpio45", "gpio46",
};
static const char * const blsp_i2c3_groups[] = {
	"gpio47", "gpio48",
};
static const char * const dac_calib24_groups[] = {
	"gpio38", "gpio65",
};
static const char * const dac_calib25_groups[] = {
	"gpio39", "gpio66",
};
static const char * const tsif1_sync_groups[] = {
	"gpio39",
};
static const char * const sd_write_groups[] = {
	"gpio40",
};
static const char * const tsif1_error_groups[] = {
	"gpio40",
};
static const char * const blsp_spi2_groups[] = {
	"gpio41", "gpio42", "gpio43", "gpio44",
};
static const char * const blsp_uart2_groups[] = {
	"gpio41", "gpio42", "gpio43", "gpio44",
};
static const char * const blsp_uim2_groups[] = {
	"gpio41", "gpio42",
};
static const char * const qdss_cti_groups[] = {
	"gpio41", "gpio42", "gpio100", "gpio101",
};
static const char * const uim3_groups[] = {
	"gpio49", "gpio50", "gpio51", "gpio52",
};
static const char * const blsp_spi9_groups[] = {
	"gpio49", "gpio50", "gpio51", "gpio52",
};
static const char * const blsp_uart9_groups[] = {
	"gpio49", "gpio50", "gpio51", "gpio52",
};
static const char * const blsp_uim9_groups[] = {
	"gpio49", "gpio50",
};
static const char * const blsp10_spi_groups[] = {
	"gpio49", "gpio50", "gpio51", "gpio52", "gpio88",
};
static const char * const blsp_i2c9_groups[] = {
	"gpio51", "gpio52",
};
static const char * const blsp_spi7_groups[] = {
	"gpio53", "gpio54", "gpio55", "gpio56",
};
static const char * const blsp_uart7_groups[] = {
	"gpio53", "gpio54", "gpio55", "gpio56",
};
static const char * const blsp_uim7_groups[] = {
	"gpio53", "gpio54",
};
static const char * const qdss_tracedata_a_groups[] = {
	"gpio53", "gpio54", "gpio63", "gpio64", "gpio65", "gpio66", "gpio67",
	"gpio74", "gpio75", "gpio76", "gpio77", "gpio85", "gpio86", "gpio87",
	"gpio89", "gpio90",
};
static const char * const blsp_i2c7_groups[] = {
	"gpio55", "gpio56",
};
static const char * const qua_mi2s_groups[] = {
	"gpio57", "gpio58", "gpio59", "gpio60", "gpio61", "gpio62", "gpio63",
};
static const char * const gcc_gp1_clk_a_groups[] = {
	"gpio57",
};
static const char * const uim4_groups[] = {
	"gpio58", "gpio59", "gpio60", "gpio61",
};
static const char * const blsp_spi11_groups[] = {
	"gpio58", "gpio59", "gpio60", "gpio61",
};
static const char * const blsp_uart11_groups[] = {
	"gpio58", "gpio59", "gpio60", "gpio61",
};
static const char * const blsp_uim11_groups[] = {
	"gpio58", "gpio59",
};
static const char * const gcc_gp2_clk_a_groups[] = {
	"gpio58",
};
static const char * const gcc_gp3_clk_a_groups[] = {
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
static const char * const qdss_stm18_groups[] = {
	"gpio63",
};
static const char * const pri_mi2s_groups[] = {
	"gpio64", "gpio65", "gpio66", "gpio67", "gpio68",
};
static const char * const qdss_stm17_groups[] = {
	"gpio64",
};
static const char * const blsp_spi4_groups[] = {
	"gpio65", "gpio66", "gpio67", "gpio68",
};
static const char * const blsp_uart4_groups[] = {
	"gpio65", "gpio66", "gpio67", "gpio68",
};
static const char * const blsp_uim4_groups[] = {
	"gpio65", "gpio66",
};
static const char * const qdss_stm16_groups[] = {
	"gpio65",
};
static const char * const qdss_stm15_groups[] = {
	"gpio66",
};
static const char * const dac_calib26_groups[] = {
	"gpio67",
};
static const char * const blsp_i2c4_groups[] = {
	"gpio67", "gpio68",
};
static const char * const qdss_stm14_groups[] = {
	"gpio67",
};
static const char * const spkr_i2s_groups[] = {
	"gpio69", "gpio70", "gpio71", "gpio72",
};
static const char * const audio_ref_groups[] = {
	"gpio69",
};
static const char * const lpass_slimbus_groups[] = {
	"gpio70", "gpio71", "gpio72",
};
static const char * const isense_dbg_groups[] = {
	"gpio70",
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
static const char * const qdss_stm22_groups[] = {
	"gpio74",
};
static const char * const qdss_stm21_groups[] = {
	"gpio75",
};
static const char * const qdss_stm20_groups[] = {
	"gpio76",
};
static const char * const qdss_stm19_groups[] = {
	"gpio77",
};
static const char * const ssc_irq_groups[] = {
	"gpio78", "gpio79", "gpio80", "gpio117", "gpio118", "gpio119",
	"gpio120", "gpio121", "gpio122", "gpio123", "gpio124", "gpio125",
};
static const char * const gcc_gp1_clk_b_groups[] = {
	"gpio78",
};
static const char * const sec_mi2s_groups[] = {
	"gpio79", "gpio80", "gpio81", "gpio82", "gpio83",
};
static const char * const blsp_spi5_groups[] = {
	"gpio81", "gpio82", "gpio83", "gpio84",
};
static const char * const blsp_uart5_groups[] = {
	"gpio81", "gpio82", "gpio83", "gpio84",
};
static const char * const blsp_uim5_groups[] = {
	"gpio81", "gpio82",
};
static const char * const gcc_gp2_clk_b_groups[] = {
	"gpio81",
};
static const char * const gcc_gp3_clk_b_groups[] = {
	"gpio82",
};
static const char * const blsp_i2c5_groups[] = {
	"gpio83", "gpio84",
};
static const char * const blsp_spi12_groups[] = {
	"gpio85", "gpio86", "gpio87", "gpio88",
};
static const char * const blsp_uart12_groups[] = {
	"gpio85", "gpio86", "gpio87", "gpio88",
};
static const char * const blsp_uim12_groups[] = {
	"gpio85", "gpio86",
};
static const char * const qdss_stm25_groups[] = {
	"gpio85",
};
static const char * const qdss_stm31_groups[] = {
	"gpio86",
};
static const char * const blsp_i2c12_groups[] = {
	"gpio87", "gpio88",
};
static const char * const qdss_stm30_groups[] = {
	"gpio87",
};
static const char * const qdss_stm29_groups[] = {
	"gpio88",
};
static const char * const tsif1_clk_groups[] = {
	"gpio89",
};
static const char * const qdss_stm28_groups[] = {
	"gpio89",
};
static const char * const tsif1_en_groups[] = {
	"gpio90",
};
static const char * const tsif1_data_groups[] = {
	"gpio91",
};
static const char * const sdc4_cmd_groups[] = {
	"gpio91",
};
static const char * const qdss_stm27_groups[] = {
	"gpio91",
};
static const char * const qdss_traceclk_b_groups[] = {
	"gpio91",
};
static const char * const tsif2_error_groups[] = {
	"gpio92",
};
static const char * const sdc43_groups[] = {
	"gpio92",
};
static const char * const vfr_1_groups[] = {
	"gpio92",
};
static const char * const qdss_stm26_groups[] = {
	"gpio92",
};
static const char * const tsif2_clk_groups[] = {
	"gpio93",
};
static const char * const sdc4_clk_groups[] = {
	"gpio93",
};
static const char * const qdss_stm24_groups[] = {
	"gpio93",
};
static const char * const tsif2_en_groups[] = {
	"gpio94",
};
static const char * const sdc42_groups[] = {
	"gpio94",
};
static const char * const qdss_stm23_groups[] = {
	"gpio94",
};
static const char * const qdss_tracectl_b_groups[] = {
	"gpio94",
};
static const char * const sd_card_groups[] = {
	"gpio95",
};
static const char * const tsif2_data_groups[] = {
	"gpio95",
};
static const char * const sdc41_groups[] = {
	"gpio95",
};
static const char * const tsif2_sync_groups[] = {
	"gpio96",
};
static const char * const sdc40_groups[] = {
	"gpio96",
};
static const char * const mdp_vsync_p_b_groups[] = {
	"gpio97",
};
static const char * const ldo_en_groups[] = {
	"gpio97",
};
static const char * const mdp_vsync_s_b_groups[] = {
	"gpio98",
};
static const char * const ldo_update_groups[] = {
	"gpio98",
};
static const char * const blsp11_uart_tx_b_groups[] = {
	"gpio100",
};
static const char * const blsp11_uart_rx_b_groups[] = {
	"gpio101",
};
static const char * const blsp11_i2c_sda_b_groups[] = {
	"gpio102",
};
static const char * const prng_rosc_groups[] = {
	"gpio102",
};
static const char * const blsp11_i2c_scl_b_groups[] = {
	"gpio103",
};
static const char * const uim2_groups[] = {
	"gpio105", "gpio106", "gpio107", "gpio108",
};
static const char * const uim1_groups[] = {
	"gpio109", "gpio110", "gpio111", "gpio112",
};
static const char * const uim_batt_groups[] = {
	"gpio113",
};
static const char * const pci_e2_groups[] = {
	"gpio114", "gpio115", "gpio116",
};
static const char * const pa_indicator_groups[] = {
	"gpio116",
};
static const char * const adsp_ext_groups[] = {
	"gpio118",
};
static const char * const ddr_bist_groups[] = {
	"gpio121", "gpio122", "gpio123", "gpio124",
};
static const char * const qdss_tracedata_11_groups[] = {
	"gpio123",
};
static const char * const qdss_tracedata_12_groups[] = {
	"gpio124",
};
static const char * const modem_tsync_groups[] = {
	"gpio128",
};
static const char * const nav_dr_groups[] = {
	"gpio128",
};
static const char * const nav_pps_groups[] = {
	"gpio128",
};
static const char * const pci_e1_groups[] = {
	"gpio130", "gpio131", "gpio132",
};
static const char * const gsm_tx_groups[] = {
	"gpio134", "gpio135",
};
static const char * const qspi_cs_groups[] = {
	"gpio138", "gpio141",
};
static const char * const ssbi2_groups[] = {
	"gpio139",
};
static const char * const ssbi1_groups[] = {
	"gpio140",
};
static const char * const mss_lte_groups[] = {
	"gpio144", "gpio145",
};
static const char * const qspi_clk_groups[] = {
	"gpio145",
};
static const char * const qspi0_groups[] = {
	"gpio146",
};
static const char * const qspi1_groups[] = {
	"gpio147",
};
static const char * const qspi2_groups[] = {
	"gpio148",
};
static const char * const qspi3_groups[] = {
	"gpio149",
};

static const struct pinfunction msm8996_functions[] = {
	MSM_PIN_FUNCTION(adsp_ext),
	MSM_PIN_FUNCTION(atest_bbrx0),
	MSM_PIN_FUNCTION(atest_bbrx1),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_char0),
	MSM_PIN_FUNCTION(atest_char1),
	MSM_PIN_FUNCTION(atest_char2),
	MSM_PIN_FUNCTION(atest_char3),
	MSM_PIN_FUNCTION(atest_gpsadc0),
	MSM_PIN_FUNCTION(atest_gpsadc1),
	MSM_PIN_FUNCTION(atest_tsens),
	MSM_PIN_FUNCTION(atest_tsens2),
	MSM_PIN_FUNCTION(atest_usb1),
	MSM_PIN_FUNCTION(atest_usb10),
	MSM_PIN_FUNCTION(atest_usb11),
	MSM_PIN_FUNCTION(atest_usb12),
	MSM_PIN_FUNCTION(atest_usb13),
	MSM_PIN_FUNCTION(atest_usb2),
	MSM_PIN_FUNCTION(atest_usb20),
	MSM_PIN_FUNCTION(atest_usb21),
	MSM_PIN_FUNCTION(atest_usb22),
	MSM_PIN_FUNCTION(atest_usb23),
	MSM_PIN_FUNCTION(audio_ref),
	MSM_PIN_FUNCTION(bimc_dte0),
	MSM_PIN_FUNCTION(bimc_dte1),
	MSM_PIN_FUNCTION(blsp10_spi),
	MSM_PIN_FUNCTION(blsp11_i2c_scl_b),
	MSM_PIN_FUNCTION(blsp11_i2c_sda_b),
	MSM_PIN_FUNCTION(blsp11_uart_rx_b),
	MSM_PIN_FUNCTION(blsp11_uart_tx_b),
	MSM_PIN_FUNCTION(blsp1_spi),
	MSM_PIN_FUNCTION(blsp2_spi),
	MSM_PIN_FUNCTION(blsp_i2c1),
	MSM_PIN_FUNCTION(blsp_i2c10),
	MSM_PIN_FUNCTION(blsp_i2c11),
	MSM_PIN_FUNCTION(blsp_i2c12),
	MSM_PIN_FUNCTION(blsp_i2c2),
	MSM_PIN_FUNCTION(blsp_i2c3),
	MSM_PIN_FUNCTION(blsp_i2c4),
	MSM_PIN_FUNCTION(blsp_i2c5),
	MSM_PIN_FUNCTION(blsp_i2c6),
	MSM_PIN_FUNCTION(blsp_i2c7),
	MSM_PIN_FUNCTION(blsp_i2c8),
	MSM_PIN_FUNCTION(blsp_i2c9),
	MSM_PIN_FUNCTION(blsp_spi1),
	MSM_PIN_FUNCTION(blsp_spi10),
	MSM_PIN_FUNCTION(blsp_spi11),
	MSM_PIN_FUNCTION(blsp_spi12),
	MSM_PIN_FUNCTION(blsp_spi2),
	MSM_PIN_FUNCTION(blsp_spi3),
	MSM_PIN_FUNCTION(blsp_spi4),
	MSM_PIN_FUNCTION(blsp_spi5),
	MSM_PIN_FUNCTION(blsp_spi6),
	MSM_PIN_FUNCTION(blsp_spi7),
	MSM_PIN_FUNCTION(blsp_spi8),
	MSM_PIN_FUNCTION(blsp_spi9),
	MSM_PIN_FUNCTION(blsp_uart1),
	MSM_PIN_FUNCTION(blsp_uart10),
	MSM_PIN_FUNCTION(blsp_uart11),
	MSM_PIN_FUNCTION(blsp_uart12),
	MSM_PIN_FUNCTION(blsp_uart2),
	MSM_PIN_FUNCTION(blsp_uart3),
	MSM_PIN_FUNCTION(blsp_uart4),
	MSM_PIN_FUNCTION(blsp_uart5),
	MSM_PIN_FUNCTION(blsp_uart6),
	MSM_PIN_FUNCTION(blsp_uart7),
	MSM_PIN_FUNCTION(blsp_uart8),
	MSM_PIN_FUNCTION(blsp_uart9),
	MSM_PIN_FUNCTION(blsp_uim1),
	MSM_PIN_FUNCTION(blsp_uim10),
	MSM_PIN_FUNCTION(blsp_uim11),
	MSM_PIN_FUNCTION(blsp_uim12),
	MSM_PIN_FUNCTION(blsp_uim2),
	MSM_PIN_FUNCTION(blsp_uim3),
	MSM_PIN_FUNCTION(blsp_uim4),
	MSM_PIN_FUNCTION(blsp_uim5),
	MSM_PIN_FUNCTION(blsp_uim6),
	MSM_PIN_FUNCTION(blsp_uim7),
	MSM_PIN_FUNCTION(blsp_uim8),
	MSM_PIN_FUNCTION(blsp_uim9),
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
	MSM_PIN_FUNCTION(dac_calib0),
	MSM_PIN_FUNCTION(dac_calib1),
	MSM_PIN_FUNCTION(dac_calib10),
	MSM_PIN_FUNCTION(dac_calib11),
	MSM_PIN_FUNCTION(dac_calib12),
	MSM_PIN_FUNCTION(dac_calib13),
	MSM_PIN_FUNCTION(dac_calib14),
	MSM_PIN_FUNCTION(dac_calib15),
	MSM_PIN_FUNCTION(dac_calib16),
	MSM_PIN_FUNCTION(dac_calib17),
	MSM_PIN_FUNCTION(dac_calib18),
	MSM_PIN_FUNCTION(dac_calib19),
	MSM_PIN_FUNCTION(dac_calib2),
	MSM_PIN_FUNCTION(dac_calib20),
	MSM_PIN_FUNCTION(dac_calib21),
	MSM_PIN_FUNCTION(dac_calib22),
	MSM_PIN_FUNCTION(dac_calib23),
	MSM_PIN_FUNCTION(dac_calib24),
	MSM_PIN_FUNCTION(dac_calib25),
	MSM_PIN_FUNCTION(dac_calib26),
	MSM_PIN_FUNCTION(dac_calib3),
	MSM_PIN_FUNCTION(dac_calib4),
	MSM_PIN_FUNCTION(dac_calib5),
	MSM_PIN_FUNCTION(dac_calib6),
	MSM_PIN_FUNCTION(dac_calib7),
	MSM_PIN_FUNCTION(dac_calib8),
	MSM_PIN_FUNCTION(dac_calib9),
	MSM_PIN_FUNCTION(dac_gpio),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(ddr_bist),
	MSM_PIN_FUNCTION(edp_hot),
	MSM_PIN_FUNCTION(edp_lcd),
	MSM_PIN_FUNCTION(gcc_gp1_clk_a),
	MSM_PIN_FUNCTION(gcc_gp1_clk_b),
	MSM_PIN_FUNCTION(gcc_gp2_clk_a),
	MSM_PIN_FUNCTION(gcc_gp2_clk_b),
	MSM_PIN_FUNCTION(gcc_gp3_clk_a),
	MSM_PIN_FUNCTION(gcc_gp3_clk_b),
	MSM_GPIO_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(gsm_tx),
	MSM_PIN_FUNCTION(hdmi_cec),
	MSM_PIN_FUNCTION(hdmi_ddc),
	MSM_PIN_FUNCTION(hdmi_hot),
	MSM_PIN_FUNCTION(hdmi_rcv),
	MSM_PIN_FUNCTION(isense_dbg),
	MSM_PIN_FUNCTION(ldo_en),
	MSM_PIN_FUNCTION(ldo_update),
	MSM_PIN_FUNCTION(lpass_slimbus),
	MSM_PIN_FUNCTION(m_voc),
	MSM_PIN_FUNCTION(mdp_vsync),
	MSM_PIN_FUNCTION(mdp_vsync_p_b),
	MSM_PIN_FUNCTION(mdp_vsync_s_b),
	MSM_PIN_FUNCTION(modem_tsync),
	MSM_PIN_FUNCTION(mss_lte),
	MSM_PIN_FUNCTION(nav_dr),
	MSM_PIN_FUNCTION(nav_pps),
	MSM_PIN_FUNCTION(pa_indicator),
	MSM_PIN_FUNCTION(pci_e0),
	MSM_PIN_FUNCTION(pci_e1),
	MSM_PIN_FUNCTION(pci_e2),
	MSM_PIN_FUNCTION(pll_bypassnl),
	MSM_PIN_FUNCTION(pll_reset),
	MSM_PIN_FUNCTION(pri_mi2s),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(pwr_crypto),
	MSM_PIN_FUNCTION(pwr_modem),
	MSM_PIN_FUNCTION(pwr_nav),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b),
	MSM_PIN_FUNCTION(qdss_stm0),
	MSM_PIN_FUNCTION(qdss_stm1),
	MSM_PIN_FUNCTION(qdss_stm10),
	MSM_PIN_FUNCTION(qdss_stm11),
	MSM_PIN_FUNCTION(qdss_stm12),
	MSM_PIN_FUNCTION(qdss_stm13),
	MSM_PIN_FUNCTION(qdss_stm14),
	MSM_PIN_FUNCTION(qdss_stm15),
	MSM_PIN_FUNCTION(qdss_stm16),
	MSM_PIN_FUNCTION(qdss_stm17),
	MSM_PIN_FUNCTION(qdss_stm18),
	MSM_PIN_FUNCTION(qdss_stm19),
	MSM_PIN_FUNCTION(qdss_stm2),
	MSM_PIN_FUNCTION(qdss_stm20),
	MSM_PIN_FUNCTION(qdss_stm21),
	MSM_PIN_FUNCTION(qdss_stm22),
	MSM_PIN_FUNCTION(qdss_stm23),
	MSM_PIN_FUNCTION(qdss_stm24),
	MSM_PIN_FUNCTION(qdss_stm25),
	MSM_PIN_FUNCTION(qdss_stm26),
	MSM_PIN_FUNCTION(qdss_stm27),
	MSM_PIN_FUNCTION(qdss_stm28),
	MSM_PIN_FUNCTION(qdss_stm29),
	MSM_PIN_FUNCTION(qdss_stm3),
	MSM_PIN_FUNCTION(qdss_stm30),
	MSM_PIN_FUNCTION(qdss_stm31),
	MSM_PIN_FUNCTION(qdss_stm4),
	MSM_PIN_FUNCTION(qdss_stm5),
	MSM_PIN_FUNCTION(qdss_stm6),
	MSM_PIN_FUNCTION(qdss_stm7),
	MSM_PIN_FUNCTION(qdss_stm8),
	MSM_PIN_FUNCTION(qdss_stm9),
	MSM_PIN_FUNCTION(qdss_traceclk_a),
	MSM_PIN_FUNCTION(qdss_traceclk_b),
	MSM_PIN_FUNCTION(qdss_tracectl_a),
	MSM_PIN_FUNCTION(qdss_tracectl_b),
	MSM_PIN_FUNCTION(qdss_tracedata_11),
	MSM_PIN_FUNCTION(qdss_tracedata_12),
	MSM_PIN_FUNCTION(qdss_tracedata_a),
	MSM_PIN_FUNCTION(qdss_tracedata_b),
	MSM_PIN_FUNCTION(qspi0),
	MSM_PIN_FUNCTION(qspi1),
	MSM_PIN_FUNCTION(qspi2),
	MSM_PIN_FUNCTION(qspi3),
	MSM_PIN_FUNCTION(qspi_clk),
	MSM_PIN_FUNCTION(qspi_cs),
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
	MSM_PIN_FUNCTION(spkr_i2s),
	MSM_PIN_FUNCTION(ssbi1),
	MSM_PIN_FUNCTION(ssbi2),
	MSM_PIN_FUNCTION(ssc_irq),
	MSM_PIN_FUNCTION(ter_mi2s),
	MSM_PIN_FUNCTION(tsense_pwm1),
	MSM_PIN_FUNCTION(tsense_pwm2),
	MSM_PIN_FUNCTION(tsif1_clk),
	MSM_PIN_FUNCTION(tsif1_data),
	MSM_PIN_FUNCTION(tsif1_en),
	MSM_PIN_FUNCTION(tsif1_error),
	MSM_PIN_FUNCTION(tsif1_sync),
	MSM_PIN_FUNCTION(tsif2_clk),
	MSM_PIN_FUNCTION(tsif2_data),
	MSM_PIN_FUNCTION(tsif2_en),
	MSM_PIN_FUNCTION(tsif2_error),
	MSM_PIN_FUNCTION(tsif2_sync),
	MSM_PIN_FUNCTION(uim1),
	MSM_PIN_FUNCTION(uim2),
	MSM_PIN_FUNCTION(uim3),
	MSM_PIN_FUNCTION(uim4),
	MSM_PIN_FUNCTION(uim_batt),
	MSM_PIN_FUNCTION(vfr_1),
};

static const struct msm_pingroup msm8996_groups[] = {
	PINGROUP(0, blsp_spi1, blsp_uart1, blsp_uim1, NA, NA, NA, NA, NA, NA),
	PINGROUP(1, blsp_spi1, blsp_uart1, blsp_uim1, NA, NA, NA, NA, NA, NA),
	PINGROUP(2, blsp_spi1, blsp_uart1, blsp_i2c1, NA, NA, NA, NA, NA, NA),
	PINGROUP(3, blsp_spi1, blsp_uart1, blsp_i2c1, NA, atest_tsens,
		 bimc_dte1, NA, NA, NA),
	PINGROUP(4, blsp_spi8, blsp_uart8, blsp_uim8, NA, qdss_cti_trig_out_b,
		 dac_calib0, bimc_dte0, NA, NA),
	PINGROUP(5, blsp_spi8, blsp_uart8, blsp_uim8, NA, qdss_cti_trig_in_b,
		 dac_calib1, bimc_dte1, NA, NA),
	PINGROUP(6, blsp_spi8, blsp_uart8, blsp_i2c8, NA, dac_calib2,
		 bimc_dte0, NA, NA, NA),
	PINGROUP(7, blsp_spi8, blsp_uart8, blsp_i2c8, NA, atest_tsens2,
		 atest_usb1, NA, NA, NA),
	PINGROUP(8, blsp_spi10, blsp_uart10, blsp_uim10, NA, atest_bbrx1,
		 atest_usb13, NA, NA, NA),
	PINGROUP(9, blsp_spi10, blsp_uart10, blsp_uim10, atest_bbrx0,
		 atest_usb12, NA, NA, NA, NA),
	PINGROUP(10, mdp_vsync, blsp_spi10, blsp_uart10, blsp_i2c10,
		 atest_gpsadc1, atest_usb11, NA, NA, NA),
	PINGROUP(11, mdp_vsync, blsp_spi10, blsp_uart10, blsp_i2c10,
		 atest_gpsadc0, atest_usb10, NA, NA, NA),
	PINGROUP(12, mdp_vsync, m_voc, dac_gpio, atest_char, NA, NA, NA, NA,
		 NA),
	PINGROUP(13, cam_mclk, pll_bypassnl, qdss_stm7, qdss_tracedata_b, NA,
		 NA, NA, NA, NA),
	PINGROUP(14, cam_mclk, pll_reset, qdss_stm6, qdss_tracedata_b, NA, NA,
		 NA, NA, NA),
	PINGROUP(15, cam_mclk, qdss_stm5, qdss_tracedata_b, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(16, cam_mclk, qdss_stm4, qdss_tracedata_b, NA, atest_usb2, NA,
		 NA, NA, NA),
	PINGROUP(17, cci_i2c, qdss_stm3, qdss_tracedata_b, dac_calib3,
		 atest_usb23, atest_char3, NA, NA, NA),
	PINGROUP(18, cci_i2c, qdss_stm2, qdss_tracedata_b, dac_calib4,
		 atest_usb22, atest_char2, NA, NA, NA),
	PINGROUP(19, cci_i2c, qdss_stm1, qdss_tracedata_b, dac_calib5,
		 atest_usb21, atest_char1, NA, NA, NA),
	PINGROUP(20, cci_i2c, dbg_out, qdss_stm0, dac_calib6, atest_usb20,
		 atest_char0, NA, NA, NA),
	PINGROUP(21, cci_timer0, qdss_stm13, qdss_tracedata_b, dac_calib7, NA,
		 NA, NA, NA, NA),
	PINGROUP(22, cci_timer1, qdss_stm12, qdss_tracedata_b, dac_calib8, NA,
		 NA, NA, NA, NA),
	PINGROUP(23, cci_timer2, blsp1_spi, qdss_stm11, qdss_tracedata_b,
		 dac_calib9, NA, NA, NA, NA),
	PINGROUP(24, cci_timer3, cci_async, blsp1_spi, qdss_stm10,
		 qdss_cti_trig_in_a, dac_calib10, NA, NA, NA),
	PINGROUP(25, cci_timer4, cci_async, blsp_spi6, blsp_uart6, blsp_uim6,
		 blsp2_spi, qdss_stm9, qdss_cti_trig_out_a, dac_calib11),
	PINGROUP(26, cci_async, blsp_spi6, blsp_uart6, blsp_uim6, qdss_stm8,
		 qdss_tracedata_b, dac_calib12, NA, NA),
	PINGROUP(27, blsp_spi6, blsp_uart6, blsp_i2c6, blsp1_spi,
		 qdss_tracectl_a, dac_calib13, NA, NA, NA),
	PINGROUP(28, blsp_spi6, blsp_uart6, blsp_i2c6, blsp1_spi,
		 qdss_traceclk_a, dac_calib14, NA, NA, NA),
	PINGROUP(29, blsp2_spi, NA, qdss_tracedata_b, dac_calib15, NA, NA, NA,
		 NA, NA),
	PINGROUP(30, hdmi_rcv, blsp2_spi, dac_calib16, NA, NA, NA, NA, NA, NA),
	PINGROUP(31, hdmi_cec, pwr_modem, dac_calib17, NA, NA, NA, NA, NA, NA),
	PINGROUP(32, hdmi_ddc, pwr_nav, NA, dac_calib18, NA, NA, NA, NA, NA),
	PINGROUP(33, hdmi_ddc, pwr_crypto, NA, dac_calib19, NA, NA, NA, NA, NA),
	PINGROUP(34, hdmi_hot, NA, dac_calib20, NA, NA, NA, NA, NA, NA),
	PINGROUP(35, pci_e0, NA, dac_calib21, NA, NA, NA, NA, NA, NA),
	PINGROUP(36, pci_e0, NA, dac_calib22, NA, NA, NA, NA, NA, NA),
	PINGROUP(37, NA, dac_calib23, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(38, NA, dac_calib24, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(39, tsif1_sync, NA, dac_calib25, NA, NA, NA, NA, NA, NA),
	PINGROUP(40, sd_write, tsif1_error, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(41, blsp_spi2, blsp_uart2, blsp_uim2, NA, qdss_cti,
		 dac_calib0, NA, NA, NA),
	PINGROUP(42, blsp_spi2, blsp_uart2, blsp_uim2, NA, qdss_cti,
		 dac_calib1, NA, NA, NA),
	PINGROUP(43, blsp_spi2, blsp_uart2, blsp_i2c2, NA, dac_calib2, NA, NA,
		 NA, NA),
	PINGROUP(44, blsp_spi2, blsp_uart2, blsp_i2c2, NA, dac_calib3, NA, NA,
		 NA, NA),
	PINGROUP(45, blsp_spi3, blsp_uart3, blsp_uim3, NA, dac_calib4, NA, NA,
		 NA, NA),
	PINGROUP(46, blsp_spi3, blsp_uart3, blsp_uim3, NA, dac_calib5, NA, NA,
		 NA, NA),
	PINGROUP(47, blsp_spi3, blsp_uart3, blsp_i2c3, dac_calib6, NA, NA, NA,
		 NA, NA),
	PINGROUP(48, blsp_spi3, blsp_uart3, blsp_i2c3, dac_calib7, NA, NA, NA,
		 NA, NA),
	PINGROUP(49, uim3, blsp_spi9, blsp_uart9, blsp_uim9, blsp10_spi,
		 dac_calib8, NA, NA, NA),
	PINGROUP(50, uim3, blsp_spi9, blsp_uart9, blsp_uim9, blsp10_spi,
		 dac_calib9, NA, NA, NA),
	PINGROUP(51, uim3, blsp_spi9, blsp_uart9, blsp_i2c9, blsp10_spi,
		 dac_calib10, NA, NA, NA),
	PINGROUP(52, uim3, blsp_spi9, blsp_uart9, blsp_i2c9,
		 blsp10_spi, dac_calib11, NA, NA, NA),
	PINGROUP(53, blsp_spi7, blsp_uart7, blsp_uim7, NA, qdss_tracedata_a,
		 dac_calib12, NA, NA, NA),
	PINGROUP(54, blsp_spi7, blsp_uart7, blsp_uim7, NA, NA,
		 qdss_tracedata_a, dac_calib13, NA, NA),
	PINGROUP(55, blsp_spi7, blsp_uart7, blsp_i2c7, NA, dac_calib14, NA, NA,
		 NA, NA),
	PINGROUP(56, blsp_spi7, blsp_uart7, blsp_i2c7, NA, dac_calib15, NA, NA,
		 NA, NA),
	PINGROUP(57, qua_mi2s, gcc_gp1_clk_a, NA, qdss_tracedata_b,
		 dac_calib16, NA, NA, NA, NA),
	PINGROUP(58, qua_mi2s, uim4, blsp_spi11, blsp_uart11, blsp_uim11,
		 gcc_gp2_clk_a, NA, qdss_tracedata_b, dac_calib17),
	PINGROUP(59, qua_mi2s, uim4, blsp_spi11, blsp_uart11, blsp_uim11,
		 gcc_gp3_clk_a, NA, dac_calib18, NA),
	PINGROUP(60, qua_mi2s, uim4, blsp_spi11, blsp_uart11, blsp_i2c11,
		 cri_trng0, NA, dac_calib19, NA),
	PINGROUP(61, qua_mi2s, uim4, blsp_spi11, blsp_uart11,
		 blsp_i2c11, cri_trng1, NA, dac_calib20, NA),
	PINGROUP(62, qua_mi2s, cri_trng, NA, dac_calib21, NA, NA, NA, NA, NA),
	PINGROUP(63, qua_mi2s, NA, NA, qdss_stm18, qdss_tracedata_a,
		 dac_calib22, NA, NA, NA),
	PINGROUP(64, pri_mi2s, NA, qdss_stm17, qdss_tracedata_a, dac_calib23,
		 NA, NA, NA, NA),
	PINGROUP(65, pri_mi2s, blsp_spi4, blsp_uart4, blsp_uim4, NA,
		 qdss_stm16, qdss_tracedata_a, dac_calib24, NA),
	PINGROUP(66, pri_mi2s, blsp_spi4, blsp_uart4, blsp_uim4, NA,
		 qdss_stm15, qdss_tracedata_a, dac_calib25, NA),
	PINGROUP(67, pri_mi2s, blsp_spi4, blsp_uart4, blsp_i2c4, qdss_stm14,
		 qdss_tracedata_a, dac_calib26, NA, NA),
	PINGROUP(68, pri_mi2s, blsp_spi4, blsp_uart4, blsp_i2c4, NA, NA, NA,
		 NA, NA),
	PINGROUP(69, spkr_i2s, audio_ref, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(70, lpass_slimbus, spkr_i2s, isense_dbg, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(71, lpass_slimbus, spkr_i2s, tsense_pwm1, tsense_pwm2, NA, NA,
		 NA, NA, NA),
	PINGROUP(72, lpass_slimbus, spkr_i2s, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(73, btfm_slimbus, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(74, btfm_slimbus, ter_mi2s, qdss_stm22, qdss_tracedata_a, NA,
		 NA, NA, NA, NA),
	PINGROUP(75, ter_mi2s, qdss_stm21, qdss_tracedata_a, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(76, ter_mi2s, qdss_stm20, qdss_tracedata_a, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(77, ter_mi2s, qdss_stm19, qdss_tracedata_a, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(78, ter_mi2s, gcc_gp1_clk_b, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(79, sec_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(80, sec_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(81, sec_mi2s, blsp_spi5, blsp_uart5, blsp_uim5, gcc_gp2_clk_b,
		 NA, NA, NA, NA),
	PINGROUP(82, sec_mi2s, blsp_spi5, blsp_uart5, blsp_uim5, gcc_gp3_clk_b,
		 NA, NA, NA, NA),
	PINGROUP(83, sec_mi2s, blsp_spi5, blsp_uart5, blsp_i2c5, NA, NA, NA,
		 NA, NA),
	PINGROUP(84, blsp_spi5, blsp_uart5, blsp_i2c5, NA, NA, NA, NA, NA, NA),
	PINGROUP(85, blsp_spi12, blsp_uart12, blsp_uim12, NA, qdss_stm25,
		 qdss_tracedata_a, NA, NA, NA),
	PINGROUP(86, blsp_spi12, blsp_uart12, blsp_uim12, NA, NA, qdss_stm31,
		 qdss_tracedata_a, NA, NA),
	PINGROUP(87, blsp_spi12, blsp_uart12, blsp_i2c12, NA, qdss_stm30,
		 qdss_tracedata_a, NA, NA, NA),
	PINGROUP(88, blsp_spi12, blsp_uart12, blsp_i2c12, blsp10_spi, NA,
		 qdss_stm29, NA, NA, NA),
	PINGROUP(89, tsif1_clk, qdss_stm28, qdss_tracedata_a, NA, NA, NA, NA,
		 NA, NA),
	PINGROUP(90, tsif1_en, blsp1_spi, qdss_tracedata_a, NA, NA, NA, NA, NA,
		 NA),
	PINGROUP(91, tsif1_data, sdc4_cmd, qdss_stm27, qdss_traceclk_b, NA, NA,
		 NA, NA, NA),
	PINGROUP(92, tsif2_error, sdc43, vfr_1, qdss_stm26, qdss_tracedata_b,
		 NA, NA, NA, NA),
	PINGROUP(93, tsif2_clk, sdc4_clk, NA, qdss_stm24, qdss_tracedata_b, NA,
		 NA, NA, NA),
	PINGROUP(94, tsif2_en, sdc42, NA, qdss_stm23, qdss_tracectl_b, NA, NA,
		 NA, NA),
	PINGROUP(95, tsif2_data, sdc41, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(96, tsif2_sync, sdc40, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(97, NA, NA, mdp_vsync_p_b, ldo_en, NA, NA, NA, NA, NA),
	PINGROUP(98, NA, NA, mdp_vsync_s_b, ldo_update, NA, NA, NA, NA, NA),
	PINGROUP(99, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(100, NA, NA, blsp11_uart_tx_b, qdss_cti, NA, NA, NA, NA, NA),
	PINGROUP(101, NA, blsp11_uart_rx_b, qdss_cti, NA, NA, NA, NA, NA, NA),
	PINGROUP(102, NA, blsp11_i2c_sda_b, prng_rosc, NA, NA, NA, NA, NA, NA),
	PINGROUP(103, NA, blsp11_i2c_scl_b, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(104, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(105, uim2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(106, uim2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(107, uim2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(108, uim2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(109, uim1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(110, uim1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(111, uim1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(112, uim1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(113, uim_batt, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(114, NA, pci_e2, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(115, NA, pci_e2, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(116, NA, pa_indicator, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(117, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(118, adsp_ext, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(119, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(120, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(121, ddr_bist, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(122, ddr_bist, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(123, ddr_bist, qdss_tracedata_11, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(124, ddr_bist, qdss_tracedata_12, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(125, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(126, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(127, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(128, NA, modem_tsync, nav_dr, nav_pps, NA, NA, NA, NA, NA),
	PINGROUP(129, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(130, pci_e1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(131, pci_e1, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(132, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(133, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(134, gsm_tx, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(135, gsm_tx, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(136, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(137, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(138, NA, qspi_cs, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(139, NA, ssbi2, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(140, NA, ssbi1, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(141, NA, qspi_cs, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(142, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(143, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(144, mss_lte, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(145, mss_lte, qspi_clk, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(146, NA, qspi0, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(147, NA, qspi1, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(148, NA, qspi2, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(149, NA, qspi3, NA, NA, NA, NA, NA, NA, NA),
	SDC_QDSD_PINGROUP(sdc1_clk, 0x12c000, 13, 6),
	SDC_QDSD_PINGROUP(sdc1_cmd, 0x12c000, 11, 3),
	SDC_QDSD_PINGROUP(sdc1_data, 0x12c000, 9, 0),
	SDC_QDSD_PINGROUP(sdc2_clk, 0x12d000, 14, 6),
	SDC_QDSD_PINGROUP(sdc2_cmd, 0x12d000, 11, 3),
	SDC_QDSD_PINGROUP(sdc2_data, 0x12d000, 9, 0),
	SDC_QDSD_PINGROUP(sdc1_rclk, 0x12c000, 15, 0),
};

static const struct msm_gpio_wakeirq_map msm8996_mpm_map[] = {
	{ 1, 3 }, { 5, 4 }, { 9, 5 }, { 11, 6 }, { 66, 7 }, { 22, 8 }, { 24, 9 }, { 26, 10 },
	{ 34, 11 }, { 36, 12 }, { 37, 13 }, { 38, 14 }, { 40, 15 }, { 42, 16 }, { 46, 17 },
	{ 50, 18 }, { 53, 19 }, { 54, 20 }, { 56, 21 }, { 57, 22 }, { 58, 23 }, { 59, 24 },
	{ 60, 25 }, { 61, 26 }, { 62, 27 }, { 63, 28 }, { 64, 29 }, { 71, 30 }, { 73, 31 },
	{ 77, 32 }, { 78, 33 }, { 79, 34 }, { 80, 35 }, { 82, 36 }, { 86, 37 }, { 91, 38 },
	{ 92, 39 }, { 95, 40 }, { 97, 41 }, { 101, 42 }, { 104, 43 }, { 106, 44 }, { 108, 45 },
	{ 112, 46 }, { 113, 47 }, { 110, 48 }, { 127, 50 }, { 115, 51 }, { 116, 54 }, { 117, 55 },
	{ 118, 56 }, { 119, 57 }, { 120, 58 }, { 121, 59 }, { 122, 60 }, { 123, 61 }, { 124, 62 },
	{ 125, 63 }, { 126, 64 }, { 129, 65 }, { 131, 66 }, { 132, 67 }, { 133, 68 }, { 145, 69 },
};

static const struct msm_pinctrl_soc_data msm8996_pinctrl = {
	.pins = msm8996_pins,
	.npins = ARRAY_SIZE(msm8996_pins),
	.functions = msm8996_functions,
	.nfunctions = ARRAY_SIZE(msm8996_functions),
	.groups = msm8996_groups,
	.ngroups = ARRAY_SIZE(msm8996_groups),
	.ngpios = 150,
	.wakeirq_map = msm8996_mpm_map,
	.nwakeirq_map = ARRAY_SIZE(msm8996_mpm_map),
};

static int msm8996_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &msm8996_pinctrl);
}

static const struct of_device_id msm8996_pinctrl_of_match[] = {
	{ .compatible = "qcom,msm8996-pinctrl", },
	{ }
};

static struct platform_driver msm8996_pinctrl_driver = {
	.driver = {
		.name = "msm8996-pinctrl",
		.of_match_table = msm8996_pinctrl_of_match,
	},
	.probe = msm8996_pinctrl_probe,
};

static int __init msm8996_pinctrl_init(void)
{
	return platform_driver_register(&msm8996_pinctrl_driver);
}
arch_initcall(msm8996_pinctrl_init);

static void __exit msm8996_pinctrl_exit(void)
{
	platform_driver_unregister(&msm8996_pinctrl_driver);
}
module_exit(msm8996_pinctrl_exit);

MODULE_DESCRIPTION("Qualcomm msm8996 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, msm8996_pinctrl_of_match);
