// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zynq pin controller
 *
 *  Copyright (C) 2014 Xilinx
 *
 *  Sören Brinkmann <soren.brinkmann@xilinx.com>
 */
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/regmap.h>
#include "pinctrl-utils.h"
#include "core.h"

#define ZYNQ_NUM_MIOS	54

#define ZYNQ_PCTRL_MIO_MST_TRI0	0x10c
#define ZYNQ_PCTRL_MIO_MST_TRI1	0x110

#define ZYNQ_PINMUX_MUX_SHIFT	1
#define ZYNQ_PINMUX_MUX_MASK	(0x7f << ZYNQ_PINMUX_MUX_SHIFT)

/**
 * struct zynq_pinctrl - driver data
 * @pctrl:		Pinctrl device
 * @syscon:		Syscon regmap
 * @pctrl_offset:	Offset for pinctrl into the @syscon space
 * @groups:		Pingroups
 * @ngroups:		Number of @groups
 * @funcs:		Pinmux functions
 * @nfuncs:		Number of @funcs
 */
struct zynq_pinctrl {
	struct pinctrl_dev *pctrl;
	struct regmap *syscon;
	u32 pctrl_offset;
	const struct zynq_pctrl_group *groups;
	unsigned int ngroups;
	const struct zynq_pinmux_function *funcs;
	unsigned int nfuncs;
};

struct zynq_pctrl_group {
	const char *name;
	const unsigned int *pins;
	const unsigned int npins;
};

/**
 * struct zynq_pinmux_function - a pinmux function
 * @name:	Name of the pinmux function.
 * @groups:	List of pingroups for this function.
 * @ngroups:	Number of entries in @groups.
 * @mux_val:	Selector for this function
 * @mux:	Offset of function specific mux
 * @mux_mask:	Mask for function specific selector
 * @mux_shift:	Shift for function specific selector
 */
struct zynq_pinmux_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
	unsigned int mux_val;
	u32 mux;
	u32 mux_mask;
	u8 mux_shift;
};

enum zynq_pinmux_functions {
	ZYNQ_PMUX_can0,
	ZYNQ_PMUX_can1,
	ZYNQ_PMUX_ethernet0,
	ZYNQ_PMUX_ethernet1,
	ZYNQ_PMUX_gpio0,
	ZYNQ_PMUX_i2c0,
	ZYNQ_PMUX_i2c1,
	ZYNQ_PMUX_mdio0,
	ZYNQ_PMUX_mdio1,
	ZYNQ_PMUX_qspi0,
	ZYNQ_PMUX_qspi1,
	ZYNQ_PMUX_qspi_fbclk,
	ZYNQ_PMUX_qspi_cs1,
	ZYNQ_PMUX_spi0,
	ZYNQ_PMUX_spi1,
	ZYNQ_PMUX_spi0_ss,
	ZYNQ_PMUX_spi1_ss,
	ZYNQ_PMUX_sdio0,
	ZYNQ_PMUX_sdio0_pc,
	ZYNQ_PMUX_sdio0_cd,
	ZYNQ_PMUX_sdio0_wp,
	ZYNQ_PMUX_sdio1,
	ZYNQ_PMUX_sdio1_pc,
	ZYNQ_PMUX_sdio1_cd,
	ZYNQ_PMUX_sdio1_wp,
	ZYNQ_PMUX_smc0_nor,
	ZYNQ_PMUX_smc0_nor_cs1,
	ZYNQ_PMUX_smc0_nor_addr25,
	ZYNQ_PMUX_smc0_nand,
	ZYNQ_PMUX_ttc0,
	ZYNQ_PMUX_ttc1,
	ZYNQ_PMUX_uart0,
	ZYNQ_PMUX_uart1,
	ZYNQ_PMUX_usb0,
	ZYNQ_PMUX_usb1,
	ZYNQ_PMUX_swdt0,
	ZYNQ_PMUX_MAX_FUNC
};

static const struct pinctrl_pin_desc zynq_pins[] = {
	PINCTRL_PIN(0,  "MIO0"),
	PINCTRL_PIN(1,  "MIO1"),
	PINCTRL_PIN(2,  "MIO2"),
	PINCTRL_PIN(3,  "MIO3"),
	PINCTRL_PIN(4,  "MIO4"),
	PINCTRL_PIN(5,  "MIO5"),
	PINCTRL_PIN(6,  "MIO6"),
	PINCTRL_PIN(7,  "MIO7"),
	PINCTRL_PIN(8,  "MIO8"),
	PINCTRL_PIN(9,  "MIO9"),
	PINCTRL_PIN(10, "MIO10"),
	PINCTRL_PIN(11, "MIO11"),
	PINCTRL_PIN(12, "MIO12"),
	PINCTRL_PIN(13, "MIO13"),
	PINCTRL_PIN(14, "MIO14"),
	PINCTRL_PIN(15, "MIO15"),
	PINCTRL_PIN(16, "MIO16"),
	PINCTRL_PIN(17, "MIO17"),
	PINCTRL_PIN(18, "MIO18"),
	PINCTRL_PIN(19, "MIO19"),
	PINCTRL_PIN(20, "MIO20"),
	PINCTRL_PIN(21, "MIO21"),
	PINCTRL_PIN(22, "MIO22"),
	PINCTRL_PIN(23, "MIO23"),
	PINCTRL_PIN(24, "MIO24"),
	PINCTRL_PIN(25, "MIO25"),
	PINCTRL_PIN(26, "MIO26"),
	PINCTRL_PIN(27, "MIO27"),
	PINCTRL_PIN(28, "MIO28"),
	PINCTRL_PIN(29, "MIO29"),
	PINCTRL_PIN(30, "MIO30"),
	PINCTRL_PIN(31, "MIO31"),
	PINCTRL_PIN(32, "MIO32"),
	PINCTRL_PIN(33, "MIO33"),
	PINCTRL_PIN(34, "MIO34"),
	PINCTRL_PIN(35, "MIO35"),
	PINCTRL_PIN(36, "MIO36"),
	PINCTRL_PIN(37, "MIO37"),
	PINCTRL_PIN(38, "MIO38"),
	PINCTRL_PIN(39, "MIO39"),
	PINCTRL_PIN(40, "MIO40"),
	PINCTRL_PIN(41, "MIO41"),
	PINCTRL_PIN(42, "MIO42"),
	PINCTRL_PIN(43, "MIO43"),
	PINCTRL_PIN(44, "MIO44"),
	PINCTRL_PIN(45, "MIO45"),
	PINCTRL_PIN(46, "MIO46"),
	PINCTRL_PIN(47, "MIO47"),
	PINCTRL_PIN(48, "MIO48"),
	PINCTRL_PIN(49, "MIO49"),
	PINCTRL_PIN(50, "MIO50"),
	PINCTRL_PIN(51, "MIO51"),
	PINCTRL_PIN(52, "MIO52"),
	PINCTRL_PIN(53, "MIO53"),
	PINCTRL_PIN(54, "EMIO_SD0_WP"),
	PINCTRL_PIN(55, "EMIO_SD0_CD"),
	PINCTRL_PIN(56, "EMIO_SD1_WP"),
	PINCTRL_PIN(57, "EMIO_SD1_CD"),
};

/* pin groups */
static const unsigned int ethernet0_0_pins[] = {16, 17, 18, 19, 20, 21, 22, 23,
						24, 25, 26, 27};
static const unsigned int ethernet1_0_pins[] = {28, 29, 30, 31, 32, 33, 34, 35,
						36, 37, 38, 39};
static const unsigned int mdio0_0_pins[] = {52, 53};
static const unsigned int mdio1_0_pins[] = {52, 53};
static const unsigned int qspi0_0_pins[] = {1, 2, 3, 4, 5, 6};

static const unsigned int qspi1_0_pins[] = {9, 10, 11, 12, 13};
static const unsigned int qspi_cs1_pins[] = {0};
static const unsigned int qspi_fbclk_pins[] = {8};
static const unsigned int spi0_0_pins[] = {16, 17, 21};
static const unsigned int spi0_0_ss0_pins[] = {18};
static const unsigned int spi0_0_ss1_pins[] = {19};
static const unsigned int spi0_0_ss2_pins[] = {20,};
static const unsigned int spi0_1_pins[] = {28, 29, 33};
static const unsigned int spi0_1_ss0_pins[] = {30};
static const unsigned int spi0_1_ss1_pins[] = {31};
static const unsigned int spi0_1_ss2_pins[] = {32};
static const unsigned int spi0_2_pins[] = {40, 41, 45};
static const unsigned int spi0_2_ss0_pins[] = {42};
static const unsigned int spi0_2_ss1_pins[] = {43};
static const unsigned int spi0_2_ss2_pins[] = {44};
static const unsigned int spi1_0_pins[] = {10, 11, 12};
static const unsigned int spi1_0_ss0_pins[] = {13};
static const unsigned int spi1_0_ss1_pins[] = {14};
static const unsigned int spi1_0_ss2_pins[] = {15};
static const unsigned int spi1_1_pins[] = {22, 23, 24};
static const unsigned int spi1_1_ss0_pins[] = {25};
static const unsigned int spi1_1_ss1_pins[] = {26};
static const unsigned int spi1_1_ss2_pins[] = {27};
static const unsigned int spi1_2_pins[] = {34, 35, 36};
static const unsigned int spi1_2_ss0_pins[] = {37};
static const unsigned int spi1_2_ss1_pins[] = {38};
static const unsigned int spi1_2_ss2_pins[] = {39};
static const unsigned int spi1_3_pins[] = {46, 47, 48, 49};
static const unsigned int spi1_3_ss0_pins[] = {49};
static const unsigned int spi1_3_ss1_pins[] = {50};
static const unsigned int spi1_3_ss2_pins[] = {51};

static const unsigned int sdio0_0_pins[] = {16, 17, 18, 19, 20, 21};
static const unsigned int sdio0_1_pins[] = {28, 29, 30, 31, 32, 33};
static const unsigned int sdio0_2_pins[] = {40, 41, 42, 43, 44, 45};
static const unsigned int sdio1_0_pins[] = {10, 11, 12, 13, 14, 15};
static const unsigned int sdio1_1_pins[] = {22, 23, 24, 25, 26, 27};
static const unsigned int sdio1_2_pins[] = {34, 35, 36, 37, 38, 39};
static const unsigned int sdio1_3_pins[] = {46, 47, 48, 49, 50, 51};
static const unsigned int sdio0_emio_wp_pins[] = {54};
static const unsigned int sdio0_emio_cd_pins[] = {55};
static const unsigned int sdio1_emio_wp_pins[] = {56};
static const unsigned int sdio1_emio_cd_pins[] = {57};
static const unsigned int smc0_nor_pins[] = {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
					     15, 16, 17, 18, 19, 20, 21, 22, 23,
					     24, 25, 26, 27, 28, 29, 30, 31, 32,
					     33, 34, 35, 36, 37, 38, 39};
static const unsigned int smc0_nor_cs1_pins[] = {1};
static const unsigned int smc0_nor_addr25_pins[] = {1};
static const unsigned int smc0_nand_pins[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
					      12, 13, 14, 16, 17, 18, 19, 20,
					      21, 22, 23};
static const unsigned int smc0_nand8_pins[] = {0, 2, 3,  4,  5,  6,  7,
					       8, 9, 10, 11, 12, 13, 14};
/* Note: CAN MIO clock inputs are modeled in the clock framework */
static const unsigned int can0_0_pins[] = {10, 11};
static const unsigned int can0_1_pins[] = {14, 15};
static const unsigned int can0_2_pins[] = {18, 19};
static const unsigned int can0_3_pins[] = {22, 23};
static const unsigned int can0_4_pins[] = {26, 27};
static const unsigned int can0_5_pins[] = {30, 31};
static const unsigned int can0_6_pins[] = {34, 35};
static const unsigned int can0_7_pins[] = {38, 39};
static const unsigned int can0_8_pins[] = {42, 43};
static const unsigned int can0_9_pins[] = {46, 47};
static const unsigned int can0_10_pins[] = {50, 51};
static const unsigned int can1_0_pins[] = {8, 9};
static const unsigned int can1_1_pins[] = {12, 13};
static const unsigned int can1_2_pins[] = {16, 17};
static const unsigned int can1_3_pins[] = {20, 21};
static const unsigned int can1_4_pins[] = {24, 25};
static const unsigned int can1_5_pins[] = {28, 29};
static const unsigned int can1_6_pins[] = {32, 33};
static const unsigned int can1_7_pins[] = {36, 37};
static const unsigned int can1_8_pins[] = {40, 41};
static const unsigned int can1_9_pins[] = {44, 45};
static const unsigned int can1_10_pins[] = {48, 49};
static const unsigned int can1_11_pins[] = {52, 53};
static const unsigned int uart0_0_pins[] = {10, 11};
static const unsigned int uart0_1_pins[] = {14, 15};
static const unsigned int uart0_2_pins[] = {18, 19};
static const unsigned int uart0_3_pins[] = {22, 23};
static const unsigned int uart0_4_pins[] = {26, 27};
static const unsigned int uart0_5_pins[] = {30, 31};
static const unsigned int uart0_6_pins[] = {34, 35};
static const unsigned int uart0_7_pins[] = {38, 39};
static const unsigned int uart0_8_pins[] = {42, 43};
static const unsigned int uart0_9_pins[] = {46, 47};
static const unsigned int uart0_10_pins[] = {50, 51};
static const unsigned int uart1_0_pins[] = {8, 9};
static const unsigned int uart1_1_pins[] = {12, 13};
static const unsigned int uart1_2_pins[] = {16, 17};
static const unsigned int uart1_3_pins[] = {20, 21};
static const unsigned int uart1_4_pins[] = {24, 25};
static const unsigned int uart1_5_pins[] = {28, 29};
static const unsigned int uart1_6_pins[] = {32, 33};
static const unsigned int uart1_7_pins[] = {36, 37};
static const unsigned int uart1_8_pins[] = {40, 41};
static const unsigned int uart1_9_pins[] = {44, 45};
static const unsigned int uart1_10_pins[] = {48, 49};
static const unsigned int uart1_11_pins[] = {52, 53};
static const unsigned int i2c0_0_pins[] = {10, 11};
static const unsigned int i2c0_1_pins[] = {14, 15};
static const unsigned int i2c0_2_pins[] = {18, 19};
static const unsigned int i2c0_3_pins[] = {22, 23};
static const unsigned int i2c0_4_pins[] = {26, 27};
static const unsigned int i2c0_5_pins[] = {30, 31};
static const unsigned int i2c0_6_pins[] = {34, 35};
static const unsigned int i2c0_7_pins[] = {38, 39};
static const unsigned int i2c0_8_pins[] = {42, 43};
static const unsigned int i2c0_9_pins[] = {46, 47};
static const unsigned int i2c0_10_pins[] = {50, 51};
static const unsigned int i2c1_0_pins[] = {12, 13};
static const unsigned int i2c1_1_pins[] = {16, 17};
static const unsigned int i2c1_2_pins[] = {20, 21};
static const unsigned int i2c1_3_pins[] = {24, 25};
static const unsigned int i2c1_4_pins[] = {28, 29};
static const unsigned int i2c1_5_pins[] = {32, 33};
static const unsigned int i2c1_6_pins[] = {36, 37};
static const unsigned int i2c1_7_pins[] = {40, 41};
static const unsigned int i2c1_8_pins[] = {44, 45};
static const unsigned int i2c1_9_pins[] = {48, 49};
static const unsigned int i2c1_10_pins[] = {52, 53};
static const unsigned int ttc0_0_pins[] = {18, 19};
static const unsigned int ttc0_1_pins[] = {30, 31};
static const unsigned int ttc0_2_pins[] = {42, 43};
static const unsigned int ttc1_0_pins[] = {16, 17};
static const unsigned int ttc1_1_pins[] = {28, 29};
static const unsigned int ttc1_2_pins[] = {40, 41};
static const unsigned int swdt0_0_pins[] = {14, 15};
static const unsigned int swdt0_1_pins[] = {26, 27};
static const unsigned int swdt0_2_pins[] = {38, 39};
static const unsigned int swdt0_3_pins[] = {50, 51};
static const unsigned int swdt0_4_pins[] = {52, 53};
static const unsigned int gpio0_0_pins[] = {0};
static const unsigned int gpio0_1_pins[] = {1};
static const unsigned int gpio0_2_pins[] = {2};
static const unsigned int gpio0_3_pins[] = {3};
static const unsigned int gpio0_4_pins[] = {4};
static const unsigned int gpio0_5_pins[] = {5};
static const unsigned int gpio0_6_pins[] = {6};
static const unsigned int gpio0_7_pins[] = {7};
static const unsigned int gpio0_8_pins[] = {8};
static const unsigned int gpio0_9_pins[] = {9};
static const unsigned int gpio0_10_pins[] = {10};
static const unsigned int gpio0_11_pins[] = {11};
static const unsigned int gpio0_12_pins[] = {12};
static const unsigned int gpio0_13_pins[] = {13};
static const unsigned int gpio0_14_pins[] = {14};
static const unsigned int gpio0_15_pins[] = {15};
static const unsigned int gpio0_16_pins[] = {16};
static const unsigned int gpio0_17_pins[] = {17};
static const unsigned int gpio0_18_pins[] = {18};
static const unsigned int gpio0_19_pins[] = {19};
static const unsigned int gpio0_20_pins[] = {20};
static const unsigned int gpio0_21_pins[] = {21};
static const unsigned int gpio0_22_pins[] = {22};
static const unsigned int gpio0_23_pins[] = {23};
static const unsigned int gpio0_24_pins[] = {24};
static const unsigned int gpio0_25_pins[] = {25};
static const unsigned int gpio0_26_pins[] = {26};
static const unsigned int gpio0_27_pins[] = {27};
static const unsigned int gpio0_28_pins[] = {28};
static const unsigned int gpio0_29_pins[] = {29};
static const unsigned int gpio0_30_pins[] = {30};
static const unsigned int gpio0_31_pins[] = {31};
static const unsigned int gpio0_32_pins[] = {32};
static const unsigned int gpio0_33_pins[] = {33};
static const unsigned int gpio0_34_pins[] = {34};
static const unsigned int gpio0_35_pins[] = {35};
static const unsigned int gpio0_36_pins[] = {36};
static const unsigned int gpio0_37_pins[] = {37};
static const unsigned int gpio0_38_pins[] = {38};
static const unsigned int gpio0_39_pins[] = {39};
static const unsigned int gpio0_40_pins[] = {40};
static const unsigned int gpio0_41_pins[] = {41};
static const unsigned int gpio0_42_pins[] = {42};
static const unsigned int gpio0_43_pins[] = {43};
static const unsigned int gpio0_44_pins[] = {44};
static const unsigned int gpio0_45_pins[] = {45};
static const unsigned int gpio0_46_pins[] = {46};
static const unsigned int gpio0_47_pins[] = {47};
static const unsigned int gpio0_48_pins[] = {48};
static const unsigned int gpio0_49_pins[] = {49};
static const unsigned int gpio0_50_pins[] = {50};
static const unsigned int gpio0_51_pins[] = {51};
static const unsigned int gpio0_52_pins[] = {52};
static const unsigned int gpio0_53_pins[] = {53};
static const unsigned int usb0_0_pins[] = {28, 29, 30, 31, 32, 33, 34, 35, 36,
					   37, 38, 39};
static const unsigned int usb1_0_pins[] = {40, 41, 42, 43, 44, 45, 46, 47, 48,
					   49, 50, 51};

#define DEFINE_ZYNQ_PINCTRL_GRP(nm) \
	{ \
		.name = #nm "_grp", \
		.pins = nm ## _pins, \
		.npins = ARRAY_SIZE(nm ## _pins), \
	}

static const struct zynq_pctrl_group zynq_pctrl_groups[] = {
	DEFINE_ZYNQ_PINCTRL_GRP(ethernet0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(ethernet1_0),
	DEFINE_ZYNQ_PINCTRL_GRP(mdio0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(mdio1_0),
	DEFINE_ZYNQ_PINCTRL_GRP(qspi0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(qspi1_0),
	DEFINE_ZYNQ_PINCTRL_GRP(qspi_fbclk),
	DEFINE_ZYNQ_PINCTRL_GRP(qspi_cs1),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_0_ss0),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_0_ss1),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_0_ss2),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_1),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_1_ss0),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_1_ss1),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_1_ss2),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_2),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_2_ss0),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_2_ss1),
	DEFINE_ZYNQ_PINCTRL_GRP(spi0_2_ss2),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_0),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_0_ss0),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_0_ss1),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_0_ss2),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_1),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_1_ss0),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_1_ss1),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_1_ss2),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_2),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_2_ss0),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_2_ss1),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_2_ss2),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_3),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_3_ss0),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_3_ss1),
	DEFINE_ZYNQ_PINCTRL_GRP(spi1_3_ss2),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio0_1),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio0_2),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio1_0),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio1_1),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio1_2),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio1_3),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio0_emio_wp),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio0_emio_cd),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio1_emio_wp),
	DEFINE_ZYNQ_PINCTRL_GRP(sdio1_emio_cd),
	DEFINE_ZYNQ_PINCTRL_GRP(smc0_nor),
	DEFINE_ZYNQ_PINCTRL_GRP(smc0_nor_cs1),
	DEFINE_ZYNQ_PINCTRL_GRP(smc0_nor_addr25),
	DEFINE_ZYNQ_PINCTRL_GRP(smc0_nand),
	DEFINE_ZYNQ_PINCTRL_GRP(smc0_nand8),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_1),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_2),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_3),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_4),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_5),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_6),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_7),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_8),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_9),
	DEFINE_ZYNQ_PINCTRL_GRP(can0_10),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_0),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_1),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_2),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_3),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_4),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_5),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_6),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_7),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_8),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_9),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_10),
	DEFINE_ZYNQ_PINCTRL_GRP(can1_11),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_1),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_2),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_3),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_4),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_5),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_6),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_7),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_8),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_9),
	DEFINE_ZYNQ_PINCTRL_GRP(uart0_10),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_0),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_1),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_2),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_3),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_4),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_5),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_6),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_7),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_8),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_9),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_10),
	DEFINE_ZYNQ_PINCTRL_GRP(uart1_11),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_1),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_2),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_3),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_4),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_5),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_6),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_7),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_8),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_9),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c0_10),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_0),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_1),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_2),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_3),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_4),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_5),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_6),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_7),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_8),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_9),
	DEFINE_ZYNQ_PINCTRL_GRP(i2c1_10),
	DEFINE_ZYNQ_PINCTRL_GRP(ttc0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(ttc0_1),
	DEFINE_ZYNQ_PINCTRL_GRP(ttc0_2),
	DEFINE_ZYNQ_PINCTRL_GRP(ttc1_0),
	DEFINE_ZYNQ_PINCTRL_GRP(ttc1_1),
	DEFINE_ZYNQ_PINCTRL_GRP(ttc1_2),
	DEFINE_ZYNQ_PINCTRL_GRP(swdt0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(swdt0_1),
	DEFINE_ZYNQ_PINCTRL_GRP(swdt0_2),
	DEFINE_ZYNQ_PINCTRL_GRP(swdt0_3),
	DEFINE_ZYNQ_PINCTRL_GRP(swdt0_4),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_1),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_2),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_3),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_4),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_5),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_6),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_7),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_8),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_9),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_10),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_11),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_12),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_13),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_14),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_15),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_16),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_17),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_18),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_19),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_20),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_21),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_22),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_23),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_24),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_25),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_26),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_27),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_28),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_29),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_30),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_31),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_32),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_33),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_34),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_35),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_36),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_37),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_38),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_39),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_40),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_41),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_42),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_43),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_44),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_45),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_46),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_47),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_48),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_49),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_50),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_51),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_52),
	DEFINE_ZYNQ_PINCTRL_GRP(gpio0_53),
	DEFINE_ZYNQ_PINCTRL_GRP(usb0_0),
	DEFINE_ZYNQ_PINCTRL_GRP(usb1_0),
};

/* function groups */
static const char * const ethernet0_groups[] = {"ethernet0_0_grp"};
static const char * const ethernet1_groups[] = {"ethernet1_0_grp"};
static const char * const usb0_groups[] = {"usb0_0_grp"};
static const char * const usb1_groups[] = {"usb1_0_grp"};
static const char * const mdio0_groups[] = {"mdio0_0_grp"};
static const char * const mdio1_groups[] = {"mdio1_0_grp"};
static const char * const qspi0_groups[] = {"qspi0_0_grp"};
static const char * const qspi1_groups[] = {"qspi1_0_grp"};
static const char * const qspi_fbclk_groups[] = {"qspi_fbclk_grp"};
static const char * const qspi_cs1_groups[] = {"qspi_cs1_grp"};
static const char * const spi0_groups[] = {"spi0_0_grp", "spi0_1_grp",
					   "spi0_2_grp"};
static const char * const spi1_groups[] = {"spi1_0_grp", "spi1_1_grp",
					   "spi1_2_grp", "spi1_3_grp"};
static const char * const spi0_ss_groups[] = {"spi0_0_ss0_grp",
		"spi0_0_ss1_grp", "spi0_0_ss2_grp", "spi0_1_ss0_grp",
		"spi0_1_ss1_grp", "spi0_1_ss2_grp", "spi0_2_ss0_grp",
		"spi0_2_ss1_grp", "spi0_2_ss2_grp"};
static const char * const spi1_ss_groups[] = {"spi1_0_ss0_grp",
		"spi1_0_ss1_grp", "spi1_0_ss2_grp", "spi1_1_ss0_grp",
		"spi1_1_ss1_grp", "spi1_1_ss2_grp", "spi1_2_ss0_grp",
		"spi1_2_ss1_grp", "spi1_2_ss2_grp", "spi1_3_ss0_grp",
		"spi1_3_ss1_grp", "spi1_3_ss2_grp"};
static const char * const sdio0_groups[] = {"sdio0_0_grp", "sdio0_1_grp",
					    "sdio0_2_grp"};
static const char * const sdio1_groups[] = {"sdio1_0_grp", "sdio1_1_grp",
					    "sdio1_2_grp", "sdio1_3_grp"};
static const char * const sdio0_pc_groups[] = {"gpio0_0_grp",
		"gpio0_2_grp", "gpio0_4_grp", "gpio0_6_grp",
		"gpio0_8_grp", "gpio0_10_grp", "gpio0_12_grp",
		"gpio0_14_grp", "gpio0_16_grp", "gpio0_18_grp",
		"gpio0_20_grp", "gpio0_22_grp", "gpio0_24_grp",
		"gpio0_26_grp", "gpio0_28_grp", "gpio0_30_grp",
		"gpio0_32_grp", "gpio0_34_grp", "gpio0_36_grp",
		"gpio0_38_grp", "gpio0_40_grp", "gpio0_42_grp",
		"gpio0_44_grp", "gpio0_46_grp", "gpio0_48_grp",
		"gpio0_50_grp", "gpio0_52_grp"};
static const char * const sdio1_pc_groups[] = {"gpio0_1_grp",
		"gpio0_3_grp", "gpio0_5_grp", "gpio0_7_grp",
		"gpio0_9_grp", "gpio0_11_grp", "gpio0_13_grp",
		"gpio0_15_grp", "gpio0_17_grp", "gpio0_19_grp",
		"gpio0_21_grp", "gpio0_23_grp", "gpio0_25_grp",
		"gpio0_27_grp", "gpio0_29_grp", "gpio0_31_grp",
		"gpio0_33_grp", "gpio0_35_grp", "gpio0_37_grp",
		"gpio0_39_grp", "gpio0_41_grp", "gpio0_43_grp",
		"gpio0_45_grp", "gpio0_47_grp", "gpio0_49_grp",
		"gpio0_51_grp", "gpio0_53_grp"};
static const char * const sdio0_cd_groups[] = {"gpio0_0_grp",
		"gpio0_2_grp", "gpio0_4_grp", "gpio0_6_grp",
		"gpio0_10_grp", "gpio0_12_grp",
		"gpio0_14_grp", "gpio0_16_grp", "gpio0_18_grp",
		"gpio0_20_grp", "gpio0_22_grp", "gpio0_24_grp",
		"gpio0_26_grp", "gpio0_28_grp", "gpio0_30_grp",
		"gpio0_32_grp", "gpio0_34_grp", "gpio0_36_grp",
		"gpio0_38_grp", "gpio0_40_grp", "gpio0_42_grp",
		"gpio0_44_grp", "gpio0_46_grp", "gpio0_48_grp",
		"gpio0_50_grp", "gpio0_52_grp", "gpio0_1_grp",
		"gpio0_3_grp", "gpio0_5_grp",
		"gpio0_9_grp", "gpio0_11_grp", "gpio0_13_grp",
		"gpio0_15_grp", "gpio0_17_grp", "gpio0_19_grp",
		"gpio0_21_grp", "gpio0_23_grp", "gpio0_25_grp",
		"gpio0_27_grp", "gpio0_29_grp", "gpio0_31_grp",
		"gpio0_33_grp", "gpio0_35_grp", "gpio0_37_grp",
		"gpio0_39_grp", "gpio0_41_grp", "gpio0_43_grp",
		"gpio0_45_grp", "gpio0_47_grp", "gpio0_49_grp",
		"gpio0_51_grp", "gpio0_53_grp", "sdio0_emio_cd_grp"};
static const char * const sdio0_wp_groups[] = {"gpio0_0_grp",
		"gpio0_2_grp", "gpio0_4_grp", "gpio0_6_grp",
		"gpio0_10_grp", "gpio0_12_grp",
		"gpio0_14_grp", "gpio0_16_grp", "gpio0_18_grp",
		"gpio0_20_grp", "gpio0_22_grp", "gpio0_24_grp",
		"gpio0_26_grp", "gpio0_28_grp", "gpio0_30_grp",
		"gpio0_32_grp", "gpio0_34_grp", "gpio0_36_grp",
		"gpio0_38_grp", "gpio0_40_grp", "gpio0_42_grp",
		"gpio0_44_grp", "gpio0_46_grp", "gpio0_48_grp",
		"gpio0_50_grp", "gpio0_52_grp", "gpio0_1_grp",
		"gpio0_3_grp", "gpio0_5_grp",
		"gpio0_9_grp", "gpio0_11_grp", "gpio0_13_grp",
		"gpio0_15_grp", "gpio0_17_grp", "gpio0_19_grp",
		"gpio0_21_grp", "gpio0_23_grp", "gpio0_25_grp",
		"gpio0_27_grp", "gpio0_29_grp", "gpio0_31_grp",
		"gpio0_33_grp", "gpio0_35_grp", "gpio0_37_grp",
		"gpio0_39_grp", "gpio0_41_grp", "gpio0_43_grp",
		"gpio0_45_grp", "gpio0_47_grp", "gpio0_49_grp",
		"gpio0_51_grp", "gpio0_53_grp", "sdio0_emio_wp_grp"};
static const char * const sdio1_cd_groups[] = {"gpio0_0_grp",
		"gpio0_2_grp", "gpio0_4_grp", "gpio0_6_grp",
		"gpio0_10_grp", "gpio0_12_grp",
		"gpio0_14_grp", "gpio0_16_grp", "gpio0_18_grp",
		"gpio0_20_grp", "gpio0_22_grp", "gpio0_24_grp",
		"gpio0_26_grp", "gpio0_28_grp", "gpio0_30_grp",
		"gpio0_32_grp", "gpio0_34_grp", "gpio0_36_grp",
		"gpio0_38_grp", "gpio0_40_grp", "gpio0_42_grp",
		"gpio0_44_grp", "gpio0_46_grp", "gpio0_48_grp",
		"gpio0_50_grp", "gpio0_52_grp", "gpio0_1_grp",
		"gpio0_3_grp", "gpio0_5_grp",
		"gpio0_9_grp", "gpio0_11_grp", "gpio0_13_grp",
		"gpio0_15_grp", "gpio0_17_grp", "gpio0_19_grp",
		"gpio0_21_grp", "gpio0_23_grp", "gpio0_25_grp",
		"gpio0_27_grp", "gpio0_29_grp", "gpio0_31_grp",
		"gpio0_33_grp", "gpio0_35_grp", "gpio0_37_grp",
		"gpio0_39_grp", "gpio0_41_grp", "gpio0_43_grp",
		"gpio0_45_grp", "gpio0_47_grp", "gpio0_49_grp",
		"gpio0_51_grp", "gpio0_53_grp", "sdio1_emio_cd_grp"};
static const char * const sdio1_wp_groups[] = {"gpio0_0_grp",
		"gpio0_2_grp", "gpio0_4_grp", "gpio0_6_grp",
		"gpio0_10_grp", "gpio0_12_grp",
		"gpio0_14_grp", "gpio0_16_grp", "gpio0_18_grp",
		"gpio0_20_grp", "gpio0_22_grp", "gpio0_24_grp",
		"gpio0_26_grp", "gpio0_28_grp", "gpio0_30_grp",
		"gpio0_32_grp", "gpio0_34_grp", "gpio0_36_grp",
		"gpio0_38_grp", "gpio0_40_grp", "gpio0_42_grp",
		"gpio0_44_grp", "gpio0_46_grp", "gpio0_48_grp",
		"gpio0_50_grp", "gpio0_52_grp", "gpio0_1_grp",
		"gpio0_3_grp", "gpio0_5_grp",
		"gpio0_9_grp", "gpio0_11_grp", "gpio0_13_grp",
		"gpio0_15_grp", "gpio0_17_grp", "gpio0_19_grp",
		"gpio0_21_grp", "gpio0_23_grp", "gpio0_25_grp",
		"gpio0_27_grp", "gpio0_29_grp", "gpio0_31_grp",
		"gpio0_33_grp", "gpio0_35_grp", "gpio0_37_grp",
		"gpio0_39_grp", "gpio0_41_grp", "gpio0_43_grp",
		"gpio0_45_grp", "gpio0_47_grp", "gpio0_49_grp",
		"gpio0_51_grp", "gpio0_53_grp", "sdio1_emio_wp_grp"};
static const char * const smc0_nor_groups[] = {"smc0_nor_grp"};
static const char * const smc0_nor_cs1_groups[] = {"smc0_nor_cs1_grp"};
static const char * const smc0_nor_addr25_groups[] = {"smc0_nor_addr25_grp"};
static const char * const smc0_nand_groups[] = {"smc0_nand_grp",
		"smc0_nand8_grp"};
static const char * const can0_groups[] = {"can0_0_grp", "can0_1_grp",
		"can0_2_grp", "can0_3_grp", "can0_4_grp", "can0_5_grp",
		"can0_6_grp", "can0_7_grp", "can0_8_grp", "can0_9_grp",
		"can0_10_grp"};
static const char * const can1_groups[] = {"can1_0_grp", "can1_1_grp",
		"can1_2_grp", "can1_3_grp", "can1_4_grp", "can1_5_grp",
		"can1_6_grp", "can1_7_grp", "can1_8_grp", "can1_9_grp",
		"can1_10_grp", "can1_11_grp"};
static const char * const uart0_groups[] = {"uart0_0_grp", "uart0_1_grp",
		"uart0_2_grp", "uart0_3_grp", "uart0_4_grp", "uart0_5_grp",
		"uart0_6_grp", "uart0_7_grp", "uart0_8_grp", "uart0_9_grp",
		"uart0_10_grp"};
static const char * const uart1_groups[] = {"uart1_0_grp", "uart1_1_grp",
		"uart1_2_grp", "uart1_3_grp", "uart1_4_grp", "uart1_5_grp",
		"uart1_6_grp", "uart1_7_grp", "uart1_8_grp", "uart1_9_grp",
		"uart1_10_grp", "uart1_11_grp"};
static const char * const i2c0_groups[] = {"i2c0_0_grp", "i2c0_1_grp",
		"i2c0_2_grp", "i2c0_3_grp", "i2c0_4_grp", "i2c0_5_grp",
		"i2c0_6_grp", "i2c0_7_grp", "i2c0_8_grp", "i2c0_9_grp",
		"i2c0_10_grp"};
static const char * const i2c1_groups[] = {"i2c1_0_grp", "i2c1_1_grp",
		"i2c1_2_grp", "i2c1_3_grp", "i2c1_4_grp", "i2c1_5_grp",
		"i2c1_6_grp", "i2c1_7_grp", "i2c1_8_grp", "i2c1_9_grp",
		"i2c1_10_grp"};
static const char * const ttc0_groups[] = {"ttc0_0_grp", "ttc0_1_grp",
					   "ttc0_2_grp"};
static const char * const ttc1_groups[] = {"ttc1_0_grp", "ttc1_1_grp",
					   "ttc1_2_grp"};
static const char * const swdt0_groups[] = {"swdt0_0_grp", "swdt0_1_grp",
		"swdt0_2_grp", "swdt0_3_grp", "swdt0_4_grp"};
static const char * const gpio0_groups[] = {"gpio0_0_grp",
		"gpio0_2_grp", "gpio0_4_grp", "gpio0_6_grp",
		"gpio0_8_grp", "gpio0_10_grp", "gpio0_12_grp",
		"gpio0_14_grp", "gpio0_16_grp", "gpio0_18_grp",
		"gpio0_20_grp", "gpio0_22_grp", "gpio0_24_grp",
		"gpio0_26_grp", "gpio0_28_grp", "gpio0_30_grp",
		"gpio0_32_grp", "gpio0_34_grp", "gpio0_36_grp",
		"gpio0_38_grp", "gpio0_40_grp", "gpio0_42_grp",
		"gpio0_44_grp", "gpio0_46_grp", "gpio0_48_grp",
		"gpio0_50_grp", "gpio0_52_grp", "gpio0_1_grp",
		"gpio0_3_grp", "gpio0_5_grp", "gpio0_7_grp",
		"gpio0_9_grp", "gpio0_11_grp", "gpio0_13_grp",
		"gpio0_15_grp", "gpio0_17_grp", "gpio0_19_grp",
		"gpio0_21_grp", "gpio0_23_grp", "gpio0_25_grp",
		"gpio0_27_grp", "gpio0_29_grp", "gpio0_31_grp",
		"gpio0_33_grp", "gpio0_35_grp", "gpio0_37_grp",
		"gpio0_39_grp", "gpio0_41_grp", "gpio0_43_grp",
		"gpio0_45_grp", "gpio0_47_grp", "gpio0_49_grp",
		"gpio0_51_grp", "gpio0_53_grp"};

#define DEFINE_ZYNQ_PINMUX_FUNCTION(fname, mval)	\
	[ZYNQ_PMUX_##fname] = {				\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
		.mux_val = mval,			\
	}

#define DEFINE_ZYNQ_PINMUX_FUNCTION_MUX(fname, mval, offset, mask, shift)\
	[ZYNQ_PMUX_##fname] = {				\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
		.mux_val = mval,			\
		.mux = offset,				\
		.mux_mask = mask,			\
		.mux_shift = shift,			\
	}

#define ZYNQ_SDIO_WP_SHIFT	0
#define ZYNQ_SDIO_WP_MASK	(0x3f << ZYNQ_SDIO_WP_SHIFT)
#define ZYNQ_SDIO_CD_SHIFT	16
#define ZYNQ_SDIO_CD_MASK	(0x3f << ZYNQ_SDIO_CD_SHIFT)

static const struct zynq_pinmux_function zynq_pmux_functions[] = {
	DEFINE_ZYNQ_PINMUX_FUNCTION(ethernet0, 1),
	DEFINE_ZYNQ_PINMUX_FUNCTION(ethernet1, 1),
	DEFINE_ZYNQ_PINMUX_FUNCTION(usb0, 2),
	DEFINE_ZYNQ_PINMUX_FUNCTION(usb1, 2),
	DEFINE_ZYNQ_PINMUX_FUNCTION(mdio0, 0x40),
	DEFINE_ZYNQ_PINMUX_FUNCTION(mdio1, 0x50),
	DEFINE_ZYNQ_PINMUX_FUNCTION(qspi0, 1),
	DEFINE_ZYNQ_PINMUX_FUNCTION(qspi1, 1),
	DEFINE_ZYNQ_PINMUX_FUNCTION(qspi_fbclk, 1),
	DEFINE_ZYNQ_PINMUX_FUNCTION(qspi_cs1, 1),
	DEFINE_ZYNQ_PINMUX_FUNCTION(spi0, 0x50),
	DEFINE_ZYNQ_PINMUX_FUNCTION(spi1, 0x50),
	DEFINE_ZYNQ_PINMUX_FUNCTION(spi0_ss, 0x50),
	DEFINE_ZYNQ_PINMUX_FUNCTION(spi1_ss, 0x50),
	DEFINE_ZYNQ_PINMUX_FUNCTION(sdio0, 0x40),
	DEFINE_ZYNQ_PINMUX_FUNCTION(sdio0_pc, 0xc),
	DEFINE_ZYNQ_PINMUX_FUNCTION_MUX(sdio0_wp, 0, 0x130, ZYNQ_SDIO_WP_MASK,
					ZYNQ_SDIO_WP_SHIFT),
	DEFINE_ZYNQ_PINMUX_FUNCTION_MUX(sdio0_cd, 0, 0x130, ZYNQ_SDIO_CD_MASK,
					ZYNQ_SDIO_CD_SHIFT),
	DEFINE_ZYNQ_PINMUX_FUNCTION(sdio1, 0x40),
	DEFINE_ZYNQ_PINMUX_FUNCTION(sdio1_pc, 0xc),
	DEFINE_ZYNQ_PINMUX_FUNCTION_MUX(sdio1_wp, 0, 0x134, ZYNQ_SDIO_WP_MASK,
					ZYNQ_SDIO_WP_SHIFT),
	DEFINE_ZYNQ_PINMUX_FUNCTION_MUX(sdio1_cd, 0, 0x134, ZYNQ_SDIO_CD_MASK,
					ZYNQ_SDIO_CD_SHIFT),
	DEFINE_ZYNQ_PINMUX_FUNCTION(smc0_nor, 4),
	DEFINE_ZYNQ_PINMUX_FUNCTION(smc0_nor_cs1, 8),
	DEFINE_ZYNQ_PINMUX_FUNCTION(smc0_nor_addr25, 4),
	DEFINE_ZYNQ_PINMUX_FUNCTION(smc0_nand, 8),
	DEFINE_ZYNQ_PINMUX_FUNCTION(can0, 0x10),
	DEFINE_ZYNQ_PINMUX_FUNCTION(can1, 0x10),
	DEFINE_ZYNQ_PINMUX_FUNCTION(uart0, 0x70),
	DEFINE_ZYNQ_PINMUX_FUNCTION(uart1, 0x70),
	DEFINE_ZYNQ_PINMUX_FUNCTION(i2c0, 0x20),
	DEFINE_ZYNQ_PINMUX_FUNCTION(i2c1, 0x20),
	DEFINE_ZYNQ_PINMUX_FUNCTION(ttc0, 0x60),
	DEFINE_ZYNQ_PINMUX_FUNCTION(ttc1, 0x60),
	DEFINE_ZYNQ_PINMUX_FUNCTION(swdt0, 0x30),
	DEFINE_ZYNQ_PINMUX_FUNCTION(gpio0, 0),
};


/* pinctrl */
static int zynq_pctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct zynq_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->ngroups;
}

static const char *zynq_pctrl_get_group_name(struct pinctrl_dev *pctldev,
					     unsigned int selector)
{
	struct zynq_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->groups[selector].name;
}

static int zynq_pctrl_get_group_pins(struct pinctrl_dev *pctldev,
				     unsigned int selector,
				     const unsigned int **pins,
				     unsigned int *num_pins)
{
	struct zynq_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pctrl->groups[selector].pins;
	*num_pins = pctrl->groups[selector].npins;

	return 0;
}

static const struct pinctrl_ops zynq_pctrl_ops = {
	.get_groups_count = zynq_pctrl_get_groups_count,
	.get_group_name = zynq_pctrl_get_group_name,
	.get_group_pins = zynq_pctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
};

/* pinmux */
static int zynq_pmux_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct zynq_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->nfuncs;
}

static const char *zynq_pmux_get_function_name(struct pinctrl_dev *pctldev,
					       unsigned int selector)
{
	struct zynq_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->funcs[selector].name;
}

static int zynq_pmux_get_function_groups(struct pinctrl_dev *pctldev,
					 unsigned int selector,
					 const char * const **groups,
					 unsigned * const num_groups)
{
	struct zynq_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctrl->funcs[selector].groups;
	*num_groups = pctrl->funcs[selector].ngroups;
	return 0;
}

static int zynq_pinmux_set_mux(struct pinctrl_dev *pctldev,
			       unsigned int function,
			       unsigned int  group)
{
	int i, ret;
	struct zynq_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct zynq_pctrl_group *pgrp = &pctrl->groups[group];
	const struct zynq_pinmux_function *func = &pctrl->funcs[function];

	/*
	 * SD WP & CD are special. They have dedicated registers
	 * to mux them in
	 */
	if (function == ZYNQ_PMUX_sdio0_cd || function == ZYNQ_PMUX_sdio0_wp ||
			function == ZYNQ_PMUX_sdio1_cd ||
			function == ZYNQ_PMUX_sdio1_wp) {
		u32 reg;

		ret = regmap_read(pctrl->syscon,
				  pctrl->pctrl_offset + func->mux, &reg);
		if (ret)
			return ret;

		reg &= ~func->mux_mask;
		reg |= pgrp->pins[0] << func->mux_shift;
		ret = regmap_write(pctrl->syscon,
				   pctrl->pctrl_offset + func->mux, reg);
		if (ret)
			return ret;
	} else {
		for (i = 0; i < pgrp->npins; i++) {
			unsigned int pin = pgrp->pins[i];
			u32 reg, addr = pctrl->pctrl_offset + (4 * pin);

			ret = regmap_read(pctrl->syscon, addr, &reg);
			if (ret)
				return ret;

			reg &= ~ZYNQ_PINMUX_MUX_MASK;
			reg |= func->mux_val << ZYNQ_PINMUX_MUX_SHIFT;
			ret = regmap_write(pctrl->syscon, addr, reg);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static const struct pinmux_ops zynq_pinmux_ops = {
	.get_functions_count = zynq_pmux_get_functions_count,
	.get_function_name = zynq_pmux_get_function_name,
	.get_function_groups = zynq_pmux_get_function_groups,
	.set_mux = zynq_pinmux_set_mux,
};

/* pinconfig */
#define ZYNQ_PINCONF_TRISTATE		BIT(0)
#define ZYNQ_PINCONF_SPEED		BIT(8)
#define ZYNQ_PINCONF_PULLUP		BIT(12)
#define ZYNQ_PINCONF_DISABLE_RECVR	BIT(13)

#define ZYNQ_PINCONF_IOTYPE_SHIFT	9
#define ZYNQ_PINCONF_IOTYPE_MASK	(7 << ZYNQ_PINCONF_IOTYPE_SHIFT)

enum zynq_io_standards {
	zynq_iostd_min,
	zynq_iostd_lvcmos18,
	zynq_iostd_lvcmos25,
	zynq_iostd_lvcmos33,
	zynq_iostd_hstl,
	zynq_iostd_max
};

/*
 * PIN_CONFIG_IOSTANDARD: if the pin can select an IO standard, the argument to
 *	this parameter (on a custom format) tells the driver which alternative
 *	IO standard to use.
 */
#define PIN_CONFIG_IOSTANDARD		(PIN_CONFIG_END + 1)

static const struct pinconf_generic_params zynq_dt_params[] = {
	{"io-standard", PIN_CONFIG_IOSTANDARD, zynq_iostd_lvcmos18},
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item zynq_conf_items[ARRAY_SIZE(zynq_dt_params)]
	= { PCONFDUMP(PIN_CONFIG_IOSTANDARD, "IO-standard", NULL, true),
};
#endif

static unsigned int zynq_pinconf_iostd_get(u32 reg)
{
	return (reg & ZYNQ_PINCONF_IOTYPE_MASK) >> ZYNQ_PINCONF_IOTYPE_SHIFT;
}

static int zynq_pinconf_cfg_get(struct pinctrl_dev *pctldev,
				unsigned int pin,
				unsigned long *config)
{
	u32 reg;
	int ret;
	unsigned int arg = 0;
	unsigned int param = pinconf_to_config_param(*config);
	struct zynq_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	if (pin >= ZYNQ_NUM_MIOS)
		return -ENOTSUPP;

	ret = regmap_read(pctrl->syscon, pctrl->pctrl_offset + (4 * pin), &reg);
	if (ret)
		return -EIO;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		if (!(reg & ZYNQ_PINCONF_PULLUP))
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		if (!(reg & ZYNQ_PINCONF_TRISTATE))
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		if (reg & ZYNQ_PINCONF_PULLUP || reg & ZYNQ_PINCONF_TRISTATE)
			return -EINVAL;
		break;
	case PIN_CONFIG_SLEW_RATE:
		arg = !!(reg & ZYNQ_PINCONF_SPEED);
		break;
	case PIN_CONFIG_MODE_LOW_POWER:
	{
		enum zynq_io_standards iostd = zynq_pinconf_iostd_get(reg);

		if (iostd != zynq_iostd_hstl)
			return -EINVAL;
		if (!(reg & ZYNQ_PINCONF_DISABLE_RECVR))
			return -EINVAL;
		arg = !!(reg & ZYNQ_PINCONF_DISABLE_RECVR);
		break;
	}
	case PIN_CONFIG_IOSTANDARD:
	case PIN_CONFIG_POWER_SOURCE:
		arg = zynq_pinconf_iostd_get(reg);
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int zynq_pinconf_cfg_set(struct pinctrl_dev *pctldev,
				unsigned int pin,
				unsigned long *configs,
				unsigned int num_configs)
{
	int i, ret;
	u32 reg;
	u32 pullup = 0;
	u32 tristate = 0;
	struct zynq_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	if (pin >= ZYNQ_NUM_MIOS)
		return -ENOTSUPP;

	ret = regmap_read(pctrl->syscon, pctrl->pctrl_offset + (4 * pin), &reg);
	if (ret)
		return -EIO;

	for (i = 0; i < num_configs; i++) {
		unsigned int param = pinconf_to_config_param(configs[i]);
		unsigned int arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			pullup = ZYNQ_PINCONF_PULLUP;
			break;
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			tristate = ZYNQ_PINCONF_TRISTATE;
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			reg &= ~(ZYNQ_PINCONF_PULLUP | ZYNQ_PINCONF_TRISTATE);
			break;
		case PIN_CONFIG_SLEW_RATE:
			if (arg)
				reg |= ZYNQ_PINCONF_SPEED;
			else
				reg &= ~ZYNQ_PINCONF_SPEED;

			break;
		case PIN_CONFIG_IOSTANDARD:
		case PIN_CONFIG_POWER_SOURCE:
			if (arg <= zynq_iostd_min || arg >= zynq_iostd_max) {
				dev_warn(pctldev->dev,
					 "unsupported IO standard '%u'\n",
					 param);
				break;
			}
			reg &= ~ZYNQ_PINCONF_IOTYPE_MASK;
			reg |= arg << ZYNQ_PINCONF_IOTYPE_SHIFT;
			break;
		case PIN_CONFIG_MODE_LOW_POWER:
			if (arg)
				reg |= ZYNQ_PINCONF_DISABLE_RECVR;
			else
				reg &= ~ZYNQ_PINCONF_DISABLE_RECVR;

			break;
		default:
			dev_warn(pctldev->dev,
				 "unsupported configuration parameter '%u'\n",
				 param);
			continue;
		}
	}

	if (tristate || pullup) {
		reg &= ~(ZYNQ_PINCONF_PULLUP | ZYNQ_PINCONF_TRISTATE);
		reg |= tristate | pullup;
	}

	ret = regmap_write(pctrl->syscon, pctrl->pctrl_offset + (4 * pin), reg);
	if (ret)
		return -EIO;

	return 0;
}

static int zynq_pinconf_group_set(struct pinctrl_dev *pctldev,
				  unsigned int selector,
				  unsigned long *configs,
				  unsigned int  num_configs)
{
	int i, ret;
	struct zynq_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct zynq_pctrl_group *pgrp = &pctrl->groups[selector];

	for (i = 0; i < pgrp->npins; i++) {
		ret = zynq_pinconf_cfg_set(pctldev, pgrp->pins[i], configs,
					   num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops zynq_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = zynq_pinconf_cfg_get,
	.pin_config_set = zynq_pinconf_cfg_set,
	.pin_config_group_set = zynq_pinconf_group_set,
};

static const struct pinctrl_desc zynq_desc = {
	.name = "zynq_pinctrl",
	.pins = zynq_pins,
	.npins = ARRAY_SIZE(zynq_pins),
	.pctlops = &zynq_pctrl_ops,
	.pmxops = &zynq_pinmux_ops,
	.confops = &zynq_pinconf_ops,
	.num_custom_params = ARRAY_SIZE(zynq_dt_params),
	.custom_params = zynq_dt_params,
#ifdef CONFIG_DEBUG_FS
	.custom_conf_items = zynq_conf_items,
#endif
	.owner = THIS_MODULE,
};

static int zynq_pinctrl_probe(struct platform_device *pdev)

{
	struct resource *res;
	struct zynq_pinctrl *pctrl;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->syscon = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							"syscon");
	if (IS_ERR(pctrl->syscon)) {
		dev_err(&pdev->dev, "unable to get syscon\n");
		return PTR_ERR(pctrl->syscon);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "missing IO resource\n");
		return -ENODEV;
	}
	pctrl->pctrl_offset = res->start;

	pctrl->groups = zynq_pctrl_groups;
	pctrl->ngroups = ARRAY_SIZE(zynq_pctrl_groups);
	pctrl->funcs = zynq_pmux_functions;
	pctrl->nfuncs = ARRAY_SIZE(zynq_pmux_functions);

	pctrl->pctrl = devm_pinctrl_register(&pdev->dev, &zynq_desc, pctrl);
	if (IS_ERR(pctrl->pctrl))
		return PTR_ERR(pctrl->pctrl);

	platform_set_drvdata(pdev, pctrl);

	dev_info(&pdev->dev, "zynq pinctrl initialized\n");

	return 0;
}

static const struct of_device_id zynq_pinctrl_of_match[] = {
	{ .compatible = "xlnx,pinctrl-zynq" },
	{ }
};
MODULE_DEVICE_TABLE(of, zynq_pinctrl_of_match);

static struct platform_driver zynq_pinctrl_driver = {
	.driver = {
		.name = "zynq-pinctrl",
		.of_match_table = zynq_pinctrl_of_match,
	},
	.probe = zynq_pinctrl_probe,
};

module_platform_driver(zynq_pinctrl_driver);
