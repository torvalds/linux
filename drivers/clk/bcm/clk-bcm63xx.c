// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2015 Broadcom Corporation
#include <linux/init.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include "clk-iproc.h"

static void __init bcm63138_armpll_init(struct device_node *node)
{
	iproc_armpll_setup(node);
}
CLK_OF_DECLARE(bcm63138_armpll, "brcm,bcm63138-armpll", bcm63138_armpll_init);
