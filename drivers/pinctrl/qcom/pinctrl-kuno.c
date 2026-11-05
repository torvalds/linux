// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

#define REG_BASE 0x100000
#define REG_SIZE 0x1000
#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)		\
	{					        \
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
		},				        \
		.nfuncs = 10,				\
		.ctl_reg = REG_BASE + REG_SIZE * id,			\
		.io_reg = REG_BASE + 0x4 + REG_SIZE * id,		\
		.intr_cfg_reg = REG_BASE + 0x8 + REG_SIZE * id,		\
		.intr_status_reg = REG_BASE + 0xc + REG_SIZE * id,	\
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

static const struct pinctrl_pin_desc kuno_pins[] = {
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

enum kuno_functions {
	msm_mux_gpio,
	msm_mux_audio_ref_clk,
	msm_mux_coex_uart,
	msm_mux_ebi2_lcd_a,
	msm_mux_ebi2_lcd_cs,
	msm_mux_ebi2_lcd_reset,
	msm_mux_ebi2_lcd_te,
	msm_mux_emac_mdc,
	msm_mux_emac_mdio,
	msm_mux_emac_pps_in,
	msm_mux_emac_ptp_aux,
	msm_mux_emac_ptp_pps,
	msm_mux_gcc_gp1_clk,
	msm_mux_gcc_gp2_clk,
	msm_mux_gcc_gp3_clk,
	msm_mux_mi2s0_data0,
	msm_mux_mi2s0_data1,
	msm_mux_mi2s0_sck,
	msm_mux_mi2s0_ws,
	msm_mux_mi2s1_data0,
	msm_mux_mi2s1_data1,
	msm_mux_mi2s1_sck,
	msm_mux_mi2s1_ws,
	msm_mux_mi2s_mclk,
	msm_mux_nav_gpio,
	msm_mux_pci_e_rst,
	msm_mux_pcie_clkreq_n,
	msm_mux_pll_bist_sync,
	msm_mux_pll_clk_aux,
	msm_mux_qdss_cti_trig0,
	msm_mux_qdss_cti_trig1,
	msm_mux_qdss_cti_trig1_mire,
	msm_mux_qdss_gpio_traceclk,
	msm_mux_qdss_gpio_tracectl,
	msm_mux_qup0_se0,
	msm_mux_qup0_se1,
	msm_mux_qup0_se2,
	msm_mux_qup0_se3_mira,
	msm_mux_qup0_se3_mirb,
	msm_mux_qup0_se4,
	msm_mux_sdc4_clk,
	msm_mux_sdc4_cmd,
	msm_mux_sdc4_data,
	msm_mux_sdc4_tb_trig,
	msm_mux_sgmii_phy_intr,
	msm_mux_spmi_coex_clk,
	msm_mux_spmi_coex_data,
	msm_mux_spmi_vgi_hwevent,
	msm_mux_uim1_clk,
	msm_mux_uim1_data,
	msm_mux_uim1_present,
	msm_mux_uim1_reset,
	msm_mux_usb2phy_ac_en,
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
	"gpio105", "gpio106", "gpio107", "gpio108", "gpio109",
};

static const char * const audio_ref_clk_groups[] = {
	"gpio62",
};

static const char * const coex_uart_groups[] = {
	"gpio44", "gpio45",
};

static const char * const ebi2_lcd_a_groups[] = {
	"gpio93",
};

static const char * const ebi2_lcd_cs_groups[] = {
	"gpio94",
};

static const char * const ebi2_lcd_reset_groups[] = {
	"gpio89",
};

static const char * const ebi2_lcd_te_groups[] = {
	"gpio88",
};

static const char * const emac_mdc_groups[] = {
	"gpio98",
};

static const char * const emac_mdio_groups[] = {
	"gpio99",
};

static const char * const emac_pps_in_groups[] = {
	"gpio88",
};

static const char * const emac_ptp_aux_groups[] = {
	"gpio93", "gpio94",
};

static const char * const emac_ptp_pps_groups[] = {
	"gpio93", "gpio94",
};

static const char * const gcc_gp1_clk_groups[] = {
	"gpio34",
};

static const char * const gcc_gp2_clk_groups[] = {
	"gpio46",
};

static const char * const gcc_gp3_clk_groups[] = {
	"gpio35",
};

static const char * const mi2s0_data0_groups[] = {
	"gpio13",
};

static const char * const mi2s0_data1_groups[] = {
	"gpio14",
};

static const char * const mi2s0_sck_groups[] = {
	"gpio15",
};

static const char * const mi2s0_ws_groups[] = {
	"gpio12",
};

static const char * const mi2s1_data0_groups[] = {
	"gpio17",
};

static const char * const mi2s1_data1_groups[] = {
	"gpio18",
};

static const char * const mi2s1_sck_groups[] = {
	"gpio19",
};

static const char * const mi2s1_ws_groups[] = {
	"gpio16",
};

static const char * const mi2s_mclk_groups[] = {
	"gpio62",
};

static const char * const nav_gpio_groups[] = {
	"gpio31", "gpio32", "gpio33",
};

static const char * const pci_e_rst_groups[] = {
	"gpio57",
};

static const char * const pcie_clkreq_n_groups[] = {
	"gpio56",
};

static const char * const pll_bist_sync_groups[] = {
	"gpio47",
};

static const char * const pll_clk_aux_groups[] = {
	"gpio54",
};

static const char * const qdss_cti_trig0_groups[] = {
	"gpio16", "gpio17", "gpio54", "gpio55", "gpio60", "gpio84", "gpio101",
	"gpio102",
};

static const char * const qdss_cti_trig1_groups[] = {
	"gpio16", "gpio17", "gpio54", "gpio55", "gpio65", "gpio66", "gpio101",
	"gpio102",
};
static const char * const qdss_cti_trig1_mire_groups[] = {
	"gpio65", "gpio66",
};

static const char * const qdss_gpio_traceclk_groups[] = {
	"gpio8",
};

static const char * const qdss_gpio_tracectl_groups[] = {
	"gpio9",
};

static const char * const qup0_se0_groups[] = {
	"gpio14", "gpio18", "gpio19", "gpio48", "gpio49", "gpio92", "gpio101",
};

static const char * const qup0_se1_groups[] = {
	"gpio14", "gpio63", "gpio64", "gpio65", "gpio66", "gpio92", "gpio101",
};

static const char * const qup0_se2_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7", "gpio14", "gpio92", "gpio101",
};

static const char * const qup0_se3_mira_groups[] = {
	"gpio8", "gpio9", "gpio14", "gpio16", "gpio17", "gpio92", "gpio101",
};

static const char * const qup0_se3_mirb_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15",
};

static const char * const qup0_se4_groups[] = {
	"gpio10", "gpio11", "gpio14", "gpio80", "gpio81", "gpio92", "gpio101",
};

static const char * const sdc4_clk_groups[] = {
	"gpio103",
};

static const char * const sdc4_cmd_groups[] = {
	"gpio102",
};

static const char * const sdc4_data_groups[] = {
	"gpio104", "gpio105", "gpio106", "gpio107",
};

static const char * const sdc4_tb_trig_groups[] = {
	"gpio101",
};

static const char * const sgmii_phy_intr_groups[] = {
	"gpio91",
};

static const char * const spmi_coex_clk_groups[] = {
	"gpio76",
};

static const char * const spmi_coex_data_groups[] = {
	"gpio75",
};

static const char * const spmi_vgi_hwevent_groups[] = {
	"gpio78", "gpio79",
};

static const char * const uim1_clk_groups[] = {
	"gpio3",
};

static const char * const uim1_data_groups[] = {
	"gpio0",
};

static const char * const uim1_present_groups[] = {
	"gpio1",
};

static const char * const uim1_reset_groups[] = {
	"gpio2",
};

static const char * const usb2phy_ac_en_groups[] = {
	"gpio90",
};

static const struct pinfunction kuno_functions[] = {
	MSM_GPIO_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(audio_ref_clk),
	MSM_PIN_FUNCTION(coex_uart),
	MSM_PIN_FUNCTION(ebi2_lcd_a),
	MSM_PIN_FUNCTION(ebi2_lcd_cs),
	MSM_PIN_FUNCTION(ebi2_lcd_reset),
	MSM_PIN_FUNCTION(ebi2_lcd_te),
	MSM_PIN_FUNCTION(emac_mdc),
	MSM_PIN_FUNCTION(emac_mdio),
	MSM_PIN_FUNCTION(emac_pps_in),
	MSM_PIN_FUNCTION(emac_ptp_aux),
	MSM_PIN_FUNCTION(emac_ptp_pps),
	MSM_PIN_FUNCTION(gcc_gp1_clk),
	MSM_PIN_FUNCTION(gcc_gp2_clk),
	MSM_PIN_FUNCTION(gcc_gp3_clk),
	MSM_PIN_FUNCTION(mi2s0_data0),
	MSM_PIN_FUNCTION(mi2s0_data1),
	MSM_PIN_FUNCTION(mi2s0_sck),
	MSM_PIN_FUNCTION(mi2s0_ws),
	MSM_PIN_FUNCTION(mi2s1_data0),
	MSM_PIN_FUNCTION(mi2s1_data1),
	MSM_PIN_FUNCTION(mi2s1_sck),
	MSM_PIN_FUNCTION(mi2s1_ws),
	MSM_PIN_FUNCTION(mi2s_mclk),
	MSM_PIN_FUNCTION(nav_gpio),
	MSM_PIN_FUNCTION(pci_e_rst),
	MSM_PIN_FUNCTION(pcie_clkreq_n),
	MSM_PIN_FUNCTION(pll_bist_sync),
	MSM_PIN_FUNCTION(pll_clk_aux),
	MSM_PIN_FUNCTION(qdss_cti_trig0),
	MSM_PIN_FUNCTION(qdss_cti_trig1),
	MSM_PIN_FUNCTION(qdss_cti_trig1_mire),
	MSM_PIN_FUNCTION(qdss_gpio_traceclk),
	MSM_PIN_FUNCTION(qdss_gpio_tracectl),
	MSM_PIN_FUNCTION(qup0_se0),
	MSM_PIN_FUNCTION(qup0_se1),
	MSM_PIN_FUNCTION(qup0_se2),
	MSM_PIN_FUNCTION(qup0_se3_mira),
	MSM_PIN_FUNCTION(qup0_se3_mirb),
	MSM_PIN_FUNCTION(qup0_se4),
	MSM_PIN_FUNCTION(sdc4_clk),
	MSM_PIN_FUNCTION(sdc4_cmd),
	MSM_PIN_FUNCTION(sdc4_data),
	MSM_PIN_FUNCTION(sdc4_tb_trig),
	MSM_PIN_FUNCTION(sgmii_phy_intr),
	MSM_PIN_FUNCTION(spmi_coex_clk),
	MSM_PIN_FUNCTION(spmi_coex_data),
	MSM_PIN_FUNCTION(spmi_vgi_hwevent),
	MSM_PIN_FUNCTION(uim1_clk),
	MSM_PIN_FUNCTION(uim1_data),
	MSM_PIN_FUNCTION(uim1_present),
	MSM_PIN_FUNCTION(uim1_reset),
	MSM_PIN_FUNCTION(usb2phy_ac_en),
};

/* Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup kuno_groups[] = {
	[0] = PINGROUP(0, uim1_data, _, _, _, _, _, _, _, _),
	[1] = PINGROUP(1, uim1_present, _, _, _, _, _, _, _, _),
	[2] = PINGROUP(2, uim1_reset, _, _, _, _, _, _, _, _),
	[3] = PINGROUP(3, uim1_clk, _, _, _, _, _, _, _, _),
	[4] = PINGROUP(4, qup0_se2, _, _, _, _, _, _, _, _),
	[5] = PINGROUP(5, qup0_se2, _, _, _, _, _, _, _, _),
	[6] = PINGROUP(6, qup0_se2, _, _, _, _, _, _, _, _),
	[7] = PINGROUP(7, qup0_se2, _, _, _, _, _, _, _, _),
	[8] = PINGROUP(8, qup0_se3_mira, _, qdss_gpio_traceclk, _, _, _, _, _, _),
	[9] = PINGROUP(9, qup0_se3_mira, _, qdss_gpio_tracectl, _, _, _, _, _, _),
	[10] = PINGROUP(10, qup0_se4, _, _, _, _, _, _, _, _),
	[11] = PINGROUP(11, qup0_se4, _, _, _, _, _, _, _, _),
	[12] = PINGROUP(12, mi2s0_ws, qup0_se3_mirb, _, _, _, _, _, _, _),
	[13] = PINGROUP(13, mi2s0_data0, qup0_se3_mirb, _, _, _, _, _, _, _),
	[14] = PINGROUP(14, mi2s0_data1, qup0_se3_mirb, qup0_se0, qup0_se1, qup0_se2,
			qup0_se3_mira, qup0_se4, _, _),
	[15] = PINGROUP(15, mi2s0_sck, qup0_se3_mirb, _, _, _, _, _, _, _),
	[16] = PINGROUP(16, mi2s1_ws, qup0_se3_mira, qdss_cti_trig1, qdss_cti_trig0,
			_, _, _, _, _),
	[17] = PINGROUP(17, mi2s1_data0, qup0_se3_mira, qdss_cti_trig1, qdss_cti_trig0,
			_, _, _, _, _),
	[18] = PINGROUP(18, mi2s1_data1, qup0_se0, _, _, _, _, _, _, _),
	[19] = PINGROUP(19, mi2s1_sck, qup0_se0, _, _, _, _, _, _, _),
	[20] = PINGROUP(20, _, _, _, _, _, _, _, _, _),
	[21] = PINGROUP(21, _, _, _, _, _, _, _, _, _),
	[22] = PINGROUP(22, _, _, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, _, _, _, _, _, _, _, _, _),
	[24] = PINGROUP(24, _, _, _, _, _, _, _, _, _),
	[25] = PINGROUP(25, _, _, _, _, _, _, _, _, _),
	[26] = PINGROUP(26, _, _, _, _, _, _, _, _, _),
	[27] = PINGROUP(27, _, _, _, _, _, _, _, _, _),
	[28] = PINGROUP(28, _, _, _, _, _, _, _, _, _),
	[29] = PINGROUP(29, _, _, _, _, _, _, _, _, _),
	[30] = PINGROUP(30, _, _, _, _, _, _, _, _, _),
	[31] = PINGROUP(31, nav_gpio, _, _, _, _, _, _, _, _),
	[32] = PINGROUP(32, nav_gpio, _, _, _, _, _, _, _, _),
	[33] = PINGROUP(33, nav_gpio, _, _, _, _, _, _, _, _),
	[34] = PINGROUP(34, _, gcc_gp1_clk, _, _, _, _, _, _, _),
	[35] = PINGROUP(35, _, _, gcc_gp3_clk, _, _, _, _, _, _),
	[36] = PINGROUP(36, _, _, _, _, _, _, _, _, _),
	[37] = PINGROUP(37, _, _, _, _, _, _, _, _, _),
	[38] = PINGROUP(38, _, _, _, _, _, _, _, _, _),
	[39] = PINGROUP(39, _, _, _, _, _, _, _, _, _),
	[40] = PINGROUP(40, _, _, _, _, _, _, _, _, _),
	[41] = PINGROUP(41, _, _, _, _, _, _, _, _, _),
	[42] = PINGROUP(42, _, _, _, _, _, _, _, _, _),
	[43] = PINGROUP(43, _, _, _, _, _, _, _, _, _),
	[44] = PINGROUP(44, coex_uart, _, _, _, _, _, _, _, _),
	[45] = PINGROUP(45, coex_uart, _, _, _, _, _, _, _, _),
	[46] = PINGROUP(46, _, _, _, gcc_gp2_clk, _, _, _, _, _),
	[47] = PINGROUP(47, _, pll_bist_sync, _, _, _, _, _, _, _),
	[48] = PINGROUP(48, qup0_se0, _, _, _, _, _, _, _, _),
	[49] = PINGROUP(49, qup0_se0, _, _, _, _, _, _, _, _),
	[50] = PINGROUP(50, _, _, _, _, _, _, _, _, _),
	[51] = PINGROUP(51, _, _, _, _, _, _, _, _, _),
	[52] = PINGROUP(52, _, _, _, _, _, _, _, _, _),
	[53] = PINGROUP(53, _, _, _, _, _, _, _, _, _),
	[54] = PINGROUP(54, qdss_cti_trig1, qdss_cti_trig0, pll_clk_aux, _, _, _, _, _, _),
	[55] = PINGROUP(55, qdss_cti_trig1, qdss_cti_trig0, _, _, _, _, _, _, _),
	[56] = PINGROUP(56, pcie_clkreq_n, _, _, _, _, _, _, _, _),
	[57] = PINGROUP(57, pci_e_rst, _, _, _, _, _, _, _, _),
	[58] = PINGROUP(58, _, _, _, _, _, _, _, _, _),
	[59] = PINGROUP(59, _, _, _, _, _, _, _, _, _),
	[60] = PINGROUP(60, qdss_cti_trig0, _, _, _, _, _, _, _, _),
	[61] = PINGROUP(61, _, _, _, _, _, _, _, _, _),
	[62] = PINGROUP(62, mi2s_mclk, audio_ref_clk, _, _, _, _, _, _, _),
	[63] = PINGROUP(63, qup0_se1, _, _, _, _, _, _, _, _),
	[64] = PINGROUP(64, qup0_se1, _, _, _, _, _, _, _, _),
	[65] = PINGROUP(65, qup0_se1, qdss_cti_trig1, qdss_cti_trig1_mire, _, _, _, _, _, _),
	[66] = PINGROUP(66, qup0_se1, qdss_cti_trig1, qdss_cti_trig1_mire, _, _, _, _, _, _),
	[67] = PINGROUP(67, _, _, _, _, _, _, _, _, _),
	[68] = PINGROUP(68, _, _, _, _, _, _, _, _, _),
	[69] = PINGROUP(69, _, _, _, _, _, _, _, _, _),
	[70] = PINGROUP(70, _, _, _, _, _, _, _, _, _),
	[71] = PINGROUP(71, _, _, _, _, _, _, _, _, _),
	[72] = PINGROUP(72, _, _, _, _, _, _, _, _, _),
	[73] = PINGROUP(73, _, _, _, _, _, _, _, _, _),
	[74] = PINGROUP(74, _, _, _, _, _, _, _, _, _),
	[75] = PINGROUP(75, spmi_coex_data, _, _, _, _, _, _, _, _),
	[76] = PINGROUP(76, spmi_coex_clk, _, _, _, _, _, _, _, _),
	[77] = PINGROUP(77, _, _, _, _, _, _, _, _, _),
	[78] = PINGROUP(78, spmi_vgi_hwevent, _, _, _, _, _, _, _, _),
	[79] = PINGROUP(79, spmi_vgi_hwevent, _, _, _, _, _, _, _, _),
	[80] = PINGROUP(80, qup0_se4, _, _, _, _, _, _, _, _),
	[81] = PINGROUP(81, qup0_se4, _, _, _, _, _, _, _, _),
	[82] = PINGROUP(82, _, _, _, _, _, _, _, _, _),
	[83] = PINGROUP(83, _, _, _, _, _, _, _, _, _),
	[84] = PINGROUP(84, qdss_cti_trig0, _, _, _, _, _, _, _, _),
	[85] = PINGROUP(85, _, _, _, _, _, _, _, _, _),
	[86] = PINGROUP(86, _, _, _, _, _, _, _, _, _),
	[87] = PINGROUP(87, _, _, _, _, _, _, _, _, _),
	[88] = PINGROUP(88, ebi2_lcd_te, emac_pps_in, _, _, _, _, _, _, _),
	[89] = PINGROUP(89, ebi2_lcd_reset, _, _, _, _, _, _, _, _),
	[90] = PINGROUP(90, usb2phy_ac_en, _, _, _, _, _, _, _, _),
	[91] = PINGROUP(91, sgmii_phy_intr, _, _, _, _, _, _, _, _),
	[92] = PINGROUP(92, qup0_se0, qup0_se1, qup0_se2, qup0_se3_mira, qup0_se4, _, _, _, _),
	[93] = PINGROUP(93, ebi2_lcd_a, emac_ptp_pps, emac_ptp_aux, _, _, _, _, _, _),
	[94] = PINGROUP(94, ebi2_lcd_cs, emac_ptp_pps, emac_ptp_aux, _, _, _, _, _, _),
	[95] = PINGROUP(95, _, _, _, _, _, _, _, _, _),
	[96] = PINGROUP(96, _, _, _, _, _, _, _, _, _),
	[97] = PINGROUP(97, _, _, _, _, _, _, _, _, _),
	[98] = PINGROUP(98, emac_mdc, _, _, _, _, _, _, _, _),
	[99] = PINGROUP(99, emac_mdio, _, _, _, _, _, _, _, _),
	[100] = PINGROUP(100, _, _, _, _, _, _, _, _, _),
	[101] = PINGROUP(101, sdc4_tb_trig, qdss_cti_trig1, qdss_cti_trig0, qup0_se0,
			qup0_se1, qup0_se2, qup0_se3_mira, qup0_se4, _),
	[102] = PINGROUP(102, sdc4_cmd, qdss_cti_trig1, qdss_cti_trig0, _, _, _, _, _, _),
	[103] = PINGROUP(103, sdc4_clk, _, _, _, _, _, _, _, _),
	[104] = PINGROUP(104, sdc4_data, _, _, _, _, _, _, _, _),
	[105] = PINGROUP(105, sdc4_data, _, _, _, _, _, _, _, _),
	[106] = PINGROUP(106, sdc4_data, _, _, _, _, _, _, _, _),
	[107] = PINGROUP(107, sdc4_data, _, _, _, _, _, _, _, _),
	[108] = PINGROUP(108, _, _, _, _, _, _, _, _, _),
	[109] = PINGROUP(109, _, _, _, _, _, _, _, _, _),
};

static const struct msm_gpio_wakeirq_map kuno_pdc_map[] = {
	{ 1, 62 }, { 2, 83 }, { 4, 59 }, { 5, 107 }, { 6, 112 }, { 7, 119 },
	{ 8, 123 }, { 10, 52 }, { 11, 73 }, { 12, 74 }, { 13, 75 }, { 14, 76 },
	{ 15, 77 }, { 16, 81 }, { 17, 82 }, { 18, 80 }, { 19, 100 }, { 21, 88 },
	{ 22, 99 }, { 23, 84 }, { 24, 85 }, { 25, 97 }, { 26, 86 }, { 27, 102 },
	{ 28, 87 }, { 35, 103 }, { 43, 90 }, { 44, 60 }, { 45, 61 }, { 46, 104 },
	{ 47, 89 }, { 48, 105 }, { 49, 106 }, { 50, 101 }, { 52, 109 }, { 53, 79 },
	{ 54, 110 }, { 55, 111 }, { 56, 78 }, { 57, 71 }, { 60, 113 }, { 61, 114 },
	{ 63, 115 }, { 64, 116 }, { 65, 57 }, { 66, 117 }, { 68, 64 }, { 69, 118 },
	{ 71, 108 }, { 73, 120 }, { 74, 121 }, { 76, 47 }, { 77, 122 }, { 78, 65 },
	{ 79, 66 }, { 80, 124 }, { 81, 125 }, { 82, 126 }, { 83, 127 }, { 84, 93 },
	{ 85, 128 }, { 86, 129 }, { 87, 70 }, { 88, 130 }, { 96, 68 }, { 98, 50 },
	{ 99, 67 }, { 100, 51 }, { 101, 53 }, { 102, 54 }, { 103, 98 }, { 104, 91 },
	{ 105, 69 }, { 106, 55 }, { 107, 56 }, { 109, 72 },
};

static const struct msm_pinctrl_soc_data kuno_tlmm = {
	.pins = kuno_pins,
	.npins = ARRAY_SIZE(kuno_pins),
	.functions = kuno_functions,
	.nfunctions = ARRAY_SIZE(kuno_functions),
	.groups = kuno_groups,
	.ngroups = ARRAY_SIZE(kuno_groups),
	.ngpios = 110,
	.wakeirq_map = kuno_pdc_map,
	.nwakeirq_map = ARRAY_SIZE(kuno_pdc_map),
};

static int kuno_tlmm_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &kuno_tlmm);
}

static const struct of_device_id kuno_tlmm_of_match[] = {
	{ .compatible = "qcom,kuno-tlmm", },
	{ },
};
MODULE_DEVICE_TABLE(of, kuno_tlmm_of_match);

static struct platform_driver kuno_tlmm_driver = {
	.driver = {
		.name = "kuno-tlmm",
		.of_match_table = kuno_tlmm_of_match,
	},
	.probe = kuno_tlmm_probe,
};

static int __init kuno_tlmm_init(void)
{
	return platform_driver_register(&kuno_tlmm_driver);
}
arch_initcall(kuno_tlmm_init);

static void __exit kuno_tlmm_exit(void)
{
	platform_driver_unregister(&kuno_tlmm_driver);
}
module_exit(kuno_tlmm_exit);

MODULE_DESCRIPTION("QTI Kuno TLMM driver");
MODULE_LICENSE("GPL");
