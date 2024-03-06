// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define REG_SIZE 0x1000

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{					        \
		.grp = PINCTRL_PINGROUP("gpio" #id,     \
			gpio##id##_pins,                \
			ARRAY_SIZE(gpio##id##_pins)),   \
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
		.ctl_reg = REG_SIZE * id,			\
		.io_reg = 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = 0x8 + REG_SIZE * id,		\
		.intr_status_reg = 0xc + REG_SIZE * id,	\
		.intr_target_reg = 0x8 + REG_SIZE * id,	\
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
	}

#define SDC_QDSD_PINGROUP(pg_name, ctl, pull, drv)	\
	{					        \
		.grp = PINCTRL_PINGROUP(#pg_name,       \
			pg_name##_pins,                 \
			ARRAY_SIZE(pg_name##_pins)),    \
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
		.grp = PINCTRL_PINGROUP(#pg_name,       \
			pg_name##_pins,                 \
			ARRAY_SIZE(pg_name##_pins)),    \
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


static const struct pinctrl_pin_desc sm4450_pins[] = {
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
	PINCTRL_PIN(136, "UFS_RESET"),
	PINCTRL_PIN(137, "SDC1_RCLK"),
	PINCTRL_PIN(138, "SDC1_CLK"),
	PINCTRL_PIN(139, "SDC1_CMD"),
	PINCTRL_PIN(140, "SDC1_DATA"),
	PINCTRL_PIN(141, "SDC2_CLK"),
	PINCTRL_PIN(142, "SDC2_CMD"),
	PINCTRL_PIN(143, "SDC2_DATA"),
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

static const unsigned int ufs_reset_pins[] = { 136 };
static const unsigned int sdc1_rclk_pins[] = { 137 };
static const unsigned int sdc1_clk_pins[] = { 138 };
static const unsigned int sdc1_cmd_pins[] = { 139 };
static const unsigned int sdc1_data_pins[] = { 140 };
static const unsigned int sdc2_clk_pins[] = { 141 };
static const unsigned int sdc2_cmd_pins[] = { 142 };
static const unsigned int sdc2_data_pins[] = { 143 };

enum sm4450_functions {
	msm_mux_gpio,
	msm_mux_atest_char,
	msm_mux_atest_usb0,
	msm_mux_audio_ref_clk,
	msm_mux_cam_mclk,
	msm_mux_cci_async_in0,
	msm_mux_cci_i2c,
	msm_mux_cci,
	msm_mux_cmu_rng,
	msm_mux_coex_uart1_rx,
	msm_mux_coex_uart1_tx,
	msm_mux_cri_trng,
	msm_mux_dbg_out_clk,
	msm_mux_ddr_bist,
	msm_mux_ddr_pxi0_test,
	msm_mux_ddr_pxi1_test,
	msm_mux_gcc_gp1_clk,
	msm_mux_gcc_gp2_clk,
	msm_mux_gcc_gp3_clk,
	msm_mux_host2wlan_sol,
	msm_mux_ibi_i3c_qup0,
	msm_mux_ibi_i3c_qup1,
	msm_mux_jitter_bist_ref,
	msm_mux_mdp_vsync0_out,
	msm_mux_mdp_vsync1_out,
	msm_mux_mdp_vsync2_out,
	msm_mux_mdp_vsync3_out,
	msm_mux_mdp_vsync,
	msm_mux_nav,
	msm_mux_pcie0_clk_req,
	msm_mux_phase_flag,
	msm_mux_pll_bist_sync,
	msm_mux_pll_clk_aux,
	msm_mux_prng_rosc,
	msm_mux_qdss_cti_trig0,
	msm_mux_qdss_cti_trig1,
	msm_mux_qdss_gpio,
	msm_mux_qlink0_enable,
	msm_mux_qlink0_request,
	msm_mux_qlink0_wmss_reset,
	msm_mux_qup0_se0,
	msm_mux_qup0_se1,
	msm_mux_qup0_se2,
	msm_mux_qup0_se3,
	msm_mux_qup0_se4,
	msm_mux_qup1_se0,
	msm_mux_qup1_se1,
	msm_mux_qup1_se2,
	msm_mux_qup1_se3,
	msm_mux_qup1_se4,
	msm_mux_sd_write_protect,
	msm_mux_tb_trig_sdc1,
	msm_mux_tb_trig_sdc2,
	msm_mux_tgu_ch0_trigout,
	msm_mux_tgu_ch1_trigout,
	msm_mux_tgu_ch2_trigout,
	msm_mux_tgu_ch3_trigout,
	msm_mux_tmess_prng,
	msm_mux_tsense_pwm1_out,
	msm_mux_tsense_pwm2_out,
	msm_mux_uim0,
	msm_mux_uim1,
	msm_mux_usb0_hs_ac,
	msm_mux_usb0_phy_ps,
	msm_mux_vfr_0_mira,
	msm_mux_vfr_0_mirb,
	msm_mux_vfr_1,
	msm_mux_vsense_trigger_mirnat,
	msm_mux_wlan1_adc_dtest0,
	msm_mux_wlan1_adc_dtest1,
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
	"gpio135",
};
static const char * const atest_char_groups[] = {
	"gpio95", "gpio97", "gpio98", "gpio99", "gpio100",
};
static const char * const atest_usb0_groups[] = {
	"gpio75", "gpio10", "gpio78", "gpio79", "gpio80",
};
static const char * const audio_ref_clk_groups[] = {
	"gpio71",
};
static const char * const cam_mclk_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};
static const char * const cci_async_in0_groups[] = {
	"gpio40",
};
static const char * const cci_i2c_groups[] = {
	"gpio45", "gpio47", "gpio49", "gpio44",
	"gpio46", "gpio48",
};
static const char * const cci_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43",
};
static const char * const cmu_rng_groups[] = {
	"gpio28", "gpio3", "gpio1", "gpio0",
};
static const char * const coex_uart1_rx_groups[] = {
	"gpio54",
};
static const char * const coex_uart1_tx_groups[] = {
	"gpio55",
};
static const char * const cri_trng_groups[] = {
	"gpio42", "gpio40", "gpio41",
};
static const char * const dbg_out_clk_groups[] = {
	"gpio80",
};
static const char * const ddr_bist_groups[] = {
	"gpio32", "gpio29", "gpio30", "gpio31",
};
static const char * const ddr_pxi0_test_groups[] = {
	"gpio90", "gpio127",
};
static const char * const ddr_pxi1_test_groups[] = {
	"gpio118", "gpio122",
};
static const char * const gcc_gp1_clk_groups[] = {
	"gpio37", "gpio48",
};
static const char * const gcc_gp2_clk_groups[] = {
	"gpio30", "gpio49",
};
static const char * const gcc_gp3_clk_groups[] = {
	"gpio3", "gpio50",
};
static const char * const host2wlan_sol_groups[] = {
	"gpio106",
};
static const char * const ibi_i3c_qup0_groups[] = {
	"gpio4", "gpio5",
};
static const char * const ibi_i3c_qup1_groups[] = {
	"gpio0", "gpio1",
};
static const char * const jitter_bist_ref_groups[] = {
	"gpio90",
};
static const char * const mdp_vsync0_out_groups[] = {
	"gpio93",
};
static const char * const mdp_vsync1_out_groups[] = {
	"gpio93",
};
static const char * const mdp_vsync2_out_groups[] = {
	"gpio22",
};
static const char * const mdp_vsync3_out_groups[] = {
	"gpio22",
};
static const char * const mdp_vsync_groups[] = {
	"gpio26", "gpio22", "gpio30", "gpio34", "gpio93", "gpio97",
};
static const char * const nav_groups[] = {
	"gpio81", "gpio83", "gpio84",
};
static const char * const pcie0_clk_req_groups[] = {
	"gpio107",
};
static const char * const phase_flag_groups[] = {
	"gpio7", "gpio8", "gpio9", "gpio11", "gpio13", "gpio14", "gpio15",
	"gpio17", "gpio18", "gpio19", "gpio21", "gpio24", "gpio25", "gpio31",
	"gpio32", "gpio33", "gpio35", "gpio61", "gpio72", "gpio82", "gpio91",
	"gpio95", "gpio97", "gpio98", "gpio99", "gpio100", "gpio105", "gpio115",
	"gpio116", "gpio117", "gpio133", "gpio135",
};
static const char * const pll_bist_sync_groups[] = {
	"gpio73",
};
static const char * const pll_clk_aux_groups[] = {
	"gpio108",
};
static const char * const prng_rosc_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};
static const char * const qdss_cti_trig0_groups[] = {
	"gpio26", "gpio60", "gpio113", "gpio114",
};
static const char * const qdss_cti_trig1_groups[] = {
	"gpio6", "gpio27", "gpio57", "gpio58",
};
static const char * const qdss_gpio_groups[] = {
	"gpio0", "gpio1", "gpio3", "gpio4", "gpio5", "gpio7", "gpio8",
	"gpio9", "gpio14", "gpio15", "gpio17", "gpio23", "gpio31", "gpio32",
	"gpio33", "gpio35", "gpio36", "gpio37", "gpio38", "gpio39", "gpio40",
	"gpio41", "gpio42", "gpio43", "gpio44", "gpio45", "gpio46", "gpio47",
	"gpio49",  "gpio59", "gpio62", "gpio118", "gpio121", "gpio122", "gpio126",
	"gpio127",
};
static const char * const qlink0_enable_groups[] = {
	"gpio88",
};
static const char * const qlink0_request_groups[] = {
	"gpio87",
};
static const char * const qlink0_wmss_reset_groups[] = {
	"gpio89",
};
static const char * const qup0_se0_groups[] = {
	"gpio4", "gpio5", "gpio34", "gpio35",
};
static const char * const qup0_se1_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13",
};
static const char * const qup0_se2_groups[] = {
	"gpio14", "gpio15", "gpio16", "gpio17",
};
static const char * const qup0_se3_groups[] = {
	"gpio18", "gpio19", "gpio20", "gpio21",
};
static const char * const qup0_se4_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9",
	"gpio26", "gpio27", "gpio34",
};
static const char * const qup1_se0_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",
};
static const char * const qup1_se1_groups[] = {
	"gpio26", "gpio27", "gpio50", "gpio51",
};
static const char * const qup1_se2_groups[] = {
	"gpio22", "gpio23", "gpio31", "gpio32",
};
static const char * const qup1_se3_groups[] = {
	"gpio24", "gpio25", "gpio51", "gpio50",
};
static const char * const qup1_se4_groups[] = {
	"gpio43", "gpio48", "gpio49", "gpio90",
	"gpio91",
};
static const char * const sd_write_protect_groups[] = {
	"gpio102",
};
static const char * const tb_trig_sdc1_groups[] = {
	"gpio128",
};
static const char * const tb_trig_sdc2_groups[] = {
	"gpio51",
};
static const char * const tgu_ch0_trigout_groups[] = {
	"gpio20",
};
static const char * const tgu_ch1_trigout_groups[] = {
	"gpio21",
};
static const char * const tgu_ch2_trigout_groups[] = {
	"gpio22",
};
static const char * const tgu_ch3_trigout_groups[] = {
	"gpio23",
};
static const char * const tmess_prng_groups[] = {
	"gpio57", "gpio58", "gpio59", "gpio60",
};
static const char * const tsense_pwm1_out_groups[] = {
	"gpio134",
};
static const char * const tsense_pwm2_out_groups[] = {
	"gpio134",
};
static const char * const uim0_groups[] = {
	"gpio64", "gpio63", "gpio66", "gpio65",
};
static const char * const uim1_groups[] = {
	"gpio68", "gpio67", "gpio69", "gpio70",
};
static const char * const usb0_hs_ac_groups[] = {
	"gpio99",
};
static const char * const usb0_phy_ps_groups[] = {
	"gpio94",
};
static const char * const vfr_0_mira_groups[] = {
	"gpio19",
};
static const char * const vfr_0_mirb_groups[] = {
	"gpio100",
};
static const char * const vfr_1_groups[] = {
	"gpio84",
};
static const char * const vsense_trigger_mirnat_groups[] = {
	"gpio75",
};
static const char * const wlan1_adc_dtest0_groups[] = {
	"gpio79",
};
static const char * const wlan1_adc_dtest1_groups[] = {
	"gpio80",
};

static const struct pinfunction sm4450_functions[] = {
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_usb0),
	MSM_PIN_FUNCTION(audio_ref_clk),
	MSM_PIN_FUNCTION(cam_mclk),
	MSM_PIN_FUNCTION(cci_async_in0),
	MSM_PIN_FUNCTION(cci_i2c),
	MSM_PIN_FUNCTION(cci),
	MSM_PIN_FUNCTION(cmu_rng),
	MSM_PIN_FUNCTION(coex_uart1_rx),
	MSM_PIN_FUNCTION(coex_uart1_tx),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(dbg_out_clk),
	MSM_PIN_FUNCTION(ddr_bist),
	MSM_PIN_FUNCTION(ddr_pxi0_test),
	MSM_PIN_FUNCTION(ddr_pxi1_test),
	MSM_PIN_FUNCTION(gcc_gp1_clk),
	MSM_PIN_FUNCTION(gcc_gp2_clk),
	MSM_PIN_FUNCTION(gcc_gp3_clk),
	MSM_PIN_FUNCTION(host2wlan_sol),
	MSM_PIN_FUNCTION(ibi_i3c_qup0),
	MSM_PIN_FUNCTION(ibi_i3c_qup1),
	MSM_PIN_FUNCTION(jitter_bist_ref),
	MSM_PIN_FUNCTION(mdp_vsync0_out),
	MSM_PIN_FUNCTION(mdp_vsync1_out),
	MSM_PIN_FUNCTION(mdp_vsync2_out),
	MSM_PIN_FUNCTION(mdp_vsync3_out),
	MSM_PIN_FUNCTION(mdp_vsync),
	MSM_PIN_FUNCTION(nav),
	MSM_PIN_FUNCTION(pcie0_clk_req),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(pll_bist_sync),
	MSM_PIN_FUNCTION(pll_clk_aux),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(qdss_cti_trig0),
	MSM_PIN_FUNCTION(qdss_cti_trig1),
	MSM_PIN_FUNCTION(qdss_gpio),
	MSM_PIN_FUNCTION(qlink0_enable),
	MSM_PIN_FUNCTION(qlink0_request),
	MSM_PIN_FUNCTION(qlink0_wmss_reset),
	MSM_PIN_FUNCTION(qup0_se0),
	MSM_PIN_FUNCTION(qup0_se1),
	MSM_PIN_FUNCTION(qup0_se2),
	MSM_PIN_FUNCTION(qup0_se3),
	MSM_PIN_FUNCTION(qup0_se4),
	MSM_PIN_FUNCTION(qup1_se0),
	MSM_PIN_FUNCTION(qup1_se1),
	MSM_PIN_FUNCTION(qup1_se2),
	MSM_PIN_FUNCTION(qup1_se3),
	MSM_PIN_FUNCTION(qup1_se4),
	MSM_PIN_FUNCTION(sd_write_protect),
	MSM_PIN_FUNCTION(tb_trig_sdc1),
	MSM_PIN_FUNCTION(tb_trig_sdc2),
	MSM_PIN_FUNCTION(tgu_ch0_trigout),
	MSM_PIN_FUNCTION(tgu_ch1_trigout),
	MSM_PIN_FUNCTION(tgu_ch2_trigout),
	MSM_PIN_FUNCTION(tgu_ch3_trigout),
	MSM_PIN_FUNCTION(tmess_prng),
	MSM_PIN_FUNCTION(tsense_pwm1_out),
	MSM_PIN_FUNCTION(tsense_pwm2_out),
	MSM_PIN_FUNCTION(uim0),
	MSM_PIN_FUNCTION(uim1),
	MSM_PIN_FUNCTION(usb0_hs_ac),
	MSM_PIN_FUNCTION(usb0_phy_ps),
	MSM_PIN_FUNCTION(vfr_0_mira),
	MSM_PIN_FUNCTION(vfr_0_mirb),
	MSM_PIN_FUNCTION(vfr_1),
	MSM_PIN_FUNCTION(vsense_trigger_mirnat),
	MSM_PIN_FUNCTION(wlan1_adc_dtest0),
	MSM_PIN_FUNCTION(wlan1_adc_dtest1),
};

/*
 * Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup sm4450_groups[] = {
	[0] = PINGROUP(0, qup1_se0, ibi_i3c_qup1, cmu_rng, qdss_gpio, _, _, _, _, _),
	[1] = PINGROUP(1, qup1_se0, ibi_i3c_qup1, cmu_rng, qdss_gpio, _, _, _, _, _),
	[2] = PINGROUP(2, qup1_se0, _, _, _, _, _, _, _, _),
	[3] = PINGROUP(3, qup1_se0, gcc_gp3_clk, cmu_rng, qdss_gpio, _, _, _, _, _),
	[4] = PINGROUP(4, qup0_se0, ibi_i3c_qup0, qdss_gpio, _, _, _, _, _, _),
	[5] = PINGROUP(5, qup0_se0, ibi_i3c_qup0, qdss_gpio, _, _, _, _, _, _),
	[6] = PINGROUP(6, qup0_se4, qdss_cti_trig1, _, _, _, _, _, _, _),
	[7] = PINGROUP(7, qup0_se4, _, phase_flag, qdss_gpio, _, _, _, _, _),
	[8] = PINGROUP(8, qup0_se4, _, phase_flag, qdss_gpio, _, _, _, _, _),
	[9] = PINGROUP(9, qup0_se4, _, phase_flag, qdss_gpio, _, _, _, _, _),
	[10] = PINGROUP(10, qup0_se1, _, atest_usb0, _, _, _, _, _, _),
	[11] = PINGROUP(11, qup0_se1, _, phase_flag, _, _, _, _, _, _),
	[12] = PINGROUP(12, qup0_se1, _, _, _, _, _, _, _, _),
	[13] = PINGROUP(13, qup0_se1, _, phase_flag, _, _, _, _, _, _),
	[14] = PINGROUP(14, qup0_se2, _, phase_flag, _, qdss_gpio, _, _, _, _),
	[15] = PINGROUP(15, qup0_se2, _, phase_flag, _, qdss_gpio, _, _, _, _),
	[16] = PINGROUP(16, qup0_se2, _, _, _, _, _, _, _, _),
	[17] = PINGROUP(17, qup0_se2, _, phase_flag, _, qdss_gpio, _, _, _, _),
	[18] = PINGROUP(18, qup0_se3, _, phase_flag, _, _, _, _, _, _),
	[19] = PINGROUP(19, qup0_se3, vfr_0_mira, _, phase_flag, _, _, _, _, _),
	[20] = PINGROUP(20, qup0_se3, tgu_ch0_trigout, _, _, _, _, _, _, _),
	[21] = PINGROUP(21, qup0_se3, _, phase_flag, tgu_ch1_trigout, _, _, _, _, _),
	[22] = PINGROUP(22, qup1_se2, mdp_vsync, mdp_vsync2_out, mdp_vsync3_out, tgu_ch2_trigout, _, _, _, _),
	[23] = PINGROUP(23, qup1_se2, tgu_ch3_trigout, qdss_gpio, _, _, _, _, _, _),
	[24] = PINGROUP(24, qup1_se3, _, phase_flag, _, _, _, _, _, _),
	[25] = PINGROUP(25, qup1_se3, _, phase_flag, _, _, _, _, _, _),
	[26] = PINGROUP(26, qup1_se1, mdp_vsync, qup0_se4, qdss_cti_trig0, _, _, _, _, _),
	[27] = PINGROUP(27, qup1_se1, qup0_se4, qdss_cti_trig1, _, _, _, _, _, _),
	[28] = PINGROUP(28, cmu_rng, _, _, _, _, _, _, _, _),
	[29] = PINGROUP(29, ddr_bist, _, _, _, _, _, _, _, _),
	[30] = PINGROUP(30, mdp_vsync, gcc_gp2_clk, ddr_bist, _, _, _, _, _, _),
	[31] = PINGROUP(31, qup1_se2, _, phase_flag, ddr_bist, qdss_gpio, _, _, _, _),
	[32] = PINGROUP(32, qup1_se2, _, phase_flag, ddr_bist, qdss_gpio, _, _, _, _),
	[33] = PINGROUP(33, _, phase_flag, qdss_gpio, _, _, _, _, _, _),
	[34] = PINGROUP(34, qup0_se0, qup0_se4, mdp_vsync, _, _, _, _, _, _),
	[35] = PINGROUP(35, qup0_se0, _, phase_flag, qdss_gpio, _, _, _, _, _),
	[36] = PINGROUP(36, cam_mclk, prng_rosc, qdss_gpio, _, _, _, _, _, _),
	[37] = PINGROUP(37, cam_mclk, gcc_gp1_clk, prng_rosc, qdss_gpio, _, _, _, _, _),
	[38] = PINGROUP(38, cam_mclk, prng_rosc, qdss_gpio, _, _, _, _, _, _),
	[39] = PINGROUP(39, cam_mclk, prng_rosc, qdss_gpio, _, _, _, _, _, _),
	[40] = PINGROUP(40, cci, cci_async_in0, cri_trng, qdss_gpio, _, _, _, _, _),
	[41] = PINGROUP(41, cci, cri_trng, qdss_gpio, _, _, _, _, _, _),
	[42] = PINGROUP(42, cci, cri_trng, qdss_gpio, _, _, _, _, _, _),
	[43] = PINGROUP(43, cci, qup1_se4, qdss_gpio, _, _, _, _, _, _),
	[44] = PINGROUP(44, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[45] = PINGROUP(45, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[46] = PINGROUP(46, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[47] = PINGROUP(47, cci_i2c, qdss_gpio, _, _, _, _, _, _, _),
	[48] = PINGROUP(48, cci_i2c, qup1_se4, gcc_gp1_clk, _, _, _, _, _, _),
	[49] = PINGROUP(49, cci_i2c, qup1_se4, gcc_gp2_clk, qdss_gpio, _, _, _, _, _),
	[50] = PINGROUP(50, qup1_se1, qup1_se3, _, gcc_gp3_clk, _, _, _, _, _),
	[51] = PINGROUP(51, qup1_se1, qup1_se3, _, tb_trig_sdc2, _, _, _, _, _),
	[52] = PINGROUP(52, _, _, _, _, _, _, _, _, _),
	[53] = PINGROUP(53, _, _, _, _, _, _, _, _, _),
	[54] = PINGROUP(54, coex_uart1_rx, _, _, _, _, _, _, _, _),
	[55] = PINGROUP(55, coex_uart1_tx, _, _, _, _, _, _, _, _),
	[56] = PINGROUP(56, _, _, _, _, _, _, _, _, _),
	[57] = PINGROUP(57, tmess_prng, qdss_cti_trig1, _, _, _, _, _, _, _),
	[58] = PINGROUP(58, tmess_prng, qdss_cti_trig1, _, _, _, _, _, _, _),
	[59] = PINGROUP(59, tmess_prng, qdss_gpio, _, _, _, _, _, _, _),
	[60] = PINGROUP(60, tmess_prng, qdss_cti_trig0, _, _, _, _, _, _, _),
	[61] = PINGROUP(61, _, phase_flag, _, _, _, _, _, _, _),
	[62] = PINGROUP(62, qdss_gpio, _, _, _, _, _, _, _, _),
	[63] = PINGROUP(63, uim0, _, _, _, _, _, _, _, _),
	[64] = PINGROUP(64, uim0, _, _, _, _, _, _, _, _),
	[65] = PINGROUP(65, uim0, _, _, _, _, _, _, _, _),
	[66] = PINGROUP(66, uim0, _, _, _, _, _, _, _, _),
	[67] = PINGROUP(67, uim1, _, _, _, _, _, _, _, _),
	[68] = PINGROUP(68, uim1, _, _, _, _, _, _, _, _),
	[69] = PINGROUP(69, uim1, _, _, _, _, _, _, _, _),
	[70] = PINGROUP(70, uim1, _, _, _, _, _, _, _, _),
	[71] = PINGROUP(71, _, _, _, audio_ref_clk, _, _, _, _, _),
	[72] = PINGROUP(72, _, _, _, phase_flag, _, _, _, _, _),
	[73] = PINGROUP(73, _, _, _, pll_bist_sync, _, _, _, _, _),
	[74] = PINGROUP(74, _, _, _, _, _, _, _, _, _),
	[75] = PINGROUP(75, _, _, _, vsense_trigger_mirnat, atest_usb0, _, _, _, _),
	[76] = PINGROUP(76, _, _, _, _, _, _, _, _, _),
	[77] = PINGROUP(77, _, _, _, _, _, _, _, _, _),
	[78] = PINGROUP(78, _, _, _, atest_usb0, _, _, _, _, _),
	[79] = PINGROUP(79, _, _, _, wlan1_adc_dtest0, atest_usb0, _, _, _, _),
	[80] = PINGROUP(80, _, _, dbg_out_clk, wlan1_adc_dtest1, atest_usb0, _, _, _, _),
	[81] = PINGROUP(81, _, nav, _, _, _, _, _, _, _),
	[82] = PINGROUP(82, _, _, phase_flag, _, _, _, _, _, _),
	[83] = PINGROUP(83, nav, _, _, _, _, _, _, _, _),
	[84] = PINGROUP(84, nav, vfr_1, _, _, _, _, _, _, _),
	[85] = PINGROUP(85, _, _, _, _, _, _, _, _, _),
	[86] = PINGROUP(86, _, _, _, _, _, _, _, _, _),
	[87] = PINGROUP(87, qlink0_request, _, _, _, _, _, _, _, _),
	[88] = PINGROUP(88, qlink0_enable, _, _, _, _, _, _, _, _),
	[89] = PINGROUP(89, qlink0_wmss_reset, _, _, _, _, _, _, _, _),
	[90] = PINGROUP(90, qup1_se4, jitter_bist_ref, ddr_pxi0_test, _, _, _, _, _, _),
	[91] = PINGROUP(91, qup1_se4, _, phase_flag, _, _, _, _, _, _),
	[92] = PINGROUP(92, _, _, _, _, _, _, _, _, _),
	[93] = PINGROUP(93, mdp_vsync, mdp_vsync0_out, mdp_vsync1_out, _, _, _, _, _, _),
	[94] = PINGROUP(94, usb0_phy_ps, _, _, _, _, _, _, _, _),
	[95] = PINGROUP(95, _, phase_flag, atest_char, _, _, _, _, _, _),
	[96] = PINGROUP(96, _, _, _, _, _, _, _, _, _),
	[97] = PINGROUP(97, mdp_vsync, _, phase_flag, atest_char, _, _, _, _, _),
	[98] = PINGROUP(98, _, phase_flag, atest_char, _, _, _, _, _, _),
	[99] = PINGROUP(99, usb0_hs_ac, _, phase_flag, atest_char, _, _, _, _, _),
	[100] = PINGROUP(100, vfr_0_mirb, _, phase_flag, atest_char, _, _, _, _, _),
	[101] = PINGROUP(101, _, _, _, _, _, _, _, _, _),
	[102] = PINGROUP(102, sd_write_protect, _, _, _, _, _, _, _, _),
	[103] = PINGROUP(103, _, _, _, _, _, _, _, _, _),
	[104] = PINGROUP(104, _, _, _, _, _, _, _, _, _),
	[105] = PINGROUP(105, _, phase_flag, _, _, _, _, _, _, _),
	[106] = PINGROUP(106, host2wlan_sol, _, _, _, _, _, _, _, _),
	[107] = PINGROUP(107, pcie0_clk_req, _, _, _, _, _, _, _, _),
	[108] = PINGROUP(108, pll_clk_aux, _, _, _, _, _, _, _, _),
	[109] = PINGROUP(109, _, _, _, _, _, _, _, _, _),
	[110] = PINGROUP(110, _, _, _, _, _, _, _, _, _),
	[111] = PINGROUP(111, _, _, _, _, _, _, _, _, _),
	[112] = PINGROUP(112, _, _, _, _, _, _, _, _, _),
	[113] = PINGROUP(113, qdss_cti_trig0, _, _, _, _, _, _, _, _),
	[114] = PINGROUP(114, qdss_cti_trig0, _, _, _, _, _, _, _, _),
	[115] = PINGROUP(115, _, phase_flag, _, _, _, _, _, _, _),
	[116] = PINGROUP(116, _, phase_flag, _, _, _, _, _, _, _),
	[117] = PINGROUP(117, _, phase_flag, _, _, _, _, _, _, _),
	[118] = PINGROUP(118, qdss_gpio, _, ddr_pxi1_test, _, _, _, _, _, _),
	[119] = PINGROUP(119, _, _, _, _, _, _, _, _, _),
	[120] = PINGROUP(120, _, _, _, _, _, _, _, _, _),
	[121] = PINGROUP(121, qdss_gpio, _, _, _, _, _, _, _, _),
	[122] = PINGROUP(122, qdss_gpio, _, ddr_pxi1_test, _, _, _, _, _, _),
	[123] = PINGROUP(123, _, _, _, _, _, _, _, _, _),
	[124] = PINGROUP(124, _, _, _, _, _, _, _, _, _),
	[125] = PINGROUP(125, _, _, _, _, _, _, _, _, _),
	[126] = PINGROUP(126, qdss_gpio, _, _, _, _, _, _, _, _),
	[127] = PINGROUP(127, qdss_gpio, ddr_pxi0_test, _, _, _, _, _, _, _),
	[128] = PINGROUP(128, tb_trig_sdc1, _, _, _, _, _, _, _, _),
	[129] = PINGROUP(129, _, _, _, _, _, _, _, _, _),
	[130] = PINGROUP(130, _, _, _, _, _, _, _, _, _),
	[131] = PINGROUP(131, _, _, _, _, _, _, _, _, _),
	[132] = PINGROUP(132, _, _, _, _, _, _, _, _, _),
	[133] = PINGROUP(133, _, phase_flag, _, _, _, _, _, _, _),
	[134] = PINGROUP(134, tsense_pwm1_out, tsense_pwm2_out, _, _, _, _, _, _, _),
	[135] = PINGROUP(135, _, phase_flag, _, _, _, _, _, _, _),
	[136] = UFS_RESET(ufs_reset, 0x97000),
	[137] = SDC_QDSD_PINGROUP(sdc1_rclk, 0x8c004, 0, 0),
	[138] = SDC_QDSD_PINGROUP(sdc1_clk, 0x8c000, 13, 6),
	[139] = SDC_QDSD_PINGROUP(sdc1_cmd, 0x8c000, 11, 3),
	[140] = SDC_QDSD_PINGROUP(sdc1_data, 0x8c000, 9, 0),
	[141] = SDC_QDSD_PINGROUP(sdc2_clk, 0x8f000, 14, 6),
	[142] = SDC_QDSD_PINGROUP(sdc2_cmd, 0x8f000, 11, 3),
	[143] = SDC_QDSD_PINGROUP(sdc2_data, 0x8f000, 9, 0),
};

static const struct msm_gpio_wakeirq_map sm4450_pdc_map[] = {
	{ 0, 67 }, { 3, 82 }, { 4, 69 }, { 5, 70 }, { 6, 44 }, { 7, 43 },
	{ 8, 71 }, { 9, 86 }, { 10, 48 }, { 11, 77 }, { 12, 90 },
	{ 13, 54 }, { 14, 91 }, { 17, 97 }, { 18, 102 }, { 21, 103 },
	{ 22, 104 }, { 23, 105 }, { 24, 53 }, { 25, 106 }, { 26, 65 },
	{ 27, 55 }, { 28, 89 }, { 30, 80 }, { 31, 109 }, { 33, 87 },
	{ 34, 81 }, { 35, 75 }, { 40, 88 }, { 41, 98 }, { 42, 110 },
	{ 43, 95 }, { 47, 118 }, { 50, 111 }, { 52, 52 }, { 53, 114 },
	{ 54, 115 }, { 55, 99 }, { 56, 45 }, { 57, 85 }, { 58, 56 },
	{ 59, 84 }, { 60, 83 }, { 61, 96 }, { 62, 93 }, { 66, 116 },
	{ 67, 113 }, { 70, 42 }, { 71, 122 }, { 73, 119 }, { 75, 121 },
	{ 77, 120 }, { 79, 123 }, { 81, 124 }, { 83, 64 }, { 84, 128 },
	{ 86, 129 }, { 87, 63 }, { 91, 92 }, { 92, 66 }, { 93, 125 },
	{ 94, 76 }, { 95, 62 }, { 96, 132 }, { 97, 135 }, { 98, 73 },
	{ 99, 133 }, { 101, 46 }, { 102, 134 }, { 103, 49 }, { 105, 58 },
	{ 107, 94 }, { 110, 59 }, { 113, 57 }, { 114, 60 }, { 118, 107 },
	{ 120, 61 }, { 121, 108 }, { 123, 68 }, { 125, 72 }, { 128, 112 },
};

static const struct msm_pinctrl_soc_data sm4450_tlmm = {
	.pins = sm4450_pins,
	.npins = ARRAY_SIZE(sm4450_pins),
	.functions = sm4450_functions,
	.nfunctions = ARRAY_SIZE(sm4450_functions),
	.groups = sm4450_groups,
	.ngroups = ARRAY_SIZE(sm4450_groups),
	.ngpios = 137,
	.wakeirq_map = sm4450_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(sm4450_pdc_map),
};

static int sm4450_tlmm_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &sm4450_tlmm);
}

static const struct of_device_id sm4450_tlmm_of_match[] = {
	{ .compatible = "qcom,sm4450-tlmm", },
	{ }
};

static struct platform_driver sm4450_tlmm_driver = {
	.driver = {
		.name = "sm4450-tlmm",
		.of_match_table = sm4450_tlmm_of_match,
	},
	.probe = sm4450_tlmm_probe,
	.remove_new = msm_pinctrl_remove,
};
MODULE_DEVICE_TABLE(of, sm4450_tlmm_of_match);

static int __init sm4450_tlmm_init(void)
{
	return platform_driver_register(&sm4450_tlmm_driver);
}
arch_initcall(sm4450_tlmm_init);

static void __exit sm4450_tlmm_exit(void)
{
	platform_driver_unregister(&sm4450_tlmm_driver);
}
module_exit(sm4450_tlmm_exit);

MODULE_DESCRIPTION("QTI SM4450 TLMM driver");
MODULE_LICENSE("GPL");
