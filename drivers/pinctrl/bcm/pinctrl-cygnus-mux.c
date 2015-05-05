/* Copyright (C) 2014-2015 Broadcom Corporation
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
 * This file contains the Cygnus IOMUX driver that supports group based PINMUX
 * configuration. Although PINMUX configuration is mainly group based, the
 * Cygnus IOMUX controller allows certain pins to be individually muxed to GPIO
 * function, and therefore be controlled by the Cygnus ASIU GPIO controller
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include "../core.h"
#include "../pinctrl-utils.h"

#define CYGNUS_NUM_IOMUX_REGS     8
#define CYGNUS_NUM_MUX_PER_REG    8
#define CYGNUS_NUM_IOMUX          (CYGNUS_NUM_IOMUX_REGS * \
				   CYGNUS_NUM_MUX_PER_REG)

/*
 * Cygnus IOMUX register description
 *
 * @offset: register offset for mux configuration of a group
 * @shift: bit shift for mux configuration of a group
 * @alt: alternate function to set to
 */
struct cygnus_mux {
	unsigned int offset;
	unsigned int shift;
	unsigned int alt;
};

/*
 * Keep track of Cygnus IOMUX configuration and prevent double configuration
 *
 * @cygnus_mux: Cygnus IOMUX register description
 * @is_configured: flag to indicate whether a mux setting has already been
 * configured
 */
struct cygnus_mux_log {
	struct cygnus_mux mux;
	bool is_configured;
};

/*
 * Group based IOMUX configuration
 *
 * @name: name of the group
 * @pins: array of pins used by this group
 * @num_pins: total number of pins used by this group
 * @mux: Cygnus group based IOMUX configuration
 */
struct cygnus_pin_group {
	const char *name;
	const unsigned *pins;
	unsigned num_pins;
	struct cygnus_mux mux;
};

/*
 * Cygnus mux function and supported pin groups
 *
 * @name: name of the function
 * @groups: array of groups that can be supported by this function
 * @num_groups: total number of groups that can be supported by this function
 */
struct cygnus_pin_function {
	const char *name;
	const char * const *groups;
	unsigned num_groups;
};

/*
 * Cygnus IOMUX pinctrl core
 *
 * @pctl: pointer to pinctrl_dev
 * @dev: pointer to device
 * @base0: first I/O register base of the Cygnus IOMUX controller
 * @base1: second I/O register base
 * @groups: pointer to array of groups
 * @num_groups: total number of groups
 * @functions: pointer to array of functions
 * @num_functions: total number of functions
 * @mux_log: pointer to the array of mux logs
 * @lock: lock to protect register access
 */
struct cygnus_pinctrl {
	struct pinctrl_dev *pctl;
	struct device *dev;
	void __iomem *base0;
	void __iomem *base1;

	const struct cygnus_pin_group *groups;
	unsigned num_groups;

	const struct cygnus_pin_function *functions;
	unsigned num_functions;

	struct cygnus_mux_log *mux_log;

	spinlock_t lock;
};

/*
 * Certain pins can be individually muxed to GPIO function
 *
 * @is_supported: flag to indicate GPIO mux is supported for this pin
 * @offset: register offset for GPIO mux override of a pin
 * @shift: bit shift for GPIO mux override of a pin
 */
struct cygnus_gpio_mux {
	int is_supported;
	unsigned int offset;
	unsigned int shift;
};

/*
 * Description of a pin in Cygnus
 *
 * @pin: pin number
 * @name: pin name
 * @gpio_mux: GPIO override related information
 */
struct cygnus_pin {
	unsigned pin;
	char *name;
	struct cygnus_gpio_mux gpio_mux;
};

#define CYGNUS_PIN_DESC(p, n, i, o, s)	\
{					\
	.pin = p,			\
	.name = n,			\
	.gpio_mux = {			\
		.is_supported = i,	\
		.offset = o,		\
		.shift = s,		\
	},				\
}

/*
 * List of pins in Cygnus
 */
static struct cygnus_pin cygnus_pins[] = {
	CYGNUS_PIN_DESC(0, "ext_device_reset_n", 0, 0, 0),
	CYGNUS_PIN_DESC(1, "chip_mode0", 0, 0, 0),
	CYGNUS_PIN_DESC(2, "chip_mode1", 0, 0, 0),
	CYGNUS_PIN_DESC(3, "chip_mode2", 0, 0, 0),
	CYGNUS_PIN_DESC(4, "chip_mode3", 0, 0, 0),
	CYGNUS_PIN_DESC(5, "chip_mode4", 0, 0, 0),
	CYGNUS_PIN_DESC(6, "bsc0_scl", 0, 0, 0),
	CYGNUS_PIN_DESC(7, "bsc0_sda", 0, 0, 0),
	CYGNUS_PIN_DESC(8, "bsc1_scl", 0, 0, 0),
	CYGNUS_PIN_DESC(9, "bsc1_sda", 0, 0, 0),
	CYGNUS_PIN_DESC(10, "d1w_dq", 1, 0x28, 0),
	CYGNUS_PIN_DESC(11, "d1wowstz_l", 1, 0x4, 28),
	CYGNUS_PIN_DESC(12, "gpio0", 0, 0, 0),
	CYGNUS_PIN_DESC(13, "gpio1", 0, 0, 0),
	CYGNUS_PIN_DESC(14, "gpio2", 0, 0, 0),
	CYGNUS_PIN_DESC(15, "gpio3", 0, 0, 0),
	CYGNUS_PIN_DESC(16, "gpio4", 0, 0, 0),
	CYGNUS_PIN_DESC(17, "gpio5", 0, 0, 0),
	CYGNUS_PIN_DESC(18, "gpio6", 0, 0, 0),
	CYGNUS_PIN_DESC(19, "gpio7", 0, 0, 0),
	CYGNUS_PIN_DESC(20, "gpio8", 0, 0, 0),
	CYGNUS_PIN_DESC(21, "gpio9", 0, 0, 0),
	CYGNUS_PIN_DESC(22, "gpio10", 0, 0, 0),
	CYGNUS_PIN_DESC(23, "gpio11", 0, 0, 0),
	CYGNUS_PIN_DESC(24, "gpio12", 0, 0, 0),
	CYGNUS_PIN_DESC(25, "gpio13", 0, 0, 0),
	CYGNUS_PIN_DESC(26, "gpio14", 0, 0, 0),
	CYGNUS_PIN_DESC(27, "gpio15", 0, 0, 0),
	CYGNUS_PIN_DESC(28, "gpio16", 0, 0, 0),
	CYGNUS_PIN_DESC(29, "gpio17", 0, 0, 0),
	CYGNUS_PIN_DESC(30, "gpio18", 0, 0, 0),
	CYGNUS_PIN_DESC(31, "gpio19", 0, 0, 0),
	CYGNUS_PIN_DESC(32, "gpio20", 0, 0, 0),
	CYGNUS_PIN_DESC(33, "gpio21", 0, 0, 0),
	CYGNUS_PIN_DESC(34, "gpio22", 0, 0, 0),
	CYGNUS_PIN_DESC(35, "gpio23", 0, 0, 0),
	CYGNUS_PIN_DESC(36, "mdc", 0, 0, 0),
	CYGNUS_PIN_DESC(37, "mdio", 0, 0, 0),
	CYGNUS_PIN_DESC(38, "pwm0", 1, 0x10, 30),
	CYGNUS_PIN_DESC(39, "pwm1", 1, 0x10, 28),
	CYGNUS_PIN_DESC(40, "pwm2", 1, 0x10, 26),
	CYGNUS_PIN_DESC(41, "pwm3", 1, 0x10, 24),
	CYGNUS_PIN_DESC(42, "sc0_clk", 1, 0x10, 22),
	CYGNUS_PIN_DESC(43, "sc0_cmdvcc_l", 1, 0x10, 20),
	CYGNUS_PIN_DESC(44, "sc0_detect", 1, 0x10, 18),
	CYGNUS_PIN_DESC(45, "sc0_fcb", 1, 0x10, 16),
	CYGNUS_PIN_DESC(46, "sc0_io", 1, 0x10, 14),
	CYGNUS_PIN_DESC(47, "sc0_rst_l", 1, 0x10, 12),
	CYGNUS_PIN_DESC(48, "sc1_clk", 1, 0x10, 10),
	CYGNUS_PIN_DESC(49, "sc1_cmdvcc_l", 1, 0x10, 8),
	CYGNUS_PIN_DESC(50, "sc1_detect", 1, 0x10, 6),
	CYGNUS_PIN_DESC(51, "sc1_fcb", 1, 0x10, 4),
	CYGNUS_PIN_DESC(52, "sc1_io", 1, 0x10, 2),
	CYGNUS_PIN_DESC(53, "sc1_rst_l", 1, 0x10, 0),
	CYGNUS_PIN_DESC(54, "spi0_clk", 1, 0x18, 10),
	CYGNUS_PIN_DESC(55, "spi0_mosi", 1, 0x18, 6),
	CYGNUS_PIN_DESC(56, "spi0_miso", 1, 0x18, 8),
	CYGNUS_PIN_DESC(57, "spi0_ss", 1, 0x18, 4),
	CYGNUS_PIN_DESC(58, "spi1_clk", 1, 0x18, 2),
	CYGNUS_PIN_DESC(59, "spi1_mosi", 1, 0x1c, 30),
	CYGNUS_PIN_DESC(60, "spi1_miso", 1, 0x18, 0),
	CYGNUS_PIN_DESC(61, "spi1_ss", 1, 0x1c, 28),
	CYGNUS_PIN_DESC(62, "spi2_clk", 1, 0x1c, 26),
	CYGNUS_PIN_DESC(63, "spi2_mosi", 1, 0x1c, 22),
	CYGNUS_PIN_DESC(64, "spi2_miso", 1, 0x1c, 24),
	CYGNUS_PIN_DESC(65, "spi2_ss", 1, 0x1c, 20),
	CYGNUS_PIN_DESC(66, "spi3_clk", 1, 0x1c, 18),
	CYGNUS_PIN_DESC(67, "spi3_mosi", 1, 0x1c, 14),
	CYGNUS_PIN_DESC(68, "spi3_miso", 1, 0x1c, 16),
	CYGNUS_PIN_DESC(69, "spi3_ss", 1, 0x1c, 12),
	CYGNUS_PIN_DESC(70, "uart0_cts", 1, 0x1c, 10),
	CYGNUS_PIN_DESC(71, "uart0_rts", 1, 0x1c, 8),
	CYGNUS_PIN_DESC(72, "uart0_rx", 1, 0x1c, 6),
	CYGNUS_PIN_DESC(73, "uart0_tx", 1, 0x1c, 4),
	CYGNUS_PIN_DESC(74, "uart1_cts", 1, 0x1c, 2),
	CYGNUS_PIN_DESC(75, "uart1_dcd", 1, 0x1c, 0),
	CYGNUS_PIN_DESC(76, "uart1_dsr", 1, 0x20, 14),
	CYGNUS_PIN_DESC(77, "uart1_dtr", 1, 0x20, 12),
	CYGNUS_PIN_DESC(78, "uart1_ri", 1, 0x20, 10),
	CYGNUS_PIN_DESC(79, "uart1_rts", 1, 0x20, 8),
	CYGNUS_PIN_DESC(80, "uart1_rx", 1, 0x20, 6),
	CYGNUS_PIN_DESC(81, "uart1_tx", 1, 0x20, 4),
	CYGNUS_PIN_DESC(82, "uart3_rx", 1, 0x20, 2),
	CYGNUS_PIN_DESC(83, "uart3_tx", 1, 0x20, 0),
	CYGNUS_PIN_DESC(84, "sdio1_clk_sdcard", 1, 0x14, 6),
	CYGNUS_PIN_DESC(85, "sdio1_cmd", 1, 0x14, 4),
	CYGNUS_PIN_DESC(86, "sdio1_data0", 1, 0x14, 2),
	CYGNUS_PIN_DESC(87, "sdio1_data1", 1, 0x14, 0),
	CYGNUS_PIN_DESC(88, "sdio1_data2", 1, 0x18, 30),
	CYGNUS_PIN_DESC(89, "sdio1_data3", 1, 0x18, 28),
	CYGNUS_PIN_DESC(90, "sdio1_wp_n", 1, 0x18, 24),
	CYGNUS_PIN_DESC(91, "sdio1_card_rst", 1, 0x14, 10),
	CYGNUS_PIN_DESC(92, "sdio1_led_on", 1, 0x18, 26),
	CYGNUS_PIN_DESC(93, "sdio1_cd", 1, 0x14, 8),
	CYGNUS_PIN_DESC(94, "sdio0_clk_sdcard", 1, 0x14, 26),
	CYGNUS_PIN_DESC(95, "sdio0_cmd", 1, 0x14, 24),
	CYGNUS_PIN_DESC(96, "sdio0_data0", 1, 0x14, 22),
	CYGNUS_PIN_DESC(97, "sdio0_data1", 1, 0x14, 20),
	CYGNUS_PIN_DESC(98, "sdio0_data2", 1, 0x14, 18),
	CYGNUS_PIN_DESC(99, "sdio0_data3", 1, 0x14, 16),
	CYGNUS_PIN_DESC(100, "sdio0_wp_n", 1, 0x14, 12),
	CYGNUS_PIN_DESC(101, "sdio0_card_rst", 1, 0x14, 30),
	CYGNUS_PIN_DESC(102, "sdio0_led_on", 1, 0x14, 14),
	CYGNUS_PIN_DESC(103, "sdio0_cd", 1, 0x14, 28),
	CYGNUS_PIN_DESC(104, "sflash_clk", 1, 0x18, 22),
	CYGNUS_PIN_DESC(105, "sflash_cs_l", 1, 0x18, 20),
	CYGNUS_PIN_DESC(106, "sflash_mosi", 1, 0x18, 14),
	CYGNUS_PIN_DESC(107, "sflash_miso", 1, 0x18, 16),
	CYGNUS_PIN_DESC(108, "sflash_wp_n", 1, 0x18, 12),
	CYGNUS_PIN_DESC(109, "sflash_hold_n", 1, 0x18, 18),
	CYGNUS_PIN_DESC(110, "nand_ale", 1, 0xc, 30),
	CYGNUS_PIN_DESC(111, "nand_ce0_l", 1, 0xc, 28),
	CYGNUS_PIN_DESC(112, "nand_ce1_l", 1, 0xc, 26),
	CYGNUS_PIN_DESC(113, "nand_cle", 1, 0xc, 24),
	CYGNUS_PIN_DESC(114, "nand_dq0", 1, 0xc, 22),
	CYGNUS_PIN_DESC(115, "nand_dq1", 1, 0xc, 20),
	CYGNUS_PIN_DESC(116, "nand_dq2", 1, 0xc, 18),
	CYGNUS_PIN_DESC(117, "nand_dq3", 1, 0xc, 16),
	CYGNUS_PIN_DESC(118, "nand_dq4", 1, 0xc, 14),
	CYGNUS_PIN_DESC(119, "nand_dq5", 1, 0xc, 12),
	CYGNUS_PIN_DESC(120, "nand_dq6", 1, 0xc, 10),
	CYGNUS_PIN_DESC(121, "nand_dq7", 1, 0xc, 8),
	CYGNUS_PIN_DESC(122, "nand_rb_l", 1, 0xc, 6),
	CYGNUS_PIN_DESC(123, "nand_re_l", 1, 0xc, 4),
	CYGNUS_PIN_DESC(124, "nand_we_l", 1, 0xc, 2),
	CYGNUS_PIN_DESC(125, "nand_wp_l", 1, 0xc, 0),
	CYGNUS_PIN_DESC(126, "lcd_clac", 1, 0x4, 26),
	CYGNUS_PIN_DESC(127, "lcd_clcp", 1, 0x4, 24),
	CYGNUS_PIN_DESC(128, "lcd_cld0", 1, 0x4, 22),
	CYGNUS_PIN_DESC(129, "lcd_cld1", 1, 0x4, 0),
	CYGNUS_PIN_DESC(130, "lcd_cld10", 1, 0x4, 20),
	CYGNUS_PIN_DESC(131, "lcd_cld11", 1, 0x4, 18),
	CYGNUS_PIN_DESC(132, "lcd_cld12", 1, 0x4, 16),
	CYGNUS_PIN_DESC(133, "lcd_cld13", 1, 0x4, 14),
	CYGNUS_PIN_DESC(134, "lcd_cld14", 1, 0x4, 12),
	CYGNUS_PIN_DESC(135, "lcd_cld15", 1, 0x4, 10),
	CYGNUS_PIN_DESC(136, "lcd_cld16", 1, 0x4, 8),
	CYGNUS_PIN_DESC(137, "lcd_cld17", 1, 0x4, 6),
	CYGNUS_PIN_DESC(138, "lcd_cld18", 1, 0x4, 4),
	CYGNUS_PIN_DESC(139, "lcd_cld19", 1, 0x4, 2),
	CYGNUS_PIN_DESC(140, "lcd_cld2", 1, 0x8, 22),
	CYGNUS_PIN_DESC(141, "lcd_cld20", 1, 0x8, 30),
	CYGNUS_PIN_DESC(142, "lcd_cld21", 1, 0x8, 28),
	CYGNUS_PIN_DESC(143, "lcd_cld22", 1, 0x8, 26),
	CYGNUS_PIN_DESC(144, "lcd_cld23", 1, 0x8, 24),
	CYGNUS_PIN_DESC(145, "lcd_cld3", 1, 0x8, 20),
	CYGNUS_PIN_DESC(146, "lcd_cld4", 1, 0x8, 18),
	CYGNUS_PIN_DESC(147, "lcd_cld5", 1, 0x8, 16),
	CYGNUS_PIN_DESC(148, "lcd_cld6", 1, 0x8, 14),
	CYGNUS_PIN_DESC(149, "lcd_cld7", 1, 0x8, 12),
	CYGNUS_PIN_DESC(150, "lcd_cld8", 1, 0x8, 10),
	CYGNUS_PIN_DESC(151, "lcd_cld9", 1, 0x8, 8),
	CYGNUS_PIN_DESC(152, "lcd_clfp", 1, 0x8, 6),
	CYGNUS_PIN_DESC(153, "lcd_clle", 1, 0x8, 4),
	CYGNUS_PIN_DESC(154, "lcd_cllp", 1, 0x8, 2),
	CYGNUS_PIN_DESC(155, "lcd_clpower", 1, 0x8, 0),
	CYGNUS_PIN_DESC(156, "camera_vsync", 1, 0x4, 30),
	CYGNUS_PIN_DESC(157, "camera_trigger", 1, 0x0, 0),
	CYGNUS_PIN_DESC(158, "camera_strobe", 1, 0x0, 2),
	CYGNUS_PIN_DESC(159, "camera_standby", 1, 0x0, 4),
	CYGNUS_PIN_DESC(160, "camera_reset_n", 1, 0x0, 6),
	CYGNUS_PIN_DESC(161, "camera_pixdata9", 1, 0x0, 8),
	CYGNUS_PIN_DESC(162, "camera_pixdata8", 1, 0x0, 10),
	CYGNUS_PIN_DESC(163, "camera_pixdata7", 1, 0x0, 12),
	CYGNUS_PIN_DESC(164, "camera_pixdata6", 1, 0x0, 14),
	CYGNUS_PIN_DESC(165, "camera_pixdata5", 1, 0x0, 16),
	CYGNUS_PIN_DESC(166, "camera_pixdata4", 1, 0x0, 18),
	CYGNUS_PIN_DESC(167, "camera_pixdata3", 1, 0x0, 20),
	CYGNUS_PIN_DESC(168, "camera_pixdata2", 1, 0x0, 22),
	CYGNUS_PIN_DESC(169, "camera_pixdata1", 1, 0x0, 24),
	CYGNUS_PIN_DESC(170, "camera_pixdata0", 1, 0x0, 26),
	CYGNUS_PIN_DESC(171, "camera_pixclk", 1, 0x0, 28),
	CYGNUS_PIN_DESC(172, "camera_hsync", 1, 0x0, 30),
	CYGNUS_PIN_DESC(173, "camera_pll_ref_clk", 0, 0, 0),
	CYGNUS_PIN_DESC(174, "usb_id_indication", 0, 0, 0),
	CYGNUS_PIN_DESC(175, "usb_vbus_indication", 0, 0, 0),
	CYGNUS_PIN_DESC(176, "gpio0_3p3", 0, 0, 0),
	CYGNUS_PIN_DESC(177, "gpio1_3p3", 0, 0, 0),
	CYGNUS_PIN_DESC(178, "gpio2_3p3", 0, 0, 0),
	CYGNUS_PIN_DESC(179, "gpio3_3p3", 0, 0, 0),
};

/*
 * List of groups of pins
 */
static const unsigned bsc1_pins[] = { 8, 9 };
static const unsigned pcie_clkreq_pins[] = { 8, 9 };

static const unsigned i2s2_0_pins[] = { 12 };
static const unsigned i2s2_1_pins[] = { 13 };
static const unsigned i2s2_2_pins[] = { 14 };
static const unsigned i2s2_3_pins[] = { 15 };
static const unsigned i2s2_4_pins[] = { 16 };

static const unsigned pwm4_pins[] = { 17 };
static const unsigned pwm5_pins[] = { 18 };

static const unsigned key0_pins[] = { 20 };
static const unsigned key1_pins[] = { 21 };
static const unsigned key2_pins[] = { 22 };
static const unsigned key3_pins[] = { 23 };
static const unsigned key4_pins[] = { 24 };
static const unsigned key5_pins[] = { 25 };

static const unsigned key6_pins[] = { 26 };
static const unsigned audio_dte0_pins[] = { 26 };

static const unsigned key7_pins[] = { 27 };
static const unsigned audio_dte1_pins[] = { 27 };

static const unsigned key8_pins[] = { 28 };
static const unsigned key9_pins[] = { 29 };
static const unsigned key10_pins[] = { 30 };
static const unsigned key11_pins[] = { 31 };
static const unsigned key12_pins[] = { 32 };
static const unsigned key13_pins[] = { 33 };

static const unsigned key14_pins[] = { 34 };
static const unsigned audio_dte2_pins[] = { 34 };

static const unsigned key15_pins[] = { 35 };
static const unsigned audio_dte3_pins[] = { 35 };

static const unsigned pwm0_pins[] = { 38 };
static const unsigned pwm1_pins[] = { 39 };
static const unsigned pwm2_pins[] = { 40 };
static const unsigned pwm3_pins[] = { 41 };

static const unsigned sdio0_pins[] = { 94, 95, 96, 97, 98, 99 };

static const unsigned smart_card0_pins[] = { 42, 43, 44, 46, 47 };
static const unsigned i2s0_0_pins[] = { 42, 43, 44, 46 };
static const unsigned spdif_pins[] = { 47 };

static const unsigned smart_card1_pins[] = { 48, 49, 50, 52, 53 };
static const unsigned i2s1_0_pins[] = { 48, 49, 50, 52 };

static const unsigned spi0_pins[] = { 54, 55, 56, 57 };

static const unsigned spi1_pins[] = { 58, 59, 60, 61 };

static const unsigned spi2_pins[] = { 62, 63, 64, 65 };

static const unsigned spi3_pins[] = { 66, 67, 68, 69 };
static const unsigned sw_led0_0_pins[] = { 66, 67, 68, 69 };

static const unsigned d1w_pins[] = { 10, 11 };
static const unsigned uart4_pins[] = { 10, 11 };
static const unsigned sw_led2_0_pins[] = { 10, 11 };

static const unsigned lcd_pins[] = { 126, 127, 128, 129, 130, 131, 132, 133,
	134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147,
	148, 149, 150, 151, 152, 153, 154, 155 };
static const unsigned sram_0_pins[] = { 126, 127, 128, 129, 130, 131, 132, 133,
	134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147,
	148, 149, 150, 151, 152, 153, 154, 155 };
static const unsigned spi5_pins[] = { 141, 142, 143, 144 };

static const unsigned uart0_pins[] = { 70, 71, 72, 73 };
static const unsigned sw_led0_1_pins[] = { 70, 71, 72, 73 };

static const unsigned uart1_dte_pins[] = { 75, 76, 77, 78 };
static const unsigned uart2_pins[] = { 75, 76, 77, 78 };

static const unsigned uart1_pins[] = { 74, 79, 80, 81 };

static const unsigned uart3_pins[] = { 82, 83 };

static const unsigned qspi_0_pins[] = { 104, 105, 106, 107 };

static const unsigned nand_pins[] = { 110, 111, 112, 113, 114, 115, 116, 117,
	118, 119, 120, 121, 122, 123, 124, 125 };

static const unsigned sdio0_cd_pins[] = { 103 };

static const unsigned sdio0_mmc_pins[] = { 100, 101, 102 };

static const unsigned sdio1_data_0_pins[] = { 86, 87 };
static const unsigned can0_pins[] = { 86, 87 };
static const unsigned spi4_0_pins[] = { 86, 87 };

static const unsigned sdio1_data_1_pins[] = { 88, 89 };
static const unsigned can1_pins[] = { 88, 89 };
static const unsigned spi4_1_pins[] = { 88, 89 };

static const unsigned sdio1_cd_pins[] = { 93 };

static const unsigned sdio1_led_pins[] = { 84, 85 };
static const unsigned sw_led2_1_pins[] = { 84, 85 };

static const unsigned sdio1_mmc_pins[] = { 90, 91, 92 };

static const unsigned cam_led_pins[] = { 156, 157, 158, 159, 160 };
static const unsigned sw_led1_pins[] = { 156, 157, 158, 159 };

static const unsigned cam_0_pins[] = { 169, 170, 171, 169, 170 };

static const unsigned cam_1_pins[] = { 161, 162, 163, 164, 165, 166, 167,
	168 };
static const unsigned sram_1_pins[] = { 161, 162, 163, 164, 165, 166, 167,
	168 };

static const unsigned qspi_1_pins[] = { 108, 109 };

static const unsigned smart_card0_fcb_pins[] = { 45 };
static const unsigned i2s0_1_pins[] = { 45 };

static const unsigned smart_card1_fcb_pins[] = { 51 };
static const unsigned i2s1_1_pins[] = { 51 };

static const unsigned gpio0_3p3_pins[] = { 176 };
static const unsigned usb0_oc_pins[] = { 176 };

static const unsigned gpio1_3p3_pins[] = { 177 };
static const unsigned usb1_oc_pins[] = { 177 };

static const unsigned gpio2_3p3_pins[] = { 178 };
static const unsigned usb2_oc_pins[] = { 178 };

#define CYGNUS_PIN_GROUP(group_name, off, sh, al)	\
{							\
	.name = __stringify(group_name) "_grp",		\
	.pins = group_name ## _pins,			\
	.num_pins = ARRAY_SIZE(group_name ## _pins),	\
	.mux = {					\
		.offset = off,				\
		.shift = sh,				\
		.alt = al,				\
	}						\
}

/*
 * List of Cygnus pin groups
 */
static const struct cygnus_pin_group cygnus_pin_groups[] = {
	CYGNUS_PIN_GROUP(i2s2_0, 0x0, 0, 2),
	CYGNUS_PIN_GROUP(i2s2_1, 0x0, 4, 2),
	CYGNUS_PIN_GROUP(i2s2_2, 0x0, 8, 2),
	CYGNUS_PIN_GROUP(i2s2_3, 0x0, 12, 2),
	CYGNUS_PIN_GROUP(i2s2_4, 0x0, 16, 2),
	CYGNUS_PIN_GROUP(pwm4, 0x0, 20, 0),
	CYGNUS_PIN_GROUP(pwm5, 0x0, 24, 2),
	CYGNUS_PIN_GROUP(key0, 0x4, 0, 1),
	CYGNUS_PIN_GROUP(key1, 0x4, 4, 1),
	CYGNUS_PIN_GROUP(key2, 0x4, 8, 1),
	CYGNUS_PIN_GROUP(key3, 0x4, 12, 1),
	CYGNUS_PIN_GROUP(key4, 0x4, 16, 1),
	CYGNUS_PIN_GROUP(key5, 0x4, 20, 1),
	CYGNUS_PIN_GROUP(key6, 0x4, 24, 1),
	CYGNUS_PIN_GROUP(audio_dte0, 0x4, 24, 2),
	CYGNUS_PIN_GROUP(key7, 0x4, 28, 1),
	CYGNUS_PIN_GROUP(audio_dte1, 0x4, 28, 2),
	CYGNUS_PIN_GROUP(key8, 0x8, 0, 1),
	CYGNUS_PIN_GROUP(key9, 0x8, 4, 1),
	CYGNUS_PIN_GROUP(key10, 0x8, 8, 1),
	CYGNUS_PIN_GROUP(key11, 0x8, 12, 1),
	CYGNUS_PIN_GROUP(key12, 0x8, 16, 1),
	CYGNUS_PIN_GROUP(key13, 0x8, 20, 1),
	CYGNUS_PIN_GROUP(key14, 0x8, 24, 1),
	CYGNUS_PIN_GROUP(audio_dte2, 0x8, 24, 2),
	CYGNUS_PIN_GROUP(key15, 0x8, 28, 1),
	CYGNUS_PIN_GROUP(audio_dte3, 0x8, 28, 2),
	CYGNUS_PIN_GROUP(pwm0, 0xc, 0, 0),
	CYGNUS_PIN_GROUP(pwm1, 0xc, 4, 0),
	CYGNUS_PIN_GROUP(pwm2, 0xc, 8, 0),
	CYGNUS_PIN_GROUP(pwm3, 0xc, 12, 0),
	CYGNUS_PIN_GROUP(sdio0, 0xc, 16, 0),
	CYGNUS_PIN_GROUP(smart_card0, 0xc, 20, 0),
	CYGNUS_PIN_GROUP(i2s0_0, 0xc, 20, 1),
	CYGNUS_PIN_GROUP(spdif, 0xc, 20, 1),
	CYGNUS_PIN_GROUP(smart_card1, 0xc, 24, 0),
	CYGNUS_PIN_GROUP(i2s1_0, 0xc, 24, 1),
	CYGNUS_PIN_GROUP(spi0, 0x10, 0, 0),
	CYGNUS_PIN_GROUP(spi1, 0x10, 4, 0),
	CYGNUS_PIN_GROUP(spi2, 0x10, 8, 0),
	CYGNUS_PIN_GROUP(spi3, 0x10, 12, 0),
	CYGNUS_PIN_GROUP(sw_led0_0, 0x10, 12, 2),
	CYGNUS_PIN_GROUP(d1w, 0x10, 16, 0),
	CYGNUS_PIN_GROUP(uart4, 0x10, 16, 1),
	CYGNUS_PIN_GROUP(sw_led2_0, 0x10, 16, 2),
	CYGNUS_PIN_GROUP(lcd, 0x10, 20, 0),
	CYGNUS_PIN_GROUP(sram_0, 0x10, 20, 1),
	CYGNUS_PIN_GROUP(spi5, 0x10, 20, 2),
	CYGNUS_PIN_GROUP(uart0, 0x14, 0, 0),
	CYGNUS_PIN_GROUP(sw_led0_1, 0x14, 0, 2),
	CYGNUS_PIN_GROUP(uart1_dte, 0x14, 4, 0),
	CYGNUS_PIN_GROUP(uart2, 0x14, 4, 1),
	CYGNUS_PIN_GROUP(uart1, 0x14, 8, 0),
	CYGNUS_PIN_GROUP(uart3, 0x14, 12, 0),
	CYGNUS_PIN_GROUP(qspi_0, 0x14, 16, 0),
	CYGNUS_PIN_GROUP(nand, 0x14, 20, 0),
	CYGNUS_PIN_GROUP(sdio0_cd, 0x18, 0, 0),
	CYGNUS_PIN_GROUP(sdio0_mmc, 0x18, 4, 0),
	CYGNUS_PIN_GROUP(sdio1_data_0, 0x18, 8, 0),
	CYGNUS_PIN_GROUP(can0, 0x18, 8, 1),
	CYGNUS_PIN_GROUP(spi4_0, 0x18, 8, 2),
	CYGNUS_PIN_GROUP(sdio1_data_1, 0x18, 12, 0),
	CYGNUS_PIN_GROUP(can1, 0x18, 12, 1),
	CYGNUS_PIN_GROUP(spi4_1, 0x18, 12, 2),
	CYGNUS_PIN_GROUP(sdio1_cd, 0x18, 16, 0),
	CYGNUS_PIN_GROUP(sdio1_led, 0x18, 20, 0),
	CYGNUS_PIN_GROUP(sw_led2_1, 0x18, 20, 2),
	CYGNUS_PIN_GROUP(sdio1_mmc, 0x18, 24, 0),
	CYGNUS_PIN_GROUP(cam_led, 0x1c, 0, 0),
	CYGNUS_PIN_GROUP(sw_led1, 0x1c, 0, 1),
	CYGNUS_PIN_GROUP(cam_0, 0x1c, 4, 0),
	CYGNUS_PIN_GROUP(cam_1, 0x1c, 8, 0),
	CYGNUS_PIN_GROUP(sram_1, 0x1c, 8, 1),
	CYGNUS_PIN_GROUP(qspi_1, 0x1c, 12, 0),
	CYGNUS_PIN_GROUP(bsc1, 0x1c, 16, 0),
	CYGNUS_PIN_GROUP(pcie_clkreq, 0x1c, 16, 1),
	CYGNUS_PIN_GROUP(smart_card0_fcb, 0x20, 0, 0),
	CYGNUS_PIN_GROUP(i2s0_1, 0x20, 0, 1),
	CYGNUS_PIN_GROUP(smart_card1_fcb, 0x20, 4, 0),
	CYGNUS_PIN_GROUP(i2s1_1, 0x20, 4, 1),
	CYGNUS_PIN_GROUP(gpio0_3p3, 0x28, 0, 0),
	CYGNUS_PIN_GROUP(usb0_oc, 0x28, 0, 1),
	CYGNUS_PIN_GROUP(gpio1_3p3, 0x28, 4, 0),
	CYGNUS_PIN_GROUP(usb1_oc, 0x28, 4, 1),
	CYGNUS_PIN_GROUP(gpio2_3p3, 0x28, 8, 0),
	CYGNUS_PIN_GROUP(usb2_oc, 0x28, 8, 1),
};

/*
 * List of groups supported by functions
 */
static const char * const i2s0_grps[] = { "i2s0_0_grp", "i2s0_1_grp" };
static const char * const i2s1_grps[] = { "i2s1_0_grp", "i2s1_1_grp" };
static const char * const i2s2_grps[] = { "i2s2_0_grp", "i2s2_1_grp",
	"i2s2_2_grp", "i2s2_3_grp", "i2s2_4_grp" };
static const char * const spdif_grps[] = { "spdif_grp" };
static const char * const pwm0_grps[] = { "pwm0_grp" };
static const char * const pwm1_grps[] = { "pwm1_grp" };
static const char * const pwm2_grps[] = { "pwm2_grp" };
static const char * const pwm3_grps[] = { "pwm3_grp" };
static const char * const pwm4_grps[] = { "pwm4_grp" };
static const char * const pwm5_grps[] = { "pwm5_grp" };
static const char * const key_grps[] = { "key0_grp", "key1_grp", "key2_grp",
	"key3_grp", "key4_grp", "key5_grp", "key6_grp", "key7_grp", "key8_grp",
	"key9_grp", "key10_grp", "key11_grp", "key12_grp", "key13_grp",
	"key14_grp", "key15_grp" };
static const char * const audio_dte_grps[] = { "audio_dte0_grp",
	"audio_dte1_grp", "audio_dte2_grp", "audio_dte3_grp" };
static const char * const smart_card0_grps[] = { "smart_card0_grp",
	"smart_card0_fcb_grp" };
static const char * const smart_card1_grps[] = { "smart_card1_grp",
	"smart_card1_fcb_grp" };
static const char * const spi0_grps[] = { "spi0_grp" };
static const char * const spi1_grps[] = { "spi1_grp" };
static const char * const spi2_grps[] = { "spi2_grp" };
static const char * const spi3_grps[] = { "spi3_grp" };
static const char * const spi4_grps[] = { "spi4_0_grp", "spi4_1_grp" };
static const char * const spi5_grps[] = { "spi5_grp" };

static const char * const sw_led0_grps[] = { "sw_led0_0_grp",
	"sw_led0_1_grp" };
static const char * const sw_led1_grps[] = { "sw_led1_grp" };
static const char * const sw_led2_grps[] = { "sw_led2_0_grp",
	"sw_led2_1_grp" };
static const char * const d1w_grps[] = { "d1w_grp" };
static const char * const lcd_grps[] = { "lcd_grp" };
static const char * const sram_grps[] = { "sram_0_grp", "sram_1_grp" };

static const char * const uart0_grps[] = { "uart0_grp" };
static const char * const uart1_grps[] = { "uart1_grp", "uart1_dte_grp" };
static const char * const uart2_grps[] = { "uart2_grp" };
static const char * const uart3_grps[] = { "uart3_grp" };
static const char * const uart4_grps[] = { "uart4_grp" };
static const char * const qspi_grps[] = { "qspi_0_grp", "qspi_1_grp" };
static const char * const nand_grps[] = { "nand_grp" };
static const char * const sdio0_grps[] = { "sdio0_grp", "sdio0_cd_grp",
	"sdio0_mmc_grp" };
static const char * const sdio1_grps[] = { "sdio1_data_0_grp",
	"sdio1_data_1_grp", "sdio1_cd_grp", "sdio1_led_grp", "sdio1_mmc_grp" };
static const char * const can0_grps[] = { "can0_grp" };
static const char * const can1_grps[] = { "can1_grp" };
static const char * const cam_grps[] = { "cam_led_grp", "cam_0_grp",
	"cam_1_grp" };
static const char * const bsc1_grps[] = { "bsc1_grp" };
static const char * const pcie_clkreq_grps[] = { "pcie_clkreq_grp" };
static const char * const usb0_oc_grps[] = { "usb0_oc_grp" };
static const char * const usb1_oc_grps[] = { "usb1_oc_grp" };
static const char * const usb2_oc_grps[] = { "usb2_oc_grp" };

#define CYGNUS_PIN_FUNCTION(func)				\
{								\
	.name = #func,						\
	.groups = func ## _grps,				\
	.num_groups = ARRAY_SIZE(func ## _grps),		\
}

/*
 * List of supported functions in Cygnus
 */
static const struct cygnus_pin_function cygnus_pin_functions[] = {
	CYGNUS_PIN_FUNCTION(i2s0),
	CYGNUS_PIN_FUNCTION(i2s1),
	CYGNUS_PIN_FUNCTION(i2s2),
	CYGNUS_PIN_FUNCTION(spdif),
	CYGNUS_PIN_FUNCTION(pwm0),
	CYGNUS_PIN_FUNCTION(pwm1),
	CYGNUS_PIN_FUNCTION(pwm2),
	CYGNUS_PIN_FUNCTION(pwm3),
	CYGNUS_PIN_FUNCTION(pwm4),
	CYGNUS_PIN_FUNCTION(pwm5),
	CYGNUS_PIN_FUNCTION(key),
	CYGNUS_PIN_FUNCTION(audio_dte),
	CYGNUS_PIN_FUNCTION(smart_card0),
	CYGNUS_PIN_FUNCTION(smart_card1),
	CYGNUS_PIN_FUNCTION(spi0),
	CYGNUS_PIN_FUNCTION(spi1),
	CYGNUS_PIN_FUNCTION(spi2),
	CYGNUS_PIN_FUNCTION(spi3),
	CYGNUS_PIN_FUNCTION(spi4),
	CYGNUS_PIN_FUNCTION(spi5),
	CYGNUS_PIN_FUNCTION(sw_led0),
	CYGNUS_PIN_FUNCTION(sw_led1),
	CYGNUS_PIN_FUNCTION(sw_led2),
	CYGNUS_PIN_FUNCTION(d1w),
	CYGNUS_PIN_FUNCTION(lcd),
	CYGNUS_PIN_FUNCTION(sram),
	CYGNUS_PIN_FUNCTION(uart0),
	CYGNUS_PIN_FUNCTION(uart1),
	CYGNUS_PIN_FUNCTION(uart2),
	CYGNUS_PIN_FUNCTION(uart3),
	CYGNUS_PIN_FUNCTION(uart4),
	CYGNUS_PIN_FUNCTION(qspi),
	CYGNUS_PIN_FUNCTION(nand),
	CYGNUS_PIN_FUNCTION(sdio0),
	CYGNUS_PIN_FUNCTION(sdio1),
	CYGNUS_PIN_FUNCTION(can0),
	CYGNUS_PIN_FUNCTION(can1),
	CYGNUS_PIN_FUNCTION(cam),
	CYGNUS_PIN_FUNCTION(bsc1),
	CYGNUS_PIN_FUNCTION(pcie_clkreq),
	CYGNUS_PIN_FUNCTION(usb0_oc),
	CYGNUS_PIN_FUNCTION(usb1_oc),
	CYGNUS_PIN_FUNCTION(usb2_oc),
};

static int cygnus_get_groups_count(struct pinctrl_dev *pctrl_dev)
{
	struct cygnus_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->num_groups;
}

static const char *cygnus_get_group_name(struct pinctrl_dev *pctrl_dev,
					 unsigned selector)
{
	struct cygnus_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->groups[selector].name;
}

static int cygnus_get_group_pins(struct pinctrl_dev *pctrl_dev,
				 unsigned selector, const unsigned **pins,
				 unsigned *num_pins)
{
	struct cygnus_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	*pins = pinctrl->groups[selector].pins;
	*num_pins = pinctrl->groups[selector].num_pins;

	return 0;
}

static void cygnus_pin_dbg_show(struct pinctrl_dev *pctrl_dev,
				struct seq_file *s, unsigned offset)
{
	seq_printf(s, " %s", dev_name(pctrl_dev->dev));
}

static const struct pinctrl_ops cygnus_pinctrl_ops = {
	.get_groups_count = cygnus_get_groups_count,
	.get_group_name = cygnus_get_group_name,
	.get_group_pins = cygnus_get_group_pins,
	.pin_dbg_show = cygnus_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_group,
	.dt_free_map = pinctrl_utils_dt_free_map,
};

static int cygnus_get_functions_count(struct pinctrl_dev *pctrl_dev)
{
	struct cygnus_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->num_functions;
}

static const char *cygnus_get_function_name(struct pinctrl_dev *pctrl_dev,
					    unsigned selector)
{
	struct cygnus_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->functions[selector].name;
}

static int cygnus_get_function_groups(struct pinctrl_dev *pctrl_dev,
				      unsigned selector,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	struct cygnus_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	*groups = pinctrl->functions[selector].groups;
	*num_groups = pinctrl->functions[selector].num_groups;

	return 0;
}

static int cygnus_pinmux_set(struct cygnus_pinctrl *pinctrl,
			     const struct cygnus_pin_function *func,
			     const struct cygnus_pin_group *grp,
			     struct cygnus_mux_log *mux_log)
{
	const struct cygnus_mux *mux = &grp->mux;
	int i;
	u32 val, mask = 0x7;
	unsigned long flags;

	for (i = 0; i < CYGNUS_NUM_IOMUX; i++) {
		if (mux->offset != mux_log[i].mux.offset ||
		    mux->shift != mux_log[i].mux.shift)
			continue;

		/* match found if we reach here */

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
		} else {
			/*
			 * One tries to configure it to the same function.
			 * Just quit and don't bother
			 */
			return 0;
		}
	}

	mux_log[i].mux.alt = mux->alt;
	mux_log[i].is_configured = true;

	spin_lock_irqsave(&pinctrl->lock, flags);

	val = readl(pinctrl->base0 + grp->mux.offset);
	val &= ~(mask << grp->mux.shift);
	val |= grp->mux.alt << grp->mux.shift;
	writel(val, pinctrl->base0 + grp->mux.offset);

	spin_unlock_irqrestore(&pinctrl->lock, flags);

	return 0;
}

static int cygnus_pinmux_set_mux(struct pinctrl_dev *pctrl_dev,
				 unsigned func_select, unsigned grp_select)
{
	struct cygnus_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	const struct cygnus_pin_function *func =
		&pinctrl->functions[func_select];
	const struct cygnus_pin_group *grp = &pinctrl->groups[grp_select];

	dev_dbg(pctrl_dev->dev, "func:%u name:%s grp:%u name:%s\n",
		func_select, func->name, grp_select, grp->name);

	dev_dbg(pctrl_dev->dev, "offset:0x%08x shift:%u alt:%u\n",
		grp->mux.offset, grp->mux.shift, grp->mux.alt);

	return cygnus_pinmux_set(pinctrl, func, grp, pinctrl->mux_log);
}

static int cygnus_gpio_request_enable(struct pinctrl_dev *pctrl_dev,
				      struct pinctrl_gpio_range *range,
				      unsigned pin)
{
	struct cygnus_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	const struct cygnus_gpio_mux *mux = pctrl_dev->desc->pins[pin].drv_data;
	u32 val;
	unsigned long flags;

	/* not all pins support GPIO pinmux override */
	if (!mux->is_supported)
		return -ENOTSUPP;

	spin_lock_irqsave(&pinctrl->lock, flags);

	val = readl(pinctrl->base1 + mux->offset);
	val |= 0x3 << mux->shift;
	writel(val, pinctrl->base1 + mux->offset);

	spin_unlock_irqrestore(&pinctrl->lock, flags);

	dev_dbg(pctrl_dev->dev,
		"gpio request enable pin=%u offset=0x%x shift=%u\n",
		pin, mux->offset, mux->shift);

	return 0;
}

static void cygnus_gpio_disable_free(struct pinctrl_dev *pctrl_dev,
				     struct pinctrl_gpio_range *range,
				     unsigned pin)
{
	struct cygnus_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	struct cygnus_gpio_mux *mux = pctrl_dev->desc->pins[pin].drv_data;
	u32 val;
	unsigned long flags;

	if (!mux->is_supported)
		return;

	spin_lock_irqsave(&pinctrl->lock, flags);

	val = readl(pinctrl->base1 + mux->offset);
	val &= ~(0x3 << mux->shift);
	writel(val, pinctrl->base1 + mux->offset);

	spin_unlock_irqrestore(&pinctrl->lock, flags);

	dev_err(pctrl_dev->dev,
		"gpio disable free pin=%u offset=0x%x shift=%u\n",
		pin, mux->offset, mux->shift);
}

static const struct pinmux_ops cygnus_pinmux_ops = {
	.get_functions_count = cygnus_get_functions_count,
	.get_function_name = cygnus_get_function_name,
	.get_function_groups = cygnus_get_function_groups,
	.set_mux = cygnus_pinmux_set_mux,
	.gpio_request_enable = cygnus_gpio_request_enable,
	.gpio_disable_free = cygnus_gpio_disable_free,
};

static struct pinctrl_desc cygnus_pinctrl_desc = {
	.name = "cygnus-pinmux",
	.pctlops = &cygnus_pinctrl_ops,
	.pmxops = &cygnus_pinmux_ops,
};

static int cygnus_mux_log_init(struct cygnus_pinctrl *pinctrl)
{
	struct cygnus_mux_log *log;
	unsigned int i, j;

	pinctrl->mux_log = devm_kcalloc(pinctrl->dev, CYGNUS_NUM_IOMUX,
					sizeof(struct cygnus_mux_log),
					GFP_KERNEL);
	if (!pinctrl->mux_log)
		return -ENOMEM;

	log = pinctrl->mux_log;
	for (i = 0; i < CYGNUS_NUM_IOMUX_REGS; i++) {
		for (j = 0; j < CYGNUS_NUM_MUX_PER_REG; j++) {
			log = &pinctrl->mux_log[i * CYGNUS_NUM_MUX_PER_REG
				+ j];
			log->mux.offset = i * 4;
			log->mux.shift = j * 4;
			log->mux.alt = 0;
			log->is_configured = false;
		}
	}

	return 0;
}

static int cygnus_pinmux_probe(struct platform_device *pdev)
{
	struct cygnus_pinctrl *pinctrl;
	struct resource *res;
	int i, ret;
	struct pinctrl_pin_desc *pins;
	unsigned num_pins = ARRAY_SIZE(cygnus_pins);

	pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;

	pinctrl->dev = &pdev->dev;
	platform_set_drvdata(pdev, pinctrl);
	spin_lock_init(&pinctrl->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pinctrl->base0 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pinctrl->base0)) {
		dev_err(&pdev->dev, "unable to map I/O space\n");
		return PTR_ERR(pinctrl->base0);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	pinctrl->base1 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pinctrl->base1)) {
		dev_err(&pdev->dev, "unable to map I/O space\n");
		return PTR_ERR(pinctrl->base1);
	}

	ret = cygnus_mux_log_init(pinctrl);
	if (ret) {
		dev_err(&pdev->dev, "unable to initialize IOMUX log\n");
		return ret;
	}

	pins = devm_kcalloc(&pdev->dev, num_pins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < num_pins; i++) {
		pins[i].number = cygnus_pins[i].pin;
		pins[i].name = cygnus_pins[i].name;
		pins[i].drv_data = &cygnus_pins[i].gpio_mux;
	}

	pinctrl->groups = cygnus_pin_groups;
	pinctrl->num_groups = ARRAY_SIZE(cygnus_pin_groups);
	pinctrl->functions = cygnus_pin_functions;
	pinctrl->num_functions = ARRAY_SIZE(cygnus_pin_functions);
	cygnus_pinctrl_desc.pins = pins;
	cygnus_pinctrl_desc.npins = num_pins;

	pinctrl->pctl = pinctrl_register(&cygnus_pinctrl_desc, &pdev->dev,
			pinctrl);
	if (!pinctrl->pctl) {
		dev_err(&pdev->dev, "unable to register Cygnus IOMUX pinctrl\n");
		return -EINVAL;
	}

	return 0;
}

static const struct of_device_id cygnus_pinmux_of_match[] = {
	{ .compatible = "brcm,cygnus-pinmux" },
	{ }
};

static struct platform_driver cygnus_pinmux_driver = {
	.driver = {
		.name = "cygnus-pinmux",
		.of_match_table = cygnus_pinmux_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = cygnus_pinmux_probe,
};

static int __init cygnus_pinmux_init(void)
{
	return platform_driver_register(&cygnus_pinmux_driver);
}
arch_initcall(cygnus_pinmux_init);

MODULE_AUTHOR("Ray Jui <rjui@broadcom.com>");
MODULE_DESCRIPTION("Broadcom Cygnus IOMUX driver");
MODULE_LICENSE("GPL v2");
