/*
 * MediaTek MT7622 Pinctrl Driver
 *
 * Copyright (C) 2017 Sean Wang <sean.wang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/regmap.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"
#include "mtk-eint.h"

#define PINCTRL_PINCTRL_DEV		KBUILD_MODNAME
#define MTK_RANGE(_a)		{ .range = (_a), .nranges = ARRAY_SIZE(_a), }
#define PINCTRL_PIN_GROUP(name, id)			\
	{						\
		name,					\
		id##_pins,				\
		ARRAY_SIZE(id##_pins),			\
		id##_funcs,				\
	}

#define MTK_GPIO_MODE	1
#define MTK_INPUT	0
#define MTK_OUTPUT	1
#define MTK_DISABLE	0
#define MTK_ENABLE	1

/* Custom pinconf parameters */
#define MTK_PIN_CONFIG_TDSEL	(PIN_CONFIG_END + 1)
#define MTK_PIN_CONFIG_RDSEL	(PIN_CONFIG_END + 2)

/* List these attributes which could be modified for the pin */
enum {
	PINCTRL_PIN_REG_MODE,
	PINCTRL_PIN_REG_DIR,
	PINCTRL_PIN_REG_DI,
	PINCTRL_PIN_REG_DO,
	PINCTRL_PIN_REG_SR,
	PINCTRL_PIN_REG_SMT,
	PINCTRL_PIN_REG_PD,
	PINCTRL_PIN_REG_PU,
	PINCTRL_PIN_REG_E4,
	PINCTRL_PIN_REG_E8,
	PINCTRL_PIN_REG_TDSEL,
	PINCTRL_PIN_REG_RDSEL,
	PINCTRL_PIN_REG_MAX,
};

/* struct mtk_pin_field - the structure that holds the information of the field
 *			  used to describe the attribute for the pin
 * @offset:		the register offset relative to the base address
 * @mask:		the mask used to filter out the field from the register
 * @bitpos:		the start bit relative to the register
 * @next:		the indication that the field would be extended to the
			next register
 */
struct mtk_pin_field {
	u32 offset;
	u32 mask;
	u8  bitpos;
	u8  next;
};

/* struct mtk_pin_field_calc - the structure that holds the range providing
 *			       the guide used to look up the relevant field
 * @s_pin:		the start pin within the range
 * @e_pin:		the end pin within the range
 * @s_addr:		the start address for the range
 * @x_addrs:		the address distance between two consecutive registers
 *			within the range
 * @s_bit:		the start bit for the first register within the range
 * @x_bits:		the bit distance between two consecutive pins within
 *			the range
 */
struct mtk_pin_field_calc {
	u16 s_pin;
	u16 e_pin;
	u32 s_addr;
	u8  x_addrs;
	u8  s_bit;
	u8  x_bits;
};

/* struct mtk_pin_reg_calc - the structure that holds all ranges used to
 *			     determine which register the pin would make use of
 *			     for certain pin attribute.
 * @range:		     the start address for the range
 * @nranges:		     the number of items in the range
 */
struct mtk_pin_reg_calc {
	const struct mtk_pin_field_calc *range;
	unsigned int nranges;
};

/* struct mtk_pin_soc - the structure that holds SoC-specific data */
struct mtk_pin_soc {
	const struct mtk_pin_reg_calc	*reg_cal;
	const struct pinctrl_pin_desc	*pins;
	unsigned int			npins;
	const struct group_desc		*grps;
	unsigned int			ngrps;
	const struct function_desc	*funcs;
	unsigned int			nfuncs;
	const struct mtk_eint_regs	*eint_regs;
	const struct mtk_eint_hw	*eint_hw;
};

struct mtk_pinctrl {
	struct pinctrl_dev		*pctrl;
	void __iomem			*base;
	struct device			*dev;
	struct gpio_chip		chip;
	const struct mtk_pin_soc	*soc;
	struct mtk_eint			*eint;
};

static const struct mtk_pin_field_calc mt7622_pin_mode_range[] = {
	{0, 0, 0x320, 0x10, 16, 4},
	{1, 4, 0x3a0, 0x10,  16, 4},
	{5, 5, 0x320, 0x10,  0, 4},
	{6, 6, 0x300, 0x10,  4, 4},
	{7, 7, 0x300, 0x10,  4, 4},
	{8, 9, 0x350, 0x10,  20, 4},
	{10, 10, 0x300, 0x10, 8, 4},
	{11, 11, 0x300, 0x10, 8, 4},
	{12, 12, 0x300, 0x10, 8, 4},
	{13, 13, 0x300, 0x10, 8, 4},
	{14, 15, 0x320, 0x10, 4, 4},
	{16, 17, 0x320, 0x10, 20, 4},
	{18, 21, 0x310, 0x10, 16, 4},
	{22, 22, 0x380, 0x10, 16, 4},
	{23, 23, 0x300,	0x10, 24, 4},
	{24, 24, 0x300, 0x10, 24, 4},
	{25, 25, 0x300, 0x10, 12, 4},
	{25, 25, 0x300, 0x10, 12, 4},
	{26, 26, 0x300, 0x10, 12, 4},
	{27, 27, 0x300, 0x10, 12, 4},
	{28, 28, 0x300, 0x10, 12, 4},
	{29, 29, 0x300, 0x10, 12, 4},
	{30, 30, 0x300, 0x10, 12, 4},
	{31, 31, 0x300, 0x10, 12, 4},
	{32, 32, 0x300, 0x10, 12, 4},
	{33, 33, 0x300,	0x10, 12, 4},
	{34, 34, 0x300,	0x10, 12, 4},
	{35, 35, 0x300,	0x10, 12, 4},
	{36, 36, 0x300, 0x10, 12, 4},
	{37, 37, 0x300, 0x10, 20, 4},
	{38, 38, 0x300, 0x10, 20, 4},
	{39, 39, 0x300, 0x10, 20, 4},
	{40, 40, 0x300, 0x10, 20, 4},
	{41, 41, 0x300,	0x10, 20, 4},
	{42, 42, 0x300, 0x10, 20, 4},
	{43, 43, 0x300,	0x10, 20, 4},
	{44, 44, 0x300, 0x10, 20, 4},
	{45, 46, 0x300, 0x10, 20, 4},
	{47, 47, 0x300,	0x10, 20, 4},
	{48, 48, 0x300, 0x10, 20, 4},
	{49, 49, 0x300, 0x10, 20, 4},
	{50, 50, 0x300, 0x10, 20, 4},
	{51, 70, 0x330, 0x10, 4, 4},
	{71, 71, 0x300, 0x10, 16, 4},
	{72, 72, 0x300, 0x10, 16, 4},
	{73, 76, 0x310, 0x10, 0, 4},
	{77, 77, 0x320, 0x10, 28, 4},
	{78, 78, 0x320, 0x10, 12, 4},
	{79, 82, 0x3a0, 0x10, 0, 4},
	{83, 83, 0x350,	0x10, 28, 4},
	{84, 84, 0x330, 0x10, 0, 4},
	{85, 90, 0x360, 0x10, 4, 4},
	{91, 94, 0x390, 0x10, 16, 4},
	{95, 97, 0x380, 0x10, 20, 4},
	{98, 101, 0x390, 0x10, 0, 4},
	{102, 102, 0x360, 0x10, 0, 4},
};

static const struct mtk_pin_field_calc mt7622_pin_dir_range[] = {
	{0, 102, 0x0, 0x10, 0, 1},
};

static const struct mtk_pin_field_calc mt7622_pin_di_range[] = {
	{0, 102, 0x200, 0x10, 0, 1},
};

static const struct mtk_pin_field_calc mt7622_pin_do_range[] = {
	{0, 102, 0x100, 0x10, 0, 1},
};

static const struct mtk_pin_field_calc mt7622_pin_sr_range[] = {
	{0, 31, 0x910, 0x10, 0, 1},
	{32, 50, 0xa10, 0x10, 0, 1},
	{51, 70, 0x810, 0x10, 0, 1},
	{71, 72, 0xb10, 0x10, 0, 1},
	{73, 86, 0xb10, 0x10, 4, 1},
	{87, 90, 0xc10, 0x10, 0, 1},
	{91, 102, 0xb10, 0x10, 18, 1},
};

static const struct mtk_pin_field_calc mt7622_pin_smt_range[] = {
	{0, 31, 0x920, 0x10, 0, 1},
	{32, 50, 0xa20, 0x10, 0, 1},
	{51, 70, 0x820, 0x10, 0, 1},
	{71, 72, 0xb20, 0x10, 0, 1},
	{73, 86, 0xb20, 0x10, 4, 1},
	{87, 90, 0xc20, 0x10, 0, 1},
	{91, 102, 0xb20, 0x10, 18, 1},
};

static const struct mtk_pin_field_calc mt7622_pin_pu_range[] = {
	{0, 31, 0x930, 0x10, 0, 1},
	{32, 50, 0xa30, 0x10, 0, 1},
	{51, 70, 0x830, 0x10, 0, 1},
	{71, 72, 0xb30, 0x10, 0, 1},
	{73, 86, 0xb30, 0x10, 4, 1},
	{87, 90, 0xc30, 0x10, 0, 1},
	{91, 102, 0xb30, 0x10, 18, 1},
};

static const struct mtk_pin_field_calc mt7622_pin_pd_range[] = {
	{0, 31, 0x940, 0x10, 0, 1},
	{32, 50, 0xa40, 0x10, 0, 1},
	{51, 70, 0x840, 0x10, 0, 1},
	{71, 72, 0xb40, 0x10, 0, 1},
	{73, 86, 0xb40, 0x10, 4, 1},
	{87, 90, 0xc40, 0x10, 0, 1},
	{91, 102, 0xb40, 0x10, 18, 1},
};

static const struct mtk_pin_field_calc mt7622_pin_e4_range[] = {
	{0, 31, 0x960, 0x10, 0, 1},
	{32, 50, 0xa60, 0x10, 0, 1},
	{51, 70, 0x860, 0x10, 0, 1},
	{71, 72, 0xb60, 0x10, 0, 1},
	{73, 86, 0xb60, 0x10, 4, 1},
	{87, 90, 0xc60, 0x10, 0, 1},
	{91, 102, 0xb60, 0x10, 18, 1},
};

static const struct mtk_pin_field_calc mt7622_pin_e8_range[] = {
	{0, 31, 0x970, 0x10, 0, 1},
	{32, 50, 0xa70, 0x10, 0, 1},
	{51, 70, 0x870, 0x10, 0, 1},
	{71, 72, 0xb70, 0x10, 0, 1},
	{73, 86, 0xb70, 0x10, 4, 1},
	{87, 90, 0xc70, 0x10, 0, 1},
	{91, 102, 0xb70, 0x10, 18, 1},
};

static const struct mtk_pin_field_calc mt7622_pin_tdsel_range[] = {
	{0, 31, 0x980, 0x4, 0, 4},
	{32, 50, 0xa80, 0x4, 0, 4},
	{51, 70, 0x880, 0x4, 0, 4},
	{71, 72, 0xb80, 0x4, 0, 4},
	{73, 86, 0xb80, 0x4, 16, 4},
	{87, 90, 0xc80, 0x4, 0, 4},
	{91, 102, 0xb88, 0x4, 8, 4},
};

static const struct mtk_pin_field_calc mt7622_pin_rdsel_range[] = {
	{0, 31, 0x990, 0x4, 0, 6},
	{32, 50, 0xa90, 0x4, 0, 6},
	{51, 58, 0x890, 0x4, 0, 6},
	{59, 60, 0x894, 0x4, 28, 6},
	{61, 62, 0x894, 0x4, 16, 6},
	{63, 66, 0x898, 0x4, 8, 6},
	{67, 68, 0x89c, 0x4, 12, 6},
	{69, 70, 0x89c, 0x4, 0, 6},
	{71, 72, 0xb90, 0x4, 0, 6},
	{73, 86, 0xb90, 0x4, 24, 6},
	{87, 90, 0xc90, 0x4, 0, 6},
	{91, 102, 0xb9c, 0x4, 12, 6},
};

static const struct mtk_pin_reg_calc mt7622_reg_cals[PINCTRL_PIN_REG_MAX] = {
	[PINCTRL_PIN_REG_MODE] = MTK_RANGE(mt7622_pin_mode_range),
	[PINCTRL_PIN_REG_DIR] = MTK_RANGE(mt7622_pin_dir_range),
	[PINCTRL_PIN_REG_DI] = MTK_RANGE(mt7622_pin_di_range),
	[PINCTRL_PIN_REG_DO] = MTK_RANGE(mt7622_pin_do_range),
	[PINCTRL_PIN_REG_SR] = MTK_RANGE(mt7622_pin_sr_range),
	[PINCTRL_PIN_REG_SMT] = MTK_RANGE(mt7622_pin_smt_range),
	[PINCTRL_PIN_REG_PU] = MTK_RANGE(mt7622_pin_pu_range),
	[PINCTRL_PIN_REG_PD] = MTK_RANGE(mt7622_pin_pd_range),
	[PINCTRL_PIN_REG_E4] = MTK_RANGE(mt7622_pin_e4_range),
	[PINCTRL_PIN_REG_E8] = MTK_RANGE(mt7622_pin_e8_range),
	[PINCTRL_PIN_REG_TDSEL] = MTK_RANGE(mt7622_pin_tdsel_range),
	[PINCTRL_PIN_REG_RDSEL] = MTK_RANGE(mt7622_pin_rdsel_range),
};

static const struct pinctrl_pin_desc mt7622_pins[] = {
	PINCTRL_PIN(0, "GPIO_A"),
	PINCTRL_PIN(1, "I2S1_IN"),
	PINCTRL_PIN(2, "I2S1_OUT"),
	PINCTRL_PIN(3, "I2S_BCLK"),
	PINCTRL_PIN(4, "I2S_WS"),
	PINCTRL_PIN(5, "I2S_MCLK"),
	PINCTRL_PIN(6, "TXD0"),
	PINCTRL_PIN(7, "RXD0"),
	PINCTRL_PIN(8, "SPI_WP"),
	PINCTRL_PIN(9, "SPI_HOLD"),
	PINCTRL_PIN(10, "SPI_CLK"),
	PINCTRL_PIN(11, "SPI_MOSI"),
	PINCTRL_PIN(12, "SPI_MISO"),
	PINCTRL_PIN(13, "SPI_CS"),
	PINCTRL_PIN(14, "I2C_SDA"),
	PINCTRL_PIN(15, "I2C_SCL"),
	PINCTRL_PIN(16, "I2S2_IN"),
	PINCTRL_PIN(17, "I2S3_IN"),
	PINCTRL_PIN(18, "I2S4_IN"),
	PINCTRL_PIN(19, "I2S2_OUT"),
	PINCTRL_PIN(20, "I2S3_OUT"),
	PINCTRL_PIN(21, "I2S4_OUT"),
	PINCTRL_PIN(22, "GPIO_B"),
	PINCTRL_PIN(23, "MDC"),
	PINCTRL_PIN(24, "MDIO"),
	PINCTRL_PIN(25, "G2_TXD0"),
	PINCTRL_PIN(26, "G2_TXD1"),
	PINCTRL_PIN(27, "G2_TXD2"),
	PINCTRL_PIN(28, "G2_TXD3"),
	PINCTRL_PIN(29, "G2_TXEN"),
	PINCTRL_PIN(30, "G2_TXC"),
	PINCTRL_PIN(31, "G2_RXD0"),
	PINCTRL_PIN(32, "G2_RXD1"),
	PINCTRL_PIN(33, "G2_RXD2"),
	PINCTRL_PIN(34, "G2_RXD3"),
	PINCTRL_PIN(35, "G2_RXDV"),
	PINCTRL_PIN(36, "G2_RXC"),
	PINCTRL_PIN(37, "NCEB"),
	PINCTRL_PIN(38, "NWEB"),
	PINCTRL_PIN(39, "NREB"),
	PINCTRL_PIN(40, "NDL4"),
	PINCTRL_PIN(41, "NDL5"),
	PINCTRL_PIN(42, "NDL6"),
	PINCTRL_PIN(43, "NDL7"),
	PINCTRL_PIN(44, "NRB"),
	PINCTRL_PIN(45, "NCLE"),
	PINCTRL_PIN(46, "NALE"),
	PINCTRL_PIN(47, "NDL0"),
	PINCTRL_PIN(48, "NDL1"),
	PINCTRL_PIN(49, "NDL2"),
	PINCTRL_PIN(50, "NDL3"),
	PINCTRL_PIN(51, "MDI_TP_P0"),
	PINCTRL_PIN(52, "MDI_TN_P0"),
	PINCTRL_PIN(53, "MDI_RP_P0"),
	PINCTRL_PIN(54, "MDI_RN_P0"),
	PINCTRL_PIN(55, "MDI_TP_P1"),
	PINCTRL_PIN(56, "MDI_TN_P1"),
	PINCTRL_PIN(57, "MDI_RP_P1"),
	PINCTRL_PIN(58, "MDI_RN_P1"),
	PINCTRL_PIN(59, "MDI_RP_P2"),
	PINCTRL_PIN(60, "MDI_RN_P2"),
	PINCTRL_PIN(61, "MDI_TP_P2"),
	PINCTRL_PIN(62, "MDI_TN_P2"),
	PINCTRL_PIN(63, "MDI_TP_P3"),
	PINCTRL_PIN(64, "MDI_TN_P3"),
	PINCTRL_PIN(65, "MDI_RP_P3"),
	PINCTRL_PIN(66, "MDI_RN_P3"),
	PINCTRL_PIN(67, "MDI_RP_P4"),
	PINCTRL_PIN(68, "MDI_RN_P4"),
	PINCTRL_PIN(69, "MDI_TP_P4"),
	PINCTRL_PIN(70, "MDI_TN_P4"),
	PINCTRL_PIN(71, "PMIC_SCL"),
	PINCTRL_PIN(72, "PMIC_SDA"),
	PINCTRL_PIN(73, "SPIC1_CLK"),
	PINCTRL_PIN(74, "SPIC1_MOSI"),
	PINCTRL_PIN(75, "SPIC1_MISO"),
	PINCTRL_PIN(76, "SPIC1_CS"),
	PINCTRL_PIN(77, "GPIO_D"),
	PINCTRL_PIN(78, "WATCHDOG"),
	PINCTRL_PIN(79, "RTS3_N"),
	PINCTRL_PIN(80, "CTS3_N"),
	PINCTRL_PIN(81, "TXD3"),
	PINCTRL_PIN(82, "RXD3"),
	PINCTRL_PIN(83, "PERST0_N"),
	PINCTRL_PIN(84, "PERST1_N"),
	PINCTRL_PIN(85, "WLED_N"),
	PINCTRL_PIN(86, "EPHY_LED0_N"),
	PINCTRL_PIN(87, "AUXIN0"),
	PINCTRL_PIN(88, "AUXIN1"),
	PINCTRL_PIN(89, "AUXIN2"),
	PINCTRL_PIN(90, "AUXIN3"),
	PINCTRL_PIN(91, "TXD4"),
	PINCTRL_PIN(92, "RXD4"),
	PINCTRL_PIN(93, "RTS4_N"),
	PINCTRL_PIN(94, "CTS4_N"),
	PINCTRL_PIN(95, "PWM1"),
	PINCTRL_PIN(96, "PWM2"),
	PINCTRL_PIN(97, "PWM3"),
	PINCTRL_PIN(98, "PWM4"),
	PINCTRL_PIN(99, "PWM5"),
	PINCTRL_PIN(100, "PWM6"),
	PINCTRL_PIN(101, "PWM7"),
	PINCTRL_PIN(102, "GPIO_E"),
};

/* List all groups consisting of these pins dedicated to the enablement of
 * certain hardware block and the corresponding mode for all of the pins. The
 * hardware probably has multiple combinations of these pinouts.
 */

/* EMMC */
static int mt7622_emmc_pins[] = { 40, 41, 42, 43, 44, 45, 47, 48, 49, 50, };
static int mt7622_emmc_funcs[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, };

static int mt7622_emmc_rst_pins[] = { 37, };
static int mt7622_emmc_rst_funcs[] = { 1, };

/* LED for EPHY */
static int mt7622_ephy_leds_pins[] = { 86, 91, 92, 93, 94, };
static int mt7622_ephy_leds_funcs[] = { 0, 0, 0, 0, 0, };
static int mt7622_ephy0_led_pins[] = { 86, };
static int mt7622_ephy0_led_funcs[] = { 0, };
static int mt7622_ephy1_led_pins[] = { 91, };
static int mt7622_ephy1_led_funcs[] = { 2, };
static int mt7622_ephy2_led_pins[] = { 92, };
static int mt7622_ephy2_led_funcs[] = { 2, };
static int mt7622_ephy3_led_pins[] = { 93, };
static int mt7622_ephy3_led_funcs[] = { 2, };
static int mt7622_ephy4_led_pins[] = { 94, };
static int mt7622_ephy4_led_funcs[] = { 2, };

/* Embedded Switch */
static int mt7622_esw_pins[] = { 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
				 62, 63, 64, 65, 66, 67, 68, 69, 70, };
static int mt7622_esw_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				  0, 0, 0, 0, 0, 0, 0, 0, 0, };
static int mt7622_esw_p0_p1_pins[] = { 51, 52, 53, 54, 55, 56, 57, 58, };
static int mt7622_esw_p0_p1_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, };
static int mt7622_esw_p2_p3_p4_pins[] = { 59, 60, 61, 62, 63, 64, 65, 66, 67,
					  68, 69, 70, };
static int mt7622_esw_p2_p3_p4_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0,
					   0, 0, 0, };
/* RGMII via ESW */
static int mt7622_rgmii_via_esw_pins[] = { 59, 60, 61, 62, 63, 64, 65, 66,
					   67, 68, 69, 70, };
static int mt7622_rgmii_via_esw_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					    0, };

/* RGMII via GMAC1 */
static int mt7622_rgmii_via_gmac1_pins[] = { 59, 60, 61, 62, 63, 64, 65, 66,
					     67, 68, 69, 70, };
static int mt7622_rgmii_via_gmac1_funcs[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
					      2, };

/* RGMII via GMAC2 */
static int mt7622_rgmii_via_gmac2_pins[] = { 25, 26, 27, 28, 29, 30, 31, 32,
					     33, 34, 35, 36, };
static int mt7622_rgmii_via_gmac2_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					      0, };

/* I2C */
static int mt7622_i2c0_pins[] = { 14, 15, };
static int mt7622_i2c0_funcs[] = { 0, 0, };
static int mt7622_i2c1_0_pins[] = { 55, 56, };
static int mt7622_i2c1_0_funcs[] = { 0, 0, };
static int mt7622_i2c1_1_pins[] = { 73, 74, };
static int mt7622_i2c1_1_funcs[] = { 3, 3, };
static int mt7622_i2c1_2_pins[] = { 87, 88, };
static int mt7622_i2c1_2_funcs[] = { 0, 0, };
static int mt7622_i2c2_0_pins[] = { 57, 58, };
static int mt7622_i2c2_0_funcs[] = { 0, 0, };
static int mt7622_i2c2_1_pins[] = { 75, 76, };
static int mt7622_i2c2_1_funcs[] = { 3, 3, };
static int mt7622_i2c2_2_pins[] = { 89, 90, };
static int mt7622_i2c2_2_funcs[] = { 0, 0, };

/* I2S */
static int mt7622_i2s_in_mclk_bclk_ws_pins[] = { 3, 4, 5, };
static int mt7622_i2s_in_mclk_bclk_ws_funcs[] = { 3, 3, 0, };
static int mt7622_i2s1_in_data_pins[] = { 1, };
static int mt7622_i2s1_in_data_funcs[] = { 0, };
static int mt7622_i2s2_in_data_pins[] = { 16, };
static int mt7622_i2s2_in_data_funcs[] = { 0, };
static int mt7622_i2s3_in_data_pins[] = { 17, };
static int mt7622_i2s3_in_data_funcs[] = { 0, };
static int mt7622_i2s4_in_data_pins[] = { 18, };
static int mt7622_i2s4_in_data_funcs[] = { 0, };
static int mt7622_i2s_out_mclk_bclk_ws_pins[] = { 3, 4, 5, };
static int mt7622_i2s_out_mclk_bclk_ws_funcs[] = { 0, 0, 0, };
static int mt7622_i2s1_out_data_pins[] = { 2, };
static int mt7622_i2s1_out_data_funcs[] = { 0, };
static int mt7622_i2s2_out_data_pins[] = { 19, };
static int mt7622_i2s2_out_data_funcs[] = { 0, };
static int mt7622_i2s3_out_data_pins[] = { 20, };
static int mt7622_i2s3_out_data_funcs[] = { 0, };
static int mt7622_i2s4_out_data_pins[] = { 21, };
static int mt7622_i2s4_out_data_funcs[] = { 0, };

/* IR */
static int mt7622_ir_0_tx_pins[] = { 16, };
static int mt7622_ir_0_tx_funcs[] = { 4, };
static int mt7622_ir_1_tx_pins[] = { 59, };
static int mt7622_ir_1_tx_funcs[] = { 5, };
static int mt7622_ir_2_tx_pins[] = { 99, };
static int mt7622_ir_2_tx_funcs[] = { 3, };
static int mt7622_ir_0_rx_pins[] = { 17, };
static int mt7622_ir_0_rx_funcs[] = { 4, };
static int mt7622_ir_1_rx_pins[] = { 60, };
static int mt7622_ir_1_rx_funcs[] = { 5, };
static int mt7622_ir_2_rx_pins[] = { 100, };
static int mt7622_ir_2_rx_funcs[] = { 3, };

/* MDIO */
static int mt7622_mdc_mdio_pins[] = { 23, 24, };
static int mt7622_mdc_mdio_funcs[] = { 0, 0, };

/* PCIE */
static int mt7622_pcie0_0_waken_pins[] = { 14, };
static int mt7622_pcie0_0_waken_funcs[] = { 2, };
static int mt7622_pcie0_0_clkreq_pins[] = { 15, };
static int mt7622_pcie0_0_clkreq_funcs[] = { 2, };
static int mt7622_pcie0_1_waken_pins[] = { 79, };
static int mt7622_pcie0_1_waken_funcs[] = { 4, };
static int mt7622_pcie0_1_clkreq_pins[] = { 80, };
static int mt7622_pcie0_1_clkreq_funcs[] = { 4, };
static int mt7622_pcie1_0_waken_pins[] = { 14, };
static int mt7622_pcie1_0_waken_funcs[] = { 3, };
static int mt7622_pcie1_0_clkreq_pins[] = { 15, };
static int mt7622_pcie1_0_clkreq_funcs[] = { 3, };

static int mt7622_pcie0_pad_perst_pins[] = { 83, };
static int mt7622_pcie0_pad_perst_funcs[] = { 0, };
static int mt7622_pcie1_pad_perst_pins[] = { 84, };
static int mt7622_pcie1_pad_perst_funcs[] = { 0, };

/* PMIC bus */
static int mt7622_pmic_bus_pins[] = { 71, 72, };
static int mt7622_pmic_bus_funcs[] = { 0, 0, };

/* Parallel NAND */
static int mt7622_pnand_pins[] = { 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
				   48, 49, 50, };
static int mt7622_pnand_funcs[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				    0, };

/* PWM */
static int mt7622_pwm_ch1_0_pins[] = { 51, };
static int mt7622_pwm_ch1_0_funcs[] = { 3, };
static int mt7622_pwm_ch1_1_pins[] = { 73, };
static int mt7622_pwm_ch1_1_funcs[] = { 4, };
static int mt7622_pwm_ch1_2_pins[] = { 95, };
static int mt7622_pwm_ch1_2_funcs[] = { 0, };
static int mt7622_pwm_ch2_0_pins[] = { 52, };
static int mt7622_pwm_ch2_0_funcs[] = { 3, };
static int mt7622_pwm_ch2_1_pins[] = { 74, };
static int mt7622_pwm_ch2_1_funcs[] = { 4, };
static int mt7622_pwm_ch2_2_pins[] = { 96, };
static int mt7622_pwm_ch2_2_funcs[] = { 0, };
static int mt7622_pwm_ch3_0_pins[] = { 53, };
static int mt7622_pwm_ch3_0_funcs[] = { 3, };
static int mt7622_pwm_ch3_1_pins[] = { 75, };
static int mt7622_pwm_ch3_1_funcs[] = { 4, };
static int mt7622_pwm_ch3_2_pins[] = { 97, };
static int mt7622_pwm_ch3_2_funcs[] = { 0, };
static int mt7622_pwm_ch4_0_pins[] = { 54, };
static int mt7622_pwm_ch4_0_funcs[] = { 3, };
static int mt7622_pwm_ch4_1_pins[] = { 67, };
static int mt7622_pwm_ch4_1_funcs[] = { 3, };
static int mt7622_pwm_ch4_2_pins[] = { 76, };
static int mt7622_pwm_ch4_2_funcs[] = { 4, };
static int mt7622_pwm_ch4_3_pins[] = { 98, };
static int mt7622_pwm_ch4_3_funcs[] = { 0, };
static int mt7622_pwm_ch5_0_pins[] = { 68, };
static int mt7622_pwm_ch5_0_funcs[] = { 3, };
static int mt7622_pwm_ch5_1_pins[] = { 77, };
static int mt7622_pwm_ch5_1_funcs[] = { 4, };
static int mt7622_pwm_ch5_2_pins[] = { 99, };
static int mt7622_pwm_ch5_2_funcs[] = { 0, };
static int mt7622_pwm_ch6_0_pins[] = { 69, };
static int mt7622_pwm_ch6_0_funcs[] = { 3, };
static int mt7622_pwm_ch6_1_pins[] = { 78, };
static int mt7622_pwm_ch6_1_funcs[] = { 4, };
static int mt7622_pwm_ch6_2_pins[] = { 81, };
static int mt7622_pwm_ch6_2_funcs[] = { 4, };
static int mt7622_pwm_ch6_3_pins[] = { 100, };
static int mt7622_pwm_ch6_3_funcs[] = { 0, };
static int mt7622_pwm_ch7_0_pins[] = { 70, };
static int mt7622_pwm_ch7_0_funcs[] = { 3, };
static int mt7622_pwm_ch7_1_pins[] = { 82, };
static int mt7622_pwm_ch7_1_funcs[] = { 4, };
static int mt7622_pwm_ch7_2_pins[] = { 101, };
static int mt7622_pwm_ch7_2_funcs[] = { 0, };

/* SD */
static int mt7622_sd_0_pins[] = { 16, 17, 18, 19, 20, 21, };
static int mt7622_sd_0_funcs[] = { 2, 2, 2, 2, 2, 2, };
static int mt7622_sd_1_pins[] = { 25, 26, 27, 28, 29, 30, };
static int mt7622_sd_1_funcs[] = { 2, 2, 2, 2, 2, 2, };

/* Serial NAND */
static int mt7622_snfi_pins[] = { 8, 9, 10, 11, 12, 13, };
static int mt7622_snfi_funcs[] = { 2, 2, 2, 2, 2, 2, };

/* SPI NOR */
static int mt7622_spi_pins[] = { 8, 9, 10, 11, 12, 13 };
static int mt7622_spi_funcs[] = { 0, 0, 0, 0, 0, 0, };

/* SPIC */
static int mt7622_spic0_0_pins[] = { 63, 64, 65, 66, };
static int mt7622_spic0_0_funcs[] = { 4, 4, 4, 4, };
static int mt7622_spic0_1_pins[] = { 79, 80, 81, 82, };
static int mt7622_spic0_1_funcs[] = { 3, 3, 3, 3, };
static int mt7622_spic1_0_pins[] = { 67, 68, 69, 70, };
static int mt7622_spic1_0_funcs[] = { 4, 4, 4, 4, };
static int mt7622_spic1_1_pins[] = { 73, 74, 75, 76, };
static int mt7622_spic1_1_funcs[] = { 0, 0, 0, 0, };
static int mt7622_spic2_0_pins[] = { 10, 11, 12, 13, };
static int mt7622_spic2_0_funcs[] = { 0, 0, 0, 0, };
static int mt7622_spic2_0_wp_hold_pins[] = { 8, 9, };
static int mt7622_spic2_0_wp_hold_funcs[] = { 0, 0, };

/* TDM */
static int mt7622_tdm_0_out_mclk_bclk_ws_pins[] = { 8, 9, 10, };
static int mt7622_tdm_0_out_mclk_bclk_ws_funcs[] = { 3, 3, 3, };
static int mt7622_tdm_0_in_mclk_bclk_ws_pins[] = { 11, 12, 13, };
static int mt7622_tdm_0_in_mclk_bclk_ws_funcs[] = { 3, 3, 3, };
static int mt7622_tdm_0_out_data_pins[] = { 20, };
static int mt7622_tdm_0_out_data_funcs[] = { 3, };
static int mt7622_tdm_0_in_data_pins[] = { 21, };
static int mt7622_tdm_0_in_data_funcs[] = { 3, };
static int mt7622_tdm_1_out_mclk_bclk_ws_pins[] = { 57, 58, 59, };
static int mt7622_tdm_1_out_mclk_bclk_ws_funcs[] = { 3, 3, 3, };
static int mt7622_tdm_1_in_mclk_bclk_ws_pins[] = { 60, 61, 62, };
static int mt7622_tdm_1_in_mclk_bclk_ws_funcs[] = { 3, 3, 3, };
static int mt7622_tdm_1_out_data_pins[] = { 55, };
static int mt7622_tdm_1_out_data_funcs[] = { 3, };
static int mt7622_tdm_1_in_data_pins[] = { 56, };
static int mt7622_tdm_1_in_data_funcs[] = { 3, };

/* UART */
static int mt7622_uart0_0_tx_rx_pins[] = { 6, 7, };
static int mt7622_uart0_0_tx_rx_funcs[] = { 0, 0, };
static int mt7622_uart1_0_tx_rx_pins[] = { 55, 56, };
static int mt7622_uart1_0_tx_rx_funcs[] = { 2, 2, };
static int mt7622_uart1_0_rts_cts_pins[] = { 57, 58, };
static int mt7622_uart1_0_rts_cts_funcs[] = { 2, 2, };
static int mt7622_uart1_1_tx_rx_pins[] = { 73, 74, };
static int mt7622_uart1_1_tx_rx_funcs[] = { 2, 2, };
static int mt7622_uart1_1_rts_cts_pins[] = { 75, 76, };
static int mt7622_uart1_1_rts_cts_funcs[] = { 2, 2, };
static int mt7622_uart2_0_tx_rx_pins[] = { 3, 4, };
static int mt7622_uart2_0_tx_rx_funcs[] = { 2, 2, };
static int mt7622_uart2_0_rts_cts_pins[] = { 1, 2, };
static int mt7622_uart2_0_rts_cts_funcs[] = { 2, 2, };
static int mt7622_uart2_1_tx_rx_pins[] = { 51, 52, };
static int mt7622_uart2_1_tx_rx_funcs[] = { 0, 0, };
static int mt7622_uart2_1_rts_cts_pins[] = { 53, 54, };
static int mt7622_uart2_1_rts_cts_funcs[] = { 0, 0, };
static int mt7622_uart2_2_tx_rx_pins[] = { 59, 60, };
static int mt7622_uart2_2_tx_rx_funcs[] = { 4, 4, };
static int mt7622_uart2_2_rts_cts_pins[] = { 61, 62, };
static int mt7622_uart2_2_rts_cts_funcs[] = { 4, 4, };
static int mt7622_uart2_3_tx_rx_pins[] = { 95, 96, };
static int mt7622_uart2_3_tx_rx_funcs[] = { 3, 3, };
static int mt7622_uart3_0_tx_rx_pins[] = { 57, 58, };
static int mt7622_uart3_0_tx_rx_funcs[] = { 5, 5, };
static int mt7622_uart3_1_tx_rx_pins[] = { 81, 82, };
static int mt7622_uart3_1_tx_rx_funcs[] = { 0, 0, };
static int mt7622_uart3_1_rts_cts_pins[] = { 79, 80, };
static int mt7622_uart3_1_rts_cts_funcs[] = { 0, 0, };
static int mt7622_uart4_0_tx_rx_pins[] = { 61, 62, };
static int mt7622_uart4_0_tx_rx_funcs[] = { 5, 5, };
static int mt7622_uart4_1_tx_rx_pins[] = { 91, 92, };
static int mt7622_uart4_1_tx_rx_funcs[] = { 0, 0, };
static int mt7622_uart4_1_rts_cts_pins[] = { 93, 94 };
static int mt7622_uart4_1_rts_cts_funcs[] = { 0, 0, };
static int mt7622_uart4_2_tx_rx_pins[] = { 97, 98, };
static int mt7622_uart4_2_tx_rx_funcs[] = { 2, 2, };
static int mt7622_uart4_2_rts_cts_pins[] = { 95, 96 };
static int mt7622_uart4_2_rts_cts_funcs[] = { 2, 2, };

/* Watchdog */
static int mt7622_watchdog_pins[] = { 78, };
static int mt7622_watchdog_funcs[] = { 0, };

/* WLAN LED */
static int mt7622_wled_pins[] = { 85, };
static int mt7622_wled_funcs[] = { 0, };

static const struct group_desc mt7622_groups[] = {
	PINCTRL_PIN_GROUP("emmc", mt7622_emmc),
	PINCTRL_PIN_GROUP("emmc_rst", mt7622_emmc_rst),
	PINCTRL_PIN_GROUP("ephy_leds", mt7622_ephy_leds),
	PINCTRL_PIN_GROUP("ephy0_led", mt7622_ephy0_led),
	PINCTRL_PIN_GROUP("ephy1_led", mt7622_ephy1_led),
	PINCTRL_PIN_GROUP("ephy2_led", mt7622_ephy2_led),
	PINCTRL_PIN_GROUP("ephy3_led", mt7622_ephy3_led),
	PINCTRL_PIN_GROUP("ephy4_led", mt7622_ephy4_led),
	PINCTRL_PIN_GROUP("esw", mt7622_esw),
	PINCTRL_PIN_GROUP("esw_p0_p1", mt7622_esw_p0_p1),
	PINCTRL_PIN_GROUP("esw_p2_p3_p4", mt7622_esw_p2_p3_p4),
	PINCTRL_PIN_GROUP("rgmii_via_esw", mt7622_rgmii_via_esw),
	PINCTRL_PIN_GROUP("rgmii_via_gmac1", mt7622_rgmii_via_gmac1),
	PINCTRL_PIN_GROUP("rgmii_via_gmac2", mt7622_rgmii_via_gmac2),
	PINCTRL_PIN_GROUP("i2c0", mt7622_i2c0),
	PINCTRL_PIN_GROUP("i2c1_0", mt7622_i2c1_0),
	PINCTRL_PIN_GROUP("i2c1_1", mt7622_i2c1_1),
	PINCTRL_PIN_GROUP("i2c1_2", mt7622_i2c1_2),
	PINCTRL_PIN_GROUP("i2c2_0", mt7622_i2c2_0),
	PINCTRL_PIN_GROUP("i2c2_1", mt7622_i2c2_1),
	PINCTRL_PIN_GROUP("i2c2_2", mt7622_i2c2_2),
	PINCTRL_PIN_GROUP("i2s_out_mclk_bclk_ws", mt7622_i2s_out_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("i2s_in_mclk_bclk_ws", mt7622_i2s_in_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("i2s1_in_data", mt7622_i2s1_in_data),
	PINCTRL_PIN_GROUP("i2s2_in_data", mt7622_i2s2_in_data),
	PINCTRL_PIN_GROUP("i2s3_in_data", mt7622_i2s3_in_data),
	PINCTRL_PIN_GROUP("i2s4_in_data", mt7622_i2s4_in_data),
	PINCTRL_PIN_GROUP("i2s1_out_data", mt7622_i2s1_out_data),
	PINCTRL_PIN_GROUP("i2s2_out_data", mt7622_i2s2_out_data),
	PINCTRL_PIN_GROUP("i2s3_out_data", mt7622_i2s3_out_data),
	PINCTRL_PIN_GROUP("i2s4_out_data", mt7622_i2s4_out_data),
	PINCTRL_PIN_GROUP("ir_0_tx", mt7622_ir_0_tx),
	PINCTRL_PIN_GROUP("ir_1_tx", mt7622_ir_1_tx),
	PINCTRL_PIN_GROUP("ir_2_tx", mt7622_ir_2_tx),
	PINCTRL_PIN_GROUP("ir_0_rx", mt7622_ir_0_rx),
	PINCTRL_PIN_GROUP("ir_1_rx", mt7622_ir_1_rx),
	PINCTRL_PIN_GROUP("ir_2_rx", mt7622_ir_2_rx),
	PINCTRL_PIN_GROUP("mdc_mdio", mt7622_mdc_mdio),
	PINCTRL_PIN_GROUP("pcie0_0_waken", mt7622_pcie0_0_waken),
	PINCTRL_PIN_GROUP("pcie0_0_clkreq", mt7622_pcie0_0_clkreq),
	PINCTRL_PIN_GROUP("pcie0_1_waken", mt7622_pcie0_1_waken),
	PINCTRL_PIN_GROUP("pcie0_1_clkreq", mt7622_pcie0_1_clkreq),
	PINCTRL_PIN_GROUP("pcie1_0_waken", mt7622_pcie1_0_waken),
	PINCTRL_PIN_GROUP("pcie1_0_clkreq", mt7622_pcie1_0_clkreq),
	PINCTRL_PIN_GROUP("pcie0_pad_perst", mt7622_pcie0_pad_perst),
	PINCTRL_PIN_GROUP("pcie1_pad_perst", mt7622_pcie1_pad_perst),
	PINCTRL_PIN_GROUP("par_nand", mt7622_pnand),
	PINCTRL_PIN_GROUP("pmic_bus", mt7622_pmic_bus),
	PINCTRL_PIN_GROUP("pwm_ch1_0", mt7622_pwm_ch1_0),
	PINCTRL_PIN_GROUP("pwm_ch1_1", mt7622_pwm_ch1_1),
	PINCTRL_PIN_GROUP("pwm_ch1_2", mt7622_pwm_ch1_2),
	PINCTRL_PIN_GROUP("pwm_ch2_0", mt7622_pwm_ch2_0),
	PINCTRL_PIN_GROUP("pwm_ch2_1", mt7622_pwm_ch2_1),
	PINCTRL_PIN_GROUP("pwm_ch2_2", mt7622_pwm_ch2_2),
	PINCTRL_PIN_GROUP("pwm_ch3_0", mt7622_pwm_ch3_0),
	PINCTRL_PIN_GROUP("pwm_ch3_1", mt7622_pwm_ch3_1),
	PINCTRL_PIN_GROUP("pwm_ch3_2", mt7622_pwm_ch3_2),
	PINCTRL_PIN_GROUP("pwm_ch4_0", mt7622_pwm_ch4_0),
	PINCTRL_PIN_GROUP("pwm_ch4_1", mt7622_pwm_ch4_1),
	PINCTRL_PIN_GROUP("pwm_ch4_2", mt7622_pwm_ch4_2),
	PINCTRL_PIN_GROUP("pwm_ch4_3", mt7622_pwm_ch4_3),
	PINCTRL_PIN_GROUP("pwm_ch5_0", mt7622_pwm_ch5_0),
	PINCTRL_PIN_GROUP("pwm_ch5_1", mt7622_pwm_ch5_1),
	PINCTRL_PIN_GROUP("pwm_ch5_2", mt7622_pwm_ch5_2),
	PINCTRL_PIN_GROUP("pwm_ch6_0", mt7622_pwm_ch6_0),
	PINCTRL_PIN_GROUP("pwm_ch6_1", mt7622_pwm_ch6_1),
	PINCTRL_PIN_GROUP("pwm_ch6_2", mt7622_pwm_ch6_2),
	PINCTRL_PIN_GROUP("pwm_ch6_3", mt7622_pwm_ch6_3),
	PINCTRL_PIN_GROUP("pwm_ch7_0", mt7622_pwm_ch7_0),
	PINCTRL_PIN_GROUP("pwm_ch7_1", mt7622_pwm_ch7_1),
	PINCTRL_PIN_GROUP("pwm_ch7_2", mt7622_pwm_ch7_2),
	PINCTRL_PIN_GROUP("sd_0", mt7622_sd_0),
	PINCTRL_PIN_GROUP("sd_1", mt7622_sd_1),
	PINCTRL_PIN_GROUP("snfi", mt7622_snfi),
	PINCTRL_PIN_GROUP("spi_nor", mt7622_spi),
	PINCTRL_PIN_GROUP("spic0_0", mt7622_spic0_0),
	PINCTRL_PIN_GROUP("spic0_1", mt7622_spic0_1),
	PINCTRL_PIN_GROUP("spic1_0", mt7622_spic1_0),
	PINCTRL_PIN_GROUP("spic1_1", mt7622_spic1_1),
	PINCTRL_PIN_GROUP("spic2_0", mt7622_spic2_0),
	PINCTRL_PIN_GROUP("spic2_0_wp_hold", mt7622_spic2_0_wp_hold),
	PINCTRL_PIN_GROUP("tdm_0_out_mclk_bclk_ws",
			  mt7622_tdm_0_out_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("tdm_0_in_mclk_bclk_ws",
			  mt7622_tdm_0_in_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("tdm_0_out_data",  mt7622_tdm_0_out_data),
	PINCTRL_PIN_GROUP("tdm_0_in_data", mt7622_tdm_0_in_data),
	PINCTRL_PIN_GROUP("tdm_1_out_mclk_bclk_ws",
			  mt7622_tdm_1_out_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("tdm_1_in_mclk_bclk_ws",
			  mt7622_tdm_1_in_mclk_bclk_ws),
	PINCTRL_PIN_GROUP("tdm_1_out_data",  mt7622_tdm_1_out_data),
	PINCTRL_PIN_GROUP("tdm_1_in_data", mt7622_tdm_1_in_data),
	PINCTRL_PIN_GROUP("uart0_0_tx_rx", mt7622_uart0_0_tx_rx),
	PINCTRL_PIN_GROUP("uart1_0_tx_rx", mt7622_uart1_0_tx_rx),
	PINCTRL_PIN_GROUP("uart1_0_rts_cts", mt7622_uart1_0_rts_cts),
	PINCTRL_PIN_GROUP("uart1_1_tx_rx", mt7622_uart1_1_tx_rx),
	PINCTRL_PIN_GROUP("uart1_1_rts_cts", mt7622_uart1_1_rts_cts),
	PINCTRL_PIN_GROUP("uart2_0_tx_rx", mt7622_uart2_0_tx_rx),
	PINCTRL_PIN_GROUP("uart2_0_rts_cts", mt7622_uart2_0_rts_cts),
	PINCTRL_PIN_GROUP("uart2_1_tx_rx", mt7622_uart2_1_tx_rx),
	PINCTRL_PIN_GROUP("uart2_1_rts_cts", mt7622_uart2_1_rts_cts),
	PINCTRL_PIN_GROUP("uart2_2_tx_rx", mt7622_uart2_2_tx_rx),
	PINCTRL_PIN_GROUP("uart2_2_rts_cts", mt7622_uart2_2_rts_cts),
	PINCTRL_PIN_GROUP("uart2_3_tx_rx", mt7622_uart2_3_tx_rx),
	PINCTRL_PIN_GROUP("uart3_0_tx_rx", mt7622_uart3_0_tx_rx),
	PINCTRL_PIN_GROUP("uart3_1_tx_rx", mt7622_uart3_1_tx_rx),
	PINCTRL_PIN_GROUP("uart3_1_rts_cts", mt7622_uart3_1_rts_cts),
	PINCTRL_PIN_GROUP("uart4_0_tx_rx", mt7622_uart4_0_tx_rx),
	PINCTRL_PIN_GROUP("uart4_1_tx_rx", mt7622_uart4_1_tx_rx),
	PINCTRL_PIN_GROUP("uart4_1_rts_cts", mt7622_uart4_1_rts_cts),
	PINCTRL_PIN_GROUP("uart4_2_tx_rx", mt7622_uart4_2_tx_rx),
	PINCTRL_PIN_GROUP("uart4_2_rts_cts", mt7622_uart4_2_rts_cts),
	PINCTRL_PIN_GROUP("watchdog", mt7622_watchdog),
	PINCTRL_PIN_GROUP("wled", mt7622_wled),
};

/* Joint those groups owning the same capability in user point of view which
 * allows that people tend to use through the device tree.
 */
static const char *mt7622_emmc_groups[] = { "emmc", "emmc_rst", };
static const char *mt7622_ethernet_groups[] = { "esw", "esw_p0_p1",
						"esw_p2_p3_p4", "mdc_mdio",
						"rgmii_via_gmac1",
						"rgmii_via_gmac2",
						"rgmii_via_esw", };
static const char *mt7622_i2c_groups[] = { "i2c0", "i2c1_0", "i2c1_1",
					   "i2c1_2", "i2c2_0", "i2c2_1",
					   "i2c2_2", };
static const char *mt7622_i2s_groups[] = { "i2s_out_mclk_bclk_ws",
					   "i2s_in_mclk_bclk_ws",
					   "i2s1_in_data", "i2s2_in_data",
					   "i2s3_in_data", "i2s4_in_data",
					   "i2s1_out_data", "i2s2_out_data",
					   "i2s3_out_data", "i2s4_out_data", };
static const char *mt7622_ir_groups[] = { "ir_0_tx", "ir_1_tx", "ir_2_tx",
					  "ir_0_rx", "ir_1_rx", "ir_2_rx"};
static const char *mt7622_led_groups[] = { "ephy_leds", "ephy0_led",
					   "ephy1_led", "ephy2_led",
					   "ephy3_led", "ephy4_led",
					   "wled", };
static const char *mt7622_flash_groups[] = { "par_nand", "snfi", "spi_nor"};
static const char *mt7622_pcie_groups[] = { "pcie0_0_waken", "pcie0_0_clkreq",
					    "pcie0_1_waken", "pcie0_1_clkreq",
					    "pcie1_0_waken", "pcie1_0_clkreq",
					    "pcie0_pad_perst",
					    "pcie1_pad_perst", };
static const char *mt7622_pmic_bus_groups[] = { "pmic_bus", };
static const char *mt7622_pwm_groups[] = { "pwm_ch1_0", "pwm_ch1_1",
					   "pwm_ch1_2", "pwm_ch2_0",
					   "pwm_ch2_1", "pwm_ch2_2",
					   "pwm_ch3_0", "pwm_ch3_1",
					   "pwm_ch3_2", "pwm_ch4_0",
					   "pwm_ch4_1", "pwm_ch4_2",
					   "pwm_ch4_3", "pwm_ch5_0",
					   "pwm_ch5_1", "pwm_ch5_2",
					   "pwm_ch6_0", "pwm_ch6_1",
					   "pwm_ch6_2", "pwm_ch6_3",
					   "pwm_ch7_0", "pwm_ch7_1",
					   "pwm_ch7_2", };
static const char *mt7622_sd_groups[] = { "sd_0", "sd_1", };
static const char *mt7622_spic_groups[] = { "spic0_0", "spic0_1", "spic1_0",
					    "spic1_1", "spic2_0",
					    "spic2_0_wp_hold", };
static const char *mt7622_tdm_groups[] = { "tdm_0_out_mclk_bclk_ws",
					   "tdm_0_in_mclk_bclk_ws",
					   "tdm_0_out_data",
					   "tdm_0_in_data",
					   "tdm_1_out_mclk_bclk_ws",
					   "tdm_1_in_mclk_bclk_ws",
					   "tdm_1_out_data",
					   "tdm_1_in_data", };

static const char *mt7622_uart_groups[] = { "uart0_0_tx_rx",
					    "uart1_0_tx_rx", "uart1_0_rts_cts",
					    "uart1_1_tx_rx", "uart1_1_rts_cts",
					    "uart2_0_tx_rx", "uart2_0_rts_cts",
					    "uart2_1_tx_rx", "uart2_1_rts_cts",
					    "uart2_2_tx_rx", "uart2_2_rts_cts",
					    "uart2_3_tx_rx",
					    "uart3_0_tx_rx",
					    "uart3_1_tx_rx", "uart3_1_rts_cts",
					    "uart4_0_tx_rx",
					    "uart4_1_tx_rx", "uart4_1_rts_cts",
					    "uart4_2_tx_rx",
					    "uart4_2_rts_cts",};
static const char *mt7622_wdt_groups[] = { "watchdog", };

static const struct function_desc mt7622_functions[] = {
	{"emmc", mt7622_emmc_groups, ARRAY_SIZE(mt7622_emmc_groups)},
	{"eth",	mt7622_ethernet_groups, ARRAY_SIZE(mt7622_ethernet_groups)},
	{"i2c", mt7622_i2c_groups, ARRAY_SIZE(mt7622_i2c_groups)},
	{"i2s",	mt7622_i2s_groups, ARRAY_SIZE(mt7622_i2s_groups)},
	{"ir", mt7622_ir_groups, ARRAY_SIZE(mt7622_ir_groups)},
	{"led",	mt7622_led_groups, ARRAY_SIZE(mt7622_led_groups)},
	{"flash", mt7622_flash_groups, ARRAY_SIZE(mt7622_flash_groups)},
	{"pcie", mt7622_pcie_groups, ARRAY_SIZE(mt7622_pcie_groups)},
	{"pmic", mt7622_pmic_bus_groups, ARRAY_SIZE(mt7622_pmic_bus_groups)},
	{"pwm",	mt7622_pwm_groups, ARRAY_SIZE(mt7622_pwm_groups)},
	{"sd", mt7622_sd_groups, ARRAY_SIZE(mt7622_sd_groups)},
	{"spi",	mt7622_spic_groups, ARRAY_SIZE(mt7622_spic_groups)},
	{"tdm",	mt7622_tdm_groups, ARRAY_SIZE(mt7622_tdm_groups)},
	{"uart", mt7622_uart_groups, ARRAY_SIZE(mt7622_uart_groups)},
	{"watchdog", mt7622_wdt_groups, ARRAY_SIZE(mt7622_wdt_groups)},
};

static const struct pinconf_generic_params mtk_custom_bindings[] = {
	{"mediatek,tdsel",	MTK_PIN_CONFIG_TDSEL,		0},
	{"mediatek,rdsel",	MTK_PIN_CONFIG_RDSEL,		0},
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item mtk_conf_items[] = {
	PCONFDUMP(MTK_PIN_CONFIG_TDSEL, "tdsel", NULL, true),
	PCONFDUMP(MTK_PIN_CONFIG_RDSEL, "rdsel", NULL, true),
};
#endif

static const struct mtk_eint_hw mt7622_eint_hw = {
	.port_mask = 7,
	.ports     = 7,
	.ap_num    = ARRAY_SIZE(mt7622_pins),
	.db_cnt    = 20,
};

static const struct mtk_pin_soc mt7622_data = {
	.reg_cal = mt7622_reg_cals,
	.pins = mt7622_pins,
	.npins = ARRAY_SIZE(mt7622_pins),
	.grps = mt7622_groups,
	.ngrps = ARRAY_SIZE(mt7622_groups),
	.funcs = mt7622_functions,
	.nfuncs = ARRAY_SIZE(mt7622_functions),
	.eint_hw = &mt7622_eint_hw,
};

static void mtk_w32(struct mtk_pinctrl *pctl, u32 reg, u32 val)
{
	writel_relaxed(val, pctl->base + reg);
}

static u32 mtk_r32(struct mtk_pinctrl *pctl, u32 reg)
{
	return readl_relaxed(pctl->base + reg);
}

static void mtk_rmw(struct mtk_pinctrl *pctl, u32 reg, u32 mask, u32 set)
{
	u32 val;

	val = mtk_r32(pctl, reg);
	val &= ~mask;
	val |= set;
	mtk_w32(pctl, reg, val);
}

static int mtk_hw_pin_field_lookup(struct mtk_pinctrl *hw, int pin,
				   const struct mtk_pin_reg_calc *rc,
				   struct mtk_pin_field *pfd)
{
	const struct mtk_pin_field_calc *c, *e;
	u32 bits;

	c = rc->range;
	e = c + rc->nranges;

	while (c < e) {
		if (pin >= c->s_pin && pin <= c->e_pin)
			break;
		c++;
	}

	if (c >= e) {
		dev_err(hw->dev, "Out of range for pin = %d\n", pin);
		return -EINVAL;
	}

	/* Caculated bits as the overall offset the pin is located at */
	bits = c->s_bit + (pin - c->s_pin) * (c->x_bits);

	/* Fill pfd from bits and 32-bit register applied is assumed */
	pfd->offset = c->s_addr + c->x_addrs * (bits / 32);
	pfd->bitpos = bits % 32;
	pfd->mask = (1 << c->x_bits) - 1;

	/* pfd->next is used for indicating that bit wrapping-around happens
	 * which requires the manipulation for bit 0 starting in the next
	 * register to form the complete field read/write.
	 */
	pfd->next = pfd->bitpos + c->x_bits - 1 > 31 ? c->x_addrs : 0;

	return 0;
}

static int mtk_hw_pin_field_get(struct mtk_pinctrl *hw, int pin,
				int field, struct mtk_pin_field *pfd)
{
	const struct mtk_pin_reg_calc *rc;

	if (field < 0 || field >= PINCTRL_PIN_REG_MAX) {
		dev_err(hw->dev, "Invalid Field %d\n", field);
		return -EINVAL;
	}

	if (hw->soc->reg_cal && hw->soc->reg_cal[field].range) {
		rc = &hw->soc->reg_cal[field];
	} else {
		dev_err(hw->dev, "Undefined range for field %d\n", field);
		return -EINVAL;
	}

	return mtk_hw_pin_field_lookup(hw, pin, rc, pfd);
}

static void mtk_hw_bits_part(struct mtk_pin_field *pf, int *h, int *l)
{
	*l = 32 - pf->bitpos;
	*h = get_count_order(pf->mask) - *l;
}

static void mtk_hw_write_cross_field(struct mtk_pinctrl *hw,
				     struct mtk_pin_field *pf, int value)
{
	int nbits_l, nbits_h;

	mtk_hw_bits_part(pf, &nbits_h, &nbits_l);

	mtk_rmw(hw, pf->offset, pf->mask << pf->bitpos,
		(value & pf->mask) << pf->bitpos);

	mtk_rmw(hw, pf->offset + pf->next, BIT(nbits_h) - 1,
		(value & pf->mask) >> nbits_l);
}

static void mtk_hw_read_cross_field(struct mtk_pinctrl *hw,
				    struct mtk_pin_field *pf, int *value)
{
	int nbits_l, nbits_h, h, l;

	mtk_hw_bits_part(pf, &nbits_h, &nbits_l);

	l  = (mtk_r32(hw, pf->offset) >> pf->bitpos) & (BIT(nbits_l) - 1);
	h  = (mtk_r32(hw, pf->offset + pf->next)) & (BIT(nbits_h) - 1);

	*value = (h << nbits_l) | l;
}

static int mtk_hw_set_value(struct mtk_pinctrl *hw, int pin, int field,
			    int value)
{
	struct mtk_pin_field pf;
	int err;

	err = mtk_hw_pin_field_get(hw, pin, field, &pf);
	if (err)
		return err;

	if (!pf.next)
		mtk_rmw(hw, pf.offset, pf.mask << pf.bitpos,
			(value & pf.mask) << pf.bitpos);
	else
		mtk_hw_write_cross_field(hw, &pf, value);

	return 0;
}

static int mtk_hw_get_value(struct mtk_pinctrl *hw, int pin, int field,
			    int *value)
{
	struct mtk_pin_field pf;
	int err;

	err = mtk_hw_pin_field_get(hw, pin, field, &pf);
	if (err)
		return err;

	if (!pf.next)
		*value = (mtk_r32(hw, pf.offset) >> pf.bitpos) & pf.mask;
	else
		mtk_hw_read_cross_field(hw, &pf, value);

	return 0;
}

static int mtk_pinmux_set_mux(struct pinctrl_dev *pctldev,
			      unsigned int selector, unsigned int group)
{
	struct mtk_pinctrl *hw = pinctrl_dev_get_drvdata(pctldev);
	struct function_desc *func;
	struct group_desc *grp;
	int i;

	func = pinmux_generic_get_function(pctldev, selector);
	if (!func)
		return -EINVAL;

	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return -EINVAL;

	dev_dbg(pctldev->dev, "enable function %s group %s\n",
		func->name, grp->name);

	for (i = 0; i < grp->num_pins; i++) {
		int *pin_modes = grp->data;

		mtk_hw_set_value(hw, grp->pins[i], PINCTRL_PIN_REG_MODE,
				 pin_modes[i]);
	}

	return 0;
}

static int mtk_pinmux_gpio_request_enable(struct pinctrl_dev *pctldev,
					  struct pinctrl_gpio_range *range,
					  unsigned int pin)
{
	struct mtk_pinctrl *hw = pinctrl_dev_get_drvdata(pctldev);

	return mtk_hw_set_value(hw, pin, PINCTRL_PIN_REG_MODE, MTK_GPIO_MODE);
}

static int mtk_pinmux_gpio_set_direction(struct pinctrl_dev *pctldev,
					 struct pinctrl_gpio_range *range,
					 unsigned int pin, bool input)
{
	struct mtk_pinctrl *hw = pinctrl_dev_get_drvdata(pctldev);

	/* hardware would take 0 as input direction */
	return mtk_hw_set_value(hw, pin, PINCTRL_PIN_REG_DIR, !input);
}

static int mtk_pinconf_get(struct pinctrl_dev *pctldev,
			   unsigned int pin, unsigned long *config)
{
	struct mtk_pinctrl *hw = pinctrl_dev_get_drvdata(pctldev);
	u32 param = pinconf_to_config_param(*config);
	int val, val2, err, reg, ret = 1;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		err = mtk_hw_get_value(hw, pin, PINCTRL_PIN_REG_PU, &val);
		if (err)
			return err;

		err = mtk_hw_get_value(hw, pin, PINCTRL_PIN_REG_PD, &val2);
		if (err)
			return err;

		if (val || val2)
			return -EINVAL;

		break;
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_SLEW_RATE:
		reg = (param == PIN_CONFIG_BIAS_PULL_UP) ?
		      PINCTRL_PIN_REG_PU :
		      (param == PIN_CONFIG_BIAS_PULL_DOWN) ?
		      PINCTRL_PIN_REG_PD : PINCTRL_PIN_REG_SR;

		err = mtk_hw_get_value(hw, pin, reg, &val);
		if (err)
			return err;

		if (!val)
			return -EINVAL;

		break;
	case PIN_CONFIG_INPUT_ENABLE:
	case PIN_CONFIG_OUTPUT_ENABLE:
		err = mtk_hw_get_value(hw, pin, PINCTRL_PIN_REG_DIR, &val);
		if (err)
			return err;

		/* HW takes input mode as zero; output mode as non-zero */
		if ((val && param == PIN_CONFIG_INPUT_ENABLE) ||
		    (!val && param == PIN_CONFIG_OUTPUT_ENABLE))
			return -EINVAL;

		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		err = mtk_hw_get_value(hw, pin, PINCTRL_PIN_REG_DIR, &val);
		if (err)
			return err;

		err = mtk_hw_get_value(hw, pin, PINCTRL_PIN_REG_SMT, &val2);
		if (err)
			return err;

		if (val || !val2)
			return -EINVAL;

		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		err = mtk_hw_get_value(hw, pin, PINCTRL_PIN_REG_E4, &val);
		if (err)
			return err;

		err = mtk_hw_get_value(hw, pin, PINCTRL_PIN_REG_E8, &val2);
		if (err)
			return err;

		/* 4mA when (e8, e4) = (0, 0); 8mA when (e8, e4) = (0, 1)
		 * 12mA when (e8, e4) = (1, 0); 16mA when (e8, e4) = (1, 1)
		 */
		ret = ((val2 << 1) + val + 1) * 4;

		break;
	case MTK_PIN_CONFIG_TDSEL:
	case MTK_PIN_CONFIG_RDSEL:
		reg = (param == MTK_PIN_CONFIG_TDSEL) ?
		       PINCTRL_PIN_REG_TDSEL : PINCTRL_PIN_REG_RDSEL;

		err = mtk_hw_get_value(hw, pin, reg, &val);
		if (err)
			return err;

		ret = val;

		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, ret);

	return 0;
}

static int mtk_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			   unsigned long *configs, unsigned int num_configs)
{
	struct mtk_pinctrl *hw = pinctrl_dev_get_drvdata(pctldev);
	u32 reg, param, arg;
	int cfg, err = 0;

	for (cfg = 0; cfg < num_configs; cfg++) {
		param = pinconf_to_config_param(configs[cfg]);
		arg = pinconf_to_config_argument(configs[cfg]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			arg = (param == PIN_CONFIG_BIAS_DISABLE) ? 0 :
			       (param == PIN_CONFIG_BIAS_PULL_UP) ? 1 : 2;

			err = mtk_hw_set_value(hw, pin, PINCTRL_PIN_REG_PU,
					       arg & 1);
			if (err)
				goto err;

			err = mtk_hw_set_value(hw, pin, PINCTRL_PIN_REG_PD,
					       !!(arg & 2));
			if (err)
				goto err;
			break;
		case PIN_CONFIG_OUTPUT_ENABLE:
			err = mtk_hw_set_value(hw, pin, PINCTRL_PIN_REG_SMT,
					       MTK_DISABLE);
			if (err)
				goto err;
		case PIN_CONFIG_INPUT_ENABLE:
		case PIN_CONFIG_SLEW_RATE:
			reg = (param == PIN_CONFIG_SLEW_RATE) ?
			       PINCTRL_PIN_REG_SR : PINCTRL_PIN_REG_DIR;

			arg = (param == PIN_CONFIG_INPUT_ENABLE) ? 0 :
			      (param == PIN_CONFIG_OUTPUT_ENABLE) ? 1 : arg;
			err = mtk_hw_set_value(hw, pin, reg, arg);
			if (err)
				goto err;

			break;
		case PIN_CONFIG_OUTPUT:
			err = mtk_hw_set_value(hw, pin, PINCTRL_PIN_REG_DIR,
					       MTK_OUTPUT);
			if (err)
				goto err;

			err = mtk_hw_set_value(hw, pin, PINCTRL_PIN_REG_DO,
					       arg);
			if (err)
				goto err;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			/* arg = 1: Input mode & SMT enable ;
			 * arg = 0: Output mode & SMT disable
			 */
			arg = arg ? 2 : 1;
			err = mtk_hw_set_value(hw, pin, PINCTRL_PIN_REG_DIR,
					       arg & 1);
			if (err)
				goto err;

			err = mtk_hw_set_value(hw, pin, PINCTRL_PIN_REG_SMT,
					       !!(arg & 2));
			if (err)
				goto err;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			/* 4mA when (e8, e4) = (0, 0);
			 * 8mA when (e8, e4) = (0, 1);
			 * 12mA when (e8, e4) = (1, 0);
			 * 16mA when (e8, e4) = (1, 1)
			 */
			if (!(arg % 4) && (arg >= 4 && arg <= 16)) {
				arg = arg / 4 - 1;
				err = mtk_hw_set_value(hw, pin,
						       PINCTRL_PIN_REG_E4,
						       arg & 0x1);
				if (err)
					goto err;

				err = mtk_hw_set_value(hw, pin,
						       PINCTRL_PIN_REG_E8,
						       (arg & 0x2) >> 1);
				if (err)
					goto err;
			} else {
				err = -ENOTSUPP;
			}
			break;
		case MTK_PIN_CONFIG_TDSEL:
		case MTK_PIN_CONFIG_RDSEL:
			reg = (param == MTK_PIN_CONFIG_TDSEL) ?
			       PINCTRL_PIN_REG_TDSEL : PINCTRL_PIN_REG_RDSEL;

			err = mtk_hw_set_value(hw, pin, reg, arg);
			if (err)
				goto err;
			break;
		default:
			err = -ENOTSUPP;
		}
	}
err:
	return err;
}

static int mtk_pinconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned int group, unsigned long *config)
{
	const unsigned int *pins;
	unsigned int i, npins, old = 0;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		if (mtk_pinconf_get(pctldev, pins[i], config))
			return -ENOTSUPP;

		/* configs do not match between two pins */
		if (i && old != *config)
			return -ENOTSUPP;

		old = *config;
	}

	return 0;
}

static int mtk_pinconf_group_set(struct pinctrl_dev *pctldev,
				 unsigned int group, unsigned long *configs,
				 unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int i, npins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = mtk_pinconf_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinctrl_ops mtk_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static const struct pinmux_ops mtk_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = mtk_pinmux_set_mux,
	.gpio_request_enable = mtk_pinmux_gpio_request_enable,
	.gpio_set_direction = mtk_pinmux_gpio_set_direction,
	.strict = true,
};

static const struct pinconf_ops mtk_confops = {
	.is_generic = true,
	.pin_config_get = mtk_pinconf_get,
	.pin_config_set = mtk_pinconf_set,
	.pin_config_group_get = mtk_pinconf_group_get,
	.pin_config_group_set = mtk_pinconf_group_set,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static struct pinctrl_desc mtk_desc = {
	.name = PINCTRL_PINCTRL_DEV,
	.pctlops = &mtk_pctlops,
	.pmxops = &mtk_pmxops,
	.confops = &mtk_confops,
	.owner = THIS_MODULE,
};

static int mtk_gpio_get(struct gpio_chip *chip, unsigned int gpio)
{
	struct mtk_pinctrl *hw = gpiochip_get_data(chip);
	int value, err;

	err = mtk_hw_get_value(hw, gpio, PINCTRL_PIN_REG_DI, &value);
	if (err)
		return err;

	return !!value;
}

static void mtk_gpio_set(struct gpio_chip *chip, unsigned int gpio, int value)
{
	struct mtk_pinctrl *hw = gpiochip_get_data(chip);

	mtk_hw_set_value(hw, gpio, PINCTRL_PIN_REG_DO, !!value);
}

static int mtk_gpio_direction_input(struct gpio_chip *chip, unsigned int gpio)
{
	return pinctrl_gpio_direction_input(chip->base + gpio);
}

static int mtk_gpio_direction_output(struct gpio_chip *chip, unsigned int gpio,
				     int value)
{
	mtk_gpio_set(chip, gpio, value);

	return pinctrl_gpio_direction_output(chip->base + gpio);
}

static int mtk_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct mtk_pinctrl *hw = gpiochip_get_data(chip);
	unsigned long eint_n;

	if (!hw->eint)
		return -ENOTSUPP;

	eint_n = offset;

	return mtk_eint_find_irq(hw->eint, eint_n);
}

static int mtk_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
			       unsigned long config)
{
	struct mtk_pinctrl *hw = gpiochip_get_data(chip);
	unsigned long eint_n;
	u32 debounce;

	if (!hw->eint ||
	    pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);
	eint_n = offset;

	return mtk_eint_set_debounce(hw->eint, eint_n, debounce);
}

static int mtk_build_gpiochip(struct mtk_pinctrl *hw, struct device_node *np)
{
	struct gpio_chip *chip = &hw->chip;
	int ret;

	chip->label		= PINCTRL_PINCTRL_DEV;
	chip->parent		= hw->dev;
	chip->request		= gpiochip_generic_request;
	chip->free		= gpiochip_generic_free;
	chip->direction_input	= mtk_gpio_direction_input;
	chip->direction_output	= mtk_gpio_direction_output;
	chip->get		= mtk_gpio_get;
	chip->set		= mtk_gpio_set;
	chip->to_irq		= mtk_gpio_to_irq,
	chip->set_config	= mtk_gpio_set_config,
	chip->base		= -1;
	chip->ngpio		= hw->soc->npins;
	chip->of_node		= np;
	chip->of_gpio_n_cells	= 2;

	ret = gpiochip_add_data(chip, hw);
	if (ret < 0)
		return ret;

	/* Just for backward compatible for these old pinctrl nodes without
	 * "gpio-ranges" property. Otherwise, called directly from a
	 * DeviceTree-supported pinctrl driver is DEPRECATED.
	 * Please see Section 2.1 of
	 * Documentation/devicetree/bindings/gpio/gpio.txt on how to
	 * bind pinctrl and gpio drivers via the "gpio-ranges" property.
	 */
	if (!of_find_property(np, "gpio-ranges", NULL)) {
		ret = gpiochip_add_pin_range(chip, dev_name(hw->dev), 0, 0,
					     chip->ngpio);
		if (ret < 0) {
			gpiochip_remove(chip);
			return ret;
		}
	}

	return 0;
}

static int mtk_build_groups(struct mtk_pinctrl *hw)
{
	int err, i;

	for (i = 0; i < hw->soc->ngrps; i++) {
		const struct group_desc *group = hw->soc->grps + i;

		err = pinctrl_generic_add_group(hw->pctrl, group->name,
						group->pins, group->num_pins,
						group->data);
		if (err) {
			dev_err(hw->dev, "Failed to register group %s\n",
				group->name);
			return err;
		}
	}

	return 0;
}

static int mtk_build_functions(struct mtk_pinctrl *hw)
{
	int i, err;

	for (i = 0; i < hw->soc->nfuncs ; i++) {
		const struct function_desc *func = hw->soc->funcs + i;

		err = pinmux_generic_add_function(hw->pctrl, func->name,
						  func->group_names,
						  func->num_group_names,
						  func->data);
		if (err) {
			dev_err(hw->dev, "Failed to register function %s\n",
				func->name);
			return err;
		}
	}

	return 0;
}

static int mtk_xt_get_gpio_n(void *data, unsigned long eint_n,
			     unsigned int *gpio_n,
			     struct gpio_chip **gpio_chip)
{
	struct mtk_pinctrl *hw = (struct mtk_pinctrl *)data;

	*gpio_chip = &hw->chip;
	*gpio_n = eint_n;

	return 0;
}

static int mtk_xt_get_gpio_state(void *data, unsigned long eint_n)
{
	struct mtk_pinctrl *hw = (struct mtk_pinctrl *)data;
	struct gpio_chip *gpio_chip;
	unsigned int gpio_n;
	int err;

	err = mtk_xt_get_gpio_n(hw, eint_n, &gpio_n, &gpio_chip);
	if (err)
		return err;

	return mtk_gpio_get(gpio_chip, gpio_n);
}

static int mtk_xt_set_gpio_as_eint(void *data, unsigned long eint_n)
{
	struct mtk_pinctrl *hw = (struct mtk_pinctrl *)data;
	struct gpio_chip *gpio_chip;
	unsigned int gpio_n;
	int err;

	err = mtk_xt_get_gpio_n(hw, eint_n, &gpio_n, &gpio_chip);
	if (err)
		return err;

	err = mtk_hw_set_value(hw, gpio_n, PINCTRL_PIN_REG_MODE,
			       MTK_GPIO_MODE);
	if (err)
		return err;

	err = mtk_hw_set_value(hw, gpio_n, PINCTRL_PIN_REG_DIR, MTK_INPUT);
	if (err)
		return err;

	err = mtk_hw_set_value(hw, gpio_n, PINCTRL_PIN_REG_SMT, MTK_ENABLE);
	if (err)
		return err;

	return 0;
}

static const struct mtk_eint_xt mtk_eint_xt = {
	.get_gpio_n = mtk_xt_get_gpio_n,
	.get_gpio_state = mtk_xt_get_gpio_state,
	.set_gpio_as_eint = mtk_xt_set_gpio_as_eint,
};

static int
mtk_build_eint(struct mtk_pinctrl *hw, struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;

	if (!IS_ENABLED(CONFIG_EINT_MTK))
		return 0;

	if (!of_property_read_bool(np, "interrupt-controller"))
		return -ENODEV;

	hw->eint = devm_kzalloc(hw->dev, sizeof(*hw->eint), GFP_KERNEL);
	if (!hw->eint)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "eint");
	if (!res) {
		dev_err(&pdev->dev, "Unable to get eint resource\n");
		return -ENODEV;
	}

	hw->eint->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hw->eint->base))
		return PTR_ERR(hw->eint->base);

	hw->eint->irq = irq_of_parse_and_map(np, 0);
	if (!hw->eint->irq)
		return -EINVAL;

	hw->eint->dev = &pdev->dev;
	hw->eint->hw = hw->soc->eint_hw;
	hw->eint->pctl = hw;
	hw->eint->gpio_xlate = &mtk_eint_xt;

	return mtk_eint_do_init(hw->eint);
}

static const struct of_device_id mtk_pinctrl_of_match[] = {
	{ .compatible = "mediatek,mt7622-pinctrl", .data = &mt7622_data},
	{ }
};

static int mtk_pinctrl_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct mtk_pinctrl *hw;
	const struct of_device_id *of_id =
		of_match_device(mtk_pinctrl_of_match, &pdev->dev);
	int err;

	hw = devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	hw->soc = of_id->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "missing IO resource\n");
		return -ENXIO;
	}

	hw->dev = &pdev->dev;
	hw->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hw->base))
		return PTR_ERR(hw->base);

	/* Setup pins descriptions per SoC types */
	mtk_desc.pins = hw->soc->pins;
	mtk_desc.npins = hw->soc->npins;
	mtk_desc.num_custom_params = ARRAY_SIZE(mtk_custom_bindings);
	mtk_desc.custom_params = mtk_custom_bindings;
#ifdef CONFIG_DEBUG_FS
	mtk_desc.custom_conf_items = mtk_conf_items;
#endif

	err = devm_pinctrl_register_and_init(&pdev->dev, &mtk_desc, hw,
					     &hw->pctrl);
	if (err)
		return err;

	/* Setup groups descriptions per SoC types */
	err = mtk_build_groups(hw);
	if (err) {
		dev_err(&pdev->dev, "Failed to build groups\n");
		return err;
	}

	/* Setup functions descriptions per SoC types */
	err = mtk_build_functions(hw);
	if (err) {
		dev_err(&pdev->dev, "Failed to build functions\n");
		return err;
	}

	/* For able to make pinctrl_claim_hogs, we must not enable pinctrl
	 * until all groups and functions are being added one.
	 */
	err = pinctrl_enable(hw->pctrl);
	if (err)
		return err;

	err = mtk_build_eint(hw, pdev);
	if (err)
		dev_warn(&pdev->dev,
			 "Failed to add EINT, but pinctrl still can work\n");

	/* Build gpiochip should be after pinctrl_enable is done */
	err = mtk_build_gpiochip(hw, pdev->dev.of_node);
	if (err) {
		dev_err(&pdev->dev, "Failed to add gpio_chip\n");
		return err;
	}

	platform_set_drvdata(pdev, hw);

	return 0;
}

static struct platform_driver mtk_pinctrl_driver = {
	.driver = {
		.name = "mtk-pinctrl",
		.of_match_table = mtk_pinctrl_of_match,
	},
	.probe = mtk_pinctrl_probe,
};

static int __init mtk_pinctrl_init(void)
{
	return platform_driver_register(&mtk_pinctrl_driver);
}
arch_initcall(mtk_pinctrl_init);
