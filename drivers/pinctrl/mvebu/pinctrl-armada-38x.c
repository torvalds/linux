/*
 * Marvell Armada 380/385 pinctrl driver based on mvebu pinctrl core
 *
 * Copyright (C) 2013 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-mvebu.h"

static void __iomem *mpp_base;

static int armada_38x_mpp_ctrl_get(unsigned pid, unsigned long *config)
{
	return default_mpp_ctrl_get(mpp_base, pid, config);
}

static int armada_38x_mpp_ctrl_set(unsigned pid, unsigned long config)
{
	return default_mpp_ctrl_set(mpp_base, pid, config);
}

enum {
	V_88F6810 = BIT(0),
	V_88F6820 = BIT(1),
	V_88F6828 = BIT(2),
	V_88F6810_PLUS = (V_88F6810 | V_88F6820 | V_88F6828),
	V_88F6820_PLUS = (V_88F6820 | V_88F6828),
};

static struct mvebu_mpp_mode armada_38x_mpp_modes[] = {
	MPP_MODE(0,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ua0",   "rxd",        V_88F6810_PLUS)),
	MPP_MODE(1,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ua0",   "txd",        V_88F6810_PLUS)),
	MPP_MODE(2,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "i2c0",  "sck",        V_88F6810_PLUS)),
	MPP_MODE(3,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "i2c0",  "sda",        V_88F6810_PLUS)),
	MPP_MODE(4,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge",    "mdc",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ua1",   "txd",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ua0",   "rts",        V_88F6810_PLUS)),
	MPP_MODE(5,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge",    "mdio",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ua1",   "rxd",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ua0",   "cts",        V_88F6810_PLUS)),
	MPP_MODE(6,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "txclkout",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge0",   "crs",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "cs3",        V_88F6810_PLUS)),
	MPP_MODE(7,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "txd0",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad9",        V_88F6810_PLUS)),
	MPP_MODE(8,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "txd1",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad10",       V_88F6810_PLUS)),
	MPP_MODE(9,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "txd2",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad11",       V_88F6810_PLUS)),
	MPP_MODE(10,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "txd3",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad12",       V_88F6810_PLUS)),
	MPP_MODE(11,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "txctl",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad13",       V_88F6810_PLUS)),
	MPP_MODE(12,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "rxd0",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "pcie0", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie1", "rstout",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(4, "spi0",  "cs1",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad14",       V_88F6810_PLUS)),
	MPP_MODE(13,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "rxd1",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "pcie0", "clkreq",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie1", "clkreq",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(4, "spi0",  "cs2",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad15",       V_88F6810_PLUS)),
	MPP_MODE(14,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "rxd2",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ptp",   "clk",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "m",     "vtt_ctrl",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "spi0",  "cs3",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "wen1",       V_88F6810_PLUS)),
	MPP_MODE(15,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "rxd3",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge",    "mdc slave",  V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie0", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "spi0",  "mosi",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "pcie1", "rstout",     V_88F6820_PLUS)),
	MPP_MODE(16,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "rxctl",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge",    "mdio slave", V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "m",     "decc_err",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "spi0",  "miso",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "pcie0", "clkreq",     V_88F6810_PLUS)),
	MPP_MODE(17,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "rxclk",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ptp",   "clk",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ua1",   "rxd",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "spi0",  "sck",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sata1", "prsnt",      V_88F6810_PLUS)),
	MPP_MODE(18,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "rxerr",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ptp",   "trig_gen",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ua1",   "txd",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "spi0",  "cs0",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "pcie1", "rstout",     V_88F6820_PLUS)),
	MPP_MODE(19,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "col",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ptp",   "event_req",  V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie0", "clkreq",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sata1", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "ua0",   "cts",        V_88F6810_PLUS)),
	MPP_MODE(20,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ge0",   "txclk",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ptp",   "clk",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie1", "rstout",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(4, "sata0", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "ua0",   "rts",        V_88F6810_PLUS)),
	MPP_MODE(21,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "spi0",  "cs1",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "rxd0",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "sata0", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sd0",   "cmd",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "bootcs",     V_88F6810_PLUS)),
	MPP_MODE(22,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "spi0",  "mosi",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad0",        V_88F6810_PLUS)),
	MPP_MODE(23,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "spi0",  "sck",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad2",        V_88F6810_PLUS)),
	MPP_MODE(24,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "spi0",  "miso",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ua0",   "cts",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ua1",   "rxd",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sd0",   "d4",         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ready",      V_88F6810_PLUS)),
	MPP_MODE(25,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "spi0",  "cs0",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ua0",   "rts",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ua1",   "txd",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sd0",   "d5",         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "cs0",        V_88F6810_PLUS)),
	MPP_MODE(26,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "spi0",  "cs2",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "i2c1",  "sck",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sd0",   "d6",         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "cs1",        V_88F6810_PLUS)),
	MPP_MODE(27,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "spi0",  "cs3",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "txclkout",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "i2c1",  "sda",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sd0",   "d7",         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "cs2",        V_88F6810_PLUS)),
	MPP_MODE(28,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "txd0",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sd0",   "clk",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad5",        V_88F6810_PLUS)),
	MPP_MODE(29,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "txd1",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ale0",       V_88F6810_PLUS)),
	MPP_MODE(30,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "txd2",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "oen",        V_88F6810_PLUS)),
	MPP_MODE(31,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "txd3",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ale1",       V_88F6810_PLUS)),
	MPP_MODE(32,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "txctl",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "wen0",       V_88F6810_PLUS)),
	MPP_MODE(33,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "m",     "decc_err",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad3",        V_88F6810_PLUS)),
	MPP_MODE(34,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad1",        V_88F6810_PLUS)),
	MPP_MODE(35,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ref",   "clk_out1",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "a1",         V_88F6810_PLUS)),
	MPP_MODE(36,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ptp",   "trig_gen",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "a0",         V_88F6810_PLUS)),
	MPP_MODE(37,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ptp",   "clk",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "rxclk",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sd0",   "d3",         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad8",        V_88F6810_PLUS)),
	MPP_MODE(38,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ptp",   "event_req",  V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "rxd1",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ref",   "clk_out0",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sd0",   "d0",         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad4",        V_88F6810_PLUS)),
	MPP_MODE(39,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "i2c1",  "sck",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "rxd2",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ua0",   "cts",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sd0",   "d1",         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "a2",         V_88F6810_PLUS)),
	MPP_MODE(40,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "i2c1",  "sda",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "rxd3",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ua0",   "rts",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "sd0",   "d2",         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad6",        V_88F6810_PLUS)),
	MPP_MODE(41,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ua1",   "rxd",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge1",   "rxctl",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ua0",   "cts",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "spi1",  "cs3",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "burst/last", V_88F6810_PLUS)),
	MPP_MODE(42,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ua1",   "txd",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "ua0",   "rts",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "ad7",        V_88F6810_PLUS)),
	MPP_MODE(43,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "pcie0", "clkreq",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "m",     "vtt_ctrl",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "m",     "decc_err",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "pcie0", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "dev",   "clkout",     V_88F6810_PLUS)),
	MPP_MODE(44,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "sata0", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "sata1", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "sata2", "prsnt",      V_88F6828),
		 MPP_VAR_FUNCTION(4, "sata3", "prsnt",      V_88F6828),
		 MPP_VAR_FUNCTION(5, "pcie0", "rstout",     V_88F6810_PLUS)),
	MPP_MODE(45,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ref",   "clk_out0",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "pcie0", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie1", "rstout",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(4, "pcie2", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "pcie3", "rstout",     V_88F6810_PLUS)),
	MPP_MODE(46,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ref",   "clk_out1",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "pcie0", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie1", "rstout",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(4, "pcie2", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "pcie3", "rstout",     V_88F6810_PLUS)),
	MPP_MODE(47,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "sata0", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "sata1", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "sata2", "prsnt",      V_88F6828),
		 MPP_VAR_FUNCTION(4, "spi1",  "cs2",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sata3", "prsnt",      V_88F6828)),
	MPP_MODE(48,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "sata0", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "m",     "vtt_ctrl",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "tdm2c", "pclk",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "audio", "mclk",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sd0",   "d4",         V_88F6810_PLUS)),
	MPP_MODE(49,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "sata2", "prsnt",      V_88F6828),
		 MPP_VAR_FUNCTION(2, "sata3", "prsnt",      V_88F6828),
		 MPP_VAR_FUNCTION(3, "tdm2c", "fsync",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "audio", "lrclk",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sd0",   "d5",         V_88F6810_PLUS)),
	MPP_MODE(50,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "pcie0", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "pcie1", "rstout",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(3, "tdm2c", "drx",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "audio", "extclk",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sd0",   "cmd",        V_88F6810_PLUS)),
	MPP_MODE(51,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "tdm2c", "dtx",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "audio", "sdo",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "m",     "decc_err",   V_88F6810_PLUS)),
	MPP_MODE(52,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "pcie0", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "pcie1", "rstout",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(3, "tdm2c", "intn",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "audio", "sdi",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sd0",   "d6",         V_88F6810_PLUS)),
	MPP_MODE(53,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "sata1", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "sata0", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "tdm2c", "rstn",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "audio", "bclk",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sd0",   "d7",         V_88F6810_PLUS)),
	MPP_MODE(54,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "sata0", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "sata1", "prsnt",      V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie0", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "pcie1", "rstout",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(5, "sd0",   "d3",         V_88F6810_PLUS)),
	MPP_MODE(55,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ua1",   "cts",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge",    "mdio",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie1", "clkreq",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(4, "spi1",  "cs1",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sd0",   "d0",         V_88F6810_PLUS)),
	MPP_MODE(56,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "ua1",   "rts",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "ge",    "mdc",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "m",     "decc_err",   V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "spi1",  "mosi",       V_88F6810_PLUS)),
	MPP_MODE(57,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "spi1",  "sck",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sd0",   "clk",        V_88F6810_PLUS)),
	MPP_MODE(58,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "pcie1", "clkreq",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(2, "i2c1",  "sck",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie2", "clkreq",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(4, "spi1",  "miso",       V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sd0",   "d1",         V_88F6810_PLUS)),
	MPP_MODE(59,
		 MPP_VAR_FUNCTION(0, "gpio",  NULL,         V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(1, "pcie0", "rstout",     V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(2, "i2c1",  "sda",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(3, "pcie1", "rstout",     V_88F6820_PLUS),
		 MPP_VAR_FUNCTION(4, "spi1",  "cs0",        V_88F6810_PLUS),
		 MPP_VAR_FUNCTION(5, "sd0",   "d2",         V_88F6810_PLUS)),
};

static struct mvebu_pinctrl_soc_info armada_38x_pinctrl_info;

static struct of_device_id armada_38x_pinctrl_of_match[] = {
	{
		.compatible = "marvell,mv88f6810-pinctrl",
		.data       = (void *) V_88F6810,
	},
	{
		.compatible = "marvell,mv88f6820-pinctrl",
		.data       = (void *) V_88F6820,
	},
	{
		.compatible = "marvell,mv88f6828-pinctrl",
		.data       = (void *) V_88F6828,
	},
	{ },
};

static struct mvebu_mpp_ctrl armada_38x_mpp_controls[] = {
	MPP_FUNC_CTRL(0, 59, NULL, armada_38x_mpp_ctrl),
};

static struct pinctrl_gpio_range armada_38x_mpp_gpio_ranges[] = {
	MPP_GPIO_RANGE(0,   0,  0, 32),
	MPP_GPIO_RANGE(1,  32, 32, 27),
};

static int armada_38x_pinctrl_probe(struct platform_device *pdev)
{
	struct mvebu_pinctrl_soc_info *soc = &armada_38x_pinctrl_info;
	const struct of_device_id *match =
		of_match_device(armada_38x_pinctrl_of_match, &pdev->dev);
	struct resource *res;

	if (!match)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mpp_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mpp_base))
		return PTR_ERR(mpp_base);

	soc->variant = (unsigned) match->data & 0xff;
	soc->controls = armada_38x_mpp_controls;
	soc->ncontrols = ARRAY_SIZE(armada_38x_mpp_controls);
	soc->gpioranges = armada_38x_mpp_gpio_ranges;
	soc->ngpioranges = ARRAY_SIZE(armada_38x_mpp_gpio_ranges);
	soc->modes = armada_38x_mpp_modes;
	soc->nmodes = armada_38x_mpp_controls[0].npins;

	pdev->dev.platform_data = soc;

	return mvebu_pinctrl_probe(pdev);
}

static int armada_38x_pinctrl_remove(struct platform_device *pdev)
{
	return mvebu_pinctrl_remove(pdev);
}

static struct platform_driver armada_38x_pinctrl_driver = {
	.driver = {
		.name = "armada-38x-pinctrl",
		.of_match_table = of_match_ptr(armada_38x_pinctrl_of_match),
	},
	.probe = armada_38x_pinctrl_probe,
	.remove = armada_38x_pinctrl_remove,
};

module_platform_driver(armada_38x_pinctrl_driver);

MODULE_AUTHOR("Thomas Petazzoni <thomas.petazzoni@free-electrons.com>");
MODULE_DESCRIPTION("Marvell Armada 38x pinctrl driver");
MODULE_LICENSE("GPL v2");
