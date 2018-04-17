/*
 * Copyright (C) 2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk-iproc.h"

static void __init hr2_armpll_init(struct device_node *node)
{
	iproc_armpll_setup(node);
}
CLK_OF_DECLARE(hr2_armpll, "brcm,hr2-armpll", hr2_armpll_init);
