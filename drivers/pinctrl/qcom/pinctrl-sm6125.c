// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-msm.h"

static const char * const sm6125_tiles[] = {
	"south",
	"east",
	"west"
};

enum {
	SOUTH,
	EAST,
	WEST
};

#define PINGROUP(id, _tile, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{						\
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
		},					\
		.nfuncs = 10,				\
		.ctl_reg = 0x1000 * id,		\
		.io_reg = 0x4 + 0x1000 * id,		\
		.intr_cfg_reg = 0x8 + 0x1000 * id,	\
		.intr_status_reg = 0xc + 0x1000 * id,	\
		.intr_target_reg = 0x8 + 0x1000 * id,	\
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
		.grp = PINCTRL_PINGROUP(#pg_name, 	\
			pg_name##_pins, 		\
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

#define UFS_RESET(pg_name, offset)				\
	{						\
		.grp = PINCTRL_PINGROUP(#pg_name, 	\
			pg_name##_pins, 		\
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
static const struct pinctrl_pin_desc sm6125_pins[] = {
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
	PINCTRL_PIN(134, "SDC1_RCLK"),
	PINCTRL_PIN(135, "SDC1_CLK"),
	PINCTRL_PIN(136, "SDC1_CMD"),
	PINCTRL_PIN(137, "SDC1_DATA"),
	PINCTRL_PIN(138, "SDC2_CLK"),
	PINCTRL_PIN(139, "SDC2_CMD"),
	PINCTRL_PIN(140, "SDC2_DATA"),
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
static const unsigned int sdc1_rclk_pins[] = { 134 };
static const unsigned int sdc1_clk_pins[] = { 135 };
static const unsigned int sdc1_cmd_pins[] = { 136 };
static const unsigned int sdc1_data_pins[] = { 137 };
static const unsigned int sdc2_clk_pins[] = { 138 };
static const unsigned int sdc2_cmd_pins[] = { 139 };
static const unsigned int sdc2_data_pins[] = { 140 };


enum sm6125_functions {
	msm_mux_qup00,
	msm_mux_gpio,
	msm_mux_qdss,
	msm_mux_qup01,
	msm_mux_qup02,
	msm_mux_ddr_pxi0,
	msm_mux_ddr_bist,
	msm_mux_atest_tsens2,
	msm_mux_vsense_trigger,
	msm_mux_atest_usb1,
	msm_mux_gp_pdm1,
	msm_mux_phase_flag,
	msm_mux_dbg_out,
	msm_mux_qup14,
	msm_mux_atest_usb11,
	msm_mux_ddr_pxi2,
	msm_mux_atest_usb10,
	msm_mux_jitter_bist,
	msm_mux_ddr_pxi3,
	msm_mux_pll_bypassnl,
	msm_mux_pll_bist,
	msm_mux_qup03,
	msm_mux_pll_reset,
	msm_mux_agera_pll,
	msm_mux_qdss_cti,
	msm_mux_qup04,
	msm_mux_wlan2_adc1,
	msm_mux_wlan2_adc0,
	msm_mux_wsa_clk,
	msm_mux_qup13,
	msm_mux_ter_mi2s,
	msm_mux_wsa_data,
	msm_mux_qup10,
	msm_mux_gcc_gp3,
	msm_mux_qup12,
	msm_mux_sd_write,
	msm_mux_qup11,
	msm_mux_cam_mclk,
	msm_mux_atest_tsens,
	msm_mux_cci_i2c,
	msm_mux_cci_timer2,
	msm_mux_cci_timer1,
	msm_mux_gcc_gp2,
	msm_mux_cci_async,
	msm_mux_cci_timer4,
	msm_mux_cci_timer0,
	msm_mux_gcc_gp1,
	msm_mux_cci_timer3,
	msm_mux_wlan1_adc1,
	msm_mux_wlan1_adc0,
	msm_mux_qlink_request,
	msm_mux_qlink_enable,
	msm_mux_pa_indicator,
	msm_mux_nav_pps,
	msm_mux_gps_tx,
	msm_mux_gp_pdm0,
	msm_mux_atest_usb13,
	msm_mux_ddr_pxi1,
	msm_mux_atest_usb12,
	msm_mux_cri_trng0,
	msm_mux_cri_trng,
	msm_mux_cri_trng1,
	msm_mux_gp_pdm2,
	msm_mux_sp_cmu,
	msm_mux_atest_usb2,
	msm_mux_atest_usb23,
	msm_mux_uim2_data,
	msm_mux_uim2_clk,
	msm_mux_uim2_reset,
	msm_mux_atest_usb22,
	msm_mux_uim2_present,
	msm_mux_atest_usb21,
	msm_mux_uim1_data,
	msm_mux_atest_usb20,
	msm_mux_uim1_clk,
	msm_mux_uim1_reset,
	msm_mux_uim1_present,
	msm_mux_mdp_vsync,
	msm_mux_copy_gp,
	msm_mux_tsense_pwm,
	msm_mux_mpm_pwr,
	msm_mux_tgu_ch3,
	msm_mux_mdp_vsync0,
	msm_mux_mdp_vsync1,
	msm_mux_mdp_vsync2,
	msm_mux_mdp_vsync3,
	msm_mux_mdp_vsync4,
	msm_mux_mdp_vsync5,
	msm_mux_tgu_ch0,
	msm_mux_tgu_ch1,
	msm_mux_atest_char1,
	msm_mux_vfr_1,
	msm_mux_tgu_ch2,
	msm_mux_atest_char0,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_ldo_en,
	msm_mux_ldo_update,
	msm_mux_prng_rosc,
	msm_mux_dp_hot,
	msm_mux_debug_hot,
	msm_mux_copy_phase,
	msm_mux_usb_phy,
	msm_mux_atest_char,
	msm_mux_unused1,
	msm_mux_qua_mi2s,
	msm_mux_mss_lte,
	msm_mux_swr_tx,
	msm_mux_aud_sb,
	msm_mux_unused2,
	msm_mux_swr_rx,
	msm_mux_edp_hot,
	msm_mux_audio_ref,
	msm_mux_pri_mi2s,
	msm_mux_pri_mi2s_ws,
	msm_mux_adsp_ext,
	msm_mux_edp_lcd,
	msm_mux_mclk2,
	msm_mux_m_voc,
	msm_mux_mclk1,
	msm_mux_qca_sb,
	msm_mux_qui_mi2s,
	msm_mux_dmic0_clk,
	msm_mux_sec_mi2s,
	msm_mux_dmic0_data,
	msm_mux_dmic1_clk,
	msm_mux_dmic1_data,
	msm_mux__,
};

static const char * const qup00_groups[] = {
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
	"gpio129", "gpio130", "gpio131", "gpio132",
};
static const char * const qdss_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio20", "gpio21", "gpio34", "gpio35",
	"gpio36", "gpio42", "gpio41", "gpio43", "gpio44", "gpio45", "gpio46",
	"gpio47", "gpio48", "gpio49", "gpio80", "gpio81", "gpio82", "gpio83",
	"gpio84", "gpio85", "gpio86", "gpio91", "gpio92", "gpio94", "gpio96",
	"gpio100", "gpio102", "gpio114", "gpio115", "gpio116", "gpio117", "gpio118",
};
static const char * const qup01_groups[] = {
	"gpio4", "gpio5",
};
static const char * const qup02_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9",
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
static const char * const gp_pdm1_groups[] = {
	"gpio8", "gpio65",
};
static const char * const phase_flag_groups[] = {
	"gpio8", "gpio9", "gpio23", "gpio24", "gpio25", "gpio26", "gpio28",
	"gpio29", "gpio30", "gpio53", "gpio54", "gpio55", "gpio56", "gpio57",
	"gpio58", "gpio59", "gpio60", "gpio61", "gpio62", "gpio80", "gpio81",
	"gpio82", "gpio83", "gpio84", "gpio88", "gpio89", "gpio91", "gpio93",
	"gpio98", "gpio129", "gpio130", "gpio131",
};
static const char * const dbg_out_groups[] = {
	"gpio9",
};
static const char * const qup14_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13",
};
static const char * const atest_usb11_groups[] = {
	"gpio10",
};
static const char * const ddr_pxi2_groups[] = {
	"gpio10", "gpio11",
};
static const char * const atest_usb10_groups[] = {
	"gpio11",
};
static const char * const jitter_bist_groups[] = {
	"gpio12", "gpio31",
};
static const char * const ddr_pxi3_groups[] = {
	"gpio12", "gpio13",
};
static const char * const pll_bypassnl_groups[] = {
	"gpio13",
};
static const char * const pll_bist_groups[] = {
	"gpio13", "gpio32",
};
static const char * const qup03_groups[] = {
	"gpio14", "gpio15",
};
static const char * const pll_reset_groups[] = {
	"gpio14",
};
static const char * const agera_pll_groups[] = {
	"gpio14", "gpio33",
};
static const char * const qdss_cti_groups[] = {
	"gpio14", "gpio15", "gpio95", "gpio101", "gpio106", "gpio107",
	"gpio110", "gpio111",
};
static const char * const qup04_groups[] = {
	"gpio16", "gpio17",
};
static const char * const wlan2_adc1_groups[] = {
	"gpio16",
};
static const char * const wlan2_adc0_groups[] = {
	"gpio17",
};
static const char * const wsa_clk_groups[] = {
	"gpio18",
};
static const char * const qup13_groups[] = {
	"gpio18", "gpio19", "gpio20", "gpio21",
};
static const char * const ter_mi2s_groups[] = {
	"gpio18", "gpio19", "gpio20", "gpio21",
};
static const char * const wsa_data_groups[] = {
	"gpio19",
};
static const char * const qup10_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio27",
};
static const char * const gcc_gp3_groups[] = {
	"gpio22", "gpio58",
};
static const char * const qup12_groups[] = {
	"gpio28", "gpio29",
};
static const char * const sd_write_groups[] = {
	"gpio29",
};
static const char * const qup11_groups[] = {
	"gpio30", "gpio31", "gpio32", "gpio33",
};
static const char * const cam_mclk_groups[] = {
	"gpio34", "gpio35", "gpio36", "gpio44",
};
static const char * const atest_tsens_groups[] = {
	"gpio34",
};
static const char * const cci_i2c_groups[] = {
	"gpio37", "gpio38", "gpio39", "gpio40",
};
static const char * const cci_timer2_groups[] = {
	"gpio42",
};
static const char * const cci_timer1_groups[] = {
	"gpio43",
};
static const char * const gcc_gp2_groups[] = {
	"gpio43", "gpio44",
};
static const char * const cci_async_groups[] = {
	"gpio44", "gpio47", "gpio48",
};
static const char * const cci_timer4_groups[] = {
	"gpio44",
};
static const char * const cci_timer0_groups[] = {
	"gpio45",
};
static const char * const gcc_gp1_groups[] = {
	"gpio45", "gpio46",
};
static const char * const cci_timer3_groups[] = {
	"gpio46",
};
static const char * const wlan1_adc1_groups[] = {
	"gpio47",
};
static const char * const wlan1_adc0_groups[] = {
	"gpio48",
};
static const char * const qlink_request_groups[] = {
	"gpio50",
};
static const char * const qlink_enable_groups[] = {
	"gpio51",
};
static const char * const pa_indicator_groups[] = {
	"gpio52",
};
static const char * const nav_pps_groups[] = {
	"gpio52", "gpio55", "gpio56", "gpio58",
	"gpio59",
};
static const char * const gps_tx_groups[] = {
	"gpio52", "gpio53", "gpio55", "gpio56", "gpio58", "gpio59",
};
static const char * const gp_pdm0_groups[] = {
	"gpio53", "gpio94",
};
static const char * const atest_usb13_groups[] = {
	"gpio53",
};
static const char * const ddr_pxi1_groups[] = {
	"gpio53", "gpio54",
};
static const char * const atest_usb12_groups[] = {
	"gpio54",
};
static const char * const cri_trng0_groups[] = {
	"gpio59",
};
static const char * const cri_trng_groups[] = {
	"gpio60",
};
static const char * const cri_trng1_groups[] = {
	"gpio61",
};
static const char * const gp_pdm2_groups[] = {
	"gpio62", "gpio78",
};
static const char * const sp_cmu_groups[] = {
	"gpio63",
};
static const char * const atest_usb2_groups[] = {
	"gpio66",
};
static const char * const atest_usb23_groups[] = {
	"gpio67",
};
static const char * const uim2_data_groups[] = {
	"gpio72",
};
static const char * const uim2_clk_groups[] = {
	"gpio73",
};
static const char * const uim2_reset_groups[] = {
	"gpio74",
};
static const char * const atest_usb22_groups[] = {
	"gpio74",
};
static const char * const uim2_present_groups[] = {
	"gpio75",
};
static const char * const atest_usb21_groups[] = {
	"gpio75",
};
static const char * const uim1_data_groups[] = {
	"gpio76",
};
static const char * const atest_usb20_groups[] = {
	"gpio76",
};
static const char * const uim1_clk_groups[] = {
	"gpio77",
};
static const char * const uim1_reset_groups[] = {
	"gpio78",
};
static const char * const uim1_present_groups[] = {
	"gpio79",
};
static const char * const mdp_vsync_groups[] = {
	"gpio80", "gpio81", "gpio82", "gpio89", "gpio96", "gpio97",
};
static const char * const copy_gp_groups[] = {
	"gpio85",
};
static const char * const tsense_pwm_groups[] = {
	"gpio87",
};
static const char * const mpm_pwr_groups[] = {
	"gpio88",
};
static const char * const tgu_ch3_groups[] = {
	"gpio88",
};
static const char * const mdp_vsync0_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync1_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync2_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync3_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync4_groups[] = {
	"gpio89",
};
static const char * const mdp_vsync5_groups[] = {
	"gpio89",
};
static const char * const tgu_ch0_groups[] = {
	"gpio89",
};
static const char * const tgu_ch1_groups[] = {
	"gpio90",
};
static const char * const atest_char1_groups[] = {
	"gpio90",
};
static const char * const vfr_1_groups[] = {
	"gpio91",
};
static const char * const tgu_ch2_groups[] = {
	"gpio91",
};
static const char * const atest_char0_groups[] = {
	"gpio92",
};
static const char * const atest_char2_groups[] = {
	"gpio93",
};
static const char * const atest_char3_groups[] = {
	"gpio94",
};
static const char * const ldo_en_groups[] = {
	"gpio96",
};
static const char * const ldo_update_groups[] = {
	"gpio97",
};
static const char * const prng_rosc_groups[] = {
	"gpio98", "gpio100",
};
static const char * const dp_hot_groups[] = {
	"gpio100",
};
static const char * const debug_hot_groups[] = {
	"gpio101",
};
static const char * const copy_phase_groups[] = {
	"gpio101",
};
static const char * const usb_phy_groups[] = {
	"gpio102",
};
static const char * const atest_char_groups[] = {
	"gpio102",
};
static const char * const unused1_groups[] = {
	"gpio104",
};
static const char * const qua_mi2s_groups[] = {
	"gpio104", "gpio106", "gpio107", "gpio108", "gpio110", "gpio111",
};
static const char * const mss_lte_groups[] = {
	"gpio105", "gpio109",
};
static const char * const swr_tx_groups[] = {
	"gpio106", "gpio107", "gpio108", "gpio109",
};
static const char * const aud_sb_groups[] = {
	"gpio106", "gpio107", "gpio108", "gpio109",
};
static const char * const unused2_groups[] = {
	"gpio109",
};
static const char * const swr_rx_groups[] = {
	"gpio110", "gpio111", "gpio112",
};
static const char * const edp_hot_groups[] = {
	"gpio111",
};
static const char * const audio_ref_groups[] = {
	"gpio112",
};
static const char * const pri_mi2s_groups[] = {
	"gpio113", "gpio115", "gpio116",
};
static const char * const pri_mi2s_ws_groups[] = {
	"gpio114",
};
static const char * const adsp_ext_groups[] = {
	"gpio116",
};
static const char * const edp_lcd_groups[] = {
	"gpio117",
};
static const char * const mclk2_groups[] = {
	"gpio118",
};
static const char * const m_voc_groups[] = {
	"gpio118",
};
static const char * const mclk1_groups[] = {
	"gpio119",
};
static const char * const qca_sb_groups[] = {
	"gpio121", "gpio122",
};
static const char * const qui_mi2s_groups[] = {
	"gpio121", "gpio122", "gpio123", "gpio124",
};
static const char * const dmic0_clk_groups[] = {
	"gpio125",
};
static const char * const sec_mi2s_groups[] = {
	"gpio125", "gpio126", "gpio127", "gpio128",
};
static const char * const dmic0_data_groups[] = {
	"gpio126",
};
static const char * const dmic1_clk_groups[] = {
	"gpio127",
};
static const char * const dmic1_data_groups[] = {
	"gpio128",
};

static const struct pinfunction sm6125_functions[] = {
	MSM_PIN_FUNCTION(qup00),
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(qdss),
	MSM_PIN_FUNCTION(qup01),
	MSM_PIN_FUNCTION(qup02),
	MSM_PIN_FUNCTION(ddr_pxi0),
	MSM_PIN_FUNCTION(ddr_bist),
	MSM_PIN_FUNCTION(atest_tsens2),
	MSM_PIN_FUNCTION(vsense_trigger),
	MSM_PIN_FUNCTION(atest_usb1),
	MSM_PIN_FUNCTION(gp_pdm1),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(dbg_out),
	MSM_PIN_FUNCTION(qup14),
	MSM_PIN_FUNCTION(atest_usb11),
	MSM_PIN_FUNCTION(ddr_pxi2),
	MSM_PIN_FUNCTION(atest_usb10),
	MSM_PIN_FUNCTION(jitter_bist),
	MSM_PIN_FUNCTION(ddr_pxi3),
	MSM_PIN_FUNCTION(pll_bypassnl),
	MSM_PIN_FUNCTION(pll_bist),
	MSM_PIN_FUNCTION(qup03),
	MSM_PIN_FUNCTION(pll_reset),
	MSM_PIN_FUNCTION(agera_pll),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qup04),
	MSM_PIN_FUNCTION(wlan2_adc1),
	MSM_PIN_FUNCTION(wlan2_adc0),
	MSM_PIN_FUNCTION(wsa_clk),
	MSM_PIN_FUNCTION(qup13),
	MSM_PIN_FUNCTION(ter_mi2s),
	MSM_PIN_FUNCTION(wsa_data),
	MSM_PIN_FUNCTION(qup10),
	MSM_PIN_FUNCTION(gcc_gp3),
	MSM_PIN_FUNCTION(qup12),
	MSM_PIN_FUNCTION(sd_write),
	MSM_PIN_FUNCTION(qup11),
	MSM_PIN_FUNCTION(cam_mclk),
	MSM_PIN_FUNCTION(atest_tsens),
	MSM_PIN_FUNCTION(cci_i2c),
	MSM_PIN_FUNCTION(cci_timer2),
	MSM_PIN_FUNCTION(cci_timer1),
	MSM_PIN_FUNCTION(gcc_gp2),
	MSM_PIN_FUNCTION(cci_async),
	MSM_PIN_FUNCTION(cci_timer4),
	MSM_PIN_FUNCTION(cci_timer0),
	MSM_PIN_FUNCTION(gcc_gp1),
	MSM_PIN_FUNCTION(cci_timer3),
	MSM_PIN_FUNCTION(wlan1_adc1),
	MSM_PIN_FUNCTION(wlan1_adc0),
	MSM_PIN_FUNCTION(qlink_request),
	MSM_PIN_FUNCTION(qlink_enable),
	MSM_PIN_FUNCTION(pa_indicator),
	MSM_PIN_FUNCTION(nav_pps),
	MSM_PIN_FUNCTION(gps_tx),
	MSM_PIN_FUNCTION(gp_pdm0),
	MSM_PIN_FUNCTION(atest_usb13),
	MSM_PIN_FUNCTION(ddr_pxi1),
	MSM_PIN_FUNCTION(atest_usb12),
	MSM_PIN_FUNCTION(cri_trng0),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(cri_trng1),
	MSM_PIN_FUNCTION(gp_pdm2),
	MSM_PIN_FUNCTION(sp_cmu),
	MSM_PIN_FUNCTION(atest_usb2),
	MSM_PIN_FUNCTION(atest_usb23),
	MSM_PIN_FUNCTION(uim2_data),
	MSM_PIN_FUNCTION(uim2_clk),
	MSM_PIN_FUNCTION(uim2_reset),
	MSM_PIN_FUNCTION(atest_usb22),
	MSM_PIN_FUNCTION(uim2_present),
	MSM_PIN_FUNCTION(atest_usb21),
	MSM_PIN_FUNCTION(uim1_data),
	MSM_PIN_FUNCTION(atest_usb20),
	MSM_PIN_FUNCTION(uim1_clk),
	MSM_PIN_FUNCTION(uim1_reset),
	MSM_PIN_FUNCTION(uim1_present),
	MSM_PIN_FUNCTION(mdp_vsync),
	MSM_PIN_FUNCTION(copy_gp),
	MSM_PIN_FUNCTION(tsense_pwm),
	MSM_PIN_FUNCTION(mpm_pwr),
	MSM_PIN_FUNCTION(tgu_ch3),
	MSM_PIN_FUNCTION(mdp_vsync0),
	MSM_PIN_FUNCTION(mdp_vsync1),
	MSM_PIN_FUNCTION(mdp_vsync2),
	MSM_PIN_FUNCTION(mdp_vsync3),
	MSM_PIN_FUNCTION(mdp_vsync4),
	MSM_PIN_FUNCTION(mdp_vsync5),
	MSM_PIN_FUNCTION(tgu_ch0),
	MSM_PIN_FUNCTION(tgu_ch1),
	MSM_PIN_FUNCTION(atest_char1),
	MSM_PIN_FUNCTION(vfr_1),
	MSM_PIN_FUNCTION(tgu_ch2),
	MSM_PIN_FUNCTION(atest_char0),
	MSM_PIN_FUNCTION(atest_char2),
	MSM_PIN_FUNCTION(atest_char3),
	MSM_PIN_FUNCTION(ldo_en),
	MSM_PIN_FUNCTION(ldo_update),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(dp_hot),
	MSM_PIN_FUNCTION(debug_hot),
	MSM_PIN_FUNCTION(copy_phase),
	MSM_PIN_FUNCTION(usb_phy),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(unused1),
	MSM_PIN_FUNCTION(qua_mi2s),
	MSM_PIN_FUNCTION(mss_lte),
	MSM_PIN_FUNCTION(swr_tx),
	MSM_PIN_FUNCTION(aud_sb),
	MSM_PIN_FUNCTION(unused2),
	MSM_PIN_FUNCTION(swr_rx),
	MSM_PIN_FUNCTION(edp_hot),
	MSM_PIN_FUNCTION(audio_ref),
	MSM_PIN_FUNCTION(pri_mi2s),
	MSM_PIN_FUNCTION(pri_mi2s_ws),
	MSM_PIN_FUNCTION(adsp_ext),
	MSM_PIN_FUNCTION(edp_lcd),
	MSM_PIN_FUNCTION(mclk2),
	MSM_PIN_FUNCTION(m_voc),
	MSM_PIN_FUNCTION(mclk1),
	MSM_PIN_FUNCTION(qca_sb),
	MSM_PIN_FUNCTION(qui_mi2s),
	MSM_PIN_FUNCTION(dmic0_clk),
	MSM_PIN_FUNCTION(sec_mi2s),
	MSM_PIN_FUNCTION(dmic0_data),
	MSM_PIN_FUNCTION(dmic1_clk),
	MSM_PIN_FUNCTION(dmic1_data),
};

 /*
 * Every pin is maintained as a single group, and missing or non-existing pin
 * would be maintained as dummy group to synchronize pin group index with
 * pin descriptor registered with pinctrl core.
 * Clients would not be able to request these dummy pin groups.
 */
static const struct msm_pingroup sm6125_groups[] = {
	[0] = PINGROUP(0, WEST, qup00, _, qdss, _, _, _, _, _, _),
	[1] = PINGROUP(1, WEST, qup00, _, qdss, _, _, _, _, _, _),
	[2] = PINGROUP(2, WEST, qup00, _, qdss, _, _, _, _, _, _),
	[3] = PINGROUP(3, WEST, qup00, _, qdss, _, _, _, _, _, _),
	[4] = PINGROUP(4, WEST, qup01, _, _, _, _, _, _, _, _),
	[5] = PINGROUP(5, WEST, qup01, _, _, _, _, _, _, _, _),
	[6] = PINGROUP(6, WEST, qup02, ddr_pxi0, _, _, _, _, _, _, _),
	[7] = PINGROUP(7, WEST, qup02, ddr_bist, atest_tsens2, vsense_trigger, atest_usb1, ddr_pxi0, _, _, _),
	[8] = PINGROUP(8, WEST, qup02, gp_pdm1, ddr_bist, _, phase_flag, _, _, _, _),
	[9] = PINGROUP(9, WEST, qup02, ddr_bist, dbg_out, phase_flag, _, _, _, _, _),
	[10] = PINGROUP(10, EAST, qup14, ddr_bist, atest_usb11, ddr_pxi2, _, _, _, _, _),
	[11] = PINGROUP(11, EAST, qup14, atest_usb10, ddr_pxi2, _, _, _, _, _, _),
	[12] = PINGROUP(12, EAST, qup14, jitter_bist, ddr_pxi3, _, _, _, _, _, _),
	[13] = PINGROUP(13, EAST, qup14, pll_bypassnl, pll_bist, _, ddr_pxi3, _, _, _, _),
	[14] = PINGROUP(14, WEST, qup03, qup03, pll_reset, agera_pll, _, qdss_cti, _, _, _),
	[15] = PINGROUP(15, WEST, qup03, qup03, qdss_cti, _, _, _, _, _, _),
	[16] = PINGROUP(16, WEST, qup04, qup04, _, wlan2_adc1, _, _, _, _, _),
	[17] = PINGROUP(17, WEST, qup04, qup04, _, wlan2_adc0, _, _, _, _, _),
	[18] = PINGROUP(18, EAST, wsa_clk, qup13, ter_mi2s, _, _, _, _, _, _),
	[19] = PINGROUP(19, EAST, wsa_data, qup13, ter_mi2s, _, _, _, _, _, _),
	[20] = PINGROUP(20, EAST, qup13, ter_mi2s, qdss, _, _, _, _, _, _),
	[21] = PINGROUP(21, EAST, qup13, ter_mi2s, _, qdss, _, _, _, _, _),
	[22] = PINGROUP(22, WEST, qup10, gcc_gp3, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, WEST, qup10, _, phase_flag, _, _, _, _, _, _),
	[24] = PINGROUP(24, WEST, qup10, _, phase_flag, _, _, _, _, _, _),
	[25] = PINGROUP(25, WEST, qup10, _, phase_flag, _, _, _, _, _, _),
	[26] = PINGROUP(26, WEST, qup10, _, phase_flag, _, _, _, _, _, _),
	[27] = PINGROUP(27, WEST, qup10, _, _, _, _, _, _, _, _),
	[28] = PINGROUP(28, WEST, qup12, _, phase_flag, _, _, _, _, _, _),
	[29] = PINGROUP(29, WEST, qup12, sd_write, _, phase_flag, _, _, _, _, _),
	[30] = PINGROUP(30, WEST, qup11, _, phase_flag, _, _, _, _, _, _),
	[31] = PINGROUP(31, WEST, qup11, jitter_bist, _, _, _, _, _, _, _),
	[32] = PINGROUP(32, WEST, qup11, pll_bist, _, _, _, _, _, _, _),
	[33] = PINGROUP(33, WEST, qup11, agera_pll, _, _, _, _, _, _, _),
	[34] = PINGROUP(34, SOUTH, cam_mclk, _, qdss, atest_tsens, _, _, _, _, _),
	[35] = PINGROUP(35, SOUTH, cam_mclk, _, qdss, _, _, _, _, _, _),
	[36] = PINGROUP(36, SOUTH, cam_mclk, _, qdss, _, _, _, _, _, _),
	[37] = PINGROUP(37, SOUTH, cci_i2c, _, _, _, _, _, _, _, _),
	[38] = PINGROUP(38, EAST, cci_i2c, _, _, _, _, _, _, _, _),
	[39] = PINGROUP(39, EAST, cci_i2c, _, _, _, _, _, _, _, _),
	[40] = PINGROUP(40, EAST, cci_i2c, _, _, _, _, _, _, _, _),
	[41] = PINGROUP(41, EAST, _, qdss, _, _, _, _, _, _, _),
	[42] = PINGROUP(42, EAST, cci_timer2, _, qdss, _, _, _, _, _, _),
	[43] = PINGROUP(43, EAST, cci_timer1, _, gcc_gp2, _, qdss, _, _, _, _),
	[44] = PINGROUP(44, SOUTH, cci_async, cci_timer4, _, gcc_gp2, _, qdss, cam_mclk, _, _),
	[45] = PINGROUP(45, SOUTH, cci_timer0, _, gcc_gp1, qdss, _, _, _, _, _),
	[46] = PINGROUP(46, SOUTH, cci_timer3, _, gcc_gp1, _, qdss, _, _, _, _),
	[47] = PINGROUP(47, SOUTH, cci_async, _, qdss, wlan1_adc1, _, _, _, _, _),
	[48] = PINGROUP(48, SOUTH, cci_async, _, qdss, wlan1_adc0, _, _, _, _, _),
	[49] = PINGROUP(49, SOUTH, qdss, _, _, _, _, _, _, _, _),
	[50] = PINGROUP(50, SOUTH, qlink_request, _, _, _, _, _, _, _, _),
	[51] = PINGROUP(51, SOUTH, qlink_enable, _, _, _, _, _, _, _, _),
	[52] = PINGROUP(52, SOUTH, pa_indicator, nav_pps, nav_pps, gps_tx, _, _, _, _, _),
	[53] = PINGROUP(53, SOUTH, _, gps_tx, gp_pdm0, _, phase_flag, atest_usb13, ddr_pxi1, _, _),
	[54] = PINGROUP(54, SOUTH, _, _, phase_flag, atest_usb12, ddr_pxi1, _, _, _, _),
	[55] = PINGROUP(55, SOUTH, _, nav_pps, nav_pps, gps_tx, _, phase_flag, _, _, _),
	[56] = PINGROUP(56, SOUTH, _, nav_pps, gps_tx, nav_pps, phase_flag, _, _, _, _),
	[57] = PINGROUP(57, SOUTH, _, phase_flag, _, _, _, _, _, _, _),
	[58] = PINGROUP(58, SOUTH, _, nav_pps, nav_pps, gps_tx, gcc_gp3, _, phase_flag, _, _),
	[59] = PINGROUP(59, SOUTH, _, nav_pps, nav_pps, gps_tx, cri_trng0, _, phase_flag, _, _),
	[60] = PINGROUP(60, SOUTH, _, cri_trng, _, phase_flag, _, _, _, _, _),
	[61] = PINGROUP(61, SOUTH, _, cri_trng1, _, phase_flag, _, _, _, _, _),
	[62] = PINGROUP(62, SOUTH, _, _, gp_pdm2, _, phase_flag, _, _, _, _),
	[63] = PINGROUP(63, SOUTH, _, sp_cmu, _, _, _, _, _, _, _),
	[64] = PINGROUP(64, SOUTH, _, _, _, _, _, _, _, _, _),
	[65] = PINGROUP(65, SOUTH, _, gp_pdm1, _, _, _, _, _, _, _),
	[66] = PINGROUP(66, SOUTH, _, _, atest_usb2, _, _, _, _, _, _),
	[67] = PINGROUP(67, SOUTH, _, _, atest_usb23, _, _, _, _, _, _),
	[68] = PINGROUP(68, SOUTH, _, _, _, _, _, _, _, _, _),
	[69] = PINGROUP(69, SOUTH, _, _, _, _, _, _, _, _, _),
	[70] = PINGROUP(70, SOUTH, _, _, _, _, _, _, _, _, _),
	[71] = PINGROUP(71, SOUTH, _, _, _, _, _, _, _, _, _),
	[72] = PINGROUP(72, SOUTH, uim2_data, _, _, _, _, _, _, _, _),
	[73] = PINGROUP(73, SOUTH, uim2_clk, _, _, _, _, _, _, _, _),
	[74] = PINGROUP(74, SOUTH, uim2_reset, _, atest_usb22, _, _, _, _, _, _),
	[75] = PINGROUP(75, SOUTH, uim2_present, _, atest_usb21, _, _, _, _, _, _),
	[76] = PINGROUP(76, SOUTH, uim1_data, _, atest_usb20, _, _, _, _, _, _),
	[77] = PINGROUP(77, SOUTH, uim1_clk, _, _, _, _, _, _, _, _),
	[78] = PINGROUP(78, SOUTH, uim1_reset, gp_pdm2, _, _, _, _, _, _, _),
	[79] = PINGROUP(79, SOUTH, uim1_present, _, _, _, _, _, _, _, _),
	[80] = PINGROUP(80, SOUTH, mdp_vsync, _, phase_flag, qdss, _, _, _, _, _),
	[81] = PINGROUP(81, SOUTH, mdp_vsync, _, phase_flag, qdss, _, _, _, _, _),
	[82] = PINGROUP(82, SOUTH, mdp_vsync, _, phase_flag, qdss, _, _, _, _, _),
	[83] = PINGROUP(83, SOUTH, _, phase_flag, qdss, _, _, _, _, _, _),
	[84] = PINGROUP(84, SOUTH, _, phase_flag, qdss, _, _, _, _, _, _),
	[85] = PINGROUP(85, SOUTH, copy_gp, _, qdss, _, _, _, _, _, _),
	[86] = PINGROUP(86, SOUTH, _, qdss, _, _, _, _, _, _, _),
	[87] = PINGROUP(87, WEST, tsense_pwm, _, _, _, _, _, _, _, _),
	[88] = PINGROUP(88, WEST, mpm_pwr, tgu_ch3, _, phase_flag, _, _, _, _, _),
	[89] = PINGROUP(89, WEST, mdp_vsync, mdp_vsync0, mdp_vsync1, mdp_vsync2, mdp_vsync3, mdp_vsync4, mdp_vsync5, tgu_ch0, _),
	[90] = PINGROUP(90, WEST, tgu_ch1, atest_char1, _, _, _, _, _, _, _),
	[91] = PINGROUP(91, WEST, vfr_1, tgu_ch2, _, phase_flag, qdss, _, _, _, _),
	[92] = PINGROUP(92, WEST, qdss, atest_char0, _, _, _, _, _, _, _),
	[93] = PINGROUP(93, WEST, _, phase_flag, atest_char2, _, _, _, _, _, _),
	[94] = PINGROUP(94, SOUTH, gp_pdm0, _, qdss, atest_char3, _, _, _, _, _),
	[95] = PINGROUP(95, SOUTH, qdss_cti, _, _, _, _, _, _, _, _),
	[96] = PINGROUP(96, SOUTH, mdp_vsync, ldo_en, qdss, _, _, _, _, _, _),
	[97] = PINGROUP(97, SOUTH, mdp_vsync, ldo_update, _, _, _, _, _, _, _),
	[98] = PINGROUP(98, SOUTH, _, phase_flag, prng_rosc, _, _, _, _, _, _),
	[99] = PINGROUP(99, SOUTH, _, _, _, _, _, _, _, _, _),
	[100] = PINGROUP(100, SOUTH, dp_hot, prng_rosc, qdss, _, _, _, _, _, _),
	[101] = PINGROUP(101, SOUTH, debug_hot, copy_phase, qdss_cti, _, _, _, _, _, _),
	[102] = PINGROUP(102, SOUTH, usb_phy, _, qdss, atest_char, _, _, _, _, _),
	[103] = PINGROUP(103, SOUTH, _, _, _, _, _, _, _, _, _),
	[104] = PINGROUP(104, EAST, unused1, _, qua_mi2s, _, _, _, _, _, _),
	[105] = PINGROUP(105, EAST, mss_lte, _, _, _, _, _, _, _, _),
	[106] = PINGROUP(106, EAST, swr_tx, aud_sb, qua_mi2s, _, qdss_cti, _, _, _, _),
	[107] = PINGROUP(107, EAST, swr_tx, aud_sb, qua_mi2s, _, qdss_cti, _, _, _, _),
	[108] = PINGROUP(108, EAST, swr_tx, aud_sb, qua_mi2s, _, _, _, _, _, _),
	[109] = PINGROUP(109, EAST, swr_tx, aud_sb, unused2, _, mss_lte, _, _, _, _),
	[110] = PINGROUP(110, EAST, swr_rx, qua_mi2s, _, qdss_cti, _, _, _, _, _),
	[111] = PINGROUP(111, EAST, swr_rx, qua_mi2s, edp_hot, _, qdss_cti, _, _, _, _),
	[112] = PINGROUP(112, EAST, swr_rx, audio_ref, _, _, _, _, _, _, _),
	[113] = PINGROUP(113, EAST, pri_mi2s, _, _, _, _, _, _, _, _),
	[114] = PINGROUP(114, EAST, pri_mi2s_ws, qdss, _, _, _, _, _, _, _),
	[115] = PINGROUP(115, EAST, pri_mi2s, qdss, _, _, _, _, _, _, _),
	[116] = PINGROUP(116, EAST, pri_mi2s, adsp_ext, qdss, _, _, _, _, _, _),
	[117] = PINGROUP(117, SOUTH, edp_lcd, qdss, _, _, _, _, _, _, _),
	[118] = PINGROUP(118, SOUTH, mclk2, m_voc, qdss, _, _, _, _, _, _),
	[119] = PINGROUP(119, SOUTH, mclk1, _, _, _, _, _, _, _, _),
	[120] = PINGROUP(120, SOUTH, _, _, _, _, _, _, _, _, _),
	[121] = PINGROUP(121, EAST, qca_sb, qui_mi2s, _, _, _, _, _, _, _),
	[122] = PINGROUP(122, EAST, qca_sb, qui_mi2s, _, _, _, _, _, _, _),
	[123] = PINGROUP(123, EAST, qui_mi2s, _, _, _, _, _, _, _, _),
	[124] = PINGROUP(124, EAST, qui_mi2s, _, _, _, _, _, _, _, _),
	[125] = PINGROUP(125, EAST, dmic0_clk, sec_mi2s, _, _, _, _, _, _, _),
	[126] = PINGROUP(126, EAST, dmic0_data, sec_mi2s, _, _, _, _, _, _, _),
	[127] = PINGROUP(127, EAST, dmic1_clk, sec_mi2s, _, _, _, _, _, _, _),
	[128] = PINGROUP(128, EAST, dmic1_data, sec_mi2s, _, _, _, _, _, _, _),
	[129] = PINGROUP(129, SOUTH, _, phase_flag, _, _, _, _, _, _, _),
	[130] = PINGROUP(130, SOUTH, phase_flag, _, _, _, _, _, _, _, _),
	[131] = PINGROUP(131, SOUTH, phase_flag, _, _, _, _, _, _, _, _),
	[132] = PINGROUP(132, SOUTH, _, _, _, _, _, _, _, _, _),
	[133] = UFS_RESET(ufs_reset, 0x190000),
	[134] = SDC_QDSD_PINGROUP(sdc1_rclk, WEST, 0x18d000, 15, 0),
	[135] = SDC_QDSD_PINGROUP(sdc1_clk, WEST, 0x18d000, 13, 6),
	[136] = SDC_QDSD_PINGROUP(sdc1_cmd, WEST, 0x18d000, 11, 3),
	[137] = SDC_QDSD_PINGROUP(sdc1_data, WEST, 0x18d000, 9, 0),
	[138] = SDC_QDSD_PINGROUP(sdc2_clk, SOUTH, 0x58b000, 14, 6),
	[139] = SDC_QDSD_PINGROUP(sdc2_cmd, SOUTH, 0x58b000, 11, 3),
	[140] = SDC_QDSD_PINGROUP(sdc2_data, SOUTH, 0x58b000, 9, 0),
};

static const struct msm_pinctrl_soc_data sm6125_tlmm = {
	.pins = sm6125_pins,
	.npins = ARRAY_SIZE(sm6125_pins),
	.functions = sm6125_functions,
	.nfunctions = ARRAY_SIZE(sm6125_functions),
	.groups = sm6125_groups,
	.ngroups = ARRAY_SIZE(sm6125_groups),
	.ngpios = 134,
	.tiles = sm6125_tiles,
	.ntiles = ARRAY_SIZE(sm6125_tiles),
};

static int sm6125_tlmm_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &sm6125_tlmm);
}

static const struct of_device_id sm6125_tlmm_of_match[] = {
	{ .compatible = "qcom,sm6125-tlmm", },
	{ },
};

static struct platform_driver sm6125_tlmm_driver = {
	.driver = {
		.name = "sm6125-tlmm",
		.of_match_table = sm6125_tlmm_of_match,
	},
	.probe = sm6125_tlmm_probe,
	.remove = msm_pinctrl_remove,
};

static int __init sm6125_tlmm_init(void)
{
	return platform_driver_register(&sm6125_tlmm_driver);
}
arch_initcall(sm6125_tlmm_init);

static void __exit sm6125_tlmm_exit(void)
{
	platform_driver_unregister(&sm6125_tlmm_driver);
}
module_exit(sm6125_tlmm_exit);

MODULE_DESCRIPTION("QTI sm6125 TLMM driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, sm6125_tlmm_of_match);
