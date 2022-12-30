// SPDX-License-Identifier: GPL-2.0+
/*
 * Bitmain BM1880 SoC Pinctrl driver
 *
 * Copyright (c) 2019 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinctrl-utils.h"

#define BM1880_REG_MUX 0x20

/**
 * struct bm1880_pinctrl - driver data
 * @base:	Pinctrl base address
 * @pctrldev:	Pinctrl device
 * @groups:	Pingroups
 * @ngroups:	Number of @groups
 * @funcs:	Pinmux functions
 * @nfuncs:	Number of @funcs
 * @pinconf:	Pinconf data
 */
struct bm1880_pinctrl {
	void __iomem *base;
	struct pinctrl_dev *pctrldev;
	const struct bm1880_pctrl_group *groups;
	unsigned int ngroups;
	const struct bm1880_pinmux_function *funcs;
	unsigned int nfuncs;
	const struct bm1880_pinconf_data *pinconf;
};

/**
 * struct bm1880_pctrl_group - pinctrl group
 * @name:	Name of the group
 * @pins:	Array of pins belonging to this group
 * @npins:	Number of @pins
 */
struct bm1880_pctrl_group {
	const char *name;
	const unsigned int *pins;
	const unsigned int npins;
};

/**
 * struct bm1880_pinmux_function - a pinmux function
 * @name:	Name of the pinmux function.
 * @groups:	List of pingroups for this function.
 * @ngroups:	Number of entries in @groups.
 * @mux_val:	Selector for this function
 * @mux:	Offset of function specific mux
 * @mux_shift:	Shift for function specific selector
 */
struct bm1880_pinmux_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
	u32 mux_val;
	u32 mux;
	u8 mux_shift;
};

/**
 * struct bm1880_pinconf_data - pinconf data
 * @drv_bits:	Drive strength bit width
 */
struct bm1880_pinconf_data {
	u32 drv_bits;
};

static const struct pinctrl_pin_desc bm1880_pins[] = {
	PINCTRL_PIN(0,   "MIO0"),
	PINCTRL_PIN(1,   "MIO1"),
	PINCTRL_PIN(2,   "MIO2"),
	PINCTRL_PIN(3,   "MIO3"),
	PINCTRL_PIN(4,   "MIO4"),
	PINCTRL_PIN(5,   "MIO5"),
	PINCTRL_PIN(6,   "MIO6"),
	PINCTRL_PIN(7,   "MIO7"),
	PINCTRL_PIN(8,   "MIO8"),
	PINCTRL_PIN(9,   "MIO9"),
	PINCTRL_PIN(10,   "MIO10"),
	PINCTRL_PIN(11,   "MIO11"),
	PINCTRL_PIN(12,   "MIO12"),
	PINCTRL_PIN(13,   "MIO13"),
	PINCTRL_PIN(14,   "MIO14"),
	PINCTRL_PIN(15,   "MIO15"),
	PINCTRL_PIN(16,   "MIO16"),
	PINCTRL_PIN(17,   "MIO17"),
	PINCTRL_PIN(18,   "MIO18"),
	PINCTRL_PIN(19,   "MIO19"),
	PINCTRL_PIN(20,   "MIO20"),
	PINCTRL_PIN(21,   "MIO21"),
	PINCTRL_PIN(22,   "MIO22"),
	PINCTRL_PIN(23,   "MIO23"),
	PINCTRL_PIN(24,   "MIO24"),
	PINCTRL_PIN(25,   "MIO25"),
	PINCTRL_PIN(26,   "MIO26"),
	PINCTRL_PIN(27,   "MIO27"),
	PINCTRL_PIN(28,   "MIO28"),
	PINCTRL_PIN(29,   "MIO29"),
	PINCTRL_PIN(30,   "MIO30"),
	PINCTRL_PIN(31,   "MIO31"),
	PINCTRL_PIN(32,   "MIO32"),
	PINCTRL_PIN(33,   "MIO33"),
	PINCTRL_PIN(34,   "MIO34"),
	PINCTRL_PIN(35,   "MIO35"),
	PINCTRL_PIN(36,   "MIO36"),
	PINCTRL_PIN(37,   "MIO37"),
	PINCTRL_PIN(38,   "MIO38"),
	PINCTRL_PIN(39,   "MIO39"),
	PINCTRL_PIN(40,   "MIO40"),
	PINCTRL_PIN(41,   "MIO41"),
	PINCTRL_PIN(42,   "MIO42"),
	PINCTRL_PIN(43,   "MIO43"),
	PINCTRL_PIN(44,   "MIO44"),
	PINCTRL_PIN(45,   "MIO45"),
	PINCTRL_PIN(46,   "MIO46"),
	PINCTRL_PIN(47,   "MIO47"),
	PINCTRL_PIN(48,   "MIO48"),
	PINCTRL_PIN(49,   "MIO49"),
	PINCTRL_PIN(50,   "MIO50"),
	PINCTRL_PIN(51,   "MIO51"),
	PINCTRL_PIN(52,   "MIO52"),
	PINCTRL_PIN(53,   "MIO53"),
	PINCTRL_PIN(54,   "MIO54"),
	PINCTRL_PIN(55,   "MIO55"),
	PINCTRL_PIN(56,   "MIO56"),
	PINCTRL_PIN(57,   "MIO57"),
	PINCTRL_PIN(58,   "MIO58"),
	PINCTRL_PIN(59,   "MIO59"),
	PINCTRL_PIN(60,   "MIO60"),
	PINCTRL_PIN(61,   "MIO61"),
	PINCTRL_PIN(62,   "MIO62"),
	PINCTRL_PIN(63,   "MIO63"),
	PINCTRL_PIN(64,   "MIO64"),
	PINCTRL_PIN(65,   "MIO65"),
	PINCTRL_PIN(66,   "MIO66"),
	PINCTRL_PIN(67,   "MIO67"),
	PINCTRL_PIN(68,   "MIO68"),
	PINCTRL_PIN(69,   "MIO69"),
	PINCTRL_PIN(70,   "MIO70"),
	PINCTRL_PIN(71,   "MIO71"),
	PINCTRL_PIN(72,   "MIO72"),
	PINCTRL_PIN(73,   "MIO73"),
	PINCTRL_PIN(74,   "MIO74"),
	PINCTRL_PIN(75,   "MIO75"),
	PINCTRL_PIN(76,   "MIO76"),
	PINCTRL_PIN(77,   "MIO77"),
	PINCTRL_PIN(78,   "MIO78"),
	PINCTRL_PIN(79,   "MIO79"),
	PINCTRL_PIN(80,   "MIO80"),
	PINCTRL_PIN(81,   "MIO81"),
	PINCTRL_PIN(82,   "MIO82"),
	PINCTRL_PIN(83,   "MIO83"),
	PINCTRL_PIN(84,   "MIO84"),
	PINCTRL_PIN(85,   "MIO85"),
	PINCTRL_PIN(86,   "MIO86"),
	PINCTRL_PIN(87,   "MIO87"),
	PINCTRL_PIN(88,   "MIO88"),
	PINCTRL_PIN(89,   "MIO89"),
	PINCTRL_PIN(90,   "MIO90"),
	PINCTRL_PIN(91,   "MIO91"),
	PINCTRL_PIN(92,   "MIO92"),
	PINCTRL_PIN(93,   "MIO93"),
	PINCTRL_PIN(94,   "MIO94"),
	PINCTRL_PIN(95,   "MIO95"),
	PINCTRL_PIN(96,   "MIO96"),
	PINCTRL_PIN(97,   "MIO97"),
	PINCTRL_PIN(98,   "MIO98"),
	PINCTRL_PIN(99,   "MIO99"),
	PINCTRL_PIN(100,   "MIO100"),
	PINCTRL_PIN(101,   "MIO101"),
	PINCTRL_PIN(102,   "MIO102"),
	PINCTRL_PIN(103,   "MIO103"),
	PINCTRL_PIN(104,   "MIO104"),
	PINCTRL_PIN(105,   "MIO105"),
	PINCTRL_PIN(106,   "MIO106"),
	PINCTRL_PIN(107,   "MIO107"),
	PINCTRL_PIN(108,   "MIO108"),
	PINCTRL_PIN(109,   "MIO109"),
	PINCTRL_PIN(110,   "MIO110"),
	PINCTRL_PIN(111,   "MIO111"),
};

enum bm1880_pinmux_functions {
	F_nand, F_spi, F_emmc, F_sdio, F_eth0, F_pwm0, F_pwm1, F_pwm2,
	F_pwm3, F_pwm4, F_pwm5, F_pwm6, F_pwm7, F_pwm8, F_pwm9, F_pwm10,
	F_pwm11, F_pwm12, F_pwm13, F_pwm14, F_pwm15, F_pwm16, F_pwm17,
	F_pwm18, F_pwm19, F_pwm20, F_pwm21, F_pwm22, F_pwm23, F_pwm24,
	F_pwm25, F_pwm26, F_pwm27, F_pwm28, F_pwm29, F_pwm30, F_pwm31,
	F_pwm32, F_pwm33, F_pwm34, F_pwm35, F_pwm36, F_pwm37, F_i2c0, F_i2c1,
	F_i2c2, F_i2c3, F_i2c4, F_uart0, F_uart1, F_uart2, F_uart3, F_uart4,
	F_uart5, F_uart6, F_uart7, F_uart8, F_uart9, F_uart10, F_uart11,
	F_uart12, F_uart13, F_uart14, F_uart15, F_gpio0, F_gpio1, F_gpio2,
	F_gpio3, F_gpio4, F_gpio5, F_gpio6, F_gpio7, F_gpio8, F_gpio9, F_gpio10,
	F_gpio11, F_gpio12, F_gpio13, F_gpio14, F_gpio15, F_gpio16, F_gpio17,
	F_gpio18, F_gpio19, F_gpio20, F_gpio21, F_gpio22, F_gpio23, F_gpio24,
	F_gpio25, F_gpio26, F_gpio27, F_gpio28, F_gpio29, F_gpio30, F_gpio31,
	F_gpio32, F_gpio33, F_gpio34, F_gpio35, F_gpio36, F_gpio37, F_gpio38,
	F_gpio39, F_gpio40, F_gpio41, F_gpio42, F_gpio43, F_gpio44, F_gpio45,
	F_gpio46, F_gpio47, F_gpio48, F_gpio49, F_gpio50, F_gpio51, F_gpio52,
	F_gpio53, F_gpio54, F_gpio55, F_gpio56, F_gpio57, F_gpio58, F_gpio59,
	F_gpio60, F_gpio61, F_gpio62, F_gpio63, F_gpio64, F_gpio65, F_gpio66,
	F_gpio67, F_eth1, F_i2s0, F_i2s0_mclkin, F_i2s1, F_i2s1_mclkin, F_spi0,
	F_max
};

static const unsigned int nand_pins[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
					  10, 11, 12, 13, 14, 15, 16 };
static const unsigned int spi_pins[] = { 0, 1, 8, 10, 11, 12, 13 };
static const unsigned int emmc_pins[] = { 2, 3, 4, 5, 6, 7, 9, 14, 15, 16 };
static const unsigned int sdio_pins[] = { 17, 18, 19, 20, 21, 22, 23, 24,
					  25, 26 };
static const unsigned int eth0_pins[] = { 27, 28, 29, 30, 31, 32, 33, 34, 35,
					  36, 37, 38, 39, 40, 41, 42 };
static const unsigned int pwm0_pins[] = { 29 };
static const unsigned int pwm1_pins[] = { 30 };
static const unsigned int pwm2_pins[] = { 34 };
static const unsigned int pwm3_pins[] = { 35 };
static const unsigned int pwm4_pins[] = { 43 };
static const unsigned int pwm5_pins[] = { 44 };
static const unsigned int pwm6_pins[] = { 45 };
static const unsigned int pwm7_pins[] = { 46 };
static const unsigned int pwm8_pins[] = { 47 };
static const unsigned int pwm9_pins[] = { 48 };
static const unsigned int pwm10_pins[] = { 49 };
static const unsigned int pwm11_pins[] = { 50 };
static const unsigned int pwm12_pins[] = { 51 };
static const unsigned int pwm13_pins[] = { 52 };
static const unsigned int pwm14_pins[] = { 53 };
static const unsigned int pwm15_pins[] = { 54 };
static const unsigned int pwm16_pins[] = { 55 };
static const unsigned int pwm17_pins[] = { 56 };
static const unsigned int pwm18_pins[] = { 57 };
static const unsigned int pwm19_pins[] = { 58 };
static const unsigned int pwm20_pins[] = { 59 };
static const unsigned int pwm21_pins[] = { 60 };
static const unsigned int pwm22_pins[] = { 61 };
static const unsigned int pwm23_pins[] = { 62 };
static const unsigned int pwm24_pins[] = { 97 };
static const unsigned int pwm25_pins[] = { 98 };
static const unsigned int pwm26_pins[] = { 99 };
static const unsigned int pwm27_pins[] = { 100 };
static const unsigned int pwm28_pins[] = { 101 };
static const unsigned int pwm29_pins[] = { 102 };
static const unsigned int pwm30_pins[] = { 103 };
static const unsigned int pwm31_pins[] = { 104 };
static const unsigned int pwm32_pins[] = { 105 };
static const unsigned int pwm33_pins[] = { 106 };
static const unsigned int pwm34_pins[] = { 107 };
static const unsigned int pwm35_pins[] = { 108 };
static const unsigned int pwm36_pins[] = { 109 };
static const unsigned int pwm37_pins[] = { 110 };
static const unsigned int i2c0_pins[] = { 63, 64 };
static const unsigned int i2c1_pins[] = { 65, 66 };
static const unsigned int i2c2_pins[] = { 67, 68 };
static const unsigned int i2c3_pins[] = { 69, 70 };
static const unsigned int i2c4_pins[] = { 71, 72 };
static const unsigned int uart0_pins[] = { 73, 74 };
static const unsigned int uart1_pins[] = { 75, 76 };
static const unsigned int uart2_pins[] = { 77, 78 };
static const unsigned int uart3_pins[] = { 79, 80 };
static const unsigned int uart4_pins[] = { 81, 82 };
static const unsigned int uart5_pins[] = { 83, 84 };
static const unsigned int uart6_pins[] = { 85, 86 };
static const unsigned int uart7_pins[] = { 87, 88 };
static const unsigned int uart8_pins[] = { 89, 90 };
static const unsigned int uart9_pins[] = { 91, 92 };
static const unsigned int uart10_pins[] = { 93, 94 };
static const unsigned int uart11_pins[] = { 95, 96 };
static const unsigned int uart12_pins[] = { 73, 74, 75, 76 };
static const unsigned int uart13_pins[] = { 77, 78, 83, 84 };
static const unsigned int uart14_pins[] = { 79, 80, 85, 86 };
static const unsigned int uart15_pins[] = { 81, 82, 87, 88 };
static const unsigned int gpio0_pins[] = { 97 };
static const unsigned int gpio1_pins[] = { 98 };
static const unsigned int gpio2_pins[] = { 99 };
static const unsigned int gpio3_pins[] = { 100 };
static const unsigned int gpio4_pins[] = { 101 };
static const unsigned int gpio5_pins[] = { 102 };
static const unsigned int gpio6_pins[] = { 103 };
static const unsigned int gpio7_pins[] = { 104 };
static const unsigned int gpio8_pins[] = { 105 };
static const unsigned int gpio9_pins[] = { 106 };
static const unsigned int gpio10_pins[] = { 107 };
static const unsigned int gpio11_pins[] = { 108 };
static const unsigned int gpio12_pins[] = { 109 };
static const unsigned int gpio13_pins[] = { 110 };
static const unsigned int gpio14_pins[] = { 43 };
static const unsigned int gpio15_pins[] = { 44 };
static const unsigned int gpio16_pins[] = { 45 };
static const unsigned int gpio17_pins[] = { 46 };
static const unsigned int gpio18_pins[] = { 47 };
static const unsigned int gpio19_pins[] = { 48 };
static const unsigned int gpio20_pins[] = { 49 };
static const unsigned int gpio21_pins[] = { 50 };
static const unsigned int gpio22_pins[] = { 51 };
static const unsigned int gpio23_pins[] = { 52 };
static const unsigned int gpio24_pins[] = { 53 };
static const unsigned int gpio25_pins[] = { 54 };
static const unsigned int gpio26_pins[] = { 55 };
static const unsigned int gpio27_pins[] = { 56 };
static const unsigned int gpio28_pins[] = { 57 };
static const unsigned int gpio29_pins[] = { 58 };
static const unsigned int gpio30_pins[] = { 59 };
static const unsigned int gpio31_pins[] = { 60 };
static const unsigned int gpio32_pins[] = { 61 };
static const unsigned int gpio33_pins[] = { 62 };
static const unsigned int gpio34_pins[] = { 63 };
static const unsigned int gpio35_pins[] = { 64 };
static const unsigned int gpio36_pins[] = { 65 };
static const unsigned int gpio37_pins[] = { 66 };
static const unsigned int gpio38_pins[] = { 67 };
static const unsigned int gpio39_pins[] = { 68 };
static const unsigned int gpio40_pins[] = { 69 };
static const unsigned int gpio41_pins[] = { 70 };
static const unsigned int gpio42_pins[] = { 71 };
static const unsigned int gpio43_pins[] = { 72 };
static const unsigned int gpio44_pins[] = { 73 };
static const unsigned int gpio45_pins[] = { 74 };
static const unsigned int gpio46_pins[] = { 75 };
static const unsigned int gpio47_pins[] = { 76 };
static const unsigned int gpio48_pins[] = { 77 };
static const unsigned int gpio49_pins[] = { 78 };
static const unsigned int gpio50_pins[] = { 79 };
static const unsigned int gpio51_pins[] = { 80 };
static const unsigned int gpio52_pins[] = { 81 };
static const unsigned int gpio53_pins[] = { 82 };
static const unsigned int gpio54_pins[] = { 83 };
static const unsigned int gpio55_pins[] = { 84 };
static const unsigned int gpio56_pins[] = { 85 };
static const unsigned int gpio57_pins[] = { 86 };
static const unsigned int gpio58_pins[] = { 87 };
static const unsigned int gpio59_pins[] = { 88 };
static const unsigned int gpio60_pins[] = { 89 };
static const unsigned int gpio61_pins[] = { 90 };
static const unsigned int gpio62_pins[] = { 91 };
static const unsigned int gpio63_pins[] = { 92 };
static const unsigned int gpio64_pins[] = { 93 };
static const unsigned int gpio65_pins[] = { 94 };
static const unsigned int gpio66_pins[] = { 95 };
static const unsigned int gpio67_pins[] = { 96 };
static const unsigned int eth1_pins[] = { 43, 44, 45, 46, 47, 48, 49, 50, 51,
					  52, 53, 54, 55, 56, 57, 58 };
static const unsigned int i2s0_pins[] = { 87, 88, 89, 90, 91 };
static const unsigned int i2s0_mclkin_pins[] = { 97 };
static const unsigned int i2s1_pins[] = { 92, 93, 94, 95, 96 };
static const unsigned int i2s1_mclkin_pins[] = { 98 };
static const unsigned int spi0_pins[] = { 59, 60, 61, 62 };

#define BM1880_PINCTRL_GRP(nm) \
	{ \
		.name = #nm "_grp", \
		.pins = nm ## _pins, \
		.npins = ARRAY_SIZE(nm ## _pins), \
	}

static const struct bm1880_pctrl_group bm1880_pctrl_groups[] = {
	BM1880_PINCTRL_GRP(nand),
	BM1880_PINCTRL_GRP(spi),
	BM1880_PINCTRL_GRP(emmc),
	BM1880_PINCTRL_GRP(sdio),
	BM1880_PINCTRL_GRP(eth0),
	BM1880_PINCTRL_GRP(pwm0),
	BM1880_PINCTRL_GRP(pwm1),
	BM1880_PINCTRL_GRP(pwm2),
	BM1880_PINCTRL_GRP(pwm3),
	BM1880_PINCTRL_GRP(pwm4),
	BM1880_PINCTRL_GRP(pwm5),
	BM1880_PINCTRL_GRP(pwm6),
	BM1880_PINCTRL_GRP(pwm7),
	BM1880_PINCTRL_GRP(pwm8),
	BM1880_PINCTRL_GRP(pwm9),
	BM1880_PINCTRL_GRP(pwm10),
	BM1880_PINCTRL_GRP(pwm11),
	BM1880_PINCTRL_GRP(pwm12),
	BM1880_PINCTRL_GRP(pwm13),
	BM1880_PINCTRL_GRP(pwm14),
	BM1880_PINCTRL_GRP(pwm15),
	BM1880_PINCTRL_GRP(pwm16),
	BM1880_PINCTRL_GRP(pwm17),
	BM1880_PINCTRL_GRP(pwm18),
	BM1880_PINCTRL_GRP(pwm19),
	BM1880_PINCTRL_GRP(pwm20),
	BM1880_PINCTRL_GRP(pwm21),
	BM1880_PINCTRL_GRP(pwm22),
	BM1880_PINCTRL_GRP(pwm23),
	BM1880_PINCTRL_GRP(pwm24),
	BM1880_PINCTRL_GRP(pwm25),
	BM1880_PINCTRL_GRP(pwm26),
	BM1880_PINCTRL_GRP(pwm27),
	BM1880_PINCTRL_GRP(pwm28),
	BM1880_PINCTRL_GRP(pwm29),
	BM1880_PINCTRL_GRP(pwm30),
	BM1880_PINCTRL_GRP(pwm31),
	BM1880_PINCTRL_GRP(pwm32),
	BM1880_PINCTRL_GRP(pwm33),
	BM1880_PINCTRL_GRP(pwm34),
	BM1880_PINCTRL_GRP(pwm35),
	BM1880_PINCTRL_GRP(pwm36),
	BM1880_PINCTRL_GRP(pwm37),
	BM1880_PINCTRL_GRP(i2c0),
	BM1880_PINCTRL_GRP(i2c1),
	BM1880_PINCTRL_GRP(i2c2),
	BM1880_PINCTRL_GRP(i2c3),
	BM1880_PINCTRL_GRP(i2c4),
	BM1880_PINCTRL_GRP(uart0),
	BM1880_PINCTRL_GRP(uart1),
	BM1880_PINCTRL_GRP(uart2),
	BM1880_PINCTRL_GRP(uart3),
	BM1880_PINCTRL_GRP(uart4),
	BM1880_PINCTRL_GRP(uart5),
	BM1880_PINCTRL_GRP(uart6),
	BM1880_PINCTRL_GRP(uart7),
	BM1880_PINCTRL_GRP(uart8),
	BM1880_PINCTRL_GRP(uart9),
	BM1880_PINCTRL_GRP(uart10),
	BM1880_PINCTRL_GRP(uart11),
	BM1880_PINCTRL_GRP(uart12),
	BM1880_PINCTRL_GRP(uart13),
	BM1880_PINCTRL_GRP(uart14),
	BM1880_PINCTRL_GRP(uart15),
	BM1880_PINCTRL_GRP(gpio0),
	BM1880_PINCTRL_GRP(gpio1),
	BM1880_PINCTRL_GRP(gpio2),
	BM1880_PINCTRL_GRP(gpio3),
	BM1880_PINCTRL_GRP(gpio4),
	BM1880_PINCTRL_GRP(gpio5),
	BM1880_PINCTRL_GRP(gpio6),
	BM1880_PINCTRL_GRP(gpio7),
	BM1880_PINCTRL_GRP(gpio8),
	BM1880_PINCTRL_GRP(gpio9),
	BM1880_PINCTRL_GRP(gpio10),
	BM1880_PINCTRL_GRP(gpio11),
	BM1880_PINCTRL_GRP(gpio12),
	BM1880_PINCTRL_GRP(gpio13),
	BM1880_PINCTRL_GRP(gpio14),
	BM1880_PINCTRL_GRP(gpio15),
	BM1880_PINCTRL_GRP(gpio16),
	BM1880_PINCTRL_GRP(gpio17),
	BM1880_PINCTRL_GRP(gpio18),
	BM1880_PINCTRL_GRP(gpio19),
	BM1880_PINCTRL_GRP(gpio20),
	BM1880_PINCTRL_GRP(gpio21),
	BM1880_PINCTRL_GRP(gpio22),
	BM1880_PINCTRL_GRP(gpio23),
	BM1880_PINCTRL_GRP(gpio24),
	BM1880_PINCTRL_GRP(gpio25),
	BM1880_PINCTRL_GRP(gpio26),
	BM1880_PINCTRL_GRP(gpio27),
	BM1880_PINCTRL_GRP(gpio28),
	BM1880_PINCTRL_GRP(gpio29),
	BM1880_PINCTRL_GRP(gpio30),
	BM1880_PINCTRL_GRP(gpio31),
	BM1880_PINCTRL_GRP(gpio32),
	BM1880_PINCTRL_GRP(gpio33),
	BM1880_PINCTRL_GRP(gpio34),
	BM1880_PINCTRL_GRP(gpio35),
	BM1880_PINCTRL_GRP(gpio36),
	BM1880_PINCTRL_GRP(gpio37),
	BM1880_PINCTRL_GRP(gpio38),
	BM1880_PINCTRL_GRP(gpio39),
	BM1880_PINCTRL_GRP(gpio40),
	BM1880_PINCTRL_GRP(gpio41),
	BM1880_PINCTRL_GRP(gpio42),
	BM1880_PINCTRL_GRP(gpio43),
	BM1880_PINCTRL_GRP(gpio44),
	BM1880_PINCTRL_GRP(gpio45),
	BM1880_PINCTRL_GRP(gpio46),
	BM1880_PINCTRL_GRP(gpio47),
	BM1880_PINCTRL_GRP(gpio48),
	BM1880_PINCTRL_GRP(gpio49),
	BM1880_PINCTRL_GRP(gpio50),
	BM1880_PINCTRL_GRP(gpio51),
	BM1880_PINCTRL_GRP(gpio52),
	BM1880_PINCTRL_GRP(gpio53),
	BM1880_PINCTRL_GRP(gpio54),
	BM1880_PINCTRL_GRP(gpio55),
	BM1880_PINCTRL_GRP(gpio56),
	BM1880_PINCTRL_GRP(gpio57),
	BM1880_PINCTRL_GRP(gpio58),
	BM1880_PINCTRL_GRP(gpio59),
	BM1880_PINCTRL_GRP(gpio60),
	BM1880_PINCTRL_GRP(gpio61),
	BM1880_PINCTRL_GRP(gpio62),
	BM1880_PINCTRL_GRP(gpio63),
	BM1880_PINCTRL_GRP(gpio64),
	BM1880_PINCTRL_GRP(gpio65),
	BM1880_PINCTRL_GRP(gpio66),
	BM1880_PINCTRL_GRP(gpio67),
	BM1880_PINCTRL_GRP(eth1),
	BM1880_PINCTRL_GRP(i2s0),
	BM1880_PINCTRL_GRP(i2s0_mclkin),
	BM1880_PINCTRL_GRP(i2s1),
	BM1880_PINCTRL_GRP(i2s1_mclkin),
	BM1880_PINCTRL_GRP(spi0),
};

static const char * const nand_group[] = { "nand_grp" };
static const char * const spi_group[] = { "spi_grp" };
static const char * const emmc_group[] = { "emmc_grp" };
static const char * const sdio_group[] = { "sdio_grp" };
static const char * const eth0_group[] = { "eth0_grp" };
static const char * const pwm0_group[] = { "pwm0_grp" };
static const char * const pwm1_group[] = { "pwm1_grp" };
static const char * const pwm2_group[] = { "pwm2_grp" };
static const char * const pwm3_group[] = { "pwm3_grp" };
static const char * const pwm4_group[] = { "pwm4_grp" };
static const char * const pwm5_group[] = { "pwm5_grp" };
static const char * const pwm6_group[] = { "pwm6_grp" };
static const char * const pwm7_group[] = { "pwm7_grp" };
static const char * const pwm8_group[] = { "pwm8_grp" };
static const char * const pwm9_group[] = { "pwm9_grp" };
static const char * const pwm10_group[] = { "pwm10_grp" };
static const char * const pwm11_group[] = { "pwm11_grp" };
static const char * const pwm12_group[] = { "pwm12_grp" };
static const char * const pwm13_group[] = { "pwm13_grp" };
static const char * const pwm14_group[] = { "pwm14_grp" };
static const char * const pwm15_group[] = { "pwm15_grp" };
static const char * const pwm16_group[] = { "pwm16_grp" };
static const char * const pwm17_group[] = { "pwm17_grp" };
static const char * const pwm18_group[] = { "pwm18_grp" };
static const char * const pwm19_group[] = { "pwm19_grp" };
static const char * const pwm20_group[] = { "pwm20_grp" };
static const char * const pwm21_group[] = { "pwm21_grp" };
static const char * const pwm22_group[] = { "pwm22_grp" };
static const char * const pwm23_group[] = { "pwm23_grp" };
static const char * const pwm24_group[] = { "pwm24_grp" };
static const char * const pwm25_group[] = { "pwm25_grp" };
static const char * const pwm26_group[] = { "pwm26_grp" };
static const char * const pwm27_group[] = { "pwm27_grp" };
static const char * const pwm28_group[] = { "pwm28_grp" };
static const char * const pwm29_group[] = { "pwm29_grp" };
static const char * const pwm30_group[] = { "pwm30_grp" };
static const char * const pwm31_group[] = { "pwm31_grp" };
static const char * const pwm32_group[] = { "pwm32_grp" };
static const char * const pwm33_group[] = { "pwm33_grp" };
static const char * const pwm34_group[] = { "pwm34_grp" };
static const char * const pwm35_group[] = { "pwm35_grp" };
static const char * const pwm36_group[] = { "pwm36_grp" };
static const char * const pwm37_group[] = { "pwm37_grp" };
static const char * const i2c0_group[] = { "i2c0_grp" };
static const char * const i2c1_group[] = { "i2c1_grp" };
static const char * const i2c2_group[] = { "i2c2_grp" };
static const char * const i2c3_group[] = { "i2c3_grp" };
static const char * const i2c4_group[] = { "i2c4_grp" };
static const char * const uart0_group[] = { "uart0_grp" };
static const char * const uart1_group[] = { "uart1_grp" };
static const char * const uart2_group[] = { "uart2_grp" };
static const char * const uart3_group[] = { "uart3_grp" };
static const char * const uart4_group[] = { "uart4_grp" };
static const char * const uart5_group[] = { "uart5_grp" };
static const char * const uart6_group[] = { "uart6_grp" };
static const char * const uart7_group[] = { "uart7_grp" };
static const char * const uart8_group[] = { "uart8_grp" };
static const char * const uart9_group[] = { "uart9_grp" };
static const char * const uart10_group[] = { "uart10_grp" };
static const char * const uart11_group[] = { "uart11_grp" };
static const char * const uart12_group[] = { "uart12_grp" };
static const char * const uart13_group[] = { "uart13_grp" };
static const char * const uart14_group[] = { "uart14_grp" };
static const char * const uart15_group[] = { "uart15_grp" };
static const char * const gpio0_group[] = { "gpio0_grp" };
static const char * const gpio1_group[] = { "gpio1_grp" };
static const char * const gpio2_group[] = { "gpio2_grp" };
static const char * const gpio3_group[] = { "gpio3_grp" };
static const char * const gpio4_group[] = { "gpio4_grp" };
static const char * const gpio5_group[] = { "gpio5_grp" };
static const char * const gpio6_group[] = { "gpio6_grp" };
static const char * const gpio7_group[] = { "gpio7_grp" };
static const char * const gpio8_group[] = { "gpio8_grp" };
static const char * const gpio9_group[] = { "gpio9_grp" };
static const char * const gpio10_group[] = { "gpio10_grp" };
static const char * const gpio11_group[] = { "gpio11_grp" };
static const char * const gpio12_group[] = { "gpio12_grp" };
static const char * const gpio13_group[] = { "gpio13_grp" };
static const char * const gpio14_group[] = { "gpio14_grp" };
static const char * const gpio15_group[] = { "gpio15_grp" };
static const char * const gpio16_group[] = { "gpio16_grp" };
static const char * const gpio17_group[] = { "gpio17_grp" };
static const char * const gpio18_group[] = { "gpio18_grp" };
static const char * const gpio19_group[] = { "gpio19_grp" };
static const char * const gpio20_group[] = { "gpio20_grp" };
static const char * const gpio21_group[] = { "gpio21_grp" };
static const char * const gpio22_group[] = { "gpio22_grp" };
static const char * const gpio23_group[] = { "gpio23_grp" };
static const char * const gpio24_group[] = { "gpio24_grp" };
static const char * const gpio25_group[] = { "gpio25_grp" };
static const char * const gpio26_group[] = { "gpio26_grp" };
static const char * const gpio27_group[] = { "gpio27_grp" };
static const char * const gpio28_group[] = { "gpio28_grp" };
static const char * const gpio29_group[] = { "gpio29_grp" };
static const char * const gpio30_group[] = { "gpio30_grp" };
static const char * const gpio31_group[] = { "gpio31_grp" };
static const char * const gpio32_group[] = { "gpio32_grp" };
static const char * const gpio33_group[] = { "gpio33_grp" };
static const char * const gpio34_group[] = { "gpio34_grp" };
static const char * const gpio35_group[] = { "gpio35_grp" };
static const char * const gpio36_group[] = { "gpio36_grp" };
static const char * const gpio37_group[] = { "gpio37_grp" };
static const char * const gpio38_group[] = { "gpio38_grp" };
static const char * const gpio39_group[] = { "gpio39_grp" };
static const char * const gpio40_group[] = { "gpio40_grp" };
static const char * const gpio41_group[] = { "gpio41_grp" };
static const char * const gpio42_group[] = { "gpio42_grp" };
static const char * const gpio43_group[] = { "gpio43_grp" };
static const char * const gpio44_group[] = { "gpio44_grp" };
static const char * const gpio45_group[] = { "gpio45_grp" };
static const char * const gpio46_group[] = { "gpio46_grp" };
static const char * const gpio47_group[] = { "gpio47_grp" };
static const char * const gpio48_group[] = { "gpio48_grp" };
static const char * const gpio49_group[] = { "gpio49_grp" };
static const char * const gpio50_group[] = { "gpio50_grp" };
static const char * const gpio51_group[] = { "gpio51_grp" };
static const char * const gpio52_group[] = { "gpio52_grp" };
static const char * const gpio53_group[] = { "gpio53_grp" };
static const char * const gpio54_group[] = { "gpio54_grp" };
static const char * const gpio55_group[] = { "gpio55_grp" };
static const char * const gpio56_group[] = { "gpio56_grp" };
static const char * const gpio57_group[] = { "gpio57_grp" };
static const char * const gpio58_group[] = { "gpio58_grp" };
static const char * const gpio59_group[] = { "gpio59_grp" };
static const char * const gpio60_group[] = { "gpio60_grp" };
static const char * const gpio61_group[] = { "gpio61_grp" };
static const char * const gpio62_group[] = { "gpio62_grp" };
static const char * const gpio63_group[] = { "gpio63_grp" };
static const char * const gpio64_group[] = { "gpio64_grp" };
static const char * const gpio65_group[] = { "gpio65_grp" };
static const char * const gpio66_group[] = { "gpio66_grp" };
static const char * const gpio67_group[] = { "gpio67_grp" };
static const char * const eth1_group[] = { "eth1_grp" };
static const char * const i2s0_group[] = { "i2s0_grp" };
static const char * const i2s0_mclkin_group[] = { "i2s0_mclkin_grp" };
static const char * const i2s1_group[] = { "i2s1_grp" };
static const char * const i2s1_mclkin_group[] = { "i2s1_mclkin_grp" };
static const char * const spi0_group[] = { "spi0_grp" };

#define BM1880_PINMUX_FUNCTION(fname, mval)		\
	[F_##fname] = {					\
		.name = #fname,				\
		.groups = fname##_group,		\
		.ngroups = ARRAY_SIZE(fname##_group),	\
		.mux_val = mval,			\
	}

static const struct bm1880_pinmux_function bm1880_pmux_functions[] = {
	BM1880_PINMUX_FUNCTION(nand, 2),
	BM1880_PINMUX_FUNCTION(spi, 0),
	BM1880_PINMUX_FUNCTION(emmc, 1),
	BM1880_PINMUX_FUNCTION(sdio, 0),
	BM1880_PINMUX_FUNCTION(eth0, 0),
	BM1880_PINMUX_FUNCTION(pwm0, 2),
	BM1880_PINMUX_FUNCTION(pwm1, 2),
	BM1880_PINMUX_FUNCTION(pwm2, 2),
	BM1880_PINMUX_FUNCTION(pwm3, 2),
	BM1880_PINMUX_FUNCTION(pwm4, 2),
	BM1880_PINMUX_FUNCTION(pwm5, 2),
	BM1880_PINMUX_FUNCTION(pwm6, 2),
	BM1880_PINMUX_FUNCTION(pwm7, 2),
	BM1880_PINMUX_FUNCTION(pwm8, 2),
	BM1880_PINMUX_FUNCTION(pwm9, 2),
	BM1880_PINMUX_FUNCTION(pwm10, 2),
	BM1880_PINMUX_FUNCTION(pwm11, 2),
	BM1880_PINMUX_FUNCTION(pwm12, 2),
	BM1880_PINMUX_FUNCTION(pwm13, 2),
	BM1880_PINMUX_FUNCTION(pwm14, 2),
	BM1880_PINMUX_FUNCTION(pwm15, 2),
	BM1880_PINMUX_FUNCTION(pwm16, 2),
	BM1880_PINMUX_FUNCTION(pwm17, 2),
	BM1880_PINMUX_FUNCTION(pwm18, 2),
	BM1880_PINMUX_FUNCTION(pwm19, 2),
	BM1880_PINMUX_FUNCTION(pwm20, 2),
	BM1880_PINMUX_FUNCTION(pwm21, 2),
	BM1880_PINMUX_FUNCTION(pwm22, 2),
	BM1880_PINMUX_FUNCTION(pwm23, 2),
	BM1880_PINMUX_FUNCTION(pwm24, 2),
	BM1880_PINMUX_FUNCTION(pwm25, 2),
	BM1880_PINMUX_FUNCTION(pwm26, 2),
	BM1880_PINMUX_FUNCTION(pwm27, 2),
	BM1880_PINMUX_FUNCTION(pwm28, 2),
	BM1880_PINMUX_FUNCTION(pwm29, 2),
	BM1880_PINMUX_FUNCTION(pwm30, 2),
	BM1880_PINMUX_FUNCTION(pwm31, 2),
	BM1880_PINMUX_FUNCTION(pwm32, 2),
	BM1880_PINMUX_FUNCTION(pwm33, 2),
	BM1880_PINMUX_FUNCTION(pwm34, 2),
	BM1880_PINMUX_FUNCTION(pwm35, 2),
	BM1880_PINMUX_FUNCTION(pwm36, 2),
	BM1880_PINMUX_FUNCTION(pwm37, 2),
	BM1880_PINMUX_FUNCTION(i2c0, 1),
	BM1880_PINMUX_FUNCTION(i2c1, 1),
	BM1880_PINMUX_FUNCTION(i2c2, 1),
	BM1880_PINMUX_FUNCTION(i2c3, 1),
	BM1880_PINMUX_FUNCTION(i2c4, 1),
	BM1880_PINMUX_FUNCTION(uart0, 3),
	BM1880_PINMUX_FUNCTION(uart1, 3),
	BM1880_PINMUX_FUNCTION(uart2, 3),
	BM1880_PINMUX_FUNCTION(uart3, 3),
	BM1880_PINMUX_FUNCTION(uart4, 1),
	BM1880_PINMUX_FUNCTION(uart5, 1),
	BM1880_PINMUX_FUNCTION(uart6, 1),
	BM1880_PINMUX_FUNCTION(uart7, 1),
	BM1880_PINMUX_FUNCTION(uart8, 1),
	BM1880_PINMUX_FUNCTION(uart9, 1),
	BM1880_PINMUX_FUNCTION(uart10, 1),
	BM1880_PINMUX_FUNCTION(uart11, 1),
	BM1880_PINMUX_FUNCTION(uart12, 3),
	BM1880_PINMUX_FUNCTION(uart13, 3),
	BM1880_PINMUX_FUNCTION(uart14, 3),
	BM1880_PINMUX_FUNCTION(uart15, 3),
	BM1880_PINMUX_FUNCTION(gpio0, 0),
	BM1880_PINMUX_FUNCTION(gpio1, 0),
	BM1880_PINMUX_FUNCTION(gpio2, 0),
	BM1880_PINMUX_FUNCTION(gpio3, 0),
	BM1880_PINMUX_FUNCTION(gpio4, 0),
	BM1880_PINMUX_FUNCTION(gpio5, 0),
	BM1880_PINMUX_FUNCTION(gpio6, 0),
	BM1880_PINMUX_FUNCTION(gpio7, 0),
	BM1880_PINMUX_FUNCTION(gpio8, 0),
	BM1880_PINMUX_FUNCTION(gpio9, 0),
	BM1880_PINMUX_FUNCTION(gpio10, 0),
	BM1880_PINMUX_FUNCTION(gpio11, 0),
	BM1880_PINMUX_FUNCTION(gpio12, 1),
	BM1880_PINMUX_FUNCTION(gpio13, 1),
	BM1880_PINMUX_FUNCTION(gpio14, 0),
	BM1880_PINMUX_FUNCTION(gpio15, 0),
	BM1880_PINMUX_FUNCTION(gpio16, 0),
	BM1880_PINMUX_FUNCTION(gpio17, 0),
	BM1880_PINMUX_FUNCTION(gpio18, 0),
	BM1880_PINMUX_FUNCTION(gpio19, 0),
	BM1880_PINMUX_FUNCTION(gpio20, 0),
	BM1880_PINMUX_FUNCTION(gpio21, 0),
	BM1880_PINMUX_FUNCTION(gpio22, 0),
	BM1880_PINMUX_FUNCTION(gpio23, 0),
	BM1880_PINMUX_FUNCTION(gpio24, 0),
	BM1880_PINMUX_FUNCTION(gpio25, 0),
	BM1880_PINMUX_FUNCTION(gpio26, 0),
	BM1880_PINMUX_FUNCTION(gpio27, 0),
	BM1880_PINMUX_FUNCTION(gpio28, 0),
	BM1880_PINMUX_FUNCTION(gpio29, 0),
	BM1880_PINMUX_FUNCTION(gpio30, 0),
	BM1880_PINMUX_FUNCTION(gpio31, 0),
	BM1880_PINMUX_FUNCTION(gpio32, 0),
	BM1880_PINMUX_FUNCTION(gpio33, 0),
	BM1880_PINMUX_FUNCTION(gpio34, 0),
	BM1880_PINMUX_FUNCTION(gpio35, 0),
	BM1880_PINMUX_FUNCTION(gpio36, 0),
	BM1880_PINMUX_FUNCTION(gpio37, 0),
	BM1880_PINMUX_FUNCTION(gpio38, 0),
	BM1880_PINMUX_FUNCTION(gpio39, 0),
	BM1880_PINMUX_FUNCTION(gpio40, 0),
	BM1880_PINMUX_FUNCTION(gpio41, 0),
	BM1880_PINMUX_FUNCTION(gpio42, 0),
	BM1880_PINMUX_FUNCTION(gpio43, 0),
	BM1880_PINMUX_FUNCTION(gpio44, 0),
	BM1880_PINMUX_FUNCTION(gpio45, 0),
	BM1880_PINMUX_FUNCTION(gpio46, 0),
	BM1880_PINMUX_FUNCTION(gpio47, 0),
	BM1880_PINMUX_FUNCTION(gpio48, 0),
	BM1880_PINMUX_FUNCTION(gpio49, 0),
	BM1880_PINMUX_FUNCTION(gpio50, 0),
	BM1880_PINMUX_FUNCTION(gpio51, 0),
	BM1880_PINMUX_FUNCTION(gpio52, 0),
	BM1880_PINMUX_FUNCTION(gpio53, 0),
	BM1880_PINMUX_FUNCTION(gpio54, 0),
	BM1880_PINMUX_FUNCTION(gpio55, 0),
	BM1880_PINMUX_FUNCTION(gpio56, 0),
	BM1880_PINMUX_FUNCTION(gpio57, 0),
	BM1880_PINMUX_FUNCTION(gpio58, 0),
	BM1880_PINMUX_FUNCTION(gpio59, 0),
	BM1880_PINMUX_FUNCTION(gpio60, 0),
	BM1880_PINMUX_FUNCTION(gpio61, 0),
	BM1880_PINMUX_FUNCTION(gpio62, 0),
	BM1880_PINMUX_FUNCTION(gpio63, 0),
	BM1880_PINMUX_FUNCTION(gpio64, 0),
	BM1880_PINMUX_FUNCTION(gpio65, 0),
	BM1880_PINMUX_FUNCTION(gpio66, 0),
	BM1880_PINMUX_FUNCTION(gpio67, 0),
	BM1880_PINMUX_FUNCTION(eth1, 1),
	BM1880_PINMUX_FUNCTION(i2s0, 2),
	BM1880_PINMUX_FUNCTION(i2s0_mclkin, 1),
	BM1880_PINMUX_FUNCTION(i2s1, 2),
	BM1880_PINMUX_FUNCTION(i2s1_mclkin, 1),
	BM1880_PINMUX_FUNCTION(spi0, 1),
};

#define BM1880_PINCONF_DAT(_width)		\
	{					\
		.drv_bits = _width,		\
	}

static const struct bm1880_pinconf_data bm1880_pinconf[] = {
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x03),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
	BM1880_PINCONF_DAT(0x02),
};

static int bm1880_pctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct bm1880_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->ngroups;
}

static const char *bm1880_pctrl_get_group_name(struct pinctrl_dev *pctldev,
					       unsigned int selector)
{
	struct bm1880_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->groups[selector].name;
}

static int bm1880_pctrl_get_group_pins(struct pinctrl_dev *pctldev,
				       unsigned int selector,
				       const unsigned int **pins,
				       unsigned int *num_pins)
{
	struct bm1880_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pctrl->groups[selector].pins;
	*num_pins = pctrl->groups[selector].npins;

	return 0;
}

static const struct pinctrl_ops bm1880_pctrl_ops = {
	.get_groups_count = bm1880_pctrl_get_groups_count,
	.get_group_name = bm1880_pctrl_get_group_name,
	.get_group_pins = bm1880_pctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
};

/* pinmux */
static int bm1880_pmux_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct bm1880_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->nfuncs;
}

static const char *bm1880_pmux_get_function_name(struct pinctrl_dev *pctldev,
						 unsigned int selector)
{
	struct bm1880_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->funcs[selector].name;
}

static int bm1880_pmux_get_function_groups(struct pinctrl_dev *pctldev,
					   unsigned int selector,
					   const char * const **groups,
					   unsigned * const num_groups)
{
	struct bm1880_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctrl->funcs[selector].groups;
	*num_groups = pctrl->funcs[selector].ngroups;
	return 0;
}

static int bm1880_pinmux_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int function,
				 unsigned int  group)
{
	struct bm1880_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct bm1880_pctrl_group *pgrp = &pctrl->groups[group];
	const struct bm1880_pinmux_function *func = &pctrl->funcs[function];
	int i;

	for (i = 0; i < pgrp->npins; i++) {
		unsigned int pin = pgrp->pins[i];
		u32 offset = (pin >> 1) << 2;
		u32 mux_offset = ((!((pin + 1) & 1) << 4) + 4);
		u32 regval = readl_relaxed(pctrl->base + BM1880_REG_MUX +
					   offset);

		regval &= ~(0x03 << mux_offset);
		regval |= func->mux_val << mux_offset;

		writel_relaxed(regval, pctrl->base + BM1880_REG_MUX + offset);
	}

	return 0;
}

#define BM1880_PINCONF(pin, idx) ((!((pin + 1) & 1) << 4) + idx)
#define BM1880_PINCONF_PULLCTRL(pin)	BM1880_PINCONF(pin, 0)
#define BM1880_PINCONF_PULLUP(pin)	BM1880_PINCONF(pin, 1)
#define BM1880_PINCONF_PULLDOWN(pin)	BM1880_PINCONF(pin, 2)
#define BM1880_PINCONF_DRV(pin)		BM1880_PINCONF(pin, 6)
#define BM1880_PINCONF_SCHMITT(pin)	BM1880_PINCONF(pin, 9)
#define BM1880_PINCONF_SLEW(pin)	BM1880_PINCONF(pin, 10)

static int bm1880_pinconf_drv_set(unsigned int mA, u32 width,
				  u32 *regval, u32 bit_offset)
{
	u32 _regval;

	_regval = *regval;

	/*
	 * There are two sets of drive strength bit width exposed by the
	 * SoC at 4mA step, hence we need to handle them separately.
	 */
	if (width == 0x03) {
		switch (mA) {
		case 4:
			_regval &= ~(width << bit_offset);
			_regval |= (0 << bit_offset);
			break;
		case 8:
			_regval &= ~(width << bit_offset);
			_regval |= (1 << bit_offset);
			break;
		case 12:
			_regval &= ~(width << bit_offset);
			_regval |= (2 << bit_offset);
			break;
		case 16:
			_regval &= ~(width << bit_offset);
			_regval |= (3 << bit_offset);
			break;
		case 20:
			_regval &= ~(width << bit_offset);
			_regval |= (4 << bit_offset);
			break;
		case 24:
			_regval &= ~(width << bit_offset);
			_regval |= (5 << bit_offset);
			break;
		case 28:
			_regval &= ~(width << bit_offset);
			_regval |= (6 << bit_offset);
			break;
		case 32:
			_regval &= ~(width << bit_offset);
			_regval |= (7 << bit_offset);
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (mA) {
		case 4:
			_regval &= ~(width << bit_offset);
			_regval |= (0 << bit_offset);
			break;
		case 8:
			_regval &= ~(width << bit_offset);
			_regval |= (1 << bit_offset);
			break;
		case 12:
			_regval &= ~(width << bit_offset);
			_regval |= (2 << bit_offset);
			break;
		case 16:
			_regval &= ~(width << bit_offset);
			_regval |= (3 << bit_offset);
			break;
		default:
			return -EINVAL;
		}
	}

	*regval = _regval;

	return 0;
}

static int bm1880_pinconf_drv_get(u32 width, u32 drv)
{
	int ret = -ENOTSUPP;

	/*
	 * There are two sets of drive strength bit width exposed by the
	 * SoC at 4mA step, hence we need to handle them separately.
	 */
	if (width == 0x03) {
		switch (drv) {
		case 0:
			ret  = 4;
			break;
		case 1:
			ret  = 8;
			break;
		case 2:
			ret  = 12;
			break;
		case 3:
			ret  = 16;
			break;
		case 4:
			ret  = 20;
			break;
		case 5:
			ret  = 24;
			break;
		case 6:
			ret  = 28;
			break;
		case 7:
			ret  = 32;
			break;
		default:
			break;
		}
	} else {
		switch (drv) {
		case 0:
			ret  = 4;
			break;
		case 1:
			ret  = 8;
			break;
		case 2:
			ret  = 12;
			break;
		case 3:
			ret  = 16;
			break;
		default:
			break;
		}
	}

	return ret;
}

static int bm1880_pinconf_cfg_get(struct pinctrl_dev *pctldev,
				  unsigned int pin,
				  unsigned long *config)
{
	struct bm1880_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*config);
	unsigned int arg = 0;
	u32 regval, offset, bit_offset;
	int ret;

	offset = (pin >> 1) << 2;
	regval = readl_relaxed(pctrl->base + BM1880_REG_MUX + offset);

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		bit_offset = BM1880_PINCONF_PULLUP(pin);
		arg = !!(regval & BIT(bit_offset));
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		bit_offset = BM1880_PINCONF_PULLDOWN(pin);
		arg = !!(regval & BIT(bit_offset));
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		bit_offset = BM1880_PINCONF_PULLCTRL(pin);
		arg = !!(regval & BIT(bit_offset));
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		bit_offset = BM1880_PINCONF_SCHMITT(pin);
		arg = !!(regval & BIT(bit_offset));
		break;
	case PIN_CONFIG_SLEW_RATE:
		bit_offset = BM1880_PINCONF_SLEW(pin);
		arg = !!(regval & BIT(bit_offset));
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		bit_offset = BM1880_PINCONF_DRV(pin);
		ret = bm1880_pinconf_drv_get(pctrl->pinconf[pin].drv_bits,
					     !!(regval & BIT(bit_offset)));
		if (ret < 0)
			return ret;

		arg = ret;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int bm1880_pinconf_cfg_set(struct pinctrl_dev *pctldev,
				  unsigned int pin,
				  unsigned long *configs,
				  unsigned int num_configs)
{
	struct bm1880_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	u32 regval, offset, bit_offset;
	int i, ret;

	offset = (pin >> 1) << 2;
	regval = readl_relaxed(pctrl->base + BM1880_REG_MUX + offset);

	for (i = 0; i < num_configs; i++) {
		unsigned int param = pinconf_to_config_param(configs[i]);
		unsigned int arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			bit_offset = BM1880_PINCONF_PULLUP(pin);
			regval |= BIT(bit_offset);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			bit_offset = BM1880_PINCONF_PULLDOWN(pin);
			regval |= BIT(bit_offset);
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			bit_offset = BM1880_PINCONF_PULLCTRL(pin);
			regval |= BIT(bit_offset);
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			bit_offset = BM1880_PINCONF_SCHMITT(pin);
			if (arg)
				regval |= BIT(bit_offset);
			else
				regval &= ~BIT(bit_offset);
			break;
		case PIN_CONFIG_SLEW_RATE:
			bit_offset = BM1880_PINCONF_SLEW(pin);
			if (arg)
				regval |= BIT(bit_offset);
			else
				regval &= ~BIT(bit_offset);
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			bit_offset = BM1880_PINCONF_DRV(pin);
			ret = bm1880_pinconf_drv_set(arg,
						pctrl->pinconf[pin].drv_bits,
						&regval, bit_offset);
			if (ret < 0)
				return ret;

			break;
		default:
			dev_warn(pctldev->dev,
				 "unsupported configuration parameter '%u'\n",
				 param);
			continue;
		}

		writel_relaxed(regval, pctrl->base + BM1880_REG_MUX + offset);
	}

	return 0;
}

static int bm1880_pinconf_group_set(struct pinctrl_dev *pctldev,
				    unsigned int selector,
				    unsigned long *configs,
				    unsigned int  num_configs)
{
	int i, ret;
	struct bm1880_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct bm1880_pctrl_group *pgrp = &pctrl->groups[selector];

	for (i = 0; i < pgrp->npins; i++) {
		ret = bm1880_pinconf_cfg_set(pctldev, pgrp->pins[i], configs,
					     num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops bm1880_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = bm1880_pinconf_cfg_get,
	.pin_config_set = bm1880_pinconf_cfg_set,
	.pin_config_group_set = bm1880_pinconf_group_set,
};

static const struct pinmux_ops bm1880_pinmux_ops = {
	.get_functions_count = bm1880_pmux_get_functions_count,
	.get_function_name = bm1880_pmux_get_function_name,
	.get_function_groups = bm1880_pmux_get_function_groups,
	.set_mux = bm1880_pinmux_set_mux,
};

static struct pinctrl_desc bm1880_desc = {
	.name = "bm1880_pinctrl",
	.pins = bm1880_pins,
	.npins = ARRAY_SIZE(bm1880_pins),
	.pctlops = &bm1880_pctrl_ops,
	.pmxops = &bm1880_pinmux_ops,
	.confops = &bm1880_pinconf_ops,
	.owner = THIS_MODULE,
};

static int bm1880_pinctrl_probe(struct platform_device *pdev)

{
	struct bm1880_pinctrl *pctrl;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->base))
		return PTR_ERR(pctrl->base);

	pctrl->groups = bm1880_pctrl_groups;
	pctrl->ngroups = ARRAY_SIZE(bm1880_pctrl_groups);
	pctrl->funcs = bm1880_pmux_functions;
	pctrl->nfuncs = ARRAY_SIZE(bm1880_pmux_functions);
	pctrl->pinconf = bm1880_pinconf;

	pctrl->pctrldev = devm_pinctrl_register(&pdev->dev, &bm1880_desc,
						pctrl);
	if (IS_ERR(pctrl->pctrldev))
		return PTR_ERR(pctrl->pctrldev);

	platform_set_drvdata(pdev, pctrl);

	dev_info(&pdev->dev, "BM1880 pinctrl driver initialized\n");

	return 0;
}

static const struct of_device_id bm1880_pinctrl_of_match[] = {
	{ .compatible = "bitmain,bm1880-pinctrl" },
	{ }
};

static struct platform_driver bm1880_pinctrl_driver = {
	.driver = {
		.name = "pinctrl-bm1880",
		.of_match_table = of_match_ptr(bm1880_pinctrl_of_match),
	},
	.probe = bm1880_pinctrl_probe,
};

static int __init bm1880_pinctrl_init(void)
{
	return platform_driver_register(&bm1880_pinctrl_driver);
}
arch_initcall(bm1880_pinctrl_init);
