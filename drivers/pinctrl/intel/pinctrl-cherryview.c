// SPDX-License-Identifier: GPL-2.0
/*
 * Cherryview/Braswell pinctrl driver
 *
 * Copyright (C) 2014, 2020 Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This driver is based on the original Cherryview GPIO driver by
 *   Ning Li <ning.li@intel.com>
 *   Alan Cox <alan@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>

#include "pinctrl-intel.h"

#define CHV_INTSTAT			0x300
#define CHV_INTMASK			0x380

#define FAMILY_PAD_REGS_OFF		0x4400
#define FAMILY_PAD_REGS_SIZE		0x400
#define MAX_FAMILY_PAD_GPIO_NO		15
#define GPIO_REGS_SIZE			8

#define CHV_PADCTRL0			0x000
#define CHV_PADCTRL0_INTSEL_SHIFT	28
#define CHV_PADCTRL0_INTSEL_MASK	GENMASK(31, 28)
#define CHV_PADCTRL0_TERM_UP		BIT(23)
#define CHV_PADCTRL0_TERM_SHIFT		20
#define CHV_PADCTRL0_TERM_MASK		GENMASK(22, 20)
#define CHV_PADCTRL0_TERM_20K		1
#define CHV_PADCTRL0_TERM_5K		2
#define CHV_PADCTRL0_TERM_1K		4
#define CHV_PADCTRL0_PMODE_SHIFT	16
#define CHV_PADCTRL0_PMODE_MASK		GENMASK(19, 16)
#define CHV_PADCTRL0_GPIOEN		BIT(15)
#define CHV_PADCTRL0_GPIOCFG_SHIFT	8
#define CHV_PADCTRL0_GPIOCFG_MASK	GENMASK(10, 8)
#define CHV_PADCTRL0_GPIOCFG_GPIO	0
#define CHV_PADCTRL0_GPIOCFG_GPO	1
#define CHV_PADCTRL0_GPIOCFG_GPI	2
#define CHV_PADCTRL0_GPIOCFG_HIZ	3
#define CHV_PADCTRL0_GPIOTXSTATE	BIT(1)
#define CHV_PADCTRL0_GPIORXSTATE	BIT(0)

#define CHV_PADCTRL1			0x004
#define CHV_PADCTRL1_CFGLOCK		BIT(31)
#define CHV_PADCTRL1_INVRXTX_SHIFT	4
#define CHV_PADCTRL1_INVRXTX_MASK	GENMASK(7, 4)
#define CHV_PADCTRL1_INVRXTX_TXDATA	BIT(7)
#define CHV_PADCTRL1_INVRXTX_RXDATA	BIT(6)
#define CHV_PADCTRL1_INVRXTX_TXENABLE	BIT(5)
#define CHV_PADCTRL1_ODEN		BIT(3)
#define CHV_PADCTRL1_INTWAKECFG_MASK	GENMASK(2, 0)
#define CHV_PADCTRL1_INTWAKECFG_FALLING	1
#define CHV_PADCTRL1_INTWAKECFG_RISING	2
#define CHV_PADCTRL1_INTWAKECFG_BOTH	3
#define CHV_PADCTRL1_INTWAKECFG_LEVEL	4

struct intel_pad_context {
	u32 padctrl0;
	u32 padctrl1;
};

#define CHV_INVALID_HWIRQ	((unsigned int)INVALID_HWIRQ)

/**
 * struct intel_community_context - community context for Cherryview
 * @intr_lines: Mapping between 16 HW interrupt wires and GPIO offset (in GPIO number space)
 * @saved_intmask: Interrupt mask saved for system sleep
 */
struct intel_community_context {
	unsigned int intr_lines[16];
	u32 saved_intmask;
};

#define	PINMODE_INVERT_OE	BIT(15)

#define PINMODE(m, i)		((m) | ((i) * PINMODE_INVERT_OE))

#define CHV_GPP(start, end)			\
	{					\
		.base = (start),		\
		.size = (end) - (start) + 1,	\
	}

#define CHV_COMMUNITY(g, i, a)			\
	{					\
		.gpps = (g),			\
		.ngpps = ARRAY_SIZE(g),		\
		.nirqs = (i),			\
		.acpi_space_id = (a),		\
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
static const unsigned southwest_spi3_pins[] = { 76, 79, 80, 81, 82 };

/* Some of LPE I2S TXD pins need to have OE inversion set */
static const unsigned int southwest_lpe_altfuncs[] = {
	PINMODE(1, 1), PINMODE(1, 0), PINMODE(1, 0), PINMODE(1, 0), /* 30, 31, 32, 33 */
	PINMODE(1, 1), PINMODE(1, 0), PINMODE(1, 0), PINMODE(1, 0), /* 34, 35, 36, 37 */
	PINMODE(1, 0), PINMODE(1, 0), PINMODE(1, 0), PINMODE(1, 1), /* 92, 94, 96, 97 */
};

/*
 * Two spi3 chipselects are available in different mode than the main spi3
 * functionality, which is using mode 2.
 */
static const unsigned int southwest_spi3_altfuncs[] = {
	PINMODE(3, 0), PINMODE(2, 0), PINMODE(3, 0), PINMODE(2, 0), /* 76, 79, 80, 81 */
	PINMODE(2, 0),						    /* 82 */
};

static const struct intel_pingroup southwest_groups[] = {
	PIN_GROUP("uart0_grp", southwest_uart0_pins, PINMODE(2, 0)),
	PIN_GROUP("uart1_grp", southwest_uart1_pins, PINMODE(1, 0)),
	PIN_GROUP("uart2_grp", southwest_uart2_pins, PINMODE(1, 0)),
	PIN_GROUP("hda_grp", southwest_hda_pins, PINMODE(2, 0)),
	PIN_GROUP("i2c0_grp", southwest_i2c0_pins, PINMODE(1, 1)),
	PIN_GROUP("i2c1_grp", southwest_i2c1_pins, PINMODE(1, 1)),
	PIN_GROUP("i2c2_grp", southwest_i2c2_pins, PINMODE(1, 1)),
	PIN_GROUP("i2c3_grp", southwest_i2c3_pins, PINMODE(1, 1)),
	PIN_GROUP("i2c4_grp", southwest_i2c4_pins, PINMODE(1, 1)),
	PIN_GROUP("i2c5_grp", southwest_i2c5_pins, PINMODE(1, 1)),
	PIN_GROUP("i2c6_grp", southwest_i2c6_pins, PINMODE(1, 1)),
	PIN_GROUP("i2c_nfc_grp", southwest_i2c_nfc_pins, PINMODE(2, 1)),
	PIN_GROUP("lpe_grp", southwest_lpe_pins, southwest_lpe_altfuncs),
	PIN_GROUP("spi3_grp", southwest_spi3_pins, southwest_spi3_altfuncs),
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
static const struct intel_function southwest_functions[] = {
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

static const struct intel_padgroup southwest_gpps[] = {
	CHV_GPP(0, 7),
	CHV_GPP(15, 22),
	CHV_GPP(30, 37),
	CHV_GPP(45, 52),
	CHV_GPP(60, 67),
	CHV_GPP(75, 82),
	CHV_GPP(90, 97),
};

/*
 * Southwest community can generate GPIO interrupts only for the first 8
 * interrupts. The upper half (8-15) can only be used to trigger GPEs.
 */
static const struct intel_community southwest_communities[] = {
	CHV_COMMUNITY(southwest_gpps, 8, 0x91),
};

static const struct intel_pinctrl_soc_data southwest_soc_data = {
	.uid = "1",
	.pins = southwest_pins,
	.npins = ARRAY_SIZE(southwest_pins),
	.groups = southwest_groups,
	.ngroups = ARRAY_SIZE(southwest_groups),
	.functions = southwest_functions,
	.nfunctions = ARRAY_SIZE(southwest_functions),
	.communities = southwest_communities,
	.ncommunities = ARRAY_SIZE(southwest_communities),
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

static const struct intel_padgroup north_gpps[] = {
	CHV_GPP(0, 8),
	CHV_GPP(15, 27),
	CHV_GPP(30, 41),
	CHV_GPP(45, 56),
	CHV_GPP(60, 72),
};

/*
 * North community can generate GPIO interrupts only for the first 8
 * interrupts. The upper half (8-15) can only be used to trigger GPEs.
 */
static const struct intel_community north_communities[] = {
	CHV_COMMUNITY(north_gpps, 8, 0x92),
};

static const struct intel_pinctrl_soc_data north_soc_data = {
	.uid = "2",
	.pins = north_pins,
	.npins = ARRAY_SIZE(north_pins),
	.communities = north_communities,
	.ncommunities = ARRAY_SIZE(north_communities),
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

static const struct intel_padgroup east_gpps[] = {
	CHV_GPP(0, 11),
	CHV_GPP(15, 26),
};

static const struct intel_community east_communities[] = {
	CHV_COMMUNITY(east_gpps, 16, 0x93),
};

static const struct intel_pinctrl_soc_data east_soc_data = {
	.uid = "3",
	.pins = east_pins,
	.npins = ARRAY_SIZE(east_pins),
	.communities = east_communities,
	.ncommunities = ARRAY_SIZE(east_communities),
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

static const struct intel_pingroup southeast_groups[] = {
	PIN_GROUP("pwm0_grp", southeast_pwm0_pins, PINMODE(1, 0)),
	PIN_GROUP("pwm1_grp", southeast_pwm1_pins, PINMODE(1, 0)),
	PIN_GROUP("sdmmc1_grp", southeast_sdmmc1_pins, PINMODE(1, 0)),
	PIN_GROUP("sdmmc2_grp", southeast_sdmmc2_pins, PINMODE(1, 0)),
	PIN_GROUP("sdmmc3_grp", southeast_sdmmc3_pins, PINMODE(1, 0)),
	PIN_GROUP("spi1_grp", southeast_spi1_pins, PINMODE(1, 0)),
	PIN_GROUP("spi2_grp", southeast_spi2_pins, PINMODE(4, 0)),
};

static const char * const southeast_pwm0_groups[] = { "pwm0_grp" };
static const char * const southeast_pwm1_groups[] = { "pwm1_grp" };
static const char * const southeast_sdmmc1_groups[] = { "sdmmc1_grp" };
static const char * const southeast_sdmmc2_groups[] = { "sdmmc2_grp" };
static const char * const southeast_sdmmc3_groups[] = { "sdmmc3_grp" };
static const char * const southeast_spi1_groups[] = { "spi1_grp" };
static const char * const southeast_spi2_groups[] = { "spi2_grp" };

static const struct intel_function southeast_functions[] = {
	FUNCTION("pwm0", southeast_pwm0_groups),
	FUNCTION("pwm1", southeast_pwm1_groups),
	FUNCTION("sdmmc1", southeast_sdmmc1_groups),
	FUNCTION("sdmmc2", southeast_sdmmc2_groups),
	FUNCTION("sdmmc3", southeast_sdmmc3_groups),
	FUNCTION("spi1", southeast_spi1_groups),
	FUNCTION("spi2", southeast_spi2_groups),
};

static const struct intel_padgroup southeast_gpps[] = {
	CHV_GPP(0, 7),
	CHV_GPP(15, 26),
	CHV_GPP(30, 35),
	CHV_GPP(45, 52),
	CHV_GPP(60, 69),
	CHV_GPP(75, 85),
};

static const struct intel_community southeast_communities[] = {
	CHV_COMMUNITY(southeast_gpps, 16, 0x94),
};

static const struct intel_pinctrl_soc_data southeast_soc_data = {
	.uid = "4",
	.pins = southeast_pins,
	.npins = ARRAY_SIZE(southeast_pins),
	.groups = southeast_groups,
	.ngroups = ARRAY_SIZE(southeast_groups),
	.functions = southeast_functions,
	.nfunctions = ARRAY_SIZE(southeast_functions),
	.communities = southeast_communities,
	.ncommunities = ARRAY_SIZE(southeast_communities),
};

static const struct intel_pinctrl_soc_data *chv_soc_data[] = {
	&southwest_soc_data,
	&north_soc_data,
	&east_soc_data,
	&southeast_soc_data,
	NULL
};

/*
 * Lock to serialize register accesses
 *
 * Due to a silicon issue, a shared lock must be used to prevent
 * concurrent accesses across the 4 GPIO controllers.
 *
 * See Intel Atom Z8000 Processor Series Specification Update (Rev. 005),
 * errata #CHT34, for further information.
 */
static DEFINE_RAW_SPINLOCK(chv_lock);

static u32 chv_pctrl_readl(struct intel_pinctrl *pctrl, unsigned int offset)
{
	const struct intel_community *community = &pctrl->communities[0];

	return readl(community->regs + offset);
}

static void chv_pctrl_writel(struct intel_pinctrl *pctrl, unsigned int offset, u32 value)
{
	const struct intel_community *community = &pctrl->communities[0];
	void __iomem *reg = community->regs + offset;

	/* Write and simple read back to confirm the bus transferring done */
	writel(value, reg);
	readl(reg);
}

static void __iomem *chv_padreg(struct intel_pinctrl *pctrl, unsigned int offset,
				unsigned int reg)
{
	const struct intel_community *community = &pctrl->communities[0];
	unsigned int family_no = offset / MAX_FAMILY_PAD_GPIO_NO;
	unsigned int pad_no = offset % MAX_FAMILY_PAD_GPIO_NO;

	offset = FAMILY_PAD_REGS_SIZE * family_no + GPIO_REGS_SIZE * pad_no;

	return community->pad_regs + offset + reg;
}

static u32 chv_readl(struct intel_pinctrl *pctrl, unsigned int pin, unsigned int offset)
{
	return readl(chv_padreg(pctrl, pin, offset));
}

static void chv_writel(struct intel_pinctrl *pctrl, unsigned int pin, unsigned int offset, u32 value)
{
	void __iomem *reg = chv_padreg(pctrl, pin, offset);

	/* Write and simple read back to confirm the bus transferring done */
	writel(value, reg);
	readl(reg);
}

/* When Pad Cfg is locked, driver can only change GPIOTXState or GPIORXState */
static bool chv_pad_locked(struct intel_pinctrl *pctrl, unsigned int offset)
{
	return chv_readl(pctrl, offset, CHV_PADCTRL1) & CHV_PADCTRL1_CFGLOCK;
}

static int chv_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->ngroups;
}

static const char *chv_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned int group)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->groups[group].name;
}

static int chv_get_group_pins(struct pinctrl_dev *pctldev, unsigned int group,
			      const unsigned int **pins, unsigned int *npins)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pctrl->soc->groups[group].pins;
	*npins = pctrl->soc->groups[group].npins;
	return 0;
}

static void chv_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			     unsigned int offset)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	u32 ctrl0, ctrl1;
	bool locked;

	raw_spin_lock_irqsave(&chv_lock, flags);

	ctrl0 = chv_readl(pctrl, offset, CHV_PADCTRL0);
	ctrl1 = chv_readl(pctrl, offset, CHV_PADCTRL1);
	locked = chv_pad_locked(pctrl, offset);

	raw_spin_unlock_irqrestore(&chv_lock, flags);

	if (ctrl0 & CHV_PADCTRL0_GPIOEN) {
		seq_puts(s, "GPIO ");
	} else {
		u32 mode;

		mode = ctrl0 & CHV_PADCTRL0_PMODE_MASK;
		mode >>= CHV_PADCTRL0_PMODE_SHIFT;

		seq_printf(s, "mode %d ", mode);
	}

	seq_printf(s, "0x%08x 0x%08x", ctrl0, ctrl1);

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
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->nfunctions;
}

static const char *chv_get_function_name(struct pinctrl_dev *pctldev,
					 unsigned int function)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->functions[function].name;
}

static int chv_get_function_groups(struct pinctrl_dev *pctldev,
				   unsigned int function,
				   const char * const **groups,
				   unsigned int * const ngroups)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctrl->soc->functions[function].groups;
	*ngroups = pctrl->soc->functions[function].ngroups;
	return 0;
}

static int chv_pinmux_set_mux(struct pinctrl_dev *pctldev,
			      unsigned int function, unsigned int group)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pctrl->dev;
	const struct intel_pingroup *grp;
	unsigned long flags;
	int i;

	grp = &pctrl->soc->groups[group];

	raw_spin_lock_irqsave(&chv_lock, flags);

	/* Check first that the pad is not locked */
	for (i = 0; i < grp->npins; i++) {
		if (chv_pad_locked(pctrl, grp->pins[i])) {
			raw_spin_unlock_irqrestore(&chv_lock, flags);
			dev_warn(dev, "unable to set mode for locked pin %u\n", grp->pins[i]);
			return -EBUSY;
		}
	}

	for (i = 0; i < grp->npins; i++) {
		int pin = grp->pins[i];
		unsigned int mode;
		bool invert_oe;
		u32 value;

		/* Check if there is pin-specific config */
		if (grp->modes)
			mode = grp->modes[i];
		else
			mode = grp->mode;

		/* Extract OE inversion */
		invert_oe = mode & PINMODE_INVERT_OE;
		mode &= ~PINMODE_INVERT_OE;

		value = chv_readl(pctrl, pin, CHV_PADCTRL0);
		/* Disable GPIO mode */
		value &= ~CHV_PADCTRL0_GPIOEN;
		/* Set to desired mode */
		value &= ~CHV_PADCTRL0_PMODE_MASK;
		value |= mode << CHV_PADCTRL0_PMODE_SHIFT;
		chv_writel(pctrl, pin, CHV_PADCTRL0, value);

		/* Update for invert_oe */
		value = chv_readl(pctrl, pin, CHV_PADCTRL1) & ~CHV_PADCTRL1_INVRXTX_MASK;
		if (invert_oe)
			value |= CHV_PADCTRL1_INVRXTX_TXENABLE;
		chv_writel(pctrl, pin, CHV_PADCTRL1, value);

		dev_dbg(dev, "configured pin %u mode %u OE %sinverted\n", pin, mode,
			invert_oe ? "" : "not ");
	}

	raw_spin_unlock_irqrestore(&chv_lock, flags);

	return 0;
}

static void chv_gpio_clear_triggering(struct intel_pinctrl *pctrl,
				      unsigned int offset)
{
	u32 invrxtx_mask = CHV_PADCTRL1_INVRXTX_MASK;
	u32 value;

	/*
	 * One some devices the GPIO should output the inverted value from what
	 * device-drivers / ACPI code expects (inverted external buffer?). The
	 * BIOS makes this work by setting the CHV_PADCTRL1_INVRXTX_TXDATA flag,
	 * preserve this flag if the pin is already setup as GPIO.
	 */
	value = chv_readl(pctrl, offset, CHV_PADCTRL0);
	if (value & CHV_PADCTRL0_GPIOEN)
		invrxtx_mask &= ~CHV_PADCTRL1_INVRXTX_TXDATA;

	value = chv_readl(pctrl, offset, CHV_PADCTRL1);
	value &= ~CHV_PADCTRL1_INTWAKECFG_MASK;
	value &= ~invrxtx_mask;
	chv_writel(pctrl, offset, CHV_PADCTRL1, value);
}

static int chv_gpio_request_enable(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned int offset)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&chv_lock, flags);

	if (chv_pad_locked(pctrl, offset)) {
		value = chv_readl(pctrl, offset, CHV_PADCTRL0);
		if (!(value & CHV_PADCTRL0_GPIOEN)) {
			/* Locked so cannot enable */
			raw_spin_unlock_irqrestore(&chv_lock, flags);
			return -EBUSY;
		}
	} else {
		struct intel_community_context *cctx = &pctrl->context.communities[0];
		int i;

		/* Reset the interrupt mapping */
		for (i = 0; i < ARRAY_SIZE(cctx->intr_lines); i++) {
			if (cctx->intr_lines[i] == offset) {
				cctx->intr_lines[i] = CHV_INVALID_HWIRQ;
				break;
			}
		}

		/* Disable interrupt generation */
		chv_gpio_clear_triggering(pctrl, offset);

		value = chv_readl(pctrl, offset, CHV_PADCTRL0);

		/*
		 * If the pin is in HiZ mode (both TX and RX buffers are
		 * disabled) we turn it to be input now.
		 */
		if ((value & CHV_PADCTRL0_GPIOCFG_MASK) ==
		     (CHV_PADCTRL0_GPIOCFG_HIZ << CHV_PADCTRL0_GPIOCFG_SHIFT)) {
			value &= ~CHV_PADCTRL0_GPIOCFG_MASK;
			value |= CHV_PADCTRL0_GPIOCFG_GPI << CHV_PADCTRL0_GPIOCFG_SHIFT;
		}

		/* Switch to a GPIO mode */
		value |= CHV_PADCTRL0_GPIOEN;
		chv_writel(pctrl, offset, CHV_PADCTRL0, value);
	}

	raw_spin_unlock_irqrestore(&chv_lock, flags);

	return 0;
}

static void chv_gpio_disable_free(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned int offset)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;

	raw_spin_lock_irqsave(&chv_lock, flags);

	if (!chv_pad_locked(pctrl, offset))
		chv_gpio_clear_triggering(pctrl, offset);

	raw_spin_unlock_irqrestore(&chv_lock, flags);
}

static int chv_gpio_set_direction(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned int offset, bool input)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	u32 ctrl0;

	raw_spin_lock_irqsave(&chv_lock, flags);

	ctrl0 = chv_readl(pctrl, offset, CHV_PADCTRL0) & ~CHV_PADCTRL0_GPIOCFG_MASK;
	if (input)
		ctrl0 |= CHV_PADCTRL0_GPIOCFG_GPI << CHV_PADCTRL0_GPIOCFG_SHIFT;
	else
		ctrl0 |= CHV_PADCTRL0_GPIOCFG_GPO << CHV_PADCTRL0_GPIOCFG_SHIFT;
	chv_writel(pctrl, offset, CHV_PADCTRL0, ctrl0);

	raw_spin_unlock_irqrestore(&chv_lock, flags);

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

static int chv_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
			  unsigned long *config)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned long flags;
	u32 ctrl0, ctrl1;
	u16 arg = 0;
	u32 term;

	raw_spin_lock_irqsave(&chv_lock, flags);
	ctrl0 = chv_readl(pctrl, pin, CHV_PADCTRL0);
	ctrl1 = chv_readl(pctrl, pin, CHV_PADCTRL1);
	raw_spin_unlock_irqrestore(&chv_lock, flags);

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

static int chv_config_set_pull(struct intel_pinctrl *pctrl, unsigned int pin,
			       enum pin_config_param param, u32 arg)
{
	unsigned long flags;
	u32 ctrl0, pull;

	raw_spin_lock_irqsave(&chv_lock, flags);
	ctrl0 = chv_readl(pctrl, pin, CHV_PADCTRL0);

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
			raw_spin_unlock_irqrestore(&chv_lock, flags);
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
			raw_spin_unlock_irqrestore(&chv_lock, flags);
			return -EINVAL;
		}

		ctrl0 |= pull;
		break;

	default:
		raw_spin_unlock_irqrestore(&chv_lock, flags);
		return -EINVAL;
	}

	chv_writel(pctrl, pin, CHV_PADCTRL0, ctrl0);
	raw_spin_unlock_irqrestore(&chv_lock, flags);

	return 0;
}

static int chv_config_set_oden(struct intel_pinctrl *pctrl, unsigned int pin,
			       bool enable)
{
	unsigned long flags;
	u32 ctrl1;

	raw_spin_lock_irqsave(&chv_lock, flags);
	ctrl1 = chv_readl(pctrl, pin, CHV_PADCTRL1);

	if (enable)
		ctrl1 |= CHV_PADCTRL1_ODEN;
	else
		ctrl1 &= ~CHV_PADCTRL1_ODEN;

	chv_writel(pctrl, pin, CHV_PADCTRL1, ctrl1);
	raw_spin_unlock_irqrestore(&chv_lock, flags);

	return 0;
}

static int chv_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
			  unsigned long *configs, unsigned int nconfigs)
{
	struct intel_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pctrl->dev;
	enum pin_config_param param;
	int i, ret;
	u32 arg;

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

		case PIN_CONFIG_DRIVE_PUSH_PULL:
			ret = chv_config_set_oden(pctrl, pin, false);
			if (ret)
				return ret;
			break;

		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			ret = chv_config_set_oden(pctrl, pin, true);
			if (ret)
				return ret;
			break;

		default:
			return -ENOTSUPP;
		}

		dev_dbg(dev, "pin %d set config %d arg %u\n", pin, param, arg);
	}

	return 0;
}

static int chv_config_group_get(struct pinctrl_dev *pctldev,
				unsigned int group,
				unsigned long *config)
{
	const unsigned int *pins;
	unsigned int npins;
	int ret;

	ret = chv_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	ret = chv_config_get(pctldev, pins[0], config);
	if (ret)
		return ret;

	return 0;
}

static int chv_config_group_set(struct pinctrl_dev *pctldev,
				unsigned int group, unsigned long *configs,
				unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int npins;
	int i, ret;

	ret = chv_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = chv_config_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops chv_pinconf_ops = {
	.is_generic = true,
	.pin_config_set = chv_config_set,
	.pin_config_get = chv_config_get,
	.pin_config_group_get = chv_config_group_get,
	.pin_config_group_set = chv_config_group_set,
};

static struct pinctrl_desc chv_pinctrl_desc = {
	.pctlops = &chv_pinctrl_ops,
	.pmxops = &chv_pinmux_ops,
	.confops = &chv_pinconf_ops,
	.owner = THIS_MODULE,
};

static int chv_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(chip);
	unsigned long flags;
	u32 ctrl0, cfg;

	raw_spin_lock_irqsave(&chv_lock, flags);
	ctrl0 = chv_readl(pctrl, offset, CHV_PADCTRL0);
	raw_spin_unlock_irqrestore(&chv_lock, flags);

	cfg = ctrl0 & CHV_PADCTRL0_GPIOCFG_MASK;
	cfg >>= CHV_PADCTRL0_GPIOCFG_SHIFT;

	if (cfg == CHV_PADCTRL0_GPIOCFG_GPO)
		return !!(ctrl0 & CHV_PADCTRL0_GPIOTXSTATE);
	return !!(ctrl0 & CHV_PADCTRL0_GPIORXSTATE);
}

static void chv_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(chip);
	unsigned long flags;
	u32 ctrl0;

	raw_spin_lock_irqsave(&chv_lock, flags);

	ctrl0 = chv_readl(pctrl, offset, CHV_PADCTRL0);

	if (value)
		ctrl0 |= CHV_PADCTRL0_GPIOTXSTATE;
	else
		ctrl0 &= ~CHV_PADCTRL0_GPIOTXSTATE;

	chv_writel(pctrl, offset, CHV_PADCTRL0, ctrl0);

	raw_spin_unlock_irqrestore(&chv_lock, flags);
}

static int chv_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 ctrl0, direction;
	unsigned long flags;

	raw_spin_lock_irqsave(&chv_lock, flags);
	ctrl0 = chv_readl(pctrl, offset, CHV_PADCTRL0);
	raw_spin_unlock_irqrestore(&chv_lock, flags);

	direction = ctrl0 & CHV_PADCTRL0_GPIOCFG_MASK;
	direction >>= CHV_PADCTRL0_GPIOCFG_SHIFT;

	if (direction == CHV_PADCTRL0_GPIOCFG_GPO)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int chv_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int chv_gpio_direction_output(struct gpio_chip *chip, unsigned int offset,
				     int value)
{
	chv_gpio_set(chip, offset, value);
	return pinctrl_gpio_direction_output(chip->base + offset);
}

static const struct gpio_chip chv_gpio_chip = {
	.owner = THIS_MODULE,
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.get_direction = chv_gpio_get_direction,
	.direction_input = chv_gpio_direction_input,
	.direction_output = chv_gpio_direction_output,
	.get = chv_gpio_get,
	.set = chv_gpio_set,
};

static void chv_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct intel_pinctrl *pctrl = gpiochip_get_data(gc);
	int pin = irqd_to_hwirq(d);
	u32 intr_line;

	raw_spin_lock(&chv_lock);

	intr_line = chv_readl(pctrl, pin, CHV_PADCTRL0);
	intr_line &= CHV_PADCTRL0_INTSEL_MASK;
	intr_line >>= CHV_PADCTRL0_INTSEL_SHIFT;
	chv_pctrl_writel(pctrl, CHV_INTSTAT, BIT(intr_line));

	raw_spin_unlock(&chv_lock);
}

static void chv_gpio_irq_mask_unmask(struct irq_data *d, bool mask)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct intel_pinctrl *pctrl = gpiochip_get_data(gc);
	int pin = irqd_to_hwirq(d);
	u32 value, intr_line;
	unsigned long flags;

	raw_spin_lock_irqsave(&chv_lock, flags);

	intr_line = chv_readl(pctrl, pin, CHV_PADCTRL0);
	intr_line &= CHV_PADCTRL0_INTSEL_MASK;
	intr_line >>= CHV_PADCTRL0_INTSEL_SHIFT;

	value = chv_pctrl_readl(pctrl, CHV_INTMASK);
	if (mask)
		value &= ~BIT(intr_line);
	else
		value |= BIT(intr_line);
	chv_pctrl_writel(pctrl, CHV_INTMASK, value);

	raw_spin_unlock_irqrestore(&chv_lock, flags);
}

static void chv_gpio_irq_mask(struct irq_data *d)
{
	chv_gpio_irq_mask_unmask(d, true);
}

static void chv_gpio_irq_unmask(struct irq_data *d)
{
	chv_gpio_irq_mask_unmask(d, false);
}

static unsigned chv_gpio_irq_startup(struct irq_data *d)
{
	/*
	 * Check if the interrupt has been requested with 0 as triggering
	 * type. In that case it is assumed that the current values
	 * programmed to the hardware are used (e.g BIOS configured
	 * defaults).
	 *
	 * In that case ->irq_set_type() will never be called so we need to
	 * read back the values from hardware now, set correct flow handler
	 * and update mappings before the interrupt is being used.
	 */
	if (irqd_get_trigger_type(d) == IRQ_TYPE_NONE) {
		struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
		struct intel_pinctrl *pctrl = gpiochip_get_data(gc);
		struct device *dev = pctrl->dev;
		struct intel_community_context *cctx = &pctrl->context.communities[0];
		unsigned int pin = irqd_to_hwirq(d);
		irq_flow_handler_t handler;
		unsigned long flags;
		u32 intsel, value;

		raw_spin_lock_irqsave(&chv_lock, flags);
		intsel = chv_readl(pctrl, pin, CHV_PADCTRL0);
		intsel &= CHV_PADCTRL0_INTSEL_MASK;
		intsel >>= CHV_PADCTRL0_INTSEL_SHIFT;

		value = chv_readl(pctrl, pin, CHV_PADCTRL1);
		if (value & CHV_PADCTRL1_INTWAKECFG_LEVEL)
			handler = handle_level_irq;
		else
			handler = handle_edge_irq;

		if (cctx->intr_lines[intsel] == CHV_INVALID_HWIRQ) {
			irq_set_handler_locked(d, handler);
			dev_dbg(dev, "using interrupt line %u for IRQ_TYPE_NONE on pin %u\n",
				intsel, pin);
			cctx->intr_lines[intsel] = pin;
		}
		raw_spin_unlock_irqrestore(&chv_lock, flags);
	}

	chv_gpio_irq_unmask(d);
	return 0;
}

static int chv_gpio_set_intr_line(struct intel_pinctrl *pctrl, unsigned int pin)
{
	struct device *dev = pctrl->dev;
	struct intel_community_context *cctx = &pctrl->context.communities[0];
	const struct intel_community *community = &pctrl->communities[0];
	u32 value, intsel;
	int i;

	value = chv_readl(pctrl, pin, CHV_PADCTRL0);
	intsel = (value & CHV_PADCTRL0_INTSEL_MASK) >> CHV_PADCTRL0_INTSEL_SHIFT;

	if (cctx->intr_lines[intsel] == pin)
		return 0;

	if (cctx->intr_lines[intsel] == CHV_INVALID_HWIRQ) {
		dev_dbg(dev, "using interrupt line %u for pin %u\n", intsel, pin);
		cctx->intr_lines[intsel] = pin;
		return 0;
	}

	/*
	 * The interrupt line selected by the BIOS is already in use by
	 * another pin, this is a known BIOS bug found on several models.
	 * But this may also be caused by Linux deciding to use a pin as
	 * IRQ which was not expected to be used as such by the BIOS authors,
	 * so log this at info level only.
	 */
	dev_info(dev, "interrupt line %u is used by both pin %u and pin %u\n", intsel,
		 cctx->intr_lines[intsel], pin);

	if (chv_pad_locked(pctrl, pin))
		return -EBUSY;

	/*
	 * The BIOS fills the interrupt lines from 0 counting up, start at
	 * the other end to find a free interrupt line to workaround this.
	 */
	for (i = community->nirqs - 1; i >= 0; i--) {
		if (cctx->intr_lines[i] == CHV_INVALID_HWIRQ)
			break;
	}
	if (i < 0)
		return -EBUSY;

	dev_info(dev, "changing the interrupt line for pin %u to %d\n", pin, i);

	value = (value & ~CHV_PADCTRL0_INTSEL_MASK) | (i << CHV_PADCTRL0_INTSEL_SHIFT);
	chv_writel(pctrl, pin, CHV_PADCTRL0, value);
	cctx->intr_lines[i] = pin;

	return 0;
}

static int chv_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct intel_pinctrl *pctrl = gpiochip_get_data(gc);
	unsigned int pin = irqd_to_hwirq(d);
	unsigned long flags;
	u32 value;
	int ret;

	raw_spin_lock_irqsave(&chv_lock, flags);

	ret = chv_gpio_set_intr_line(pctrl, pin);
	if (ret)
		goto out_unlock;

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
		value = chv_readl(pctrl, pin, CHV_PADCTRL1);
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

		chv_writel(pctrl, pin, CHV_PADCTRL1, value);
	}

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);

out_unlock:
	raw_spin_unlock_irqrestore(&chv_lock, flags);

	return ret;
}

static void chv_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct intel_pinctrl *pctrl = gpiochip_get_data(gc);
	struct device *dev = pctrl->dev;
	const struct intel_community *community = &pctrl->communities[0];
	struct intel_community_context *cctx = &pctrl->context.communities[0];
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long pending;
	unsigned long flags;
	u32 intr_line;

	chained_irq_enter(chip, desc);

	raw_spin_lock_irqsave(&chv_lock, flags);
	pending = chv_pctrl_readl(pctrl, CHV_INTSTAT);
	raw_spin_unlock_irqrestore(&chv_lock, flags);

	for_each_set_bit(intr_line, &pending, community->nirqs) {
		unsigned int offset;

		offset = cctx->intr_lines[intr_line];
		if (offset == CHV_INVALID_HWIRQ) {
			dev_warn_once(dev, "interrupt on unmapped interrupt line %u\n", intr_line);
			/* Some boards expect hwirq 0 to trigger in this case */
			offset = 0;
		}

		generic_handle_domain_irq(gc->irq.domain, offset);
	}

	chained_irq_exit(chip, desc);
}

/*
 * Certain machines seem to hardcode Linux IRQ numbers in their ACPI
 * tables. Since we leave GPIOs that are not capable of generating
 * interrupts out of the irqdomain the numbering will be different and
 * cause devices using the hardcoded IRQ numbers fail. In order not to
 * break such machines we will only mask pins from irqdomain if the machine
 * is not listed below.
 */
static const struct dmi_system_id chv_no_valid_mask[] = {
	/* See https://bugzilla.kernel.org/show_bug.cgi?id=194945 */
	{
		.ident = "Intel_Strago based Chromebooks (All models)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Intel_Strago"),
		},
	},
	{
		.ident = "HP Chromebook 11 G5 (Setzer)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Setzer"),
		},
	},
	{
		.ident = "Acer Chromebook R11 (Cyan)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Cyan"),
		},
	},
	{
		.ident = "Samsung Chromebook 3 (Celes)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GOOGLE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Celes"),
		},
	},
	{}
};

static void chv_init_irq_valid_mask(struct gpio_chip *chip,
				    unsigned long *valid_mask,
				    unsigned int ngpios)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct intel_community *community = &pctrl->communities[0];
	int i;

	/* Do not add GPIOs that can only generate GPEs to the IRQ domain */
	for (i = 0; i < pctrl->soc->npins; i++) {
		const struct pinctrl_pin_desc *desc;
		u32 intsel;

		desc = &pctrl->soc->pins[i];

		intsel = chv_readl(pctrl, desc->number, CHV_PADCTRL0);
		intsel &= CHV_PADCTRL0_INTSEL_MASK;
		intsel >>= CHV_PADCTRL0_INTSEL_SHIFT;

		if (intsel >= community->nirqs)
			clear_bit(desc->number, valid_mask);
	}
}

static int chv_gpio_irq_init_hw(struct gpio_chip *chip)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(chip);
	const struct intel_community *community = &pctrl->communities[0];

	/*
	 * The same set of machines in chv_no_valid_mask[] have incorrectly
	 * configured GPIOs that generate spurious interrupts so we use
	 * this same list to apply another quirk for them.
	 *
	 * See also https://bugzilla.kernel.org/show_bug.cgi?id=197953.
	 */
	if (!pctrl->chip.irq.init_valid_mask) {
		/*
		 * Mask all interrupts the community is able to generate
		 * but leave the ones that can only generate GPEs unmasked.
		 */
		chv_pctrl_writel(pctrl, CHV_INTMASK, GENMASK(31, community->nirqs));
	}

	/* Clear all interrupts */
	chv_pctrl_writel(pctrl, CHV_INTSTAT, 0xffff);

	return 0;
}

static int chv_gpio_add_pin_ranges(struct gpio_chip *chip)
{
	struct intel_pinctrl *pctrl = gpiochip_get_data(chip);
	struct device *dev = pctrl->dev;
	const struct intel_community *community = &pctrl->communities[0];
	const struct intel_padgroup *gpp;
	int ret, i;

	for (i = 0; i < community->ngpps; i++) {
		gpp = &community->gpps[i];
		ret = gpiochip_add_pin_range(chip, dev_name(dev), gpp->base, gpp->base, gpp->size);
		if (ret) {
			dev_err(dev, "failed to add GPIO pin range\n");
			return ret;
		}
	}

	return 0;
}

static int chv_gpio_probe(struct intel_pinctrl *pctrl, int irq)
{
	const struct intel_community *community = &pctrl->communities[0];
	const struct intel_padgroup *gpp;
	struct gpio_chip *chip = &pctrl->chip;
	struct device *dev = pctrl->dev;
	bool need_valid_mask = !dmi_check_system(chv_no_valid_mask);
	int ret, i, irq_base;

	*chip = chv_gpio_chip;

	chip->ngpio = pctrl->soc->pins[pctrl->soc->npins - 1].number + 1;
	chip->label = dev_name(dev);
	chip->add_pin_ranges = chv_gpio_add_pin_ranges;
	chip->parent = dev;
	chip->base = -1;

	pctrl->irq = irq;
	pctrl->irqchip.name = "chv-gpio";
	pctrl->irqchip.irq_startup = chv_gpio_irq_startup;
	pctrl->irqchip.irq_ack = chv_gpio_irq_ack;
	pctrl->irqchip.irq_mask = chv_gpio_irq_mask;
	pctrl->irqchip.irq_unmask = chv_gpio_irq_unmask;
	pctrl->irqchip.irq_set_type = chv_gpio_irq_type;
	pctrl->irqchip.flags = IRQCHIP_SKIP_SET_WAKE;

	chip->irq.chip = &pctrl->irqchip;
	chip->irq.init_hw = chv_gpio_irq_init_hw;
	chip->irq.parent_handler = chv_gpio_irq_handler;
	chip->irq.num_parents = 1;
	chip->irq.parents = &pctrl->irq;
	chip->irq.default_type = IRQ_TYPE_NONE;
	chip->irq.handler = handle_bad_irq;
	if (need_valid_mask) {
		chip->irq.init_valid_mask = chv_init_irq_valid_mask;
	} else {
		irq_base = devm_irq_alloc_descs(dev, -1, 0, pctrl->soc->npins, NUMA_NO_NODE);
		if (irq_base < 0) {
			dev_err(dev, "Failed to allocate IRQ numbers\n");
			return irq_base;
		}
	}

	ret = devm_gpiochip_add_data(dev, chip, pctrl);
	if (ret) {
		dev_err(dev, "Failed to register gpiochip\n");
		return ret;
	}

	if (!need_valid_mask) {
		for (i = 0; i < community->ngpps; i++) {
			gpp = &community->gpps[i];

			irq_domain_associate_many(chip->irq.domain, irq_base,
						  gpp->base, gpp->size);
			irq_base += gpp->size;
		}
	}

	return 0;
}

static acpi_status chv_pinctrl_mmio_access_handler(u32 function,
	acpi_physical_address address, u32 bits, u64 *value,
	void *handler_context, void *region_context)
{
	struct intel_pinctrl *pctrl = region_context;
	unsigned long flags;
	acpi_status ret = AE_OK;

	raw_spin_lock_irqsave(&chv_lock, flags);

	if (function == ACPI_WRITE)
		chv_pctrl_writel(pctrl, address, *value);
	else if (function == ACPI_READ)
		*value = chv_pctrl_readl(pctrl, address);
	else
		ret = AE_BAD_PARAMETER;

	raw_spin_unlock_irqrestore(&chv_lock, flags);

	return ret;
}

static int chv_pinctrl_probe(struct platform_device *pdev)
{
	const struct intel_pinctrl_soc_data *soc_data;
	struct intel_community_context *cctx;
	struct intel_community *community;
	struct device *dev = &pdev->dev;
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct intel_pinctrl *pctrl;
	acpi_status status;
	unsigned int i;
	int ret, irq;

	soc_data = intel_pinctrl_get_soc_data(pdev);
	if (IS_ERR(soc_data))
		return PTR_ERR(soc_data);

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = dev;
	pctrl->soc = soc_data;

	pctrl->ncommunities = pctrl->soc->ncommunities;
	pctrl->communities = devm_kmemdup(dev, pctrl->soc->communities,
					  pctrl->ncommunities * sizeof(*pctrl->communities),
					  GFP_KERNEL);
	if (!pctrl->communities)
		return -ENOMEM;

	community = &pctrl->communities[0];
	community->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(community->regs))
		return PTR_ERR(community->regs);

	community->pad_regs = community->regs + FAMILY_PAD_REGS_OFF;

#ifdef CONFIG_PM_SLEEP
	pctrl->context.pads = devm_kcalloc(dev, pctrl->soc->npins,
					   sizeof(*pctrl->context.pads),
					   GFP_KERNEL);
	if (!pctrl->context.pads)
		return -ENOMEM;
#endif

	pctrl->context.communities = devm_kcalloc(dev, pctrl->soc->ncommunities,
						  sizeof(*pctrl->context.communities),
						  GFP_KERNEL);
	if (!pctrl->context.communities)
		return -ENOMEM;

	cctx = &pctrl->context.communities[0];
	for (i = 0; i < ARRAY_SIZE(cctx->intr_lines); i++)
		cctx->intr_lines[i] = CHV_INVALID_HWIRQ;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	pctrl->pctldesc = chv_pinctrl_desc;
	pctrl->pctldesc.name = dev_name(dev);
	pctrl->pctldesc.pins = pctrl->soc->pins;
	pctrl->pctldesc.npins = pctrl->soc->npins;

	pctrl->pctldev = devm_pinctrl_register(dev, &pctrl->pctldesc, pctrl);
	if (IS_ERR(pctrl->pctldev)) {
		dev_err(dev, "failed to register pinctrl driver\n");
		return PTR_ERR(pctrl->pctldev);
	}

	ret = chv_gpio_probe(pctrl, irq);
	if (ret)
		return ret;

	status = acpi_install_address_space_handler(adev->handle,
					community->acpi_space_id,
					chv_pinctrl_mmio_access_handler,
					NULL, pctrl);
	if (ACPI_FAILURE(status))
		dev_err(dev, "failed to install ACPI addr space handler\n");

	platform_set_drvdata(pdev, pctrl);

	return 0;
}

static int chv_pinctrl_remove(struct platform_device *pdev)
{
	struct intel_pinctrl *pctrl = platform_get_drvdata(pdev);
	const struct intel_community *community = &pctrl->communities[0];

	acpi_remove_address_space_handler(ACPI_COMPANION(&pdev->dev),
					  community->acpi_space_id,
					  chv_pinctrl_mmio_access_handler);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int chv_pinctrl_suspend_noirq(struct device *dev)
{
	struct intel_pinctrl *pctrl = dev_get_drvdata(dev);
	struct intel_community_context *cctx = &pctrl->context.communities[0];
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(&chv_lock, flags);

	cctx->saved_intmask = chv_pctrl_readl(pctrl, CHV_INTMASK);

	for (i = 0; i < pctrl->soc->npins; i++) {
		const struct pinctrl_pin_desc *desc;
		struct intel_pad_context *ctx = &pctrl->context.pads[i];

		desc = &pctrl->soc->pins[i];
		if (chv_pad_locked(pctrl, desc->number))
			continue;

		ctx->padctrl0 = chv_readl(pctrl, desc->number, CHV_PADCTRL0);
		ctx->padctrl0 &= ~CHV_PADCTRL0_GPIORXSTATE;

		ctx->padctrl1 = chv_readl(pctrl, desc->number, CHV_PADCTRL1);
	}

	raw_spin_unlock_irqrestore(&chv_lock, flags);

	return 0;
}

static int chv_pinctrl_resume_noirq(struct device *dev)
{
	struct intel_pinctrl *pctrl = dev_get_drvdata(dev);
	struct intel_community_context *cctx = &pctrl->context.communities[0];
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(&chv_lock, flags);

	/*
	 * Mask all interrupts before restoring per-pin configuration
	 * registers because we don't know in which state BIOS left them
	 * upon exiting suspend.
	 */
	chv_pctrl_writel(pctrl, CHV_INTMASK, 0x0000);

	for (i = 0; i < pctrl->soc->npins; i++) {
		const struct pinctrl_pin_desc *desc;
		struct intel_pad_context *ctx = &pctrl->context.pads[i];
		u32 val;

		desc = &pctrl->soc->pins[i];
		if (chv_pad_locked(pctrl, desc->number))
			continue;

		/* Only restore if our saved state differs from the current */
		val = chv_readl(pctrl, desc->number, CHV_PADCTRL0);
		val &= ~CHV_PADCTRL0_GPIORXSTATE;
		if (ctx->padctrl0 != val) {
			chv_writel(pctrl, desc->number, CHV_PADCTRL0, ctx->padctrl0);
			dev_dbg(dev, "restored pin %2u ctrl0 0x%08x\n", desc->number,
				chv_readl(pctrl, desc->number, CHV_PADCTRL0));
		}

		val = chv_readl(pctrl, desc->number, CHV_PADCTRL1);
		if (ctx->padctrl1 != val) {
			chv_writel(pctrl, desc->number, CHV_PADCTRL1, ctx->padctrl1);
			dev_dbg(dev, "restored pin %2u ctrl1 0x%08x\n", desc->number,
				chv_readl(pctrl, desc->number, CHV_PADCTRL1));
		}
	}

	/*
	 * Now that all pins are restored to known state, we can restore
	 * the interrupt mask register as well.
	 */
	chv_pctrl_writel(pctrl, CHV_INTSTAT, 0xffff);
	chv_pctrl_writel(pctrl, CHV_INTMASK, cctx->saved_intmask);

	raw_spin_unlock_irqrestore(&chv_lock, flags);

	return 0;
}
#endif

static const struct dev_pm_ops chv_pinctrl_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(chv_pinctrl_suspend_noirq,
				      chv_pinctrl_resume_noirq)
};

static const struct acpi_device_id chv_pinctrl_acpi_match[] = {
	{ "INT33FF", (kernel_ulong_t)chv_soc_data },
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
