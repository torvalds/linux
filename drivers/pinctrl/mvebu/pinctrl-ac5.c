// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell ac5 pinctrl driver based on mvebu pinctrl core
 *
 * Copyright (C) 2021 Marvell
 *
 * Noam Liron <lnoam@marvell.com>
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-mvebu.h"

static struct mvebu_mpp_mode ac5_mpp_modes[] = {
	MPP_MODE(0,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "d0"),
		 MPP_FUNCTION(2, "nand",  "io4")),
	MPP_MODE(1,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "d1"),
		 MPP_FUNCTION(2, "nand",  "io3")),
	MPP_MODE(2,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "d2"),
		 MPP_FUNCTION(2, "nand",  "io2")),
	MPP_MODE(3,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "d3"),
		 MPP_FUNCTION(2, "nand",  "io7")),
	MPP_MODE(4,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "d4"),
		 MPP_FUNCTION(2, "nand",  "io6"),
		 MPP_FUNCTION(3, "uart3", "txd"),
		 MPP_FUNCTION(4, "uart2", "txd")),
	MPP_MODE(5,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "d5"),
		 MPP_FUNCTION(2, "nand",  "io5"),
		 MPP_FUNCTION(3, "uart3", "rxd"),
		 MPP_FUNCTION(4, "uart2", "rxd")),
	MPP_MODE(6,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "d6"),
		 MPP_FUNCTION(2, "nand",  "io0"),
		 MPP_FUNCTION(3, "i2c1",  "sck")),
	MPP_MODE(7,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "d7"),
		 MPP_FUNCTION(2, "nand",  "io1"),
		 MPP_FUNCTION(3, "i2c1",  "sda")),
	MPP_MODE(8,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "clk"),
		 MPP_FUNCTION(2, "nand",  "wen")),
	MPP_MODE(9,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "cmd"),
		 MPP_FUNCTION(2, "nand",  "ale")),
	MPP_MODE(10,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "ds"),
		 MPP_FUNCTION(2, "nand",  "cle")),
	MPP_MODE(11,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "sdio",  "rst"),
		 MPP_FUNCTION(2, "nand",  "cen")),
	MPP_MODE(12,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "spi0",  "clk")),
	MPP_MODE(13,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "spi0",  "csn")),
	MPP_MODE(14,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "spi0",  "mosi")),
	MPP_MODE(15,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "spi0",  "miso")),
	MPP_MODE(16,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "spi0",  "wpn"),
		 MPP_FUNCTION(2, "nand",  "ren"),
		 MPP_FUNCTION(3, "uart1", "txd")),
	MPP_MODE(17,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "spi0",  "hold"),
		 MPP_FUNCTION(2, "nand",  "rb"),
		 MPP_FUNCTION(3, "uart1", "rxd")),
	MPP_MODE(18,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "tsen_int", NULL),
		 MPP_FUNCTION(2, "uart2", "rxd"),
		 MPP_FUNCTION(3, "wd_int", NULL)),
	MPP_MODE(19,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "dev_init_done", NULL),
		 MPP_FUNCTION(2, "uart2", "txd")),
	MPP_MODE(20,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(2, "i2c1",  "sck"),
		 MPP_FUNCTION(3, "spi1",  "clk"),
		 MPP_FUNCTION(4, "uart3", "txd")),
	MPP_MODE(21,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(2, "i2c1",  "sda"),
		 MPP_FUNCTION(3, "spi1",  "csn"),
		 MPP_FUNCTION(4, "uart3", "rxd")),
	MPP_MODE(22,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(3, "spi1",  "mosi")),
	MPP_MODE(23,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(3, "spi1",  "miso")),
	MPP_MODE(24,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "wd_int", NULL),
		 MPP_FUNCTION(2, "uart2", "txd"),
		 MPP_FUNCTION(3, "uartsd", "txd")),
	MPP_MODE(25,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "int_out", NULL),
		 MPP_FUNCTION(2, "uart2", "rxd"),
		 MPP_FUNCTION(3, "uartsd", "rxd")),
	MPP_MODE(26,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "i2c0",  "sck"),
		 MPP_FUNCTION(2, "ptp", "clk1"),
		 MPP_FUNCTION(3, "uart3", "txd")),
	MPP_MODE(27,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "i2c0",  "sda"),
		 MPP_FUNCTION(2, "ptp", "pulse"),
		 MPP_FUNCTION(3, "uart3", "rxd")),
	MPP_MODE(28,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "xg", "mdio"),
		 MPP_FUNCTION(2, "ge", "mdio"),
		 MPP_FUNCTION(3, "uart3", "txd")),
	MPP_MODE(29,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "xg", "mdio"),
		 MPP_FUNCTION(2, "ge", "mdio"),
		 MPP_FUNCTION(3, "uart3", "rxd")),
	MPP_MODE(30,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "xg", "mdio"),
		 MPP_FUNCTION(2, "ge", "mdio"),
		 MPP_FUNCTION(3, "ge", "mdio")),
	MPP_MODE(31,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "xg", "mdio"),
		 MPP_FUNCTION(2, "ge", "mdio"),
		 MPP_FUNCTION(3, "ge", "mdio")),
	MPP_MODE(32,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "uart0", "txd")),
	MPP_MODE(33,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "uart0", "rxd"),
		 MPP_FUNCTION(2, "ptp", "clk1"),
		 MPP_FUNCTION(3, "ptp", "pulse")),
	MPP_MODE(34,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "ge", "mdio"),
		 MPP_FUNCTION(2, "uart3", "rxd")),
	MPP_MODE(35,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "ge", "mdio"),
		 MPP_FUNCTION(2, "uart3", "txd"),
		 MPP_FUNCTION(3, "pcie", "rstoutn")),
	MPP_MODE(36,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "ptp", "clk0_tp"),
		 MPP_FUNCTION(2, "ptp", "clk1_tp")),
	MPP_MODE(37,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "ptp", "pulse_tp"),
		 MPP_FUNCTION(2, "wd_int", NULL)),
	MPP_MODE(38,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "synce", "clk_out0")),
	MPP_MODE(39,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "synce", "clk_out1")),
	MPP_MODE(40,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "ptp", "pclk_out0"),
		 MPP_FUNCTION(2, "ptp", "pclk_out1")),
	MPP_MODE(41,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "ptp", "ref_clk"),
		 MPP_FUNCTION(2, "ptp", "clk1"),
		 MPP_FUNCTION(3, "ptp", "pulse"),
		 MPP_FUNCTION(4, "uart2", "txd"),
		 MPP_FUNCTION(5, "i2c1",  "sck")),
	MPP_MODE(42,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "ptp", "clk0"),
		 MPP_FUNCTION(2, "ptp", "clk1"),
		 MPP_FUNCTION(3, "ptp", "pulse"),
		 MPP_FUNCTION(4, "uart2", "rxd"),
		 MPP_FUNCTION(5, "i2c1",  "sda")),
	MPP_MODE(43,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "led", "clk")),
	MPP_MODE(44,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "led", "stb")),
	MPP_MODE(45,
		 MPP_FUNCTION(0, "gpio",  NULL),
		 MPP_FUNCTION(1, "led", "data")),
};

static struct mvebu_pinctrl_soc_info ac5_pinctrl_info;

static const struct of_device_id ac5_pinctrl_of_match[] = {
	{
		.compatible = "marvell,ac5-pinctrl",
	},
	{ },
};

static const struct mvebu_mpp_ctrl ac5_mpp_controls[] = {
	MPP_FUNC_CTRL(0, 45, NULL, mvebu_mmio_mpp_ctrl), };

static struct pinctrl_gpio_range ac5_mpp_gpio_ranges[] = {
	MPP_GPIO_RANGE(0,   0,  0, 46), };

static int ac5_pinctrl_probe(struct platform_device *pdev)
{
	struct mvebu_pinctrl_soc_info *soc = &ac5_pinctrl_info;

	soc->variant = 0; /* no variants for ac5 */
	soc->controls = ac5_mpp_controls;
	soc->ncontrols = ARRAY_SIZE(ac5_mpp_controls);
	soc->gpioranges = ac5_mpp_gpio_ranges;
	soc->ngpioranges = ARRAY_SIZE(ac5_mpp_gpio_ranges);
	soc->modes = ac5_mpp_modes;
	soc->nmodes = ac5_mpp_controls[0].npins;

	pdev->dev.platform_data = soc;

	return mvebu_pinctrl_simple_mmio_probe(pdev);
}

static struct platform_driver ac5_pinctrl_driver = {
	.driver = {
		.name = "ac5-pinctrl",
		.of_match_table = of_match_ptr(ac5_pinctrl_of_match),
	},
	.probe = ac5_pinctrl_probe,
};
builtin_platform_driver(ac5_pinctrl_driver);
