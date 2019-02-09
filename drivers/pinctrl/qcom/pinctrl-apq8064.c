/*
 * Copyright (c) 2014, Sony Mobile Communications AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm.h"

static const struct pinctrl_pin_desc apq8064_pins[] = {
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

	PINCTRL_PIN(90, "SDC1_CLK"),
	PINCTRL_PIN(91, "SDC1_CMD"),
	PINCTRL_PIN(92, "SDC1_DATA"),
	PINCTRL_PIN(93, "SDC3_CLK"),
	PINCTRL_PIN(94, "SDC3_CMD"),
	PINCTRL_PIN(95, "SDC3_DATA"),
};

#define DECLARE_APQ_GPIO_PINS(pin) static const unsigned int gpio##pin##_pins[] = { pin }
DECLARE_APQ_GPIO_PINS(0);
DECLARE_APQ_GPIO_PINS(1);
DECLARE_APQ_GPIO_PINS(2);
DECLARE_APQ_GPIO_PINS(3);
DECLARE_APQ_GPIO_PINS(4);
DECLARE_APQ_GPIO_PINS(5);
DECLARE_APQ_GPIO_PINS(6);
DECLARE_APQ_GPIO_PINS(7);
DECLARE_APQ_GPIO_PINS(8);
DECLARE_APQ_GPIO_PINS(9);
DECLARE_APQ_GPIO_PINS(10);
DECLARE_APQ_GPIO_PINS(11);
DECLARE_APQ_GPIO_PINS(12);
DECLARE_APQ_GPIO_PINS(13);
DECLARE_APQ_GPIO_PINS(14);
DECLARE_APQ_GPIO_PINS(15);
DECLARE_APQ_GPIO_PINS(16);
DECLARE_APQ_GPIO_PINS(17);
DECLARE_APQ_GPIO_PINS(18);
DECLARE_APQ_GPIO_PINS(19);
DECLARE_APQ_GPIO_PINS(20);
DECLARE_APQ_GPIO_PINS(21);
DECLARE_APQ_GPIO_PINS(22);
DECLARE_APQ_GPIO_PINS(23);
DECLARE_APQ_GPIO_PINS(24);
DECLARE_APQ_GPIO_PINS(25);
DECLARE_APQ_GPIO_PINS(26);
DECLARE_APQ_GPIO_PINS(27);
DECLARE_APQ_GPIO_PINS(28);
DECLARE_APQ_GPIO_PINS(29);
DECLARE_APQ_GPIO_PINS(30);
DECLARE_APQ_GPIO_PINS(31);
DECLARE_APQ_GPIO_PINS(32);
DECLARE_APQ_GPIO_PINS(33);
DECLARE_APQ_GPIO_PINS(34);
DECLARE_APQ_GPIO_PINS(35);
DECLARE_APQ_GPIO_PINS(36);
DECLARE_APQ_GPIO_PINS(37);
DECLARE_APQ_GPIO_PINS(38);
DECLARE_APQ_GPIO_PINS(39);
DECLARE_APQ_GPIO_PINS(40);
DECLARE_APQ_GPIO_PINS(41);
DECLARE_APQ_GPIO_PINS(42);
DECLARE_APQ_GPIO_PINS(43);
DECLARE_APQ_GPIO_PINS(44);
DECLARE_APQ_GPIO_PINS(45);
DECLARE_APQ_GPIO_PINS(46);
DECLARE_APQ_GPIO_PINS(47);
DECLARE_APQ_GPIO_PINS(48);
DECLARE_APQ_GPIO_PINS(49);
DECLARE_APQ_GPIO_PINS(50);
DECLARE_APQ_GPIO_PINS(51);
DECLARE_APQ_GPIO_PINS(52);
DECLARE_APQ_GPIO_PINS(53);
DECLARE_APQ_GPIO_PINS(54);
DECLARE_APQ_GPIO_PINS(55);
DECLARE_APQ_GPIO_PINS(56);
DECLARE_APQ_GPIO_PINS(57);
DECLARE_APQ_GPIO_PINS(58);
DECLARE_APQ_GPIO_PINS(59);
DECLARE_APQ_GPIO_PINS(60);
DECLARE_APQ_GPIO_PINS(61);
DECLARE_APQ_GPIO_PINS(62);
DECLARE_APQ_GPIO_PINS(63);
DECLARE_APQ_GPIO_PINS(64);
DECLARE_APQ_GPIO_PINS(65);
DECLARE_APQ_GPIO_PINS(66);
DECLARE_APQ_GPIO_PINS(67);
DECLARE_APQ_GPIO_PINS(68);
DECLARE_APQ_GPIO_PINS(69);
DECLARE_APQ_GPIO_PINS(70);
DECLARE_APQ_GPIO_PINS(71);
DECLARE_APQ_GPIO_PINS(72);
DECLARE_APQ_GPIO_PINS(73);
DECLARE_APQ_GPIO_PINS(74);
DECLARE_APQ_GPIO_PINS(75);
DECLARE_APQ_GPIO_PINS(76);
DECLARE_APQ_GPIO_PINS(77);
DECLARE_APQ_GPIO_PINS(78);
DECLARE_APQ_GPIO_PINS(79);
DECLARE_APQ_GPIO_PINS(80);
DECLARE_APQ_GPIO_PINS(81);
DECLARE_APQ_GPIO_PINS(82);
DECLARE_APQ_GPIO_PINS(83);
DECLARE_APQ_GPIO_PINS(84);
DECLARE_APQ_GPIO_PINS(85);
DECLARE_APQ_GPIO_PINS(86);
DECLARE_APQ_GPIO_PINS(87);
DECLARE_APQ_GPIO_PINS(88);
DECLARE_APQ_GPIO_PINS(89);

static const unsigned int sdc1_clk_pins[] = { 90 };
static const unsigned int sdc1_cmd_pins[] = { 91 };
static const unsigned int sdc1_data_pins[] = { 92 };
static const unsigned int sdc3_clk_pins[] = { 93 };
static const unsigned int sdc3_cmd_pins[] = { 94 };
static const unsigned int sdc3_data_pins[] = { 95 };

#define FUNCTION(fname)					\
	[APQ_MUX_##fname] = {				\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) \
	{						\
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = ARRAY_SIZE(gpio##id##_pins),	\
		.funcs = (int[]){			\
			APQ_MUX_gpio,			\
			APQ_MUX_##f1,			\
			APQ_MUX_##f2,			\
			APQ_MUX_##f3,			\
			APQ_MUX_##f4,			\
			APQ_MUX_##f5,			\
			APQ_MUX_##f6,			\
			APQ_MUX_##f7,			\
			APQ_MUX_##f8,			\
			APQ_MUX_##f9,			\
			APQ_MUX_##f10,			\
		},					\
		.nfuncs = 11,				\
		.ctl_reg = 0x1000 + 0x10 * id,		\
		.io_reg = 0x1004 + 0x10 * id,		\
		.intr_cfg_reg = 0x1008 + 0x10 * id,	\
		.intr_status_reg = 0x100c + 0x10 * id,	\
		.intr_target_reg = 0x400 + 0x4 * id,	\
		.mux_bit = 2,				\
		.pull_bit = 0,				\
		.drv_bit = 6,				\
		.oe_bit = 9,				\
		.in_bit = 0,				\
		.out_bit = 1,				\
		.intr_enable_bit = 0,			\
		.intr_status_bit = 0,			\
		.intr_ack_high = 1,			\
		.intr_target_bit = 0,			\
		.intr_target_kpss_val = 4,		\
		.intr_raw_status_bit = 3,		\
		.intr_polarity_bit = 1,			\
		.intr_detection_bit = 2,		\
		.intr_detection_width = 1,		\
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

enum apq8064_functions {
	APQ_MUX_cam_mclk,
	APQ_MUX_codec_mic_i2s,
	APQ_MUX_codec_spkr_i2s,
	APQ_MUX_gp_clk_0a,
	APQ_MUX_gp_clk_0b,
	APQ_MUX_gp_clk_1a,
	APQ_MUX_gp_clk_1b,
	APQ_MUX_gp_clk_2a,
	APQ_MUX_gp_clk_2b,
	APQ_MUX_gpio,
	APQ_MUX_gsbi1,
	APQ_MUX_gsbi2,
	APQ_MUX_gsbi3,
	APQ_MUX_gsbi4,
	APQ_MUX_gsbi4_cam_i2c,
	APQ_MUX_gsbi5,
	APQ_MUX_gsbi5_spi_cs1,
	APQ_MUX_gsbi5_spi_cs2,
	APQ_MUX_gsbi5_spi_cs3,
	APQ_MUX_gsbi6,
	APQ_MUX_gsbi6_spi_cs1,
	APQ_MUX_gsbi6_spi_cs2,
	APQ_MUX_gsbi6_spi_cs3,
	APQ_MUX_gsbi7,
	APQ_MUX_gsbi7_spi_cs1,
	APQ_MUX_gsbi7_spi_cs2,
	APQ_MUX_gsbi7_spi_cs3,
	APQ_MUX_gsbi_cam_i2c,
	APQ_MUX_hdmi,
	APQ_MUX_mi2s,
	APQ_MUX_riva_bt,
	APQ_MUX_riva_fm,
	APQ_MUX_riva_wlan,
	APQ_MUX_sdc2,
	APQ_MUX_sdc4,
	APQ_MUX_slimbus,
	APQ_MUX_spkr_i2s,
	APQ_MUX_tsif1,
	APQ_MUX_tsif2,
	APQ_MUX_usb2_hsic,
	APQ_MUX_ps_hold,
	APQ_MUX_NA,
};

static const char * const cam_mclk_groups[] = {
	"gpio4" "gpio5"
};
static const char * const codec_mic_i2s_groups[] = {
	"gpio34", "gpio35", "gpio36", "gpio37", "gpio38"
};
static const char * const codec_spkr_i2s_groups[] = {
	"gpio39", "gpio40", "gpio41", "gpio42"
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
	"gpio85", "gpio86", "gpio87", "gpio88", "gpio89"
};
static const char * const gp_clk_0a_groups[] = {
	"gpio3"
};
static const char * const gp_clk_0b_groups[] = {
	"gpio34"
};
static const char * const gp_clk_1a_groups[] = {
	"gpio4"
};
static const char * const gp_clk_1b_groups[] = {
	"gpio50"
};
static const char * const gp_clk_2a_groups[] = {
	"gpio32"
};
static const char * const gp_clk_2b_groups[] = {
	"gpio25"
};
static const char * const ps_hold_groups[] = {
	"gpio78"
};
static const char * const gsbi1_groups[] = {
	"gpio18", "gpio19", "gpio20", "gpio21"
};
static const char * const gsbi2_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25"
};
static const char * const gsbi3_groups[] = {
	"gpio6", "gpio7", "gpio8", "gpio9"
};
static const char * const gsbi4_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13"
};
static const char * const gsbi4_cam_i2c_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13"
};
static const char * const gsbi5_groups[] = {
	"gpio51", "gpio52", "gpio53", "gpio54"
};
static const char * const gsbi5_spi_cs1_groups[] = {
	"gpio47"
};
static const char * const gsbi5_spi_cs2_groups[] = {
	"gpio31"
};
static const char * const gsbi5_spi_cs3_groups[] = {
	"gpio32"
};
static const char * const gsbi6_groups[] = {
	"gpio14", "gpio15", "gpio16", "gpio17"
};
static const char * const gsbi6_spi_cs1_groups[] = {
	"gpio47"
};
static const char * const gsbi6_spi_cs2_groups[] = {
	"gpio31"
};
static const char * const gsbi6_spi_cs3_groups[] = {
	"gpio32"
};
static const char * const gsbi7_groups[] = {
	"gpio82", "gpio83", "gpio84", "gpio85"
};
static const char * const gsbi7_spi_cs1_groups[] = {
	"gpio47"
};
static const char * const gsbi7_spi_cs2_groups[] = {
	"gpio31"
};
static const char * const gsbi7_spi_cs3_groups[] = {
	"gpio32"
};
static const char * const gsbi_cam_i2c_groups[] = {
	"gpio10", "gpio11", "gpio12", "gpio13"
};
static const char * const hdmi_groups[] = {
	"gpio69", "gpio70", "gpio71", "gpio72"
};
static const char * const mi2s_groups[] = {
	"gpio27", "gpio28", "gpio29", "gpio30", "gpio31", "gpio32", "gpio33"
};
static const char * const riva_bt_groups[] = {
	"gpio16", "gpio17"
};
static const char * const riva_fm_groups[] = {
	"gpio14", "gpio15"
};
static const char * const riva_wlan_groups[] = {
	"gpio64", "gpio65", "gpio66", "gpio67", "gpio68"
};
static const char * const sdc2_groups[] = {
	"gpio57", "gpio58", "gpio59", "gpio60", "gpio61", "gpio62"
};
static const char * const sdc4_groups[] = {
	"gpio63", "gpio64", "gpio65", "gpio66", "gpio67", "gpio68"
};
static const char * const slimbus_groups[] = {
	"gpio40", "gpio41"
};
static const char * const spkr_i2s_groups[] = {
	"gpio47", "gpio48", "gpio49", "gpio50"
};
static const char * const tsif1_groups[] = {
	"gpio55", "gpio56", "gpio57"
};
static const char * const tsif2_groups[] = {
	"gpio58", "gpio59", "gpio60"
};
static const char * const usb2_hsic_groups[] = {
	"gpio88", "gpio89"
};

static const struct msm_function apq8064_functions[] = {
	FUNCTION(cam_mclk),
	FUNCTION(codec_mic_i2s),
	FUNCTION(codec_spkr_i2s),
	FUNCTION(gp_clk_0a),
	FUNCTION(gp_clk_0b),
	FUNCTION(gp_clk_1a),
	FUNCTION(gp_clk_1b),
	FUNCTION(gp_clk_2a),
	FUNCTION(gp_clk_2b),
	FUNCTION(gpio),
	FUNCTION(gsbi1),
	FUNCTION(gsbi2),
	FUNCTION(gsbi3),
	FUNCTION(gsbi4),
	FUNCTION(gsbi4_cam_i2c),
	FUNCTION(gsbi5),
	FUNCTION(gsbi5_spi_cs1),
	FUNCTION(gsbi5_spi_cs2),
	FUNCTION(gsbi5_spi_cs3),
	FUNCTION(gsbi6),
	FUNCTION(gsbi6_spi_cs1),
	FUNCTION(gsbi6_spi_cs2),
	FUNCTION(gsbi6_spi_cs3),
	FUNCTION(gsbi7),
	FUNCTION(gsbi7_spi_cs1),
	FUNCTION(gsbi7_spi_cs2),
	FUNCTION(gsbi7_spi_cs3),
	FUNCTION(gsbi_cam_i2c),
	FUNCTION(hdmi),
	FUNCTION(mi2s),
	FUNCTION(riva_bt),
	FUNCTION(riva_fm),
	FUNCTION(riva_wlan),
	FUNCTION(sdc2),
	FUNCTION(sdc4),
	FUNCTION(slimbus),
	FUNCTION(spkr_i2s),
	FUNCTION(tsif1),
	FUNCTION(tsif2),
	FUNCTION(usb2_hsic),
	FUNCTION(ps_hold),
};

static const struct msm_pingroup apq8064_groups[] = {
	PINGROUP(0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(3, NA, gp_clk_0a, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(4, NA, NA, cam_mclk, gp_clk_1a, NA, NA, NA, NA, NA, NA),
	PINGROUP(5, NA, cam_mclk, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(6, gsbi3, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(7, gsbi3, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(8, gsbi3, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(9, gsbi3, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(10, gsbi4, NA, NA, NA, NA, NA, NA, NA, gsbi4_cam_i2c, NA),
	PINGROUP(11, gsbi4, NA, NA, NA, NA, NA, NA, NA, NA, gsbi4_cam_i2c),
	PINGROUP(12, gsbi4, NA, NA, NA, NA, gsbi4_cam_i2c, NA, NA, NA, NA),
	PINGROUP(13, gsbi4, NA, NA, NA, NA, gsbi4_cam_i2c, NA, NA, NA, NA),
	PINGROUP(14, riva_fm, gsbi6, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(15, riva_fm, gsbi6, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(16, riva_bt, gsbi6, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(17, riva_bt, gsbi6, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(18, gsbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(19, gsbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(20, gsbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(21, gsbi1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(22, gsbi2, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(23, gsbi2, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(24, gsbi2, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(25, gsbi2, gp_clk_2b, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(26, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(27, mi2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(28, mi2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(29, mi2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(30, mi2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(31, mi2s, NA, gsbi5_spi_cs2, gsbi6_spi_cs2, gsbi7_spi_cs2, NA, NA, NA, NA, NA),
	PINGROUP(32, mi2s, gp_clk_2a, NA, NA, NA, gsbi5_spi_cs3, gsbi6_spi_cs3, gsbi7_spi_cs3, NA, NA),
	PINGROUP(33, mi2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(34, codec_mic_i2s, gp_clk_0b, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(35, codec_mic_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(36, codec_mic_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(37, codec_mic_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(38, codec_mic_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(39, codec_spkr_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(40, slimbus, codec_spkr_i2s, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(41, slimbus, codec_spkr_i2s, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(42, codec_spkr_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(43, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(44, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(45, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(46, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(47, spkr_i2s, gsbi5_spi_cs1, gsbi6_spi_cs1, gsbi7_spi_cs1, NA, NA, NA, NA, NA, NA),
	PINGROUP(48, spkr_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(49, spkr_i2s, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(50, spkr_i2s, gp_clk_1b, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(51, NA, gsbi5, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(52, NA, gsbi5, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(53, NA, gsbi5, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(54, NA, gsbi5, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(55, tsif1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(56, tsif1, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(57, tsif1, sdc2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(58, tsif2, sdc2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(59, tsif2, sdc2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(60, tsif2, sdc2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(61, NA, sdc2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(62, NA, sdc2, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(63, NA, sdc4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(64, riva_wlan, sdc4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(65, riva_wlan, sdc4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(66, riva_wlan, sdc4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(67, riva_wlan, sdc4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(68, riva_wlan, sdc4, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(69, hdmi, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(70, hdmi, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(71, hdmi, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(72, hdmi, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(73, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(74, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(75, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(76, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(77, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(78, ps_hold, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(79, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(80, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(81, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(82, NA, gsbi7, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(83, gsbi7, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(84, NA, gsbi7, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(85, NA, NA, gsbi7, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(86, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(87, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(88, usb2_hsic, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(89, usb2_hsic, NA, NA, NA, NA, NA, NA, NA, NA, NA),

	SDC_PINGROUP(sdc1_clk, 0x20a0, 13, 6),
	SDC_PINGROUP(sdc1_cmd, 0x20a0, 11, 3),
	SDC_PINGROUP(sdc1_data, 0x20a0, 9, 0),

	SDC_PINGROUP(sdc3_clk, 0x20a4, 14, 6),
	SDC_PINGROUP(sdc3_cmd, 0x20a4, 11, 3),
	SDC_PINGROUP(sdc3_data, 0x20a4, 9, 0),
};

#define NUM_GPIO_PINGROUPS 90

static const struct msm_pinctrl_soc_data apq8064_pinctrl = {
	.pins = apq8064_pins,
	.npins = ARRAY_SIZE(apq8064_pins),
	.functions = apq8064_functions,
	.nfunctions = ARRAY_SIZE(apq8064_functions),
	.groups = apq8064_groups,
	.ngroups = ARRAY_SIZE(apq8064_groups),
	.ngpios = NUM_GPIO_PINGROUPS,
};

static int apq8064_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &apq8064_pinctrl);
}

static const struct of_device_id apq8064_pinctrl_of_match[] = {
	{ .compatible = "qcom,apq8064-pinctrl", },
	{ },
};

static struct platform_driver apq8064_pinctrl_driver = {
	.driver = {
		.name = "apq8064-pinctrl",
		.of_match_table = apq8064_pinctrl_of_match,
	},
	.probe = apq8064_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init apq8064_pinctrl_init(void)
{
	return platform_driver_register(&apq8064_pinctrl_driver);
}
arch_initcall(apq8064_pinctrl_init);

static void __exit apq8064_pinctrl_exit(void)
{
	platform_driver_unregister(&apq8064_pinctrl_driver);
}
module_exit(apq8064_pinctrl_exit);

MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@sonymobile.com>");
MODULE_DESCRIPTION("Qualcomm APQ8064 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, apq8064_pinctrl_of_match);
