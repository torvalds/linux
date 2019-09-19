// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

#define FUNCTION(fname)					\
	[msm_mux_##fname] = {				\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define NORTH	0x00500000
#define SOUTH	0x00900000
#define EAST	0x00100000
#define REG_SIZE 0x1000
#define PINGROUP(id, base, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10)	\
	{						\
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = ARRAY_SIZE(gpio##id##_pins),	\
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
		},					\
		.nfuncs = 11,				\
		.ctl_reg = base + REG_SIZE * id,		\
		.io_reg = base + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = base + 0x8 + REG_SIZE * id,	\
		.intr_status_reg = base + 0xc + REG_SIZE * id,	\
		.intr_target_reg = base + 0x8 + REG_SIZE * id,	\
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
		.intr_raw_status_bit = -1,		\
		.intr_polarity_bit = -1,		\
		.intr_detection_bit = -1,		\
		.intr_detection_width = -1,		\
	}

#define UFS_RESET(pg_name, offset)				\
	{						\
		.name = #pg_name,			\
		.pins = pg_name##_pins,			\
		.npins = ARRAY_SIZE(pg_name##_pins),	\
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
static const struct pinctrl_pin_desc sdm845_pins[] = {
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

static const unsigned int ufs_reset_pins[] = { 150 };
static const unsigned int sdc2_clk_pins[] = { 151 };
static const unsigned int sdc2_cmd_pins[] = { 152 };
static const unsigned int sdc2_data_pins[] = { 153 };

enum sdm845_functions {
	msm_mux_gpio,
	msm_mux_adsp_ext,
	msm_mux_agera_pll,
	msm_mux_atest_char,
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
	msm_mux_edp_hot,
	msm_mux_edp_lcd,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
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
	msm_mux_mss_lte,
	msm_mux_nav_pps,
	msm_mux_pa_indicator,
	msm_mux_pci_e0,
	msm_mux_pci_e1,
	msm_mux_phase_flag,
	msm_mux_pll_bist,
	msm_mux_pll_bypassnl,
	msm_mux_pll_reset,
	msm_mux_pri_mi2s,
	msm_mux_pri_mi2s_ws,
	msm_mux_prng_rosc,
	msm_mux_qdss_cti,
	msm_mux_qdss,
	msm_mux_qlink_enable,
	msm_mux_qlink_request,
	msm_mux_qspi_clk,
	msm_mux_qspi_cs,
	msm_mux_qspi_data,
	msm_mux_qua_mi2s,
	msm_mux_qup0,
	msm_mux_qup1,
	msm_mux_qup10,
	msm_mux_qup11,
	msm_mux_qup12,
	msm_mux_qup13,
	msm_mux_qup14,
	msm_mux_qup15,
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
	msm_mux_sdc4_clk,
	msm_mux_sdc4_cmd,
	msm_mux_sdc4_data,
	msm_mux_sec_mi2s,
	msm_mux_sp_cmu,
	msm_mux_spkr_i2s,
	msm_mux_ter_mi2s,
	msm_mux_tgu_ch0,
	msm_mux_tgu_ch1,
	msm_mux_tgu_ch2,
	msm_mux_tgu_ch3,
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
	msm_mux_vsense_trigger,
	msm_mux_wlan1_adc0,
	msm_mux_wlan1_adc1,
	msm_mux_wlan2_adc0,
	msm_mux_wlan2_adc1,
	msm_mux__,
};

static const char * const ddr_pxi3_groups[] = {
	"gpio12", "gpio13",
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
static const char * const qup1_groups[] = {
	"gpio17", "gpio18", "gpio19", "gpio20",
};
static const char * const cci_timer0_groups[] = {
	"gpio21",
};
static const char * const gcc_gp2_groups[] = {
	"gpio21", "gpio58",
};
static const char * const cci_timer1_groups[] = {
	"gpio22",
};
static const char * const gcc_gp3_groups[] = {
	"gpio22", "gpio59",
};
static const char * const cci_timer2_groups[] = {
	"gpio23",
};
static const char * const cci_timer3_groups[] = {
	"gpio24",
};
static const char * const cci_async_groups[] = {
	"gpio24", "gpio25", "gpio26",
};
static const char * const cci_timer4_groups[] = {
	"gpio25",
};
static const char * const qup2_groups[] = {
	"gpio27", "gpio28", "gpio29", "gpio30",
};
static const char * const phase_flag_groups[] = {
	"gpio29", "gpio30", "gpio52", "gpio53", "gpio54", "gpio55", "gpio56",
	"gpio57", "gpio58", "gpio59", "gpio60", "gpio61", "gpio62", "gpio63",
	"gpio64", "gpio74", "gpio75", "gpio76", "gpio77", "gpio89", "gpio90",
	"gpio96", "gpio99", "gpio100", "gpio103", "gpio137", "gpio138",
	"gpio139", "gpio140", "gpio141", "gpio142", "gpio143",
};
static const char * const qup11_groups[] = {
	"gpio31", "gpio32", "gpio33", "gpio34",
};
static const char * const qup14_groups[] = {
	"gpio31", "gpio32", "gpio33", "gpio34",
};
static const char * const pci_e0_groups[] = {
	"gpio35", "gpio36",
};
static const char * const jitter_bist_groups[] = {
	"gpio35",
};
static const char * const pll_bist_groups[] = {
	"gpio36",
};
static const char * const atest_tsens_groups[] = {
	"gpio36",
};
static const char * const agera_pll_groups[] = {
	"gpio37",
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
static const char * const tsif1_error_groups[] = {
	"gpio40",
};
static const char * const qup3_groups[] = {
	"gpio41", "gpio42", "gpio43", "gpio44",
};
static const char * const qup6_groups[] = {
	"gpio45", "gpio46", "gpio47", "gpio48",
};
static const char * const qup12_groups[] = {
	"gpio49", "gpio50", "gpio51", "gpio52",
};
static const char * const qup10_groups[] = {
	"gpio53", "gpio54", "gpio55", "gpio56",
};
static const char * const qua_mi2s_groups[] = {
	"gpio57", "gpio58", "gpio59", "gpio60", "gpio61", "gpio62", "gpio63",
};
static const char * const gcc_gp1_groups[] = {
	"gpio57", "gpio78",
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
static const char * const qup8_groups[] = {
	"gpio65", "gpio66", "gpio67", "gpio68",
};
static const char * const pri_mi2s_ws_groups[] = {
	"gpio66",
};
static const char * const spkr_i2s_groups[] = {
	"gpio69", "gpio70", "gpio71", "gpio72",
};
static const char * const audio_ref_groups[] = {
	"gpio69",
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
static const char * const atest_usb2_groups[] = {
	"gpio73",
};
static const char * const ter_mi2s_groups[] = {
	"gpio74", "gpio75", "gpio76", "gpio77", "gpio78",
};
static const char * const atest_usb23_groups[] = {
	"gpio74",
};
static const char * const atest_usb22_groups[] = {
	"gpio75",
};
static const char * const atest_usb21_groups[] = {
	"gpio76",
};
static const char * const atest_usb20_groups[] = {
	"gpio77",
};
static const char * const sec_mi2s_groups[] = {
	"gpio79", "gpio80", "gpio81", "gpio82", "gpio83",
};
static const char * const qup15_groups[] = {
	"gpio81", "gpio82", "gpio83", "gpio84",
};
static const char * const qup5_groups[] = {
	"gpio85", "gpio86", "gpio87", "gpio88",
};
static const char * const tsif1_clk_groups[] = {
	"gpio89",
};
static const char * const qup4_groups[] = {
	"gpio89", "gpio90", "gpio91", "gpio92",
};
static const char * const qspi_cs_groups[] = {
	"gpio89", "gpio90",
};
static const char * const tgu_ch3_groups[] = {
	"gpio89",
};
static const char * const tsif1_en_groups[] = {
	"gpio90",
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
static const char * const tgu_ch0_groups[] = {
	"gpio90",
};
static const char * const tsif1_data_groups[] = {
	"gpio91",
};
static const char * const sdc4_cmd_groups[] = {
	"gpio91",
};
static const char * const qspi_data_groups[] = {
	"gpio91", "gpio92", "gpio93", "gpio94",
};
static const char * const tgu_ch1_groups[] = {
	"gpio91",
};
static const char * const tsif2_error_groups[] = {
	"gpio92",
};
static const char * const sdc4_data_groups[] = {
	"gpio92",
	"gpio94",
	"gpio95",
	"gpio96",
};
static const char * const vfr_1_groups[] = {
	"gpio92",
};
static const char * const tgu_ch2_groups[] = {
	"gpio92",
};
static const char * const tsif2_clk_groups[] = {
	"gpio93",
};
static const char * const sdc4_clk_groups[] = {
	"gpio93",
};
static const char * const qup7_groups[] = {
	"gpio93", "gpio94", "gpio95", "gpio96",
};
static const char * const tsif2_en_groups[] = {
	"gpio94",
};
static const char * const tsif2_data_groups[] = {
	"gpio95",
};
static const char * const qspi_clk_groups[] = {
	"gpio95",
};
static const char * const tsif2_sync_groups[] = {
	"gpio96",
};
static const char * const ldo_en_groups[] = {
	"gpio97",
};
static const char * const ldo_update_groups[] = {
	"gpio98",
};
static const char * const pci_e1_groups[] = {
	"gpio102", "gpio103",
};
static const char * const prng_rosc_groups[] = {
	"gpio102",
};
static const char * const uim2_data_groups[] = {
	"gpio105",
};
static const char * const qup13_groups[] = {
	"gpio105", "gpio106", "gpio107", "gpio108",
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
static const char * const edp_hot_groups[] = {
	"gpio113",
};
static const char * const nav_pps_groups[] = {
	"gpio114", "gpio114", "gpio115", "gpio115", "gpio128", "gpio128",
	"gpio129", "gpio129", "gpio143", "gpio143",
};
static const char * const atest_char_groups[] = {
	"gpio117", "gpio118", "gpio119", "gpio120", "gpio121",
};
static const char * const adsp_ext_groups[] = {
	"gpio118",
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
static const char * const mss_lte_groups[] = {
	"gpio144", "gpio145",
};
static const char * const qup0_groups[] = {
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
	"gpio147", "gpio148", "gpio149",
};
static const char * const qup9_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7",
};
static const char * const qdss_cti_groups[] = {
	"gpio4", "gpio5", "gpio51", "gpio52", "gpio62", "gpio63", "gpio90",
	"gpio91",
};
static const char * const ddr_pxi0_groups[] = {
	"gpio6", "gpio7",
};
static const char * const ddr_bist_groups[] = {
	"gpio7", "gpio8", "gpio9", "gpio10",
};
static const char * const atest_tsens2_groups[] = {
	"gpio7",
};
static const char * const vsense_trigger_groups[] = {
	"gpio7",
};
static const char * const atest_usb1_groups[] = {
	"gpio7",
};
static const char * const qup_l4_groups[] = {
	"gpio8", "gpio35", "gpio105", "gpio123",
};
static const char * const wlan1_adc1_groups[] = {
	"gpio8",
};
static const char * const atest_usb13_groups[] = {
	"gpio8",
};
static const char * const ddr_pxi1_groups[] = {
	"gpio8", "gpio9",
};
static const char * const qup_l5_groups[] = {
	"gpio9", "gpio36", "gpio106", "gpio124",
};
static const char * const wlan1_adc0_groups[] = {
	"gpio9",
};
static const char * const atest_usb12_groups[] = {
	"gpio9",
};
static const char * const mdp_vsync_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio97", "gpio98",
};
static const char * const qup_l6_groups[] = {
	"gpio10", "gpio37", "gpio107", "gpio125",
};
static const char * const wlan2_adc1_groups[] = {
	"gpio10",
};
static const char * const atest_usb11_groups[] = {
	"gpio10",
};
static const char * const ddr_pxi2_groups[] = {
	"gpio10", "gpio11",
};
static const char * const edp_lcd_groups[] = {
	"gpio11",
};
static const char * const dbg_out_groups[] = {
	"gpio11",
};
static const char * const wlan2_adc0_groups[] = {
	"gpio11",
};
static const char * const atest_usb10_groups[] = {
	"gpio11",
};
static const char * const m_voc_groups[] = {
	"gpio12",
};
static const char * const tsif1_sync_groups[] = {
	"gpio12",
};

static const struct msm_function sdm845_functions[] = {
	FUNCTION(gpio),
	FUNCTION(adsp_ext),
	FUNCTION(agera_pll),
	FUNCTION(atest_char),
	FUNCTION(atest_tsens),
	FUNCTION(atest_tsens2),
	FUNCTION(atest_usb1),
	FUNCTION(atest_usb10),
	FUNCTION(atest_usb11),
	FUNCTION(atest_usb12),
	FUNCTION(atest_usb13),
	FUNCTION(atest_usb2),
	FUNCTION(atest_usb20),
	FUNCTION(atest_usb21),
	FUNCTION(atest_usb22),
	FUNCTION(atest_usb23),
	FUNCTION(audio_ref),
	FUNCTION(btfm_slimbus),
	FUNCTION(cam_mclk),
	FUNCTION(cci_async),
	FUNCTION(cci_i2c),
	FUNCTION(cci_timer0),
	FUNCTION(cci_timer1),
	FUNCTION(cci_timer2),
	FUNCTION(cci_timer3),
	FUNCTION(cci_timer4),
	FUNCTION(cri_trng),
	FUNCTION(cri_trng0),
	FUNCTION(cri_trng1),
	FUNCTION(dbg_out),
	FUNCTION(ddr_bist),
	FUNCTION(ddr_pxi0),
	FUNCTION(ddr_pxi1),
	FUNCTION(ddr_pxi2),
	FUNCTION(ddr_pxi3),
	FUNCTION(edp_hot),
	FUNCTION(edp_lcd),
	FUNCTION(gcc_gp1),
	FUNCTION(gcc_gp2),
	FUNCTION(gcc_gp3),
	FUNCTION(jitter_bist),
	FUNCTION(ldo_en),
	FUNCTION(ldo_update),
	FUNCTION(lpass_slimbus),
	FUNCTION(m_voc),
	FUNCTION(mdp_vsync),
	FUNCTION(mdp_vsync0),
	FUNCTION(mdp_vsync1),
	FUNCTION(mdp_vsync2),
	FUNCTION(mdp_vsync3),
	FUNCTION(mss_lte),
	FUNCTION(nav_pps),
	FUNCTION(pa_indicator),
	FUNCTION(pci_e0),
	FUNCTION(pci_e1),
	FUNCTION(phase_flag),
	FUNCTION(pll_bist),
	FUNCTION(pll_bypassnl),
	FUNCTION(pll_reset),
	FUNCTION(pri_mi2s),
	FUNCTION(pri_mi2s_ws),
	FUNCTION(prng_rosc),
	FUNCTION(qdss_cti),
	FUNCTION(qdss),
	FUNCTION(qlink_enable),
	FUNCTION(qlink_request),
	FUNCTION(qspi_clk),
	FUNCTION(qspi_cs),
	FUNCTION(qspi_data),
	FUNCTION(qua_mi2s),
	FUNCTION(qup0),
	FUNCTION(qup1),
	FUNCTION(qup10),
	FUNCTION(qup11),
	FUNCTION(qup12),
	FUNCTION(qup13),
	FUNCTION(qup14),
	FUNCTION(qup15),
	FUNCTION(qup2),
	FUNCTION(qup3),
	FUNCTION(qup4),
	FUNCTION(qup5),
	FUNCTION(qup6),
	FUNCTION(qup7),
	FUNCTION(qup8),
	FUNCTION(qup9),
	FUNCTION(qup_l4),
	FUNCTION(qup_l5),
	FUNCTION(qup_l6),
	FUNCTION(sd_write),
	FUNCTION(sdc4_clk),
	FUNCTION(sdc4_cmd),
	FUNCTION(sdc4_data),
	FUNCTION(sec_mi2s),
	FUNCTION(sp_cmu),
	FUNCTION(spkr_i2s),
	FUNCTION(ter_mi2s),
	FUNCTION(tgu_ch0),
	FUNCTION(tgu_ch1),
	FUNCTION(tgu_ch2),
	FUNCTION(tgu_ch3),
	FUNCTION(tsense_pwm1),
	FUNCTION(tsense_pwm2),
	FUNCTION(tsif1_clk),
	FUNCTION(tsif1_data),
	FUNCTION(tsif1_en),
	FUNCTION(tsif1_error),
	FUNCTION(tsif1_sync),
	FUNCTION(tsif2_clk),
	FUNCTION(tsif2_data),
	FUNCTION(tsif2_en),
	FUNCTION(tsif2_error),
	FUNCTION(tsif2_sync),
	FUNCTION(uim1_clk),
	FUNCTION(uim1_data),
	FUNCTION(uim1_present),
	FUNCTION(uim1_reset),
	FUNCTION(uim2_clk),
	FUNCTION(uim2_data),
	FUNCTION(uim2_present),
	FUNCTION(uim2_reset),
	FUNCTION(uim_batt),
	FUNCTION(usb_phy),
	FUNCTION(vfr_1),
	FUNCTION(vsense_trigger),
	FUNCTION(wlan1_adc0),
	FUNCTION(wlan1_adc1),
	FUNCTION(wlan2_adc0),
	FUNCTION(wlan2_adc1),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup sdm845_groups[] = {
	PINGROUP(0, EAST, qup0, _, _, _, _, _, _, _, _, _),
	PINGROUP(1, EAST, qup0, _, _, _, _, _, _, _, _, _),
	PINGROUP(2, EAST, qup0, _, _, _, _, _, _, _, _, _),
	PINGROUP(3, EAST, qup0, _, _, _, _, _, _, _, _, _),
	PINGROUP(4, NORTH, qup9, qdss_cti, _, _, _, _, _, _, _, _),
	PINGROUP(5, NORTH, qup9, qdss_cti, _, _, _, _, _, _, _, _),
	PINGROUP(6, NORTH, qup9, _, ddr_pxi0, _, _, _, _, _, _, _),
	PINGROUP(7, NORTH, qup9, ddr_bist, _, atest_tsens2, vsense_trigger, atest_usb1, ddr_pxi0, _, _, _),
	PINGROUP(8, EAST, qup_l4, _, ddr_bist, _, _, wlan1_adc1, atest_usb13, ddr_pxi1, _, _),
	PINGROUP(9, EAST, qup_l5, ddr_bist, _, wlan1_adc0, atest_usb12, ddr_pxi1, _, _, _, _),
	PINGROUP(10, EAST, mdp_vsync, qup_l6, ddr_bist, wlan2_adc1, atest_usb11, ddr_pxi2, _, _, _, _),
	PINGROUP(11, EAST, mdp_vsync, edp_lcd, dbg_out, wlan2_adc0, atest_usb10, ddr_pxi2, _, _, _, _),
	PINGROUP(12, SOUTH, mdp_vsync, m_voc, tsif1_sync, ddr_pxi3, _, _, _, _, _, _),
	PINGROUP(13, SOUTH, cam_mclk, pll_bypassnl, qdss, ddr_pxi3, _, _, _, _, _, _),
	PINGROUP(14, SOUTH, cam_mclk, pll_reset, qdss, _, _, _, _, _, _, _),
	PINGROUP(15, SOUTH, cam_mclk, qdss, _, _, _, _, _, _, _, _),
	PINGROUP(16, SOUTH, cam_mclk, qdss, _, _, _, _, _, _, _, _),
	PINGROUP(17, SOUTH, cci_i2c, qup1, qdss, _, _, _, _, _, _, _),
	PINGROUP(18, SOUTH, cci_i2c, qup1, _, qdss, _, _, _, _, _, _),
	PINGROUP(19, SOUTH, cci_i2c, qup1, _, qdss, _, _, _, _, _, _),
	PINGROUP(20, SOUTH, cci_i2c, qup1, _, qdss, _, _, _, _, _, _),
	PINGROUP(21, SOUTH, cci_timer0, gcc_gp2, qdss, _, _, _, _, _, _, _),
	PINGROUP(22, SOUTH, cci_timer1, gcc_gp3, qdss, _, _, _, _, _, _, _),
	PINGROUP(23, SOUTH, cci_timer2, qdss, _, _, _, _, _, _, _, _),
	PINGROUP(24, SOUTH, cci_timer3, cci_async, qdss, _, _, _, _, _, _, _),
	PINGROUP(25, SOUTH, cci_timer4, cci_async, qdss, _, _, _, _, _, _, _),
	PINGROUP(26, SOUTH, cci_async, qdss, _, _, _, _, _, _, _, _),
	PINGROUP(27, EAST, qup2, qdss, _, _, _, _, _, _, _, _),
	PINGROUP(28, EAST, qup2, qdss, _, _, _, _, _, _, _, _),
	PINGROUP(29, EAST, qup2, _, phase_flag, qdss, _, _, _, _, _, _),
	PINGROUP(30, EAST, qup2, phase_flag, qdss, _, _, _, _, _, _, _),
	PINGROUP(31, NORTH, qup11, qup14, _, _, _, _, _, _, _, _),
	PINGROUP(32, NORTH, qup11, qup14, _, _, _, _, _, _, _, _),
	PINGROUP(33, NORTH, qup11, qup14, _, _, _, _, _, _, _, _),
	PINGROUP(34, NORTH, qup11, qup14, _, _, _, _, _, _, _, _),
	PINGROUP(35, SOUTH, pci_e0, qup_l4, jitter_bist, _, _, _, _, _, _, _),
	PINGROUP(36, SOUTH, pci_e0, qup_l5, pll_bist, _, atest_tsens, _, _, _, _, _),
	PINGROUP(37, SOUTH, qup_l6, agera_pll, _, _, _, _, _, _, _, _),
	PINGROUP(38, NORTH, usb_phy, _, _, _, _, _, _, _, _, _),
	PINGROUP(39, EAST, lpass_slimbus, _, _, _, _, _, _, _, _, _),
	PINGROUP(40, SOUTH, sd_write, tsif1_error, _, _, _, _, _, _, _, _),
	PINGROUP(41, EAST, qup3, _, qdss, _, _, _, _, _, _, _),
	PINGROUP(42, EAST, qup3, _, qdss, _, _, _, _, _, _, _),
	PINGROUP(43, EAST, qup3, _, qdss, _, _, _, _, _, _, _),
	PINGROUP(44, EAST, qup3, _, qdss, _, _, _, _, _, _, _),
	PINGROUP(45, EAST, qup6, _, _, _, _, _, _, _, _, _),
	PINGROUP(46, EAST, qup6, _, _, _, _, _, _, _, _, _),
	PINGROUP(47, EAST, qup6, _, _, _, _, _, _, _, _, _),
	PINGROUP(48, EAST, qup6, _, _, _, _, _, _, _, _, _),
	PINGROUP(49, NORTH, qup12, _, _, _, _, _, _, _, _, _),
	PINGROUP(50, NORTH, qup12, _, _, _, _, _, _, _, _, _),
	PINGROUP(51, NORTH, qup12, qdss_cti, _, _, _, _, _, _, _, _),
	PINGROUP(52, NORTH, qup12, phase_flag, qdss_cti, _, _, _, _, _, _, _),
	PINGROUP(53, NORTH, qup10, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(54, NORTH, qup10, _, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(55, NORTH, qup10, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(56, NORTH, qup10, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(57, NORTH, qua_mi2s, gcc_gp1, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(58, NORTH, qua_mi2s, gcc_gp2, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(59, NORTH, qua_mi2s, gcc_gp3, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(60, NORTH, qua_mi2s, cri_trng0, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(61, NORTH, qua_mi2s, cri_trng1, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(62, NORTH, qua_mi2s, cri_trng, phase_flag, qdss_cti, _, _, _, _, _, _),
	PINGROUP(63, NORTH, qua_mi2s, _, phase_flag, qdss_cti, _, _, _, _, _, _),
	PINGROUP(64, NORTH, pri_mi2s, sp_cmu, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(65, NORTH, pri_mi2s, qup8, _, _, _, _, _, _, _, _),
	PINGROUP(66, NORTH, pri_mi2s_ws, qup8, _, _, _, _, _, _, _, _),
	PINGROUP(67, NORTH, pri_mi2s, qup8, _, _, _, _, _, _, _, _),
	PINGROUP(68, NORTH, pri_mi2s, qup8, _, _, _, _, _, _, _, _),
	PINGROUP(69, EAST, spkr_i2s, audio_ref, _, _, _, _, _, _, _, _),
	PINGROUP(70, EAST, lpass_slimbus, spkr_i2s, _, _, _, _, _, _, _, _),
	PINGROUP(71, EAST, lpass_slimbus, spkr_i2s, tsense_pwm1, tsense_pwm2, _, _, _, _, _, _),
	PINGROUP(72, EAST, lpass_slimbus, spkr_i2s, _, _, _, _, _, _, _, _),
	PINGROUP(73, EAST, btfm_slimbus, atest_usb2, _, _, _, _, _, _, _, _),
	PINGROUP(74, EAST, btfm_slimbus, ter_mi2s, phase_flag, atest_usb23, _, _, _, _, _, _),
	PINGROUP(75, EAST, ter_mi2s, phase_flag, qdss, atest_usb22, _, _, _, _, _, _),
	PINGROUP(76, EAST, ter_mi2s, phase_flag, qdss, atest_usb21, _, _, _, _, _, _),
	PINGROUP(77, EAST, ter_mi2s, phase_flag, qdss, atest_usb20, _, _, _, _, _, _),
	PINGROUP(78, EAST, ter_mi2s, gcc_gp1, _, _, _, _, _, _, _, _),
	PINGROUP(79, NORTH, sec_mi2s, _, _, qdss, _, _, _, _, _, _),
	PINGROUP(80, NORTH, sec_mi2s, _, qdss, _, _, _, _, _, _, _),
	PINGROUP(81, NORTH, sec_mi2s, qup15, _, _, _, _, _, _, _, _),
	PINGROUP(82, NORTH, sec_mi2s, qup15, _, _, _, _, _, _, _, _),
	PINGROUP(83, NORTH, sec_mi2s, qup15, _, _, _, _, _, _, _, _),
	PINGROUP(84, NORTH, qup15, _, _, _, _, _, _, _, _, _),
	PINGROUP(85, EAST, qup5, _, _, _, _, _, _, _, _, _),
	PINGROUP(86, EAST, qup5, _, _, _, _, _, _, _, _, _),
	PINGROUP(87, EAST, qup5, _, _, _, _, _, _, _, _, _),
	PINGROUP(88, EAST, qup5, _, _, _, _, _, _, _, _, _),
	PINGROUP(89, SOUTH, tsif1_clk, qup4, qspi_cs, tgu_ch3, phase_flag, _, _, _, _, _),
	PINGROUP(90, SOUTH, tsif1_en, mdp_vsync0, qup4, qspi_cs, mdp_vsync1,
			    mdp_vsync2, mdp_vsync3, tgu_ch0, phase_flag, qdss_cti),
	PINGROUP(91, SOUTH, tsif1_data, sdc4_cmd, qup4, qspi_data, tgu_ch1, _, qdss_cti, _, _, _),
	PINGROUP(92, SOUTH, tsif2_error, sdc4_data, qup4, qspi_data, vfr_1, tgu_ch2, _, _, _, _),
	PINGROUP(93, SOUTH, tsif2_clk, sdc4_clk, qup7, qspi_data, _, qdss, _, _, _, _),
	PINGROUP(94, SOUTH, tsif2_en, sdc4_data, qup7, qspi_data, _, _, _, _, _, _),
	PINGROUP(95, SOUTH, tsif2_data, sdc4_data, qup7, qspi_clk, _, _, _, _, _, _),
	PINGROUP(96, SOUTH, tsif2_sync, sdc4_data, qup7, phase_flag, _, _, _, _, _, _),
	PINGROUP(97, NORTH, _, _, mdp_vsync, ldo_en, _, _, _, _, _, _),
	PINGROUP(98, NORTH, _, mdp_vsync, ldo_update, _, _, _, _, _, _, _),
	PINGROUP(99, NORTH, phase_flag, _, _, _, _, _, _, _, _, _),
	PINGROUP(100, NORTH, phase_flag, _, _, _, _, _, _, _, _, _),
	PINGROUP(101, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(102, NORTH, pci_e1, prng_rosc, _, _, _, _, _, _, _, _),
	PINGROUP(103, NORTH, pci_e1, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(104, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(105, NORTH, uim2_data, qup13, qup_l4, _, _, _, _, _, _, _),
	PINGROUP(106, NORTH, uim2_clk, qup13, qup_l5, _, _, _, _, _, _, _),
	PINGROUP(107, NORTH, uim2_reset, qup13, qup_l6, _, _, _, _, _, _, _),
	PINGROUP(108, NORTH, uim2_present, qup13, _, _, _, _, _, _, _, _),
	PINGROUP(109, NORTH, uim1_data, _, _, _, _, _, _, _, _, _),
	PINGROUP(110, NORTH, uim1_clk, _, _, _, _, _, _, _, _, _),
	PINGROUP(111, NORTH, uim1_reset, _, _, _, _, _, _, _, _, _),
	PINGROUP(112, NORTH, uim1_present, _, _, _, _, _, _, _, _, _),
	PINGROUP(113, NORTH, uim_batt, edp_hot, _, _, _, _, _, _, _, _),
	PINGROUP(114, NORTH, _, nav_pps, nav_pps, _, _, _, _, _, _, _),
	PINGROUP(115, NORTH, _, nav_pps, nav_pps, _, _, _, _, _, _, _),
	PINGROUP(116, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(117, NORTH, _, qdss, atest_char, _, _, _, _, _, _, _),
	PINGROUP(118, NORTH, adsp_ext, _, qdss, atest_char, _, _, _, _, _, _),
	PINGROUP(119, NORTH, _, qdss, atest_char, _, _, _, _, _, _, _),
	PINGROUP(120, NORTH, _, qdss, atest_char, _, _, _, _, _, _, _),
	PINGROUP(121, NORTH, _, qdss, atest_char, _, _, _, _, _, _, _),
	PINGROUP(122, EAST, _, qdss, _, _, _, _, _, _, _, _),
	PINGROUP(123, EAST, qup_l4, _, qdss, _, _, _, _, _, _, _),
	PINGROUP(124, EAST, qup_l5, _, qdss, _, _, _, _, _, _, _),
	PINGROUP(125, EAST, qup_l6, _, _, _, _, _, _, _, _, _),
	PINGROUP(126, EAST, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(127, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(128, NORTH, nav_pps, nav_pps, _, _, _, _, _, _, _, _),
	PINGROUP(129, NORTH, nav_pps, nav_pps, _, _, _, _, _, _, _, _),
	PINGROUP(130, NORTH, qlink_request, _, _, _, _, _, _, _, _, _),
	PINGROUP(131, NORTH, qlink_enable, _, _, _, _, _, _, _, _, _),
	PINGROUP(132, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(133, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(134, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(135, NORTH, _, pa_indicator, _, _, _, _, _, _, _, _),
	PINGROUP(136, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(137, NORTH, _, _, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(138, NORTH, _, _, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(139, NORTH, _, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(140, NORTH, _, _, phase_flag, _, _, _, _, _, _, _),
	PINGROUP(141, NORTH, _, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(142, NORTH, _, phase_flag, _, _, _, _, _, _, _, _),
	PINGROUP(143, NORTH, _, nav_pps, nav_pps, _, phase_flag, _, _, _, _, _),
	PINGROUP(144, NORTH, mss_lte, _, _, _, _, _, _, _, _, _),
	PINGROUP(145, NORTH, mss_lte, _, _, _, _, _, _, _, _, _),
	PINGROUP(146, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(147, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(148, NORTH, _, _, _, _, _, _, _, _, _, _),
	PINGROUP(149, NORTH, _, _, _, _, _, _, _, _, _, _),
	UFS_RESET(ufs_reset, 0x99f000),
	SDC_QDSD_PINGROUP(sdc2_clk, 0x99a000, 14, 6),
	SDC_QDSD_PINGROUP(sdc2_cmd, 0x99a000, 11, 3),
	SDC_QDSD_PINGROUP(sdc2_data, 0x99a000, 9, 0),
};

static const int sdm845_acpi_reserved_gpios[] = {
	0, 1, 2, 3, 81, 82, 83, 84, -1
};

static const struct msm_pinctrl_soc_data sdm845_pinctrl = {
	.pins = sdm845_pins,
	.npins = ARRAY_SIZE(sdm845_pins),
	.functions = sdm845_functions,
	.nfunctions = ARRAY_SIZE(sdm845_functions),
	.groups = sdm845_groups,
	.ngroups = ARRAY_SIZE(sdm845_groups),
	.ngpios = 151,
};

static const struct msm_pinctrl_soc_data sdm845_acpi_pinctrl = {
	.pins = sdm845_pins,
	.npins = ARRAY_SIZE(sdm845_pins),
	.groups = sdm845_groups,
	.ngroups = ARRAY_SIZE(sdm845_groups),
	.reserved_gpios = sdm845_acpi_reserved_gpios,
	.ngpios = 150,
};

static int sdm845_pinctrl_probe(struct platform_device *pdev)
{
	int ret;

	if (pdev->dev.of_node) {
		ret = msm_pinctrl_probe(pdev, &sdm845_pinctrl);
	} else if (has_acpi_companion(&pdev->dev)) {
		ret = msm_pinctrl_probe(pdev, &sdm845_acpi_pinctrl);
	} else {
		dev_err(&pdev->dev, "DT and ACPI disabled\n");
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id sdm845_pinctrl_acpi_match[] = {
	{ "QCOM0217"},
	{ },
};
MODULE_DEVICE_TABLE(acpi, sdm845_pinctrl_acpi_match);
#endif

static const struct of_device_id sdm845_pinctrl_of_match[] = {
	{ .compatible = "qcom,sdm845-pinctrl", },
	{ },
};

static struct platform_driver sdm845_pinctrl_driver = {
	.driver = {
		.name = "sdm845-pinctrl",
		.pm = &msm_pinctrl_dev_pm_ops,
		.of_match_table = sdm845_pinctrl_of_match,
		.acpi_match_table = ACPI_PTR(sdm845_pinctrl_acpi_match),
	},
	.probe = sdm845_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init sdm845_pinctrl_init(void)
{
	return platform_driver_register(&sdm845_pinctrl_driver);
}
arch_initcall(sdm845_pinctrl_init);

static void __exit sdm845_pinctrl_exit(void)
{
	platform_driver_unregister(&sdm845_pinctrl_driver);
}
module_exit(sdm845_pinctrl_exit);

MODULE_DESCRIPTION("QTI sdm845 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, sdm845_pinctrl_of_match);
