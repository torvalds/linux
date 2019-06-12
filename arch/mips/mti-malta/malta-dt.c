// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */

#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>

void __init device_tree_init(void)
{
	unflatten_and_copy_device_tree();
}
