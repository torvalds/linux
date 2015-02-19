/*
 * Cherryview/Braswell pinctrl driver
 *
 * Copyright (C) 2014, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This driver is based on the original Cherryview GPIO driver by
 *   Ning Li <ning.li@intel.com>
 *   Alan Cox <alan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/acpi.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>

#define CHV_INTSTAT			0x300
#define CHV_INTMASK			0x380

#define FAMILY_PAD_REGS_OFF		0x4400
#define FAMILY_PAD_REGS_SIZE		0x400
#define MAX_FAMILY_PAD_GPIO_NO		15
#define GPIO_REGS_SIZE			8

#define CHV_PADCTRL0			0x000
#define CHV_PADCTRL0_INTSEL_SHIFT	28
#define CHV_PADCTRL0_INTSEL_MASK	(0xf << CHV_PADCTRL0_INTSEL_SHIFT)
#define CHV_PADCTRL0_TERM_UP		BIT(23)
#define CHV_PADCTRL0_TERM_SHIFT		20
#define CHV_PADCTRL0_TERM_MASK		(7 << CHV_PADCTRL0_TERM_SHIFT)
#define CHV_PADCTRL0_TERM_20K		1
#define CHV_PADCTRL0_TERM_5K		2
#define CHV_PADCTRL0_TERM_1K		4
#define CHV_PADCTRL0_PMODE_SHIFT	16
#define CHV_PADCTRL0_PMODE_MASK		(0xf << CHV_PADCTRL0_PMODE_SHIFT)
#define CHV_PADCTRL0_GPIOEN		BIT(15)
#define CHV_PADCTRL0_GPIOCFG_SHIFT	8
#define CHV_PADCTRL0_GPIOCFG_MASK	(7 << CHV_PADCTRL0_GPIOCFG_SHIFT)
#define CHV_PADCTRL0_GPIOCFG_GPIO	0
#define CHV_PADCTRL0_GPIOCFG_GPO	1
#define CHV_PADCTRL0_GPIOCFG_GPI	2
#define CHV_PADCTRL0_GPIOCFG_HIZ	3
#define CHV_PADCTRL0_GPIOTXSTATE	BIT(1)
#define CHV_PADCTRL0_GPIORXSTATE	BIT(0)

#define CHV_PADCTRL1			0x004
#define CHV_PADCTRL1_CFGLOCK		BIT(31)
#define CHV_PADCTRL1_INVRXTX_SHIFT	4
#define CHV_PADCTRL1_INVRXTX_MASK	(0xf << CHV_PADCTRL1_INVRXTX_SHIFT)
#define CHV_PADCTRL1_INVRXTX_TXENABLE	(2 << CHV_PADCTRL1_INVRXTX_SHIFT)
#define CHV_PADCTRL1_ODEN		BIT(3)
#define CHV_PADCTRL1_INVRXTX_RXDATA	(4 << CHV_PADCTRL1_INVRXTX_SHIFT)
#define CHV_PADCTRL1_INTWAKECFG_MASK	7
#define CHV_PADCTRL1_INTWAKECFG_FALLING	1
#define CHV_PADCTRL1_INTWAKECFG_RISING	2
#define CHV_PADCTRL1_INTWAKECFG_BOTH	3
#define CHV_PADCTRL1_INTWAKECFG_LEVEL	4

/**
 * struct chv_alternate_function - A per group or per pin alternate function
 * @pin: Pin number (only used in per pin configs)
 * @mode: Mode the pin should be set in
 * @invert_oe: Invert OE for this pin
 */
struct chv_alternate_function {
	unsigned pin;
	u8 mode;
	bool invert_oe;
};

/**
 * struct chv_pincgroup - describes a CHV pin group
 * @name: Name of the group
 * @pins: An array of pins in this group
 * @npins: Number of pins in this group
 * @altfunc: Alternate function applied to all pins in this group
 * @overrides: Alternate function override per pin or %NULL if not used
 * @noverrides: Number of per pin alternate function overrides if
 *              @overrides != NULL.
 */
struct chv_pingroup {
	const char *name;
	const unsigned *pins;
	size_t npins;
	struct chv_alternate_function altfunc;
	const struct chv_alternate_function *overrides;
	size_t noverrides;
};

/**
 * struct chv_function - A CHV pinmux function
 * @name: Name of the function
 * @groups: An array of groups for this function
 * @ngroups: Number of groups in @groups
 */
struct chv_function {
	const char *name;
	const char * const *groups;
	size_t ngroups;
};

/**
 * struct chv_gpio_pinrange - A range of pins that can be used as GPIOs
 * @base: Start pin number
 * @npins: Number of pins in this range
 */
struct chv_gpio_pinrange {
	unsigned base;
	unsigned npins;
};

/**
 * struct chv_community - A community specific configuration
 * @uid: ACPI _UID used to match the community
 * @pins: All pins in this community
 * @npins: Number of pins
 * @groups: All groups in this community
 * @ngroups: Number of groups
 * @functions: All functions in this community
 * @nfunctions: Number of functions
 * @ngpios: Number of GPIOs in this community
 * @gpio_ranges: An array of GPIO ranges in this community
 * @ngpio_ranges: Number of GPIO ranges
 * @ngpios: Total number of GPIOs in this community
 */
struct chv_community {
	const char *uid;
	const struct pinctrl_pin_desc *pins;
	size_t npins;
	const struct chv_pingroup *groups;
	size_t ngroups;
	const struct chv_function *functions;
	size_t nfunctions;
	const struct chv_gpio_pinrange *gpio_ranges;
	size_t ngpio_ranges;
	size_t ngpios;
};

struct chv_pin_context {
	u32 padctrl0;
	u32 padctrl1;
};

/**
 * struct chv_pinctrl - CHV pinctrl private structure
 * @dev: Pointer to the parent device
 * @pctldesc: Pin controller description
 * @pctldev: Pointer to the pin controller device
 * @chip: GPIO chip in this pin controller
 * @regs: MMIO registers
 * @lock: Lock to serialize register accesses
 * @intr_lines: Stores mapping between 16 HW interrupt wires and GPIO
 *		offset (in GPIO number space)
 * @community: Community this pinctrl instance represents
 *
 * The first group in @groups is expected to contain all pins that can be
 * used as GPIOs.
 */
struct chv_pinctrl {
	struct device *dev;
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;
	struct gpio_chip chip;
	void __iomem *regs;
	spinlock_t lock;
	unsigned intr_lines[16];
	const struct chv_community *community;
	u32 saved_intmask;
	struct chv_pin_context *saved_pin_context;
};

#define gpiochip_to_pinctrl(c) container_of(c, struct chv_pinctrl, chip)

#define ALTERNATE_FUNCTION(p, m, i)		\
	{					\
		.pin = (p),			\
		.mode = (m),			\
		.invert_oe = (i),		\
	}

#define PIN_GROUP(n, p, m, i)			\
	{					\
		.name = (n),			\
		.pins = (p),			\
		.npins = ARRAY_SIZE((p)),	\
		.altfunc.mode = (m),		\
		.altfunc.invert_oe = (i),	\
	}

#define PIN_GROUP_WITH_OVERRIDE(n, p, m, i, o)	\
	{					\
		.name = (n),			\
		.pins = (p),			\
		.npins = ARRAY_SIZE((p)),	\
		.altfunc.mode = (m),		\
		.altfunc.invert_oe = (i),	\
		.overrides = (o),		\
		.noverrides = ARRAY_SIZE((o)),	\
	}

#define FUNCTION(n, g)				\
	{					\
		.name = (n),			\
		.groups = (g),			\
		.ngroups = ARRAY_SIZE((g)),	\
	}

#define GPIO_PINRANGE(start, end)		\
	{					\
		.base = (start),		\
		.npins = (end) - (start) + 1,	\
	}

static const struct pinctrl_pin_desc southwest_pins[] = {
	PINCTRL_PIN(0, "FST_SPI_D2"),
	PINCTRL_PIN(1, "FST_SPI_D0"),
	PINCTRL_PIN(2, "FST_SPI_CLK"),
	PINCTRL_PIN(3, "FST_SPI_D3"),
	PINCTRL_PIN(4, "FST_SPI_CS1_B"),
	PINCTRL_PIN(5, "FST_SPI_D1"),
	PINCTRL_PIN(6, "FST_SPI_CS0_B"),
	PINCTRL_PIN(7, "FST_SPI_CS2_B"),

	PINCTRL_PIN(15, "UART1_RTS_B"),
	PINCTRL_PIN(16, "UART1_RXD"),
	PINCTRL_PIN(17, "UART2_RXD"),
	PINCTRL_PIN(18, "UART1_CTS_B"),
	PINCTRL_PIN(19, "UART2_RTS_B"),
	PINCTRL_PIN(20, "UART1_TXD"),
	PINCTRL_PIN(21, "UART2_TXD"),
	PINCTRL_PIN(22, "UART2_CTS_B"),

	PINCTRL_PIN(30, "MF_HDA_CLK"),
	PINCTRL_PIN(31, "MF_HDA_RSTB"),
	PINCTRL_PIN(32, "MF_HDA_SDIO"),
	PINCTRL_PIN(33, "MF_HDA_SDO"),
	PINCTRL_PIN(34, "MF_HDA_DOCKRSTB"),
	PINCTRL_PIN(35, "MF_HDA_SYNC"),
	PINCTRL_PIN(36, "MF_HDA_SDI1"),
	PINCTRL_PIN(37, "MF_HDA_DOCKENB"),

	PINCTRL_PIN(45, "I2C5_SDA"),
	PINCTRL_PIN(46, "I2C4_SDA"),
	PINCTRL_PIN(47, "I2C6_SDA"),
	PINCTRL_PIN(48, "I2C5_SCL"),
	PINCTRL_PIN(49, "I2C_NFC_SDA"),
	PINCTRL_PIN(50, "I2C4_SCL"),
	PINCTRL_PIN(51, "I2C6_SCL"),
	PINCTRL_PIN(52, "I2C_NFC_SCL"),

	PINCTRL_PIN(60, "I2C1_SDA"),
	PINCTRL_PIN(61, "I2C0_SDA"),
	PINCTRL_PIN(62, "I2C2_SDA"),
	PINCTRL_PIN(63, "I2C1_SCL"),
	PINCTRL_PIN(64, "I2C3_SDA"),
	PINCTRL_PIN(65, "I2C0_SCL"),
	PINCTRL_PIN(66, "I2C2_SCL"),
	PINCTRL_PIN(67, "I2C3_SCL"),

	PINCTRL_PIN(75, "SATA_GP0"),
	PINCTRL_PIN(76, "SATA_GP1"),
	PINCTRL_PIN(77, "SATA_LEDN"),
	PINCTRL_PIN(78, "SATA_GP2"),
	PINCTRL_PIN(79, "MF_SMB_ALERTB"),
	PINCTRL_PIN(80, "SATA_GP3"),
	PINCTRL_PIN(81, "MF_SMB_CLK"),
	PINCTRL_PIN(82, "MF_SMB_DATA"),

	PINCTRL_PIN(90, "PCIE_CLKREQ0B"),
	PINCTRL_PIN(91, "PCIE_CLKREQ1B"),
	PINCTRL_PIN(92, "GP_SSP_2_CLK"),
	PINCTRL_PIN(93, "PCIE_CLKREQ2B"),
	PINCTRL_PIN(94, "GP_SSP_2_RXD"),
	PINCTRL_PIN(95, "PCIE_CLKREQ3B"),
	PINCTRL_PIN(96, "GP_SSP_2_FS"),
	PINCTRL_PIN(97, "GP_SSP_2_TXD"),
};

static const unsigned southwest_fspi_pins[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const unsigned southwest_uart0_pins[] = { 16, 20 };
static const unsigned southwest_uart1_pins[] = { 15, 16, 18, 20 };
static const unsigned southwest_uart2_pins[] = { 17, 19, 21, 22 };
static const unsigned southwest_i2c0_pins[] = { 61, 65 };
static const unsigned southwest_hda_pins[] = { 30, 31, 32, 33, 34, 35, 36, 37 };
static const unsigned southwest_lpe_pins[] = {
	30, 31, 32, 33, 34, 35, 36, 37, 92, 94, 96, 97,
};
static const unsigned southwest_i2c1_pins[] = { 60, 63 };
static const unsigned southwest_i2c2_pins[] = { 62, 66 };
static const unsigned southwest_i2c3_pins[] = { 64, 67 };
static const unsigned southwest_i2c4_pins[] = { 46, 50 };
static const unsigned southwest_i2c5_pins[] = { 45, 48 };
static const unsigned southwest_i2c6_pins[] = { 47, 51 };
static const unsigned southwest_i2c_nfc_pins[] = { 49, 52 };
static const unsigned southwest_smbus_pins[] = { 79, 81, 82 };
static const unsigned southwest_spi3_pins[] = { 76, 79, 80, 81, 82 };

/* LPE I2S TXD pins need to have invert_oe set */
static const struct chv_alternate_function southwest_lpe_altfuncs[] = {
	ALTERNATE_FUNCTION(30, 1, true),
	ALTERNATE_FUNCTION(34, 1, true),
	ALTERNATE_FUNCTION(97, 1, true),
};

/*
 * Two spi3 chipselects are available in different mode than the main spi3
 * functionality, which is using mode 1.
 */
static const struct chv_alternate_function southwest_spi3_altfuncs[] = {
	ALTERNATE_FUNCTION(76, 3, false),
	ALTERNATE_FUNCTION(80, 3, false),
};

static const struct chv_pingroup southwest_groups[] = {
	PIN_GROUP("uart0_grp", southwest_uart0_pins, 2, false),
	PIN_GROUP("uart1_grp", southwest_uart1_pins, 1, false),
	PIN_GROUP("uart2_grp", southwest_uart2_pins, 1, false),
	PIN_GROUP("hda_grp", southwest_hda_pins, 2, false),
	PIN_GROUP("i2c0_grp", southwest_i2c0_pins, 1, true),
	PIN_GROUP("i2c1_grp", southwest_i2c1_pins, 1, true),
	PIN_GROUP("i2c2_grp", southwest_i2c2_pins, 1, true),
	PIN_GROUP("i2c3_grp", southwest_i2c3_pins, 1, true),
	PIN_GROUP("i2c4_grp", southwest_i2c4_pins, 1, true),
	PIN_GROUP("i2c5_grp", southwest_i2c5_pins, 1, true),
	PIN_GROUP("i2c6_grp", southwest_i2c6_pins, 1, true),
	PIN_GROUP("i2c_nfc_grp", southwest_i2c_nfc_pins, 2, true),

	PIN_GROUP_WITH_OVERRIDE("lpe_grp", southwest_lpe_pins, 1, false,
				southwest_lpe_altfuncs),
	PIN_GROUP_WITH_OVERRIDE("spi3_grp", southwest_spi3_pins, 2, false,
				southwest_spi3_altfuncs),
};

static const char * const southwest_uart0_groups[] = { "uart0_grp" };
static const char * const southwest_uart1_groups[] = { "uart1_grp" };
static const char * const southwest_uart2_groups[] = { "uart2_grp" };
static const char * const southwest_hda_groups[] = { "hda_grp" };
static const char * const southwest_lpe_groups[] = { "lpe_grp" };
static const char * const southwest_i2c0_groups[] = { "i2c0_grp" };
static const char * const southwest_i2c1_groups[] = { "i2c1_grp" };
static const char * const southwest_i2c2_groups[] = { "i2c2_grp" };
static const char * const southwest_i2c3_groups[] = { "i2c3_grp" };
static const char * const southwest_i2c4_groups[] = { "i2c4_grp" };
static const char * const southwest_i2c5_groups[] = { "i2c5_grp" };
static const char * const southwest_i2c6_groups[] = { "i2c6_grp" };
static const char * const southwest_i2c_nfc_groups[] = { "i2c_nfc_grp" };
static const char * const southwest_spi3_groups[] = { "spi3_grp" };

/*
 * Only do pinmuxing for certain LPSS devices for now. Rest of the pins are
 * enabled only as GPIOs.
 */
static const struct chv_function southwest_functions[] = {
	FUNCTION("uart0", southwest_uart0_groups),
	FUNCTION("uart1", southwest_uart1_groups),
	FUNCTION("uart2", southwest_uart2_groups),
	FUNCTION("hda", southwest_hda_groups),
	FUNCTION("lpe", southwest_lpe_groups),
	FUNCTION("i2c0", southwest_i2c0_groups),
	FUNCTION("i2c1", southwest_i2c1_groups),
	FUNCTION("i2c2", southwest_i2c2_groups),
	FUNCTION("i2c3", southwest_i2c3_groups),
	FUNCTION("i2c4", southwest_i2c4_groups),
	FUNCTION("i2c5", southwest_i2c5_groups),
	FUNCTION("i2c6", southwest_i2c6_groups),
	FUNCTION("i2c_nfc", southwest_i2c_nfc_groups),
	FUNCTION("spi3", southwest_spi3_groups),
};

static const struct chv_gpio_pinrange southwest_gpio_ranges[] = {
	GPIO_PINRANGE(0, 7),
	GPIO_PINRANGE(15, 22),
	GPIO_PINRANGE(30, 37),
	GPIO_PINRANGE(45, 52),
	GPIO_PINRANGE(60, 67),
	GPIO_PINRANGE(75, 82),
	GPIO_PINRANGE(90, 97),
};

static const struct chv_community southwest_community = {
	.uid = "1",
	.pins = southwest_pins,
	.npins = ARRAY_SIZE(southwest_pins),
	.groups = southwest_groups,
	.ngroups = ARRAY_SIZE(southwest_groups),
	.functions = southwest_functions,
	.nfunctions = ARRAY_SIZE(southwest_functions),
	.gpio_ranges = southwest_gpio_ranges,
	.ngpio_ranges = ARRAY_SIZE(southwest_gpio_ranges),
	.ngpios = ARRAY_SIZE(southwest_pins),
};

static const struct pinctrl_pin_desc north_pins[] = {
	PINCTRL_PIN(0, "GPIO_DFX_0"),
	PINCTRL_PIN(1, "GPIO_DFX_3"),
	PINCTRL_PIN(2, "GPIO_DFX_7"),
	PINCTRL_PIN(3, "GPIO_DFX_1"),
	PINCTRL_PIN(4, "GPIO_DFX_5"),
	PINCTRL_PIN(5, "GPIO_DFX_4"),
	PINCTRL_PIN(6, "GPIO_DFX_8"),
	PINCTRL_PIN(7, "GPIO_DFX_2"),
	PINCTRL_PIN(8, "GPIO_DFX_6"),

	PINCTRL_PIN(15, "GPIO_SUS0"),
	PINCTRL_PIN(16, "SEC_GPIO_SUS10"),
	PINCTRL_PIN(17, "GPIO_SUS3"),
	PINCTRL_PIN(18, "GPIO_SUS7"),
	PINCTRL_PIN(19, "GPIO_SUS1"),
	PINCTRL_PIN(20, "GPIO_SUS5"),
	PINCTRL_PIN(21, "SEC_GPIO_SUS11"),
	PINCTRL_PIN(22, "GPIO_SUS4"),
	PINCTRL_PIN(23, "SEC_GPIO_SUS8"),
	PINCTRL_PIN(24, "GPIO_SUS2"),
	PINCTRL_PIN(25, "GPIO_SUS6"),
	PINCTRL_PIN(26, "CX_PREQ_B"),
	PINCTRL_PIN(27, "SEC_GPIO_SUS9"),

	PINCTRL_PIN(30, "TRST_B"),
	PINCTRL_PIN(31, "TCK"),
	PINCTRL_PIN(32, "PROCHOT_B"),
	PINCTRL_PIN(33, "SVIDO_DATA"),
	PINCTRL_PIN(34, "TMS"),
	PINCTRL_PIN(35, "CX_PRDY_B_2"),
	PINCTRL_PIN(36, "TDO_2"),
	PINCTRL_PIN(37, "CX_PRDY_B"),
	PINCTRL_PIN(38, "SVIDO_ALERT_B"),
	PINCTRL_PIN(39, "TDO"),
	PINCTRL_PIN(40, "SVIDO_CLK"),
	PINCTRL_PIN(41, "TDI"),

	PINCTRL_PIN(45, "GP_CAMERASB_05"),
	PINCTRL_PIN(46, "GP_CAMERASB_02"),
	PINCTRL_PIN(47, "GP_CAMERASB_08"),
	PINCTRL_PIN(48, "GP_CAMERASB_00"),
	PINCTRL_PIN(49, "GP_CAMERASB_06"),
	PINCTRL_PIN(50, "GP_CAMERASB_10"),
	PINCTRL_PIN(51, "GP_CAMERASB_03"),
	PINCTRL_PIN(52, "GP_CAMERASB_09"),
	PINCTRL_PIN(53, "GP_CAMERASB_01"),
	PINCTRL_PIN(54, "GP_CAMERASB_07"),
	PINCTRL_PIN(55, "GP_CAMERASB_11"),
	PINCTRL_PIN(56, "GP_CAMERASB_04"),

	PINCTRL_PIN(60, "PANEL0_BKLTEN"),
	PINCTRL_PIN(61, "HV_DDI0_HPD"),
	PINCTRL_PIN(62, "HV_DDI2_DDC_SDA"),
	PINCTRL_PIN(63, "PANEL1_BKLTCTL"),
	PINCTRL_PIN(64, "HV_DDI1_HPD"),
	PINCTRL_PIN(65, "PANEL0_BKLTCTL"),
	PINCTRL_PIN(66, "HV_DDI0_DDC_SDA"),
	PINCTRL_PIN(67, "HV_DDI2_DDC_SCL"),
	PINCTRL_PIN(68, "HV_DDI2_HPD"),
	PINCTRL_PIN(69, "PANEL1_VDDEN"),
	PINCTRL_PIN(70, "PANEL1_BKLTEN"),
	PINCTRL_PIN(71, "HV_DDI0_DDC_SCL"),
	PINCTRL_PIN(72, "PANEL0_VDDEN"),
};

static const struct chv_gpio_pinrange north_gpio_ranges[] = {
	GPIO_PINRANGE(0, 8),
	GPIO_PINRANGE(15, 27),
	GPIO_PINRANGE(30, 41),
	GPIO_PINRANGE(45, 56),
	GPIO_PINRANGE(60, 72),
};

static const struct chv_community north_community = {
	.uid = "2",
	.pins = north_pins,
	.npins = ARRAY_SIZE(north_pins),
	.gpio_ranges = north_gpio_ranges,
	.ngpio_ranges = ARRAY_SIZE(north_gpio_ranges),
	.ngpios = ARRAY_SIZE(north_pins),
};

static const struct pinctrl_pin_desc east_pins[] = {
	PINCTRL_PIN(0, "PMU_SLP_S3_B"),
	PINCTRL_PIN(1, "PMU_BATLOW_B"),
	PINCTRL_PIN(2, "SUS_STAT_B"),
	PINCTRL_PIN(3, "PMU_SLP_S0IX_B"),
	PINCTRL_PIN(4, "PMU_AC_PRESENT"),
	PINCTRL_PIN(5, "PMU_PLTRST_B"),
	PINCTRL_PIN(6, "PMU_SUSCLK"),
	PINCTRL_PIN(7, "PMU_SLP_LAN_B"),
	PINCTRL_PIN(8, "PMU_PWRBTN_B"),
	PINCTRL_PIN(9, "PMU_SLP_S4_B"),
	PINCTRL_PIN(10, "PMU_WAKE_B"),
	PINCTRL_PIN(11, "PMU_WAKE_LAN_B"),

	PINCTRL_PIN(15, "MF_ISH_GPIO_3"),
	PINCTRL_PIN(16, "MF_ISH_GPIO_7"),
	PINCTRL_PIN(17, "MF_ISH_I2C1_SCL"),
	PINCTRL_PIN(18, "MF_ISH_GPIO_1"),
	PINCTRL_PIN(19, "MF_ISH_GPIO_5"),
	PINCTRL_PIN(20, "MF_ISH_GPIO_9"),
	PINCTRL_PIN(21, "MF_ISH_GPIO_0"),
	PINCTRL_PIN(22, "MF_ISH_GPIO_4"),
	PINCTRL_PIN(23, "MF_ISH_GPIO_8"),
	PINCTRL_PIN(24, "MF_ISH_GPIO_2"),
	PINCTRL_PIN(25, "MF_ISH_GPIO_6"),
	PINCTRL_PIN(26, "MF_ISH_I2C1_SDA"),
};

static const struct chv_gpio_pinrange east_gpio_ranges[] = {
	GPIO_PINRANGE(0, 11),
	GPIO_PINRANGE(15, 26),
};

static const struct chv_community east_community = {
	.uid = "3",
	.pins = east_pins,
	.npins = ARRAY_SIZE(east_pins),
	.gpio_ranges = east_gpio_ranges,
	.ngpio_ranges = ARRAY_SIZE(east_gpio_ranges),
	.ngpios = ARRAY_SIZE(east_pins),
};

static const struct pinctrl_pin_desc southeast_pins[] = {
	PINCTRL_PIN(0, "MF_PLT_CLK0"),
	PINCTRL_PIN(1, "PWM1"),
	PINCTRL_PIN(2, "MF_PLT_CLK1"),
	PINCTRL_PIN(3, "MF_PLT_CLK4"),
	PINCTRL_PIN(4, "MF_PLT_CLK3"),
	PINCTRL_PIN(5, "PWM0"),
	PINCTRL_PIN(6, "MF_PLT_CLK5"),
	PINCTRL_PIN(7, "MF_PLT_CLK2"),

	PINCTRL_PIN(15, "SDMMC2_D3_CD_B"),
	PINCTRL_PIN(16, "SDMMC1_CLK"),
	PINCTRL_PIN(17, "SDMMC1_D0"),
	PINCTRL_PIN(18, "SDMMC2_D1"),
	PINCTRL_PIN(19, "SDMMC2_CLK"),
	PINCTRL_PIN(20, "SDMMC1_D2"),
	PINCTRL_PIN(21, "SDMMC2_D2"),
	PINCTRL_PIN(22, "SDMMC2_CMD"),
	PINCTRL_PIN(23, "SDMMC1_CMD"),
	PINCTRL_PIN(24, "SDMMC1_D1"),
	PINCTRL_PIN(25, "SDMMC2_D0"),
	PINCTRL_PIN(26, "SDMMC1_D3_CD_B"),

	PINCTRL_PIN(30, "SDMMC3_D1"),
	PINCTRL_PIN(31, "SDMMC3_CLK"),
	PINCTRL_PIN(32, "SDMMC3_D3"),
	PINCTRL_PIN(33, "SDMMC3_D2"),
	PINCTRL_PIN(34, "SDMMC3_CMD"),
	PINCTRL_PIN(35, "SDMMC3_D0"),

	PINCTRL_PIN(45, "MF_LPC_AD2"),
	PINCTRL_PIN(46, "LPC_CLKRUNB"),
	PINCTRL_PIN(47, "MF_LPC_AD0"),
	PINCTRL_PIN(48, "LPC_FRAMEB"),
	PINCTRL_PIN(49, "MF_LPC_CLKOUT1"),
	PINCTRL_PIN(50, "MF_LPC_AD3"),
	PINCTRL_PIN(51, "MF_LPC_CLKOUT0"),
	PINCTRL_PIN(52, "MF_LPC_AD1"),

	PINCTRL_PIN(60, "SPI1_MISO"),
	PINCTRL_PIN(61, "SPI1_CSO_B"),
	PINCTRL_PIN(62, "SPI1_CLK"),
	PINCTRL_PIN(63, "MMC1_D6"),
	PINCTRL_PIN(64, "SPI1_MOSI"),
	PINCTRL_PIN(65, "MMC1_D5"),
	PINCTRL_PIN(66, "SPI1_CS1_B"),
	PINCTRL_PIN(67, "MMC1_D4_SD_WE"),
	PINCTRL_PIN(68, "MMC1_D7"),
	PINCTRL_PIN(69, "MMC1_RCLK"),

	PINCTRL_PIN(75, "USB_OC1_B"),
	PINCTRL_PIN(76, "PMU_RESETBUTTON_B"),
	PINCTRL_PIN(77, "GPIO_ALERT"),
	PINCTRL_PIN(78, "SDMMC3_PWR_EN_B"),
	PINCTRL_PIN(79, "ILB_SERIRQ"),
	PINCTRL_PIN(80, "USB_OC0_B"),
	PINCTRL_PIN(81, "SDMMC3_CD_B"),
	PINCTRL_PIN(82, "SPKR"),
	PINCTRL_PIN(83, "SUSPWRDNACK"),
	PINCTRL_PIN(84, "SPARE_PIN"),
	PINCTRL_PIN(85, "SDMMC3_1P8_EN"),
};

static const unsigned southeast_pwm0_pins[] = { 5 };
static const unsigned southeast_pwm1_pins[] = { 1 };
static const unsigned southeast_sdmmc1_pins[] = {
	16, 17, 20, 23, 24, 26, 63, 65, 67, 68, 69,
};
static const unsigned southeast_sdmmc2_pins[] = { 15, 18, 19, 21, 22, 25 };
static const unsigned southeast_sdmmc3_pins[] = {
	30, 31, 32, 33, 34, 35, 78, 81, 85,
};
static const unsigned southeast_spi1_pins[] = { 60, 61, 62, 64, 66 };
static const unsigned southeast_spi2_pins[] = { 2, 3, 4, 6, 7 };

static const struct chv_pingroup southeast_groups[] = {
	PIN_GROUP("pwm0_grp", southeast_pwm0_pins, 1, false),
	PIN_GROUP("pwm1_grp", southeast_pwm1_pins, 1, false),
	PIN_GROUP("sdmmc1_grp", southeast_sdmmc1_pins, 1, false),
	PIN_GROUP("sdmmc2_grp", southeast_sdmmc2_pins, 1, false),
	PIN_GROUP("sdmmc3_grp", southeast_sdmmc3_pins, 1, false),
	PIN_GROUP("spi1_grp", southeast_spi1_pins, 1, false),
	PIN_GROUP("spi2_grp", southeast_spi2_pins, 4, false),
};

static const char * const southeast_pwm0_groups[] = { "pwm0_grp" };
static const char * const southeast_pwm1_groups[] = { "pwm1_grp" };
static const char * const southeast_sdmmc1_groups[] = { "sdmmc1_grp" };
static const char * const southeast_sdmmc2_groups[] = { "sdmmc2_grp" };
static const char * const southeast_sdmmc3_groups[] = { "sdmmc3_grp" };
static const char * const southeast_spi1_groups[] = { "spi1_grp" };
static const char * const southeast_spi2_groups[] = { "spi2_grp" };

static const struct chv_function southeast_functions[] = {
	FUNCTION("pwm0", southeast_pwm0_groups),
	FUNCTION("pwm1", southeast_pwm1_groups),
	FUNCTION("sdmmc1", southeast_sdmmc1_groups),
	FUNCTION("sdmmc2", southeast_sdmmc2_groups),
	FUNCTION("sdmmc3", southeast_sdmmc3_groups),
	FUNCTION("spi1", southeast_spi1_groups),
	FUNCTION("spi2", southeast_spi2_groups),
};

static const struct chv_gpio_pinrange southeast_gpio_ranges[] = {
	GPIO_PINRANGE(0, 7),
	GPIO_PINRANGE(15, 26),
	GPIO_PINRANGE(30, 35),
	GPIO_PINRANGE(45, 52),
	GPIO_PINRANGE(60, 69),
	GPIO_PINRANGE(75, 85),
};

static const struct chv_community southeast_community = {
	.uid = "4",
	.pins = southeast_pins,
	.npins = ARRAY_SIZE(southeast_pins),
	.groups = southeast_groups,
	.ngroups = ARRAY_SIZE(southeast_groups),
	.functions = southeast_functions,
	.nfunctions = ARRAY_SIZE(southeast_functions),
	.gpio_ranges = southeast_gpio_ranges,
	.ngpio_ranges = ARRAY_SIZE(southeast_gpio_ranges),
	.ngpios = ARRAY_SIZE(southeast_pins),
};

static const struct chv_community *chv_communities[] = {
	&southwest_community,
	&north_community,
	&east_community,
	&southeast_community,
};

static void __iomem *chv_padreg(struct chv_pinctrl *pctrl, unsigned offset,
				unsigned reg)
{
	unsigned family_no = offset / MAX_FAMILY_PAD_GPIO_NO;
	unsigned pad_no = offset % MAX_FAMILY_PAD_GPIO_NO;

	offset = FAMILY_PAD_REGS_OFF + FAMILY_PAD_REGS_SIZE * family_no +
		 GPIO_REGS_SIZE * pad_no;

	return pctrl->regs + offset + reg;
}

static void chv_writel(u32 value, void __iomem *reg)
{
	writel(value, reg);
	/* simple readback to confirm the bus transferring done */
	readl(reg);
}

/* When Pad Cfg is locked, driver can only change GPIOTXState or GPIORXState */
static bool chv_pad_locked(struct chv_pinctrl *pctrl, unsigned offset)
{
	void __iomem *reg;

	reg = chv_padreg(pctrl, offset, CHV_PADCTRL1);
	return readl(reg) & CHV_PADCTRL1_CFGLOCK;
}

static int chv_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->community->ngroups;
}

static const char *chv_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned group)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->community->groups[group].name;
}

static int chv_get_group_pins(struct pinctrl_dev *pctldev, unsigned group,
			      const unsigned **pins, unsigned *npins)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pctrl->community->groups[group].pins;
	*npins = pctrl->community->groups[group].npins;
	return 0;
}

static void chv_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			     unsigned offset)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	u32 ctrl0, ctrl1;
	bool locked;

	spin_lock_irqsave(&pctrl->lock, flags);

	ctrl0 = readl(chv_padreg(pctrl, offset, CHV_PADCTRL0));
	ctrl1 = readl(chv_padreg(pctrl, offset, CHV_PADCTRL1));
	locked = chv_pad_locked(pctrl, offset);

	spin_unlock_irqrestore(&pctrl->lock, flags);

	if (ctrl0 & CHV_PADCTRL0_GPIOEN) {
		seq_puts(s, "GPIO ");
	} else {
		u32 mode;

		mode = ctrl0 & CHV_PADCTRL0_PMODE_MASK;
		mode >>= CHV_PADCTRL0_PMODE_SHIFT;

		seq_printf(s, "mode %d ", mode);
	}

	seq_printf(s, "ctrl0 0x%08x ctrl1 0x%08x", ctrl0, ctrl1);

	if (locked)
		seq_puts(s, " [LOCKED]");
}

static const struct pinctrl_ops chv_pinctrl_ops = {
	.get_groups_count = chv_get_groups_count,
	.get_group_name = chv_get_group_name,
	.get_group_pins = chv_get_group_pins,
	.pin_dbg_show = chv_pin_dbg_show,
};

static int chv_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->community->nfunctions;
}

static const char *chv_get_function_name(struct pinctrl_dev *pctldev,
					 unsigned function)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->community->functions[function].name;
}

static int chv_get_function_groups(struct pinctrl_dev *pctldev,
				   unsigned function,
				   const char * const **groups,
				   unsigned * const ngroups)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctrl->community->functions[function].groups;
	*ngroups = pctrl->community->functions[function].ngroups;
	return 0;
}

static int chv_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned function,
			      unsigned group)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct chv_pingroup *grp;
	unsigned long flags;
	int i;

	grp = &pctrl->community->groups[group];

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Check first that the pad is not locked */
	for (i = 0; i < grp->npins; i++) {
		if (chv_pad_locked(pctrl, grp->pins[i])) {
			dev_warn(pctrl->dev, "unable to set mode for locked pin %u\n",
				 grp->pins[i]);
			spin_unlock_irqrestore(&pctrl->lock, flags);
			return -EBUSY;
		}
	}

	for (i = 0; i < grp->npins; i++) {
		const struct chv_alternate_function *altfunc = &grp->altfunc;
		int pin = grp->pins[i];
		void __iomem *reg;
		u32 value;

		/* Check if there is pin-specific config */
		if (grp->overrides) {
			int j;

			for (j = 0; j < grp->noverrides; j++) {
				if (grp->overrides[j].pin == pin) {
					altfunc = &grp->overrides[j];
					break;
				}
			}
		}

		reg = chv_padreg(pctrl, pin, CHV_PADCTRL0);
		value = readl(reg);
		/* Disable GPIO mode */
		value &= ~CHV_PADCTRL0_GPIOEN;
		/* Set to desired mode */
		value &= ~CHV_PADCTRL0_PMODE_MASK;
		value |= altfunc->mode << CHV_PADCTRL0_PMODE_SHIFT;
		chv_writel(value, reg);

		/* Update for invert_oe */
		reg = chv_padreg(pctrl, pin, CHV_PADCTRL1);
		value = readl(reg) & ~CHV_PADCTRL1_INVRXTX_MASK;
		if (altfunc->invert_oe)
			value |= CHV_PADCTRL1_INVRXTX_TXENABLE;
		chv_writel(value, reg);

		dev_dbg(pctrl->dev, "configured pin %u mode %u OE %sinverted\n",
			pin, altfunc->mode, altfunc->invert_oe ? "" : "not ");
	}

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int chv_gpio_request_enable(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned offset)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	void __iomem *reg;
	u32 value;

	spin_lock_irqsave(&pctrl->lock, flags);

	if (chv_pad_locked(pctrl, offset)) {
		value = readl(chv_padreg(pctrl, offset, CHV_PADCTRL0));
		if (!(value & CHV_PADCTRL0_GPIOEN)) {
			/* Locked so cannot enable */
			spin_unlock_irqrestore(&pctrl->lock, flags);
			return -EBUSY;
		}
	} else {
		int i;

		/* Reset the interrupt mapping */
		for (i = 0; i < ARRAY_SIZE(pctrl->intr_lines); i++) {
			if (pctrl->intr_lines[i] == offset) {
				pctrl->intr_lines[i] = 0;
				break;
			}
		}

		/* Disable interrupt generation */
		reg = chv_padreg(pctrl, offset, CHV_PADCTRL1);
		value = readl(reg);
		value &= ~CHV_PADCTRL1_INTWAKECFG_MASK;
		value &= ~CHV_PADCTRL1_INVRXTX_MASK;
		chv_writel(value, reg);

		reg = chv_padreg(pctrl, offset, CHV_PADCTRL0);
		value = readl(reg);

		/*
		 * If the pin is in HiZ mode (both TX and RX buffers are
		 * disabled) we turn it to be input now.
		 */
		if ((value & CHV_PADCTRL0_GPIOCFG_MASK) ==
		     (CHV_PADCTRL0_GPIOCFG_HIZ << CHV_PADCTRL0_GPIOCFG_SHIFT)) {
			value &= ~CHV_PADCTRL0_GPIOCFG_MASK;
			value |= CHV_PADCTRL0_GPIOCFG_GPI <<
				CHV_PADCTRL0_GPIOCFG_SHIFT;
		}

		/* Switch to a GPIO mode */
		value |= CHV_PADCTRL0_GPIOEN;
		chv_writel(value, reg);
	}

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static void chv_gpio_disable_free(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned offset)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	void __iomem *reg;
	u32 value;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg = chv_padreg(pctrl, offset, CHV_PADCTRL0);
	value = readl(reg) & ~CHV_PADCTRL0_GPIOEN;
	chv_writel(value, reg);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int chv_gpio_set_direction(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned offset, bool input)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	void __iomem *reg = chv_padreg(pctrl, offset, CHV_PADCTRL0);
	unsigned long flags;
	u32 ctrl0;

	spin_lock_irqsave(&pctrl->lock, flags);

	ctrl0 = readl(reg) & ~CHV_PADCTRL0_GPIOCFG_MASK;
	if (input)
		ctrl0 |= CHV_PADCTRL0_GPIOCFG_GPI << CHV_PADCTRL0_GPIOCFG_SHIFT;
	else
		ctrl0 |= CHV_PADCTRL0_GPIOCFG_GPO << CHV_PADCTRL0_GPIOCFG_SHIFT;
	chv_writel(ctrl0, reg);

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static const struct pinmux_ops chv_pinmux_ops = {
	.get_functions_count = chv_get_functions_count,
	.get_function_name = chv_get_function_name,
	.get_function_groups = chv_get_function_groups,
	.set_mux = chv_pinmux_set_mux,
	.gpio_request_enable = chv_gpio_request_enable,
	.gpio_disable_free = chv_gpio_disable_free,
	.gpio_set_direction = chv_gpio_set_direction,
};

static int chv_config_get(struct pinctrl_dev *pctldev, unsigned pin,
			  unsigned long *config)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned long flags;
	u32 ctrl0, ctrl1;
	u16 arg = 0;
	u32 term;

	spin_lock_irqsave(&pctrl->lock, flags);
	ctrl0 = readl(chv_padreg(pctrl, pin, CHV_PADCTRL0));
	ctrl1 = readl(chv_padreg(pctrl, pin, CHV_PADCTRL1));
	spin_unlock_irqrestore(&pctrl->lock, flags);

	term = (ctrl0 & CHV_PADCTRL0_TERM_MASK) >> CHV_PADCTRL0_TERM_SHIFT;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (term)
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		if (!(ctrl0 & CHV_PADCTRL0_TERM_UP))
			return -EINVAL;

		switch (term) {
		case CHV_PADCTRL0_TERM_20K:
			arg = 20000;
			break;
		case CHV_PADCTRL0_TERM_5K:
			arg = 5000;
			break;
		case CHV_PADCTRL0_TERM_1K:
			arg = 1000;
			break;
		}

		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!term || (ctrl0 & CHV_PADCTRL0_TERM_UP))
			return -EINVAL;

		switch (term) {
		case CHV_PADCTRL0_TERM_20K:
			arg = 20000;
			break;
		case CHV_PADCTRL0_TERM_5K:
			arg = 5000;
			break;
		}

		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (!(ctrl1 & CHV_PADCTRL1_ODEN))
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE: {
		u32 cfg;

		cfg = ctrl0 & CHV_PADCTRL0_GPIOCFG_MASK;
		cfg >>= CHV_PADCTRL0_GPIOCFG_SHIFT;
		if (cfg != CHV_PADCTRL0_GPIOCFG_HIZ)
			return -EINVAL;

		break;
	}

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int chv_config_set_pull(struct chv_pinctrl *pctrl, unsigned pin,
			       enum pin_config_param param, u16 arg)
{
	void __iomem *reg = chv_padreg(pctrl, pin, CHV_PADCTRL0);
	unsigned long flags;
	u32 ctrl0, pull;

	spin_lock_irqsave(&pctrl->lock, flags);
	ctrl0 = readl(reg);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		ctrl0 &= ~(CHV_PADCTRL0_TERM_MASK | CHV_PADCTRL0_TERM_UP);
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		ctrl0 &= ~(CHV_PADCTRL0_TERM_MASK | CHV_PADCTRL0_TERM_UP);

		switch (arg) {
		case 1000:
			/* For 1k there is only pull up */
			pull = CHV_PADCTRL0_TERM_1K << CHV_PADCTRL0_TERM_SHIFT;
			break;
		case 5000:
			pull = CHV_PADCTRL0_TERM_5K << CHV_PADCTRL0_TERM_SHIFT;
			break;
		case 20000:
			pull = CHV_PADCTRL0_TERM_20K << CHV_PADCTRL0_TERM_SHIFT;
			break;
		default:
			spin_unlock_irqrestore(&pctrl->lock, flags);
			return -EINVAL;
		}

		ctrl0 |= CHV_PADCTRL0_TERM_UP | pull;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		ctrl0 &= ~(CHV_PADCTRL0_TERM_MASK | CHV_PADCTRL0_TERM_UP);

		switch (arg) {
		case 5000:
			pull = CHV_PADCTRL0_TERM_5K << CHV_PADCTRL0_TERM_SHIFT;
			break;
		case 20000:
			pull = CHV_PADCTRL0_TERM_20K << CHV_PADCTRL0_TERM_SHIFT;
			break;
		default:
			spin_unlock_irqrestore(&pctrl->lock, flags);
			return -EINVAL;
		}

		ctrl0 |= pull;
		break;

	default:
		spin_unlock_irqrestore(&pctrl->lock, flags);
		return -EINVAL;
	}

	chv_writel(ctrl0, reg);
	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int chv_config_set(struct pinctrl_dev *pctldev, unsigned pin,
			  unsigned long *configs, unsigned nconfigs)
{
	struct chv_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	int i, ret;
	u16 arg;

	if (chv_pad_locked(pctrl, pin))
		return -EBUSY;

	for (i = 0; i < nconfigs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = chv_config_set_pull(pctrl, pin, param, arg);
			if (ret)
				return ret;
			break;

		default:
			return -ENOTSUPP;
		}

		dev_dbg(pctrl->dev, "pin %d set config %d arg %u\n", pin,
			param, arg);
	}

	return 0;
}

static const struct pinconf_ops chv_pinconf_ops = {
	.is_generic = true,
	.pin_config_set = chv_config_set,
	.pin_config_get = chv_config_get,
};

static struct pinctrl_desc chv_pinctrl_desc = {
	.pctlops = &chv_pinctrl_ops,
	.pmxops = &chv_pinmux_ops,
	.confops = &chv_pinconf_ops,
	.owner = THIS_MODULE,
};

static int chv_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void chv_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static unsigned chv_gpio_offset_to_pin(struct chv_pinctrl *pctrl,
				       unsigned offset)
{
	return pctrl->community->pins[offset].number;
}

static int chv_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct chv_pinctrl *pctrl = gpiochip_to_pinctrl(chip);
	int pin = chv_gpio_offset_to_pin(pctrl, offset);
	u32 ctrl0, cfg;

	ctrl0 = readl(chv_padreg(pctrl, pin, CHV_PADCTRL0));

	cfg = ctrl0 & CHV_PADCTRL0_GPIOCFG_MASK;
	cfg >>= CHV_PADCTRL0_GPIOCFG_SHIFT;

	if (cfg == CHV_PADCTRL0_GPIOCFG_GPO)
		return !!(ctrl0 & CHV_PADCTRL0_GPIOTXSTATE);
	return !!(ctrl0 & CHV_PADCTRL0_GPIORXSTATE);
}

static void chv_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct chv_pinctrl *pctrl = gpiochip_to_pinctrl(chip);
	unsigned pin = chv_gpio_offset_to_pin(pctrl, offset);
	unsigned long flags;
	void __iomem *reg;
	u32 ctrl0;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg = chv_padreg(pctrl, pin, CHV_PADCTRL0);
	ctrl0 = readl(reg);

	if (value)
		ctrl0 |= CHV_PADCTRL0_GPIOTXSTATE;
	else
		ctrl0 &= ~CHV_PADCTRL0_GPIOTXSTATE;

	chv_writel(ctrl0, reg);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int chv_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct chv_pinctrl *pctrl = gpiochip_to_pinctrl(chip);
	unsigned pin = chv_gpio_offset_to_pin(pctrl, offset);
	u32 ctrl0, direction;

	ctrl0 = readl(chv_padreg(pctrl, pin, CHV_PADCTRL0));

	direction = ctrl0 & CHV_PADCTRL0_GPIOCFG_MASK;
	direction >>= CHV_PADCTRL0_GPIOCFG_SHIFT;

	return direction != CHV_PADCTRL0_GPIOCFG_GPO;
}

static int chv_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int chv_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				     int value)
{
	return pinctrl_gpio_direction_output(chip->base + offset);
}

static const struct gpio_chip chv_gpio_chip = {
	.owner = THIS_MODULE,
	.request = chv_gpio_request,
	.free = chv_gpio_free,
	.get_direction = chv_gpio_get_direction,
	.direction_input = chv_gpio_direction_input,
	.direction_output = chv_gpio_direction_output,
	.get = chv_gpio_get,
	.set = chv_gpio_set,
};

static void chv_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct chv_pinctrl *pctrl = gpiochip_to_pinctrl(gc);
	int pin = chv_gpio_offset_to_pin(pctrl, irqd_to_hwirq(d));
	u32 intr_line;

	spin_lock(&pctrl->lock);

	intr_line = readl(chv_padreg(pctrl, pin, CHV_PADCTRL0));
	intr_line &= CHV_PADCTRL0_INTSEL_MASK;
	intr_line >>= CHV_PADCTRL0_INTSEL_SHIFT;
	chv_writel(BIT(intr_line), pctrl->regs + CHV_INTSTAT);

	spin_unlock(&pctrl->lock);
}

static void chv_gpio_irq_mask_unmask(struct irq_data *d, bool mask)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct chv_pinctrl *pctrl = gpiochip_to_pinctrl(gc);
	int pin = chv_gpio_offset_to_pin(pctrl, irqd_to_hwirq(d));
	u32 value, intr_line;
	unsigned long flags;

	spin_lock_irqsave(&pctrl->lock, flags);

	intr_line = readl(chv_padreg(pctrl, pin, CHV_PADCTRL0));
	intr_line &= CHV_PADCTRL0_INTSEL_MASK;
	intr_line >>= CHV_PADCTRL0_INTSEL_SHIFT;

	value = readl(pctrl->regs + CHV_INTMASK);
	if (mask)
		value &= ~BIT(intr_line);
	else
		value |= BIT(intr_line);
	chv_writel(value, pctrl->regs + CHV_INTMASK);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void chv_gpio_irq_mask(struct irq_data *d)
{
	chv_gpio_irq_mask_unmask(d, true);
}

static void chv_gpio_irq_unmask(struct irq_data *d)
{
	chv_gpio_irq_mask_unmask(d, false);
}

static int chv_gpio_irq_type(struct irq_data *d, unsigned type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct chv_pinctrl *pctrl = gpiochip_to_pinctrl(gc);
	unsigned offset = irqd_to_hwirq(d);
	int pin = chv_gpio_offset_to_pin(pctrl, offset);
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&pctrl->lock, flags);

	/*
	 * Pins which can be used as shared interrupt are configured in
	 * BIOS. Driver trusts BIOS configurations and assigns different
	 * handler according to the irq type.
	 *
	 * Driver needs to save the mapping between each pin and
	 * its interrupt line.
	 * 1. If the pin cfg is locked in BIOS:
	 *	Trust BIOS has programmed IntWakeCfg bits correctly,
	 *	driver just needs to save the mapping.
	 * 2. If the pin cfg is not locked in BIOS:
	 *	Driver programs the IntWakeCfg bits and save the mapping.
	 */
	if (!chv_pad_locked(pctrl, pin)) {
		void __iomem *reg = chv_padreg(pctrl, pin, CHV_PADCTRL1);

		value = readl(reg);
		value &= ~CHV_PADCTRL1_INTWAKECFG_MASK;
		value &= ~CHV_PADCTRL1_INVRXTX_MASK;

		if (type & IRQ_TYPE_EDGE_BOTH) {
			if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
				value |= CHV_PADCTRL1_INTWAKECFG_BOTH;
			else if (type & IRQ_TYPE_EDGE_RISING)
				value |= CHV_PADCTRL1_INTWAKECFG_RISING;
			else if (type & IRQ_TYPE_EDGE_FALLING)
				value |= CHV_PADCTRL1_INTWAKECFG_FALLING;
		} else if (type & IRQ_TYPE_LEVEL_MASK) {
			value |= CHV_PADCTRL1_INTWAKECFG_LEVEL;
			if (type & IRQ_TYPE_LEVEL_LOW)
				value |= CHV_PADCTRL1_INVRXTX_RXDATA;
		}

		chv_writel(value, reg);
	}

	value = readl(chv_padreg(pctrl, pin, CHV_PADCTRL0));
	value &= CHV_PADCTRL0_INTSEL_MASK;
	value >>= CHV_PADCTRL0_INTSEL_SHIFT;

	pctrl->intr_lines[value] = offset;

	if (type & IRQ_TYPE_EDGE_BOTH)
		__irq_set_handler_locked(d->irq, handle_edge_irq);
	else if (type & IRQ_TYPE_LEVEL_MASK)
		__irq_set_handler_locked(d->irq, handle_level_irq);

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static struct irq_chip chv_gpio_irqchip = {
	.name = "chv-gpio",
	.irq_ack = chv_gpio_irq_ack,
	.irq_mask = chv_gpio_irq_mask,
	.irq_unmask = chv_gpio_irq_unmask,
	.irq_set_type = chv_gpio_irq_type,
	.flags = IRQCHIP_SKIP_SET_WAKE,
};

static void chv_gpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct chv_pinctrl *pctrl = gpiochip_to_pinctrl(gc);
	struct irq_chip *chip = irq_get_chip(irq);
	unsigned long pending;
	u32 intr_line;

	chained_irq_enter(chip, desc);

	pending = readl(pctrl->regs + CHV_INTSTAT);
	for_each_set_bit(intr_line, &pending, 16) {
		unsigned irq, offset;

		offset = pctrl->intr_lines[intr_line];
		irq = irq_find_mapping(gc->irqdomain, offset);
		generic_handle_irq(irq);
	}

	chained_irq_exit(chip, desc);
}

static int chv_gpio_probe(struct chv_pinctrl *pctrl, int irq)
{
	const struct chv_gpio_pinrange *range;
	struct gpio_chip *chip = &pctrl->chip;
	int ret, i, offset;

	*chip = chv_gpio_chip;

	chip->ngpio = pctrl->community->ngpios;
	chip->label = dev_name(pctrl->dev);
	chip->dev = pctrl->dev;
	chip->base = -1;

	ret = gpiochip_add(chip);
	if (ret) {
		dev_err(pctrl->dev, "Failed to register gpiochip\n");
		return ret;
	}

	for (i = 0, offset = 0; i < pctrl->community->ngpio_ranges; i++) {
		range = &pctrl->community->gpio_ranges[i];
		ret = gpiochip_add_pin_range(chip, dev_name(pctrl->dev), offset,
					     range->base, range->npins);
		if (ret) {
			dev_err(pctrl->dev, "failed to add GPIO pin range\n");
			goto fail;
		}

		offset += range->npins;
	}

	/* Mask and clear all interrupts */
	chv_writel(0, pctrl->regs + CHV_INTMASK);
	chv_writel(0xffff, pctrl->regs + CHV_INTSTAT);

	ret = gpiochip_irqchip_add(chip, &chv_gpio_irqchip, 0,
				   handle_simple_irq, IRQ_TYPE_NONE);
	if (ret) {
		dev_err(pctrl->dev, "failed to add IRQ chip\n");
		goto fail;
	}

	gpiochip_set_chained_irqchip(chip, &chv_gpio_irqchip, irq,
				     chv_gpio_irq_handler);
	return 0;

fail:
	gpiochip_remove(chip);

	return ret;
}

static int chv_pinctrl_probe(struct platform_device *pdev)
{
	struct chv_pinctrl *pctrl;
	struct acpi_device *adev;
	struct resource *res;
	int ret, irq, i;

	adev = ACPI_COMPANION(&pdev->dev);
	if (!adev)
		return -ENODEV;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(chv_communities); i++)
		if (!strcmp(adev->pnp.unique_id, chv_communities[i]->uid)) {
			pctrl->community = chv_communities[i];
			break;
		}
	if (i == ARRAY_SIZE(chv_communities))
		return -ENODEV;

	spin_lock_init(&pctrl->lock);
	pctrl->dev = &pdev->dev;

#ifdef CONFIG_PM_SLEEP
	pctrl->saved_pin_context = devm_kcalloc(pctrl->dev,
		pctrl->community->npins, sizeof(*pctrl->saved_pin_context),
		GFP_KERNEL);
	if (!pctrl->saved_pin_context)
		return -ENOMEM;
#endif

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pctrl->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pctrl->regs))
		return PTR_ERR(pctrl->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get interrupt number\n");
		return irq;
	}

	pctrl->pctldesc = chv_pinctrl_desc;
	pctrl->pctldesc.name = dev_name(&pdev->dev);
	pctrl->pctldesc.pins = pctrl->community->pins;
	pctrl->pctldesc.npins = pctrl->community->npins;

	pctrl->pctldev = pinctrl_register(&pctrl->pctldesc, &pdev->dev, pctrl);
	if (!pctrl->pctldev) {
		dev_err(&pdev->dev, "failed to register pinctrl driver\n");
		return -ENODEV;
	}

	ret = chv_gpio_probe(pctrl, irq);
	if (ret) {
		pinctrl_unregister(pctrl->pctldev);
		return ret;
	}

	platform_set_drvdata(pdev, pctrl);

	return 0;
}

static int chv_pinctrl_remove(struct platform_device *pdev)
{
	struct chv_pinctrl *pctrl = platform_get_drvdata(pdev);

	gpiochip_remove(&pctrl->chip);
	pinctrl_unregister(pctrl->pctldev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int chv_pinctrl_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct chv_pinctrl *pctrl = platform_get_drvdata(pdev);
	int i;

	pctrl->saved_intmask = readl(pctrl->regs + CHV_INTMASK);

	for (i = 0; i < pctrl->community->npins; i++) {
		const struct pinctrl_pin_desc *desc;
		struct chv_pin_context *ctx;
		void __iomem *reg;

		desc = &pctrl->community->pins[i];
		if (chv_pad_locked(pctrl, desc->number))
			continue;

		ctx = &pctrl->saved_pin_context[i];

		reg = chv_padreg(pctrl, desc->number, CHV_PADCTRL0);
		ctx->padctrl0 = readl(reg) & ~CHV_PADCTRL0_GPIORXSTATE;

		reg = chv_padreg(pctrl, desc->number, CHV_PADCTRL1);
		ctx->padctrl1 = readl(reg);
	}

	return 0;
}

static int chv_pinctrl_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct chv_pinctrl *pctrl = platform_get_drvdata(pdev);
	int i;

	/*
	 * Mask all interrupts before restoring per-pin configuration
	 * registers because we don't know in which state BIOS left them
	 * upon exiting suspend.
	 */
	chv_writel(0, pctrl->regs + CHV_INTMASK);

	for (i = 0; i < pctrl->community->npins; i++) {
		const struct pinctrl_pin_desc *desc;
		const struct chv_pin_context *ctx;
		void __iomem *reg;
		u32 val;

		desc = &pctrl->community->pins[i];
		if (chv_pad_locked(pctrl, desc->number))
			continue;

		ctx = &pctrl->saved_pin_context[i];

		/* Only restore if our saved state differs from the current */
		reg = chv_padreg(pctrl, desc->number, CHV_PADCTRL0);
		val = readl(reg) & ~CHV_PADCTRL0_GPIORXSTATE;
		if (ctx->padctrl0 != val) {
			chv_writel(ctx->padctrl0, reg);
			dev_dbg(pctrl->dev, "restored pin %2u ctrl0 0x%08x\n",
				desc->number, readl(reg));
		}

		reg = chv_padreg(pctrl, desc->number, CHV_PADCTRL1);
		val = readl(reg);
		if (ctx->padctrl1 != val) {
			chv_writel(ctx->padctrl1, reg);
			dev_dbg(pctrl->dev, "restored pin %2u ctrl1 0x%08x\n",
				desc->number, readl(reg));
		}
	}

	/*
	 * Now that all pins are restored to known state, we can restore
	 * the interrupt mask register as well.
	 */
	chv_writel(0xffff, pctrl->regs + CHV_INTSTAT);
	chv_writel(pctrl->saved_intmask, pctrl->regs + CHV_INTMASK);

	return 0;
}
#endif

static const struct dev_pm_ops chv_pinctrl_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(chv_pinctrl_suspend, chv_pinctrl_resume)
};

static const struct acpi_device_id chv_pinctrl_acpi_match[] = {
	{ "INT33FF" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, chv_pinctrl_acpi_match);

static struct platform_driver chv_pinctrl_driver = {
	.probe = chv_pinctrl_probe,
	.remove = chv_pinctrl_remove,
	.driver = {
		.name = "cherryview-pinctrl",
		.pm = &chv_pinctrl_pm_ops,
		.acpi_match_table = chv_pinctrl_acpi_match,
	},
};

static int __init chv_pinctrl_init(void)
{
	return platform_driver_register(&chv_pinctrl_driver);
}
subsys_initcall(chv_pinctrl_init);

static void __exit chv_pinctrl_exit(void)
{
	platform_driver_unregister(&chv_pinctrl_driver);
}
module_exit(chv_pinctrl_exit);

MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("Intel Cherryview/Braswell pinctrl driver");
MODULE_LICENSE("GPL v2");
