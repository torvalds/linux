/*
 * Marvell Armada XP pinctrl driver based on mvebu pinctrl core
 *
 * Copyright (C) 2012 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file supports the three variants of Armada XP SoCs that are
 * available: mv78230, mv78260 and mv78460. From a pin muxing
 * perspective, the mv78230 has 49 MPP pins. The mv78260 and mv78460
 * both have 67 MPP pins (more GPIOs and address lines for the memory
 * bus mainly).
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/bitops.h>

#include "pinctrl-mvebu.h"

static void __iomem *mpp_base;
static u32 *mpp_saved_regs;

static int armada_xp_mpp_ctrl_get(unsigned pid, unsigned long *config)
{
	return default_mpp_ctrl_get(mpp_base, pid, config);
}

static int armada_xp_mpp_ctrl_set(unsigned pid, unsigned long config)
{
	return default_mpp_ctrl_set(mpp_base, pid, config);
}

enum armada_xp_variant {
	V_MV78230	= BIT(0),
	V_MV78260	= BIT(1),
	V_MV78460	= BIT(2),
	V_MV78230_PLUS	= (V_MV78230 | V_MV78260 | V_MV78460),
	V_MV78260_PLUS	= (V_MV78260 | V_MV78460),
};

static struct mvebu_mpp_mode armada_xp_mpp_modes[] = {
	MPP_MODE(0,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txclkout",   V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d0",         V_MV78230_PLUS)),
	MPP_MODE(1,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txd0",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d1",         V_MV78230_PLUS)),
	MPP_MODE(2,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txd1",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d2",         V_MV78230_PLUS)),
	MPP_MODE(3,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txd2",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d3",         V_MV78230_PLUS)),
	MPP_MODE(4,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txd3",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d4",         V_MV78230_PLUS)),
	MPP_MODE(5,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txctl",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d5",         V_MV78230_PLUS)),
	MPP_MODE(6,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxd0",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d6",         V_MV78230_PLUS)),
	MPP_MODE(7,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxd1",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d7",         V_MV78230_PLUS)),
	MPP_MODE(8,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxd2",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d8",         V_MV78230_PLUS)),
	MPP_MODE(9,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxd3",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d9",         V_MV78230_PLUS)),
	MPP_MODE(10,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxctl",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d10",        V_MV78230_PLUS)),
	MPP_MODE(11,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxclk",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d11",        V_MV78230_PLUS)),
	MPP_MODE(12,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txd4",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "txclkout",   V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d12",        V_MV78230_PLUS)),
	MPP_MODE(13,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txd5",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "txd0",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "spi1", "mosi",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d13",        V_MV78230_PLUS)),
	MPP_MODE(14,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txd6",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "txd1",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "spi1", "sck",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d14",        V_MV78230_PLUS)),
	MPP_MODE(15,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txd7",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "txd2",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d15",        V_MV78230_PLUS)),
	MPP_MODE(16,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "txclk",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "txd3",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "spi1", "cs0",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d16",        V_MV78230_PLUS)),
	MPP_MODE(17,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "col",        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "txctl",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "spi1", "miso",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d17",        V_MV78230_PLUS)),
	MPP_MODE(18,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxerr",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "rxd0",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "ptp", "trig",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d18",        V_MV78230_PLUS)),
	MPP_MODE(19,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "crs",        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "rxd1",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "ptp", "evreq",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d19",        V_MV78230_PLUS)),
	MPP_MODE(20,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxd4",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "rxd2",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "ptp", "clk",        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d20",        V_MV78230_PLUS)),
	MPP_MODE(21,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxd5",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "rxd3",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "dram", "bat",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d21",        V_MV78230_PLUS)),
	MPP_MODE(22,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxd6",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "rxctl",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "sata0", "prsnt",    V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d22",        V_MV78230_PLUS)),
	MPP_MODE(23,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ge0", "rxd7",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "ge1", "rxclk",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "sata1", "prsnt",    V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "d23",        V_MV78230_PLUS)),
	MPP_MODE(24,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "sata1", "prsnt",    V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "rst",        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "hsync",      V_MV78230_PLUS)),
	MPP_MODE(25,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "sata0", "prsnt",    V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "pclk",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "vsync",      V_MV78230_PLUS)),
	MPP_MODE(26,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "fsync",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "clk",        V_MV78230_PLUS)),
	MPP_MODE(27,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ptp", "trig",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "dtx",        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "e",          V_MV78230_PLUS)),
	MPP_MODE(28,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ptp", "evreq",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "drx",        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "pwm",        V_MV78230_PLUS)),
	MPP_MODE(29,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "ptp", "clk",        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "int0",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "ref-clk",    V_MV78230_PLUS)),
	MPP_MODE(30,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "sd0", "clk",        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "int1",       V_MV78230_PLUS)),
	MPP_MODE(31,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "sd0", "cmd",        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "int2",       V_MV78230_PLUS)),
	MPP_MODE(32,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "sd0", "d0",         V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "int3",       V_MV78230_PLUS)),
	MPP_MODE(33,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "sd0", "d1",         V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "int4",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "dram", "bat",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x5, "dram", "vttctrl",   V_MV78230_PLUS)),
	MPP_MODE(34,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "sd0", "d2",         V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "sata0", "prsnt",    V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "int5",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "dram", "deccerr",   V_MV78230_PLUS)),
	MPP_MODE(35,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "sd0", "d3",         V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "sata1", "prsnt",    V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "int6",       V_MV78230_PLUS)),
	MPP_MODE(36,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "spi0", "mosi",      V_MV78230_PLUS)),
	MPP_MODE(37,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "spi0", "miso",      V_MV78230_PLUS)),
	MPP_MODE(38,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "spi0", "sck",       V_MV78230_PLUS)),
	MPP_MODE(39,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "spi0", "cs0",       V_MV78230_PLUS)),
	MPP_MODE(40,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "spi0", "cs1",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "uart2", "cts",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "vga-hsync",  V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x5, "pcie", "clkreq0",   V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x6, "spi1", "cs1",       V_MV78230_PLUS)),
	MPP_MODE(41,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "spi0", "cs2",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "uart2", "rts",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "sata1", "prsnt",    V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "lcd", "vga-vsync",  V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x5, "pcie", "clkreq1",   V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x6, "spi1", "cs2",       V_MV78230_PLUS)),
	MPP_MODE(42,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "uart2", "rxd",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "uart0", "cts",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "tdm", "int7",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "tdm", "timer",      V_MV78230_PLUS)),
	MPP_MODE(43,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "uart2", "txd",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "uart0", "rts",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "spi0", "cs3",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "pcie", "rstout",    V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x6, "spi1", "cs3",       V_MV78230_PLUS)),
	MPP_MODE(44,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "uart2", "cts",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "uart3", "rxd",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "spi0", "cs4",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "dram", "bat",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x5, "pcie", "clkreq2",   V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x6, "spi1", "cs4",       V_MV78230_PLUS)),
	MPP_MODE(45,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "uart2", "rts",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "uart3", "txd",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "spi0", "cs5",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "sata1", "prsnt",    V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x5, "dram", "vttctrl",   V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x6, "spi1", "cs5",       V_MV78230_PLUS)),
	MPP_MODE(46,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "uart3", "rts",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "uart1", "rts",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "spi0", "cs6",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "sata0", "prsnt",    V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x6, "spi1", "cs6",       V_MV78230_PLUS)),
	MPP_MODE(47,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "uart3", "cts",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "uart1", "cts",      V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "spi0", "cs7",       V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x4, "ref", "clkout",     V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x5, "pcie", "clkreq3",   V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x6, "spi1", "cs7",       V_MV78230_PLUS)),
	MPP_MODE(48,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "clkout",     V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x2, "dev", "burst/last", V_MV78230_PLUS),
		 MPP_VAR_FUNCTION(0x3, "nand", "rb",        V_MV78230_PLUS)),
	MPP_MODE(49,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "we3",        V_MV78260_PLUS)),
	MPP_MODE(50,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "we2",        V_MV78260_PLUS)),
	MPP_MODE(51,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad16",       V_MV78260_PLUS)),
	MPP_MODE(52,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad17",       V_MV78260_PLUS)),
	MPP_MODE(53,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad18",       V_MV78260_PLUS)),
	MPP_MODE(54,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad19",       V_MV78260_PLUS)),
	MPP_MODE(55,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad20",       V_MV78260_PLUS)),
	MPP_MODE(56,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad21",       V_MV78260_PLUS)),
	MPP_MODE(57,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad22",       V_MV78260_PLUS)),
	MPP_MODE(58,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad23",       V_MV78260_PLUS)),
	MPP_MODE(59,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad24",       V_MV78260_PLUS)),
	MPP_MODE(60,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad25",       V_MV78260_PLUS)),
	MPP_MODE(61,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad26",       V_MV78260_PLUS)),
	MPP_MODE(62,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad27",       V_MV78260_PLUS)),
	MPP_MODE(63,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad28",       V_MV78260_PLUS)),
	MPP_MODE(64,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad29",       V_MV78260_PLUS)),
	MPP_MODE(65,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad30",       V_MV78260_PLUS)),
	MPP_MODE(66,
		 MPP_VAR_FUNCTION(0x0, "gpio", NULL,        V_MV78260_PLUS),
		 MPP_VAR_FUNCTION(0x1, "dev", "ad31",       V_MV78260_PLUS)),
};

static struct mvebu_pinctrl_soc_info armada_xp_pinctrl_info;

static const struct of_device_id armada_xp_pinctrl_of_match[] = {
	{
		.compatible = "marvell,mv78230-pinctrl",
		.data       = (void *) V_MV78230,
	},
	{
		.compatible = "marvell,mv78260-pinctrl",
		.data       = (void *) V_MV78260,
	},
	{
		.compatible = "marvell,mv78460-pinctrl",
		.data       = (void *) V_MV78460,
	},
	{ },
};

static struct mvebu_mpp_ctrl mv78230_mpp_controls[] = {
	MPP_FUNC_CTRL(0, 48, NULL, armada_xp_mpp_ctrl),
};

static struct pinctrl_gpio_range mv78230_mpp_gpio_ranges[] = {
	MPP_GPIO_RANGE(0,   0,  0, 32),
	MPP_GPIO_RANGE(1,  32, 32, 17),
};

static struct mvebu_mpp_ctrl mv78260_mpp_controls[] = {
	MPP_FUNC_CTRL(0, 66, NULL, armada_xp_mpp_ctrl),
};

static struct pinctrl_gpio_range mv78260_mpp_gpio_ranges[] = {
	MPP_GPIO_RANGE(0,   0,  0, 32),
	MPP_GPIO_RANGE(1,  32, 32, 32),
	MPP_GPIO_RANGE(2,  64, 64,  3),
};

static struct mvebu_mpp_ctrl mv78460_mpp_controls[] = {
	MPP_FUNC_CTRL(0, 66, NULL, armada_xp_mpp_ctrl),
};

static struct pinctrl_gpio_range mv78460_mpp_gpio_ranges[] = {
	MPP_GPIO_RANGE(0,   0,  0, 32),
	MPP_GPIO_RANGE(1,  32, 32, 32),
	MPP_GPIO_RANGE(2,  64, 64,  3),
};

static int armada_xp_pinctrl_suspend(struct platform_device *pdev,
				     pm_message_t state)
{
	struct mvebu_pinctrl_soc_info *soc =
		platform_get_drvdata(pdev);
	int i, nregs;

	nregs = DIV_ROUND_UP(soc->nmodes, MVEBU_MPPS_PER_REG);

	for (i = 0; i < nregs; i++)
		mpp_saved_regs[i] = readl(mpp_base + i * 4);

	return 0;
}

static int armada_xp_pinctrl_resume(struct platform_device *pdev)
{
	struct mvebu_pinctrl_soc_info *soc =
		platform_get_drvdata(pdev);
	int i, nregs;

	nregs = DIV_ROUND_UP(soc->nmodes, MVEBU_MPPS_PER_REG);

	for (i = 0; i < nregs; i++)
		writel(mpp_saved_regs[i], mpp_base + i * 4);

	return 0;
}

static int armada_xp_pinctrl_probe(struct platform_device *pdev)
{
	struct mvebu_pinctrl_soc_info *soc = &armada_xp_pinctrl_info;
	const struct of_device_id *match =
		of_match_device(armada_xp_pinctrl_of_match, &pdev->dev);
	struct resource *res;
	int nregs;

	if (!match)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mpp_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mpp_base))
		return PTR_ERR(mpp_base);

	soc->variant = (unsigned) match->data & 0xff;

	switch (soc->variant) {
	case V_MV78230:
		soc->controls = mv78230_mpp_controls;
		soc->ncontrols = ARRAY_SIZE(mv78230_mpp_controls);
		soc->modes = armada_xp_mpp_modes;
		/* We don't necessarily want the full list of the
		 * armada_xp_mpp_modes, but only the first 'n' ones
		 * that are available on this SoC */
		soc->nmodes = mv78230_mpp_controls[0].npins;
		soc->gpioranges = mv78230_mpp_gpio_ranges;
		soc->ngpioranges = ARRAY_SIZE(mv78230_mpp_gpio_ranges);
		break;
	case V_MV78260:
		soc->controls = mv78260_mpp_controls;
		soc->ncontrols = ARRAY_SIZE(mv78260_mpp_controls);
		soc->modes = armada_xp_mpp_modes;
		/* We don't necessarily want the full list of the
		 * armada_xp_mpp_modes, but only the first 'n' ones
		 * that are available on this SoC */
		soc->nmodes = mv78260_mpp_controls[0].npins;
		soc->gpioranges = mv78260_mpp_gpio_ranges;
		soc->ngpioranges = ARRAY_SIZE(mv78260_mpp_gpio_ranges);
		break;
	case V_MV78460:
		soc->controls = mv78460_mpp_controls;
		soc->ncontrols = ARRAY_SIZE(mv78460_mpp_controls);
		soc->modes = armada_xp_mpp_modes;
		/* We don't necessarily want the full list of the
		 * armada_xp_mpp_modes, but only the first 'n' ones
		 * that are available on this SoC */
		soc->nmodes = mv78460_mpp_controls[0].npins;
		soc->gpioranges = mv78460_mpp_gpio_ranges;
		soc->ngpioranges = ARRAY_SIZE(mv78460_mpp_gpio_ranges);
		break;
	}

	nregs = DIV_ROUND_UP(soc->nmodes, MVEBU_MPPS_PER_REG);

	mpp_saved_regs = devm_kmalloc(&pdev->dev, nregs * sizeof(u32),
				      GFP_KERNEL);
	if (!mpp_saved_regs)
		return -ENOMEM;

	pdev->dev.platform_data = soc;

	return mvebu_pinctrl_probe(pdev);
}

static struct platform_driver armada_xp_pinctrl_driver = {
	.driver = {
		.name = "armada-xp-pinctrl",
		.of_match_table = armada_xp_pinctrl_of_match,
	},
	.probe = armada_xp_pinctrl_probe,
	.suspend = armada_xp_pinctrl_suspend,
	.resume = armada_xp_pinctrl_resume,
};

module_platform_driver(armada_xp_pinctrl_driver);

MODULE_AUTHOR("Thomas Petazzoni <thomas.petazzoni@free-electrons.com>");
MODULE_DESCRIPTION("Marvell Armada XP pinctrl driver");
MODULE_LICENSE("GPL v2");
