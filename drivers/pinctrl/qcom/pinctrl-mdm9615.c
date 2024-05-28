// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, Sony Mobile Communications AB.
 * Copyright (c) 2016 BayLibre, SAS.
 * Author : Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinmux.h>

#include "pinctrl-msm.h"

static const struct pinctrl_pin_desc mdm9615_pins[] = {
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

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11) \
	{						\
		.grp = PINCTRL_PINGROUP("gpio" #id, 	\
			gpio##id##_pins, 		\
			ARRAY_SIZE(gpio##id##_pins)),	\
		.funcs = (int[]){			\
			msm_mux_gpio,			\
			msm_mux_##f1,			\
			msm_mux_##f2,			\
			msm_mux_##f3,			\
			msm_mux_##f4,			\
			msm_mux_##f5,			\
			msm_mux_##f6,			\
			msm_mux_##f7,			\
			msm_mux_##f8,			\
			msm_mux_##f9,			\
			msm_mux_##f10,			\
			msm_mux_##f11			\
		},					\
		.nfuncs = 12,				\
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

enum mdm9615_functions {
	msm_mux_gpio,
	msm_mux_gsbi2_i2c,
	msm_mux_gsbi3,
	msm_mux_gsbi4,
	msm_mux_gsbi5_i2c,
	msm_mux_gsbi5_uart,
	msm_mux_sdc2,
	msm_mux_ebi2_lcdc,
	msm_mux_ps_hold,
	msm_mux_prim_audio,
	msm_mux_sec_audio,
	msm_mux_cdc_mclk,
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
	"gpio85", "gpio86", "gpio87"
};

static const char * const gsbi2_i2c_groups[] = {
	"gpio4", "gpio5"
};

static const char * const gsbi3_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};

static const char * const gsbi4_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15"
};

static const char * const gsbi5_i2c_groups[] = {
	"gpio16", "gpio17"
};

static const char * const gsbi5_uart_groups[] = {
	"gpio18", "gpio19"
};

static const char * const sdc2_groups[] = {
	"gpio25", "gpio26", "gpio27", "gpio28", "gpio29", "gpio30",
};

static const char * const ebi2_lcdc_groups[] = {
	"gpio21", "gpio22", "gpio24",
};

static const char * const ps_hold_groups[] = {
	"gpio83",
};

static const char * const prim_audio_groups[] = {
	"gpio20", "gpio21", "gpio22", "gpio23",
};

static const char * const sec_audio_groups[] = {
	"gpio25", "gpio26", "gpio27", "gpio28",
};

static const char * const cdc_mclk_groups[] = {
	"gpio24",
};

static const struct pinfunction mdm9615_functions[] = {
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(gsbi2_i2c),
	MSM_PIN_FUNCTION(gsbi3),
	MSM_PIN_FUNCTION(gsbi4),
	MSM_PIN_FUNCTION(gsbi5_i2c),
	MSM_PIN_FUNCTION(gsbi5_uart),
	MSM_PIN_FUNCTION(sdc2),
	MSM_PIN_FUNCTION(ebi2_lcdc),
	MSM_PIN_FUNCTION(ps_hold),
	MSM_PIN_FUNCTION(prim_audio),
	MSM_PIN_FUNCTION(sec_audio),
	MSM_PIN_FUNCTION(cdc_mclk),
};

static const struct msm_pingroup mdm9615_groups[] = {
	PINGROUP(0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(3, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(4, gsbi2_i2c, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(5, gsbi2_i2c, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(6, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(7, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(8, gsbi3, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(9, gsbi3, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(10, gsbi3, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(11, gsbi3, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(12, gsbi4, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(13, gsbi4, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(14, gsbi4, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(15, gsbi4, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(16, gsbi5_i2c, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(17, gsbi5_i2c, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(18, gsbi5_uart, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(19, gsbi5_uart, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(20, prim_audio, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(21, prim_audio, ebi2_lcdc, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(22, prim_audio, ebi2_lcdc, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(23, prim_audio, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(24, cdc_mclk, NA, ebi2_lcdc, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(25, sdc2, sec_audio, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(26, sdc2, sec_audio, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(27, sdc2, sec_audio, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(28, sdc2, sec_audio, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(29, sdc2, sec_audio, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(30, sdc2, sec_audio, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(31, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(32, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(33, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(34, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(35, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(36, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(37, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(38, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(39, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(40, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(41, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(42, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(43, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(44, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(45, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(46, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(47, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(48, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(49, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(50, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(51, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(52, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(53, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(54, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(55, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(56, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(57, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(58, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(59, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(60, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(61, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(62, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(63, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(64, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(65, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(66, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(67, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(68, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(69, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(70, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(71, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(72, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(73, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(74, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(75, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(76, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(77, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(78, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(79, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(80, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(81, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(82, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(83, ps_hold, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(84, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(85, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(86, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(87, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
};

#define NUM_GPIO_PINGROUPS 88

static const struct msm_pinctrl_soc_data mdm9615_pinctrl = {
	.pins = mdm9615_pins,
	.npins = ARRAY_SIZE(mdm9615_pins),
	.functions = mdm9615_functions,
	.nfunctions = ARRAY_SIZE(mdm9615_functions),
	.groups = mdm9615_groups,
	.ngroups = ARRAY_SIZE(mdm9615_groups),
	.ngpios = NUM_GPIO_PINGROUPS,
};

static int mdm9615_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &mdm9615_pinctrl);
}

static const struct of_device_id mdm9615_pinctrl_of_match[] = {
	{ .compatible = "qcom,mdm9615-pinctrl", },
	{ },
};

static struct platform_driver mdm9615_pinctrl_driver = {
	.driver = {
		.name = "mdm9615-pinctrl",
		.of_match_table = mdm9615_pinctrl_of_match,
	},
	.probe = mdm9615_pinctrl_probe,
	.remove_new = msm_pinctrl_remove,
};

static int __init mdm9615_pinctrl_init(void)
{
	return platform_driver_register(&mdm9615_pinctrl_driver);
}
arch_initcall(mdm9615_pinctrl_init);

static void __exit mdm9615_pinctrl_exit(void)
{
	platform_driver_unregister(&mdm9615_pinctrl_driver);
}
module_exit(mdm9615_pinctrl_exit);

MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION("Qualcomm MDM9615 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, mdm9615_pinctrl_of_match);
