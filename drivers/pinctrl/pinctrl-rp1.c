// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Raspberry Pi RP1 GPIO unit
 *
 * Copyright (C) 2023 Raspberry Pi Ltd.
 *
 * This driver is inspired by:
 * pinctrl-bcm2835.c, please see original file for copyright information
 */

#include <linux/gpio/driver.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/regmap.h>

#include "pinmux.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

#define MODULE_NAME "pinctrl-rp1"
#define RP1_NUM_GPIOS	54
#define RP1_NUM_BANKS	3

#define RP1_INT_EDGE_FALLING		BIT(0)
#define RP1_INT_EDGE_RISING		BIT(1)
#define RP1_INT_LEVEL_LOW		BIT(2)
#define RP1_INT_LEVEL_HIGH		BIT(3)
#define RP1_INT_MASK			GENMASK(3, 0)
#define RP1_INT_EDGE_BOTH		(RP1_INT_EDGE_FALLING |	\
					 RP1_INT_EDGE_RISING)

#define RP1_FSEL_COUNT			9

#define RP1_FSEL_ALT0			0x00
#define RP1_FSEL_GPIO			0x05
#define RP1_FSEL_NONE			0x09
#define RP1_FSEL_NONE_HW		0x1f

#define RP1_PAD_DRIVE_2MA		0x0
#define RP1_PAD_DRIVE_4MA		0x1
#define RP1_PAD_DRIVE_8MA		0x2
#define RP1_PAD_DRIVE_12MA		0x3

enum {
	RP1_PUD_OFF			= 0,
	RP1_PUD_DOWN			= 1,
	RP1_PUD_UP			= 2,
};

enum {
	RP1_DIR_OUTPUT			= 0,
	RP1_DIR_INPUT			= 1,
};

enum {
	RP1_OUTOVER_PERI		= 0,
	RP1_OUTOVER_INVPERI		= 1,
	RP1_OUTOVER_LOW			= 2,
	RP1_OUTOVER_HIGH		= 3,
};

enum {
	RP1_OEOVER_PERI			= 0,
	RP1_OEOVER_INVPERI		= 1,
	RP1_OEOVER_DISABLE		= 2,
	RP1_OEOVER_ENABLE		= 3,
};

enum {
	RP1_INOVER_PERI			= 0,
	RP1_INOVER_INVPERI		= 1,
	RP1_INOVER_LOW			= 2,
	RP1_INOVER_HIGH			= 3,
};

enum {
	RP1_GPIO_CTRL_IRQRESET_SET		= 0,
	RP1_GPIO_CTRL_INT_CLR			= 1,
	RP1_GPIO_CTRL_INT_SET			= 2,
	RP1_GPIO_CTRL_OEOVER			= 3,
	RP1_GPIO_CTRL_FUNCSEL			= 4,
	RP1_GPIO_CTRL_OUTOVER			= 5,
	RP1_GPIO_CTRL				= 6,
};

enum {
	RP1_INTE_SET			= 0,
	RP1_INTE_CLR			= 1,
};

enum {
	RP1_RIO_OUT_SET			= 0,
	RP1_RIO_OUT_CLR			= 1,
	RP1_RIO_OE			= 2,
	RP1_RIO_OE_SET			= 3,
	RP1_RIO_OE_CLR			= 4,
	RP1_RIO_IN			= 5,
};

enum {
	RP1_PAD_SLEWFAST		= 0,
	RP1_PAD_SCHMITT			= 1,
	RP1_PAD_PULL			= 2,
	RP1_PAD_DRIVE			= 3,
	RP1_PAD_IN_ENABLE		= 4,
	RP1_PAD_OUT_DISABLE		= 5,
};

static const struct reg_field rp1_gpio_fields[] = {
	[RP1_GPIO_CTRL_IRQRESET_SET]	= REG_FIELD(0x2004, 28, 28),
	[RP1_GPIO_CTRL_INT_CLR]		= REG_FIELD(0x3004, 20, 23),
	[RP1_GPIO_CTRL_INT_SET]		= REG_FIELD(0x2004, 20, 23),
	[RP1_GPIO_CTRL_OEOVER]		= REG_FIELD(0x0004, 14, 15),
	[RP1_GPIO_CTRL_FUNCSEL]		= REG_FIELD(0x0004, 0, 4),
	[RP1_GPIO_CTRL_OUTOVER]		= REG_FIELD(0x0004, 12, 13),
	[RP1_GPIO_CTRL]			= REG_FIELD(0x0004, 0, 31),
};

static const struct reg_field rp1_inte_fields[] = {
	[RP1_INTE_SET]			= REG_FIELD(0x2000, 0, 0),
	[RP1_INTE_CLR]			= REG_FIELD(0x3000, 0, 0),
};

static const struct reg_field rp1_rio_fields[] = {
	[RP1_RIO_OUT_SET]		= REG_FIELD(0x2000, 0, 0),
	[RP1_RIO_OUT_CLR]		= REG_FIELD(0x3000, 0, 0),
	[RP1_RIO_OE]			= REG_FIELD(0x0004, 0, 0),
	[RP1_RIO_OE_SET]		= REG_FIELD(0x2004, 0, 0),
	[RP1_RIO_OE_CLR]		= REG_FIELD(0x3004, 0, 0),
	[RP1_RIO_IN]			= REG_FIELD(0x0008, 0, 0),
};

static const struct reg_field rp1_pad_fields[] = {
	[RP1_PAD_SLEWFAST]		= REG_FIELD(0, 0, 0),
	[RP1_PAD_SCHMITT]		= REG_FIELD(0, 1, 1),
	[RP1_PAD_PULL]			= REG_FIELD(0, 2, 3),
	[RP1_PAD_DRIVE]			= REG_FIELD(0, 4, 5),
	[RP1_PAD_IN_ENABLE]		= REG_FIELD(0, 6, 6),
	[RP1_PAD_OUT_DISABLE]		= REG_FIELD(0, 7, 7),
};

#define FUNC(f) \
	[func_##f] = #f
#define RP1_MAX_FSEL 8
#define PIN(i, f0, f1, f2, f3, f4, f5, f6, f7, f8) \
	[i] = { \
		.funcs = { \
			func_##f0, \
			func_##f1, \
			func_##f2, \
			func_##f3, \
			func_##f4, \
			func_##f5, \
			func_##f6, \
			func_##f7, \
			func_##f8, \
		}, \
	}

#define LEGACY_MAP(n, f0, f1, f2, f3, f4, f5) \
	[n] = { \
		func_gpio, \
		func_gpio, \
		func_##f5, \
		func_##f4, \
		func_##f0, \
		func_##f1, \
		func_##f2, \
		func_##f3, \
	}

enum funcs {
	func_alt0,
	func_alt1,
	func_alt2,
	func_alt3,
	func_alt4,
	func_gpio,
	func_alt6,
	func_alt7,
	func_alt8,
	func_none,
	func_aaud,
	func_dpi,
	func_dsi0_te_ext,
	func_dsi1_te_ext,
	func_gpclk0,
	func_gpclk1,
	func_gpclk2,
	func_gpclk3,
	func_gpclk4,
	func_gpclk5,
	func_i2c0,
	func_i2c1,
	func_i2c2,
	func_i2c3,
	func_i2c4,
	func_i2c5,
	func_i2c6,
	func_i2s0,
	func_i2s1,
	func_i2s2,
	func_ir,
	func_mic,
	func_pcie_clkreq_n,
	func_pio,
	func_proc_rio,
	func_pwm0,
	func_pwm1,
	func_sd0,
	func_sd1,
	func_spi0,
	func_spi1,
	func_spi2,
	func_spi3,
	func_spi4,
	func_spi5,
	func_spi6,
	func_spi7,
	func_spi8,
	func_uart0,
	func_uart1,
	func_uart2,
	func_uart3,
	func_uart4,
	func_uart5,
	func_vbus0,
	func_vbus1,
	func_vbus2,
	func_vbus3,
	func__,
	func_count = func__,
	func_invalid = func__,
};

struct rp1_pin_funcs {
	u8 funcs[RP1_FSEL_COUNT];
};

struct rp1_iobank_desc {
	int min_gpio;
	int num_gpios;
	int gpio_offset;
	int inte_offset;
	int ints_offset;
	int rio_offset;
	int pads_offset;
};

struct rp1_pin_info {
	u8 num;
	u8 bank;
	u8 offset;
	u8 fsel;
	u8 irq_type;

	struct regmap_field *gpio[ARRAY_SIZE(rp1_gpio_fields)];
	struct regmap_field *rio[ARRAY_SIZE(rp1_rio_fields)];
	struct regmap_field *inte[ARRAY_SIZE(rp1_inte_fields)];
	struct regmap_field *pad[ARRAY_SIZE(rp1_pad_fields)];
};

struct rp1_pinctrl {
	struct device *dev;
	void __iomem *gpio_base;
	void __iomem *rio_base;
	void __iomem *pads_base;
	int irq[RP1_NUM_BANKS];
	struct rp1_pin_info pins[RP1_NUM_GPIOS];

	struct pinctrl_dev *pctl_dev;
	struct gpio_chip gpio_chip;
	struct pinctrl_gpio_range gpio_range;

	raw_spinlock_t irq_lock[RP1_NUM_BANKS];
};

/* pins are just named GPIO0..GPIO53 */
#define RP1_GPIO_PIN(a) PINCTRL_PIN(a, "gpio" #a)
static struct pinctrl_pin_desc rp1_gpio_pins[] = {
	RP1_GPIO_PIN(0),
	RP1_GPIO_PIN(1),
	RP1_GPIO_PIN(2),
	RP1_GPIO_PIN(3),
	RP1_GPIO_PIN(4),
	RP1_GPIO_PIN(5),
	RP1_GPIO_PIN(6),
	RP1_GPIO_PIN(7),
	RP1_GPIO_PIN(8),
	RP1_GPIO_PIN(9),
	RP1_GPIO_PIN(10),
	RP1_GPIO_PIN(11),
	RP1_GPIO_PIN(12),
	RP1_GPIO_PIN(13),
	RP1_GPIO_PIN(14),
	RP1_GPIO_PIN(15),
	RP1_GPIO_PIN(16),
	RP1_GPIO_PIN(17),
	RP1_GPIO_PIN(18),
	RP1_GPIO_PIN(19),
	RP1_GPIO_PIN(20),
	RP1_GPIO_PIN(21),
	RP1_GPIO_PIN(22),
	RP1_GPIO_PIN(23),
	RP1_GPIO_PIN(24),
	RP1_GPIO_PIN(25),
	RP1_GPIO_PIN(26),
	RP1_GPIO_PIN(27),
	RP1_GPIO_PIN(28),
	RP1_GPIO_PIN(29),
	RP1_GPIO_PIN(30),
	RP1_GPIO_PIN(31),
	RP1_GPIO_PIN(32),
	RP1_GPIO_PIN(33),
	RP1_GPIO_PIN(34),
	RP1_GPIO_PIN(35),
	RP1_GPIO_PIN(36),
	RP1_GPIO_PIN(37),
	RP1_GPIO_PIN(38),
	RP1_GPIO_PIN(39),
	RP1_GPIO_PIN(40),
	RP1_GPIO_PIN(41),
	RP1_GPIO_PIN(42),
	RP1_GPIO_PIN(43),
	RP1_GPIO_PIN(44),
	RP1_GPIO_PIN(45),
	RP1_GPIO_PIN(46),
	RP1_GPIO_PIN(47),
	RP1_GPIO_PIN(48),
	RP1_GPIO_PIN(49),
	RP1_GPIO_PIN(50),
	RP1_GPIO_PIN(51),
	RP1_GPIO_PIN(52),
	RP1_GPIO_PIN(53),
};

#define PIN_ARRAY(...) \
	(const unsigned int []) {__VA_ARGS__}
#define PIN_ARRAY_SIZE(...) \
	(sizeof((unsigned int[]) {__VA_ARGS__}) / sizeof(unsigned int))
#define RP1_GROUP(name, ...) \
	PINCTRL_PINGROUP(#name, PIN_ARRAY(__VA_ARGS__), \
			 PIN_ARRAY_SIZE(__VA_ARGS__))

static const struct pingroup rp1_gpio_groups[] = {
	RP1_GROUP(uart0, 14, 15),
	RP1_GROUP(uart0_ctrl, 4, 5, 6, 7, 16, 17),
	RP1_GROUP(uart1, 0, 1),
	RP1_GROUP(uart1_ctrl, 2, 3),
	RP1_GROUP(uart2, 4, 5),
	RP1_GROUP(uart2_ctrl, 6, 7),
	RP1_GROUP(uart3, 8, 9),
	RP1_GROUP(uart3_ctrl, 10, 11),
	RP1_GROUP(uart4, 12, 13),
	RP1_GROUP(uart4_ctrl, 14, 15),
	RP1_GROUP(uart5_0, 30, 31),
	RP1_GROUP(uart5_0_ctrl, 32, 33),
	RP1_GROUP(uart5_1, 36, 37),
	RP1_GROUP(uart5_1_ctrl, 38, 39),
	RP1_GROUP(uart5_2, 40, 41),
	RP1_GROUP(uart5_2_ctrl, 42, 43),
	RP1_GROUP(uart5_3, 48, 49),
	RP1_GROUP(sd0, 22, 23, 24, 25, 26, 27),
	RP1_GROUP(sd1, 28, 29, 30, 31, 32, 33),
	RP1_GROUP(i2s0, 18, 19, 20, 21),
	RP1_GROUP(i2s0_dual, 18, 19, 20, 21, 22, 23),
	RP1_GROUP(i2s0_quad, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
	RP1_GROUP(i2s1, 18, 19, 20, 21),
	RP1_GROUP(i2s1_dual, 18, 19, 20, 21, 22, 23),
	RP1_GROUP(i2s1_quad, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
	RP1_GROUP(i2s2_0, 28, 29, 30, 31),
	RP1_GROUP(i2s2_0_dual, 28, 29, 30, 31, 32, 33),
	RP1_GROUP(i2s2_1, 42, 43, 44, 45),
	RP1_GROUP(i2s2_1_dual, 42, 43, 44, 45, 46, 47),
	RP1_GROUP(i2c4_0, 28, 29),
	RP1_GROUP(i2c4_1, 34, 35),
	RP1_GROUP(i2c4_2, 40, 41),
	RP1_GROUP(i2c4_3, 46, 47),
	RP1_GROUP(i2c6_0, 38, 39),
	RP1_GROUP(i2c6_1, 51, 52),
	RP1_GROUP(i2c5_0, 30, 31),
	RP1_GROUP(i2c5_1, 36, 37),
	RP1_GROUP(i2c5_2, 44, 45),
	RP1_GROUP(i2c5_3, 49, 50),
	RP1_GROUP(i2c0_0, 0, 1),
	RP1_GROUP(i2c0_1, 8, 9),
	RP1_GROUP(i2c1_0, 2, 3),
	RP1_GROUP(i2c1_1, 10, 11),
	RP1_GROUP(i2c2_0, 4, 5),
	RP1_GROUP(i2c2_1, 12, 13),
	RP1_GROUP(i2c3_0, 6, 7),
	RP1_GROUP(i2c3_1, 14, 15),
	RP1_GROUP(i2c3_2, 22, 23),
	RP1_GROUP(dpi_16bit, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
		  11, 12, 13, 14, 15, 16, 17, 18, 19),
	RP1_GROUP(dpi_16bit_cpadhi, 0, 1, 2, 3, 4, 5, 6, 7, 8,
		  12, 13, 14, 15, 16, 17, 20, 21, 22, 23, 24),
	RP1_GROUP(dpi_16bit_pad666, 0, 1, 2, 3, 5, 6, 7, 8, 9,
		  12, 13, 14, 15, 16, 17, 21, 22, 23, 24, 25),
	RP1_GROUP(dpi_18bit, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
		  11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
	RP1_GROUP(dpi_18bit_cpadhi, 0, 1, 2, 3, 4, 5, 6, 7, 8,
		  9, 12, 13, 14, 15, 16, 17, 20, 21, 22, 23, 24,
		  25),
	RP1_GROUP(dpi_24bit, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
		  11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
		  22, 23, 24, 25, 26, 27),
	RP1_GROUP(spi0, 9, 10, 11),
	RP1_GROUP(spi0_quad, 0, 1, 9, 10, 11),
	RP1_GROUP(spi1, 19, 20, 21),
	RP1_GROUP(spi2, 1, 2, 3),
	RP1_GROUP(spi3, 5, 6, 7),
	RP1_GROUP(spi4, 9, 10, 11),
	RP1_GROUP(spi5, 13, 14, 15),
	RP1_GROUP(spi6_0, 28, 29, 30),
	RP1_GROUP(spi6_1, 40, 41, 42),
	RP1_GROUP(spi7_0, 46, 47, 48),
	RP1_GROUP(spi7_1, 49, 50, 51),
	RP1_GROUP(spi8_0, 37, 38, 39),
	RP1_GROUP(spi8_1, 49, 50, 51),
	RP1_GROUP(aaud_0, 12, 13),
	RP1_GROUP(aaud_1, 38, 39),
	RP1_GROUP(aaud_2, 40, 41),
	RP1_GROUP(aaud_3, 49, 50),
	RP1_GROUP(aaud_4, 51, 52),
	RP1_GROUP(vbus0_0, 28, 29),
	RP1_GROUP(vbus0_1, 34, 35),
	RP1_GROUP(vbus1, 42, 43),
	RP1_GROUP(vbus2, 50, 51),
	RP1_GROUP(vbus3, 52, 53),
	RP1_GROUP(mic_0, 25, 26, 27),
	RP1_GROUP(mic_1, 34, 35, 36),
	RP1_GROUP(mic_2, 37, 38, 39),
	RP1_GROUP(mic_3, 46, 47, 48),
	RP1_GROUP(ir, 2, 3),
};

#define GRP_ARRAY(...) \
	(const char * []) {__VA_ARGS__}
#define GRP_ARRAY_SIZE(...) \
	(sizeof((char *[]) {__VA_ARGS__}) / sizeof(char *))
#define RP1_FNC(f, ...) \
	[func_##f] = PINCTRL_PINFUNCTION(#f, GRP_ARRAY(__VA_ARGS__), \
					 GRP_ARRAY_SIZE(__VA_ARGS__))
#define RP1_NULL_FNC(f) \
	[func_##f] = PINCTRL_PINFUNCTION(#f, NULL, 0)
#define RP1_ALL_LEGACY_PINS \
		"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", \
		"gpio5", "gpio6", "gpio7", "gpio8", "gpio9", \
		"gpio10", "gpio11", "gpio12", "gpio13", "gpio14", \
		"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", \
		"gpio20", "gpio21", "gpio22", "gpio32", "gpio24", \
		"gpio25", "gpio26", "gpio27"
#define RP1_ALL_PINS RP1_ALL_LEGACY_PINS, \
		"gpio28", "gpio29", "gpio30", "gpio31", "gpio32", \
		"gpio33", "gpio34", "gpio35", "gpio36", "gpio37", \
		"gpio38", "gpio39", "gpio40", "gpio41", "gpio42", \
		"gpio43", "gpio44", "gpio45", "gpio46", "gpio47", \
		"gpio48", "gpio49", "gpio50", "gpio51", "gpio52", \
		"gpio53"

static const struct pinfunction rp1_func_names[] = {
	RP1_NULL_FNC(alt0),
	RP1_NULL_FNC(alt1),
	RP1_NULL_FNC(alt2),
	RP1_NULL_FNC(alt3),
	RP1_NULL_FNC(alt4),
	RP1_FNC(gpio, RP1_ALL_PINS),
	RP1_NULL_FNC(alt6),
	RP1_NULL_FNC(alt7),
	RP1_NULL_FNC(alt8),
	RP1_NULL_FNC(none),
	RP1_FNC(aaud, "aaud_0", "aaud_1", "aaud_2", "aaud_3", "aaud_4",
		"gpio12", "gpio13", "gpio38", "gpio39", "gpio40", "gpio41",
		"gpio49", "gpio50", "gpio51", "gpio52"),
	RP1_FNC(dpi, "dpi_16bit", "dpi_16bit_cpadhi",
		"dpi_16bit_pad666", "dpi_18bit, dpi_18bit_cpadhi",
		"dpi_24bit", RP1_ALL_LEGACY_PINS),
	RP1_FNC(dsi0_te_ext, "gpio16", "gpio38", "gpio46"),
	RP1_FNC(dsi1_te_ext, "gpio17", "gpio39", "gpio47"),
	RP1_FNC(gpclk0, "gpio4", "gpio20"),
	RP1_FNC(gpclk1, "gpio5", "gpio18", "gpio21"),
	RP1_FNC(gpclk2, "gpio6"),
	RP1_FNC(gpclk3, "gpio32", "gpio34", "gpio46"),
	RP1_FNC(gpclk4, "gpio33", "gpio43"),
	RP1_FNC(gpclk5, "gpio42", "gpio44", "gpio47"),
	RP1_FNC(i2c0, "i2c0_0", "i2c0_1", "gpio0", "gpio1", "gpio8", "gpio9"),
	RP1_FNC(i2c1, "i2c1_0", "i2c1_1", "gpio2", "gpio3", "gpio10", "gpio11"),
	RP1_FNC(i2c2, "i2c2_0", "i2c2_1", "gpio4", "gpio5", "gpio12", "gpio13"),
	RP1_FNC(i2c3, "i2c3_0", "i2c3_1", "i2c3_2", "gpio6", "gpio7", "gpio14",
		"gpio15", "gpio22", "gpio23"),
	RP1_FNC(i2c4, "i2c4_0", "i2c4_1", "i2c4_2", "i2c4_3", "gpio28",
		"gpio29", "gpio34", "gpio35", "gpio40", "gpio41", "gpio46",
		"gpio47"),
	RP1_FNC(i2c5, "i2c5_0", "i2c5_1", "i2c5_2", "i2c5_3", "gpio30",
		"gpio31", "gpio36", "gpio37", "gpio44", "gpio45", "gpio49",
		"gpio50"),
	RP1_FNC(i2c6, "i2c6_0", "i2c6_1", "gpio38", "gpio39", "gpio51",
		"gpio52"),
	RP1_FNC(i2s0, "i2s0", "i2s0_dual", "i2s0_quad", "gpio18", "gpio19",
		"gpio20", "gpio21", "gpio22", "gpio23", "gpio24", "gpio25",
		"gpio26", "gpio27"),
	RP1_FNC(i2s1, "i2s1", "i2s1_dual", "i2s1_quad", "gpio18", "gpio19",
		"gpio20", "gpio21", "gpio22", "gpio23", "gpio24", "gpio25",
		"gpio26", "gpio27"),
	RP1_FNC(i2s2, "i2s2_0", "i2s2_0_dual", "i2s2_1", "i2s2_1_dual",
		"gpio28", "gpio29", "gpio30", "gpio31", "gpio32", "gpio33",
		"gpio42", "gpio43", "gpio44", "gpio45", "gpio46", "gpio47"),
	RP1_FNC(ir, "gpio2", "gpio3"),
	RP1_FNC(mic, "mic_0", "mic_1", "mic_2", "mic_3", "gpio25", "gpio26",
		"gpio27", "gpio34", "gpio35", "gpio36", "gpio37", "gpio38",
		"gpio39", "gpio46", "gpio47", "gpio48"),
	RP1_FNC(pcie_clkreq_n, "gpio36", "gpio37", "gpio48", "gpio53"),
	RP1_FNC(pio, RP1_ALL_LEGACY_PINS),
	RP1_FNC(proc_rio, RP1_ALL_PINS),
	RP1_FNC(pwm0, "gpio12", "gpio13", "gpio14", "gpio15", "gpio18",
		"gpio19"),
	RP1_FNC(pwm1, "gpio34", "gpio35", "gpio40", "gpio41", "gpio44",
		"gpio45", "gpio48"),
	RP1_FNC(sd0, "sd0", "gpio22", "gpio23", "gpio24", "gpio25", "gpio26",
		"gpio27"),
	RP1_FNC(sd1, "sd1", "gpio28", "gpio29", "gpio30", "gpio31", "gpio32",
		"gpio33"),
	RP1_FNC(spi0, "spi0", "spi0_quad", "gpio0", "gpio1", "gpio2", "gpio3",
		"gpio7", "gpio8", "gpio9", "gpio10", "gpio11"),
	RP1_FNC(spi1, "spi1", "gpio19", "gpio20", "gpio21", "gpio16", "gpio17",
		"gpio18", "gpio27"),
	RP1_FNC(spi2, "spi2", "gpio0", "gpio1", "gpio2", "gpio3", "gpio24"),
	RP1_FNC(spi3, "spi3", "gpio4", "gpio5", "gpio6", "gpio7", "gpio25"),
	RP1_FNC(spi4, "spi4", "gpio8", "gpio9", "gpio10", "gpio11"),
	RP1_FNC(spi5, "spi5", "gpio12", "gpio13", "gpio14", "gpio15", "gpio26"),
	RP1_FNC(spi6, "spi6_0", "spi6_1", "gpio28", "gpio29", "gpio30",
		"gpio31", "gpio32", "gpio33", "gpio40", "gpio41", "gpio42",
		"gpio43", "gpio44", "gpio45"),
	RP1_FNC(spi7, "spi7_0", "spi7_1", "gpio45", "gpio46", "gpio47",
		"gpio48", "gpio49", "gpio50", "gpio51", "gpio53"),
	RP1_FNC(spi8, "spi8_0", "spi8_1", "gpio35", "gpio36", "gpio37",
		"gpio38", "gpio39", "gpio49", "gpio50", "gpio51", "gpio52",
		"gpio53"),
	RP1_FNC(uart0, "uart0", "uart0_ctrl", "gpio4", "gpio5", "gpio6",
		"gpio7", "gpio14", "gpio15", "gpio16", "gpio17"),
	RP1_FNC(uart1, "uart1", "uart1_ctrl", "gpio0", "gpio1", "gpio2",
		"gpio3"),
	RP1_FNC(uart2, "uart2", "uart2_ctrl", "gpio4", "gpio5", "gpio6",
		"gpio7"),
	RP1_FNC(uart3, "uart3", "uart3_ctrl", "gpio8", "gpio9", "gpio10",
		"gpio11"),
	RP1_FNC(uart4, "uart4", "uart4_ctrl", "gpio12", "gpio13", "gpio14",
		"gpio15"),
	RP1_FNC(uart5, "uart5_0", "uart5_0_ctrl", "uart5_1", "uart5_1_ctrl",
		"uart5_2", "uart5_2_ctrl", "uart5_3"),
	RP1_FNC(vbus0, "vbus0_0", "vbus0_1", "gpio28", "gpio29", "gpio34",
		"gpio35"),
	RP1_FNC(vbus1, "vbus1", "gpio42", "gpio43"),
	RP1_FNC(vbus2, "vbus2", "gpio50", "gpio51"),
	RP1_FNC(vbus3, "vbus3", "gpio52", "gpio53"),
	RP1_NULL_FNC(invalid),	//[func_invalid] = "?"
};

static const struct rp1_pin_funcs rp1_gpio_pin_funcs[] = {
	PIN(0, spi0, dpi, uart1, i2c0, _, gpio, proc_rio, pio, spi2),
	PIN(1, spi0, dpi, uart1, i2c0, _, gpio, proc_rio, pio, spi2),
	PIN(2, spi0, dpi, uart1, i2c1, ir, gpio, proc_rio, pio, spi2),
	PIN(3, spi0, dpi, uart1, i2c1, ir, gpio, proc_rio, pio, spi2),
	PIN(4, gpclk0, dpi, uart2, i2c2, uart0, gpio, proc_rio, pio, spi3),
	PIN(5, gpclk1, dpi, uart2, i2c2, uart0, gpio, proc_rio, pio, spi3),
	PIN(6, gpclk2, dpi, uart2, i2c3, uart0, gpio, proc_rio, pio, spi3),
	PIN(7, spi0, dpi, uart2, i2c3, uart0, gpio, proc_rio, pio, spi3),
	PIN(8, spi0, dpi, uart3, i2c0, _, gpio, proc_rio, pio, spi4),
	PIN(9, spi0, dpi, uart3, i2c0, _, gpio, proc_rio, pio, spi4),
	PIN(10, spi0, dpi, uart3, i2c1, _, gpio, proc_rio, pio, spi4),
	PIN(11, spi0, dpi, uart3, i2c1, _, gpio, proc_rio, pio, spi4),
	PIN(12, pwm0, dpi, uart4, i2c2, aaud, gpio, proc_rio, pio, spi5),
	PIN(13, pwm0, dpi, uart4, i2c2, aaud, gpio, proc_rio, pio, spi5),
	PIN(14, pwm0, dpi, uart4, i2c3, uart0, gpio, proc_rio, pio, spi5),
	PIN(15, pwm0, dpi, uart4, i2c3, uart0, gpio, proc_rio, pio, spi5),
	PIN(16, spi1, dpi, dsi0_te_ext, _, uart0, gpio, proc_rio, pio, _),
	PIN(17, spi1, dpi, dsi1_te_ext, _, uart0, gpio, proc_rio, pio, _),
	PIN(18, spi1, dpi, i2s0, pwm0, i2s1, gpio, proc_rio, pio, gpclk1),
	PIN(19, spi1, dpi, i2s0, pwm0, i2s1, gpio, proc_rio, pio, _),
	PIN(20, spi1, dpi, i2s0, gpclk0, i2s1, gpio, proc_rio, pio, _),
	PIN(21, spi1, dpi, i2s0, gpclk1, i2s1, gpio, proc_rio, pio, _),
	PIN(22, sd0, dpi, i2s0, i2c3, i2s1, gpio, proc_rio, pio, _),
	PIN(23, sd0, dpi, i2s0, i2c3, i2s1, gpio, proc_rio, pio, _),
	PIN(24, sd0, dpi, i2s0, _, i2s1, gpio, proc_rio, pio, spi2),
	PIN(25, sd0, dpi, i2s0, mic, i2s1, gpio, proc_rio, pio, spi3),
	PIN(26, sd0, dpi, i2s0, mic, i2s1, gpio, proc_rio, pio, spi5),
	PIN(27, sd0, dpi, i2s0, mic, i2s1, gpio, proc_rio, pio, spi1),
	PIN(28, sd1, i2c4, i2s2, spi6, vbus0, gpio, proc_rio, _, _),
	PIN(29, sd1, i2c4, i2s2, spi6, vbus0, gpio, proc_rio, _, _),
	PIN(30, sd1, i2c5, i2s2, spi6, uart5, gpio, proc_rio, _, _),
	PIN(31, sd1, i2c5, i2s2, spi6, uart5, gpio, proc_rio, _, _),
	PIN(32, sd1, gpclk3, i2s2, spi6, uart5, gpio, proc_rio, _, _),
	PIN(33, sd1, gpclk4, i2s2, spi6, uart5, gpio, proc_rio, _, _),
	PIN(34, pwm1, gpclk3, vbus0, i2c4, mic, gpio, proc_rio, _, _),
	PIN(35, spi8, pwm1, vbus0, i2c4, mic, gpio, proc_rio, _, _),
	PIN(36, spi8, uart5, pcie_clkreq_n, i2c5, mic, gpio, proc_rio, _, _),
	PIN(37, spi8, uart5, mic, i2c5, pcie_clkreq_n, gpio, proc_rio, _, _),
	PIN(38, spi8, uart5, mic, i2c6, aaud, gpio, proc_rio, dsi0_te_ext, _),
	PIN(39, spi8, uart5, mic, i2c6, aaud, gpio, proc_rio, dsi1_te_ext, _),
	PIN(40, pwm1, uart5, i2c4, spi6, aaud, gpio, proc_rio, _, _),
	PIN(41, pwm1, uart5, i2c4, spi6, aaud, gpio, proc_rio, _, _),
	PIN(42, gpclk5, uart5, vbus1, spi6, i2s2, gpio, proc_rio, _, _),
	PIN(43, gpclk4, uart5, vbus1, spi6, i2s2, gpio, proc_rio, _, _),
	PIN(44, gpclk5, i2c5, pwm1, spi6, i2s2, gpio, proc_rio, _, _),
	PIN(45, pwm1, i2c5, spi7, spi6, i2s2, gpio, proc_rio, _, _),
	PIN(46, gpclk3, i2c4, spi7, mic, i2s2, gpio, proc_rio, dsi0_te_ext, _),
	PIN(47, gpclk5, i2c4, spi7, mic, i2s2, gpio, proc_rio, dsi1_te_ext, _),
	PIN(48, pwm1, pcie_clkreq_n, spi7, mic, uart5, gpio, proc_rio, _, _),
	PIN(49, spi8, spi7, i2c5, aaud, uart5, gpio, proc_rio, _, _),
	PIN(50, spi8, spi7, i2c5, aaud, vbus2, gpio, proc_rio, _, _),
	PIN(51, spi8, spi7, i2c6, aaud, vbus2, gpio, proc_rio, _, _),
	PIN(52, spi8, _, i2c6, aaud, vbus3, gpio, proc_rio, _, _),
	PIN(53, spi8, spi7, _, pcie_clkreq_n, vbus3, gpio, proc_rio, _, _),
};

static const u8 legacy_fsel_map[][8] = {
	LEGACY_MAP(0, i2c0, _, dpi, spi2, uart1, _),
	LEGACY_MAP(1, i2c0, _, dpi, spi2, uart1, _),
	LEGACY_MAP(2, i2c1, _, dpi, spi2, uart1, _),
	LEGACY_MAP(3, i2c1, _, dpi, spi2, uart1, _),
	LEGACY_MAP(4, gpclk0, _, dpi, spi3, uart2, i2c2),
	LEGACY_MAP(5, gpclk1, _, dpi, spi3, uart2, i2c2),
	LEGACY_MAP(6, gpclk2, _, dpi, spi3, uart2, i2c3),
	LEGACY_MAP(7, spi0, _, dpi, spi3, uart2, i2c3),
	LEGACY_MAP(8, spi0, _, dpi, _, uart3, i2c0),
	LEGACY_MAP(9, spi0, _, dpi, _, uart3, i2c0),
	LEGACY_MAP(10, spi0, _, dpi, _, uart3, i2c1),
	LEGACY_MAP(11, spi0, _, dpi, _, uart3, i2c1),
	LEGACY_MAP(12, pwm0, _, dpi, spi5, uart4, i2c2),
	LEGACY_MAP(13, pwm0, _, dpi, spi5, uart4, i2c2),
	LEGACY_MAP(14, uart0, _, dpi, spi5, uart4, _),
	LEGACY_MAP(15, uart0, _, dpi, spi5, uart4, _),
	LEGACY_MAP(16, _, _, dpi, uart0, spi1, _),
	LEGACY_MAP(17, _, _, dpi, uart0, spi1, _),
	LEGACY_MAP(18, i2s0, _, dpi, _, spi1, pwm0),
	LEGACY_MAP(19, i2s0, _, dpi, _, spi1, pwm0),
	LEGACY_MAP(20, i2s0, _, dpi, _, spi1, gpclk0),
	LEGACY_MAP(21, i2s0, _, dpi, _, spi1, gpclk1),
	LEGACY_MAP(22, sd0, _, dpi, _, _, i2c3),
	LEGACY_MAP(23, sd0, _, dpi, _, _, i2c3),
	LEGACY_MAP(24, sd0, _, dpi, _, _, spi2),
	LEGACY_MAP(25, sd0, _, dpi, _, _, spi3),
	LEGACY_MAP(26, sd0, _, dpi, _, _, spi5),
	LEGACY_MAP(27, sd0, _, dpi, _, _, _),
};

static const char * const irq_type_names[] = {
	[IRQ_TYPE_NONE] = "none",
	[IRQ_TYPE_EDGE_RISING] = "edge-rising",
	[IRQ_TYPE_EDGE_FALLING] = "edge-falling",
	[IRQ_TYPE_EDGE_BOTH] = "edge-both",
	[IRQ_TYPE_LEVEL_HIGH] = "level-high",
	[IRQ_TYPE_LEVEL_LOW] = "level-low",
};

static bool persist_gpio_outputs = true;
module_param(persist_gpio_outputs, bool, 0644);
MODULE_PARM_DESC(persist_gpio_outputs, "Enable GPIO_OUT persistence when pin is freed");

static const struct rp1_iobank_desc rp1_iobanks[RP1_NUM_BANKS] = {
	/*         gpio   inte    ints     rio    pads */
	{  0, 28, 0x0000, 0x011c, 0x0124, 0x0000, 0x0004 },
	{ 28,  6, 0x4000, 0x411c, 0x4124, 0x4000, 0x4004 },
	{ 34, 20, 0x8000, 0x811c, 0x8124, 0x8000, 0x8004 },
};

static int rp1_pinconf_set(struct pinctrl_dev *pctldev,
			   unsigned int offset, unsigned long *configs,
			   unsigned int num_configs);

static struct rp1_pin_info *rp1_get_pin(struct gpio_chip *chip,
					unsigned int offset)
{
	struct rp1_pinctrl *pc = gpiochip_get_data(chip);

	if (pc && offset < RP1_NUM_GPIOS)
		return &pc->pins[offset];
	return NULL;
}

static struct rp1_pin_info *rp1_get_pin_pctl(struct pinctrl_dev *pctldev,
					     unsigned int offset)
{
	struct rp1_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	if (pc && offset < RP1_NUM_GPIOS)
		return &pc->pins[offset];
	return NULL;
}

static void rp1_input_enable(struct rp1_pin_info *pin, int value)
{
	regmap_field_write(pin->pad[RP1_PAD_IN_ENABLE], !!value);
}

static void rp1_output_enable(struct rp1_pin_info *pin, int value)
{
	regmap_field_write(pin->pad[RP1_PAD_OUT_DISABLE], !value);
}

static u32 rp1_get_fsel(struct rp1_pin_info *pin)
{
	u32 oeover, fsel;

	regmap_field_read(pin->gpio[RP1_GPIO_CTRL_OEOVER], &oeover);
	regmap_field_read(pin->gpio[RP1_GPIO_CTRL_FUNCSEL], &fsel);

	if (oeover != RP1_OEOVER_PERI || fsel >= RP1_FSEL_COUNT)
		fsel = RP1_FSEL_NONE;

	return fsel;
}

static void rp1_set_fsel(struct rp1_pin_info *pin, u32 fsel)
{
	if (fsel >= RP1_FSEL_COUNT)
		fsel = RP1_FSEL_NONE_HW;

	rp1_input_enable(pin, 1);
	rp1_output_enable(pin, 1);

	if (fsel == RP1_FSEL_NONE) {
		regmap_field_write(pin->gpio[RP1_GPIO_CTRL_OEOVER], RP1_OEOVER_DISABLE);
	} else {
		regmap_field_write(pin->gpio[RP1_GPIO_CTRL_OUTOVER], RP1_OUTOVER_PERI);
		regmap_field_write(pin->gpio[RP1_GPIO_CTRL_OEOVER], RP1_OEOVER_PERI);
	}

	regmap_field_write(pin->gpio[RP1_GPIO_CTRL_FUNCSEL], fsel);
}

static int rp1_get_dir(struct rp1_pin_info *pin)
{
	unsigned int val;

	regmap_field_read(pin->rio[RP1_RIO_OE], &val);

	return !val ? RP1_DIR_INPUT : RP1_DIR_OUTPUT;
}

static void rp1_set_dir(struct rp1_pin_info *pin, bool is_input)
{
	int reg = is_input ? RP1_RIO_OE_CLR : RP1_RIO_OE_SET;

	regmap_field_write(pin->rio[reg], 1);
}

static int rp1_get_value(struct rp1_pin_info *pin)
{
	unsigned int val;

	regmap_field_read(pin->rio[RP1_RIO_IN], &val);

	return !!val;
}

static void rp1_set_value(struct rp1_pin_info *pin, int value)
{
	/* Assume the pin is already an output */
	int reg = value ? RP1_RIO_OUT_SET : RP1_RIO_OUT_CLR;

	regmap_field_write(pin->rio[reg], 1);
}

static int rp1_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rp1_pin_info *pin = rp1_get_pin(chip, offset);
	int ret;

	if (!pin)
		return -EINVAL;

	ret = rp1_get_value(pin);

	return ret;
}

static int rp1_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct rp1_pin_info *pin = rp1_get_pin(chip, offset);

	if (pin)
		rp1_set_value(pin, value);

	return 0;
}

static int rp1_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct rp1_pin_info *pin = rp1_get_pin(chip, offset);
	u32 fsel;

	if (!pin)
		return -EINVAL;

	fsel = rp1_get_fsel(pin);
	if (fsel != RP1_FSEL_GPIO)
		return -EINVAL;

	return (rp1_get_dir(pin) == RP1_DIR_OUTPUT) ?
		GPIO_LINE_DIRECTION_OUT :
		GPIO_LINE_DIRECTION_IN;
}

static int rp1_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct rp1_pin_info *pin = rp1_get_pin(chip, offset);

	if (!pin)
		return -EINVAL;
	rp1_set_dir(pin, RP1_DIR_INPUT);
	rp1_set_fsel(pin, RP1_FSEL_GPIO);

	return 0;
}

static int rp1_gpio_direction_output(struct gpio_chip *chip, unsigned int offset,
				     int value)
{
	struct rp1_pin_info *pin = rp1_get_pin(chip, offset);

	if (!pin)
		return -EINVAL;
	rp1_set_value(pin, value);
	rp1_set_dir(pin, RP1_DIR_OUTPUT);
	rp1_set_fsel(pin, RP1_FSEL_GPIO);

	return 0;
}

static int rp1_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
			       unsigned long config)
{
	struct rp1_pinctrl *pc = gpiochip_get_data(chip);
	unsigned long configs[] = { config };

	return rp1_pinconf_set(pc->pctl_dev, offset, configs,
			       ARRAY_SIZE(configs));
}

static const struct gpio_chip rp1_gpio_chip = {
	.label = MODULE_NAME,
	.owner = THIS_MODULE,
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.direction_input = rp1_gpio_direction_input,
	.direction_output = rp1_gpio_direction_output,
	.get_direction = rp1_gpio_get_direction,
	.get = rp1_gpio_get,
	.set = rp1_gpio_set,
	.base = -1,
	.set_config = rp1_gpio_set_config,
	.ngpio = RP1_NUM_GPIOS,
	.can_sleep = false,
};

static void rp1_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *chip = irq_desc_get_handler_data(desc);
	struct irq_chip *host_chip = irq_desc_get_chip(desc);
	struct rp1_pinctrl *pc = gpiochip_get_data(chip);
	const struct rp1_iobank_desc *bank;
	int irq = irq_desc_get_irq(desc);
	unsigned long ints;
	int bit_pos;

	if (pc->irq[0] == irq)
		bank = &rp1_iobanks[0];
	else if (pc->irq[1] == irq)
		bank = &rp1_iobanks[1];
	else
		bank = &rp1_iobanks[2];

	chained_irq_enter(host_chip, desc);

	ints = readl(pc->gpio_base + bank->ints_offset);
	for_each_set_bit(bit_pos, &ints, 32) {
		struct rp1_pin_info *pin = rp1_get_pin(chip, bit_pos);

		regmap_field_write(pin->gpio[RP1_GPIO_CTRL_IRQRESET_SET], 1);
		generic_handle_irq(irq_find_mapping(pc->gpio_chip.irq.domain,
						    bank->gpio_offset + bit_pos));
	}

	chained_irq_exit(host_chip, desc);
}

static void rp1_gpio_irq_config(struct rp1_pin_info *pin, bool enable)
{
	int reg = enable ? RP1_INTE_SET : RP1_INTE_CLR;

	regmap_field_write(pin->inte[reg], 1);
	if (!enable)
		/* Clear any latched events */
		regmap_field_write(pin->gpio[RP1_GPIO_CTRL_IRQRESET_SET], 1);
}

static void rp1_gpio_irq_enable(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	unsigned int gpio = irqd_to_hwirq(data);
	struct rp1_pin_info *pin = rp1_get_pin(chip, gpio);

	rp1_gpio_irq_config(pin, true);
}

static void rp1_gpio_irq_disable(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	unsigned int gpio = irqd_to_hwirq(data);
	struct rp1_pin_info *pin = rp1_get_pin(chip, gpio);

	rp1_gpio_irq_config(pin, false);
}

static int rp1_irq_set_type(struct rp1_pin_info *pin, unsigned int type)
{
	u32 irq_flags;

	switch (type) {
	case IRQ_TYPE_NONE:
		irq_flags = 0;
		break;
	case IRQ_TYPE_EDGE_RISING:
		irq_flags = RP1_INT_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_flags = RP1_INT_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		irq_flags = RP1_INT_EDGE_RISING | RP1_INT_EDGE_FALLING;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irq_flags = RP1_INT_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_flags = RP1_INT_LEVEL_LOW;
		break;

	default:
		return -EINVAL;
	}

	/* Clear them all */
	regmap_field_write(pin->gpio[RP1_GPIO_CTRL_INT_CLR], RP1_INT_MASK);

	/* Set those that are needed */
	regmap_field_write(pin->gpio[RP1_GPIO_CTRL_INT_SET], irq_flags);
	pin->irq_type = type;

	return 0;
}

static int rp1_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	unsigned int gpio = irqd_to_hwirq(data);
	struct rp1_pin_info *pin = rp1_get_pin(chip, gpio);
	struct rp1_pinctrl *pc = gpiochip_get_data(chip);
	int bank = pin->bank;
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&pc->irq_lock[bank], flags);

	ret = rp1_irq_set_type(pin, type);
	if (!ret) {
		if (type & IRQ_TYPE_EDGE_BOTH)
			irq_set_handler_locked(data, handle_edge_irq);
		else
			irq_set_handler_locked(data, handle_level_irq);
	}

	raw_spin_unlock_irqrestore(&pc->irq_lock[bank], flags);

	return ret;
}

static void rp1_gpio_irq_ack(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	unsigned int gpio = irqd_to_hwirq(data);
	struct rp1_pin_info *pin = rp1_get_pin(chip, gpio);

	/* Clear any latched events */
	regmap_field_write(pin->gpio[RP1_GPIO_CTRL_IRQRESET_SET], 1);
}

static int rp1_gpio_irq_set_affinity(struct irq_data *data, const struct cpumask *dest, bool force)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct rp1_pinctrl *pc = gpiochip_get_data(chip);
	const struct rp1_iobank_desc *bank;
	struct irq_data *parent_data = NULL;
	int i;

	for (i = 0; i < 3; i++) {
		bank = &rp1_iobanks[i];
		if (data->hwirq >= bank->min_gpio &&
		    data->hwirq < bank->min_gpio + bank->num_gpios) {
			parent_data = irq_get_irq_data(pc->irq[i]);
			break;
		}
	}

	if (parent_data && parent_data->chip->irq_set_affinity)
		return parent_data->chip->irq_set_affinity(parent_data, dest, force);

	return -EINVAL;
}

static struct irq_chip rp1_gpio_irq_chip = {
	.name = MODULE_NAME,
	.irq_enable = rp1_gpio_irq_enable,
	.irq_disable = rp1_gpio_irq_disable,
	.irq_set_type = rp1_gpio_irq_set_type,
	.irq_ack = rp1_gpio_irq_ack,
	.irq_mask = rp1_gpio_irq_disable,
	.irq_unmask = rp1_gpio_irq_enable,
	.irq_set_affinity = rp1_gpio_irq_set_affinity,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int rp1_pctl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(rp1_gpio_groups) + ARRAY_SIZE(rp1_gpio_pins);
}

static const char *rp1_pctl_get_group_name(struct pinctrl_dev *pctldev,
					   unsigned int selector)
{
	unsigned int ngroups = ARRAY_SIZE(rp1_gpio_groups);

	if (selector < ngroups)
		return rp1_gpio_groups[selector].name;

	return rp1_gpio_pins[selector - ngroups].name;
}

static enum funcs rp1_get_fsel_func(unsigned int pin, unsigned int fsel)
{
	if (pin < RP1_NUM_GPIOS) {
		if (fsel < RP1_FSEL_COUNT)
			return rp1_gpio_pin_funcs[pin].funcs[fsel];
		else if (fsel == RP1_FSEL_NONE)
			return func_none;
	}
	return func_invalid;
}

static int rp1_pctl_get_group_pins(struct pinctrl_dev *pctldev,
				   unsigned int selector,
				   const unsigned int **pins,
				   unsigned int *num_pins)
{
	unsigned int ngroups = ARRAY_SIZE(rp1_gpio_groups);

	if (selector < ngroups) {
		*pins = rp1_gpio_groups[selector].pins;
		*num_pins = rp1_gpio_groups[selector].npins;
	} else {
		*pins = &rp1_gpio_pins[selector - ngroups].number;
		*num_pins = 1;
	}

	return 0;
}

static void rp1_pctl_pin_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *s,
				  unsigned int offset)
{
	struct rp1_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip = &pc->gpio_chip;
	struct rp1_pin_info *pin = rp1_get_pin_pctl(pctldev, offset);
	u32 fsel = rp1_get_fsel(pin);
	enum funcs func = rp1_get_fsel_func(offset, fsel);
	int value = rp1_get_value(pin);
	int irq = irq_find_mapping(chip->irq.domain, offset);

	seq_printf(s, "function %s (%s) in %s; irq %d (%s)",
		   rp1_func_names[fsel].name, rp1_func_names[func].name,
		   value ? "hi" : "lo",
		   irq, irq_type_names[pin->irq_type]);
}

static void rp1_pctl_dt_free_map(struct pinctrl_dev *pctldev,
				 struct pinctrl_map *maps, unsigned int num_maps)
{
	int i;

	for (i = 0; i < num_maps; i++)
		if (maps[i].type == PIN_MAP_TYPE_CONFIGS_PIN)
			kfree(maps[i].data.configs.configs);

	kfree(maps);
}

static int rp1_pctl_legacy_map_func(struct rp1_pinctrl *pc,
				    struct device_node *np, u32 pin, u32 fnum,
				    struct pinctrl_map *maps,
				    unsigned int *num_maps)
{
	struct pinctrl_map *map = &maps[*num_maps];
	enum funcs func;

	if (fnum >= ARRAY_SIZE(legacy_fsel_map[0])) {
		dev_err(pc->dev, "%pOF: invalid brcm,function %d\n", np, fnum);
		return -EINVAL;
	}

	if (pin < ARRAY_SIZE(legacy_fsel_map)) {
		func = legacy_fsel_map[pin][fnum];
	} else if (fnum < 2) {
		func = func_gpio;
	} else {
		dev_err(pc->dev, "%pOF: invalid brcm,pins value %d\n",
			np, pin);
		return -EINVAL;
	}

	map->type = PIN_MAP_TYPE_MUX_GROUP;
	map->data.mux.group = rp1_pctl_get_group_name(pc->pctl_dev,
						      ARRAY_SIZE(rp1_gpio_groups)
						      + pin);
	map->data.mux.function = rp1_func_names[func].name;
	(*num_maps)++;

	return 0;
}

static int rp1_pctl_legacy_map_pull(struct rp1_pinctrl *pc,
				    struct device_node *np, u32 pin, u32 pull,
				    struct pinctrl_map *maps,
				    unsigned int *num_maps)
{
	struct pinctrl_map *map = &maps[*num_maps];
	enum pin_config_param param;
	unsigned long *configs;

	switch (pull) {
	case RP1_PUD_OFF:
		param = PIN_CONFIG_BIAS_DISABLE;
		break;
	case RP1_PUD_DOWN:
		param = PIN_CONFIG_BIAS_PULL_DOWN;
		break;
	case RP1_PUD_UP:
		param = PIN_CONFIG_BIAS_PULL_UP;
		break;
	default:
		dev_err(pc->dev, "%pOF: invalid brcm,pull %d\n", np, pull);
		return -EINVAL;
	}

	configs = kzalloc(sizeof(*configs), GFP_KERNEL);
	if (!configs)
		return -ENOMEM;

	configs[0] = pinconf_to_config_packed(param, 0);
	map->type = PIN_MAP_TYPE_CONFIGS_PIN;
	map->data.configs.group_or_pin = rp1_gpio_pins[pin].name;
	map->data.configs.configs = configs;
	map->data.configs.num_configs = 1;
	(*num_maps)++;

	return 0;
}

static int rp1_pctl_dt_node_to_map(struct pinctrl_dev *pctldev,
				   struct device_node *np,
				   struct pinctrl_map **map,
				   unsigned int *num_maps)
{
	struct rp1_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	struct property *pins, *funcs, *pulls;
	int num_pins, num_funcs, num_pulls, maps_per_pin;
	struct pinctrl_map *maps;
	unsigned long *configs = NULL;
	const char *function = NULL;
	unsigned int reserved_maps;
	int num_configs = 0;
	int i, err;
	u32 pin, func, pull;

	/* Check for legacy pin declaration */
	pins = of_find_property(np, "brcm,pins", NULL);

	if (!pins) /* Assume generic bindings in this node */
		return pinconf_generic_dt_node_to_map_all(pctldev, np, map, num_maps);

	funcs = of_find_property(np, "brcm,function", NULL);
	if (!funcs)
		of_property_read_string(np, "function", &function);

	pulls = of_find_property(np, "brcm,pull", NULL);
	if (!pulls)
		pinconf_generic_parse_dt_config(np, pctldev, &configs, &num_configs);

	if (!function && !funcs && !num_configs && !pulls) {
		dev_err(pc->dev,
			"%pOF: no function, brcm,function, brcm,pull, etc.\n",
			np);
		return -EINVAL;
	}

	num_pins = pins->length / 4;
	num_funcs = funcs ? (funcs->length / 4) : 0;
	num_pulls = pulls ? (pulls->length / 4) : 0;

	if (num_funcs > 1 && num_funcs != num_pins) {
		dev_err(pc->dev,
			"%pOF: brcm,function must have 1 or %d entries\n",
			np, num_pins);
		return -EINVAL;
	}

	if (num_pulls > 1 && num_pulls != num_pins) {
		dev_err(pc->dev,
			"%pOF: brcm,pull must have 1 or %d entries\n",
			np, num_pins);
		return -EINVAL;
	}

	maps_per_pin = 0;
	if (function || num_funcs)
		maps_per_pin++;
	if (num_configs || num_pulls)
		maps_per_pin++;
	reserved_maps = num_pins * maps_per_pin;
	maps = kcalloc(reserved_maps, sizeof(*maps), GFP_KERNEL);
	if (!maps)
		return -ENOMEM;

	*num_maps = 0;

	for (i = 0; i < num_pins; i++) {
		err = of_property_read_u32_index(np, "brcm,pins", i, &pin);
		if (err)
			goto out;
		if (num_funcs) {
			err = of_property_read_u32_index(np, "brcm,function",
							 (num_funcs > 1) ? i : 0,
							 &func);
			if (err)
				goto out;
			err = rp1_pctl_legacy_map_func(pc, np, pin, func,
						       maps, num_maps);
		} else if (function) {
			err = pinctrl_utils_add_map_mux(pctldev, &maps,
							&reserved_maps, num_maps,
							rp1_gpio_groups[pin].name,
							function);
		}

		if (err)
			goto out;

		if (num_pulls) {
			err = of_property_read_u32_index(np, "brcm,pull",
							 (num_pulls > 1) ? i : 0,
							 &pull);
			if (err)
				goto out;
			err = rp1_pctl_legacy_map_pull(pc, np, pin, pull,
						       maps, num_maps);
		} else if (num_configs) {
			err = pinctrl_utils_add_map_configs(pctldev, &maps,
							    &reserved_maps, num_maps,
							    rp1_gpio_groups[pin].name,
							    configs, num_configs,
							    PIN_MAP_TYPE_CONFIGS_PIN);
		}

		if (err)
			goto out;
	}

	*map = maps;

	return 0;

out:
	rp1_pctl_dt_free_map(pctldev, maps, reserved_maps);
	return err;
}

static const struct pinctrl_ops rp1_pctl_ops = {
	.get_groups_count = rp1_pctl_get_groups_count,
	.get_group_name = rp1_pctl_get_group_name,
	.get_group_pins = rp1_pctl_get_group_pins,
	.pin_dbg_show = rp1_pctl_pin_dbg_show,
	.dt_node_to_map = rp1_pctl_dt_node_to_map,
	.dt_free_map = rp1_pctl_dt_free_map,
};

static int rp1_pmx_free(struct pinctrl_dev *pctldev, unsigned int offset)
{
	struct rp1_pin_info *pin = rp1_get_pin_pctl(pctldev, offset);
	u32 fsel = rp1_get_fsel(pin);

	/* Return all pins to GPIO_IN, unless persist_gpio_outputs is set */
	if (persist_gpio_outputs && fsel == RP1_FSEL_GPIO)
		return 0;

	rp1_set_dir(pin, RP1_DIR_INPUT);
	rp1_set_fsel(pin, RP1_FSEL_GPIO);

	return 0;
}

static int rp1_pmx_get_functions_count(struct pinctrl_dev *pctldev)
{
	return func_count;
}

static const char *rp1_pmx_get_function_name(struct pinctrl_dev *pctldev,
					     unsigned int selector)
{
	return (selector < func_count) ? rp1_func_names[selector].name : NULL;
}

static int rp1_pmx_get_function_groups(struct pinctrl_dev *pctldev,
				       unsigned int selector,
				       const char * const **groups,
				       unsigned * const num_groups)
{
	*groups = rp1_func_names[selector].groups;
	*num_groups = rp1_func_names[selector].ngroups;

	return 0;
}

static int rp1_pmx_set(struct pinctrl_dev *pctldev, unsigned int func_selector,
		       unsigned int group_selector)
{
	struct rp1_pin_info *pin;
	const unsigned int *pins;
	const u8 *pin_funcs;
	unsigned int num_pins;
	int offset, fsel;

	rp1_pctl_get_group_pins(pctldev, group_selector, &pins, &num_pins);

	for (offset = 0; offset < num_pins; ++offset) {
		pin = rp1_get_pin_pctl(pctldev, pins[offset]);
		/* func_selector is an enum funcs, so needs translation */
		if (func_selector >= RP1_FSEL_COUNT) {
			/* Convert to an fsel number */
			pin_funcs = rp1_gpio_pin_funcs[pin->num].funcs;
			for (fsel = 0; fsel < RP1_FSEL_COUNT; fsel++) {
				if (pin_funcs[fsel] == func_selector)
					break;
			}
		} else {
			fsel = (int)func_selector;
		}

		if (fsel >= RP1_FSEL_COUNT && fsel != RP1_FSEL_NONE)
			return -EINVAL;

		rp1_set_fsel(pin, fsel);
	}

	return 0;
}

static void rp1_pmx_gpio_disable_free(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset)
{
	(void)rp1_pmx_free(pctldev, offset);
}

static int rp1_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset,
				      bool input)
{
	struct rp1_pin_info *pin = rp1_get_pin_pctl(pctldev, offset);

	rp1_set_dir(pin, input);
	rp1_set_fsel(pin, RP1_FSEL_GPIO);

	return 0;
}

static const struct pinmux_ops rp1_pmx_ops = {
	.free = rp1_pmx_free,
	.get_functions_count = rp1_pmx_get_functions_count,
	.get_function_name = rp1_pmx_get_function_name,
	.get_function_groups = rp1_pmx_get_function_groups,
	.set_mux = rp1_pmx_set,
	.gpio_disable_free = rp1_pmx_gpio_disable_free,
	.gpio_set_direction = rp1_pmx_gpio_set_direction,
};

static void rp1_pull_config_set(struct rp1_pin_info *pin, unsigned int arg)
{
	regmap_field_write(pin->pad[RP1_PAD_PULL], arg & 0x3);
}

static int rp1_pinconf_set(struct pinctrl_dev *pctldev, unsigned int offset,
			   unsigned long *configs, unsigned int num_configs)
{
	struct rp1_pin_info *pin = rp1_get_pin_pctl(pctldev, offset);
	u32 param, arg;
	int i;

	if (!pin)
		return -EINVAL;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			rp1_pull_config_set(pin, RP1_PUD_OFF);
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			rp1_pull_config_set(pin, RP1_PUD_DOWN);
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			rp1_pull_config_set(pin, RP1_PUD_UP);
			break;

		case PIN_CONFIG_INPUT_ENABLE:
			rp1_input_enable(pin, arg);
			break;

		case PIN_CONFIG_OUTPUT_ENABLE:
			rp1_output_enable(pin, arg);
			break;

		case PIN_CONFIG_LEVEL:
			rp1_set_value(pin, arg);
			rp1_set_dir(pin, RP1_DIR_OUTPUT);
			rp1_set_fsel(pin, RP1_FSEL_GPIO);
			break;

		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			regmap_field_write(pin->pad[RP1_PAD_SCHMITT], !!arg);
			break;

		case PIN_CONFIG_SLEW_RATE:
			regmap_field_write(pin->pad[RP1_PAD_SLEWFAST], !!arg);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			switch (arg) {
			case 2:
				arg = RP1_PAD_DRIVE_2MA;
				break;
			case 4:
				arg = RP1_PAD_DRIVE_4MA;
				break;
			case 8:
				arg = RP1_PAD_DRIVE_8MA;
				break;
			case 12:
				arg = RP1_PAD_DRIVE_12MA;
				break;
			default:
				return -ENOTSUPP;
			}
			regmap_field_write(pin->pad[RP1_PAD_DRIVE], arg);
			break;

		default:
			return -ENOTSUPP;

		} /* switch param type */
	} /* for each config */

	return 0;
}

static int rp1_pinconf_get(struct pinctrl_dev *pctldev, unsigned int offset,
			   unsigned long *config)
{
	struct rp1_pin_info *pin = rp1_get_pin_pctl(pctldev, offset);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 padctrl;
	u32 arg;

	if (!pin)
		return -EINVAL;

	switch (param) {
	case PIN_CONFIG_INPUT_ENABLE:
		regmap_field_read(pin->pad[RP1_PAD_IN_ENABLE], &padctrl);
		arg = !!padctrl;
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
		regmap_field_read(pin->pad[RP1_PAD_OUT_DISABLE], &padctrl);
		arg = !padctrl;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		regmap_field_read(pin->pad[RP1_PAD_SCHMITT], &padctrl);
		arg = !!padctrl;
		break;
	case PIN_CONFIG_SLEW_RATE:
		regmap_field_read(pin->pad[RP1_PAD_SLEWFAST], &padctrl);
		arg = !!padctrl;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		regmap_field_read(pin->pad[RP1_PAD_DRIVE], &padctrl);
		switch (padctrl) {
		case RP1_PAD_DRIVE_2MA:
			arg = 2;
			break;
		case RP1_PAD_DRIVE_4MA:
			arg = 4;
			break;
		case RP1_PAD_DRIVE_8MA:
			arg = 8;
			break;
		case RP1_PAD_DRIVE_12MA:
			arg = 12;
			break;
		}
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		regmap_field_read(pin->pad[RP1_PAD_PULL], &padctrl);
		arg = ((padctrl == RP1_PUD_OFF));
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		regmap_field_read(pin->pad[RP1_PAD_PULL], &padctrl);
		arg = ((padctrl == RP1_PUD_DOWN));
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		regmap_field_read(pin->pad[RP1_PAD_PULL], &padctrl);
		arg = ((padctrl == RP1_PUD_UP));
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int rp1_pinconf_group_get(struct pinctrl_dev *pctldev, unsigned int selector,
				 unsigned long *config)
{
	const unsigned int *pins;
	unsigned int npins;
	int ret;

	ret = rp1_pctl_get_group_pins(pctldev, selector, &pins, &npins);
	if (ret < 0)
		return ret;

	if (!npins)
		return -ENODEV;

	ret = rp1_pinconf_get(pctldev, pins[0], config);

	return ret;
}

static int rp1_pinconf_group_set(struct pinctrl_dev *pctldev, unsigned int selector,
				 unsigned long *configs, unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int npins;
	int ret, i;

	ret = rp1_pctl_get_group_pins(pctldev, selector, &pins, &npins);
	if (ret < 0)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = rp1_pinconf_set(pctldev, pins[i], configs, num_configs);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops rp1_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = rp1_pinconf_get,
	.pin_config_set = rp1_pinconf_set,
	.pin_config_group_get = rp1_pinconf_group_get,
	.pin_config_group_set = rp1_pinconf_group_set,
};

static struct pinctrl_desc rp1_pinctrl_desc = {
	.name = MODULE_NAME,
	.pins = rp1_gpio_pins,
	.npins = ARRAY_SIZE(rp1_gpio_pins),
	.pctlops = &rp1_pctl_ops,
	.pmxops = &rp1_pmx_ops,
	.confops = &rp1_pinconf_ops,
	.owner = THIS_MODULE,
};

static struct pinctrl_gpio_range rp1_pinctrl_gpio_range = {
	.name = MODULE_NAME,
	.npins = RP1_NUM_GPIOS,
};

static const struct of_device_id rp1_pinctrl_match[] = {
	{
		.compatible = "raspberrypi,rp1-gpio",
		.data = &rp1_pinconf_ops,
	},
	{},
};
MODULE_DEVICE_TABLE(of, rp1_pinctrl_match);

static struct rp1_pinctrl rp1_pinctrl_data = {};

static const struct regmap_range rp1_gpio_reg_ranges[] = {
	/* BANK 0 */
	regmap_reg_range(0x2004, 0x20dc),
	regmap_reg_range(0x3004, 0x30dc),
	regmap_reg_range(0x0004, 0x00dc),
	regmap_reg_range(0x0124, 0x0124),
	regmap_reg_range(0x211c, 0x211c),
	regmap_reg_range(0x311c, 0x311c),
	/* BANK 1 */
	regmap_reg_range(0x6004, 0x602c),
	regmap_reg_range(0x7004, 0x702c),
	regmap_reg_range(0x4004, 0x402c),
	regmap_reg_range(0x4124, 0x4124),
	regmap_reg_range(0x611c, 0x611c),
	regmap_reg_range(0x711c, 0x711c),
	/* BANK 2 */
	regmap_reg_range(0xa004, 0xa09c),
	regmap_reg_range(0xb004, 0xb09c),
	regmap_reg_range(0x8004, 0x809c),
	regmap_reg_range(0x8124, 0x8124),
	regmap_reg_range(0xa11c, 0xa11c),
	regmap_reg_range(0xb11c, 0xb11c),
};

static const struct regmap_range rp1_rio_reg_ranges[] = {
	/* BANK 0 */
	regmap_reg_range(0x2000, 0x2004),
	regmap_reg_range(0x3000, 0x3004),
	regmap_reg_range(0x0004, 0x0008),
	/* BANK 1 */
	regmap_reg_range(0x6000, 0x6004),
	regmap_reg_range(0x7000, 0x7004),
	regmap_reg_range(0x4004, 0x4008),
	/* BANK 2 */
	regmap_reg_range(0xa000, 0xa004),
	regmap_reg_range(0xb000, 0xb004),
	regmap_reg_range(0x8004, 0x8008),
};

static const struct regmap_range rp1_pads_reg_ranges[] = {
	/* BANK 0 */
	regmap_reg_range(0x0004, 0x0070),
	/* BANK 1 */
	regmap_reg_range(0x4004, 0x4018),
	/* BANK 2 */
	regmap_reg_range(0x8004, 0x8050),
};

static const struct regmap_access_table rp1_gpio_reg_table = {
	.yes_ranges = rp1_gpio_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(rp1_gpio_reg_ranges),
};

static const struct regmap_access_table rp1_rio_reg_table = {
	.yes_ranges = rp1_rio_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(rp1_rio_reg_ranges),
};

static const struct regmap_access_table rp1_pads_reg_table = {
	.yes_ranges = rp1_pads_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(rp1_pads_reg_ranges),
};

static const struct regmap_config rp1_pinctrl_gpio_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.rd_table = &rp1_gpio_reg_table,
	.name = "rp1-gpio",
	.max_register = 0xb11c,
};

static const struct regmap_config rp1_pinctrl_rio_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.rd_table = &rp1_rio_reg_table,
	.name = "rp1-rio",
	.max_register = 0xb004,
};

static const struct regmap_config rp1_pinctrl_pads_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.rd_table = &rp1_pads_reg_table,
	.name = "rp1-pads",
	.max_register = 0x8050,
};

static int rp1_gen_regfield(struct device *dev,
			    const struct reg_field *array,
			    size_t array_size,
			    int reg_off,
			    int pin_off,
			    bool additive_offset,
			    struct regmap *regmap,
			    struct regmap_field *out[])
{
	struct reg_field regfield;
	int k;

	for (k = 0; k < array_size; k++) {
		regfield = array[k];
		regfield.reg = (additive_offset ? regfield.reg : 0) + reg_off;
		if (pin_off >= 0) {
			regfield.lsb = pin_off;
			regfield.msb = regfield.lsb;
		}
		out[k] = devm_regmap_field_alloc(dev, regmap, regfield);

		if (IS_ERR(out[k]))
			return PTR_ERR(out[k]);
	}

	return 0;
}

static int rp1_pinctrl_probe(struct platform_device *pdev)
{
	struct regmap *gpio_regmap, *rio_regmap, *pads_regmap;
	struct rp1_pinctrl *pc = &rp1_pinctrl_data;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct gpio_irq_chip *girq;
	int err, i;

	pc->dev = dev;
	pc->gpio_chip = rp1_gpio_chip;
	pc->gpio_chip.parent = dev;

	pc->gpio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->gpio_base))
		return dev_err_probe(dev, PTR_ERR(pc->gpio_base), "could not get GPIO IO memory\n");

	pc->rio_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(pc->rio_base))
		return dev_err_probe(dev, PTR_ERR(pc->rio_base), "could not get RIO IO memory\n");

	pc->pads_base = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(pc->pads_base))
		return dev_err_probe(dev, PTR_ERR(pc->pads_base), "could not get PADS IO memory\n");

	gpio_regmap = devm_regmap_init_mmio(dev, pc->gpio_base,
					    &rp1_pinctrl_gpio_regmap_cfg);
	if (IS_ERR(gpio_regmap))
		return dev_err_probe(dev, PTR_ERR(gpio_regmap), "could not init GPIO regmap\n");

	rio_regmap = devm_regmap_init_mmio(dev, pc->rio_base,
					   &rp1_pinctrl_rio_regmap_cfg);
	if (IS_ERR(rio_regmap))
		return dev_err_probe(dev, PTR_ERR(rio_regmap), "could not init RIO regmap\n");

	pads_regmap = devm_regmap_init_mmio(dev, pc->pads_base,
					    &rp1_pinctrl_pads_regmap_cfg);
	if (IS_ERR(pads_regmap))
		return dev_err_probe(dev, PTR_ERR(pads_regmap), "could not init PADS regmap\n");

	for (i = 0; i < RP1_NUM_BANKS; i++) {
		const struct rp1_iobank_desc *bank = &rp1_iobanks[i];
		int j;

		for (j = 0; j < bank->num_gpios; j++) {
			struct rp1_pin_info *pin =
				&pc->pins[bank->min_gpio + j];
			int reg_off;

			pin->num = bank->min_gpio + j;
			pin->bank = i;
			pin->offset = j;

			reg_off = bank->gpio_offset + pin->offset *
				  sizeof(u32) * 2;
			err = rp1_gen_regfield(dev,
					       rp1_gpio_fields,
					       ARRAY_SIZE(rp1_gpio_fields),
					       reg_off,
					       -1,
					       true,
					       gpio_regmap,
					       pin->gpio);

			if (err)
				return dev_err_probe(dev, err,
						     "Unable to allocate regmap for gpio\n");

			reg_off = bank->inte_offset;
			err = rp1_gen_regfield(dev,
					       rp1_inte_fields,
					       ARRAY_SIZE(rp1_inte_fields),
					       reg_off,
					       pin->offset,
					       true,
					       gpio_regmap,
					       pin->inte);

			if (err)
				return dev_err_probe(dev, err,
						     "Unable to allocate regmap for inte\n");

			reg_off = bank->rio_offset;
			err = rp1_gen_regfield(dev,
					       rp1_rio_fields,
					       ARRAY_SIZE(rp1_rio_fields),
					       reg_off,
					       pin->offset,
					       true,
					       rio_regmap,
					       pin->rio);

			if (err)
				return dev_err_probe(dev, err,
						     "Unable to allocate regmap for rio\n");

			reg_off = bank->pads_offset + pin->offset * sizeof(u32);
			err = rp1_gen_regfield(dev,
					       rp1_pad_fields,
					       ARRAY_SIZE(rp1_pad_fields),
					       reg_off,
					       -1,
					       false,
					       pads_regmap,
					       pin->pad);

			if (err)
				return dev_err_probe(dev, err,
						     "Unable to allocate regmap for pad\n");
		}

		raw_spin_lock_init(&pc->irq_lock[i]);
	}

	pc->pctl_dev = devm_pinctrl_register(dev, &rp1_pinctrl_desc, pc);
	if (IS_ERR(pc->pctl_dev))
		return dev_err_probe(dev, PTR_ERR(pc->pctl_dev),
				     "Could not register pin controller\n");

	girq = &pc->gpio_chip.irq;
	girq->chip = &rp1_gpio_irq_chip;
	girq->parent_handler = rp1_gpio_irq_handler;
	girq->num_parents = RP1_NUM_BANKS;
	girq->parents = pc->irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;

	/*
	 * Use the same handler for all groups: this is necessary
	 * since we use one gpiochip to cover all lines - the
	 * irq handler then needs to figure out which group and
	 * bank that was firing the IRQ and look up the per-group
	 * and bank data.
	 */
	for (i = 0; i < RP1_NUM_BANKS; i++) {
		pc->irq[i] = irq_of_parse_and_map(np, i);
		if (!pc->irq[i]) {
			girq->num_parents = i;
			break;
		}
	}

	platform_set_drvdata(pdev, pc);

	err = devm_gpiochip_add_data(dev, &pc->gpio_chip, pc);
	if (err)
		return dev_err_probe(dev, err, "could not add GPIO chip\n");

	pc->gpio_range = rp1_pinctrl_gpio_range;
	pc->gpio_range.base = pc->gpio_chip.base;
	pc->gpio_range.gc = &pc->gpio_chip;
	pinctrl_add_gpio_range(pc->pctl_dev, &pc->gpio_range);

	return 0;
}

static struct platform_driver rp1_pinctrl_driver = {
	.probe = rp1_pinctrl_probe,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = rp1_pinctrl_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(rp1_pinctrl_driver);

MODULE_AUTHOR("Phil Elwell <phil@raspberrypi.com>");
MODULE_AUTHOR("Andrea della Porta <andrea.porta@suse.com>");
MODULE_DESCRIPTION("RP1 pinctrl/gpio driver");
MODULE_LICENSE("GPL");
