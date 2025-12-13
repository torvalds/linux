// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN Pinctrl Controller Platform Device Driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 * Authors: Samuel Holland <samuel.holland@sifive.com>
 *          Yulin Lu <luyulin@eswincomputing.com>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/regulator/consumer.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinmux.h"
#include "pinconf.h"

#define EIC7700_PIN_REG(i)		(4 * (i))
#define EIC7700_IE			BIT(0)
#define EIC7700_PU			BIT(1)
#define EIC7700_PD			BIT(2)
#define EIC7700_DS			GENMASK(6, 3)
#define EIC7700_ST			BIT(7)
#define EIC7700_FUNC_SEL		GENMASK(18, 16)

#define EIC7700_BIAS			(EIC7700_PD | EIC7700_PU)
#define EIC7700_PINCONF			GENMASK(7, 0)

#define EIC7700_RGMII0_SEL_MODE		(0x310 - 0x80)
#define EIC7700_RGMII1_SEL_MODE		(0x314 - 0x80)
#define EIC7700_MS			GENMASK(1, 0)
#define EIC7700_MS_3V3			0x0
#define EIC7700_MS_1V8			0x3

#define EIC7700_FUNCTIONS_PER_PIN	8

struct eic7700_pin {
	u8 functions[EIC7700_FUNCTIONS_PER_PIN];
};

struct eic7700_pinctrl {
	void __iomem *base;
	struct pinctrl_desc desc;
	unsigned int functions_count;
	struct pinfunction functions[] __counted_by(functions_count);
};

enum {
	F_DISABLED,
	F_BOOT_SEL,
	F_CHIP_MODE,
	F_EMMC,
	F_FAN_TACH,
	F_GPIO,
	F_HDMI,
	F_I2C,
	F_I2S,
	F_JTAG,
	F_DDR_REF_CLK_SEL,
	F_LPDDR_REF_CLK,
	F_MIPI_CSI,
	F_OSC,
	F_PCIE,
	F_PWM,
	F_RGMII,
	F_RESET,
	F_SATA,
	F_SDIO,
	F_SPI,
	F_S_MODE,
	F_UART,
	F_USB,
	EIC7700_FUNCTIONS_COUNT
};

static const char *const eic7700_functions[EIC7700_FUNCTIONS_COUNT] = {
	[F_DISABLED]		= "disabled",
	[F_BOOT_SEL]		= "boot_sel",
	[F_CHIP_MODE]		= "chip_mode",
	[F_EMMC]		= "emmc",
	[F_FAN_TACH]		= "fan_tach",
	[F_GPIO]		= "gpio",
	[F_HDMI]		= "hdmi",
	[F_I2C]			= "i2c",
	[F_I2S]			= "i2s",
	[F_JTAG]		= "jtag",
	[F_DDR_REF_CLK_SEL]	= "ddr_ref_clk_sel",
	[F_LPDDR_REF_CLK]	= "lpddr_ref_clk",
	[F_MIPI_CSI]		= "mipi_csi",
	[F_OSC]			= "osc",
	[F_PCIE]		= "pcie",
	[F_PWM]			= "pwm",
	[F_RGMII]		= "rgmii",
	[F_RESET]		= "reset",
	[F_SATA]		= "sata",
	[F_SDIO]		= "sdio",
	[F_SPI]			= "spi",
	[F_S_MODE]		= "s_mode",
	[F_UART]		= "uart",
	[F_USB]			= "usb",
};

#define EIC7700_PIN(_number, _name, ...) \
	{ \
		.number	= _number, \
		.name = _name, \
		.drv_data = (void *)&(struct eic7700_pin) { { __VA_ARGS__ } } \
	}

static const struct pinctrl_pin_desc eic7700_pins[] = {
	EIC7700_PIN(0,   "chip_mode",		[0] = F_CHIP_MODE),
	EIC7700_PIN(1,   "mode_set0",		[0] = F_SDIO, [2] = F_GPIO),
	EIC7700_PIN(2,   "mode_set1",		[0] = F_SDIO, [2] = F_GPIO),
	EIC7700_PIN(3,   "mode_set2",		[0] = F_SDIO, [2] = F_GPIO),
	EIC7700_PIN(4,   "mode_set3",		[0] = F_SDIO, [2] = F_GPIO),
	EIC7700_PIN(5,   "xin",			[0] = F_OSC),
	EIC7700_PIN(6,   "rtc_xin",		[0] = F_DISABLED),
	EIC7700_PIN(7,   "rst_out_n",		[0] = F_RESET),
	EIC7700_PIN(8,   "key_reset_n",		[0] = F_RESET),
	EIC7700_PIN(9,   "rst_in_n",		[0] = F_DISABLED),
	EIC7700_PIN(10,  "por_in_n",		[0] = F_DISABLED),
	EIC7700_PIN(11,  "por_out_n",		[0] = F_DISABLED),
	EIC7700_PIN(12,  "gpio0",		[0] = F_GPIO),
	EIC7700_PIN(13,  "por_sel",		[0] = F_RESET),
	EIC7700_PIN(14,  "jtag0_tck",		[0] = F_JTAG, [1] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(15,  "jtag0_tms",		[0] = F_JTAG, [1] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(16,  "jtag0_tdi",		[0] = F_JTAG, [1] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(17,  "jtag0_tdo",		[0] = F_JTAG, [1] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(18,  "gpio5",		[0] = F_GPIO, [1] = F_SPI),
	EIC7700_PIN(19,  "spi2_cs0_n",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(20,  "jtag1_tck",		[0] = F_JTAG, [2] = F_GPIO),
	EIC7700_PIN(21,  "jtag1_tms",		[0] = F_JTAG, [2] = F_GPIO),
	EIC7700_PIN(22,  "jtag1_tdi",		[0] = F_JTAG, [2] = F_GPIO),
	EIC7700_PIN(23,  "jtag1_tdo",		[0] = F_JTAG, [2] = F_GPIO),
	EIC7700_PIN(24,  "gpio11",		[0] = F_GPIO),
	EIC7700_PIN(25,  "spi2_cs1_n",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(26,  "pcie_clkreq_n",	[0] = F_PCIE),
	EIC7700_PIN(27,  "pcie_wake_n",		[0] = F_PCIE),
	EIC7700_PIN(28,  "pcie_perst_n",	[0] = F_PCIE),
	EIC7700_PIN(29,  "hdmi_scl",		[0] = F_HDMI),
	EIC7700_PIN(30,  "hdmi_sda",		[0] = F_HDMI),
	EIC7700_PIN(31,  "hdmi_cec",		[0] = F_HDMI),
	EIC7700_PIN(32,  "jtag2_trst",		[0] = F_JTAG, [2] = F_GPIO),
	EIC7700_PIN(33,  "rgmii0_clk_125",	[0] = F_RGMII),
	EIC7700_PIN(34,  "rgmii0_txen",		[0] = F_RGMII),
	EIC7700_PIN(35,  "rgmii0_txclk",	[0] = F_RGMII),
	EIC7700_PIN(36,  "rgmii0_txd0",		[0] = F_RGMII),
	EIC7700_PIN(37,  "rgmii0_txd1",		[0] = F_RGMII),
	EIC7700_PIN(38,  "rgmii0_txd2",		[0] = F_RGMII),
	EIC7700_PIN(39,  "rgmii0_txd3",		[0] = F_RGMII),
	EIC7700_PIN(40,  "i2s0_bclk",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(41,  "i2s0_wclk",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(42,  "i2s0_sdi",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(43,  "i2s0_sdo",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(44,  "i2s_mclk",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(45,  "rgmii0_rxclk",	[0] = F_RGMII),
	EIC7700_PIN(46,  "rgmii0_rxdv",		[0] = F_RGMII),
	EIC7700_PIN(47,  "rgmii0_rxd0",		[0] = F_RGMII),
	EIC7700_PIN(48,  "rgmii0_rxd1",		[0] = F_RGMII),
	EIC7700_PIN(49,  "rgmii0_rxd2",		[0] = F_RGMII),
	EIC7700_PIN(50,  "rgmii0_rxd3",		[0] = F_RGMII),
	EIC7700_PIN(51,  "i2s2_bclk",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(52,  "i2s2_wclk",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(53,  "i2s2_sdi",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(54,  "i2s2_sdo",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(55,  "gpio27",		[0] = F_GPIO, [1] = F_SATA),
	EIC7700_PIN(56,  "gpio28",		[0] = F_GPIO),
	EIC7700_PIN(57,  "gpio29",		[0] = F_RESET, [1] = F_EMMC, [2] = F_GPIO),
	EIC7700_PIN(58,  "rgmii0_mdc",		[0] = F_RGMII),
	EIC7700_PIN(59,  "rgmii0_mdio",		[0] = F_RGMII),
	EIC7700_PIN(60,  "rgmii0_intb",		[0] = F_RGMII),
	EIC7700_PIN(61,  "rgmii1_clk_125",	[0] = F_RGMII),
	EIC7700_PIN(62,  "rgmii1_txen",		[0] = F_RGMII),
	EIC7700_PIN(63,  "rgmii1_txclk",	[0] = F_RGMII),
	EIC7700_PIN(64,  "rgmii1_txd0",		[0] = F_RGMII),
	EIC7700_PIN(65,  "rgmii1_txd1",		[0] = F_RGMII),
	EIC7700_PIN(66,  "rgmii1_txd2",		[0] = F_RGMII),
	EIC7700_PIN(67,  "rgmii1_txd3",		[0] = F_RGMII),
	EIC7700_PIN(68,  "i2s1_bclk",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(69,  "i2s1_wclk",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(70,  "i2s1_sdi",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(71,  "i2s1_sdo",		[0] = F_I2S, [2] = F_GPIO),
	EIC7700_PIN(72,  "gpio34",		[0] = F_RESET, [1] = F_SDIO, [2] = F_GPIO),
	EIC7700_PIN(73,  "rgmii1_rxclk",	[0] = F_RGMII),
	EIC7700_PIN(74,  "rgmii1_rxdv",		[0] = F_RGMII),
	EIC7700_PIN(75,  "rgmii1_rxd0",		[0] = F_RGMII),
	EIC7700_PIN(76,  "rgmii1_rxd1",		[0] = F_RGMII),
	EIC7700_PIN(77,  "rgmii1_rxd2",		[0] = F_RGMII),
	EIC7700_PIN(78,  "rgmii1_rxd3",		[0] = F_RGMII),
	EIC7700_PIN(79,  "spi1_cs0_n",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(80,  "spi1_clk",        [0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(81,  "spi1_d0", [0] = F_SPI, [1] = F_I2C, [2] = F_GPIO, [3] = F_UART),
	EIC7700_PIN(82,  "spi1_d1", [0] = F_SPI, [1] = F_I2C, [2] = F_GPIO, [3] = F_UART),
	EIC7700_PIN(83,  "spi1_d2",		[0] = F_SPI, [1] = F_SDIO, [2] = F_GPIO),
	EIC7700_PIN(84,  "spi1_d3",		[0] = F_SPI, [1] = F_PWM, [2] = F_GPIO),
	EIC7700_PIN(85,  "spi1_cs1_n",		[0] = F_SPI, [1] = F_PWM, [2] = F_GPIO),
	EIC7700_PIN(86,  "rgmii1_mdc",		[0] = F_RGMII),
	EIC7700_PIN(87,  "rgmii1_mdio",		[0] = F_RGMII),
	EIC7700_PIN(88,  "rgmii1_intb",		[0] = F_RGMII),
	EIC7700_PIN(89,  "usb0_pwren",		[0] = F_USB, [2] = F_GPIO),
	EIC7700_PIN(90,  "usb1_pwren",		[0] = F_USB, [2] = F_GPIO),
	EIC7700_PIN(91,  "i2c0_scl",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(92,  "i2c0_sda",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(93,  "i2c1_scl",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(94,  "i2c1_sda",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(95,  "i2c2_scl",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(96,  "i2c2_sda",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(97,  "i2c3_scl",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(98,  "i2c3_sda",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(99,  "i2c4_scl",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(100, "i2c4_sda",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(101, "i2c5_scl",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(102, "i2c5_sda",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(103, "uart0_tx",		[0] = F_UART, [2] = F_GPIO),
	EIC7700_PIN(104, "uart0_rx",		[0] = F_UART, [2] = F_GPIO),
	EIC7700_PIN(105, "uart1_tx",		[0] = F_UART, [2] = F_GPIO),
	EIC7700_PIN(106, "uart1_rx",		[0] = F_UART, [2] = F_GPIO),
	EIC7700_PIN(107, "uart1_cts",		[0] = F_UART, [1] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(108, "uart1_rts",		[0] = F_UART, [1] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(109, "uart2_tx",		[0] = F_UART, [1] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(110, "uart2_rx",		[0] = F_UART, [1] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(111, "jtag2_tck",		[0] = F_JTAG, [2] = F_GPIO),
	EIC7700_PIN(112, "jtag2_tms",		[0] = F_JTAG, [2] = F_GPIO),
	EIC7700_PIN(113, "jtag2_tdi",		[0] = F_JTAG, [2] = F_GPIO),
	EIC7700_PIN(114, "jtag2_tdo",		[0] = F_JTAG, [2] = F_GPIO),
	EIC7700_PIN(115, "fan_pwm",		[0] = F_PWM, [2] = F_GPIO),
	EIC7700_PIN(116, "fan_tach",		[0] = F_FAN_TACH, [2] = F_GPIO),
	EIC7700_PIN(117, "mipi_csi0_xvs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(118, "mipi_csi0_xhs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(119, "mipi_csi0_mclk",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(120, "mipi_csi1_xvs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(121, "mipi_csi1_xhs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(122, "mipi_csi1_mclk",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(123, "mipi_csi2_xvs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(124, "mipi_csi2_xhs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(125, "mipi_csi2_mclk",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(126, "mipi_csi3_xvs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(127, "mipi_csi3_xhs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(128, "mipi_csi3_mclk",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(129, "mipi_csi4_xvs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(130, "mipi_csi4_xhs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(131, "mipi_csi4_mclk",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(132, "mipi_csi5_xvs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(133, "mipi_csi5_xhs",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(134, "mipi_csi5_mclk",	[0] = F_MIPI_CSI, [2] = F_GPIO),
	EIC7700_PIN(135, "spi3_cs_n",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(136, "spi3_clk",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(137, "spi3_di",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(138, "spi3_do",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(139, "gpio92",	[0] = F_I2C, [1] = F_MIPI_CSI, [2] = F_GPIO, [3] = F_UART),
	EIC7700_PIN(140, "gpio93",	[0] = F_I2C, [1] = F_MIPI_CSI, [2] = F_GPIO, [3] = F_UART),
	EIC7700_PIN(141, "s_mode",		[0] = F_S_MODE, [2] = F_GPIO),
	EIC7700_PIN(142, "gpio95",		[0] = F_DDR_REF_CLK_SEL, [2] = F_GPIO),
	EIC7700_PIN(143, "spi0_cs_n",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(144, "spi0_clk",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(145, "spi0_d0",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(146, "spi0_d1",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(147, "spi0_d2",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(148, "spi0_d3",		[0] = F_SPI, [2] = F_GPIO),
	EIC7700_PIN(149, "i2c10_scl",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(150, "i2c10_sda",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(151, "i2c11_scl",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(152, "i2c11_sda",		[0] = F_I2C, [2] = F_GPIO),
	EIC7700_PIN(153, "gpio106",		[0] = F_GPIO),
	EIC7700_PIN(154, "boot_sel0",		[0] = F_BOOT_SEL, [2] = F_GPIO),
	EIC7700_PIN(155, "boot_sel1",		[0] = F_BOOT_SEL, [2] = F_GPIO),
	EIC7700_PIN(156, "boot_sel2",		[0] = F_BOOT_SEL, [2] = F_GPIO),
	EIC7700_PIN(157, "boot_sel3",		[0] = F_BOOT_SEL, [2] = F_GPIO),
	EIC7700_PIN(158, "gpio111",		[0] = F_GPIO),
	EIC7700_PIN(159, "reserved0",		[0] = F_DISABLED),
	EIC7700_PIN(160, "reserved1",		[0] = F_DISABLED),
	EIC7700_PIN(161, "reserved2",		[0] = F_DISABLED),
	EIC7700_PIN(162, "reserved3",		[0] = F_DISABLED),
	EIC7700_PIN(163, "lpddr_ref_clk",		[0] = F_LPDDR_REF_CLK),
};

static int eic7700_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->desc.npins;
}

static const char *eic7700_get_group_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->desc.pins[selector].name;
}

static int eic7700_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
				  const unsigned int **pins, unsigned int *npins)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pc->desc.pins[selector].number;
	*npins = 1;

	return 0;
}

static const struct pinctrl_ops eic7700_pinctrl_ops = {
	.get_groups_count	= eic7700_get_groups_count,
	.get_group_name		= eic7700_get_group_name,
	.get_group_pins		= eic7700_get_group_pins,
#ifdef CONFIG_OF
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_pin,
	.dt_free_map		= pinconf_generic_dt_free_map,
#endif
};

static int eic7700_pin_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
				  unsigned long *config)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	const struct eic7700_pin *pin_data = pc->desc.pins[pin].drv_data;
	u32 arg, value;
	int param;

	if (pin_data->functions[0] == F_OSC || pin_data->functions[0] == F_DISABLED)
		return -EOPNOTSUPP;

	value = readl_relaxed(pc->base + EIC7700_PIN_REG(pin));

	param = pinconf_to_config_param(*config);
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		arg = (value & EIC7700_BIAS) == 0;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = (value & EIC7700_BIAS) == EIC7700_PD;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		arg = (value & EIC7700_BIAS) == EIC7700_PU;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		if (pin_data->functions[0] == F_RGMII ||
			pin_data->functions[0] == F_LPDDR_REF_CLK)
			arg = FIELD_GET(EIC7700_DS, value) * 3000 + 3000;
		else
			arg = FIELD_GET(EIC7700_DS, value) * 3000 + 6000;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		arg = value & EIC7700_IE;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		arg = value & EIC7700_ST;
		break;
	default:
		return -EOPNOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return arg ? 0 : -EINVAL;
}

static int eic7700_pin_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
				  unsigned long *configs, unsigned int num_configs)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	const struct eic7700_pin *pin_data = pc->desc.pins[pin].drv_data;
	u32 value;

	if (pin_data->functions[0] == F_OSC || pin_data->functions[0] == F_DISABLED)
		return -EOPNOTSUPP;

	value = readl_relaxed(pc->base + EIC7700_PIN_REG(pin));

	for (unsigned int i = 0; i < num_configs; i++) {
		int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			value &= ~EIC7700_BIAS;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (arg == 0)
				return -EOPNOTSUPP;
			value &= ~EIC7700_BIAS;
			value |= EIC7700_PD;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (arg == 0)
				return -EOPNOTSUPP;
			value &= ~EIC7700_BIAS;
			value |= EIC7700_PU;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH_UA:
			value &= ~EIC7700_DS;
			if (pin_data->functions[0] == F_RGMII ||
				pin_data->functions[0] == F_LPDDR_REF_CLK) {
				if (arg < 3000 || arg > 24000)
					return -EOPNOTSUPP;
				value |= FIELD_PREP(EIC7700_DS, (arg - 3000) / 3000);
			} else {
				if (arg < 6000 || arg > 27000)
					return -EOPNOTSUPP;
				value |= FIELD_PREP(EIC7700_DS, (arg - 6000) / 3000);
			}
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			if (arg)
				value |= EIC7700_IE;
			else
				value &= ~EIC7700_IE;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			if (arg)
				value |= EIC7700_ST;
			else
				value &= ~EIC7700_ST;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	writel_relaxed(value, pc->base + EIC7700_PIN_REG(pin));

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void eic7700_pin_config_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
					unsigned int pin)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	u32 value = readl_relaxed(pc->base + EIC7700_PIN_REG(pin)) & EIC7700_PINCONF;

	seq_printf(s, " [0x%02x]", value);
}
#else
#define eic7700_pin_config_dbg_show NULL
#endif

static const struct pinconf_ops eic7700_pinconf_ops = {
	.is_generic			= true,
	.pin_config_get			= eic7700_pin_config_get,
	.pin_config_set			= eic7700_pin_config_set,
	.pin_config_group_get		= eic7700_pin_config_get,
	.pin_config_group_set		= eic7700_pin_config_set,
	.pin_config_dbg_show		= eic7700_pin_config_dbg_show,
	.pin_config_group_dbg_show	= eic7700_pin_config_dbg_show,
};

static int eic7700_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->functions_count;
}

static const char *eic7700_get_function_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	return pc->functions[selector].name;
}

static int eic7700_get_function_groups(struct pinctrl_dev *pctldev, unsigned int selector,
				       const char *const **groups, unsigned int *num_groups)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	*groups = pc->functions[selector].groups;
	*num_groups = pc->functions[selector].ngroups;

	return 0;
}

static int eic7700_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
			   unsigned int group_selector)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	const struct eic7700_pin *pin_data = pc->desc.pins[group_selector].drv_data;
	u32 fs, value;

	if (pin_data->functions[0] == F_OSC || pin_data->functions[0] == F_DISABLED)
		return -EOPNOTSUPP;

	for (fs = 0; fs < EIC7700_FUNCTIONS_PER_PIN; fs++)
		if (pin_data->functions[fs] == func_selector)
			break;

	if (fs == EIC7700_FUNCTIONS_PER_PIN) {
		dev_err(pctldev->dev, "invalid mux %s for pin %s\n",
			pc->functions[func_selector].name,
			pc->desc.pins[group_selector].name);
		return -EINVAL;
	}

	value = readl_relaxed(pc->base + EIC7700_PIN_REG(group_selector));
	value &= ~EIC7700_FUNC_SEL;
	value |= FIELD_PREP(EIC7700_FUNC_SEL, fs);
	writel_relaxed(value, pc->base + EIC7700_PIN_REG(group_selector));

	return 0;
}

static int eic7700_gpio_request_enable(struct pinctrl_dev *pctldev,
				       struct pinctrl_gpio_range *range, unsigned int offset)
{
	return eic7700_set_mux(pctldev, F_GPIO, offset);
}

static void eic7700_gpio_disable_free(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range, unsigned int offset)
{
	eic7700_set_mux(pctldev, F_DISABLED, offset);
}

static int eic7700_gpio_set_direction(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range, unsigned int offset,
				      bool input)
{
	struct eic7700_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	u32 value;

	value = readl_relaxed(pc->base + EIC7700_PIN_REG(offset));
	if (input)
		value |= EIC7700_IE;
	else
		value &= ~EIC7700_IE;
	writel_relaxed(value, pc->base + EIC7700_PIN_REG(offset));

	return 0;
}

static const struct pinmux_ops eic7700_pinmux_ops = {
	.get_functions_count	= eic7700_get_functions_count,
	.get_function_name	= eic7700_get_function_name,
	.get_function_groups	= eic7700_get_function_groups,
	.set_mux		= eic7700_set_mux,
	.gpio_request_enable	= eic7700_gpio_request_enable,
	.gpio_disable_free	= eic7700_gpio_disable_free,
	.gpio_set_direction	= eic7700_gpio_set_direction,
	.strict			= true,
};

static int eic7700_pinctrl_init_function_groups(struct device *dev, struct eic7700_pinctrl *pc,
						const char *const *function_names)
{
	unsigned int ngroups = 0;
	const char **groups;

	/* Count the number of groups for each function */
	for (unsigned int pin = 0; pin < pc->desc.npins; pin++) {
		const struct eic7700_pin *pin_data = pc->desc.pins[pin].drv_data;
		bool found_disabled = false;

		for (unsigned int fs = 0; fs < EIC7700_FUNCTIONS_PER_PIN; fs++) {
			unsigned int selector = pin_data->functions[fs];
			struct pinfunction *function = &pc->functions[selector];

			/* Only count F_DISABLED once per pin */
			if (selector == F_DISABLED) {
				if (found_disabled)
					continue;
				found_disabled = true;
			}

			function->ngroups++;
			ngroups++;
		}
	}

	groups = devm_kcalloc(dev, ngroups, sizeof(*groups), GFP_KERNEL);
	if (!groups)
		return -ENOMEM;

	for (unsigned int selector = 0; selector < pc->functions_count; selector++) {
		struct pinfunction *function = &pc->functions[selector];

		function->name = function_names[selector];
		function->groups = groups;
		groups += function->ngroups;

		/* Reset per-function ngroups for use as iterator below */
		function->ngroups = 0;
	}

	/* Fill in the group pointers for each function */
	for (unsigned int pin = 0; pin < pc->desc.npins; pin++) {
		const struct pinctrl_pin_desc *desc = &pc->desc.pins[pin];
		const struct eic7700_pin *pin_data = desc->drv_data;
		bool found_disabled = false;

		for (unsigned int fs = 0; fs < EIC7700_FUNCTIONS_PER_PIN; fs++) {
			unsigned int selector = pin_data->functions[fs];
			struct pinfunction *function = &pc->functions[selector];

			/* Only count F_DISABLED once per pin */
			if (selector == F_DISABLED) {
				if (found_disabled)
					continue;
				found_disabled = true;
			}

			((const char **)function->groups)[function->ngroups++] = desc->name;
		}
	}

	return 0;
}

static int eic7700_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pinctrl_dev *pctldev;
	struct eic7700_pinctrl *pc;
	struct regulator *regulator;
	u32 rgmii0_mode, rgmii1_mode;
	int ret, voltage;

	pc = devm_kzalloc(dev, struct_size(pc, functions, EIC7700_FUNCTIONS_COUNT), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	pc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->base))
		return PTR_ERR(pc->base);

	regulator = devm_regulator_get(dev, "vrgmii");
	if (IS_ERR(regulator)) {
		return dev_err_probe(dev, PTR_ERR(regulator),
					 "failed to get vrgmii regulator\n");
	}

	voltage = regulator_get_voltage(regulator);
	if (voltage < 0) {
		return dev_err_probe(&pdev->dev, voltage,
			 "Failed to get voltage from regulator\n");
	}

	rgmii0_mode = readl_relaxed(pc->base + EIC7700_RGMII0_SEL_MODE);
	rgmii1_mode = readl_relaxed(pc->base + EIC7700_RGMII1_SEL_MODE);
	rgmii0_mode &= ~EIC7700_MS;
	rgmii1_mode &= ~EIC7700_MS;
	if (voltage == 1800000) {
		rgmii0_mode |= FIELD_PREP(EIC7700_MS, EIC7700_MS_1V8);
		rgmii1_mode |= FIELD_PREP(EIC7700_MS, EIC7700_MS_1V8);
	} else if (voltage == 3300000) {
		rgmii0_mode |= FIELD_PREP(EIC7700_MS, EIC7700_MS_3V3);
		rgmii1_mode |= FIELD_PREP(EIC7700_MS, EIC7700_MS_3V3);
	} else {
		return dev_err_probe(&pdev->dev, -EINVAL,
			 "Invalid voltage configuration, should be either 1.8V or 3.3V\n");
	}

	writel_relaxed(rgmii0_mode, pc->base + EIC7700_RGMII0_SEL_MODE);
	writel_relaxed(rgmii1_mode, pc->base + EIC7700_RGMII1_SEL_MODE);

	pc->desc.name = dev_name(dev);
	pc->desc.pins = eic7700_pins;
	pc->desc.npins = ARRAY_SIZE(eic7700_pins);
	pc->desc.pctlops = &eic7700_pinctrl_ops;
	pc->desc.pmxops = &eic7700_pinmux_ops;
	pc->desc.confops = &eic7700_pinconf_ops;
	pc->desc.owner = THIS_MODULE;

	pc->functions_count = EIC7700_FUNCTIONS_COUNT;
	ret = eic7700_pinctrl_init_function_groups(dev, pc, eic7700_functions);
	if (ret)
		return ret;

	ret = devm_pinctrl_register_and_init(dev, &pc->desc, pc, &pctldev);
	if (ret)
		return dev_err_probe(dev, ret, "could not register pinctrl driver\n");

	return pinctrl_enable(pctldev);
}

static const struct of_device_id eic7700_pinctrl_of_match[] = {
	{ .compatible = "eswin,eic7700-pinctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, eic7700_pinctrl_of_match);

static struct platform_driver eic7700_pinctrl_driver = {
	.probe	= eic7700_pinctrl_probe,
	.driver	= {
		.name		= "pinctrl-eic7700",
		.of_match_table	= eic7700_pinctrl_of_match,
	},
};
module_platform_driver(eic7700_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for the ESWIN EIC7700 SoC");
MODULE_AUTHOR("Samuel Holland <samuel.holland@sifive.com>");
MODULE_AUTHOR("Yulin Lu <luyulin@eswincomputing.com>");
MODULE_LICENSE("GPL");
