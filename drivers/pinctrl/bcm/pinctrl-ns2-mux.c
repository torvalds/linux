/* Copyright (C) 2016 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file contains the Northstar2 IOMUX driver that supports group
 * based PINMUX configuration. The PWM is functional only when the
 * corresponding mfio pin group is selected as gpio.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../core.h"
#include "../pinctrl-utils.h"

#define NS2_NUM_IOMUX			19
#define NS2_NUM_PWM_MUX			4

#define NS2_PIN_MUX_BASE0		0x00
#define NS2_PIN_MUX_BASE1		0x01
#define NS2_PIN_CONF_BASE		0x02
#define NS2_MUX_PAD_FUNC1_OFFSET	0x04

#define NS2_PIN_SRC_MASK		0x01
#define NS2_PIN_PULL_MASK		0x03
#define NS2_PIN_DRIVE_STRENGTH_MASK	0x07

#define NS2_PIN_PULL_UP			0x01
#define NS2_PIN_PULL_DOWN		0x02

#define NS2_PIN_INPUT_EN_MASK		0x01

/*
 * Northstar2 IOMUX register description
 *
 * @base: base address number
 * @offset: register offset for mux configuration of a group
 * @shift: bit shift for mux configuration of a group
 * @mask: mask bits
 * @alt: alternate function to set to
 */
struct ns2_mux {
	unsigned int base;
	unsigned int offset;
	unsigned int shift;
	unsigned int mask;
	unsigned int alt;
};

/*
 * Keep track of Northstar2 IOMUX configuration and prevent double
 * configuration
 *
 * @ns2_mux: Northstar2 IOMUX register description
 * @is_configured: flag to indicate whether a mux setting has already
 * been configured
 */
struct ns2_mux_log {
	struct ns2_mux mux;
	bool is_configured;
};

/*
 * Group based IOMUX configuration
 *
 * @name: name of the group
 * @pins: array of pins used by this group
 * @num_pins: total number of pins used by this group
 * @mux: Northstar2 group based IOMUX configuration
 */
struct ns2_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned int num_pins;
	const struct ns2_mux mux;
};

/*
 * Northstar2 mux function and supported pin groups
 *
 * @name: name of the function
 * @groups: array of groups that can be supported by this function
 * @num_groups: total number of groups that can be supported by function
 */
struct ns2_pin_function {
	const char *name;
	const char * const *groups;
	const unsigned int num_groups;
};

/*
 * Northstar2 IOMUX pinctrl core
 *
 * @pctl: pointer to pinctrl_dev
 * @dev: pointer to device
 * @base0: first IOMUX register base
 * @base1: second IOMUX register base
 * @pinconf_base: configuration register base
 * @groups: pointer to array of groups
 * @num_groups: total number of groups
 * @functions: pointer to array of functions
 * @num_functions: total number of functions
 * @mux_log: pointer to the array of mux logs
 * @lock: lock to protect register access
 */
struct ns2_pinctrl {
	struct pinctrl_dev *pctl;
	struct device *dev;
	void __iomem *base0;
	void __iomem *base1;
	void __iomem *pinconf_base;

	const struct ns2_pin_group *groups;
	unsigned int num_groups;

	const struct ns2_pin_function *functions;
	unsigned int num_functions;

	struct ns2_mux_log *mux_log;

	spinlock_t lock;
};

/*
 * Pin configuration info
 *
 * @base: base address number
 * @offset: register offset from base
 * @src_shift: slew rate control bit shift in the register
 * @input_en: input enable control bit shift
 * @pull_shift: pull-up/pull-down control bit shift in the register
 * @drive_shift: drive strength control bit shift in the register
 */
struct ns2_pinconf {
	unsigned int base;
	unsigned int offset;
	unsigned int src_shift;
	unsigned int input_en;
	unsigned int pull_shift;
	unsigned int drive_shift;
};

/*
 * Description of a pin in Northstar2
 *
 * @pin: pin number
 * @name: pin name
 * @pin_conf: pin configuration structure
 */
struct ns2_pin {
	unsigned int pin;
	char *name;
	struct ns2_pinconf pin_conf;
};

#define NS2_PIN_DESC(p, n, b, o, s, i, pu, d)	\
{						\
	.pin = p,				\
	.name = n,				\
	.pin_conf = {				\
		.base = b,			\
		.offset = o,			\
		.src_shift = s,			\
		.input_en = i,			\
		.pull_shift = pu,		\
		.drive_shift = d,		\
	}					\
}

/*
 * List of pins in Northstar2
 */
static struct ns2_pin ns2_pins[] = {
	NS2_PIN_DESC(0, "mfio_0", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(1, "mfio_1", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(2, "mfio_2", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(3, "mfio_3", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(4, "mfio_4", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(5, "mfio_5", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(6, "mfio_6", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(7, "mfio_7", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(8, "mfio_8", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(9, "mfio_9", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(10, "mfio_10", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(11, "mfio_11", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(12, "mfio_12", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(13, "mfio_13", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(14, "mfio_14", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(15, "mfio_15", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(16, "mfio_16", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(17, "mfio_17", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(18, "mfio_18", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(19, "mfio_19", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(20, "mfio_20", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(21, "mfio_21", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(22, "mfio_22", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(23, "mfio_23", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(24, "mfio_24", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(25, "mfio_25", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(26, "mfio_26", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(27, "mfio_27", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(28, "mfio_28", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(29, "mfio_29", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(30, "mfio_30", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(31, "mfio_31", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(32, "mfio_32", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(33, "mfio_33", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(34, "mfio_34", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(35, "mfio_35", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(36, "mfio_36", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(37, "mfio_37", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(38, "mfio_38", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(39, "mfio_39", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(40, "mfio_40", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(41, "mfio_41", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(42, "mfio_42", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(43, "mfio_43", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(44, "mfio_44", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(45, "mfio_45", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(46, "mfio_46", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(47, "mfio_47", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(48, "mfio_48", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(49, "mfio_49", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(50, "mfio_50", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(51, "mfio_51", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(52, "mfio_52", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(53, "mfio_53", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(54, "mfio_54", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(55, "mfio_55", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(56, "mfio_56", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(57, "mfio_57", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(58, "mfio_58", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(59, "mfio_59", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(60, "mfio_60", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(61, "mfio_61", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(62, "mfio_62", -1, 0, 0, 0, 0, 0),
	NS2_PIN_DESC(63, "qspi_wp", 2, 0x0, 31, 30, 27, 24),
	NS2_PIN_DESC(64, "qspi_hold", 2, 0x0, 23, 22, 19, 16),
	NS2_PIN_DESC(65, "qspi_cs", 2, 0x0, 15, 14, 11, 8),
	NS2_PIN_DESC(66, "qspi_sck", 2, 0x0, 7, 6, 3, 0),
	NS2_PIN_DESC(67, "uart3_sin", 2, 0x04, 31, 30, 27, 24),
	NS2_PIN_DESC(68, "uart3_sout", 2, 0x04, 23, 22, 19, 16),
	NS2_PIN_DESC(69, "qspi_mosi", 2, 0x04, 15, 14, 11, 8),
	NS2_PIN_DESC(70, "qspi_miso", 2, 0x04, 7, 6, 3, 0),
	NS2_PIN_DESC(71, "spi0_fss", 2, 0x08, 31, 30, 27, 24),
	NS2_PIN_DESC(72, "spi0_rxd", 2, 0x08, 23, 22, 19, 16),
	NS2_PIN_DESC(73, "spi0_txd", 2, 0x08, 15, 14, 11, 8),
	NS2_PIN_DESC(74, "spi0_sck", 2, 0x08, 7, 6, 3, 0),
	NS2_PIN_DESC(75, "spi1_fss", 2, 0x0c, 31, 30, 27, 24),
	NS2_PIN_DESC(76, "spi1_rxd", 2, 0x0c, 23, 22, 19, 16),
	NS2_PIN_DESC(77, "spi1_txd", 2, 0x0c, 15, 14, 11, 8),
	NS2_PIN_DESC(78, "spi1_sck", 2, 0x0c, 7, 6, 3, 0),
	NS2_PIN_DESC(79, "sdio0_data7", 2, 0x10, 31, 30, 27, 24),
	NS2_PIN_DESC(80, "sdio0_emmc_rst", 2, 0x10, 23, 22, 19, 16),
	NS2_PIN_DESC(81, "sdio0_led_on", 2, 0x10, 15, 14, 11, 8),
	NS2_PIN_DESC(82, "sdio0_wp", 2, 0x10, 7, 6, 3, 0),
	NS2_PIN_DESC(83, "sdio0_data3", 2, 0x14, 31, 30, 27, 24),
	NS2_PIN_DESC(84, "sdio0_data4", 2, 0x14, 23, 22, 19, 16),
	NS2_PIN_DESC(85, "sdio0_data5", 2, 0x14, 15, 14, 11, 8),
	NS2_PIN_DESC(86, "sdio0_data6", 2, 0x14, 7, 6, 3, 0),
	NS2_PIN_DESC(87, "sdio0_cmd", 2, 0x18, 31, 30, 27, 24),
	NS2_PIN_DESC(88, "sdio0_data0", 2, 0x18, 23, 22, 19, 16),
	NS2_PIN_DESC(89, "sdio0_data1", 2, 0x18, 15, 14, 11, 8),
	NS2_PIN_DESC(90, "sdio0_data2", 2, 0x18, 7, 6, 3, 0),
	NS2_PIN_DESC(91, "sdio1_led_on", 2, 0x1c, 31, 30, 27, 24),
	NS2_PIN_DESC(92, "sdio1_wp", 2, 0x1c, 23, 22, 19, 16),
	NS2_PIN_DESC(93, "sdio0_cd_l", 2, 0x1c, 15, 14, 11, 8),
	NS2_PIN_DESC(94, "sdio0_clk", 2, 0x1c, 7, 6, 3, 0),
	NS2_PIN_DESC(95, "sdio1_data5", 2, 0x20, 31, 30, 27, 24),
	NS2_PIN_DESC(96, "sdio1_data6", 2, 0x20, 23, 22, 19, 16),
	NS2_PIN_DESC(97, "sdio1_data7", 2, 0x20, 15, 14, 11, 8),
	NS2_PIN_DESC(98, "sdio1_emmc_rst", 2, 0x20, 7, 6, 3, 0),
	NS2_PIN_DESC(99, "sdio1_data1", 2, 0x24, 31, 30, 27, 24),
	NS2_PIN_DESC(100, "sdio1_data2", 2, 0x24, 23, 22, 19, 16),
	NS2_PIN_DESC(101, "sdio1_data3", 2, 0x24, 15, 14, 11, 8),
	NS2_PIN_DESC(102, "sdio1_data4", 2, 0x24, 7, 6, 3, 0),
	NS2_PIN_DESC(103, "sdio1_cd_l", 2, 0x28, 31, 30, 27, 24),
	NS2_PIN_DESC(104, "sdio1_clk", 2, 0x28, 23, 22, 19, 16),
	NS2_PIN_DESC(105, "sdio1_cmd", 2, 0x28, 15, 14, 11, 8),
	NS2_PIN_DESC(106, "sdio1_data0", 2, 0x28, 7, 6, 3, 0),
	NS2_PIN_DESC(107, "ext_mdio_0", 2, 0x2c, 15, 14, 11, 8),
	NS2_PIN_DESC(108, "ext_mdc_0", 2, 0x2c, 7, 6, 3, 0),
	NS2_PIN_DESC(109, "usb3_p1_vbus_ppc", 2, 0x34, 31, 30, 27, 24),
	NS2_PIN_DESC(110, "usb3_p1_overcurrent", 2, 0x34, 23, 22, 19, 16),
	NS2_PIN_DESC(111, "usb3_p0_vbus_ppc", 2, 0x34, 15, 14, 11, 8),
	NS2_PIN_DESC(112, "usb3_p0_overcurrent", 2, 0x34, 7, 6, 3, 0),
	NS2_PIN_DESC(113, "usb2_presence_indication", 2, 0x38, 31, 30, 27, 24),
	NS2_PIN_DESC(114, "usb2_vbus_present", 2, 0x38, 23, 22, 19, 16),
	NS2_PIN_DESC(115, "usb2_vbus_ppc", 2, 0x38, 15, 14, 11, 8),
	NS2_PIN_DESC(116, "usb2_overcurrent", 2, 0x38, 7, 6, 3, 0),
	NS2_PIN_DESC(117, "sata_led1", 2, 0x3c, 15, 14, 11, 8),
	NS2_PIN_DESC(118, "sata_led0", 2, 0x3c, 7, 6, 3, 0),
};

/*
 * List of groups of pins
 */

static const unsigned int nand_pins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
	11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
static const unsigned int nor_data_pins[] =  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};

static const unsigned int gpio_0_1_pins[] = {24, 25};
static const unsigned int pwm_0_pins[] = {24};
static const unsigned int pwm_1_pins[] = {25};

static const unsigned int uart1_ext_clk_pins[] = {26};
static const unsigned int nor_adv_pins[] = {26};

static const unsigned int gpio_2_5_pins[] = {27, 28, 29, 30};
static const unsigned int pcie_ab1_clk_wak_pins[] = {27, 28, 29, 30};
static const unsigned int nor_addr_0_3_pins[] = {27, 28, 29, 30};
static const unsigned int pwm_2_pins[] = {27};
static const unsigned int pwm_3_pins[] = {28};

static const unsigned int gpio_6_7_pins[] = {31, 32};
static const unsigned int pcie_a3_clk_wak_pins[] = {31, 32};
static const unsigned int nor_addr_4_5_pins[] = {31, 32};

static const unsigned int gpio_8_9_pins[] = {33, 34};
static const unsigned int pcie_b3_clk_wak_pins[] = {33, 34};
static const unsigned int nor_addr_6_7_pins[] = {33, 34};

static const unsigned int gpio_10_11_pins[] = {35, 36};
static const unsigned int pcie_b2_clk_wak_pins[] = {35, 36};
static const unsigned int nor_addr_8_9_pins[] = {35, 36};

static const unsigned int gpio_12_13_pins[] = {37, 38};
static const unsigned int pcie_a2_clk_wak_pins[] = {37, 38};
static const unsigned int nor_addr_10_11_pins[] = {37, 38};

static const unsigned int gpio_14_17_pins[] = {39, 40, 41, 42};
static const unsigned int uart0_modem_pins[] = {39, 40, 41, 42};
static const unsigned int nor_addr_12_15_pins[] = {39, 40, 41, 42};

static const unsigned int gpio_18_19_pins[] = {43, 44};
static const unsigned int uart0_rts_cts_pins[] = {43, 44};

static const unsigned int gpio_20_21_pins[] = {45, 46};
static const unsigned int uart0_in_out_pins[] = {45, 46};

static const unsigned int gpio_22_23_pins[] = {47, 48};
static const unsigned int uart1_dcd_dsr_pins[] = {47, 48};

static const unsigned int gpio_24_25_pins[] = {49, 50};
static const unsigned int uart1_ri_dtr_pins[] = {49, 50};

static const unsigned int gpio_26_27_pins[] = {51, 52};
static const unsigned int uart1_rts_cts_pins[] = {51, 52};

static const unsigned int gpio_28_29_pins[] = {53, 54};
static const unsigned int uart1_in_out_pins[] = {53, 54};

static const unsigned int gpio_30_31_pins[] = {55, 56};
static const unsigned int uart2_rts_cts_pins[] = {55, 56};

#define NS2_PIN_GROUP(group_name, ba, off, sh, ma, al)	\
{							\
	.name = __stringify(group_name) "_grp",		\
	.pins = group_name ## _pins,			\
	.num_pins = ARRAY_SIZE(group_name ## _pins),	\
	.mux = {					\
		.base = ba,				\
		.offset = off,				\
		.shift = sh,				\
		.mask = ma,				\
		.alt = al,				\
	}						\
}

/*
 * List of Northstar2 pin groups
 */
static const struct ns2_pin_group ns2_pin_groups[] = {
	NS2_PIN_GROUP(nand, 0, 0, 31, 1, 0),
	NS2_PIN_GROUP(nor_data, 0, 0, 31, 1, 1),
	NS2_PIN_GROUP(gpio_0_1, 0, 0, 31, 1, 0),

	NS2_PIN_GROUP(uart1_ext_clk, 0, 4, 30, 3, 1),
	NS2_PIN_GROUP(nor_adv, 0, 4, 30, 3, 2),

	NS2_PIN_GROUP(gpio_2_5,	0, 4, 28, 3, 0),
	NS2_PIN_GROUP(pcie_ab1_clk_wak, 0, 4, 28, 3, 1),
	NS2_PIN_GROUP(nor_addr_0_3, 0, 4, 28, 3, 2),

	NS2_PIN_GROUP(gpio_6_7, 0, 4, 26, 3, 0),
	NS2_PIN_GROUP(pcie_a3_clk_wak, 0, 4, 26, 3, 1),
	NS2_PIN_GROUP(nor_addr_4_5, 0, 4, 26, 3, 2),

	NS2_PIN_GROUP(gpio_8_9, 0, 4, 24, 3, 0),
	NS2_PIN_GROUP(pcie_b3_clk_wak, 0, 4, 24, 3, 1),
	NS2_PIN_GROUP(nor_addr_6_7, 0, 4, 24, 3, 2),

	NS2_PIN_GROUP(gpio_10_11, 0, 4, 22, 3, 0),
	NS2_PIN_GROUP(pcie_b2_clk_wak, 0, 4, 22, 3, 1),
	NS2_PIN_GROUP(nor_addr_8_9, 0, 4, 22, 3, 2),

	NS2_PIN_GROUP(gpio_12_13, 0, 4, 20, 3, 0),
	NS2_PIN_GROUP(pcie_a2_clk_wak, 0, 4, 20, 3, 1),
	NS2_PIN_GROUP(nor_addr_10_11, 0, 4, 20, 3, 2),

	NS2_PIN_GROUP(gpio_14_17, 0, 4, 18, 3, 0),
	NS2_PIN_GROUP(uart0_modem, 0, 4, 18, 3, 1),
	NS2_PIN_GROUP(nor_addr_12_15, 0, 4, 18, 3, 2),

	NS2_PIN_GROUP(gpio_18_19, 0, 4, 16, 3, 0),
	NS2_PIN_GROUP(uart0_rts_cts, 0, 4, 16, 3, 1),

	NS2_PIN_GROUP(gpio_20_21, 0, 4, 14, 3, 0),
	NS2_PIN_GROUP(uart0_in_out, 0, 4, 14, 3, 1),

	NS2_PIN_GROUP(gpio_22_23, 0, 4, 12, 3, 0),
	NS2_PIN_GROUP(uart1_dcd_dsr, 0, 4, 12, 3, 1),

	NS2_PIN_GROUP(gpio_24_25, 0, 4, 10, 3, 0),
	NS2_PIN_GROUP(uart1_ri_dtr, 0, 4, 10, 3, 1),

	NS2_PIN_GROUP(gpio_26_27, 0, 4, 8, 3, 0),
	NS2_PIN_GROUP(uart1_rts_cts, 0, 4, 8, 3, 1),

	NS2_PIN_GROUP(gpio_28_29, 0, 4, 6, 3, 0),
	NS2_PIN_GROUP(uart1_in_out, 0, 4, 6, 3, 1),

	NS2_PIN_GROUP(gpio_30_31, 0, 4, 4, 3, 0),
	NS2_PIN_GROUP(uart2_rts_cts, 0, 4, 4, 3, 1),

	NS2_PIN_GROUP(pwm_0, 1, 0, 0, 1, 1),
	NS2_PIN_GROUP(pwm_1, 1, 0, 1, 1, 1),
	NS2_PIN_GROUP(pwm_2, 1, 0, 2, 1, 1),
	NS2_PIN_GROUP(pwm_3, 1, 0, 3, 1, 1),
};

/*
 * List of groups supported by functions
 */

static const char * const nand_grps[] = {"nand_grp"};

static const char * const nor_grps[] = {"nor_data_grp", "nor_adv_grp",
	"nor_addr_0_3_grp", "nor_addr_4_5_grp",	"nor_addr_6_7_grp",
	"nor_addr_8_9_grp", "nor_addr_10_11_grp", "nor_addr_12_15_grp"};

static const char * const gpio_grps[] = {"gpio_0_1_grp", "gpio_2_5_grp",
	"gpio_6_7_grp",	"gpio_8_9_grp",	"gpio_10_11_grp", "gpio_12_13_grp",
	"gpio_14_17_grp", "gpio_18_19_grp", "gpio_20_21_grp", "gpio_22_23_grp",
	"gpio_24_25_grp", "gpio_26_27_grp", "gpio_28_29_grp",
	"gpio_30_31_grp"};

static const char * const pcie_grps[] = {"pcie_ab1_clk_wak_grp",
	"pcie_a3_clk_wak_grp", "pcie_b3_clk_wak_grp", "pcie_b2_clk_wak_grp",
	"pcie_a2_clk_wak_grp"};

static const char * const uart0_grps[] = {"uart0_modem_grp",
	"uart0_rts_cts_grp", "uart0_in_out_grp"};

static const char * const uart1_grps[] = {"uart1_ext_clk_grp",
	"uart1_dcd_dsr_grp", "uart1_ri_dtr_grp", "uart1_rts_cts_grp",
	"uart1_in_out_grp"};

static const char * const uart2_grps[] = {"uart2_rts_cts_grp"};

static const char * const pwm_grps[] = {"pwm_0_grp", "pwm_1_grp",
	"pwm_2_grp", "pwm_3_grp"};

#define NS2_PIN_FUNCTION(func)				\
{							\
	.name = #func,					\
	.groups = func ## _grps,			\
	.num_groups = ARRAY_SIZE(func ## _grps),	\
}

/*
 * List of supported functions
 */
static const struct ns2_pin_function ns2_pin_functions[] = {
	NS2_PIN_FUNCTION(nand),
	NS2_PIN_FUNCTION(nor),
	NS2_PIN_FUNCTION(gpio),
	NS2_PIN_FUNCTION(pcie),
	NS2_PIN_FUNCTION(uart0),
	NS2_PIN_FUNCTION(uart1),
	NS2_PIN_FUNCTION(uart2),
	NS2_PIN_FUNCTION(pwm),
};

static int ns2_get_groups_count(struct pinctrl_dev *pctrl_dev)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->num_groups;
}

static const char *ns2_get_group_name(struct pinctrl_dev *pctrl_dev,
				      unsigned int selector)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->groups[selector].name;
}

static int ns2_get_group_pins(struct pinctrl_dev *pctrl_dev,
			      unsigned int selector, const unsigned int **pins,
			      unsigned int *num_pins)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	*pins = pinctrl->groups[selector].pins;
	*num_pins = pinctrl->groups[selector].num_pins;

	return 0;
}

static void ns2_pin_dbg_show(struct pinctrl_dev *pctrl_dev,
			     struct seq_file *s, unsigned int offset)
{
	seq_printf(s, " %s", dev_name(pctrl_dev->dev));
}

static const struct pinctrl_ops ns2_pinctrl_ops = {
	.get_groups_count = ns2_get_groups_count,
	.get_group_name = ns2_get_group_name,
	.get_group_pins = ns2_get_group_pins,
	.pin_dbg_show = ns2_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static int ns2_get_functions_count(struct pinctrl_dev *pctrl_dev)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->num_functions;
}

static const char *ns2_get_function_name(struct pinctrl_dev *pctrl_dev,
					 unsigned int selector)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->functions[selector].name;
}

static int ns2_get_function_groups(struct pinctrl_dev *pctrl_dev,
				   unsigned int selector,
				   const char * const **groups,
				   unsigned int * const num_groups)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	*groups = pinctrl->functions[selector].groups;
	*num_groups = pinctrl->functions[selector].num_groups;

	return 0;
}

static int ns2_pinmux_set(struct ns2_pinctrl *pinctrl,
			  const struct ns2_pin_function *func,
			  const struct ns2_pin_group *grp,
			  struct ns2_mux_log *mux_log)
{
	const struct ns2_mux *mux = &grp->mux;
	int i;
	u32 val, mask;
	unsigned long flags;
	void __iomem *base_address;

	for (i = 0; i < NS2_NUM_IOMUX; i++) {
		if ((mux->shift != mux_log[i].mux.shift) ||
			(mux->base != mux_log[i].mux.base) ||
			(mux->offset != mux_log[i].mux.offset))
			continue;

		/* if this is a new configuration, just do it! */
		if (!mux_log[i].is_configured)
			break;

		/*
		 * IOMUX has been configured previously and one is trying to
		 * configure it to a different function
		 */
		if (mux_log[i].mux.alt != mux->alt) {
			dev_err(pinctrl->dev,
				"double configuration error detected!\n");
			dev_err(pinctrl->dev, "func:%s grp:%s\n",
				func->name, grp->name);
			return -EINVAL;
		}

		return 0;
	}
	if (i == NS2_NUM_IOMUX)
		return -EINVAL;

	mask = mux->mask;
	mux_log[i].mux.alt = mux->alt;
	mux_log[i].is_configured = true;

	switch (mux->base) {
	case NS2_PIN_MUX_BASE0:
		base_address = pinctrl->base0;
		break;

	case NS2_PIN_MUX_BASE1:
		base_address = pinctrl->base1;
		break;

	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(base_address + grp->mux.offset);
	val &= ~(mask << grp->mux.shift);
	val |= grp->mux.alt << grp->mux.shift;
	writel(val, (base_address + grp->mux.offset));
	spin_unlock_irqrestore(&pinctrl->lock, flags);

	return 0;
}

static int ns2_pinmux_enable(struct pinctrl_dev *pctrl_dev,
			     unsigned int func_select, unsigned int grp_select)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	const struct ns2_pin_function *func;
	const struct ns2_pin_group *grp;

	if (grp_select >= pinctrl->num_groups ||
		func_select >= pinctrl->num_functions)
		return -EINVAL;

	func = &pinctrl->functions[func_select];
	grp = &pinctrl->groups[grp_select];

	dev_dbg(pctrl_dev->dev, "func:%u name:%s grp:%u name:%s\n",
		func_select, func->name, grp_select, grp->name);

	dev_dbg(pctrl_dev->dev, "offset:0x%08x shift:%u alt:%u\n",
		grp->mux.offset, grp->mux.shift, grp->mux.alt);

	return ns2_pinmux_set(pinctrl, func, grp, pinctrl->mux_log);
}

static int ns2_pin_set_enable(struct pinctrl_dev *pctrldev, unsigned int pin,
			    u16 enable)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrldev);
	struct ns2_pin *pin_data = pctrldev->desc->pins[pin].drv_data;
	unsigned long flags;
	u32 val;
	void __iomem *base_address;

	base_address = pinctrl->pinconf_base;
	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(base_address + pin_data->pin_conf.offset);
	val &= ~(NS2_PIN_SRC_MASK << pin_data->pin_conf.input_en);

	if (!enable)
		val |= NS2_PIN_INPUT_EN_MASK << pin_data->pin_conf.input_en;

	writel(val, (base_address + pin_data->pin_conf.offset));
	spin_unlock_irqrestore(&pinctrl->lock, flags);

	dev_dbg(pctrldev->dev, "pin:%u set enable:%d\n", pin, enable);
	return 0;
}

static int ns2_pin_get_enable(struct pinctrl_dev *pctrldev, unsigned int pin)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrldev);
	struct ns2_pin *pin_data = pctrldev->desc->pins[pin].drv_data;
	unsigned long flags;
	int enable;

	spin_lock_irqsave(&pinctrl->lock, flags);
	enable = readl(pinctrl->pinconf_base + pin_data->pin_conf.offset);
	enable = (enable >> pin_data->pin_conf.input_en) &
			NS2_PIN_INPUT_EN_MASK;
	spin_unlock_irqrestore(&pinctrl->lock, flags);

	if (!enable)
		enable = NS2_PIN_INPUT_EN_MASK;
	else
		enable = 0;

	dev_dbg(pctrldev->dev, "pin:%u get disable:%d\n", pin, enable);
	return enable;
}

static int ns2_pin_set_slew(struct pinctrl_dev *pctrldev, unsigned int pin,
			    u32 slew)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrldev);
	struct ns2_pin *pin_data = pctrldev->desc->pins[pin].drv_data;
	unsigned long flags;
	u32 val;
	void __iomem *base_address;

	base_address = pinctrl->pinconf_base;
	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(base_address + pin_data->pin_conf.offset);
	val &= ~(NS2_PIN_SRC_MASK << pin_data->pin_conf.src_shift);

	if (slew)
		val |= NS2_PIN_SRC_MASK << pin_data->pin_conf.src_shift;

	writel(val, (base_address + pin_data->pin_conf.offset));
	spin_unlock_irqrestore(&pinctrl->lock, flags);

	dev_dbg(pctrldev->dev, "pin:%u set slew:%d\n", pin, slew);
	return 0;
}

static int ns2_pin_get_slew(struct pinctrl_dev *pctrldev, unsigned int pin,
			    u16 *slew)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrldev);
	struct ns2_pin *pin_data = pctrldev->desc->pins[pin].drv_data;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(pinctrl->pinconf_base + pin_data->pin_conf.offset);
	*slew = (val >> pin_data->pin_conf.src_shift) & NS2_PIN_SRC_MASK;
	spin_unlock_irqrestore(&pinctrl->lock, flags);

	dev_dbg(pctrldev->dev, "pin:%u get slew:%d\n", pin, *slew);
	return 0;
}

static int ns2_pin_set_pull(struct pinctrl_dev *pctrldev, unsigned int pin,
			    bool pull_up, bool pull_down)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrldev);
	struct ns2_pin *pin_data = pctrldev->desc->pins[pin].drv_data;
	unsigned long flags;
	u32 val;
	void __iomem *base_address;

	base_address = pinctrl->pinconf_base;
	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(base_address + pin_data->pin_conf.offset);
	val &= ~(NS2_PIN_PULL_MASK << pin_data->pin_conf.pull_shift);

	if (pull_up == true)
		val |= NS2_PIN_PULL_UP << pin_data->pin_conf.pull_shift;
	if (pull_down == true)
		val |= NS2_PIN_PULL_DOWN << pin_data->pin_conf.pull_shift;
	writel(val, (base_address + pin_data->pin_conf.offset));
	spin_unlock_irqrestore(&pinctrl->lock, flags);

	dev_dbg(pctrldev->dev, "pin:%u set pullup:%d pulldown: %d\n",
		pin, pull_up, pull_down);
	return 0;
}

static void ns2_pin_get_pull(struct pinctrl_dev *pctrldev,
			     unsigned int pin, bool *pull_up,
			     bool *pull_down)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrldev);
	struct ns2_pin *pin_data = pctrldev->desc->pins[pin].drv_data;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(pinctrl->pinconf_base + pin_data->pin_conf.offset);
	val = (val >> pin_data->pin_conf.pull_shift) & NS2_PIN_PULL_MASK;
	*pull_up = false;
	*pull_down = false;

	if (val == NS2_PIN_PULL_UP)
		*pull_up = true;

	if (val == NS2_PIN_PULL_DOWN)
		*pull_down = true;
	spin_unlock_irqrestore(&pinctrl->lock, flags);
}

static int ns2_pin_set_strength(struct pinctrl_dev *pctrldev, unsigned int pin,
				u32 strength)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrldev);
	struct ns2_pin *pin_data = pctrldev->desc->pins[pin].drv_data;
	u32 val;
	unsigned long flags;
	void __iomem *base_address;

	/* make sure drive strength is supported */
	if (strength < 2 || strength > 16 || (strength % 2))
		return -ENOTSUPP;

	base_address = pinctrl->pinconf_base;
	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(base_address + pin_data->pin_conf.offset);
	val &= ~(NS2_PIN_DRIVE_STRENGTH_MASK << pin_data->pin_conf.drive_shift);
	val |= ((strength / 2) - 1) << pin_data->pin_conf.drive_shift;
	writel(val, (base_address + pin_data->pin_conf.offset));
	spin_unlock_irqrestore(&pinctrl->lock, flags);

	dev_dbg(pctrldev->dev, "pin:%u set drive strength:%d mA\n",
		pin, strength);
	return 0;
}

static int ns2_pin_get_strength(struct pinctrl_dev *pctrldev, unsigned int pin,
				 u16 *strength)
{
	struct ns2_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrldev);
	struct ns2_pin *pin_data = pctrldev->desc->pins[pin].drv_data;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(pinctrl->pinconf_base + pin_data->pin_conf.offset);
	*strength = (val >> pin_data->pin_conf.drive_shift) &
					NS2_PIN_DRIVE_STRENGTH_MASK;
	*strength = (*strength + 1) * 2;
	spin_unlock_irqrestore(&pinctrl->lock, flags);

	dev_dbg(pctrldev->dev, "pin:%u get drive strength:%d mA\n",
		pin, *strength);
	return 0;
}

static int ns2_pin_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *config)
{
	struct ns2_pin *pin_data = pctldev->desc->pins[pin].drv_data;
	enum pin_config_param param = pinconf_to_config_param(*config);
	bool pull_up, pull_down;
	u16 arg = 0;
	int ret;

	if (pin_data->pin_conf.base == -1)
		return -ENOTSUPP;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		ns2_pin_get_pull(pctldev, pin, &pull_up, &pull_down);
		if ((pull_up == false) && (pull_down == false))
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_BIAS_PULL_UP:
		ns2_pin_get_pull(pctldev, pin, &pull_up, &pull_down);
		if (pull_up)
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		ns2_pin_get_pull(pctldev, pin, &pull_up, &pull_down);
		if (pull_down)
			return 0;
		else
			return -EINVAL;

	case PIN_CONFIG_DRIVE_STRENGTH:
		ret = ns2_pin_get_strength(pctldev, pin, &arg);
		if (ret)
			return ret;
		*config = pinconf_to_config_packed(param, arg);
		return 0;

	case PIN_CONFIG_SLEW_RATE:
		ret = ns2_pin_get_slew(pctldev, pin, &arg);
		if (ret)
			return ret;
		*config = pinconf_to_config_packed(param, arg);
		return 0;

	case PIN_CONFIG_INPUT_ENABLE:
		ret = ns2_pin_get_enable(pctldev, pin);
		if (ret)
			return 0;
		else
			return -EINVAL;

	default:
		return -ENOTSUPP;
	}
}

static int ns2_pin_config_set(struct pinctrl_dev *pctrldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	struct ns2_pin *pin_data = pctrldev->desc->pins[pin].drv_data;
	enum pin_config_param param;
	unsigned int i;
	u32 arg;
	int ret = -ENOTSUPP;

	if (pin_data->pin_conf.base == -1)
		return -ENOTSUPP;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			ret = ns2_pin_set_pull(pctrldev, pin, false, false);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			ret = ns2_pin_set_pull(pctrldev, pin, true, false);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = ns2_pin_set_pull(pctrldev, pin, false, true);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			ret = ns2_pin_set_strength(pctrldev, pin, arg);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_SLEW_RATE:
			ret = ns2_pin_set_slew(pctrldev, pin, arg);
			if (ret < 0)
				goto out;
			break;

		case PIN_CONFIG_INPUT_ENABLE:
			ret = ns2_pin_set_enable(pctrldev, pin, arg);
			if (ret < 0)
				goto out;
			break;

		default:
			dev_err(pctrldev->dev, "invalid configuration\n");
			return -ENOTSUPP;
		}
	}
out:
	return ret;
}
static const struct pinmux_ops ns2_pinmux_ops = {
	.get_functions_count = ns2_get_functions_count,
	.get_function_name = ns2_get_function_name,
	.get_function_groups = ns2_get_function_groups,
	.set_mux = ns2_pinmux_enable,
};

static const struct pinconf_ops ns2_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = ns2_pin_config_get,
	.pin_config_set = ns2_pin_config_set,
};

static struct pinctrl_desc ns2_pinctrl_desc = {
	.name = "ns2-pinmux",
	.pctlops = &ns2_pinctrl_ops,
	.pmxops = &ns2_pinmux_ops,
	.confops = &ns2_pinconf_ops,
};

static int ns2_mux_log_init(struct ns2_pinctrl *pinctrl)
{
	struct ns2_mux_log *log;
	unsigned int i;

	pinctrl->mux_log = devm_kcalloc(pinctrl->dev, NS2_NUM_IOMUX,
					sizeof(struct ns2_mux_log),
					GFP_KERNEL);
	if (!pinctrl->mux_log)
		return -ENOMEM;

	for (i = 0; i < NS2_NUM_IOMUX; i++)
		pinctrl->mux_log[i].is_configured = false;
	/* Group 0 uses bit 31 in the IOMUX_PAD_FUNCTION_0 register */
	log = &pinctrl->mux_log[0];
	log->mux.base = NS2_PIN_MUX_BASE0;
	log->mux.offset = 0;
	log->mux.shift = 31;
	log->mux.alt = 0;

	/*
	 * Groups 1 through 14 use two bits each in the
	 * IOMUX_PAD_FUNCTION_1 register starting with
	 * bit position 30.
	 */
	for (i = 1; i < (NS2_NUM_IOMUX - NS2_NUM_PWM_MUX); i++) {
		log = &pinctrl->mux_log[i];
		log->mux.base = NS2_PIN_MUX_BASE0;
		log->mux.offset = NS2_MUX_PAD_FUNC1_OFFSET;
		log->mux.shift = 32 - (i * 2);
		log->mux.alt = 0;
	}

	/*
	 * Groups 15 through 18 use one bit each in the
	 * AUX_SEL register.
	 */
	for (i = 0; i < NS2_NUM_PWM_MUX; i++) {
		log = &pinctrl->mux_log[(NS2_NUM_IOMUX - NS2_NUM_PWM_MUX) + i];
		log->mux.base = NS2_PIN_MUX_BASE1;
		log->mux.offset = 0;
		log->mux.shift = i;
		log->mux.alt =  0;
	}
	return 0;
}

static int ns2_pinmux_probe(struct platform_device *pdev)
{
	struct ns2_pinctrl *pinctrl;
	struct resource *res;
	int i, ret;
	struct pinctrl_pin_desc *pins;
	unsigned int num_pins = ARRAY_SIZE(ns2_pins);

	pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;

	pinctrl->dev = &pdev->dev;
	platform_set_drvdata(pdev, pinctrl);
	spin_lock_init(&pinctrl->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pinctrl->base0 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pinctrl->base0))
		return PTR_ERR(pinctrl->base0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	pinctrl->base1 = devm_ioremap_nocache(&pdev->dev, res->start,
					resource_size(res));
	if (!pinctrl->base1) {
		dev_err(&pdev->dev, "unable to map I/O space\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	pinctrl->pinconf_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pinctrl->pinconf_base))
		return PTR_ERR(pinctrl->pinconf_base);

	ret = ns2_mux_log_init(pinctrl);
	if (ret) {
		dev_err(&pdev->dev, "unable to initialize IOMUX log\n");
		return ret;
	}

	pins = devm_kcalloc(&pdev->dev, num_pins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < num_pins; i++) {
		pins[i].number = ns2_pins[i].pin;
		pins[i].name = ns2_pins[i].name;
		pins[i].drv_data = &ns2_pins[i];
	}

	pinctrl->groups = ns2_pin_groups;
	pinctrl->num_groups = ARRAY_SIZE(ns2_pin_groups);
	pinctrl->functions = ns2_pin_functions;
	pinctrl->num_functions = ARRAY_SIZE(ns2_pin_functions);
	ns2_pinctrl_desc.pins = pins;
	ns2_pinctrl_desc.npins = num_pins;

	pinctrl->pctl = pinctrl_register(&ns2_pinctrl_desc, &pdev->dev,
			pinctrl);
	if (IS_ERR(pinctrl->pctl)) {
		dev_err(&pdev->dev, "unable to register IOMUX pinctrl\n");
		return PTR_ERR(pinctrl->pctl);
	}

	return 0;
}

static const struct of_device_id ns2_pinmux_of_match[] = {
	{.compatible = "brcm,ns2-pinmux"},
	{ }
};

static struct platform_driver ns2_pinmux_driver = {
	.driver = {
		.name = "ns2-pinmux",
		.of_match_table = ns2_pinmux_of_match,
	},
	.probe = ns2_pinmux_probe,
};

static int __init ns2_pinmux_init(void)
{
	return platform_driver_register(&ns2_pinmux_driver);
}
arch_initcall(ns2_pinmux_init);
