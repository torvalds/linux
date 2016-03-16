/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

static const struct pinctrl_pin_desc ipq4019_pins[] = {
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
};

#define DECLARE_QCA_GPIO_PINS(pin) \
	static const unsigned int gpio##pin##_pins[] = { pin }
DECLARE_QCA_GPIO_PINS(0);
DECLARE_QCA_GPIO_PINS(1);
DECLARE_QCA_GPIO_PINS(2);
DECLARE_QCA_GPIO_PINS(3);
DECLARE_QCA_GPIO_PINS(4);
DECLARE_QCA_GPIO_PINS(5);
DECLARE_QCA_GPIO_PINS(6);
DECLARE_QCA_GPIO_PINS(7);
DECLARE_QCA_GPIO_PINS(8);
DECLARE_QCA_GPIO_PINS(9);
DECLARE_QCA_GPIO_PINS(10);
DECLARE_QCA_GPIO_PINS(11);
DECLARE_QCA_GPIO_PINS(12);
DECLARE_QCA_GPIO_PINS(13);
DECLARE_QCA_GPIO_PINS(14);
DECLARE_QCA_GPIO_PINS(15);
DECLARE_QCA_GPIO_PINS(16);
DECLARE_QCA_GPIO_PINS(17);
DECLARE_QCA_GPIO_PINS(18);
DECLARE_QCA_GPIO_PINS(19);
DECLARE_QCA_GPIO_PINS(20);
DECLARE_QCA_GPIO_PINS(21);
DECLARE_QCA_GPIO_PINS(22);
DECLARE_QCA_GPIO_PINS(23);
DECLARE_QCA_GPIO_PINS(24);
DECLARE_QCA_GPIO_PINS(25);
DECLARE_QCA_GPIO_PINS(26);
DECLARE_QCA_GPIO_PINS(27);
DECLARE_QCA_GPIO_PINS(28);
DECLARE_QCA_GPIO_PINS(29);
DECLARE_QCA_GPIO_PINS(30);
DECLARE_QCA_GPIO_PINS(31);
DECLARE_QCA_GPIO_PINS(32);
DECLARE_QCA_GPIO_PINS(33);
DECLARE_QCA_GPIO_PINS(34);
DECLARE_QCA_GPIO_PINS(35);
DECLARE_QCA_GPIO_PINS(36);
DECLARE_QCA_GPIO_PINS(37);
DECLARE_QCA_GPIO_PINS(38);
DECLARE_QCA_GPIO_PINS(39);
DECLARE_QCA_GPIO_PINS(40);
DECLARE_QCA_GPIO_PINS(41);
DECLARE_QCA_GPIO_PINS(42);
DECLARE_QCA_GPIO_PINS(43);
DECLARE_QCA_GPIO_PINS(44);
DECLARE_QCA_GPIO_PINS(45);
DECLARE_QCA_GPIO_PINS(46);
DECLARE_QCA_GPIO_PINS(47);
DECLARE_QCA_GPIO_PINS(48);
DECLARE_QCA_GPIO_PINS(49);
DECLARE_QCA_GPIO_PINS(50);
DECLARE_QCA_GPIO_PINS(51);
DECLARE_QCA_GPIO_PINS(52);
DECLARE_QCA_GPIO_PINS(53);
DECLARE_QCA_GPIO_PINS(54);
DECLARE_QCA_GPIO_PINS(55);
DECLARE_QCA_GPIO_PINS(56);
DECLARE_QCA_GPIO_PINS(57);
DECLARE_QCA_GPIO_PINS(58);
DECLARE_QCA_GPIO_PINS(59);
DECLARE_QCA_GPIO_PINS(60);
DECLARE_QCA_GPIO_PINS(61);
DECLARE_QCA_GPIO_PINS(62);
DECLARE_QCA_GPIO_PINS(63);
DECLARE_QCA_GPIO_PINS(64);
DECLARE_QCA_GPIO_PINS(65);
DECLARE_QCA_GPIO_PINS(66);
DECLARE_QCA_GPIO_PINS(67);
DECLARE_QCA_GPIO_PINS(68);
DECLARE_QCA_GPIO_PINS(69);
DECLARE_QCA_GPIO_PINS(70);
DECLARE_QCA_GPIO_PINS(71);
DECLARE_QCA_GPIO_PINS(72);
DECLARE_QCA_GPIO_PINS(73);
DECLARE_QCA_GPIO_PINS(74);
DECLARE_QCA_GPIO_PINS(75);
DECLARE_QCA_GPIO_PINS(76);
DECLARE_QCA_GPIO_PINS(77);
DECLARE_QCA_GPIO_PINS(78);
DECLARE_QCA_GPIO_PINS(79);
DECLARE_QCA_GPIO_PINS(80);
DECLARE_QCA_GPIO_PINS(81);
DECLARE_QCA_GPIO_PINS(82);
DECLARE_QCA_GPIO_PINS(83);
DECLARE_QCA_GPIO_PINS(84);
DECLARE_QCA_GPIO_PINS(85);
DECLARE_QCA_GPIO_PINS(86);
DECLARE_QCA_GPIO_PINS(87);
DECLARE_QCA_GPIO_PINS(88);
DECLARE_QCA_GPIO_PINS(89);
DECLARE_QCA_GPIO_PINS(90);
DECLARE_QCA_GPIO_PINS(91);
DECLARE_QCA_GPIO_PINS(92);
DECLARE_QCA_GPIO_PINS(93);
DECLARE_QCA_GPIO_PINS(94);
DECLARE_QCA_GPIO_PINS(95);
DECLARE_QCA_GPIO_PINS(96);
DECLARE_QCA_GPIO_PINS(97);
DECLARE_QCA_GPIO_PINS(98);
DECLARE_QCA_GPIO_PINS(99);

#define FUNCTION(fname)			                \
	[qca_mux_##fname] = {		                \
		.name = #fname,				\
		.groups = fname##_groups,               \
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14) \
	{					        \
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.npins = (unsigned)ARRAY_SIZE(gpio##id##_pins),	\
		.funcs = (int[]){			\
			qca_mux_NA, /* gpio mode */	\
			qca_mux_##f1,			\
			qca_mux_##f2,			\
			qca_mux_##f3,			\
			qca_mux_##f4,			\
			qca_mux_##f5,			\
			qca_mux_##f6,			\
			qca_mux_##f7,			\
			qca_mux_##f8,			\
			qca_mux_##f9,			\
			qca_mux_##f10,			\
			qca_mux_##f11,			\
			qca_mux_##f12,			\
			qca_mux_##f13,			\
			qca_mux_##f14			\
		},				        \
		.nfuncs = 15,				\
		.ctl_reg = 0x1000 + 0x10 * id,		\
		.io_reg = 0x1004 + 0x10 * id,		\
		.intr_cfg_reg = 0x1008 + 0x10 * id,	\
		.intr_status_reg = 0x100c + 0x10 * id,	\
		.intr_target_reg = 0x400 + 0x4 * id,	\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
		.oe_bit = 9,			\
		.in_bit = 0,			\
		.out_bit = 1,			\
		.intr_enable_bit = 0,		\
		.intr_status_bit = 0,		\
		.intr_target_bit = 5,		\
		.intr_raw_status_bit = 4,	\
		.intr_polarity_bit = 1,		\
		.intr_detection_bit = 2,	\
		.intr_detection_width = 2,	\
	}


enum ipq4019_functions {
	qca_mux_gpio,
	qca_mux_blsp_uart1,
	qca_mux_blsp_i2c0,
	qca_mux_blsp_i2c1,
	qca_mux_blsp_uart0,
	qca_mux_blsp_spi1,
	qca_mux_blsp_spi0,
	qca_mux_NA,
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
	"gpio99",
};

static const char * const blsp_uart1_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11",
};
static const char * const blsp_i2c0_groups[] = {
	"gpio10", "gpio11", "gpio20", "gpio21", "gpio58", "gpio59",
};
static const char * const blsp_spi0_groups[] = {
	"gpio12", "gpio13", "gpio14", "gpio15", "gpio45",
	"gpio54", "gpio55", "gpio56", "gpio57",
};
static const char * const blsp_i2c1_groups[] = {
	"gpio12", "gpio13", "gpio34", "gpio35",
};
static const char * const blsp_uart0_groups[] = {
	"gpio16", "gpio17", "gpio60", "gpio61",
};
static const char * const blsp_spi1_groups[] = {
	"gpio44", "gpio45", "gpio46", "gpio47",
};

static const struct msm_function ipq4019_functions[] = {
	FUNCTION(gpio),
	FUNCTION(blsp_uart1),
	FUNCTION(blsp_i2c0),
	FUNCTION(blsp_i2c1),
	FUNCTION(blsp_uart0),
	FUNCTION(blsp_spi1),
	FUNCTION(blsp_spi0),
};

static const struct msm_pingroup ipq4019_groups[] = {
	PINGROUP(0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(2, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(3, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(4, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(5, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(6, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(7, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(8, blsp_uart1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(9, blsp_uart1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(10, blsp_uart1, NA, NA, blsp_i2c0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(11, blsp_uart1, NA, NA, blsp_i2c0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(12, blsp_spi0, blsp_i2c1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(13, blsp_spi0, blsp_i2c1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(14, blsp_spi0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(15, blsp_spi0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(16, blsp_uart0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(17, blsp_uart0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(18, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(19, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(20, blsp_i2c0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(21, blsp_i2c0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(22, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(23, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(24, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(25, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(26, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(27, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(28, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(29, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(30, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(31, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(32, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(33, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(34, blsp_i2c1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(35, blsp_i2c1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(36, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(37, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(38, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(39, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(40, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(41, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(42, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(43, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(44, NA, blsp_spi1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(45, NA, blsp_spi1, blsp_spi0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(46, NA, blsp_spi1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(47, NA, blsp_spi1, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(48, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(49, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(50, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(51, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(52, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(53, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(54, NA, blsp_spi0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(55, NA, blsp_spi0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(56, NA, blsp_spi0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(57, NA, blsp_spi0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(58, NA, NA, blsp_i2c0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(59, NA, blsp_i2c0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(60, NA, blsp_uart0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(61, NA, blsp_uart0, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(62, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(63, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(64, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(65, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(66, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(67, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(68, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
	PINGROUP(69, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA, NA),
};

static const struct msm_pinctrl_soc_data ipq4019_pinctrl = {
	.pins = ipq4019_pins,
	.npins = ARRAY_SIZE(ipq4019_pins),
	.functions = ipq4019_functions,
	.nfunctions = ARRAY_SIZE(ipq4019_functions),
	.groups = ipq4019_groups,
	.ngroups = ARRAY_SIZE(ipq4019_groups),
	.ngpios = 70,
};

static int ipq4019_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_probe(pdev, &ipq4019_pinctrl);
}

static const struct of_device_id ipq4019_pinctrl_of_match[] = {
	{ .compatible = "qcom,ipq4019-pinctrl", },
	{ },
};

static struct platform_driver ipq4019_pinctrl_driver = {
	.driver = {
		.name = "ipq4019-pinctrl",
		.of_match_table = ipq4019_pinctrl_of_match,
	},
	.probe = ipq4019_pinctrl_probe,
	.remove = msm_pinctrl_remove,
};

static int __init ipq4019_pinctrl_init(void)
{
	return platform_driver_register(&ipq4019_pinctrl_driver);
}
arch_initcall(ipq4019_pinctrl_init);

static void __exit ipq4019_pinctrl_exit(void)
{
	platform_driver_unregister(&ipq4019_pinctrl_driver);
}
module_exit(ipq4019_pinctrl_exit);

MODULE_DESCRIPTION("Qualcomm ipq4019 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, ipq4019_pinctrl_of_match);
