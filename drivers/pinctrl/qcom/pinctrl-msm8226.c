// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

static const struct pinctrl_pin_desc msm8226_pins[] = {
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

	PINCTRL_PIN(117, "SDC1_CLK"),
	PINCTRL_PIN(118, "SDC1_CMD"),
	PINCTRL_PIN(119, "SDC1_DATA"),
	PINCTRL_PIN(120, "SDC2_CLK"),
	PINCTRL_PIN(121, "SDC2_CMD"),
	PINCTRL_PIN(122, "SDC2_DATA"),
};

#define DECLARE_MSM_GPIO_PINS(pin) static const unsigned int gpio##pin##_pins[] = { pin }
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

static const unsigned int sdc1_clk_pins[] = { 117 };
static const unsigned int sdc1_cmd_pins[] = { 118 };
static const unsigned int sdc1_data_pins[] = { 119 };
static const unsigned int sdc2_clk_pins[] = { 120 };
static const unsigned int sdc2_cmd_pins[] = { 121 };
static const unsigned int sdc2_data_pins[] = { 122 };

#define FUNCTION(fname)					\
	[MSM_MUX_##fname] = {				\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7)	\
	{						\
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = ARRAY_SIZE(gpio##id##_pins),	\
		.funcs = (int[]){			\
			MSM_MUX_gpio,			\
			MSM_MUX_##f1,			\
			MSM_MUX_##f2,			\
			MSM_MUX_##f3,			\
			MSM_MUX_##f4,			\
			MSM_MUX_##f5,			\
			MSM_MUX_##f6,			\
			MSM_MUX_##f7			\
		},					\
		.nfuncs = 8,				\
		.ctl_reg = 0x1000 + 0x10 * id,		\
		.io_reg = 0x1004 + 0x10 * id,		\
		.intr_cfg_reg = 0x1008 + 0x10 * id,	\
		.intr_status_reg = 0x100c + 0x10 * id,	\
		.intr_target_reg = 0x1008 + 0x10 * id,	\
		.mux_bit = 2,				\
		.pull_bit = 0,				\
		.drv_bit = 6,				\
		.oe_bit = 9,				\
		.in_bit = 0,				\
		.out_bit = 1,				\
		.intr_enable_bit = 0,			\
		.intr_status_bit = 0,			\
		.intr_target_bit = 5,			\
		.intr_target_kpss_val = 4,		\
		.intr_raw_status_bit = 4,		\
		.intr_polarity_bit = 1,			\
		.intr_detection_bit = 2,		\
		.intr_detection_width = 2,		\
	}

#define SDC_PINGROUP(pg_name, ctl, pull, drv)		\
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
		.intr_target_kpss_val = -1,		\
		.intr_raw_status_bit = -1,		\
		.intr_polarity_bit = -1,		\
		.intr_detection_bit = -1,		\
		.intr_detection_width = -1,		\
	}

/*
 * TODO: Add the rest of the possible functions and fill out
 * the pingroup table below.
 */
enum msm8226_functions {
	MSM_MUX_audio_pcm,
	MSM_MUX_blsp_i2c1,
	MSM_MUX_blsp_i2c2,
	MSM_MUX_blsp_i2c3,
	MSM_MUX_blsp_i2c4,
	MSM_MUX_blsp_i2c5,
	MSM_MUX_blsp_spi1,
	MSM_MUX_blsp_spi2,
	MSM_MUX_blsp_spi3,
	MSM_MUX_blsp_spi4,
	MSM_MUX_blsp_spi5,
	MSM_MUX_blsp_uart1,
	MSM_MUX_blsp_uart2,
	MSM_MUX_blsp_uart3,
	MSM_MUX_blsp_uart4,
	MSM_MUX_blsp_uart5,
	MSM_MUX_blsp_uim1,
	MSM_MUX_blsp_uim2,
	MSM_MUX_blsp_uim3,
	MSM_MUX_blsp_uim4,
	MSM_MUX_blsp_uim5,
	MSM_MUX_cam_mclk0,
	MSM_MUX_cam_mclk1,
	MSM_MUX_cci_i2c0,
	MSM_MUX_gp0_clk,
	MSM_MUX_gp1_clk,
	MSM_MUX_gpio,
	MSM_MUX_sdc3,
	MSM_MUX_wlan,
	MSM_MUX_NA,
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
};

static const char * const audio_pcm_groups[] = {
	"gpio63", "gpio64", "gpio65", "gpio66"
};

static const char * const blsp_uart1_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3"
};

static const char * const blsp_uim1_groups[] = { "gpio0", "gpio1" };
static const char * const blsp_i2c1_groups[] = { "gpio2", "gpio3" };
static const char * const blsp_spi1_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3"
};

static const char * const blsp_uart2_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7"
};

static const char * const blsp_uim2_groups[] = { "gpio4", "gpio5" };
static const char * const blsp_i2c2_groups[] = { "gpio6", "gpio7" };
static const char * const blsp_spi2_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7"
};

static const char * const blsp_uart3_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};

static const char * const blsp_uim3_groups[] = { "gpio8", "gpio9" };
static const char * const blsp_i2c3_groups[] = { "gpio10", "gpio11" };
static const char * const blsp_spi3_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};

static const char * const blsp_uart4_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15"
};

static const char * const blsp_uim4_groups[] = { "gpio12", "gpio13" };
static const char * const blsp_i2c4_groups[] = { "gpio14", "gpio15" };
static const char * const blsp_spi4_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15"
};

static const char * const blsp_uart5_groups[] = {
	"gpio16", "gpio17", "gpio18", "gpio19"
};

static const char * const blsp_uim5_groups[] = { "gpio16", "gpio17" };
static const char * const blsp_i2c5_groups[] = { "gpio18", "gpio19" };
static const char * const blsp_spi5_groups[] = {
	"gpio16", "gpio17", "gpio18", "gpio19"
};

static const char * const cci_i2c0_groups[] = { "gpio29", "gpio30" };

static const char * const cam_mclk0_groups[] = { "gpio26" };
static const char * const cam_mclk1_groups[] = { "gpio27" };

static const char * const gp0_clk_groups[] = { "gpio33" };
static const char * const gp1_clk_groups[] = { "gpio34" };

static const char * const sdc3_groups[] = {
	"gpio39", "gpio40", "gpio41", "gpio42", "gpio43", "gpio44"
};

static const char * const wlan_groups[] = {
	"gpio40", "gpio41", "gpio42", "gpio43", "gpio44"
};

static const struct msm_function msm8226_functions[] = {
	FUNCTION(audio_pcm),
	FUNCTION(blsp_i2c1),
	FUNCTION(blsp_i2c2),
	FUNCTION(blsp_i2c3),
	FUNCTION(blsp_i2c4),
	FUNCTION(blsp_i2c5),
	FUNCTION(blsp_spi1),
	FUNCTION(blsp_spi2),
	FUNCTION(blsp_spi3),
	FUNCTION(blsp_spi4),
	FUNCTION(blsp_spi5),
	FUNCTION(blsp_uart1),
	FUNCTION(blsp_uart2),
	FUNCTION(blsp_uart3),
	FUNCTION(blsp_uart4),
	FUNCTION(blsp_uart5),
	FUNCTION(blsp_uim1),
	FUNCTION(blsp_uim2),
	FUNCTION(blsp_uim3),
	FUNCTION(blsp_uim4),
	FUNCTION(blsp_uim5),
	FUNCTION(cam_mclk0),
	FUNCTION(cam_mclk1),
	FUNCTION(cci_i2c0),
	FUNCTION(gp0_clk),
	FUNCTION(gp1_clk),
	FUNCTION(gpio),
	FUNCTION(sdc3),
	FUNCTION(wlan),
};

static const struct msm_pingroup msm8226_groups[] = {
	PINGROUP(0,   blsp_spi1, blsp_uart1, blsp_uim1, NA, NA, NA, NA),
	PINGROUP(1,   blsp_spi1, blsp_uart1, blsp_uim1, NA, NA, NA, NA),
	PINGROUP(2,   blsp_spi1, blsp_uart1, blsp_i2c1, NA, NA, NA, NA),
	PINGROUP(3,   blsp_spi1, blsp_uart1, blsp_i2c1, NA, NA, NA, NA),
	PINGROUP(4,   blsp_spi2, blsp_uart2, blsp_uim2, NA, NA, NA, NA),
	PINGROUP(5,   blsp_spi2, blsp_uart2, blsp_uim2, NA, NA, NA, NA),
	PINGROUP(6,   blsp_spi2, blsp_uart2, blsp_i2c2, NA, NA, NA, NA),
	PINGROUP(7,   blsp_spi2, blsp_uart2, blsp_i2c2, NA, NA, NA, NA),
	PINGROUP(8,   blsp_spi3, blsp_uart3, blsp_uim3, NA, NA, NA, NA),
	PINGROUP(9,   blsp_spi3, blsp_uart3, blsp_uim3, NA, NA, NA, NA),
	PINGROUP(10,  blsp_spi3, blsp_uart3, blsp_i2c3, NA, NA, NA, NA),
	PINGROUP(11,  blsp_spi3, blsp_uart3, blsp_i2c3, NA, NA, NA, NA),
	PINGROUP(12,  blsp_spi4, blsp_uart4, blsp_uim4, NA, NA, NA, NA),
	PINGROUP(13,  blsp_spi4, blsp_uart4, blsp_uim4, NA, NA, NA, NA),
	PINGROUP(14,  blsp_spi4, blsp_uart4, blsp_i2c4, NA, NA, NA, NA),
	PINGROUP(15,  blsp_spi4, blsp_uart4, blsp_i2c4, NA, NA, NA, NA),
	PINGROUP(16,  blsp_spi5, blsp_uart5, blsp_uim5, NA, NA, NA, NA),
	PINGROUP(17,  blsp_spi5, blsp_uart5, blsp_uim5, NA, NA, NA, NA),
	PINGROUP(18,  blsp_spi5, blsp_uart5, blsp_i2c5, NA, NA, NA, NA),
	PINGROUP(19,  blsp_spi5, blsp_uart5, blsp_i2c5, NA, NA, NA, NA),
	PINGROUP(20,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(21,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(22,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(23,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(24,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(25,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(26,  cam_mclk0, NA, NA, NA, NA, NA, NA),
	PINGROUP(27,  cam_mclk1, NA, NA, NA, NA, NA, NA),
	PINGROUP(28,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(29,  cci_i2c0, NA, NA, NA, NA, NA, NA),
	PINGROUP(30,  cci_i2c0, NA, NA, NA, NA, NA, NA),
	PINGROUP(31,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(32,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(33,  NA, NA, gp0_clk, NA, NA, NA, NA),
	PINGROUP(34,  NA, NA, gp1_clk, NA, NA, NA, NA),
	PINGROUP(35,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(36,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(37,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(38,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(39,  NA, sdc3, NA, NA, NA, NA, NA),
	PINGROUP(40,  wlan, sdc3, NA, NA, NA, NA, NA),
	PINGROUP(41,  wlan, sdc3, NA, NA, NA, NA, NA),
	PINGROUP(42,  wlan, sdc3, NA, NA, NA, NA, NA),
	PINGROUP(43,  wlan, sdc3, NA, NA, NA, NA, NA),
	PINGROUP(44,  wlan, sdc3, NA, NA, NA, NA, NA),
	PINGROUP(45,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(46,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(47,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(48,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(49,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(50,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(51,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(52,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(53,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(54,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(55,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(56,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(57,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(58,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(59,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(60,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(61,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(62,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(63,  audio_pcm, NA, NA, NA, NA, NA, NA),
	PINGROUP(64,  audio_pcm, NA, NA, NA, NA, NA, NA),
	PINGROUP(65,  audio_pcm, NA, NA, NA, NA, NA, NA),
	PINGROUP(66,  audio_pcm, NA, NA, NA, NA, NA, NA),
	PINGROUP(67,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(68,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(69,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(70,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(71,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(72,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(73,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(74,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(75,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(76,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(77,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(78,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(79,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(80,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(81,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(82,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(83,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(84,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(85,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(86,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(87,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(88,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(89,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(90,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(91,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(92,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(93,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(94,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(95,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(96,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(97,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(98,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(99,  NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(100, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(101, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(102, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(103, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(104, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(105, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(106, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(107, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(108, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(109, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(110, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(111, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(112, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(113, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(114, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(115, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(116, NA, NA, NA, NA, NA, NA, NA),
	SDC_PINGROUP(sdc1_clk, 0x2044, 13, 6),
	SDC_PINGROUP(sdc1_cmd, 0x2044, 11, 3),
	SDC_PINGROUP(sdc1_data, 0x2044, 9, 0),
	SDC_PINGROUP(sdc2_clk, 0x2048, 14, 6),
	SDC_PINGROUP(sdc2_cmd, 0x2048, 11, 3),
	SDC_PINGROUP(sdc2_data, 0x2048, 9, 0),
};

#define NUM_GPIO_PINGROUPS 117

static const struct msm_pinctrl_soc_data msm8226_pinctrl = {
	.pins = msm8226_pins,
	.npins = ARRAY_SIZE(msm8226_pins),
	.functions = msm8226_functions,
	.nfunctions = ARRAY_SIZE(msm8226_functions),
	.groups = msm8226_groups,
	.ngroups = ARRAY_SIZE(msm8226_groups),
	.ngpios = NUM_GPIO_PINGROUPS,
};

static int msm8226_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &msm8226_pinctrl);
}

static const struct of_device_id msm8226_pinctrl_of_match[] = {
	{ .compatible = "qcom,msm8226-pinctrl", },
	{ },
};

static struct platform_driver msm8226_pinctrl_driver = {
	.driver = {
		.name = "msm8226-pinctrl",
		.of_match_table = msm8226_pinctrl_of_match,
	},
	.probe = msm8226_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init msm8226_pinctrl_init(void)
{
	return platform_driver_register(&msm8226_pinctrl_driver);
}
arch_initcall(msm8226_pinctrl_init);

static void __exit msm8226_pinctrl_exit(void)
{
	platform_driver_unregister(&msm8226_pinctrl_driver);
}
module_exit(msm8226_pinctrl_exit);

MODULE_AUTHOR("Bartosz Dudziak <bartosz.dudziak@snejp.pl>");
MODULE_DESCRIPTION("Qualcomm MSM8226 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, msm8226_pinctrl_of_match);
