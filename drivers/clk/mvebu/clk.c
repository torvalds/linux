/*
 * Marvell EBU SoC clock handling.
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/clk/mvebu.h>
#include <linux/of.h>
#include "clk-core.h"
#include "clk-cpu.h"

void __init mvebu_clocks_init(void)
{
	mvebu_core_clk_init();
	mvebu_cpu_clk_init();
}
