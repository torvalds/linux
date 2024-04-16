// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

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

#define SOUTH	0x00D00000
#define WEST	0x00500000
#define EAST	0x00100000
#define SOUTH_PDC_OFFSET    0xa2000
#define WEST_PDC_OFFSET     0xa6000
#define EAST_PDC_OFFSET     0x9f000
#define NUM_TILES 3
#define DUMMY	0x0
#define REG_SIZE 0x1000
#define PINGROUP(id, base, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{						\
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
		},					\
		.nfuncs = 10,				\
		.ctl_reg = base + REG_SIZE * id,		\
		.io_reg = base + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = base + 0x8 + REG_SIZE * id,	\
		.intr_status_reg = base + 0xc + REG_SIZE * id,	\
		.intr_target_reg = base + 0x8 + REG_SIZE * id,	\
		.dir_conn_reg = (base == EAST) ? base + EAST_PDC_OFFSET : \
			((base == WEST) ? base + WEST_PDC_OFFSET : \
			base + SOUTH_PDC_OFFSET), \
		.mux_bit = 2,			\
		.dir_conn_reg = (base == EAST) ? base + EAST_PDC_OFFSET : \
			((base == WEST) ? base + WEST_PDC_OFFSET : \
			base + SOUTH_PDC_OFFSET),       \
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
		.dir_conn_en_bit = 8,		        \
	}

#define SDC_QDSD_PINGROUP(pg_name, ctl, pull, drv)	\
	{						\
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
	{						\
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
static const struct pinctrl_pin_desc sm6150_pins[] = {
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

static const unsigned int sdc1_rclk_pins[] = { 124 };
static const unsigned int sdc1_clk_pins[] = { 125 };
static const unsigned int sdc1_cmd_pins[] = { 126 };
static const unsigned int sdc1_data_pins[] = { 127 };
static const unsigned int sdc2_clk_pins[] = { 128 };
static const unsigned int sdc2_cmd_pins[] = { 129 };
static const unsigned int sdc2_data_pins[] = { 130 };
static const unsigned int ufs_reset_pins[] = { 123 };

enum sm6150_functions {
	msm_mux_qup02,
	msm_mux_gpio,
	msm_mux_qdss_gpio6,
	msm_mux_qdss_gpio7,
	msm_mux_qdss_gpio8,
	msm_mux_qdss_gpio9,
	msm_mux_qup01,
	msm_mux_qup12,
	msm_mux_qdss_gpio0,
	msm_mux_ddr_pxi0,
	msm_mux_ddr_bist,
	msm_mux_qdss_gpio1,
	msm_mux_atest_tsens2,
	msm_mux_vsense_trigger,
	msm_mux_atest_usb1,
	msm_mux_GP_PDM1,
	msm_mux_qdss_gpio2,
	msm_mux_qdss_gpio3,
	msm_mux_qup13,
	msm_mux_phase_flag28,
	msm_mux_atest_usb11,
	msm_mux_ddr_pxi2,
	msm_mux_dbg_out,
	msm_mux_atest_usb10,
	msm_mux_JITTER_BIST,
	msm_mux_ddr_pxi3,
	msm_mux_pll_bypassnl,
	msm_mux_qup11,
	msm_mux_pll_reset,
	msm_mux_qdss_gpio,
	msm_mux_qup00,
	msm_mux_wlan2_adc1,
	msm_mux_wlan2_adc0,
	msm_mux_qup03,
	msm_mux_phase_flag3,
	msm_mux_phase_flag2,
	msm_mux_qup10,
	msm_mux_phase_flag1,
	msm_mux_qdss_gpio4,
	msm_mux_gcc_gp2,
	msm_mux_qdss_gpio5,
	msm_mux_gcc_gp3,
	msm_mux_phase_flag0,
	msm_mux_hs1_mi2s,
	msm_mux_sd_write,
	msm_mux_phase_flag29,
	msm_mux_phase_flag10,
	msm_mux_cci_async,
	msm_mux_PLL_BIST,
	msm_mux_cam_mclk,
	msm_mux_AGERA_PLL,
	msm_mux_atest_tsens,
	msm_mux_cci_i2c,
	msm_mux_qdss_gpio10,
	msm_mux_qdss_gpio11,
	msm_mux_hs0_mi2s,
	msm_mux_cci_timer2,
	msm_mux_cci_timer0,
	msm_mux_phase_flag15,
	msm_mux_cci_timer1,
	msm_mux_phase_flag27,
	msm_mux_cci_timer3,
	msm_mux_phase_flag26,
	msm_mux_cci_timer4,
	msm_mux_phase_flag25,
	msm_mux_phase_flag9,
	msm_mux_qspi_cs,
	msm_mux_phase_flag8,
	msm_mux_qdss_gpio12,
	msm_mux_qspi0,
	msm_mux_phase_flag7,
	msm_mux_qdss_gpio13,
	msm_mux_qspi1,
	msm_mux_qdss_gpio14,
	msm_mux_qspi2,
	msm_mux_qdss_gpio15,
	msm_mux_wlan1_adc1,
	msm_mux_qspi_clk,
	msm_mux_wlan1_adc0,
	msm_mux_qspi3,
	msm_mux_qlink_request,
	msm_mux_qlink_enable,
	msm_mux_pa_indicator,
	msm_mux_NAV_PPS_IN,
	msm_mux_NAV_PPS_OUT,
	msm_mux_GPS_TX,
	msm_mux_phase_flag23,
	msm_mux_GP_PDM0,
	msm_mux_phase_flag22,
	msm_mux_atest_usb13,
	msm_mux_ddr_pxi1,
	msm_mux_phase_flag4,
	msm_mux_atest_usb12,
	msm_mux_gcc_gp1,
	msm_mux_CRI_TRNG0,
	msm_mux_CRI_TRNG,
	msm_mux_CRI_TRNG1,
	msm_mux_GP_PDM2,
	msm_mux_SP_CMU,
	msm_mux_phase_flag6,
	msm_mux_atest_usb2,
	msm_mux_phase_flag5,
	msm_mux_atest_usb23,
	msm_mux_uim2_data,
	msm_mux_uim2_clk,
	msm_mux_uim2_reset,
	msm_mux_phase_flag17,
	msm_mux_atest_usb22,
	msm_mux_uim2_present,
	msm_mux_phase_flag16,
	msm_mux_atest_usb21,
	msm_mux_uim1_data,
	msm_mux_phase_flag31,
	msm_mux_atest_usb20,
	msm_mux_uim1_clk,
	msm_mux_phase_flag11,
	msm_mux_uim1_reset,
	msm_mux_phase_flag24,
	msm_mux_uim1_present,
	msm_mux_phase_flag14,
	msm_mux_rgmii_rxd2,
	msm_mux_mdp_vsync,
	msm_mux_rgmii_rxd1,
	msm_mux_phase_flag13,
	msm_mux_rgmii_rxd0,
	msm_mux_qdss_cti,
	msm_mux_phase_flag12,
	msm_mux_copy_gp,
	msm_mux_usb0_hs_ac,
	msm_mux_emac_phy,
	msm_mux_pcie_ep,
	msm_mux_tgu_ch3,
	msm_mux_usb1_hs_ac,
	msm_mux_mdp_vsync0,
	msm_mux_mdp_vsync1,
	msm_mux_mdp_vsync2,
	msm_mux_mdp_vsync3,
	msm_mux_mdp_vsync4,
	msm_mux_mdp_vsync5,
	msm_mux_pcie_clk,
	msm_mux_tgu_ch0,
	msm_mux_rgmii_sync,
	msm_mux_tgu_ch1,
	msm_mux_rgmii_txc,
	msm_mux_vfr_1,
	msm_mux_tgu_ch2,
	msm_mux_phase_flag30,
	msm_mux_rgmii_txd3,
	msm_mux_rgmii_txd2,
	msm_mux_rgmii_txd1,
	msm_mux_rgmii_txd0,
	msm_mux_rgmii_tx,
	msm_mux_ldo_en,
	msm_mux_ldo_update,
	msm_mux_prng_rosc,
	msm_mux_emac_gcc0,
	msm_mux_rgmii_rxc,
	msm_mux_dp_hot,
	msm_mux_egpio,
	msm_mux_emac_gcc1,
	msm_mux_rgmii_rxd3,
	msm_mux_debug_hot,
	msm_mux_COPY_PHASE,
	msm_mux_usb_phy,
	msm_mux_mss_lte,
	msm_mux_mi2s_1,
	msm_mux_WSA_DATA,
	msm_mux_WSA_CLK,
	msm_mux_rgmii_rx,
	msm_mux_rgmii_mdc,
	msm_mux_edp_hot,
	msm_mux_rgmii_mdio,
	msm_mux_ter_mi2s,
	msm_mux_atest_char,
	msm_mux_phase_flag21,
	msm_mux_phase_flag20,
	msm_mux_atest_char3,
	msm_mux_adsp_ext,
	msm_mux_phase_flag19,
	msm_mux_atest_char2,
	msm_mux_edp_lcd,
	msm_mux_phase_flag18,
	msm_mux_atest_char1,
	msm_mux_m_voc,
	msm_mux_atest_char0,
	msm_mux_mclk1,
	msm_mux_mclk2,
	msm_mux_NA,
};

static const char * const qup02_groups[] = {
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
};
static const char * const egpio_groups[] = {
	"gpio85", "gpio98",
};
static const char * const qdss_gpio6_groups[] = {
	"gpio0", "gpio30",
};
static const char * const qdss_gpio7_groups[] = {
	"gpio1", "gpio31",
};
static const char * const qdss_gpio8_groups[] = {
	"gpio2", "gpio32",
};
static const char * const qdss_gpio9_groups[] = {
	"gpio3", "gpio33",
};
static const char * const qup01_groups[] = {
	"gpio4", "gpio5",
};
static const char * const qup12_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9",
};
static const char * const qdss_gpio0_groups[] = {
	"gpio6", "gpio117",
};
static const char * const ddr_pxi0_groups[] = {
	"gpio6", "gpio7",
};
static const char * const ddr_bist_groups[] = {
	"gpio7", "gpio8", "gpio9", "gpio10",
};
static const char * const qdss_gpio1_groups[] = {
	"gpio7", "gpio118",
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
static const char * const GP_PDM1_groups[] = {
	"gpio8", "gpio66",
};
static const char * const qdss_gpio2_groups[] = {
	"gpio8", "gpio119",
};
static const char * const qdss_gpio3_groups[] = {
	"gpio9", "gpio120",
};
static const char * const qup13_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13",
};
static const char * const phase_flag28_groups[] = {
	"gpio10",
};
static const char * const atest_usb11_groups[] = {
	"gpio10",
};
static const char * const ddr_pxi2_groups[] = {
	"gpio10", "gpio11",
};
static const char * const dbg_out_groups[] = {
	"gpio11",
};
static const char * const atest_usb10_groups[] = {
	"gpio11",
};
static const char * const JITTER_BIST_groups[] = {
	"gpio12", "gpio26",
};
static const char * const ddr_pxi3_groups[] = {
	"gpio12", "gpio13",
};
static const char * const pll_bypassnl_groups[] = {
	"gpio13",
};
static const char * const qup11_groups[] = {
	"gpio14", "gpio15",
};
static const char * const pll_reset_groups[] = {
	"gpio14",
};
static const char * const qdss_gpio_groups[] = {
	"gpio14", "gpio15", "gpio108", "gpio109",
};
static const char * const qup00_groups[] = {
	"gpio16", "gpio17",
};
static const char * const wlan2_adc1_groups[] = {
	"gpio16",
};
static const char * const wlan2_adc0_groups[] = {
	"gpio17",
};
static const char * const qup03_groups[] = {
	"gpio18", "gpio19",
};
static const char * const phase_flag3_groups[] = {
	"gpio18",
};
static const char * const phase_flag2_groups[] = {
	"gpio19",
};
static const char * const qup10_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23",
};
static const char * const phase_flag1_groups[] = {
	"gpio20",
};
static const char * const qdss_gpio4_groups[] = {
	"gpio20", "gpio28",
};
static const char * const gcc_gp2_groups[] = {
	"gpio21", "gpio58",
};
static const char * const qdss_gpio5_groups[] = {
	"gpio21", "gpio29",
};
static const char * const gcc_gp3_groups[] = {
	"gpio22", "gpio59",
};
static const char * const phase_flag0_groups[] = {
	"gpio23",
};
static const char * const hs1_mi2s_groups[] = {
	"gpio24", "gpio25", "gpio26", "gpio27",
};
static const char * const sd_write_groups[] = {
	"gpio24",
};
static const char * const phase_flag29_groups[] = {
	"gpio24",
};
static const char * const phase_flag10_groups[] = {
	"gpio25",
};
static const char * const cci_async_groups[] = {
	"gpio26", "gpio41", "gpio42",
};
static const char * const PLL_BIST_groups[] = {
	"gpio27",
};
static const char * const cam_mclk_groups[] = {
	"gpio28", "gpio29", "gpio30", "gpio31",
};
static const char * const AGERA_PLL_groups[] = {
	"gpio28",
};
static const char * const atest_tsens_groups[] = {
	"gpio29",
};
static const char * const cci_i2c_groups[] = {
	"gpio32", "gpio33", "gpio34", "gpio35",
};
static const char * const qdss_gpio10_groups[] = {
	"gpio34", "gpio92",
};
static const char * const qdss_gpio11_groups[] = {
	"gpio35", "gpio93",
};
static const char * const hs0_mi2s_groups[] = {
	"gpio36", "gpio37", "gpio38", "gpio39",
};
static const char * const cci_timer2_groups[] = {
	"gpio37",
};
static const char * const cci_timer0_groups[] = {
	"gpio38",
};
static const char * const phase_flag15_groups[] = {
	"gpio38",
};
static const char * const cci_timer1_groups[] = {
	"gpio39",
};
static const char * const phase_flag27_groups[] = {
	"gpio40",
};
static const char * const cci_timer3_groups[] = {
	"gpio41",
};
static const char * const phase_flag26_groups[] = {
	"gpio41",
};
static const char * const cci_timer4_groups[] = {
	"gpio42",
};
static const char * const phase_flag25_groups[] = {
	"gpio42",
};
static const char * const phase_flag9_groups[] = {
	"gpio43",
};
static const char * const qspi_cs_groups[] = {
	"gpio44", "gpio50",
};
static const char * const phase_flag8_groups[] = {
	"gpio44",
};
static const char * const qdss_gpio12_groups[] = {
	"gpio44", "gpio94",
};
static const char * const qspi0_groups[] = {
	"gpio45",
};
static const char * const phase_flag7_groups[] = {
	"gpio45",
};
static const char * const qdss_gpio13_groups[] = {
	"gpio45", "gpio95",
};
static const char * const qspi1_groups[] = {
	"gpio46",
};
static const char * const qdss_gpio14_groups[] = {
	"gpio46", "gpio81",
};
static const char * const qspi2_groups[] = {
	"gpio47",
};
static const char * const qdss_gpio15_groups[] = {
	"gpio47", "gpio82",
};
static const char * const wlan1_adc1_groups[] = {
	"gpio47",
};
static const char * const qspi_clk_groups[] = {
	"gpio48",
};
static const char * const wlan1_adc0_groups[] = {
	"gpio48",
};
static const char * const qspi3_groups[] = {
	"gpio49",
};
static const char * const qlink_request_groups[] = {
	"gpio51",
};
static const char * const qlink_enable_groups[] = {
	"gpio52",
};
static const char * const pa_indicator_groups[] = {
	"gpio53",
};
static const char * const NAV_PPS_IN_groups[] = {
	"gpio53", "gpio56", "gpio57", "gpio59",
	"gpio60",
};
static const char * const NAV_PPS_OUT_groups[] = {
	"gpio53", "gpio56", "gpio57", "gpio59",
	"gpio60",
};
static const char * const GPS_TX_groups[] = {
	"gpio53", "gpio54", "gpio56", "gpio57", "gpio59", "gpio60",
};
static const char * const phase_flag23_groups[] = {
	"gpio53",
};
static const char * const GP_PDM0_groups[] = {
	"gpio54", "gpio95",
};
static const char * const phase_flag22_groups[] = {
	"gpio54",
};
static const char * const atest_usb13_groups[] = {
	"gpio54",
};
static const char * const ddr_pxi1_groups[] = {
	"gpio54", "gpio55",
};
static const char * const phase_flag4_groups[] = {
	"gpio55",
};
static const char * const atest_usb12_groups[] = {
	"gpio55",
};
static const char * const gcc_gp1_groups[] = {
	"gpio57", "gpio78",
};
static const char * const CRI_TRNG0_groups[] = {
	"gpio60",
};
static const char * const CRI_TRNG_groups[] = {
	"gpio61",
};
static const char * const CRI_TRNG1_groups[] = {
	"gpio62",
};
static const char * const GP_PDM2_groups[] = {
	"gpio63", "gpio79",
};
static const char * const SP_CMU_groups[] = {
	"gpio64",
};
static const char * const phase_flag6_groups[] = {
	"gpio67",
};
static const char * const atest_usb2_groups[] = {
	"gpio67",
};
static const char * const phase_flag5_groups[] = {
	"gpio68",
};
static const char * const atest_usb23_groups[] = {
	"gpio68",
};
static const char * const uim2_data_groups[] = {
	"gpio73",
};
static const char * const uim2_clk_groups[] = {
	"gpio74",
};
static const char * const uim2_reset_groups[] = {
	"gpio75",
};
static const char * const phase_flag17_groups[] = {
	"gpio75",
};
static const char * const atest_usb22_groups[] = {
	"gpio75",
};
static const char * const uim2_present_groups[] = {
	"gpio76",
};
static const char * const phase_flag16_groups[] = {
	"gpio76",
};
static const char * const atest_usb21_groups[] = {
	"gpio76",
};
static const char * const uim1_data_groups[] = {
	"gpio77",
};
static const char * const phase_flag31_groups[] = {
	"gpio77",
};
static const char * const atest_usb20_groups[] = {
	"gpio77",
};
static const char * const uim1_clk_groups[] = {
	"gpio78",
};
static const char * const phase_flag11_groups[] = {
	"gpio78",
};
static const char * const uim1_reset_groups[] = {
	"gpio79",
};
static const char * const phase_flag24_groups[] = {
	"gpio79",
};
static const char * const uim1_present_groups[] = {
	"gpio80",
};
static const char * const phase_flag14_groups[] = {
	"gpio80",
};
static const char * const rgmii_rxd2_groups[] = {
	"gpio81",
};
static const char * const mdp_vsync_groups[] = {
	"gpio81", "gpio82", "gpio83", "gpio90", "gpio97", "gpio98",
};
static const char * const rgmii_rxd1_groups[] = {
	"gpio82",
};
static const char * const phase_flag13_groups[] = {
	"gpio82",
};
static const char * const rgmii_rxd0_groups[] = {
	"gpio83",
};
static const char * const qdss_cti_groups[] = {
	"gpio83", "gpio96", "gpio97", "gpio98", "gpio103", "gpio104",
	"gpio112", "gpio113",
};
static const char * const phase_flag12_groups[] = {
	"gpio84",
};
static const char * const copy_gp_groups[] = {
	"gpio86",
};
static const char * const usb0_hs_ac_groups[] = {
	"gpio88",
};
static const char * const emac_phy_groups[] = {
	"gpio89",
};
static const char * const pcie_ep_groups[] = {
	"gpio89",
};
static const char * const tgu_ch3_groups[] = {
	"gpio89",
};
static const char * const usb1_hs_ac_groups[] = {
	"gpio89",
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
static const char * const mdp_vsync4_groups[] = {
	"gpio90",
};
static const char * const mdp_vsync5_groups[] = {
	"gpio90",
};
static const char * const pcie_clk_groups[] = {
	"gpio90",
};
static const char * const tgu_ch0_groups[] = {
	"gpio90",
};
static const char * const rgmii_sync_groups[] = {
	"gpio91",
};
static const char * const tgu_ch1_groups[] = {
	"gpio91",
};
static const char * const rgmii_txc_groups[] = {
	"gpio92",
};
static const char * const vfr_1_groups[] = {
	"gpio92",
};
static const char * const tgu_ch2_groups[] = {
	"gpio92",
};
static const char * const phase_flag30_groups[] = {
	"gpio92",
};
static const char * const rgmii_txd3_groups[] = {
	"gpio93",
};
static const char * const rgmii_txd2_groups[] = {
	"gpio94",
};
static const char * const rgmii_txd1_groups[] = {
	"gpio95",
};
static const char * const rgmii_txd0_groups[] = {
	"gpio96",
};
static const char * const rgmii_tx_groups[] = {
	"gpio97",
};
static const char * const ldo_en_groups[] = {
	"gpio97",
};
static const char * const ldo_update_groups[] = {
	"gpio98",
};
static const char * const prng_rosc_groups[] = {
	"gpio99", "gpio102",
};
static const char * const emac_gcc0_groups[] = {
	"gpio101",
};
static const char * const rgmii_rxc_groups[] = {
	"gpio102",
};
static const char * const dp_hot_groups[] = {
	"gpio102",
};
static const char * const emac_gcc1_groups[] = {
	"gpio102",
};
static const char * const rgmii_rxd3_groups[] = {
	"gpio103",
};
static const char * const debug_hot_groups[] = {
	"gpio103",
};
static const char * const COPY_PHASE_groups[] = {
	"gpio103",
};
static const char * const usb_phy_groups[] = {
	"gpio104",
};
static const char * const mss_lte_groups[] = {
	"gpio106", "gpio107",
};
static const char * const mi2s_1_groups[] = {
	"gpio108", "gpio109", "gpio110", "gpio111",
};
static const char * const WSA_DATA_groups[] = {
	"gpio110",
};
static const char * const WSA_CLK_groups[] = {
	"gpio111",
};
static const char * const rgmii_rx_groups[] = {
	"gpio112",
};
static const char * const rgmii_mdc_groups[] = {
	"gpio113",
};
static const char * const edp_hot_groups[] = {
	"gpio113",
};
static const char * const rgmii_mdio_groups[] = {
	"gpio114",
};
static const char * const ter_mi2s_groups[] = {
	"gpio115", "gpio116", "gpio117", "gpio118",
};
static const char * const atest_char_groups[] = {
	"gpio115",
};
static const char * const phase_flag21_groups[] = {
	"gpio116",
};
static const char * const phase_flag20_groups[] = {
	"gpio117",
};
static const char * const atest_char3_groups[] = {
	"gpio117",
};
static const char * const adsp_ext_groups[] = {
	"gpio118",
};
static const char * const phase_flag19_groups[] = {
	"gpio118",
};
static const char * const atest_char2_groups[] = {
	"gpio118",
};
static const char * const edp_lcd_groups[] = {
	"gpio119",
};
static const char * const phase_flag18_groups[] = {
	"gpio119",
};
static const char * const atest_char1_groups[] = {
	"gpio119",
};
static const char * const m_voc_groups[] = {
	"gpio120",
};
static const char * const atest_char0_groups[] = {
	"gpio120",
};
static const char * const mclk1_groups[] = {
	"gpio121",
};
static const char * const mclk2_groups[] = {
	"gpio122",
};

static const struct msm_function sm6150_functions[] = {
	FUNCTION(qup02),
	FUNCTION(gpio),
	FUNCTION(qdss_gpio6),
	FUNCTION(qdss_gpio7),
	FUNCTION(qdss_gpio8),
	FUNCTION(qdss_gpio9),
	FUNCTION(qup01),
	FUNCTION(qup12),
	FUNCTION(qdss_gpio0),
	FUNCTION(ddr_pxi0),
	FUNCTION(ddr_bist),
	FUNCTION(qdss_gpio1),
	FUNCTION(atest_tsens2),
	FUNCTION(vsense_trigger),
	FUNCTION(atest_usb1),
	FUNCTION(GP_PDM1),
	FUNCTION(qdss_gpio2),
	FUNCTION(qdss_gpio3),
	FUNCTION(qup13),
	FUNCTION(phase_flag28),
	FUNCTION(atest_usb11),
	FUNCTION(ddr_pxi2),
	FUNCTION(dbg_out),
	FUNCTION(atest_usb10),
	FUNCTION(JITTER_BIST),
	FUNCTION(ddr_pxi3),
	FUNCTION(pll_bypassnl),
	FUNCTION(qup11),
	FUNCTION(pll_reset),
	FUNCTION(qdss_gpio),
	FUNCTION(qup00),
	FUNCTION(wlan2_adc1),
	FUNCTION(wlan2_adc0),
	FUNCTION(qup03),
	FUNCTION(phase_flag3),
	FUNCTION(phase_flag2),
	FUNCTION(qup10),
	FUNCTION(phase_flag1),
	FUNCTION(qdss_gpio4),
	FUNCTION(gcc_gp2),
	FUNCTION(qdss_gpio5),
	FUNCTION(gcc_gp3),
	FUNCTION(phase_flag0),
	FUNCTION(hs1_mi2s),
	FUNCTION(sd_write),
	FUNCTION(phase_flag29),
	FUNCTION(phase_flag10),
	FUNCTION(cci_async),
	FUNCTION(PLL_BIST),
	FUNCTION(cam_mclk),
	FUNCTION(AGERA_PLL),
	FUNCTION(atest_tsens),
	FUNCTION(cci_i2c),
	FUNCTION(qdss_gpio10),
	FUNCTION(qdss_gpio11),
	FUNCTION(hs0_mi2s),
	FUNCTION(cci_timer2),
	FUNCTION(cci_timer0),
	FUNCTION(phase_flag15),
	FUNCTION(cci_timer1),
	FUNCTION(phase_flag27),
	FUNCTION(cci_timer3),
	FUNCTION(phase_flag26),
	FUNCTION(cci_timer4),
	FUNCTION(phase_flag25),
	FUNCTION(phase_flag9),
	FUNCTION(qspi_cs),
	FUNCTION(phase_flag8),
	FUNCTION(qdss_gpio12),
	FUNCTION(qspi0),
	FUNCTION(phase_flag7),
	FUNCTION(qdss_gpio13),
	FUNCTION(qspi1),
	FUNCTION(qdss_gpio14),
	FUNCTION(qspi2),
	FUNCTION(qdss_gpio15),
	FUNCTION(wlan1_adc1),
	FUNCTION(qspi_clk),
	FUNCTION(wlan1_adc0),
	FUNCTION(qspi3),
	FUNCTION(qlink_request),
	FUNCTION(qlink_enable),
	FUNCTION(pa_indicator),
	FUNCTION(NAV_PPS_IN),
	FUNCTION(NAV_PPS_OUT),
	FUNCTION(GPS_TX),
	FUNCTION(phase_flag23),
	FUNCTION(GP_PDM0),
	FUNCTION(phase_flag22),
	FUNCTION(atest_usb13),
	FUNCTION(ddr_pxi1),
	FUNCTION(phase_flag4),
	FUNCTION(atest_usb12),
	FUNCTION(gcc_gp1),
	FUNCTION(CRI_TRNG0),
	FUNCTION(CRI_TRNG),
	FUNCTION(CRI_TRNG1),
	FUNCTION(GP_PDM2),
	FUNCTION(SP_CMU),
	FUNCTION(phase_flag6),
	FUNCTION(atest_usb2),
	FUNCTION(phase_flag5),
	FUNCTION(atest_usb23),
	FUNCTION(uim2_data),
	FUNCTION(uim2_clk),
	FUNCTION(uim2_reset),
	FUNCTION(phase_flag17),
	FUNCTION(atest_usb22),
	FUNCTION(uim2_present),
	FUNCTION(phase_flag16),
	FUNCTION(atest_usb21),
	FUNCTION(uim1_data),
	FUNCTION(phase_flag31),
	FUNCTION(atest_usb20),
	FUNCTION(uim1_clk),
	FUNCTION(phase_flag11),
	FUNCTION(uim1_reset),
	FUNCTION(phase_flag24),
	FUNCTION(uim1_present),
	FUNCTION(phase_flag14),
	FUNCTION(rgmii_rxd2),
	FUNCTION(mdp_vsync),
	FUNCTION(rgmii_rxd1),
	FUNCTION(phase_flag13),
	FUNCTION(rgmii_rxd0),
	FUNCTION(qdss_cti),
	FUNCTION(phase_flag12),
	FUNCTION(copy_gp),
	FUNCTION(usb0_hs_ac),
	FUNCTION(emac_phy),
	FUNCTION(pcie_ep),
	FUNCTION(tgu_ch3),
	FUNCTION(usb1_hs_ac),
	FUNCTION(mdp_vsync0),
	FUNCTION(mdp_vsync1),
	FUNCTION(mdp_vsync2),
	FUNCTION(mdp_vsync3),
	FUNCTION(mdp_vsync4),
	FUNCTION(mdp_vsync5),
	FUNCTION(pcie_clk),
	FUNCTION(tgu_ch0),
	FUNCTION(rgmii_sync),
	FUNCTION(tgu_ch1),
	FUNCTION(rgmii_txc),
	FUNCTION(vfr_1),
	FUNCTION(tgu_ch2),
	FUNCTION(phase_flag30),
	FUNCTION(rgmii_txd3),
	FUNCTION(rgmii_txd2),
	FUNCTION(rgmii_txd1),
	FUNCTION(rgmii_txd0),
	FUNCTION(rgmii_tx),
	FUNCTION(ldo_en),
	FUNCTION(ldo_update),
	FUNCTION(prng_rosc),
	FUNCTION(emac_gcc0),
	FUNCTION(rgmii_rxc),
	FUNCTION(dp_hot),
	FUNCTION(egpio),
	FUNCTION(emac_gcc1),
	FUNCTION(rgmii_rxd3),
	FUNCTION(debug_hot),
	FUNCTION(COPY_PHASE),
	FUNCTION(usb_phy),
	FUNCTION(mss_lte),
	FUNCTION(mi2s_1),
	FUNCTION(WSA_DATA),
	FUNCTION(WSA_CLK),
	FUNCTION(rgmii_rx),
	FUNCTION(rgmii_mdc),
	FUNCTION(edp_hot),
	FUNCTION(rgmii_mdio),
	FUNCTION(ter_mi2s),
	FUNCTION(atest_char),
	FUNCTION(phase_flag21),
	FUNCTION(phase_flag20),
	FUNCTION(atest_char3),
	FUNCTION(adsp_ext),
	FUNCTION(phase_flag19),
	FUNCTION(atest_char2),
	FUNCTION(edp_lcd),
	FUNCTION(phase_flag18),
	FUNCTION(atest_char1),
	FUNCTION(m_voc),
	FUNCTION(atest_char0),
	FUNCTION(mclk1),
	FUNCTION(mclk2),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup sm6150_groups[] = {
	[0] = PINGROUP(0, WEST, qup02, NA, qdss_gpio6, NA, NA, NA, NA, NA, NA),
	[1] = PINGROUP(1, WEST, qup02, NA, qdss_gpio7, NA, NA, NA, NA, NA, NA),
	[2] = PINGROUP(2, WEST, qup02, NA, qdss_gpio8, NA, NA, NA, NA, NA, NA),
	[3] = PINGROUP(3, WEST, qup02, NA, qdss_gpio9, NA, NA, NA, NA, NA, NA),
	[4] = PINGROUP(4, WEST, qup01, NA, NA, NA, NA, NA, NA, NA, NA),
	[5] = PINGROUP(5, WEST, qup01, NA, NA, NA, NA, NA, NA, NA, NA),
	[6] = PINGROUP(6, EAST, qup12, qdss_gpio0, ddr_pxi0, NA, NA, NA, NA,
		       NA, NA),
	[7] = PINGROUP(7, EAST, qup12, ddr_bist, qdss_gpio1, atest_tsens2,
		       vsense_trigger, atest_usb1, ddr_pxi0, NA, NA),
	[8] = PINGROUP(8, EAST, qup12, GP_PDM1, ddr_bist, qdss_gpio2, NA, NA,
		       NA, NA, NA),
	[9] = PINGROUP(9, EAST, qup12, ddr_bist, qdss_gpio3, NA, NA, NA, NA,
		       NA, NA),
	[10] = PINGROUP(10, EAST, qup13, ddr_bist, NA, phase_flag28,
			atest_usb11, ddr_pxi2, NA, NA, NA),
	[11] = PINGROUP(11, EAST, qup13, dbg_out, atest_usb10, ddr_pxi2, NA,
			NA, NA, NA, NA),
	[12] = PINGROUP(12, EAST, qup13, JITTER_BIST, ddr_pxi3, NA, NA, NA, NA,
			NA, NA),
	[13] = PINGROUP(13, EAST, qup13, pll_bypassnl, NA, ddr_pxi3, NA, NA,
			NA, NA, NA),
	[14] = PINGROUP(14, EAST, qup11, pll_reset, NA, qdss_gpio, NA, NA, NA,
			NA, NA),
	[15] = PINGROUP(15, EAST, qup11, qdss_gpio, NA, NA, NA, NA, NA, NA, NA),
	[16] = PINGROUP(16, WEST, qup00, NA, wlan2_adc1, NA, NA, NA, NA, NA,
			NA),
	[17] = PINGROUP(17, WEST, qup00, NA, wlan2_adc0, NA, NA, NA, NA, NA,
			NA),
	[18] = PINGROUP(18, WEST, qup03, NA, phase_flag3, NA, NA, NA, NA, NA,
			NA),
	[19] = PINGROUP(19, WEST, qup03, NA, phase_flag2, NA, NA, NA, NA, NA,
			NA),
	[20] = PINGROUP(20, SOUTH, qup10, NA, phase_flag1, qdss_gpio4, NA, NA,
			NA, NA, NA),
	[21] = PINGROUP(21, SOUTH, qup10, gcc_gp2, NA, qdss_gpio5, NA, NA, NA,
			NA, NA),
	[22] = PINGROUP(22, SOUTH, qup10, gcc_gp3, NA, NA, NA, NA, NA, NA, NA),
	[23] = PINGROUP(23, SOUTH, qup10, NA, phase_flag0, NA, NA, NA, NA, NA,
			NA),
	[24] = PINGROUP(24, EAST, hs1_mi2s, sd_write, NA, phase_flag29, NA, NA,
			NA, NA, NA),
	[25] = PINGROUP(25, EAST, hs1_mi2s, NA, phase_flag10, NA, NA, NA, NA,
			NA, NA),
	[26] = PINGROUP(26, EAST, cci_async, hs1_mi2s, JITTER_BIST, NA, NA, NA,
			NA, NA, NA),
	[27] = PINGROUP(27, EAST, hs1_mi2s, PLL_BIST, NA, NA, NA, NA, NA, NA,
			NA),
	[28] = PINGROUP(28, EAST, cam_mclk, AGERA_PLL, qdss_gpio4, NA, NA, NA,
			NA, NA, NA),
	[29] = PINGROUP(29, EAST, cam_mclk, NA, qdss_gpio5, atest_tsens, NA,
			NA, NA, NA, NA),
	[30] = PINGROUP(30, EAST, cam_mclk, qdss_gpio6, NA, NA, NA, NA, NA, NA,
			NA),
	[31] = PINGROUP(31, EAST, cam_mclk, NA, qdss_gpio7, NA, NA, NA, NA, NA,
			NA),
	[32] = PINGROUP(32, EAST, cci_i2c, NA, qdss_gpio8, NA, NA, NA, NA, NA,
			NA),
	[33] = PINGROUP(33, EAST, cci_i2c, NA, qdss_gpio9, NA, NA, NA, NA, NA,
			NA),
	[34] = PINGROUP(34, EAST, cci_i2c, NA, qdss_gpio10, NA, NA, NA, NA, NA,
			NA),
	[35] = PINGROUP(35, EAST, cci_i2c, NA, qdss_gpio11, NA, NA, NA, NA, NA,
			NA),
	[36] = PINGROUP(36, EAST, hs0_mi2s, NA, NA, NA, NA, NA, NA, NA, NA),
	[37] = PINGROUP(37, EAST, cci_timer2, hs0_mi2s, NA, NA, NA, NA, NA, NA,
			NA),
	[38] = PINGROUP(38, EAST, cci_timer0, hs0_mi2s, NA, phase_flag15, NA,
			NA, NA, NA, NA),
	[39] = PINGROUP(39, EAST, cci_timer1, hs0_mi2s, NA, NA, NA, NA, NA, NA,
			NA),
	[40] = PINGROUP(40, EAST, NA, phase_flag27, NA, NA, NA, NA, NA, NA, NA),
	[41] = PINGROUP(41, EAST, cci_async, cci_timer3, NA, phase_flag26, NA,
			NA, NA, NA, NA),
	[42] = PINGROUP(42, EAST, cci_async, cci_timer4, NA, phase_flag25, NA,
			NA, NA, NA, NA),
	[43] = PINGROUP(43, SOUTH, NA, phase_flag9, NA, NA, NA, NA, NA, NA, NA),
	[44] = PINGROUP(44, EAST, qspi_cs, NA, phase_flag8, qdss_gpio12, NA,
			NA, NA, NA, NA),
	[45] = PINGROUP(45, EAST, qspi0, NA, phase_flag7, qdss_gpio13, NA, NA,
			NA, NA, NA),
	[46] = PINGROUP(46, EAST, qspi1, NA, qdss_gpio14, NA, NA, NA, NA, NA,
			NA),
	[47] = PINGROUP(47, EAST, qspi2, NA, qdss_gpio15, wlan1_adc1, NA, NA,
			NA, NA, NA),
	[48] = PINGROUP(48, EAST, qspi_clk, NA, wlan1_adc0, NA, NA, NA, NA, NA,
			NA),
	[49] = PINGROUP(49, EAST, qspi3, NA, NA, NA, NA, NA, NA, NA, NA),
	[50] = PINGROUP(50, EAST, qspi_cs, NA, NA, NA, NA, NA, NA, NA, NA),
	[51] = PINGROUP(51, SOUTH, qlink_request, NA, NA, NA, NA, NA, NA, NA,
			NA),
	[52] = PINGROUP(52, SOUTH, qlink_enable, NA, NA, NA, NA, NA, NA, NA,
			NA),
	[53] = PINGROUP(53, SOUTH, pa_indicator, NAV_PPS_IN, NAV_PPS_OUT,
			GPS_TX, NA, phase_flag23, NA, NA, NA),
	[54] = PINGROUP(54, SOUTH, NA, GPS_TX, GP_PDM0, NA, phase_flag22,
			atest_usb13, ddr_pxi1, NA, NA),
	[55] = PINGROUP(55, SOUTH, NA, NA, phase_flag4, atest_usb12, ddr_pxi1,
			NA, NA, NA, NA),
	[56] = PINGROUP(56, SOUTH, NA, NAV_PPS_IN, NAV_PPS_OUT, GPS_TX, NA,
			NA, NA, NA, NA),
	[57] = PINGROUP(57, SOUTH, NA, NAV_PPS_IN, GPS_TX, NAV_PPS_OUT,
			gcc_gp1, NA, NA, NA, NA),
	[58] = PINGROUP(58, SOUTH, NA, gcc_gp2, NA, NA, NA, NA, NA, NA, NA),
	[59] = PINGROUP(59, SOUTH, NA, NAV_PPS_IN, NAV_PPS_OUT, GPS_TX,
			gcc_gp3, NA, NA, NA, NA),
	[60] = PINGROUP(60, SOUTH, NA, NAV_PPS_IN, NAV_PPS_OUT, GPS_TX,
			CRI_TRNG0, NA, NA, NA, NA),
	[61] = PINGROUP(61, SOUTH, NA, CRI_TRNG, NA, NA, NA, NA, NA, NA, NA),
	[62] = PINGROUP(62, SOUTH, NA, CRI_TRNG1, NA, NA, NA, NA, NA, NA, NA),
	[63] = PINGROUP(63, SOUTH, NA, NA, GP_PDM2, NA, NA, NA, NA, NA, NA),
	[64] = PINGROUP(64, SOUTH, NA, SP_CMU, NA, NA, NA, NA, NA, NA, NA),
	[65] = PINGROUP(65, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[66] = PINGROUP(66, SOUTH, NA, GP_PDM1, NA, NA, NA, NA, NA, NA, NA),
	[67] = PINGROUP(67, SOUTH, NA, NA, NA, phase_flag6, atest_usb2, NA, NA,
			NA, NA),
	[68] = PINGROUP(68, SOUTH, NA, NA, NA, phase_flag5, atest_usb23, NA,
			NA, NA, NA),
	[69] = PINGROUP(69, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[70] = PINGROUP(70, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[71] = PINGROUP(71, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[72] = PINGROUP(72, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[73] = PINGROUP(73, SOUTH, uim2_data, NA, NA, NA, NA, NA, NA, NA, NA),
	[74] = PINGROUP(74, SOUTH, uim2_clk, NA, NA, NA, NA, NA, NA, NA, NA),
	[75] = PINGROUP(75, SOUTH, uim2_reset, NA, phase_flag17, atest_usb22,
			NA, NA, NA, NA, NA),
	[76] = PINGROUP(76, SOUTH, uim2_present, NA, phase_flag16, atest_usb21,
			NA, NA, NA, NA, NA),
	[77] = PINGROUP(77, SOUTH, uim1_data, NA, phase_flag31, atest_usb20,
			NA, NA, NA, NA, NA),
	[78] = PINGROUP(78, SOUTH, uim1_clk, gcc_gp1, NA, phase_flag11, NA, NA,
			NA, NA, NA),
	[79] = PINGROUP(79, SOUTH, uim1_reset, GP_PDM2, NA, phase_flag24, NA,
			NA, NA, NA, NA),
	[80] = PINGROUP(80, SOUTH, uim1_present, NA, phase_flag14, NA, NA, NA,
			NA, NA, NA),
	[81] = PINGROUP(81, WEST, rgmii_rxd2, mdp_vsync, NA, qdss_gpio14, NA,
			NA, NA, NA, NA),
	[82] = PINGROUP(82, WEST, rgmii_rxd1, mdp_vsync, NA, phase_flag13,
			qdss_gpio15, NA, NA, NA, NA),
	[83] = PINGROUP(83, WEST, rgmii_rxd0, mdp_vsync, NA, qdss_cti, NA, NA,
			NA, NA, NA),
	[84] = PINGROUP(84, SOUTH, NA, phase_flag12, NA, NA, NA, NA, NA, NA,
			NA),
	[85] = PINGROUP(85, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, egpio),
	[86] = PINGROUP(86, SOUTH, copy_gp, NA, NA, NA, NA, NA, NA, NA, NA),
	[87] = PINGROUP(87, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[88] = PINGROUP(88, WEST, NA, usb0_hs_ac, NA, NA, NA, NA, NA, NA, NA),
	[89] = PINGROUP(89, WEST, emac_phy, pcie_ep, tgu_ch3, usb1_hs_ac, NA,
			NA, NA, NA, NA),
	[90] = PINGROUP(90, WEST, mdp_vsync, mdp_vsync0, mdp_vsync1,
			mdp_vsync2, mdp_vsync3, mdp_vsync4, mdp_vsync5,
			pcie_clk, tgu_ch0),
	[91] = PINGROUP(91, WEST, rgmii_sync, tgu_ch1, NA, NA, NA, NA, NA, NA,
			NA),
	[92] = PINGROUP(92, WEST, rgmii_txc, vfr_1, tgu_ch2, NA, phase_flag30,
			qdss_gpio10, NA, NA, NA),
	[93] = PINGROUP(93, WEST, rgmii_txd3, qdss_gpio11, NA, NA, NA, NA, NA,
			NA, NA),
	[94] = PINGROUP(94, WEST, rgmii_txd2, qdss_gpio12, NA, NA, NA, NA, NA,
			NA, NA),
	[95] = PINGROUP(95, WEST, rgmii_txd1, GP_PDM0, qdss_gpio13, NA, NA, NA,
			NA, NA, NA),
	[96] = PINGROUP(96, WEST, rgmii_txd0, qdss_cti, NA, NA, NA, NA, NA, NA,
			NA),
	[97] = PINGROUP(97, WEST, rgmii_tx, mdp_vsync, ldo_en, qdss_cti, NA,
			NA, NA, NA, NA),
	[98] = PINGROUP(98, WEST, mdp_vsync, ldo_update, qdss_cti, NA, NA, NA,
			NA, NA, egpio),
	[99] = PINGROUP(99, EAST, prng_rosc, NA, NA, NA, NA, NA, NA, NA, NA),
	[100] = PINGROUP(100, WEST, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[101] = PINGROUP(101, WEST, emac_gcc0, NA, NA, NA, NA, NA, NA, NA, NA),
	[102] = PINGROUP(102, WEST, rgmii_rxc, dp_hot, emac_gcc1, prng_rosc,
			 NA, NA, NA, NA, NA),
	[103] = PINGROUP(103, WEST, rgmii_rxd3, debug_hot, COPY_PHASE,
			 qdss_cti, NA, NA, NA, NA, NA),
	[104] = PINGROUP(104, WEST, usb_phy, NA, qdss_cti, NA, NA, NA, NA, NA,
			 NA),
	[105] = PINGROUP(105, SOUTH, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	[106] = PINGROUP(106, EAST, mss_lte, NA, NA, NA, NA, NA, NA, NA, NA),
	[107] = PINGROUP(107, EAST, mss_lte, NA, NA, NA, NA, NA, NA, NA, NA),
	[108] = PINGROUP(108, SOUTH, mi2s_1, NA, qdss_gpio, NA, NA, NA, NA, NA,
			 NA),
	[109] = PINGROUP(109, SOUTH, mi2s_1, NA, qdss_gpio, NA, NA, NA, NA, NA,
			 NA),
	[110] = PINGROUP(110, SOUTH, WSA_DATA, mi2s_1, NA, NA, NA, NA, NA, NA,
			 NA),
	[111] = PINGROUP(111, SOUTH, WSA_CLK, mi2s_1, NA, NA, NA, NA, NA, NA,
			 NA),
	[112] = PINGROUP(112, WEST, rgmii_rx, NA, qdss_cti, NA, NA, NA, NA, NA,
			 NA),
	[113] = PINGROUP(113, WEST, rgmii_mdc, edp_hot, NA, qdss_cti, NA, NA,
			 NA, NA, NA),
	[114] = PINGROUP(114, WEST, rgmii_mdio, NA, NA, NA, NA, NA, NA, NA, NA),
	[115] = PINGROUP(115, SOUTH, ter_mi2s, atest_char, NA, NA, NA, NA, NA,
			 NA, NA),
	[116] = PINGROUP(116, SOUTH, ter_mi2s, NA, phase_flag21, NA, NA, NA,
			 NA, NA, NA),
	[117] = PINGROUP(117, SOUTH, ter_mi2s, NA, phase_flag20, qdss_gpio0,
			 atest_char3, NA, NA, NA, NA),
	[118] = PINGROUP(118, SOUTH, ter_mi2s, adsp_ext, NA, phase_flag19,
			 qdss_gpio1, atest_char2, NA, NA, NA),
	[119] = PINGROUP(119, SOUTH, edp_lcd, NA, phase_flag18, qdss_gpio2,
			 atest_char1, NA, NA, NA, NA),
	[120] = PINGROUP(120, SOUTH, m_voc, qdss_gpio3, atest_char0, NA, NA,
			 NA, NA, NA, NA),
	[121] = PINGROUP(121, SOUTH, mclk1, NA, NA, NA, NA, NA, NA, NA, NA),
	[122] = PINGROUP(122, SOUTH, mclk2, NA, NA, NA, NA, NA, NA, NA, NA),
	[123] = UFS_RESET(ufs_reset, 0x59f000),
	[124] = SDC_QDSD_PINGROUP(sdc1_rclk, 0x59a000, 15, 0),
	[125] = SDC_QDSD_PINGROUP(sdc1_clk, 0x59a000, 13, 6),
	[126] = SDC_QDSD_PINGROUP(sdc1_cmd, 0x59a000, 11, 3),
	[127] = SDC_QDSD_PINGROUP(sdc1_data, 0x59a000, 9, 0),
	[128] = SDC_QDSD_PINGROUP(sdc2_clk, 0xd98000, 14, 6),
	[129] = SDC_QDSD_PINGROUP(sdc2_cmd, 0xd98000, 11, 3),
	[130] = SDC_QDSD_PINGROUP(sdc2_data, 0xd98000, 9, 0),
};

static struct msm_dir_conn sm6150_dir_conn[] = {
	  {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0},
	  {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}
};

#ifdef CONFIG_HIBERNATION
static u32 tile_dir_conn_addr[NUM_TILES] = {
	[0] =   SOUTH + SOUTH_PDC_OFFSET,
	[1] =   WEST + WEST_PDC_OFFSET,
	[2] =   EAST + EAST_PDC_OFFSET
};
#endif

static struct msm_pinctrl_soc_data sm6150_pinctrl = {
	.pins = sm6150_pins,
	.npins = ARRAY_SIZE(sm6150_pins),
	.functions = sm6150_functions,
	.nfunctions = ARRAY_SIZE(sm6150_functions),
	.groups = sm6150_groups,
	.ngroups = ARRAY_SIZE(sm6150_groups),
	.ngpios = 124,
	.dir_conn = sm6150_dir_conn,
	.egpio_func = 9,
	.ntiles = NUM_TILES,
#ifdef CONFIG_HIBERNATION
	.dir_conn_addr = tile_dir_conn_addr,
#endif
};

static int sm6150_pinctrl_dirconn_list_probe(struct platform_device *pdev)
{
	int ret, n, dirconn_list_count, m;
	struct device_node *np = pdev->dev.of_node;

	n = of_property_count_elems_of_size(np, "qcom,dirconn-list",
					sizeof(u32));
	if (n <= 0 || n % 2)
		return -EINVAL;

	m = ARRAY_SIZE(sm6150_dir_conn) - 1;

	dirconn_list_count = n / 2;

	for (n = 0; n < dirconn_list_count; n++) {
		ret = of_property_read_u32_index(np, "qcom,dirconn-list",
						n * 2 + 0,
						&sm6150_dir_conn[m].gpio);
		if (ret)
			return ret;
		ret = of_property_read_u32_index(np, "qcom,dirconn-list",
						n * 2 + 1,
						&sm6150_dir_conn[m].irq);
		if (ret)
			return ret;
		m--;
	}

	return 0;
}

static int sm6150_pinctrl_probe(struct platform_device *pdev)
{
	int len, ret;

	if (of_find_property(pdev->dev.of_node, "qcom,dirconn-list", &len)) {
		ret = sm6150_pinctrl_dirconn_list_probe(pdev);
		if (ret) {
			dev_err(&pdev->dev,
				"Unable to parse Direct Connect List\n");
			return ret;
		}
	}

	return msm_pinctrl_probe(pdev, &sm6150_pinctrl);
}

static const struct of_device_id sm6150_pinctrl_of_match[] = {
	{ .compatible = "qcom,sm6150-pinctrl", },
	{ },
};

static struct platform_driver sm6150_pinctrl_driver = {
	.driver = {
		.name = "sm6150-pinctrl",
		.of_match_table = sm6150_pinctrl_of_match,
	},
	.probe = sm6150_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init sm6150_pinctrl_init(void)
{
	return platform_driver_register(&sm6150_pinctrl_driver);
}
arch_initcall(sm6150_pinctrl_init);

static void __exit sm6150_pinctrl_exit(void)
{
	platform_driver_unregister(&sm6150_pinctrl_driver);
}
module_exit(sm6150_pinctrl_exit);

MODULE_DESCRIPTION("QTI sm6150 pinctrl driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, sm6150_pinctrl_of_match);
