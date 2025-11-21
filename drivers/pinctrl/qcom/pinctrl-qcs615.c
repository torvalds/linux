// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

enum {
	SOUTH,
	EAST,
	WEST
};

static const char * const qcs615_tiles[] = {
	[SOUTH] = "south",
	[EAST] = "east",
	[WEST] = "west"
};

#define PINGROUP(id, _tile, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{						\
		.grp = PINCTRL_PINGROUP("gpio" #id,	\
			gpio##id##_pins,		\
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
		.ctl_reg = 0x1000 * id,		\
		.io_reg = 0x1000 * id + 0x4,		\
		.intr_cfg_reg = 0x1000 * id + 0x8,	\
		.intr_status_reg = 0x1000 * id + 0xc,	\
		.intr_target_reg = 0x1000 * id + 0x8,	\
		.tile = _tile,			\
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

#define SDC_QDSD_PINGROUP(pg_name, _tile, ctl, pull, drv)	\
	{						\
		.grp = PINCTRL_PINGROUP(#pg_name,	\
			pg_name##_pins,			\
			ARRAY_SIZE(pg_name##_pins)),	\
		.ctl_reg = ctl,				\
		.io_reg = 0,				\
		.intr_cfg_reg = 0,			\
		.intr_status_reg = 0,			\
		.intr_target_reg = 0,			\
		.tile = _tile,				\
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

#define UFS_RESET(pg_name, offset)			\
	{						\
		.grp = PINCTRL_PINGROUP(#pg_name,	\
			pg_name##_pins,			\
			ARRAY_SIZE(pg_name##_pins)),	\
		.ctl_reg = offset,			\
		.io_reg = offset + 0x4,			\
		.intr_cfg_reg = 0,			\
		.intr_status_reg = 0,			\
		.intr_target_reg = 0,			\
		.tile = WEST,				\
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

static const struct pinctrl_pin_desc qcs615_pins[] = {
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
	PINCTRL_PIN(123, "UFS_RESET"),
	PINCTRL_PIN(124, "SDC1_RCLK"),
	PINCTRL_PIN(125, "SDC1_CLK"),
	PINCTRL_PIN(126, "SDC1_CMD"),
	PINCTRL_PIN(127, "SDC1_DATA"),
	PINCTRL_PIN(128, "SDC2_CLK"),
	PINCTRL_PIN(129, "SDC2_CMD"),
	PINCTRL_PIN(130, "SDC2_DATA"),
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

static const unsigned int ufs_reset_pins[] = { 123 };
static const unsigned int sdc1_rclk_pins[] = { 124 };
static const unsigned int sdc1_clk_pins[] = { 125 };
static const unsigned int sdc1_cmd_pins[] = { 126 };
static const unsigned int sdc1_data_pins[] = { 127 };
static const unsigned int sdc2_clk_pins[] = { 128 };
static const unsigned int sdc2_cmd_pins[] = { 129 };
static const unsigned int sdc2_data_pins[] = { 130 };

enum qcs615_functions {
	msm_mux_gpio,
	msm_mux_adsp_ext,
	msm_mux_agera_pll,
	msm_mux_aoss_cti,
	msm_mux_atest_char,
	msm_mux_atest_tsens,
	msm_mux_atest_usb,
	msm_mux_cam_mclk,
	msm_mux_cci_async,
	msm_mux_cci_i2c,
	msm_mux_cci_timer,
	msm_mux_copy_gp,
	msm_mux_copy_phase,
	msm_mux_cri_trng,
	msm_mux_dbg_out_clk,
	msm_mux_ddr_bist,
	msm_mux_ddr_pxi,
	msm_mux_dp_hot,
	msm_mux_edp_hot,
	msm_mux_edp_lcd,
	msm_mux_emac_gcc,
	msm_mux_emac_phy_intr,
	msm_mux_forced_usb,
	msm_mux_gcc_gp,
	msm_mux_gp_pdm,
	msm_mux_gps_tx,
	msm_mux_hs0_mi2s,
	msm_mux_hs1_mi2s,
	msm_mux_jitter_bist,
	msm_mux_ldo_en,
	msm_mux_ldo_update,
	msm_mux_m_voc,
	msm_mux_mclk1,
	msm_mux_mclk2,
	msm_mux_mdp_vsync,
	msm_mux_mdp_vsync0_out,
	msm_mux_mdp_vsync1_out,
	msm_mux_mdp_vsync2_out,
	msm_mux_mdp_vsync3_out,
	msm_mux_mdp_vsync4_out,
	msm_mux_mdp_vsync5_out,
	msm_mux_mi2s_1,
	msm_mux_mss_lte,
	msm_mux_nav_pps_in,
	msm_mux_nav_pps_out,
	msm_mux_pa_indicator_or,
	msm_mux_pcie_clk_req,
	msm_mux_pcie_ep_rst,
	msm_mux_phase_flag,
	msm_mux_pll_bist,
	msm_mux_pll_bypassnl,
	msm_mux_pll_reset_n,
	msm_mux_prng_rosc,
	msm_mux_qdss_cti,
	msm_mux_qdss_gpio,
	msm_mux_qlink_enable,
	msm_mux_qlink_request,
	msm_mux_qspi,
	msm_mux_qup0,
	msm_mux_qup1,
	msm_mux_rgmii,
	msm_mux_sd_write_protect,
	msm_mux_sp_cmu,
	msm_mux_ter_mi2s,
	msm_mux_tgu_ch,
	msm_mux_uim1,
	msm_mux_uim2,
	msm_mux_usb0_hs,
	msm_mux_usb1_hs,
	msm_mux_usb_phy_ps,
	msm_mux_vfr_1,
	msm_mux_vsense_trigger_mirnat,
	msm_mux_wlan,
	msm_mux_wsa_clk,
	msm_mux_wsa_data,
	msm_mux__,
};

static const char *const gpio_groups[] = {
	"gpio0",   "gpio1",   "gpio2",   "gpio3",   "gpio4",   "gpio5",
	"gpio6",   "gpio7",   "gpio8",   "gpio9",   "gpio10",  "gpio11",
	"gpio12",  "gpio13",  "gpio14",  "gpio15",  "gpio16",  "gpio17",
	"gpio18",  "gpio19",  "gpio20",  "gpio21",  "gpio22",  "gpio23",
	"gpio24",  "gpio25",  "gpio26",  "gpio27",  "gpio28",  "gpio29",
	"gpio30",  "gpio31",  "gpio32",  "gpio33",  "gpio34",  "gpio35",
	"gpio36",  "gpio37",  "gpio38",  "gpio39",  "gpio40",  "gpio41",
	"gpio42",  "gpio43",  "gpio44",  "gpio45",  "gpio46",  "gpio47",
	"gpio48",  "gpio49",  "gpio50",  "gpio51",  "gpio52",  "gpio53",
	"gpio54",  "gpio55",  "gpio56",  "gpio57",  "gpio58",  "gpio59",
	"gpio60",  "gpio61",  "gpio62",  "gpio63",  "gpio64",  "gpio65",
	"gpio66",  "gpio67",  "gpio68",  "gpio69",  "gpio70",  "gpio71",
	"gpio72",  "gpio73",  "gpio74",  "gpio75",  "gpio76",  "gpio77",
	"gpio78",  "gpio79",  "gpio80",  "gpio81",  "gpio82",  "gpio83",
	"gpio84",  "gpio85",  "gpio86",  "gpio87",  "gpio88",  "gpio89",
	"gpio90",  "gpio91",  "gpio92",  "gpio93",  "gpio94",  "gpio95",
	"gpio96",  "gpio97",  "gpio98",  "gpio99",  "gpio100", "gpio101",
	"gpio102", "gpio103", "gpio104", "gpio105", "gpio106", "gpio107",
	"gpio108", "gpio109", "gpio110", "gpio111", "gpio112", "gpio113",
	"gpio114", "gpio115", "gpio116", "gpio117", "gpio118", "gpio119",
	"gpio120", "gpio121", "gpio122",
};

static const char *const adsp_ext_groups[] = {
	"gpio118",
};

static const char *const agera_pll_groups[] = {
	"gpio28",
};

static const char *const aoss_cti_groups[] = {
	"gpio76",
};

static const char *const atest_char_groups[] = {
	"gpio84",  "gpio85",  "gpio86",  "gpio87",
	"gpio115", "gpio117", "gpio118", "gpio119",
	"gpio120", "gpio121",
};

static const char *const atest_tsens_groups[] = {
	"gpio7", "gpio29",
};

static const char *const atest_usb_groups[] = {
	"gpio7",  "gpio10", "gpio11", "gpio54",
	"gpio55", "gpio67", "gpio68", "gpio76",
	"gpio75", "gpio77",
};

static const char *const cam_mclk_groups[] = {
	"gpio28", "gpio29", "gpio30", "gpio31",
};

static const char *const cci_async_groups[] = {
	"gpio26", "gpio41", "gpio42",
};

static const char *const cci_i2c_groups[] = {
	"gpio32", "gpio33", "gpio34", "gpio35",
};

static const char *const cci_timer_groups[] = {
	"gpio37", "gpio38", "gpio39", "gpio41",
	"gpio42",
};

static const char *const copy_gp_groups[] = {
	"gpio86",
};

static const char *const copy_phase_groups[] = {
	"gpio103",
};

static const char *const cri_trng_groups[] = {
	"gpio60", "gpio61", "gpio62",
};

static const char *const dbg_out_clk_groups[] = {
	"gpio11",
};

static const char *const ddr_bist_groups[] = {
	"gpio7", "gpio8", "gpio9", "gpio10",
};

static const char *const ddr_pxi_groups[] = {
	"gpio6",  "gpio7",  "gpio10", "gpio11",
	"gpio12", "gpio13", "gpio54", "gpio55",
};

static const char *const dp_hot_groups[] = {
	"gpio102", "gpio103", "gpio104",
};

static const char *const edp_hot_groups[] = {
	"gpio113",
};

static const char *const edp_lcd_groups[] = {
	"gpio119",
};

static const char *const emac_gcc_groups[] = {
	"gpio101", "gpio102",
};

static const char *const emac_phy_intr_groups[] = {
	"gpio89",
};

static const char *const forced_usb_groups[] = {
	"gpio43",
};

static const char *const gcc_gp_groups[] = {
	"gpio21", "gpio22", "gpio57", "gpio58",
	"gpio59", "gpio78",
};

static const char *const gp_pdm_groups[] = {
	"gpio8", "gpio54", "gpio63", "gpio66",
	"gpio79", "gpio95",
};

static const char *const gps_tx_groups[] = {
	"gpio53", "gpio54", "gpio56", "gpio57",
	"gpio59", "gpio60",
};

static const char *const hs0_mi2s_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};

static const char *const hs1_mi2s_groups[] = {
	"gpio24", "gpio25", "gpio26", "gpio27",
};

static const char *const jitter_bist_groups[] = {
	"gpio12", "gpio26",
};

static const char *const ldo_en_groups[] = {
	"gpio97",
};

static const char *const ldo_update_groups[] = {
	"gpio98",
};

static const char *const m_voc_groups[] = {
	"gpio120",
};

static const char *const mclk1_groups[] = {
	"gpio121",
};

static const char *const mclk2_groups[] = {
	"gpio122",
};

static const char *const mdp_vsync_groups[] = {
	"gpio81", "gpio82", "gpio83", "gpio90",
	"gpio97", "gpio98",
};

static const char *const mdp_vsync0_out_groups[] = {
	"gpio90",
};

static const char *const mdp_vsync1_out_groups[] = {
	"gpio90",
};

static const char *const mdp_vsync2_out_groups[] = {
	"gpio90",
};

static const char *const mdp_vsync3_out_groups[] = {
	"gpio90",
};

static const char *const mdp_vsync4_out_groups[] = {
	"gpio90",
};

static const char *const mdp_vsync5_out_groups[] = {
	"gpio90",
};

static const char *const mi2s_1_groups[] = {
	"gpio108", "gpio109", "gpio110", "gpio111",
};

static const char *const mss_lte_groups[] = {
	"gpio106", "gpio107",
};

static const char *const nav_pps_in_groups[] = {
	"gpio53", "gpio56", "gpio57", "gpio59",
	"gpio60",
};

static const char *const nav_pps_out_groups[] = {
	"gpio53", "gpio56", "gpio57", "gpio59",
	"gpio60",
};

static const char *const pa_indicator_or_groups[] = {
	"gpio53",
};

static const char *const pcie_clk_req_groups[] = {
	"gpio90",
};

static const char *const pcie_ep_rst_groups[] = {
	"gpio89",
};

static const char *const phase_flag_groups[] = {
	"gpio10",  "gpio18",  "gpio19",  "gpio20",
	"gpio23",  "gpio24",  "gpio25",  "gpio38",
	"gpio40",  "gpio41",  "gpio42",  "gpio43",
	"gpio44",  "gpio45",  "gpio53",  "gpio54",
	"gpio55",  "gpio67",  "gpio68",  "gpio75",
	"gpio76",  "gpio77",  "gpio78",  "gpio79",
	"gpio80",  "gpio82",  "gpio84",  "gpio92",
	"gpio116", "gpio117", "gpio118", "gpio119",
};

static const char *const pll_bist_groups[] = {
	"gpio27",
};

static const char *const pll_bypassnl_groups[] = {
	"gpio13",
};

static const char *const pll_reset_n_groups[] = {
	"gpio14",
};

static const char *const prng_rosc_groups[] = {
	"gpio99", "gpio102",
};

static const char *const qdss_cti_groups[] = {
	"gpio83",  "gpio96",  "gpio97",  "gpio98",
	"gpio103", "gpio104", "gpio112", "gpio113",
};

static const char *const qdss_gpio_groups[] = {
	"gpio0",   "gpio1",   "gpio2",   "gpio3",
	"gpio6",   "gpio7",   "gpio8",   "gpio9",
	"gpio14",  "gpio15",  "gpio20",  "gpio21",
	"gpio28",  "gpio29",  "gpio30",  "gpio31",
	"gpio32",  "gpio33",  "gpio34",  "gpio35",
	"gpio44",  "gpio45",  "gpio46",  "gpio47",
	"gpio81",  "gpio82",  "gpio92",  "gpio93",
	"gpio94",  "gpio95",  "gpio108", "gpio109",
	"gpio117", "gpio118", "gpio119", "gpio120",
};

static const char *const qlink_enable_groups[] = {
	"gpio52",
};

static const char *const qlink_request_groups[] = {
	"gpio51",
};

static const char *const qspi_groups[] = {
	"gpio44", "gpio45", "gpio46", "gpio47",
	"gpio48", "gpio49", "gpio50",
};

static const char *const qup0_groups[] = {
	"gpio0",  "gpio1",  "gpio2",  "gpio3",
	"gpio4",  "gpio5",  "gpio16", "gpio17",
	"gpio18", "gpio19",
};

static const char *const qup1_groups[] = {
	"gpio6",  "gpio7",  "gpio8",  "gpio9",
	"gpio10", "gpio11", "gpio12", "gpio13",
	"gpio14", "gpio15", "gpio20", "gpio21",
	"gpio22", "gpio23",
};

static const char *const rgmii_groups[] = {
	"gpio81",  "gpio82",  "gpio83",  "gpio91",
	"gpio92",  "gpio93",  "gpio94",  "gpio95",
	"gpio96",  "gpio97",  "gpio102", "gpio103",
	"gpio112", "gpio113", "gpio114",
};

static const char *const sd_write_protect_groups[] = {
	"gpio24",
};

static const char *const sp_cmu_groups[] = {
	"gpio64",
};

static const char *const ter_mi2s_groups[] = {
	"gpio115", "gpio116", "gpio117", "gpio118",
};

static const char *const tgu_ch_groups[] = {
	"gpio89", "gpio90", "gpio91", "gpio92",
};

static const char *const uim1_groups[] = {
	"gpio77", "gpio78", "gpio79", "gpio80",
};

static const char *const uim2_groups[] = {
	"gpio73", "gpio74", "gpio75", "gpio76",
};

static const char *const usb0_hs_groups[] = {
	"gpio88",
};

static const char *const usb1_hs_groups[] = {
	"gpio89",
};

static const char *const usb_phy_ps_groups[] = {
	"gpio104",
};

static const char *const vfr_1_groups[] = {
	"gpio92",
};

static const char *const vsense_trigger_mirnat_groups[] = {
	"gpio7",
};

static const char *const wlan_groups[] = {
	"gpio16", "gpio17", "gpio47", "gpio48",
};

static const char *const wsa_clk_groups[] = {
	"gpio111",
};

static const char *const wsa_data_groups[] = {
	"gpio110",
};

static const struct pinfunction qcs615_functions[] = {
	MSM_GPIO_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(adsp_ext),
	MSM_PIN_FUNCTION(agera_pll),
	MSM_PIN_FUNCTION(aoss_cti),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_tsens),
	MSM_PIN_FUNCTION(atest_usb),
	MSM_PIN_FUNCTION(cam_mclk),
	MSM_PIN_FUNCTION(cci_async),
	MSM_PIN_FUNCTION(cci_i2c),
	MSM_PIN_FUNCTION(cci_timer),
	MSM_PIN_FUNCTION(copy_gp),
	MSM_PIN_FUNCTION(copy_phase),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(dbg_out_clk),
	MSM_PIN_FUNCTION(ddr_bist),
	MSM_PIN_FUNCTION(ddr_pxi),
	MSM_PIN_FUNCTION(dp_hot),
	MSM_PIN_FUNCTION(edp_hot),
	MSM_PIN_FUNCTION(edp_lcd),
	MSM_PIN_FUNCTION(emac_gcc),
	MSM_PIN_FUNCTION(emac_phy_intr),
	MSM_PIN_FUNCTION(forced_usb),
	MSM_PIN_FUNCTION(gcc_gp),
	MSM_PIN_FUNCTION(gp_pdm),
	MSM_PIN_FUNCTION(gps_tx),
	MSM_PIN_FUNCTION(hs0_mi2s),
	MSM_PIN_FUNCTION(hs1_mi2s),
	MSM_PIN_FUNCTION(jitter_bist),
	MSM_PIN_FUNCTION(ldo_en),
	MSM_PIN_FUNCTION(ldo_update),
	MSM_PIN_FUNCTION(m_voc),
	MSM_PIN_FUNCTION(mclk1),
	MSM_PIN_FUNCTION(mclk2),
	MSM_PIN_FUNCTION(mdp_vsync),
	MSM_PIN_FUNCTION(mdp_vsync0_out),
	MSM_PIN_FUNCTION(mdp_vsync1_out),
	MSM_PIN_FUNCTION(mdp_vsync2_out),
	MSM_PIN_FUNCTION(mdp_vsync3_out),
	MSM_PIN_FUNCTION(mdp_vsync4_out),
	MSM_PIN_FUNCTION(mdp_vsync5_out),
	MSM_PIN_FUNCTION(mi2s_1),
	MSM_PIN_FUNCTION(mss_lte),
	MSM_PIN_FUNCTION(nav_pps_in),
	MSM_PIN_FUNCTION(nav_pps_out),
	MSM_PIN_FUNCTION(pa_indicator_or),
	MSM_PIN_FUNCTION(pcie_clk_req),
	MSM_PIN_FUNCTION(pcie_ep_rst),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(pll_bist),
	MSM_PIN_FUNCTION(pll_bypassnl),
	MSM_PIN_FUNCTION(pll_reset_n),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qdss_gpio),
	MSM_PIN_FUNCTION(qlink_enable),
	MSM_PIN_FUNCTION(qlink_request),
	MSM_PIN_FUNCTION(qspi),
	MSM_PIN_FUNCTION(qup0),
	MSM_PIN_FUNCTION(qup1),
	MSM_PIN_FUNCTION(rgmii),
	MSM_PIN_FUNCTION(sd_write_protect),
	MSM_PIN_FUNCTION(sp_cmu),
	MSM_PIN_FUNCTION(ter_mi2s),
	MSM_PIN_FUNCTION(tgu_ch),
	MSM_PIN_FUNCTION(uim1),
	MSM_PIN_FUNCTION(uim2),
	MSM_PIN_FUNCTION(usb0_hs),
	MSM_PIN_FUNCTION(usb1_hs),
	MSM_PIN_FUNCTION(usb_phy_ps),
	MSM_PIN_FUNCTION(vfr_1),
	MSM_PIN_FUNCTION(vsense_trigger_mirnat),
	MSM_PIN_FUNCTION(wlan),
	MSM_PIN_FUNCTION(wsa_clk),
	MSM_PIN_FUNCTION(wsa_data),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup qcs615_groups[] = {
	[0] = PINGROUP(0, WEST, qup0, _, qdss_gpio, _, _, _, _, _, _),
	[1] = PINGROUP(1, WEST, qup0, _, qdss_gpio, _, _, _, _, _, _),
	[2] = PINGROUP(2, WEST, qup0, _, qdss_gpio, _, _, _, _, _, _),
	[3] = PINGROUP(3, WEST, qup0, _, qdss_gpio, _, _, _, _, _, _),
	[4] = PINGROUP(4, WEST, qup0, _, _, _, _, _, _, _, _),
	[5] = PINGROUP(5, WEST, qup0, _, _, _, _, _, _, _, _),
	[6] = PINGROUP(6, EAST, qup1, qdss_gpio, ddr_pxi, _, _, _, _, _, _),
	[7] = PINGROUP(7, EAST, qup1, ddr_bist, qdss_gpio, atest_tsens,
			vsense_trigger_mirnat, atest_usb, ddr_pxi, _, _),
	[8] = PINGROUP(8, EAST, qup1, gp_pdm, ddr_bist, qdss_gpio, _, _, _, _, _),
	[9] = PINGROUP(9, EAST, qup1, ddr_bist, qdss_gpio, _, _, _, _, _, _),
	[10] = PINGROUP(10, EAST, qup1, ddr_bist, _, phase_flag, atest_usb, ddr_pxi, _, _, _),
	[11] = PINGROUP(11, EAST, qup1, dbg_out_clk, atest_usb, ddr_pxi, _, _, _, _, _),
	[12] = PINGROUP(12, EAST, qup1, jitter_bist, ddr_pxi, _, _, _, _, _, _),
	[13] = PINGROUP(13, EAST, qup1, pll_bypassnl, _, ddr_pxi, _, _, _, _, _),
	[14] = PINGROUP(14, EAST, qup1, pll_reset_n, _, qdss_gpio, _, _, _, _, _),
	[15] = PINGROUP(15, EAST, qup1, qdss_gpio, _, _, _, _, _, _, _),
	[16] = PINGROUP(16, WEST, qup0, _, wlan, _, _, _, _, _, _),
	[17] = PINGROUP(17, WEST, qup0, _, wlan, _, _, _, _, _, _),
	[18] = PINGROUP(18, WEST, qup0, _, phase_flag, _, _, _, _, _, _),
	[19] = PINGROUP(19, WEST, qup0, _, phase_flag, _, _, _, _, _, _),
	[20] = PINGROUP(20, SOUTH, qup1, _, phase_flag, qdss_gpio, _, _, _, _, _),
	[21] = PINGROUP(21, SOUTH, qup1, gcc_gp, _, qdss_gpio, _, _, _, _, _),
	[22] = PINGROUP(22, SOUTH, qup1, gcc_gp, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, SOUTH, qup1, _, phase_flag, _, _, _, _, _, _),
	[24] = PINGROUP(24, EAST, hs1_mi2s, sd_write_protect, _, phase_flag, _, _, _, _, _),
	[25] = PINGROUP(25, EAST, hs1_mi2s, _, phase_flag, _, _, _, _, _, _),
	[26] = PINGROUP(26, EAST, cci_async, hs1_mi2s, jitter_bist, _, _, _, _, _, _),
	[27] = PINGROUP(27, EAST, hs1_mi2s, pll_bist, _, _, _, _, _, _, _),
	[28] = PINGROUP(28, EAST, cam_mclk, agera_pll, qdss_gpio, _, _, _, _, _, _),
	[29] = PINGROUP(29, EAST, cam_mclk, _, qdss_gpio, atest_tsens, _, _, _, _, _),
	[30] = PINGROUP(30, EAST, cam_mclk, qdss_gpio, _, _, _, _, _, _, _),
	[31] = PINGROUP(31, EAST, cam_mclk, _, qdss_gpio, _, _, _, _, _, _),
	[32] = PINGROUP(32, EAST, cci_i2c, _, qdss_gpio, _, _, _, _, _, _),
	[33] = PINGROUP(33, EAST, cci_i2c, _, qdss_gpio, _, _, _, _, _, _),
	[34] = PINGROUP(34, EAST, cci_i2c, _, qdss_gpio, _, _, _, _, _, _),
	[35] = PINGROUP(35, EAST, cci_i2c, _, qdss_gpio, _, _, _, _, _, _),
	[36] = PINGROUP(36, EAST, hs0_mi2s, _, _, _, _, _, _, _, _),
	[37] = PINGROUP(37, EAST, cci_timer, hs0_mi2s, _, _, _, _, _, _, _),
	[38] = PINGROUP(38, EAST, cci_timer, hs0_mi2s, _, phase_flag, _, _, _, _, _),
	[39] = PINGROUP(39, EAST, cci_timer, hs0_mi2s, _, _, _, _, _, _, _),
	[40] = PINGROUP(40, EAST, _, phase_flag, _, _, _, _, _, _, _),
	[41] = PINGROUP(41, EAST, cci_async, cci_timer, _, phase_flag, _, _, _, _, _),
	[42] = PINGROUP(42, EAST, cci_async, cci_timer, _, phase_flag, _, _, _, _, _),
	[43] = PINGROUP(43, SOUTH, _, phase_flag, forced_usb, _, _, _, _, _, _),
	[44] = PINGROUP(44, EAST, qspi, _, phase_flag, qdss_gpio, _, _, _, _, _),
	[45] = PINGROUP(45, EAST, qspi, _, phase_flag, qdss_gpio, _, _, _, _, _),
	[46] = PINGROUP(46, EAST, qspi, _, qdss_gpio, _, _, _, _, _, _),
	[47] = PINGROUP(47, EAST, qspi, _, qdss_gpio, wlan, _, _, _, _, _),
	[48] = PINGROUP(48, EAST, qspi, _, wlan, _, _, _, _, _, _),
	[49] = PINGROUP(49, EAST, qspi, _, _, _, _, _, _, _, _),
	[50] = PINGROUP(50, EAST, qspi, _, _, _, _, _, _, _, _),
	[51] = PINGROUP(51, SOUTH, qlink_request, _, _, _, _, _, _, _, _),
	[52] = PINGROUP(52, SOUTH, qlink_enable, _, _, _, _, _, _, _, _),
	[53] = PINGROUP(53, SOUTH, pa_indicator_or, nav_pps_in, nav_pps_out, gps_tx, _,
			phase_flag, _, _, _),
	[54] = PINGROUP(54, SOUTH, _, gps_tx, gp_pdm, _, phase_flag, atest_usb, ddr_pxi, _, _),
	[55] = PINGROUP(55, SOUTH, _, _, phase_flag, atest_usb, ddr_pxi, _, _, _, _),
	[56] = PINGROUP(56, SOUTH, _, nav_pps_in, nav_pps_out, gps_tx, _, _, _, _, _),
	[57] = PINGROUP(57, SOUTH, _, nav_pps_in, gps_tx, nav_pps_out, gcc_gp, _, _, _, _),
	[58] = PINGROUP(58, SOUTH, _, gcc_gp, _, _, _, _, _, _, _),
	[59] = PINGROUP(59, SOUTH, _, nav_pps_in, nav_pps_out, gps_tx, gcc_gp, _, _, _, _),
	[60] = PINGROUP(60, SOUTH, _, nav_pps_in, nav_pps_out, gps_tx, cri_trng, _, _, _, _),
	[61] = PINGROUP(61, SOUTH, _, cri_trng, _, _, _, _, _, _, _),
	[62] = PINGROUP(62, SOUTH, _, cri_trng, _, _, _, _, _, _, _),
	[63] = PINGROUP(63, SOUTH, _, _, gp_pdm, _, _, _, _, _, _),
	[64] = PINGROUP(64, SOUTH, _, sp_cmu, _, _, _, _, _, _, _),
	[65] = PINGROUP(65, SOUTH, _, _, _, _, _, _, _, _, _),
	[66] = PINGROUP(66, SOUTH, _, gp_pdm, _, _, _, _, _, _, _),
	[67] = PINGROUP(67, SOUTH, _, _, _, phase_flag, atest_usb, _, _, _, _),
	[68] = PINGROUP(68, SOUTH, _, _, _, phase_flag, atest_usb, _, _, _, _),
	[69] = PINGROUP(69, SOUTH, _, _, _, _, _, _, _, _, _),
	[70] = PINGROUP(70, SOUTH, _, _, _, _, _, _, _, _, _),
	[71] = PINGROUP(71, SOUTH, _, _, _, _, _, _, _, _, _),
	[72] = PINGROUP(72, SOUTH, _, _, _, _, _, _, _, _, _),
	[73] = PINGROUP(73, SOUTH, uim2, _, _, _, _, _, _, _, _),
	[74] = PINGROUP(74, SOUTH, uim2, _, _, _, _, _, _, _, _),
	[75] = PINGROUP(75, SOUTH, uim2, _, phase_flag, atest_usb, _, _, _, _, _),
	[76] = PINGROUP(76, SOUTH, uim2, _, phase_flag, atest_usb, aoss_cti, _, _, _, _),
	[77] = PINGROUP(77, SOUTH, uim1, _, phase_flag, atest_usb, _, _, _, _, _),
	[78] = PINGROUP(78, SOUTH, uim1, gcc_gp, _, phase_flag, _, _, _, _, _),
	[79] = PINGROUP(79, SOUTH, uim1, gp_pdm, _, phase_flag, _, _, _, _, _),
	[80] = PINGROUP(80, SOUTH, uim1, _, phase_flag, _, _, _, _, _, _),
	[81] = PINGROUP(81, WEST, rgmii, mdp_vsync, _, qdss_gpio, _, _, _, _, _),
	[82] = PINGROUP(82, WEST, rgmii, mdp_vsync, _, phase_flag, qdss_gpio, _, _, _, _),
	[83] = PINGROUP(83, WEST, rgmii, mdp_vsync, _, qdss_cti, _, _, _, _, _),
	[84] = PINGROUP(84, SOUTH, _, phase_flag, atest_char, _, _, _, _, _, _),
	[85] = PINGROUP(85, SOUTH, _, atest_char, _, _, _, _, _, _, _),
	[86] = PINGROUP(86, SOUTH, copy_gp, _, atest_char, _, _, _, _, _, _),
	[87] = PINGROUP(87, SOUTH, _, atest_char, _, _, _, _, _, _, _),
	[88] = PINGROUP(88, WEST, _, usb0_hs, _, _, _, _, _, _, _),
	[89] = PINGROUP(89, WEST, emac_phy_intr, pcie_ep_rst, tgu_ch, usb1_hs, _, _, _, _, _),
	[90] = PINGROUP(90, WEST, mdp_vsync, mdp_vsync0_out, mdp_vsync1_out,
			mdp_vsync2_out, mdp_vsync3_out, mdp_vsync4_out, mdp_vsync5_out,
			pcie_clk_req, tgu_ch),
	[91] = PINGROUP(91, WEST, rgmii, tgu_ch, _, _, _, _, _, _, _),
	[92] = PINGROUP(92, WEST, rgmii, vfr_1, tgu_ch, _, phase_flag, qdss_gpio, _, _, _),
	[93] = PINGROUP(93, WEST, rgmii, qdss_gpio, _, _, _, _, _, _, _),
	[94] = PINGROUP(94, WEST, rgmii, qdss_gpio, _, _, _, _, _, _, _),
	[95] = PINGROUP(95, WEST, rgmii, gp_pdm, qdss_gpio, _, _, _, _, _, _),
	[96] = PINGROUP(96, WEST, rgmii, qdss_cti, _, _, _, _, _, _, _),
	[97] = PINGROUP(97, WEST, rgmii, mdp_vsync, ldo_en, qdss_cti, _, _, _, _, _),
	[98] = PINGROUP(98, WEST, mdp_vsync, ldo_update, qdss_cti, _, _, _, _, _, _),
	[99] = PINGROUP(99, EAST, prng_rosc, _, _, _, _, _, _, _, _),
	[100] = PINGROUP(100, WEST, _, _, _, _, _, _, _, _, _),
	[101] = PINGROUP(101, WEST, emac_gcc, _, _, _, _, _, _, _, _),
	[102] = PINGROUP(102, WEST, rgmii, dp_hot, emac_gcc, prng_rosc, _, _, _, _, _),
	[103] = PINGROUP(103, WEST, rgmii, dp_hot, copy_phase, qdss_cti, _, _, _, _, _),
	[104] = PINGROUP(104, WEST, usb_phy_ps, _, qdss_cti, dp_hot, _, _, _, _, _),
	[105] = PINGROUP(105, SOUTH, _, _, _, _, _, _, _, _, _),
	[106] = PINGROUP(106, EAST, mss_lte, _, _, _, _, _, _, _, _),
	[107] = PINGROUP(107, EAST, mss_lte, _, _, _, _, _, _, _, _),
	[108] = PINGROUP(108, SOUTH, mi2s_1, _, qdss_gpio, _, _, _, _, _, _),
	[109] = PINGROUP(109, SOUTH, mi2s_1, _, qdss_gpio, _, _, _, _, _, _),
	[110] = PINGROUP(110, SOUTH, wsa_data, mi2s_1, _, _, _, _, _, _, _),
	[111] = PINGROUP(111, SOUTH, wsa_clk, mi2s_1, _, _, _, _, _, _, _),
	[112] = PINGROUP(112, WEST, rgmii, _, qdss_cti, _, _, _, _, _, _),
	[113] = PINGROUP(113, WEST, rgmii, edp_hot, _, qdss_cti, _, _, _, _, _),
	[114] = PINGROUP(114, WEST, rgmii, _, _, _, _, _, _, _, _),
	[115] = PINGROUP(115, SOUTH, ter_mi2s, atest_char, _, _, _, _, _, _, _),
	[116] = PINGROUP(116, SOUTH, ter_mi2s, _, phase_flag, _, _, _, _, _, _),
	[117] = PINGROUP(117, SOUTH, ter_mi2s, _, phase_flag, qdss_gpio, atest_char, _, _, _, _),
	[118] = PINGROUP(118, SOUTH, ter_mi2s, adsp_ext, _, phase_flag, qdss_gpio, atest_char,
			_, _, _),
	[119] = PINGROUP(119, SOUTH, edp_lcd, _, phase_flag, qdss_gpio, atest_char, _, _, _, _),
	[120] = PINGROUP(120, SOUTH, m_voc, qdss_gpio, atest_char, _, _, _, _, _, _),
	[121] = PINGROUP(121, SOUTH, mclk1, atest_char, _, _, _, _, _, _, _),
	[122] = PINGROUP(122, SOUTH, mclk2, _, _, _, _, _, _, _, _),
	[123] = UFS_RESET(ufs_reset, 0x9f000),
	[124] = SDC_QDSD_PINGROUP(sdc1_rclk, WEST, 0x9a000, 15, 0),
	[125] = SDC_QDSD_PINGROUP(sdc1_clk, WEST, 0x9a000, 13, 6),
	[126] = SDC_QDSD_PINGROUP(sdc1_cmd, WEST, 0x9a000, 11, 3),
	[127] = SDC_QDSD_PINGROUP(sdc1_data, WEST, 0x9a000, 9, 0),
	[128] = SDC_QDSD_PINGROUP(sdc2_clk, SOUTH, 0x98000, 14, 6),
	[129] = SDC_QDSD_PINGROUP(sdc2_cmd, SOUTH, 0x98000, 11, 3),
	[130] = SDC_QDSD_PINGROUP(sdc2_data, SOUTH, 0x98000, 9, 0),
};

static const struct msm_gpio_wakeirq_map qcs615_pdc_map[] = {
	{ 1, 45 },    { 3, 31 },    { 7, 55 },    { 9, 110 },   { 11, 34 },
	{ 13, 33 },   { 14, 35 },   { 17, 46 },   { 19, 48 },   { 21, 83 },
	{ 22, 36 },   { 26, 38 },   { 35, 37 },   { 39, 125 },  { 41, 47 },
	{ 47, 49 },   { 48, 51 },   { 50, 52 },   { 51, 123 },  { 55, 56 },
	{ 56, 57 },   { 57, 58 },   { 60, 60 },   { 71, 54 },   { 80, 73 },
	{ 81, 64 },   { 82, 50 },   { 83, 65 },   { 84, 92 },   { 85, 99 },
	{ 86, 67 },   { 87, 84 },   { 88, 124 },  { 89, 122 },  { 90, 69 },
	{ 92, 88 },   { 93, 75 },   { 94, 91 },   { 95, 72 },   { 96, 82 },
	{ 97, 74 },   { 98, 95 },   { 99, 94 },   { 100, 100 }, { 101, 40 },
	{ 102, 93 },  { 103, 77 },  { 104, 78 },  { 105, 96 },  { 107, 97 },
	{ 108, 111 }, { 112, 112 }, { 113, 113 }, { 117, 85 },  { 118, 102 },
	{ 119, 87 },  { 120, 114 }, { 121, 89 },  { 122, 90 },
};

static const struct msm_pinctrl_soc_data qcs615_tlmm = {
	.pins = qcs615_pins,
	.npins = ARRAY_SIZE(qcs615_pins),
	.functions = qcs615_functions,
	.nfunctions = ARRAY_SIZE(qcs615_functions),
	.groups = qcs615_groups,
	.ngroups = ARRAY_SIZE(qcs615_groups),
	.ngpios = 124,
	.tiles = qcs615_tiles,
	.ntiles = ARRAY_SIZE(qcs615_tiles),
	.wakeirq_map = qcs615_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(qcs615_pdc_map),
};

static const struct of_device_id qcs615_tlmm_of_match[] = {
	{
		.compatible = "qcom,qcs615-tlmm",
	},
	{},
};

static int qcs615_tlmm_probe(struct platform_device *pdev)
{
	return  msm_pinctrl_probe(pdev, &qcs615_tlmm);
}

static struct platform_driver qcs615_tlmm_driver = {
	.driver = {
		.name = "qcs615-tlmm",
		.of_match_table = qcs615_tlmm_of_match,
	},
	.probe = qcs615_tlmm_probe,
};

static int __init qcs615_tlmm_init(void)
{
	return platform_driver_register(&qcs615_tlmm_driver);
}
arch_initcall(qcs615_tlmm_init);

static void __exit qcs615_tlmm_exit(void)
{
	platform_driver_unregister(&qcs615_tlmm_driver);
}
module_exit(qcs615_tlmm_exit);

MODULE_DESCRIPTION("QTI QCS615 TLMM driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, qcs615_tlmm_of_match);
